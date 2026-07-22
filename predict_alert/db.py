import configparser
import json
from pathlib import Path

import pymysql


def load_mysql_config(config_path: str) -> dict:
    path = Path(config_path)
    if not path.is_file():
        raise FileNotFoundError(f"找不到数据库配置: {path.resolve()}")

    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    section = parser["MySQL"] if parser.has_section("MySQL") else parser["mysql"]
    return {
        "host": section.get("host", "127.0.0.1"),
        "port": int(section.get("port", 3306)),
        "user": section.get("username", section.get("user", "root")),
        "password": section.get("password", ""),
        "database": section.get("database", "sensor_db"),
        "charset": section.get("charset", "utf8mb4"),
    }


def connect_mysql(mysql_cfg: dict):
    return pymysql.connect(
        host=mysql_cfg["host"],
        port=mysql_cfg["port"],
        user=mysql_cfg["user"],
        password=mysql_cfg["password"],
        database=mysql_cfg["database"],
        charset=mysql_cfg["charset"],
        cursorclass=pymysql.cursors.DictCursor,
        autocommit=True,
    )


def ensure_alert_table(conn) -> None:
    sql = """
    CREATE TABLE IF NOT EXISTS ai_alert (
        id BIGINT PRIMARY KEY AUTO_INCREMENT,
        alert_time DATETIME NOT NULL,
        alert_type VARCHAR(32) NOT NULL,
        severity VARCHAR(16) NOT NULL,
        target_metric VARCHAR(64) NOT NULL,
        target_label VARCHAR(64) NOT NULL,
        current_value DOUBLE NULL,
        predicted_value DOUBLE NULL,
        risk_score DOUBLE NULL,
        message TEXT NOT NULL,
        evidence_json JSON NULL,
        status VARCHAR(16) NOT NULL DEFAULT 'open',
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        INDEX idx_alert_time (alert_time),
        INDEX idx_status (status),
        INDEX idx_severity (severity)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='AI预测预警表';
    """
    with conn.cursor() as cur:
        cur.execute(sql)


def _table_columns(conn, table_name: str) -> set:
    with conn.cursor() as cur:
        cur.execute(f"SHOW COLUMNS FROM {table_name}")
        return {row["Field"] for row in cur.fetchall()}


def fetch_qualified_rate_rows(conn, lookback_hours: int) -> list:
    columns = _table_columns(conn, "qualified_rate")

    if "time" in columns and "w_qualified_rate" in columns:
        sql = """
        SELECT
            time,
            w_qualified_rate,
            t_qualified_rate,
            e_qualified_rate,
            avg_temperature,
            avg_humidity,
            avg_cleanliness
        FROM qualified_rate
        WHERE time >= DATE_SUB(NOW(), INTERVAL %s HOUR)
        ORDER BY time ASC
        """
    elif "record_time" in columns and "device_type" in columns:
        # 兼容旧版表结构：每种设备类型一行，按时间汇总成宽表
        sql = """
        SELECT
            record_time AS time,
            MAX(CASE WHEN device_type IN ('腕带', 'W', 'w') THEN qualified_rate END) AS w_qualified_rate,
            MAX(CASE WHEN device_type IN ('台垫', 'T', 't') THEN qualified_rate END) AS t_qualified_rate,
            MAX(CASE WHEN device_type IN ('设备', 'E', 'e') THEN qualified_rate END) AS e_qualified_rate,
            MAX(avg_temperature) AS avg_temperature,
            MAX(avg_humidity) AS avg_humidity,
            MAX(avg_cleanliness) AS avg_cleanliness
        FROM qualified_rate
        WHERE record_time >= DATE_SUB(NOW(), INTERVAL %s HOUR)
        GROUP BY record_time
        ORDER BY record_time ASC
        """
    else:
        raise RuntimeError("qualified_rate 表结构无法识别，请检查数据库字段")

    with conn.cursor() as cur:
        cur.execute(sql, (lookback_hours,))
        return list(cur.fetchall())


def insert_alert(conn, alert: dict) -> None:
    sql = """
    INSERT INTO ai_alert (
        alert_time, alert_type, severity, target_metric, target_label,
        current_value, predicted_value, risk_score, message, evidence_json, status
    ) VALUES (
        NOW(), %(alert_type)s, %(severity)s, %(target_metric)s, %(target_label)s,
        %(current_value)s, %(predicted_value)s, %(risk_score)s, %(message)s, %(evidence_json)s, 'open'
    )
    """
    with conn.cursor() as cur:
        cur.execute(sql, alert)


def has_recent_duplicate(conn, message: str, minutes: int = 30) -> bool:
    sql = """
    SELECT COUNT(*) AS cnt
    FROM ai_alert
    WHERE message = %s
      AND alert_time >= DATE_SUB(NOW(), INTERVAL %s MINUTE)
      AND status = 'open'
    """
    with conn.cursor() as cur:
        cur.execute(sql, (message, minutes))
        row = cur.fetchone()
        return bool(row and row["cnt"] > 0)


def has_recent_ai_analysis(conn, minutes: int = 60) -> bool:
    sql = """
    SELECT COUNT(*) AS cnt
    FROM ai_alert
    WHERE alert_type = 'ai_analysis'
      AND alert_time >= DATE_SUB(NOW(), INTERVAL %s MINUTE)
      AND status = 'open'
    """
    with conn.cursor() as cur:
        cur.execute(sql, (minutes,))
        row = cur.fetchone()
        return bool(row and row["cnt"] > 0)
