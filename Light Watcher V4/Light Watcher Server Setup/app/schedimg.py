import hashlib
import io
import json
from collections import OrderedDict
from datetime import date, datetime
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

DESIGN = "img1"
W = 1080
PAD = 48
GAP = 12
CELL_H = 64
RADIUS = 12
COLS = 6
CACHE_MAX = 128
IMG_HOLD_SEC = 120

BG = (15, 20, 26)
INK = (230, 235, 241)
MUT = (142, 155, 171)
LINE = (38, 49, 64)
ON = (51, 192, 114)
OFF = (239, 95, 95)
WARN = (240, 168, 60)
NOD = (44, 54, 68)
CELL_INK = (255, 255, 255)

READY_TOMORROW = {"ScheduleApplies", "NoShutdowns", "EmergencyShutdowns"}
MONTHS = ["", "січня", "лютого", "березня", "квітня", "травня", "червня",
          "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"]
DAYS = ["пн", "вт", "ср", "чт", "пт", "сб", "нд"]

FONT_DIRS = [
    Path(__file__).resolve().parent / "fonts",
    Path("/usr/share/fonts/truetype/dejavu"),
    Path("/System/Library/Fonts/Supplemental"),
]
FONT_FILES = {
    False: ["DejaVuSans.ttf", "Arial.ttf"],
    True: ["DejaVuSans-Bold.ttf", "Arial Bold.ttf"],
}
_fonts: dict = {}


def font(size: int, bold: bool = False):
    k = (size, bold)
    if k not in _fonts:
        f = None
        for d in FONT_DIRS:
            for name in FONT_FILES[bold]:
                p = d / name
                if p.is_file():
                    f = ImageFont.truetype(str(p), size)
                    break
            if f:
                break
        _fonts[k] = f or ImageFont.load_default(size)
    return _fonts[k]


def day_caption(iso: str) -> str:
    try:
        d = date.fromisoformat(iso[:10])
    except ValueError:
        return "дата невідома"
    return f"{DAYS[d.weekday()]}, {d.day} {MONTHS[d.month]}"


def updated_caption(iso: str, tz: str) -> str:
    try:
        t = datetime.fromisoformat(iso.replace("Z", "+00:00"))
    except ValueError:
        return ""
    if t.tzinfo:
        try:
            from zoneinfo import ZoneInfo
            t = t.astimezone(ZoneInfo(tz))
        except Exception:
            pass
    return f"{t.day:02d}.{t.month:02d} о {t:%H:%M}"


def parse_slots(raw: str) -> list[tuple[int, int]]:
    try:
        arr = json.loads(raw or "[]")
    except ValueError:
        return []
    out = []
    if not isinstance(arr, list):
        return out
    for s in arr:
        if not isinstance(s, dict):
            continue
        st, en = s.get("s"), s.get("e")
        if isinstance(st, int) and isinstance(en, int) and 0 <= st < en <= 1440:
            out.append((st, en))
    return out


def off_minutes(slots: list[tuple[int, int]]) -> int:
    marks = [False] * 1440
    for st, en in slots:
        for m in range(st, en):
            marks[m] = True
    return sum(marks)


def duration_text(m: int) -> str:
    h, mm = divmod(m, 60)
    if h and mm:
        return f"{h} год {mm} хв"
    return f"{h} год" if h else f"{mm} хв"


def tomorrow_ready(s: dict) -> bool:
    dt, dm = s.get("dt", ""), s.get("dm", "")
    return len(dm) >= 10 and len(dt) >= 10 and dm[:10] > dt[:10] and s.get("sm", "") in READY_TOMORROW


def day_view(iso: str, status: str, slots_raw: str) -> dict:
    if status == "NoShutdowns":
        return {"date": iso, "mode": "on", "slots": [], "note": "відключень немає", "tone": ON}
    if status == "EmergencyShutdowns":
        return {"date": iso, "mode": "nod", "slots": [], "note": "екстрені відключення, графік не діє", "tone": WARN}
    if status == "WaitingForSchedule":
        return {"date": iso, "mode": "nod", "slots": [], "note": "графік ще не підтверджено", "tone": MUT}
    slots = parse_slots(slots_raw)
    m = off_minutes(slots)
    note = f"без світла {duration_text(m)}" if m else "відключень немає"
    return {"date": iso, "mode": "slots", "slots": slots, "note": note, "tone": OFF if m else ON}


