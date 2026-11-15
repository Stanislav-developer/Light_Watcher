import bisect
import io
import json
from datetime import date, datetime, timedelta
from zoneinfo import ZoneInfo

from PIL import Image, ImageDraw

from .schedimg import BG, INK, LINE, MUT, NOD, OFF, ON, WARN, font, parse_slots, upload_retry_sec

W = 1600
PAD = 44
DAYS = 7
CACHE_MAX = 64
SITE_TTL_SEC = 120
KEEP_DAYS = 70
HIST_DAYS = 30
MONTHS = ["", "січня", "лютого", "березня", "квітня", "травня", "червня",
          "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"]
DN = ["Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Нд"]
KINDS = {"off", "on", "boot"}


def dur(m: float) -> str:
    h, mm = divmod(int(round(m)), 60)
    if h and mm:
        return f"{h} год {mm} хв"
    return f"{h} год" if h else f"{mm} хв"


def sign(m: int) -> str:
    return ("+" if m >= 0 else "−") + dur(abs(m))


def slot_id(now: float, tz: str, hours: int) -> str:
    t = datetime.fromtimestamp(now, ZoneInfo(tz))
    return f"{t:%Y%m%d}{t.hour - t.hour % max(hours, 1):02d}"


def period_title(first: date, last: date) -> str:
    if first.month == last.month:
        return f"{first.day}–{last.day} {MONTHS[last.month]}"
    return f"{first.day} {MONTHS[first.month]} – {last.day} {MONTHS[last.month]}"


def segments(events: list[dict], end_ep: int) -> list[tuple[int, int, int]]:
    evs = sorted((e for e in events if e.get("k") in KINDS and (e.get("ep") or 0) > 0),
                 key=lambda e: (e["ep"], e.get("sid") or 0, e.get("q") or 0))
    out: list[tuple[int, int, int]] = []
    cur, ct = None, 0
    for e in evs:
        k, ep = e["k"], int(e["ep"])
        if k == "boot":
            s = 1 if e.get("pw") else 0
        elif k == "off":
            s = 0
        else:
            s = 1
            d = int(e.get("d") or 0)
            if cur != 0 and d > 0:
                start = ep - d
                if cur is None:
                    cur, ct = 0, start
                elif start > ct:
                    out.append((ct, start, cur))
                    cur, ct = 0, start
        if cur is None:
            cur, ct = s, ep
            continue
        if s == cur:
            continue
        if ep > ct:
            out.append((ct, ep, cur))
        cur, ct = s, ep
    if cur is not None and end_ep > ct:
        out.append((ct, end_ep, cur))
    return out


def day_epochs(day: date, tz: str) -> list[int]:
    z = ZoneInfo(tz)
    base = datetime(day.year, day.month, day.day, tzinfo=z)
    return [int((base + timedelta(minutes=m)).timestamp()) for m in range(1440)]


def fact_marks(segs: list[tuple[int, int, int]], day: date, tz: str) -> list:
    starts = [s[0] for s in segs]
    out = []
    for ep in day_epochs(day, tz):
        i = bisect.bisect_right(starts, ep) - 1
        out.append(segs[i][2] if i >= 0 and ep < segs[i][1] else None)
    return out


def plan_view(row: dict | None) -> dict:
    if not row:
        return {"mode": "", "arr": None}
    st = row.get("status", "")
    if st == "EmergencyShutdowns":
        return {"mode": "emerg", "arr": None}
    if st == "NoShutdowns":
        return {"mode": "slots", "arr": [1] * 1440}
    if st != "ScheduleApplies":
        return {"mode": "", "arr": None}
    arr = [1] * 1440
    for s, e in parse_slots(row.get("slots", "[]")):
        for m in range(s, e):
            arr[m] = 0
    return {"mode": "slots", "arr": arr}


def build(events: list[dict], plans: dict[str, dict], now: float, end_ep: int, tz: str, group: str = "") -> dict:
    today = datetime.fromtimestamp(now, ZoneInfo(tz)).date()
    segs = segments(events, end_ep)
    days = []
    for i in range(DAYS - 1, -1, -1):
        d = today - timedelta(days=i)
        days.append({"date": d, "fact": fact_marks(segs, d, tz), "plan": plan_view(plans.get(d.isoformat())),
                     "today": i == 0})
    t = datetime.fromtimestamp(now, ZoneInfo(tz))
    now_min = t.hour * 60 + t.minute
    return {"days": days, "group": group, "now_min": now_min, "made": f"{t.day:02d}.{t.month:02d} о {t:%H:%M}"}


