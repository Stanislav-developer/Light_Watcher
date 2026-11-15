import asyncio
import json
import re
import time
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, PlainTextResponse, Response
from pydantic import BaseModel, Field, ValidationError

from . import auth
from .config import Settings
from .db import Db
from .telegram import Notifier
from .schedimg import DEMO, ImageStore, image_params, render, tomorrow_ready
from . import weekimg
from .widget import build as widget_build
from .yasno import YasnoCache, group_schedule

API_VERSION = 1
EVENT_KINDS = {"off", "on", "blip", "boot", "stop", "sens", "volt"}
MAX_EVENTS = 64
MAX_BODY = 16384
MAX_DATA = 65536
MAX_CFG = 5000
CFG_FORMAT = 1
P_HIST = 0x01
P_WEB = 0x02
P_ALIVE = 0x04
P_YASNO = 0x08
P_IMG = 0x10
P_WEEK = 0x20
P_READY = P_HIST | P_WEB | P_ALIVE | P_YASNO | P_IMG | P_WEEK
YASNO_REFRESH_SEC = 300
YASNO_RETRY_MAX_SEC = 1800
FAST_SEC = 10
VIEW_FRESH_SEC = 120
SNAPSHOT_MAX_AGE = 900
SNAPSHOT_VIEW_AGE = 20
KEEP_MAX = 10
STATIC = Path(__file__).resolve().parent / "static"
WIDGET_LOADER = (
    'const U = "{BASE}";\n'
    'const f = FileManager.local(), p = f.joinPath(f.documentsDirectory(), "lw-widget.js");\n'
    'try { const r = new Request(U + "widget.js"), s = await r.loadString(); if (r.response.statusCode == 200) f.writeString(p, s); } catch (e) {}\n'
    'await eval(f.readString(p));\n'
)


class Event(BaseModel):
    q: int = Field(ge=1)
    k: str
    ep: int | None = None
    up: int | None = None
    d: int | None = None
    pw: int | None = None
    v: int | None = None


class SyncBody(BaseModel):
    fw: str = Field(max_length=16)
    up: int = 0
    ep: int = 0
    tt: int = 0
    hb: int = 30
    hbOn: int = 30
    hbOff: int = 60
    pt: int = 0
    sid: int = 0
    kp: int = 0
    hello: int = 0
    bye: int = 0
    ysg: str = Field(default="", max_length=8)
    ysv: str = Field(default="", max_length=16)
    stc: str = Field(default="", max_length=24)
    wkh: int = 0
    st: dict = Field(default_factory=dict)
    ev: list[Event] = Field(default_factory=list, max_length=MAX_EVENTS)
    ack: int = 0


def offline_after(hb_on: int, hb_off: int, pw: int) -> int:
    hb = hb_on if pw else hb_off
    return max(180, 3 * max(int(hb or 0), 0) + 60)


def fw_tuple(fw: str) -> tuple[int, ...]:
    try:
        return tuple(int(x) for x in fw.strip().split(".")[:3])
    except ValueError:
        return (0,)


def accepted_parts(fw: str, pt: int) -> int:
    pt &= P_READY
    if fw_tuple(fw) < (3, 1, 2) and pt & (P_HIST | P_WEB):
        pt |= P_ALIVE
    if not pt & P_YASNO:
        pt &= ~P_IMG
    if not pt & P_HIST:
        pt &= ~P_WEEK
    return pt


def store_chat(stc: str) -> str:
    return stc if re.fullmatch(r"-?\d{5,20}", stc or "") else ""


def dev_slug(dev: str) -> str:
    return dev.replace(":", "").lower()


