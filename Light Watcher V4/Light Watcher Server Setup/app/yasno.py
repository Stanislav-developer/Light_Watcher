import asyncio
import hashlib
import json
import time
import urllib.request

PLANNED_URL = "https://app.yasno.ua/api/blackout-service/public/shutdowns/regions/25/dsos/902/planned-outages"
NOT_PLANNED = {"NotPlanned", "NotPlaned"}
PLANNED = {"Definite", "Possible"}
NOT_PUBLISHED = "Ще не опубліковано"


def _int(v) -> int | None:
    if isinstance(v, bool):
        return None
    if isinstance(v, int):
        return v
    if isinstance(v, float) and v.is_integer():
        return int(v)
    return None


def slots_compact(slots) -> str | None:
    if not isinstance(slots, list):
        return None
    out = []
    for slot in slots:
        if not isinstance(slot, dict):
            return None
        kind = slot.get("type") if isinstance(slot.get("type"), str) else ""
        if kind in NOT_PLANNED:
            continue
        if kind not in PLANNED:
            return None
        start, end = _int(slot.get("start")), _int(slot.get("end"))
        if start is None or end is None or start < 0 or end <= start or end > 1440:
            return None
        out.append(f'{{"s":{start},"e":{end},"t":"{kind}"}}')
    return "[" + ",".join(out) + "]"


def _str(v) -> str:
    return v if isinstance(v, str) else ""


def group_schedule(doc: dict, group: str) -> dict | None:
    g = doc.get(group) if isinstance(doc, dict) else None
    if not isinstance(g, dict):
        return None
    td = g.get("today")
    if not isinstance(td, dict) or not isinstance(td.get("slots"), list):
        return None
    today_date = _str(td.get("date"))
    if not today_date:
        return None
    today = slots_compact(td["slots"])
    if today is None:
        return None
    tm = g.get("tomorrow")
    tomorrow, tomorrow_date, tomorrow_status = None, "", ""
    if isinstance(tm, dict) and isinstance(tm.get("slots"), list):
        tomorrow_date = _str(tm.get("date"))
        tomorrow_status = _str(tm.get("status"))
        if tomorrow_date:
            tomorrow = slots_compact(tm["slots"])
    if tomorrow is None:
        tomorrow, tomorrow_date, tomorrow_status = "[]", "", NOT_PUBLISHED
    out = {"g": group, "td": today, "tm": tomorrow, "dt": today_date, "dm": tomorrow_date,
           "st": _str(td.get("status")), "sm": tomorrow_status, "up": _str(g.get("updatedOn"))}
    out["v"] = hashlib.sha1(json.dumps(out, sort_keys=True, ensure_ascii=False).encode()).hexdigest()[:12]
    return out


class YasnoCache:
    def __init__(self, url: str = PLANNED_URL, fresh_sec: int = 7200):
        self.url = url
        self.fresh_sec = fresh_sec
        self.doc: dict | None = None
        self.at = 0.0
        self.error = ""
        self.fails = 0
        self.busy = False
        self.tried_at = 0.0

    def fresh(self, now: float) -> bool:
        return self.doc is not None and now - self.at < self.fresh_sec

    def set_doc(self, doc: dict, now: float) -> None:
        self.doc, self.at, self.error, self.fails = doc, now, "", 0

    def _fetch(self) -> dict:
        req = urllib.request.Request(self.url, headers={"User-Agent": "LightWatcherServer/1", "Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=20) as r:
            doc = json.loads(r.read().decode())
        if not isinstance(doc, dict) or not doc:
            raise ValueError("format")
        return doc

    def should_kick(self, now: float, min_gap: float = 60) -> bool:
        return not self.busy and not self.fresh(now) and now - self.tried_at >= min_gap

    async def refresh(self) -> bool:
        if self.busy:
            return False
        self.busy = True
        self.tried_at = time.time()
        try:
            doc = await asyncio.to_thread(self._fetch)
        except Exception as e:
            self.error = type(e).__name__
            self.fails += 1
            return False
        finally:
            self.busy = False
        self.set_doc(doc, time.time())
        return True

    def for_group(self, group: str, now: float) -> dict | None:
        if not self.fresh(now) or not group:
            return None
        return group_schedule(self.doc, group)