def runs(arr: list) -> list[tuple[int, int, object]]:
    out, s = [], 0
    for i in range(1, len(arr) + 1):
        if i == len(arr) or arr[i] != arr[s]:
            out.append((s, i, arr[s]))
            s = i
    return out


def stats(week: dict) -> dict:
    t = {"on": 0, "off": 0, "pon": 0, "diff": 0, "match": 0, "cmp": 0, "n": 0, "np": 0, "inplan": 0, "maxon": 0, "maxoff": 0}
    fact, plan = [], []
    for d in week["days"]:
        f, p = d["fact"], d["plan"]["arr"]
        on, off = f.count(1), f.count(0)
        both = [m for m in range(1440) if p is not None and f[m] is not None]
        d["on"], d["off"] = on, off
        d["pon"] = sum(1 for m in both if p[m] == 1) if p is not None else None
        d["diff"] = sum(1 for m in both if f[m] == 1) - d["pon"] if p is not None else None
        t["on"] += on
        t["off"] += off
        if p is not None:
            t["pon"] += d["pon"]
            t["diff"] += d["diff"]
            t["cmp"] += len(both)
            t["match"] += sum(1 for m in both if p[m] == f[m])
        fact += f
        plan += p if p is not None else [None] * 1440
    for i in range(1, len(fact)):
        if fact[i - 1] == 1 and fact[i] == 0:
            t["n"] += 1
            if plan[i] is not None:
                t["np"] += 1
                if any(plan[j] == 0 for j in range(max(0, i - 15), min(len(plan), i + 16))):
                    t["inplan"] += 1
    for a, b, v in runs(fact):
        if v == 1:
            t["maxon"] = max(t["maxon"], b - a)
        elif v == 0:
            t["maxoff"] = max(t["maxoff"], b - a)
    t["known"] = t["on"] + t["off"]
    return t


def caption(week: dict) -> str:
    ds = week["days"]
    return f"📈 {period_title(ds[0]['date'], ds[-1]['date'])} · оновлено о {week['made'][-5:]}"


def pct(a: int, b: int) -> str:
    return f"{round(100 * a / b)} %" if b else "—"


def _right(dr, x, y, text, f, fill):
    dr.text((x - dr.textlength(text, font=f), y), text, font=f, fill=fill)


def _rich(dr, x, y, parts, size, right=False):
    fonts = {False: font(size), True: font(size, True)}
    if right:
        x -= sum(dr.textlength(t, font=fonts[b]) for t, _, b in parts)
    for t, color, bold in parts:
        dr.text((x, y), t, font=fonts[bold], fill=color)
        x += dr.textlength(t, font=fonts[bold])
    return x


def _bar(img, box, segs, radius, gap, base=NOD):
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    im = Image.new("RGB", (w, h), base)
    dr = ImageDraw.Draw(im)
    for a, b, v in segs:
        if v is None:
            continue
        xa = round(a * w / 1440)
        dr.rectangle((xa, 0, max(round(b * w / 1440), xa + 2) - 1, h), fill=ON if v == 1 else OFF)
    for q in (4, 8, 12, 16, 20):
        x = round(q * 60 * w / 1440)
        dr.line((x, 0, x, h), fill=gap, width=2)
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), radius, fill=255)
    img.paste(im, (x0, y0), mask)


LC = 180
RC = 390
TOP = PAD + 90
ROW_H = 108
BX = PAD + LC
BW = W - 2 * PAD - LC - RC