def image_params(sched: dict) -> dict:
    p = {"d": DESIGN, "g": sched.get("g", ""), "up": sched.get("up", ""),
         "dt": sched.get("dt", ""), "st": sched.get("st", ""), "td": sched.get("td", "[]")}
    if tomorrow_ready(sched):
        p.update({"dm": sched.get("dm", ""), "sm": sched.get("sm", ""), "tm": sched.get("tm", "[]")})
    return p


def image_key(params: dict) -> str:
    return hashlib.sha1(json.dumps(params, sort_keys=True, ensure_ascii=False).encode()).hexdigest()[:16]


def _cell(w: int, h: int, hour: int, view: dict) -> Image.Image:
    im = Image.new("RGB", (w, h), NOD if view["mode"] == "nod" else ON)
    dr = ImageDraw.Draw(im)
    if view["mode"] == "slots":
        a, b = hour * 60, hour * 60 + 60
        for st, en in view["slots"]:
            lo, hi = max(st, a), min(en, b)
            if hi > lo:
                x0 = round((lo - a) * w / 60)
                x1 = round((hi - a) * w / 60)
                dr.rectangle((x0, 0, x1 - 1, h), fill=OFF)
    f = font(28, True)
    label = f"{hour:02d}:00"
    tw = dr.textlength(label, font=f)
    dr.text(((w - tw) / 2, h / 2), label, font=f, fill=CELL_INK if view["mode"] != "nod" else MUT, anchor="lm")
    return im


def cell_box(hour: int, top: int) -> tuple[int, int, int, int]:
    cw = (W - 2 * PAD - (COLS - 1) * GAP) // COLS
    r, c = divmod(hour, COLS)
    x = PAD + c * (cw + GAP)
    y = top + r * (CELL_H + GAP)
    return x, y, x + cw - 1, y + CELL_H - 1


def _grid(img: Image.Image, top: int, view: dict) -> int:
    cw = (W - 2 * PAD - (COLS - 1) * GAP) // COLS
    mask = Image.new("L", (cw, CELL_H), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, cw - 1, CELL_H - 1), RADIUS, fill=255)
    for hour in range(24):
        x, y, _, _ = cell_box(hour, top)
        img.paste(_cell(cw, CELL_H, hour, view), (x, y), mask)
    return top + 4 * CELL_H + 3 * GAP


def _right(dr: ImageDraw.ImageDraw, x: int, y: int, text: str, f, fill) -> None:
    dr.text((x - dr.textlength(text, font=f), y), text, font=f, fill=fill)


def render(params: dict, tz: str = "Europe/Kyiv") -> bytes:
    days = [day_view(params.get("dt", ""), params.get("st", ""), params.get("td", "[]"))]
    if params.get("dm"):
        days.append(day_view(params["dm"], params.get("sm", ""), params.get("tm", "[]")))
    grid_h = 4 * CELL_H + 3 * GAP
    h = PAD + 120 + len(days) * (grid_h + 160) + PAD
    img = Image.new("RGB", (W, h), BG)
    dr = ImageDraw.Draw(img)
    y = PAD
    dr.text((PAD, y), "Київ · YASNO", font=font(26), fill=MUT)
    dr.text((PAD, y + 30), f"Черга {params.get('g', '')}", font=font(52, True), fill=INK)
    _right(dr, W - PAD, y + 4, day_caption(days[0]["date"]), font(34, True), INK)
    _right(dr, W - PAD, y + 48, days[0]["note"], font(28), days[0]["tone"])
    y = _grid(img, y + 106, days[0])
    for d in days[1:]:
        y += 40
        dr.line((PAD, y, W - PAD, y), fill=LINE, width=2)
        y += 24
        dr.text((PAD, y + 4), "Завтра", font=font(34, True), fill=INK)
        _right(dr, W - PAD, y, day_caption(d["date"]), font(30, True), INK)
        _right(dr, W - PAD, y + 38, d["note"], font(26), d["tone"])
        y = _grid(img, y + 78, d)
    y += 32
    lx = PAD
    for color, text in ((ON, "світло є"), (OFF, "відключення")):
        dr.rounded_rectangle((lx, y + 4, lx + 26, y + 30), 6, fill=color)
        dr.text((lx + 38, y + 17), text, font=font(26), fill=MUT, anchor="lm")
        lx += 38 + int(dr.textlength(text, font=font(26))) + 36
    upd = updated_caption(params.get("up", ""), tz)
    _right(dr, W - PAD, y + 4, "Light Watcher" + (f" · YASNO {upd}" if upd else ""), font(24), MUT)
    y += 40 + PAD
    img = img.crop((0, 0, W, y))
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


