from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

from .schedimg import READY_TOMORROW

EV_BOOT_ON, EV_BOOT_OFF, EV_OFF, EV_ON = 1, 2, 3, 4
STATE_ON = {EV_BOOT_ON, EV_ON}
STATE_OFF = {EV_BOOT_OFF, EV_OFF}
MIN_EPOCH = 1_500_000_000
DAY = 1440


def local_now(now: float, tz: str) -> datetime:
    try:
        zone = ZoneInfo(tz)
    except Exception:
        zone = ZoneInfo("Europe/Kyiv")
    return datetime.fromtimestamp(now, zone)


def slot_pairs(raw) -> list[list[int]]:
    out = []
    if not isinstance(raw, list):
        return out
    for s in raw:
        if not isinstance(s, dict):
            continue
        st, en = s.get("s"), s.get("e")
        if isinstance(st, int) and isinstance(en, int) and not isinstance(st, bool) and 0 <= st < en <= DAY:
            out.append([st, en])
    out.sort()
    return out


def since_epoch(ev, pw: int) -> int | None:
    if not isinstance(ev, list):
        return None
    want = STATE_ON if pw else STATE_OFF
    other = STATE_OFF if pw else STATE_ON
    best = None
    for item in reversed(ev):
        if not (isinstance(item, list) and len(item) == 2 and all(isinstance(x, int) for x in item)):
            continue
        t, k = item
        if k in other:
            break
        if k in want and t >= MIN_EPOCH:
            best = t
    return best


def day_slots(ys: dict, today_iso: str, tomorrow_iso: str) -> tuple[list | None, list | None, bool]:
    if not isinstance(ys, dict) or not ys.get("configured") or not ys.get("valid"):
        return None, None, False
    td = str(ys.get("todayDate", ""))[:10]
    tm = str(ys.get("tomorrowDate", ""))[:10]
    today = slot_pairs(ys.get("today"))
    tomorrow = slot_pairs(ys.get("tomorrow"))
    tm_ok = tm == tomorrow_iso and ys.get("tomorrowStatus") in READY_TOMORROW
    if td == today_iso:
        return today, (tomorrow if tm_ok else None), tm_ok
    if td < today_iso and tm == today_iso and ys.get("tomorrowStatus") in READY_TOMORROW:
        return tomorrow, None, False
    return None, None, False


def merged(today: list, tomorrow: list | None) -> list[list[int]]:
    seq = [list(p) for p in today]
    if tomorrow:
        seq += [[s + DAY, e + DAY] for s, e in tomorrow]
    out: list[list[int]] = []
    for s, e in sorted(seq):
        if out and s <= out[-1][1]:
            out[-1][1] = max(out[-1][1], e)
        else:
            out.append([s, e])
    return out


def next_event(pw: int, nm: int, today: list, tomorrow: list | None) -> dict | None:
    for s, e in merged(today, tomorrow):
        if pw and s > nm:
            return {"k": "off", "m": s}
        if not pw and s <= nm < e:
            return {"k": "on", "m": e}
    return None


def build(snap: dict, now: float, tz: str, age: float) -> dict:
    pw = 1 if snap.get("pw") else 0
    lt = local_now(now, tz)
    nm = lt.hour * 60 + lt.minute
    d0 = lt.date().isoformat()
    d1 = (lt.date() + timedelta(days=1)).isoformat()
    today, tomorrow, tm_ok = day_slots(snap.get("ys"), d0, d1)
    volt = snap.get("volt")
    out = {
        "ok": 1,
        "pw": pw,
        "ts": int(now),
        "age": int(max(age, 0)),
        "nm": nm,
        "since": since_epoch(snap.get("ev"), pw),
        "volt": round(float(volt), 1) if isinstance(volt, (int, float)) and not isinstance(volt, bool) and pw else None,
        "ys": 1 if today is not None else 0,
        "today": today or [],
        "tomorrow": tomorrow or [],
        "tm": 1 if tm_ok else 0,
        "nx": None,
    }
    if today is not None:
        out["nx"] = next_event(pw, nm, today, tomorrow)
    return out
