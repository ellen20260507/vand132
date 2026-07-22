from __future__ import annotations

from dataclasses import dataclass
from statistics import mean


@dataclass
class FeatureSnapshot:
    current_w: float
    current_t: float
    current_e: float
    current_temp: float | None
    current_humidity: float | None
    current_cleanliness: float | None
    w_trend_per_hour: float
    predicted_w: float


def _safe_mean(values: list[float | None]) -> float | None:
    nums = [v for v in values if v is not None]
    if not nums:
        return None
    return float(mean(nums))


def _linear_trend_per_hour(values: list[float], hours: list[float]) -> float:
    if len(values) < 2:
        return 0.0

    n = len(values)
    x_mean = sum(hours) / n
    y_mean = sum(values) / n
    numerator = sum((hours[i] - x_mean) * (values[i] - y_mean) for i in range(n))
    denominator = sum((hours[i] - x_mean) ** 2 for i in range(n))
    if denominator == 0:
        return 0.0
    return numerator / denominator


def build_features(rows: list[dict], target_column: str, horizon_hours: float) -> FeatureSnapshot | None:
    if not rows:
        return None

    w_values: list[float] = []
    hours: list[float] = []
    base_time = rows[0]["time"]

    for row in rows:
        value = row.get(target_column)
        if value is None:
            continue
        delta_hours = (row["time"] - base_time).total_seconds() / 3600.0
        hours.append(delta_hours)
        w_values.append(float(value))

    if not w_values:
        return None

    trend = _linear_trend_per_hour(w_values, hours)
    current_w = w_values[-1]
    predicted_w = current_w + trend * horizon_hours

    latest = rows[-1]
    return FeatureSnapshot(
        current_w=current_w,
        current_t=float(latest.get("t_qualified_rate") or 0.0),
        current_e=float(latest.get("e_qualified_rate") or 0.0),
        current_temp=latest.get("avg_temperature"),
        current_humidity=latest.get("avg_humidity"),
        current_cleanliness=latest.get("avg_cleanliness"),
        w_trend_per_hour=trend,
        predicted_w=predicted_w,
    )


def evaluate_rules(features: FeatureSnapshot, rules: dict, target_cfg: dict) -> dict | None:
    risk_score = 0.0
    reasons: list[str] = []

    humidity = features.current_humidity
    if humidity is not None and humidity >= rules["humidity_high"]:
        risk_score += rules["humidity_weight"]
        reasons.append(f"湿度偏高({humidity:.1f}%>={rules['humidity_high']:.1f}%)")

    temp = features.current_temp
    if temp is not None and temp >= rules["temp_high"]:
        risk_score += rules["temp_weight"]
        reasons.append(f"温度偏高({temp:.1f}℃>={rules['temp_high']:.1f}℃)")

    cleanliness = features.current_cleanliness
    if cleanliness is not None and cleanliness <= rules["cleanliness_low"]:
        risk_score += rules["cleanliness_weight"]
        reasons.append(f"洁净度偏低({cleanliness:.1f}<={rules['cleanliness_low']:.1f})")

    peer_avg = _safe_mean([features.current_t, features.current_e])
    if peer_avg is not None and peer_avg <= rules["peer_rate_low"]:
        risk_score += rules["peer_rate_weight"]
        reasons.append(f"台垫/设备合格率偏低({peer_avg:.1f}%<={rules['peer_rate_low']:.1f}%)")

    if features.w_trend_per_hour <= -rules["trend_drop_per_hour"]:
        risk_score += rules["trend_weight"]
        reasons.append(f"腕带合格率下降趋势({features.w_trend_per_hour:.2f}%/小时)")

    warn_threshold = target_cfg["warn_threshold"]
    critical_threshold = target_cfg["critical_threshold"]
    predicted = features.predicted_w

    severity = None
    if predicted < critical_threshold or risk_score >= rules["risk_score_critical"]:
        severity = "critical"
    elif predicted < warn_threshold or risk_score >= rules["risk_score_warn"]:
        severity = "warning"

    if severity is None:
        return None

    label = target_cfg["label"]
    message = (
        f"{label}预测预警：当前{features.current_w:.1f}%，"
        f"预计{target_cfg.get('horizon_hours', 4)}小时后约{predicted:.1f}%。"
        f"风险分={risk_score:.0f}。依据：" + "；".join(reasons or ["综合指标偏离正常"])
    )

    return {
        "alert_type": "predict",
        "severity": severity,
        "target_metric": target_cfg["column"],
        "target_label": label,
        "current_value": features.current_w,
        "predicted_value": predicted,
        "risk_score": risk_score,
        "message": message,
        "evidence_json": {
            "current_w": features.current_w,
            "current_t": features.current_t,
            "current_e": features.current_e,
            "current_temp": features.current_temp,
            "current_humidity": features.current_humidity,
            "current_cleanliness": features.current_cleanliness,
            "w_trend_per_hour": features.w_trend_per_hour,
            "predicted_w": predicted,
            "reasons": reasons,
            "risk_score": risk_score,
        },
    }
