#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import json
import sys
import time
import traceback
from datetime import datetime
from pathlib import Path

from ai_analyzer import analyze_with_ai
from db import (
    connect_mysql,
    ensure_alert_table,
    fetch_qualified_rate_rows,
    has_recent_ai_analysis,
    has_recent_duplicate,
    insert_alert,
    load_mysql_config,
)
from predictor import build_features, evaluate_rules

_LOG_FILE: Path | None = None


def setup_logging(log_file: Path | None) -> None:
    global _LOG_FILE
    _LOG_FILE = log_file
    if log_file:
        log_file.parent.mkdir(parents=True, exist_ok=True)


def log(message: str) -> None:
    line = f"[{datetime.now():%Y-%m-%d %H:%M:%S}] {message}"
    print(line, flush=True)
    if _LOG_FILE:
        with _LOG_FILE.open("a", encoding="utf-8") as fp:
            fp.write(line + "\n")


def load_app_config() -> dict:
    config_path = Path(__file__).with_name("config.json")
    with config_path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_ai_secrets(app_cfg: dict) -> dict | None:
    ai_cfg = app_cfg.get("ai", {})
    if ai_cfg.get("api_key"):
        return {"api_key": ai_cfg["api_key"]}

    secrets_name = ai_cfg.get("secrets_file", "ai_secrets.json")
    secrets_path = Path(__file__).parent / secrets_name
    if not secrets_path.is_file():
        return None

    with secrets_path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    api_key = (data.get("api_key") or "").strip()
    if not api_key or api_key.startswith("在这里填写"):
        return None
    return {"api_key": api_key}


def resolve_mysql_config_path(app_cfg: dict) -> Path:
    raw = app_cfg.get("mysql_config_path", "../mysql_config.ini")
    path = Path(raw)
    if not path.is_file():
        path = Path(__file__).parent / raw
    if not path.is_file():
        path = Path(__file__).parent.parent / "mysql_config.ini"
    return path


def run_rule_alert(conn, app_cfg: dict, rows: list) -> int:
    rules = app_cfg["rules"]
    horizon_hours = app_cfg["predict_horizon_hours"]
    target_cfg = dict(app_cfg["targets"]["wristband_rate"])
    target_cfg["horizon_hours"] = horizon_hours

    features = build_features(rows, target_cfg["column"], horizon_hours)
    if features is None:
        log("规则模式：无法构建特征，跳过。")
        return 0

    alert = evaluate_rules(features, rules, target_cfg)
    if alert is None:
        log("规则模式：未触发预警。")
        return 0

    alert["evidence_json"] = json.dumps(alert["evidence_json"], ensure_ascii=False)
    if has_recent_duplicate(conn, alert["message"], minutes=30):
        log("规则模式：30 分钟内已有相同预警，跳过。")
        return 0

    insert_alert(conn, alert)
    log(f"规则预警 [{alert['severity']}] {alert['message']}")
    return 1


def run_ai_alert(conn, app_cfg: dict, rows: list, secrets: dict | None) -> int:
    ai_cfg = app_cfg.get("ai", {})
    duplicate_minutes = int(ai_cfg.get("duplicate_minutes", 60))
    provider = ai_cfg.get("provider", "ollama")
    model = ai_cfg.get("model", "")

    if has_recent_ai_analysis(conn, duplicate_minutes):
        log(f"AI 分析：{duplicate_minutes} 分钟内已有报告，跳过。")
        return 0

    log(f"正在调用 AI 分析（{provider} / {model}）...")
    alert = analyze_with_ai(rows, app_cfg, secrets=secrets)
    alert["evidence_json"] = json.dumps(alert["evidence_json"], ensure_ascii=False)
    insert_alert(conn, alert)
    log(f"AI 分析完成 [{alert['severity']}]")
    log(alert["message"].replace("\n", " | "))
    return 1


