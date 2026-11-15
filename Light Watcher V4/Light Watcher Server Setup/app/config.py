import os
from dataclasses import dataclass, field


def _parse_keys(raw: str) -> dict[str, str]:
    keys: dict[str, str] = {}
    for part in raw.split(","):
        part = part.strip()
        if not part or "=" not in part:
            continue
        dev, key = part.split("=", 1)
        dev, key = dev.strip().upper(), key.strip()
        if dev and key:
            keys[dev] = key
    return keys


@dataclass
class Settings:
    db_path: str = "lw.sqlite3"
    device_keys: dict[str, str] = field(default_factory=dict)
    ts_window_sec: int = 300
    bot_token: str = ""
    owner_id: str = ""
    tz: str = "Europe/Kyiv"
    monitor_sec: int = 15
    yasno_sec: int = 1
    backup_dir: str = ""
    store_chat: str = ""

    @classmethod
    def from_env(cls) -> "Settings":
        return cls(
            db_path=os.environ.get("LW_DB_PATH", "lw.sqlite3"),
            device_keys=_parse_keys(os.environ.get("LW_DEVICE_KEYS", "")),
            ts_window_sec=int(os.environ.get("LW_TS_WINDOW_SEC", "300")),
            bot_token=os.environ.get("LW_BOT_TOKEN", "").strip(),
            owner_id=os.environ.get("LW_OWNER_ID", "").strip(),
            tz=os.environ.get("LW_TZ", "Europe/Kyiv").strip() or "Europe/Kyiv",
            monitor_sec=int(os.environ.get("LW_MONITOR_SEC", "15")),
            yasno_sec=int(os.environ.get("LW_YASNO", "1")),
            backup_dir=os.environ.get("LW_BACKUP_DIR", ""),
            store_chat=os.environ.get("LW_STORE_CHAT", "").strip(),
        )
