from __future__ import annotations

import json
import re
from datetime import datetime
from statistics import mean
from typing import Any

from ai_client import chat_completion
from predictor import build_features


SYSTEM_PROMPT = """你是 ESD 静电监控系统的数据分析专家。
你会根据历史合格率与环境数据，给出专业、简洁、可执行的分析。
请严格输出 JSON，不要输出 Markdown 代码块，格式如下：
{
  "severity": "info 或 warning 或 critical",
  "summary": "一句话结论",
  "analysis": "详细分析，2-5句",
  "risks": ["风险1", "风险2"],
  "suggestions": ["建议1", "建议2"]
}
severity 判定参考：
- critical: 腕带合格率已很低或预计很快跌破 80%，或多指标严重异常
- warning: 有明显下降趋势或环境指标不利
- info: 整体正常，仅需例行关注
"""


def _safe_float(value: Any) -> float | None:
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _series_stats(values: list[float]) -> dict:
    if not values:
        return {"count": 0}
    return {
        "count": len(values),
        "latest": values[-1],
        "avg": round(mean(values), 2),
        "min": round(min(values), 2),
        "max": round(max(values), 2),
    }


def build_analysis_payload(rows: list[dict], lookback_hours: int, horizon_hours: int) -> dict:
    w_vals = [_safe_float(r.get("w_qualified_rate")) for r in rows]
    t_vals = [_safe_float(r.get("t_qualified_rate")) for r in rows]
    e_vals = [_safe_float(r.get("e_qualified_rate")) for r in rows]
    temp_vals = [_safe_float(r.get("avg_temperature")) for r in rows]
    hum_vals = [_safe_float(r.get("avg_humidity")) for r in rows]
    clean_vals = [_safe_float(r.get("avg_cleanliness")) for r in rows]

    w_vals = [v for v in w_vals if v is not None]
    t_vals = [v for v in t_vals if v is not None]
    e_vals = [v for v in e_vals if v is not None]
    temp_vals = [v for v in temp_vals if v is not None]
    hum_vals = [v for v in hum_vals if v is not None]
    clean_vals = [v for v in clean_vals if v is not None]

    features = build_features(rows, "w_qualified_rate", horizon_hours)
    latest_time = rows[-1].get("time")
    if isinstance(latest_time, datetime):
        latest_time = latest_time.strftime("%Y-%m-%d %H:%M:%S")

    payload = {
        "lookback_hours": lookback_hours,
        "predict_horizon_hours": horizon_hours,
        "latest_record_time": latest_time,
        "sample_count": len(rows),
        "wristband_rate": _series_stats(w_vals),
        "mat_rate": _series_stats(t_vals),
        "device_rate": _series_stats(e_vals),
        "temperature": _series_stats(temp_vals),
        "humidity": _series_stats(hum_vals),
        "cleanliness": _series_stats(clean_vals),
    }

    if features:
        payload["derived_features"] = {
            "current_w": round(features.current_w, 2),
            "current_t": round(features.current_t, 2),
            "current_e": round(features.current_e, 2),
            "w_trend_per_hour": round(features.w_trend_per_hour, 3),
            "predicted_w_in_horizon": round(features.predicted_w, 2),
        }
    return payload


def _extract_json(text: str) -> dict:
    text = text.strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        match = re.search(r"\{.*\}", text, flags=re.S)
        if not match:
            raise ValueError("AI 未返回有效 JSON")
        return json.loads(match.group(0))


def analyze_with_ai(
    rows: list[dict],
    app_cfg: dict,
    secrets: dict | None = None,
) -> dict:
    ai_cfg = app_cfg.get("ai", {})
    lookback_hours = app_cfg["lookback_hours"]
    horizon_hours = app_cfg["predict_horizon_hours"]
    payload = build_analysis_payload(rows, lookback_hours, horizon_hours)

    user_prompt = (
        "请分析以下 ESD 监控历史数据，并按要求返回 JSON：\n"
        f"{json.dumps(payload, ensure_ascii=False, indent=2)}"
    )

    raw_text = chat_completion(
        ai_cfg,
        [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": user_prompt},
        ],
        secrets=secrets,
        temperature=float(ai_cfg.get("temperature", 0.2)),
    )

    parsed = _extract_json(raw_text)
    severity = str(parsed.get("severity", "info")).lower()
    if severity not in {"info", "warning", "critical"}:
        severity = "info"

    summary = str(parsed.get("summary", "AI 已完成分析")).strip()
    analysis = str(parsed.get("analysis", "")).strip()
    risks = parsed.get("risks") or []
    suggestions = parsed.get("suggestions") or []

    message_parts = [f"【AI分析】{summary}"]
    if analysis:
        message_parts.append(analysis)
    if risks:
        message_parts.append("风险：" + "；".join(str(x) for x in risks))
    if suggestions:
        message_parts.append("建议：" + "；".join(str(x) for x in suggestions))

    features = build_features(rows, "w_qualified_rate", horizon_hours)
    current_w = features.current_w if features else None
    predicted_w = features.predicted_w if features else None

    return {
        "alert_type": "ai_analysis",
        "severity": severity,
        "target_metric": "w_qualified_rate",
        "target_label": "腕带合格率",
        "current_value": current_w,
        "predicted_value": predicted_w,
        "risk_score": None,
        "message": "\n".join(message_parts),
        "evidence_json": {
            "provider": ai_cfg.get("provider"),
            "model": ai_cfg.get("model"),
            "input_payload": payload,
            "ai_raw_text": raw_text,
            "ai_parsed": parsed,
        },
    }