def run_once(app_cfg: dict) -> int:
    mysql_path = resolve_mysql_config_path(app_cfg)
    mysql_cfg = load_mysql_config(str(mysql_path))
    lookback_hours = app_cfg["lookback_hours"]
    min_samples = app_cfg["min_samples"]
    ai_cfg = app_cfg.get("ai", {})
    ai_enabled = bool(ai_cfg.get("enabled"))
    rules_fallback = bool(ai_cfg.get("rules_fallback"))

    log(f"连接数据库 {mysql_cfg['host']}:{mysql_cfg['port']}/{mysql_cfg['database']}")
    conn = connect_mysql(mysql_cfg)
    ensure_alert_table(conn)

    rows = fetch_qualified_rate_rows(conn, lookback_hours)
    log(f"读取 qualified_rate 记录数: {len(rows)}")
    if len(rows) < min_samples:
        log(f"样本不足（需要至少 {min_samples} 条），本次跳过。")
        conn.close()
        return 0

    result = 0
    if ai_enabled:
        secrets = load_ai_secrets(app_cfg)
        if ai_cfg.get("provider", "").lower() != "ollama" and not secrets and not ai_cfg.get("api_key"):
            log("AI 已启用但未配置 API Key，请编辑 ai_secrets.json")
            conn.close()
            return 2
        try:
            result = run_ai_alert(conn, app_cfg, rows, secrets)
        except Exception as exc:
            log(f"AI 分析失败: {exc}")
            if not rules_fallback:
                conn.close()
                return 2
            log("回退到规则模式...")
            result = run_rule_alert(conn, app_cfg, rows)
    elif rules_fallback:
        result = run_rule_alert(conn, app_cfg, rows)
    else:
        log("AI 未启用，且 rules_fallback=false，本次跳过。")

    conn.close()
    return result


def run_ai_test(app_cfg: dict) -> int:
    ai_cfg = app_cfg.get("ai", {})
    if not ai_cfg.get("enabled", True):
        print("请先在 config.json 里设置 ai.enabled = true")
        return 2

    from ai_client import resolve_ai_config, test_connection

    secrets = load_ai_secrets(app_cfg)
    provider = (ai_cfg.get("provider") or "ollama").lower()
    model = ai_cfg.get("model", "")
    resolved = resolve_ai_config(ai_cfg, secrets)

    if provider != "ollama" and not resolved["api_key"]:
        print("连接失败: 未配置 API Key")
        print()
        print("云端免费对接步骤：")
        print("  1. 双击 setup_cloud.bat")
        print("  2. 或复制 ai_secrets.example.json 为 ai_secrets.json 并填入密钥")
        print("  3. 硅基流动注册: https://cloud.siliconflow.cn/account/ak")
        return 2

    print(f"测试 AI 连接: {provider} / {model}")
    try:
        reply = test_connection(ai_cfg, secrets=secrets)
        print(f"连接成功，AI 回复: {reply}")
        return 0
    except Exception as exc:
        print(f"连接失败: {exc}")
        if provider == "siliconflow":
            print()
            print("提示: 硅基流动免费模型需实名认证；密钥在 https://cloud.siliconflow.cn/account/ak 创建")
        return 2


def main() -> int:
    parser = argparse.ArgumentParser(description="ESD 预测预警 / AI 分析服务")
    parser.add_argument("--once", action="store_true", help="只运行一次")
    parser.add_argument("--loop", action="store_true", help="按配置间隔循环运行")
    parser.add_argument("--service", action="store_true", help="后台服务模式（写日志文件）")
    parser.add_argument("--test-ai", action="store_true", help="只测试 AI 接口连接")
    args = parser.parse_args()

    app_cfg = load_app_config()

    if args.test_ai:
        return run_ai_test(app_cfg)

    if args.service:
        args.loop = True
    if not args.once and not args.loop:
        args.once = True

    if args.loop or args.service:
        log_path = Path(__file__).parent / "logs" / "predict_alert.log"
        setup_logging(log_path)
        log("预测预警服务启动")

    if args.loop:
        interval = int(app_cfg.get("poll_interval_seconds", 300))
        log(f"循环模式，间隔 {interval} 秒")
        while True:
            try:
                run_once(app_cfg)
            except Exception as exc:
                log(f"运行失败: {exc}")
                if _LOG_FILE:
                    with _LOG_FILE.open("a", encoding="utf-8") as fp:
                        fp.write(traceback.format_exc() + "\n")
            time.sleep(interval)
    else:
        try:
            return run_once(app_cfg)
        except Exception as exc:
            print(f"运行失败: {exc}", file=sys.stderr)
            return 2


if __name__ == "__main__":
    raise SystemExit(main())