def _summary(t: dict) -> list[list[tuple[str, tuple, bool]]]:
    known = t["known"]
    if not known:
        return [[("Даних про світло за ці дні ще немає: історія з'являється, коли на сервері ввімкнено «Історія».", MUT, False)]]
    lines = []
    first = [("Зі світлом ", INK, False), (dur(t["on"]), ON, True), (f" ({pct(t['on'], known)})", INK, False)]
    if t["cmp"]:
        more = t["diff"] >= 0
        first += [(", за графіком мало бути ", INK, False), (dur(t["pon"]), INK, True), (" — на ", INK, False),
                  (dur(abs(t["diff"])), ON if more else OFF, True), (" більше" if more else " менше", INK, False)]
    else:
        first += [(" · у середньому за добу ", INK, False), (dur(t["on"] * 1440 / known), ON, True)]
    lines.append(first)
    second = [("Без світла ", INK, False), (dur(t["off"]), OFF, True), (f" ({pct(t['off'], known)}) · відключень ", INK, False),
              (str(t["n"]), OFF, True)]
    if t["n"]:
        second += [(" · середнє ", INK, False), (dur(t["off"] / t["n"]), OFF, True)]
    lines.append(second)
    if t["maxoff"] or t["maxon"]:
        lines.append([("Найдовше: без світла ", INK, False), (dur(t["maxoff"]) if t["maxoff"] else "—", OFF, True),
                      (", зі світлом ", INK, False), (dur(t["maxon"]) if t["maxon"] else "—", ON, True)])
    if t["cmp"]:
        last = [("Збіг з графіком ", INK, False), (pct(t["match"], t["cmp"]), WARN, True), (" часу", INK, False)]
        if t["np"]:
            last += [(" · у межах графіка (±15 хв) почалися ", INK, False), (pct(t["inplan"], t["np"]), WARN, True),
                     (" відключень", INK, False)]
        lines.append(last)
    else:
        lines.append([("Графіка YASNO за ці дні немає — порівняння з'явиться, коли сервер його збереже.", MUT, False)])
    return lines


def render(week: dict) -> bytes:
    t = stats(week)
    days = week["days"]
    img = Image.new("RGB", (W, 2000), BG)
    dr = ImageDraw.Draw(img)
    first, last = days[0]["date"], days[-1]["date"]
    dr.text((PAD, PAD), f"Тиждень {period_title(first, last)} {last.year}", font=font(46, True), fill=INK)
    head = f"Київ · YASNO · черга {week['group']}" if week.get("group") else "Light Watcher"
    _right(dr, W - PAD, PAD + 14, head, font(26), MUT)
    y = TOP
    rx = W - PAD - RC + 24
    for d in days:
        dd = d["date"]
        dr.text((PAD, y + 4), f"{DN[dd.weekday()]} {dd.day:02d}.{dd.month:02d}", font=font(30, True), fill=INK)
        if d["today"]:
            dr.text((PAD, y + 42), "сьогодні", font=font(20), fill=MUT)
        _bar(img, (BX, y, BX + BW, y + 34), runs(d["fact"]), 8, BG)
        if d["today"]:
            x = BX + round(week["now_min"] * BW / 1440)
            dr.rectangle((x - 1, y - 5, x + 1, y + 58), fill=INK)
        mode = d["plan"]["mode"]
        if mode == "slots":
            _bar(img, (BX, y + 40, BX + BW, y + 52), runs(d["plan"]["arr"]), 4, BG)
        elif mode == "emerg":
            _bar(img, (BX, y + 40, BX + BW, y + 52), [], 4, BG, WARN)
        for q in range(0, 25, 4):
            label = str(q)
            dr.text((BX + round(q * BW / 24) - dr.textlength(label, font=font(18)) / 2, y + 58), label, font=font(18), fill=MUT)
        if d["on"] + d["off"]:
            _rich(dr, rx, y + 2, [(dur(d["on"]), ON, True), (" | ", MUT, False), (dur(d["off"]), OFF, True)], 25)
        else:
            dr.text((rx, y + 2), "немає даних", font=font(25), fill=MUT)
        if mode == "slots":
            parts = [(f"графік {dur(d['pon'])}  ", MUT, False)]
            if d["on"] + d["off"]:
                parts.append((sign(d["diff"]), ON if d["diff"] >= 0 else OFF, True))
            _rich(dr, rx, y + 38, parts, 22)
        elif mode == "emerg":
            dr.text((rx, y + 38), "екстрені, графік не діяв", font=font(22), fill=WARN)
        else:
            dr.text((rx, y + 38), "графіка немає", font=font(22), fill=MUT)
        y += ROW_H
    y += 6
    dr.line((PAD, y, W - PAD, y), fill=LINE, width=2)
    y += 22
    lx = PAD
    for color, text in ((ON, "світло є"), (OFF, "світла немає"), (NOD, "немає даних")):
        dr.rounded_rectangle((lx, y + 2, lx + 24, y + 26), 6, fill=color)
        dr.text((lx + 34, y + 14), text, font=font(23), fill=MUT, anchor="lm")
        lx += 34 + int(dr.textlength(text, font=font(23))) + 30
    _right(dr, W - PAD, y + 2, "товста смуга — як було · тонка — графік", font(22), MUT)
    y += 52
    end_x = PAD
    for line in _summary(t):
        dot = next((c for _, c, b in line if b), line[0][1])
        dr.ellipse((PAD, y + 9, PAD + 14, y + 23), fill=dot)
        end_x = _rich(dr, PAD + 28, y, line, 27)
        y += 44
    foot = f"Light Watcher · оновлено {week['made']}"
    fw = dr.textlength(foot, font=font(22))
    if end_x + 40 > W - PAD - fw:
        y += 6
        _right(dr, W - PAD, y, foot, font(22), MUT)
        y += 30
    else:
        _right(dr, W - PAD, y - 30, foot, font(22), MUT)
    y += PAD - 10
    buf = io.BytesIO()
    img.crop((0, 0, W, y)).save(buf, "PNG", optimize=True)
    return buf.getvalue()