DEMO = {"g": "10.1", "up": "2026-11-12T20:55:00+02:00",
        "dt": "2026-11-12", "st": "ScheduleApplies",
        "td": '[{"s":0,"e":240,"t":"Definite"},{"s":510,"e":870,"t":"Definite"},{"s":990,"e":1320,"t":"Definite"}]',
        "dm": "2026-11-13", "sm": "ScheduleApplies",
        "tm": '[{"s":120,"e":360,"t":"Definite"},{"s":720,"e":900,"t":"Possible"},{"s":1080,"e":1260,"t":"Definite"}]'}


def upload_retry_sec(fails: int) -> int:
    return min(60 * 2 ** max(fails - 1, 0), 1800)


class ImageStore:
    def __init__(self, tz: str = "Europe/Kyiv", cap: int = CACHE_MAX):
        self.tz = tz
        self.cap = cap
        self.params: OrderedDict[str, dict] = OrderedDict()
        self.png: OrderedDict[str, bytes] = OrderedDict()
        self.fid: OrderedDict[str, str] = OrderedDict()
        self.busy: set[str] = set()
        self.fails: dict[str, tuple[int, float]] = {}
        self.first: dict[str, float] = {}
        self.by_ver: OrderedDict[str, str] = OrderedDict()

    def last_for(self, ver: str) -> str:
        return self.by_ver.get(ver, "")

    def holding(self, key: str, now: float) -> bool:
        return key not in self.fid and now - self.first.get(key, now) < IMG_HOLD_SEC

    def file_id(self, key: str) -> str:
        return self.fid.get(key, "")

    def can_upload(self, key: str, now: float) -> bool:
        if key in self.busy or key in self.fid or key not in self.params:
            return False
        n, at = self.fails.get(key, (0, 0.0))
        return n == 0 or now - at >= upload_retry_sec(n)

    def upload_done(self, key: str, fid: str | None, now: float, ver: str = "") -> None:
        self.busy.discard(key)
        if fid and ver:
            self.by_ver[ver] = fid
            self.by_ver.move_to_end(ver)
            while len(self.by_ver) > self.cap:
                self.by_ver.popitem(last=False)
        if fid:
            self.fails.pop(key, None)
            self.fid[key] = fid
            self.fid.move_to_end(key)
            while len(self.fid) > self.cap:
                self.fid.popitem(last=False)
        else:
            n, _ = self.fails.get(key, (0, 0.0))
            self.fails[key] = (n + 1, now)
            while len(self.fails) > self.cap:
                self.fails.pop(next(iter(self.fails)))

    def register(self, sched: dict, now: float | None = None) -> str:
        p = image_params(sched)
        k = image_key(p)
        self.params[k] = p
        self.params.move_to_end(k)
        if now is not None:
            self.first.setdefault(k, now)
        while len(self.params) > self.cap:
            old, _ = self.params.popitem(last=False)
            self.png.pop(old, None)
            self.first.pop(old, None)
        return k

    def cached(self, key: str) -> bytes | None:
        return self.png.get(key)

    def find(self, key: str, fallback=None) -> dict | None:
        p = self.params.get(key)
        if p is None and fallback is not None:
            for sched in fallback():
                if image_key(image_params(sched)) == key:
                    self.register(sched)
                    return self.params[key]
        return p

    def put_png(self, key: str, data: bytes) -> None:
        self.png[key] = data
        while len(self.png) > 16:
            self.png.popitem(last=False)
