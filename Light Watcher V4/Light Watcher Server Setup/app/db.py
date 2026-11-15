import json
import sqlite3
import threading
import time
import zlib
from pathlib import Path

SCHEMA_VERSION = 2

SCHEMA = """
CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY,
    fw TEXT,
    last_seen REAL,
    state TEXT,
    parts INTEGER NOT NULL DEFAULT 0,
    hb_on INTEGER NOT NULL DEFAULT 30,
    hb_off INTEGER NOT NULL DEFAULT 60,
    boot TEXT,
    n INTEGER NOT NULL DEFAULT 0,
    detached INTEGER NOT NULL DEFAULT 0,
    offline_since REAL,
    snapshot TEXT,
    snapshot_at REAL,
    viewed_at REAL,
    keep TEXT NOT NULL DEFAULT '[]',
    grp TEXT
);
CREATE TABLE IF NOT EXISTS events (
    device_id TEXT NOT NULL,
    sid INTEGER NOT NULL,
    q INTEGER NOT NULL,
    kind TEXT NOT NULL,
    ep INTEGER,
    up INTEGER,
    d INTEGER,
    pw INTEGER,
    v INTEGER,
    received_at REAL NOT NULL,
    PRIMARY KEY (device_id, sid, q)
);
CREATE TABLE IF NOT EXISTS cfg_backups (
    device_id TEXT NOT NULL,
    saved_at REAL NOT NULL,
    hash TEXT NOT NULL,
    data TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS cfg_backups_dev ON cfg_backups (device_id, saved_at);
CREATE INDEX IF NOT EXISTS events_ep ON events (device_id, ep);
CREATE TABLE IF NOT EXISTS ys_days (
    grp TEXT NOT NULL,
    day TEXT NOT NULL,
    status TEXT NOT NULL,
    slots TEXT NOT NULL,
    saved_at REAL NOT NULL,
    PRIMARY KEY (grp, day)
);
"""

DEVICE_COLUMNS = {
    "parts": "INTEGER NOT NULL DEFAULT 0",
    "hb_on": "INTEGER NOT NULL DEFAULT 30",
    "hb_off": "INTEGER NOT NULL DEFAULT 60",
    "boot": "TEXT",
    "n": "INTEGER NOT NULL DEFAULT 0",
    "detached": "INTEGER NOT NULL DEFAULT 0",
    "offline_since": "REAL",
    "snapshot": "TEXT",
    "snapshot_at": "REAL",
    "viewed_at": "REAL",
    "keep": "TEXT NOT NULL DEFAULT '[]'",
    "grp": "TEXT",
}

FIELDS = ("id", "fw", "last_seen", "state", "parts", "hb_on", "hb_off", "boot", "n", "detached",
          "offline_since", "snapshot_at", "viewed_at", "keep", "grp")