def archive_rows(scheds: list[dict], ready) -> list[tuple[str, str, str, str]]:
    rows = []
    for s in scheds:
        if s.get("dt"):
            rows.append((s["g"], s["dt"][:10], s.get("st", ""), s.get("td", "[]")))
        if ready(s):
            rows.append((s["g"], s["dm"][:10], s.get("sm", ""), s.get("tm", "[]")))
    return rows


class WeekStore:
    def __init__(self, cap: int = CACHE_MAX):
        self.cap = cap
        self.cur: dict[str, dict] = {}
        self.busy: set[tuple[str, str]] = set()
        self.fails: dict[tuple[str, str], tuple[int, float]] = {}
        self.site: dict[str, tuple[float, bytes]] = {}

    def get(self, dev: str) -> dict | None:
        return self.cur.get(dev)

    def can_upload(self, dev: str, slot: str, now: float) -> bool:
        k = (dev, slot)
        if k in self.busy or (self.cur.get(dev) or {}).get("slot") == slot:
            return False
        n, at = self.fails.get(k, (0, 0.0))
        return n == 0 or now - at >= upload_retry_sec(n)

    def done(self, dev: str, slot: str, fid: str | None, cap: str, now: float) -> None:
        self.busy.discard((dev, slot))
        if fid:
            self.fails.pop((dev, slot), None)
            self.cur[dev] = {"slot": slot, "fid": fid, "cap": cap}
            while len(self.cur) > self.cap:
                self.cur.pop(next(iter(self.cur)))
        else:
            n, _ = self.fails.get((dev, slot), (0, 0.0))
            self.fails[(dev, slot)] = (n + 1, now)
            while len(self.fails) > self.cap:
                self.fails.pop(next(iter(self.fails)))

    def site_png(self, dev: str, now: float) -> bytes | None:
        at, png = self.site.get(dev, (0.0, b""))
        return png if png and now - at < SITE_TTL_SEC else None

    def put_site(self, dev: str, png: bytes, now: float) -> None:
        self.site[dev] = (now, png)
        while len(self.site) > 16:
            self.site.pop(next(iter(self.site)))


def demo(now: float, tz: str) -> tuple[list[dict], dict[str, dict]]:
    today = datetime.fromtimestamp(now, ZoneInfo(tz)).date()
    plans_raw = [[(60, 390), (750, 870)], [(0, 60), (180, 300), (540, 960)], [(600, 690), (810, 1440)],
                 [(240, 510), (660, 1200)], [(0, 330), (510, 1050)], [(0, 240), (420, 840)], [(240, 720), (840, 1260)]]
    facts = [[(0, 67), (380, 400)], [(10, 60), (540, 900)], [(610, 700), (820, 1100)], [(250, 500)],
             [(5, 320), (520, 1000), (1150, 1290)], [(10, 250), (430, 800)], [(250, 700)]]
    events, plans, q = [{"k": "boot", "ep": 0, "pw": 1, "q": 0}], {}, 0
    z = ZoneInfo(tz)
    for i in range(DAYS):
        d = today - timedelta(days=DAYS - 1 - i)
        plans[d.isoformat()] = {"status": "ScheduleApplies",
                                "slots": json.dumps([{"s": s, "e": e} for s, e in plans_raw[i]])}
        base = datetime(d.year, d.month, d.day, tzinfo=z).timestamp()
        for a, b in facts[i]:
            q += 1
            events.append({"k": "off", "ep": int(base + a * 60), "q": q})
            q += 1
            events.append({"k": "on", "ep": int(base + b * 60), "q": q, "d": (b - a) * 60})
    first = datetime(today.year, today.month, today.day, tzinfo=z).timestamp() - (DAYS - 1) * 86400
    events[0]["ep"] = int(first)
    return [e for e in events if e["ep"] <= now], plans