def short_duration(sec: float) -> str:
    m = int(sec // 60)
    if m < 60:
        return f"{max(m, 1)} хв"
    h, m = divmod(m, 60)
    return f"{h} год {m} хв" if m else f"{h} год"


def create_app(settings: Settings | None = None, notifier: Notifier | None = None, yasno: YasnoCache | None = None) -> FastAPI:
    cfg = settings or Settings.from_env()
    tg = notifier or Notifier(cfg.bot_token, cfg.owner_id, cfg.store_chat)
    ys = yasno or YasnoCache()
    imgs = ImageStore(cfg.tz)
    weeks = weekimg.WeekStore()

    async def upload_image(key: str, ver: str, store: str) -> None:
        fid = None
        try:
            params = imgs.find(key)
            if params is not None:
                png = imgs.cached(key) or await asyncio.to_thread(render, params, cfg.tz)
                imgs.put_png(key, png)
                fid = await tg.upload_photo(png, store)
        finally:
            imgs.upload_done(key, fid, time.time(), ver)

    def week_png(db: Db, d: dict, now: float) -> tuple[bytes, str]:
        since = int(now) - weekimg.HIST_DAYS * 86400
        pw = int(d["state"].get("pw", 1) or 0)
        online = now - (d.get("last_seen") or 0) <= offline_after(d["hb_on"], d["hb_off"], pw)
        end_ep = int(now if online else (d.get("last_seen") or now))
        today = datetime.fromtimestamp(now, ZoneInfo(cfg.tz)).date()
        first = (today - timedelta(days=weekimg.DAYS - 1)).isoformat()
        grp = d.get("grp") or ""
        plans = db.days(grp, first, today.isoformat()) if grp else {}
        week = weekimg.build(db.events_window(d["id"], since), plans, now, end_ep, cfg.tz, grp)
        return weekimg.render(week), weekimg.caption(week)

    async def upload_week(app: FastAPI, dev: str, slot: str, store: str) -> None:
        fid, cap = None, ""
        try:
            d = app.state.db.device(dev)
            if d:
                png, cap = await asyncio.to_thread(week_png, app.state.db, d, time.time())
                fid = await tg.upload_photo(png, store)
        finally:
            weeks.done(dev, slot, fid, cap, time.time())

    def archive_days(app: FastAPI, now: float) -> int:
        if not ys.doc:
            return 0
        rows = weekimg.archive_rows([x for x in (group_schedule(ys.doc, g) for g in ys.doc) if x], tomorrow_ready)
        keep_from = (datetime.fromtimestamp(now, ZoneInfo(cfg.tz)).date() - timedelta(days=weekimg.KEEP_DAYS)).isoformat()
        app.state.db.save_days(rows, now, keep_from)
        return len(rows)

    async def yasno_refresh(app: FastAPI) -> bool:
        ok = await ys.refresh()
        if ok:
            try:
                await asyncio.to_thread(archive_days, app, time.time())
            except Exception:
                pass
        return ok

    def all_schedules():
        if not ys.fresh(time.time()):
            return []
        return [x for x in (group_schedule(ys.doc, g) for g in ys.doc) if x]

    def local_hm(ts: float) -> str:
        try:
            return datetime.fromtimestamp(ts, ZoneInfo(cfg.tz)).strftime("%H:%M")
        except Exception:
            return datetime.fromtimestamp(ts, timezone.utc).strftime("%H:%M UTC")

    async def notify(app: FastAPI, dev: str, text: str) -> int | None:
        mid = await tg.send(text)
        if mid:
            d = app.state.db.device(dev)
            if d:
                app.state.db.set_fields(dev, keep=(d["keep"] + [mid])[-KEEP_MAX:])
        return mid

    def spawn(app: FastAPI, coro) -> None:
        task = asyncio.get_running_loop().create_task(coro)
        app.state.tasks.add(task)
        task.add_done_callback(app.state.tasks.discard)

    async def check_offline(app: FastAPI, now: float) -> None:
        db: Db = app.state.db
        for d in db.devices():
            if d["detached"] or not (d["parts"] & P_ALIVE) or d["offline_since"] or not d["last_seen"]:
                continue
            pw = int(d["state"].get("pw", 1) or 0)
            if now - d["last_seen"] <= offline_after(d["hb_on"], d["hb_off"], pw):
                continue
            db.set_fields(d["id"], offline_since=now)
            light = "🟢 світло було" if pw else "🔴 світла не було"
            mid = await notify(app, d["id"], f"📵 <b>Light Watcher не на зв'язку</b> з {local_hm(d['last_seen'])}.\n"
                                             f"Останній стан: {light}. Можливо, сів акумулятор або зник інтернет.")
            if mid is None and tg.enabled:
                db.set_fields(d["id"], offline_since=None)

    def backup_tick(app: FastAPI, now: float) -> None:
        if not cfg.backup_dir:
            return
        day = datetime.fromtimestamp(now, timezone.utc).strftime("%Y%m%d")
        if app.state.backup_day != day:
            app.state.db.backup(cfg.backup_dir, day)
            app.state.backup_day = day

    async def monitor(app: FastAPI) -> None:
        while True:
            await asyncio.sleep(cfg.monitor_sec)
            try:
                await check_offline(app, time.time())
            except Exception:
                pass
            try:
                await asyncio.to_thread(backup_tick, app, time.time())
            except Exception:
                pass
            try:
                if getattr(tg, "pending_del", None):
                    await asyncio.to_thread(tg.retry_deletes, time.time())
            except Exception:
                pass

    def yasno_wanted(app: FastAPI) -> bool:
        return any(not d["detached"] and (d["parts"] & P_YASNO or d["parts"] & P_WEEK and d.get("grp"))
                   for d in app.state.db.devices())

    async def yasno_loop(app: FastAPI) -> None:
        while True:
            delay = YASNO_REFRESH_SEC
            try:
                if yasno_wanted(app) and not await yasno_refresh(app):
                    delay = min(YASNO_REFRESH_SEC * (2 ** min(ys.fails, 4)), YASNO_RETRY_MAX_SEC)
            except Exception:
                pass
            await asyncio.sleep(delay)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        app.state.db = Db(cfg.db_path)
        app.state.tasks = set()
        app.state.backup_day = ""
        mon = asyncio.get_running_loop().create_task(monitor(app)) if cfg.monitor_sec > 0 else None
        yl = asyncio.get_running_loop().create_task(yasno_loop(app)) if cfg.yasno_sec > 0 else None
        yield
        if mon:
            mon.cancel()
        if yl:
            yl.cancel()
        for t in list(app.state.tasks):
            t.cancel()
        app.state.db.close()

    app = FastAPI(title="Light Watcher V3", lifespan=lifespan)
    app.state.settings = cfg
    app.state.check_offline = check_offline
    app.state.yasno = ys
    app.state.backup_tick = backup_tick
    app.state.archive_days = archive_days
    app.state.weeks = weeks

    def device_key(dev: str) -> str | None:
        if dev in cfg.device_keys:
            return cfg.device_keys[dev]
        if cfg.bot_token:
            return auth.derive_key(cfg.bot_token, dev)
        return None

    async def limited_body(request: Request, limit: int) -> bytes:
        cl = request.headers.get("content-length", "").strip()
        if cl.isdigit() and int(cl) > limit:
            raise HTTPException(status_code=413, detail="body")
        parts, size = [], 0
        async for chunk in request.stream():
            size += len(chunk)
            if size > limit:
                raise HTTPException(status_code=413, detail="body")
            parts.append(chunk)
        return b"".join(parts)

    async def verified(request: Request, limit: int) -> tuple[str, str, str, bytes]:
        dev = request.headers.get("X-LW-Dev", "").strip().upper()
        ts = request.headers.get("X-LW-Ts", "").strip()
        n = request.headers.get("X-LW-N", "").strip()
        sig = request.headers.get("X-LW-Sig", "")
        key = device_key(dev) if dev else None
        if not key or not ts or not sig or not n:
            raise HTTPException(status_code=401, detail="auth")
        body = await limited_body(request, limit)
        if not auth.verify(auth.sign_request(key, ts, n, body), sig):
            raise HTTPException(status_code=401, detail="sig")
        if not auth.ts_fresh(ts, time.time(), cfg.ts_window_sec):
            raise HTTPException(status_code=401, detail="ts")
        parsed = auth.parse_n(n)
        if parsed is None:
            raise HTTPException(status_code=401, detail="n")
        if not request.app.state.db.accept_n(dev, parsed[0], parsed[1]):
            raise HTTPException(status_code=401, detail="replay")
        return dev, key, n, body

    def signed(key: str, n: str, payload: dict) -> Response:
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode()
        return Response(content=body, media_type="application/json", headers={"X-LW-Sig": auth.sign_response(key, n, body)})

    @app.get("/healthz")
    def healthz():
        return {"ok": 1, "api": API_VERSION}

    @app.post("/api/v1/dev/sync")
    async def dev_sync(request: Request):
        db: Db = request.app.state.db
        dev_before = db.device(request.headers.get("X-LW-Dev", "").strip().upper())
        dev, key, n, raw = await verified(request, MAX_BODY)
        try:
            data = SyncBody.model_validate(json.loads(raw))
        except (ValueError, ValidationError):
            raise HTTPException(status_code=400, detail="body")
        if data.bye:
            db.detach(dev)
            return signed(key, n, {"ok": 1, "ack": 0, "pt": 0})
        pt = accepted_parts(data.fw, data.pt)
        db.touch_device(dev, data.fw, {"up": data.up, "ep": data.ep, "tt": data.tt, "hb": data.hb, **data.st},
                        pt, data.hbOn, data.hbOff)
        events = [e.model_dump() for e in data.ev if e.k in EVENT_KINDS]
        added = db.add_events(dev, data.sid, events) if events else 0
        now = time.time()
        known = dev_before is not None and dev_before.get("fw") and not dev_before.get("detached")
        if data.hello and not known:
            spawn(request.app, notify(request.app, dev, f"🖥 <b>Light Watcher {dev}</b> підключився до сервера."))
        d = db.device(dev) or {}
        if d.get("offline_since"):
            gone = now - (dev_before or {}).get("last_seen", now) if dev_before else 0
            db.set_fields(dev, offline_since=None)
            spawn(request.app, notify(request.app, dev, f"🖥 <b>Light Watcher знову на зв'язку</b> (не було {short_duration(gone)})."))
        if data.ysg and pt & (P_YASNO | P_WEEK) and d.get("grp") != data.ysg:
            db.set_fields(dev, grp=data.ysg)
        keep = [m for m in d.get("keep", []) if m > data.kp]
        if keep != d.get("keep", []):
            db.set_fields(dev, keep=keep)
        resp = {"ok": 1, "ack": db.last_q(dev, data.sid), "pt": pt, "keep": keep}
        if pt & P_YASNO:
            sched = ys.for_group(data.ysg, now)
            resp["ysf"] = 1 if sched else 0
            if not sched and data.ysg and ys.should_kick(now):
                spawn(request.app, yasno_refresh(request.app))
            wait_img = False
            if sched and pt & P_IMG:
                ikey = imgs.register(sched, now)
                fid = imgs.file_id(ikey)
                if fid:
                    resp["ysi"] = fid
                    resp["yiv"] = sched["v"]
                elif tg.enabled:
                    if imgs.can_upload(ikey, now):
                        imgs.busy.add(ikey)
                        spawn(request.app, upload_image(ikey, sched["v"], store_chat(data.stc)))
                    if ikey in imgs.busy:
                        resp["fast"] = FAST_SEC
                    prev = imgs.last_for(sched["v"])
                    if prev:
                        resp["ysi"] = prev
                        resp["yiv"] = sched["v"]
                    else:
                        wait_img = imgs.holding(ikey, now)
            if sched and sched["v"] != data.ysv:
                if wait_img:
                    resp["fast"] = FAST_SEC
                else:
                    resp["ys"] = sched
        if pt & P_WEEK and data.wkh in (1, 2):
            slot = weekimg.slot_id(now, cfg.tz, data.wkh)
            cur = weeks.get(dev)
            if not cur or cur["slot"] != slot:
                if tg.enabled and weeks.can_upload(dev, slot, now):
                    weeks.busy.add((dev, slot))
                    spawn(request.app, upload_week(request.app, dev, slot, store_chat(data.stc)))
                if (dev, slot) in weeks.busy:
                    resp["fast"] = FAST_SEC
            if cur:
                resp["wki"], resp["wkv"], resp["wkt"] = cur["fid"], cur["slot"], cur["cap"]
        viewed = d.get("viewed_at") or 0
        if pt & P_WEB:
            snap_age = now - (d.get("snapshot_at") or 0)
            if added or snap_age > SNAPSHOT_MAX_AGE or (now - viewed < VIEW_FRESH_SEC and snap_age > SNAPSHOT_VIEW_AGE):
                resp["dat"] = 1
            if now - viewed < VIEW_FRESH_SEC and int(data.st.get("pw", 0) or 0) == 1:
                resp["fast"] = FAST_SEC
        return signed(key, n, resp)

    @app.post("/api/v1/dev/data")
    async def dev_data(request: Request):
        dev, key, n, raw = await verified(request, MAX_DATA)
        try:
            snap = json.loads(raw)
        except ValueError:
            raise HTTPException(status_code=400, detail="body")
        if not isinstance(snap, dict) or "dev" in snap:
            raise HTTPException(status_code=400, detail="body")
        request.app.state.db.set_snapshot(dev, raw.decode())
        return signed(key, n, {"ok": 1})

    @app.post("/api/v1/dev/cfg")
    async def dev_cfg(request: Request):
        dev, key, n, raw = await verified(request, MAX_CFG)
        try:
            cfg_doc = json.loads(raw)
        except ValueError:
            raise HTTPException(status_code=400, detail="body")
        if not isinstance(cfg_doc, dict) or cfg_doc.get("v") != CFG_FORMAT:
            raise HTTPException(status_code=400, detail="body")
        request.app.state.db.save_cfg(dev, json.dumps(cfg_doc, ensure_ascii=False, separators=(",", ":")))
        return signed(key, n, {"ok": 1})

    def cfg_req(raw: bytes) -> dict:
        try:
            req = json.loads(raw or b"{}")
        except ValueError:
            return {}
        return req if isinstance(req, dict) else {}

    def cfg_int(req: dict, name: str, top: int) -> int | None:
        v = req.get(name)
        return v if isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= top else None

    @app.post("/api/v1/dev/cfg/get")
    async def dev_cfg_get(request: Request):
        dev, key, n, raw = await verified(request, 64)
        req = cfg_req(raw)
        pick = cfg_int(req, "i", 2 ** 62)
        if pick is not None:
            found = request.app.state.db.cfg_by_id(pick)
        else:
            found = request.app.state.db.latest_cfg(dev, cfg_int(req, "c", 0xFFFFFFFF))
        if not found:
            return signed(key, n, {"ok": 1, "found": 0})
        return signed(key, n, {"ok": 1, "found": 1, "at": found["at"], "from": found["from"], "data": found["data"]})

    @app.post("/api/v1/dev/cfg/list")
    async def dev_cfg_list(request: Request):
        dev, key, n, raw = await verified(request, 64)
        listed = request.app.state.db.list_cfg(dev, cfg_int(cfg_req(raw), "c", 0xFFFFFFFF))
        return signed(key, n, {"ok": 1, **listed})

    @app.post("/api/v1/dev/cfg/del")
    async def dev_cfg_del(request: Request):
        dev, key, n, raw = await verified(request, 64)
        req = cfg_req(raw)
        pick = cfg_int(req, "i", 2 ** 62)
        if pick is None and cfg_int(req, "all", 1) != 1:
            raise HTTPException(status_code=400, detail="body")
        return signed(key, n, {"ok": 1, "n": request.app.state.db.delete_cfg(dev, pick)})

    def web_device(request: Request, slug: str | None) -> dict:
        db: Db = request.app.state.db
        for d in db.devices():
            if d["detached"] or not (d["parts"] & P_WEB):
                continue
            if slug is None or dev_slug(d["id"]) == slug:
                return d
        raise HTTPException(status_code=404, detail="Сайт на сервері не увімкнено")

    def page(request: Request, slug: str | None):
        web_device(request, slug)
        return HTMLResponse((STATIC / "page.html").read_text(encoding="utf-8"), headers={"Cache-Control": "no-cache"})

    def data(request: Request, slug: str | None):
        d = web_device(request, slug)
        db: Db = request.app.state.db
        now = time.time()
        db.set_fields(d["id"], viewed_at=now)
        snap = db.snapshot(d["id"])
        pw = int(d["state"].get("pw", 1) or 0)
        if not snap or now - (d["last_seen"] or 0) > offline_after(d["hb_on"], d["hb_off"], pw):
            return JSONResponse({"ok": 0}, status_code=503, headers={"Cache-Control": "no-store"})
        j = json.loads(snap)
        if d["parts"] & P_HIST:
            j["wkimg"] = 1
        if "l" in request.query_params:
            j.pop("vs", None)
            j.pop("sp", None)
            j["lite"] = 1
        return JSONResponse(j, headers={"Cache-Control": "no-store"})

    def widget_data(request: Request, slug: str | None):
        d = web_device(request, slug)
        db: Db = request.app.state.db
        now = time.time()
        db.set_fields(d["id"], viewed_at=now)
        snap = db.snapshot(d["id"])
        pw = int(d["state"].get("pw", 1) or 0)
        if not snap or now - (d["last_seen"] or 0) > offline_after(d["hb_on"], d["hb_off"], pw):
            return JSONResponse({"ok": 0}, status_code=503, headers={"Cache-Control": "no-store"})
        age = now - (d.get("snapshot_at") or now)
        return JSONResponse(widget_build(json.loads(snap), now, cfg.tz, age), headers={"Cache-Control": "no-store"})

    def widget_script(request: Request, slug: str | None):
        web_device(request, slug)
        return Response((STATIC / "widget.js").read_text(encoding="utf-8"), media_type="text/javascript",
                        headers={"Cache-Control": "no-cache"})

    def widget_page(request: Request, slug: str | None):
        web_device(request, slug)
        base = str(request.base_url).rstrip("/")
        proto = request.headers.get("x-forwarded-proto", "").split(",")[0].strip().lower()
        if base.startswith("http://") and (proto == "https" or '"scheme":"https"' in request.headers.get("cf-visitor", "").replace(" ", "")):
            base = "https://" + base[7:]
        base += "/" + (f"d/{slug}/" if slug else "")
        text = (STATIC / "widget_install.html").read_text(encoding="utf-8")
        loader = WIDGET_LOADER.replace("{BASE}", base)
        html = text.replace("{LOADER}", loader.replace("&", "&amp;").replace("<", "&lt;"))
        return HTMLResponse(html, headers={"Cache-Control": "no-cache"})

    async def site_week(request: Request, slug: str | None):
        d = web_device(request, slug)
        if not d["parts"] & P_HIST:
            raise HTTPException(status_code=404)
        now = time.time()
        png = weeks.site_png(d["id"], now)
        if png is None:
            png, _ = await asyncio.to_thread(week_png, request.app.state.db, d, now)
            weeks.put_site(d["id"], png, now)
        return Response(content=png, media_type="image/png", headers={"Cache-Control": "no-store"})

    def refresh(request: Request, slug: str | None):
        d = web_device(request, slug)
        request.app.state.db.set_fields(d["id"], viewed_at=time.time())
        return {"ok": 1, "updated": 0}

    def icon():
        return Response((STATIC / "favicon.png").read_bytes(), media_type="image/png",
                        headers={"Cache-Control": "public, max-age=604800"})

    @app.get("/")
    def root_page(request: Request):
        return page(request, None)

    @app.get("/api/data")
    def root_data(request: Request):
        return data(request, None)

    @app.get("/api/voltage/refresh")
    def root_refresh(request: Request):
        return refresh(request, None)

    @app.get("/d/{slug}/")
    def dev_page(request: Request, slug: str):
        return page(request, slug)

    @app.get("/d/{slug}/api/data")
    def dev_page_data(request: Request, slug: str):
        return data(request, slug)

    @app.get("/d/{slug}/api/voltage/refresh")
    def dev_refresh(request: Request, slug: str):
        return refresh(request, slug)

    @app.get("/api/widget")
    def root_widget_data(request: Request):
        return widget_data(request, None)

    @app.get("/widget.js")
    def root_widget_script(request: Request):
        return widget_script(request, None)

    @app.get("/widget")
    def root_widget_page(request: Request):
        return widget_page(request, None)

    @app.get("/d/{slug}/api/widget")
    def dev_widget_data(request: Request, slug: str):
        return widget_data(request, slug)

    @app.get("/d/{slug}/widget.js")
    def dev_widget_script(request: Request, slug: str):
        return widget_script(request, slug)

    @app.get("/d/{slug}/widget")
    def dev_widget_page(request: Request, slug: str):
        return widget_page(request, slug)

    @app.get("/week.png")
    async def root_week(request: Request):
        return await site_week(request, None)

    @app.get("/d/{slug}/week.png")
    async def dev_week(request: Request, slug: str):
        return await site_week(request, slug)

    @app.get("/w/test.png")
    async def week_test():
        now = time.time()
        events, plans = weekimg.demo(now, cfg.tz)
        week = weekimg.build(events, plans, now, int(now), cfg.tz, "10.1")
        png = await asyncio.to_thread(weekimg.render, week)
        return Response(content=png, media_type="image/png", headers={"Cache-Control": "no-store"})

    @app.get("/favicon.png")
    @app.get("/favicon.ico")
    @app.get("/d/{slug}/favicon.png")
    @app.get("/d/{slug}/favicon.ico")
    def favicon():
        return icon()

    @app.get("/i/{name}")
    async def schedule_image(name: str):
        headers = {"Cache-Control": "public, max-age=86400"}
        if name == "test.png":
            png = imgs.cached("test")
            if png is None:
                png = await asyncio.to_thread(render, image_params(DEMO), cfg.tz)
                imgs.put_png("test", png)
            return Response(content=png, media_type="image/png", headers={"Cache-Control": "no-store"})
        key = name[:-4] if name.endswith(".png") else ""
        if len(key) != 16 or any(ch not in "0123456789abcdef" for ch in key):
            raise HTTPException(status_code=404)
        png = imgs.cached(key)
        if png is None:
            params = imgs.find(key, all_schedules)
            if params is None:
                raise HTTPException(status_code=404)
            png = await asyncio.to_thread(render, params, cfg.tz)
            imgs.put_png(key, png)
        return Response(content=png, media_type="image/png", headers=headers)

    @app.get("/lw-check")
    def lw_check():
        return PlainTextResponse("light-watcher-ok")

    return app


app = create_app()