class Db:
    def __init__(self, path: str):
        self._lock = threading.Lock()
        self._conn = sqlite3.connect(path, check_same_thread=False)
        self._migrate()

    def _migrate(self) -> None:
        c = self._conn
        version = c.execute("PRAGMA user_version").fetchone()[0]
        if version < 1:
            tables = {r[0] for r in c.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            if "events" in tables:
                cols = {r[1] for r in c.execute("PRAGMA table_info(events)")}
                if "sid" not in cols:
                    c.execute("ALTER TABLE events RENAME TO events_v0")
            if "devices" in tables:
                cols = {r[1] for r in c.execute("PRAGMA table_info(devices)")}
                for name, decl in DEVICE_COLUMNS.items():
                    if name not in cols:
                        c.execute(f"ALTER TABLE devices ADD COLUMN {name} {decl}")
        c.executescript(SCHEMA)
        cols = {r[1] for r in c.execute("PRAGMA table_info(devices)")}
        for name, decl in DEVICE_COLUMNS.items():
            if name not in cols:
                c.execute(f"ALTER TABLE devices ADD COLUMN {name} {decl}")
        c.execute(f"PRAGMA user_version = {SCHEMA_VERSION}")
        c.commit()

    def backup(self, folder: str, day: str, keep: int = 7) -> Path:
        d = Path(folder)
        d.mkdir(parents=True, exist_ok=True)
        target = d / f"lw-{day}.sqlite3"
        dest = sqlite3.connect(str(target))
        try:
            with self._lock:
                self._conn.backup(dest)
        finally:
            dest.close()
        for old in sorted(d.glob("lw-*.sqlite3"))[:-keep]:
            old.unlink()
        return target

    def close(self) -> None:
        with self._lock:
            self._conn.close()

    def device(self, dev: str) -> dict | None:
        with self._lock:
            row = self._conn.execute(f"SELECT {', '.join(FIELDS)} FROM devices WHERE id = ?", (dev,)).fetchone()
        if not row:
            return None
        d = dict(zip(FIELDS, row))
        d["state"] = json.loads(d["state"] or "{}")
        d["keep"] = json.loads(d["keep"] or "[]")
        return d

    def devices(self) -> list[dict]:
        with self._lock:
            ids = [r[0] for r in self._conn.execute("SELECT id FROM devices ORDER BY last_seen DESC")]
        return [d for d in (self.device(i) for i in ids) if d]

    def accept_n(self, dev: str, boot: str, n: int) -> bool:
        with self._lock:
            row = self._conn.execute("SELECT boot, n FROM devices WHERE id = ?", (dev,)).fetchone()
            if row and row[0] == boot and n <= (row[1] or 0):
                return False
            if row:
                self._conn.execute("UPDATE devices SET boot = ?, n = ? WHERE id = ?", (boot, n, dev))
            else:
                self._conn.execute("INSERT INTO devices (id, boot, n, last_seen) VALUES (?, ?, ?, ?)", (dev, boot, n, time.time()))
            self._conn.commit()
        return True

    def touch_device(self, dev: str, fw: str, state: dict, parts: int, hb_on: int, hb_off: int) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT INTO devices (id, fw, last_seen, state, parts, hb_on, hb_off, detached) VALUES (?, ?, ?, ?, ?, ?, ?, 0) "
                "ON CONFLICT(id) DO UPDATE SET fw=excluded.fw, last_seen=excluded.last_seen, state=excluded.state, "
                "parts=excluded.parts, hb_on=excluded.hb_on, hb_off=excluded.hb_off, detached=0",
                (dev, fw, time.time(), json.dumps(state, ensure_ascii=False), parts, hb_on, hb_off),
            )
            self._conn.commit()

    def set_fields(self, dev: str, **fields) -> None:
        if not fields:
            return
        cols = ", ".join(f"{k} = ?" for k in fields)
        vals = [json.dumps(v) if k == "keep" else v for k, v in fields.items()]
        with self._lock:
            self._conn.execute(f"UPDATE devices SET {cols} WHERE id = ?", (*vals, dev))
            self._conn.commit()

    def detach(self, dev: str) -> None:
        self.set_fields(dev, detached=1, parts=0, offline_since=None)

    def add_events(self, dev: str, sid: int, events: list[dict]) -> int:
        now = time.time()
        with self._lock:
            cur = self._conn.executemany(
                "INSERT OR IGNORE INTO events (device_id, sid, q, kind, ep, up, d, pw, v, received_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                [(dev, sid, e["q"], e["k"], e.get("ep"), e.get("up"), e.get("d"), e.get("pw"), e.get("v"), now) for e in events],
            )
            self._conn.commit()
            return cur.rowcount

    def last_q(self, dev: str, sid: int) -> int:
        with self._lock:
            row = self._conn.execute("SELECT MAX(q) FROM events WHERE device_id = ? AND sid = ?", (dev, sid)).fetchone()
        return int(row[0]) if row and row[0] is not None else 0

    def events(self, dev: str) -> list[dict]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT sid, q, kind, ep, up, d, pw, v FROM events WHERE device_id = ? ORDER BY received_at, sid, q", (dev,)
            ).fetchall()
        return [{"sid": r[0], "q": r[1], "k": r[2], "ep": r[3], "up": r[4], "d": r[5], "pw": r[6], "v": r[7]} for r in rows]

    def events_window(self, dev: str, since_ep: int) -> list[dict]:
        with self._lock:
            last = self._conn.execute(
                "SELECT sid, q, kind, ep, up, d, pw, v FROM events WHERE device_id = ? AND ep > 0 AND ep < ? "
                "AND kind IN ('off', 'on', 'boot') ORDER BY ep DESC, sid DESC, q DESC LIMIT 1", (dev, since_ep)).fetchall()
            rows = self._conn.execute(
                "SELECT sid, q, kind, ep, up, d, pw, v FROM events WHERE device_id = ? AND ep >= ? "
                "AND kind IN ('off', 'on', 'boot') ORDER BY ep, sid, q", (dev, since_ep)).fetchall()
        return [{"sid": r[0], "q": r[1], "k": r[2], "ep": r[3], "up": r[4], "d": r[5], "pw": r[6], "v": r[7]}
                for r in last + rows]

    def save_days(self, rows: list[tuple[str, str, str, str]], now: float, keep_from: str) -> None:
        with self._lock:
            self._conn.executemany(
                "INSERT INTO ys_days (grp, day, status, slots, saved_at) VALUES (?, ?, ?, ?, ?) "
                "ON CONFLICT(grp, day) DO UPDATE SET status=excluded.status, slots=excluded.slots, saved_at=excluded.saved_at",
                [(*r, now) for r in rows])
            self._conn.execute("DELETE FROM ys_days WHERE day < ?", (keep_from,))
            self._conn.commit()

    def days(self, grp: str, first: str, last: str) -> dict[str, dict]:
        with self._lock:
            rows = self._conn.execute("SELECT day, status, slots FROM ys_days WHERE grp = ? AND day >= ? AND day <= ?",
                                      (grp, first, last)).fetchall()
        return {r[0]: {"status": r[1], "slots": r[2]} for r in rows}

    def save_cfg(self, dev: str, data: str, keep: int = 10) -> bool:
        import hashlib
        h = hashlib.sha1(data.encode()).hexdigest()
        with self._lock:
            last = self._conn.execute(
                "SELECT hash FROM cfg_backups WHERE device_id = ? ORDER BY saved_at DESC LIMIT 1", (dev,)
            ).fetchone()
            if last and last[0] == h:
                self._conn.execute(
                    "UPDATE cfg_backups SET saved_at = ? WHERE device_id = ? AND hash = ? AND saved_at = "
                    "(SELECT MAX(saved_at) FROM cfg_backups WHERE device_id = ?)", (time.time(), dev, h, dev))
                self._conn.commit()
                return False
            self._conn.execute("INSERT INTO cfg_backups (device_id, saved_at, hash, data) VALUES (?, ?, ?, ?)",
                               (dev, time.time(), h, data))
            self._conn.execute(
                "DELETE FROM cfg_backups WHERE device_id = ? AND saved_at NOT IN "
                "(SELECT saved_at FROM cfg_backups WHERE device_id = ? ORDER BY saved_at DESC LIMIT ?)", (dev, dev, keep))
            self._conn.commit()
        return True

    def latest_cfg(self, dev: str, skip_crc: int | None = None) -> dict | None:
        with self._lock:
            own = self._conn.execute(
                "SELECT device_id, saved_at, data FROM cfg_backups WHERE device_id = ? ORDER BY saved_at DESC", (dev,)
            ).fetchall()
            other = self._conn.execute(
                "SELECT device_id, saved_at, data FROM cfg_backups WHERE device_id != ? ORDER BY saved_at DESC", (dev,)
            ).fetchall()
        for row in own + other:
            if skip_crc is None or zlib.crc32(row[2].encode()) != skip_crc:
                return {"from": row[0], "at": int(row[1]), "data": json.loads(row[2])}
        return None

    def list_cfg(self, dev: str, crc: int | None, limit: int = 8) -> dict:
        with self._lock:
            own = self._conn.execute(
                "SELECT rowid, saved_at, data FROM cfg_backups WHERE device_id = ? ORDER BY saved_at DESC", (dev,)
            ).fetchall()
            other = self._conn.execute(
                "SELECT rowid, saved_at, data FROM cfg_backups WHERE device_id != ? ORDER BY saved_at DESC", (dev,)
            ).fetchall()
        items = []
        for rows, mine in ((own, 1), (other, 0)):
            for row in rows:
                if len(items) >= limit:
                    break
                same = 1 if crc is not None and zlib.crc32(row[2].encode()) == crc else 0
                items.append({"i": row[0], "at": int(row[1]), "o": mine, "s": same})
        return {"own": len(own), "other": len(other), "items": items}

    def cfg_by_id(self, i: int) -> dict | None:
        with self._lock:
            row = self._conn.execute("SELECT device_id, saved_at, data FROM cfg_backups WHERE rowid = ?", (i,)).fetchone()
        return {"from": row[0], "at": int(row[1]), "data": json.loads(row[2])} if row else None

    def delete_cfg(self, dev: str, i: int | None) -> int:
        with self._lock:
            if i is None:
                cur = self._conn.execute("DELETE FROM cfg_backups WHERE device_id = ?", (dev,))
            else:
                cur = self._conn.execute("DELETE FROM cfg_backups WHERE rowid = ?", (i,))
            self._conn.commit()
        return cur.rowcount

    def set_snapshot(self, dev: str, snapshot: str) -> None:
        self.set_fields(dev, snapshot=snapshot, snapshot_at=time.time())

    def snapshot(self, dev: str) -> str | None:
        with self._lock:
            row = self._conn.execute("SELECT snapshot FROM devices WHERE id = ?", (dev,)).fetchone()
        return row[0] if row else None
