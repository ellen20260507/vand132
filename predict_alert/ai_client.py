from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any


PROVIDER_PRESETS: dict[str, dict[str, str]] = {
    "ollama": {
        "api_base": "http://127.0.0.1:11434/v1",
        "model": "qwen2.5:7b",
        "api_key": "",
    },
    "deepseek": {
        "api_base": "https://api.deepseek.com/v1",
        "model": "deepseek-chat",
    },
    "siliconflow": {
        "api_base": "https://api.siliconflow.cn/v1",
        "model": "Qwen/Qwen2.5-7B-Instruct",
    },
    "gemini_openai": {
        "api_base": "https://generativelanguage.googleapis.com/v1beta/openai",
        "model": "gemini-2.0-flash",
    },
}


def resolve_ai_config(ai_cfg: dict, secrets: dict | None = None) -> dict:
    provider = (ai_cfg.get("provider") or "ollama").lower()
    preset = PROVIDER_PRESETS.get(provider, {})
    api_base = ai_cfg.get("api_base") or preset.get("api_base", "")
    model = ai_cfg.get("model") or preset.get("model", "")
    api_key = ai_cfg.get("api_key") or preset.get("api_key", "")
    if secrets and secrets.get("api_key"):
        api_key = secrets["api_key"]
    return {
        "provider": provider,
        "api_base": api_base.rstrip("/"),
        "model": model,
        "api_key": api_key,
        "timeout_seconds": int(ai_cfg.get("timeout_seconds", 120)),
    }


def chat_completion(
    ai_cfg: dict,
    messages: list[dict[str, str]],
    secrets: dict | None = None,
    temperature: float = 0.2,
) -> str:
    resolved = resolve_ai_config(ai_cfg, secrets)
    if not resolved["api_base"] or not resolved["model"]:
        raise RuntimeError("AI 配置不完整，请检查 api_base 和 model")

    url = f"{resolved['api_base']}/chat/completions"
    payload: dict[str, Any] = {
        "model": resolved["model"],
        "messages": messages,
        "temperature": temperature,
        "stream": False,
    }

    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if resolved["api_key"]:
        headers["Authorization"] = f"Bearer {resolved['api_key']}"

    request = urllib.request.Request(url, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=resolved["timeout_seconds"]) as response:
            body = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"AI 接口 HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"AI 接口连接失败: {exc.reason}") from exc

    try:
        return body["choices"][0]["message"]["content"].strip()
    except (KeyError, IndexError, TypeError) as exc:
        raise RuntimeError(f"AI 返回格式异常: {body}") from exc


def test_connection(ai_cfg: dict, secrets: dict | None = None) -> str:
    reply = chat_completion(
        ai_cfg,
        [
            {"role": "system", "content": "你是测试助手。"},
            {"role": "user", "content": "请只回复：连接成功"},
        ],
        secrets=secrets,
        temperature=0,
    )
    return reply
