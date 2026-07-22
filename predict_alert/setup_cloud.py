#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).parent
SECRETS_PATH = ROOT / "ai_secrets.json"
CONFIG_PATH = ROOT / "config.json"

REGISTER_URL = "https://cloud.siliconflow.cn/account/ak"
DEFAULT_PROVIDER = "siliconflow"
DEFAULT_MODEL = "Qwen/Qwen2.5-7B-Instruct"
DEFAULT_API_BASE = "https://api.siliconflow.cn/v1"


def load_existing_key() -> str:
    if not SECRETS_PATH.is_file():
        return ""
    try:
        data = json.loads(SECRETS_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return ""
    key = (data.get("api_key") or "").strip()
    if key.startswith("sk-在这里") or key.startswith("在这里填写"):
        return ""
    return key


def save_key(api_key: str) -> None:
    SECRETS_PATH.write_text(
        json.dumps({"api_key": api_key}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def ensure_cloud_config() -> None:
    cfg = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    ai = cfg.setdefault("ai", {})
    ai["enabled"] = True
    ai["provider"] = DEFAULT_PROVIDER
    ai["api_base"] = DEFAULT_API_BASE
    ai["model"] = DEFAULT_MODEL
    ai.setdefault("secrets_file", "ai_secrets.json")
    ai.setdefault("rules_fallback", True)
    CONFIG_PATH.write_text(json.dumps(cfg, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    print("=" * 50)
    print("  ESD AI 云端免费对接（硅基流动 SiliconFlow）")
    print("=" * 50)
    print()
    print("步骤：")
    print(f"  1. 打开 {REGISTER_URL}")
    print("  2. 注册账号并完成实名认证（免费模型需要）")
    print("  3. 创建 API Key 并复制")
    print()
    print(f"默认免费模型: {DEFAULT_MODEL}")
    print()

    existing = load_existing_key()
    if existing:
        masked = existing[:8] + "..." + existing[-4:] if len(existing) > 12 else "***"
        print(f"已检测到密钥: {masked}")
        answer = input("是否覆盖？(y/N): ").strip().lower()
        if answer not in ("y", "yes"):
            api_key = existing
        else:
            api_key = input("请粘贴 API Key: ").strip()
    else:
        api_key = input("请粘贴 API Key: ").strip()

    if not api_key:
        print("未输入 API Key，已取消。")
        return 2
    if not api_key.startswith("sk-"):
        print("警告: 密钥通常以 sk- 开头，请确认是否粘贴正确。")

    save_key(api_key)
    ensure_cloud_config()
    print()
    print("已保存 ai_secrets.json，并更新 config.json 为云端模式。")
    print("正在测试连接...")
    print()

    from predict_service import load_app_config, run_ai_test

    return run_ai_test(load_app_config())


if __name__ == "__main__":
    raise SystemExit(main())
