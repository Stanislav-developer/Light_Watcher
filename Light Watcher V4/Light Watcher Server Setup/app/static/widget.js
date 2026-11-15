(async () => {
  const BASE = typeof U === "string" ? U : "";
  const C = {
    bg: new Color("#1b1f1c"), fg: new Color("#f2f5f2"), sub: new Color("#c4ccc5"), lbl: new Color("#9aa49c"),
    tick: new Color("#7d867f"), ok: new Color("#34c77b"), bad: new Color("#ff5a4e"), off: new Color("#8a918b"),
    warn: new Color("#e0b252"), past: new Color("#000000", 0.55), now: new Color("#ffffff")
  };
  const STALE_SEC = 1200;
  const DAY = 1440;
  const fm = FileManager.local();
  const cachePath = fm.joinPath(fm.documentsDirectory(), "lw-widget-cache.json");
  const nowSec = () => Date.now() / 1000;
  const pad = (n) => String(n).padStart(2, "0");

  async function load() {
    try {
      const r = new Request(BASE + "api/widget");
      r.timeoutInterval = 15;
      const j = await r.loadJSON();
      if (r.response && r.response.statusCode == 200 && j && j.ok) {
        const live = { j: j, at: nowSec() };
        try { fm.writeString(cachePath, JSON.stringify(live)); } catch (e) {}
        return { j: j, at: live.at, live: true };
      }
    } catch (e) {}
    try {
      if (fm.fileExists(cachePath)) {
        const c = JSON.parse(fm.readString(cachePath));
        if (c && c.j && c.at) return { j: c.j, at: c.at, live: false };
      }
    } catch (e) {}
    return null;
  }

  function dur(sec) {
    let m = Math.max(1, Math.floor(sec / 60));
    if (m < 60) return m + " хв";
    let h = Math.floor(m / 60);
    m = m % 60;
    if (h >= 24) return Math.floor(h / 24) + " д " + (h % 24) + " год";
    return m ? h + " год " + m + " хв" : h + " год";
  }

  function clock(sec) {
    const m = Math.max(0, Math.floor(sec / 60));
    return Math.floor(m / 60) + ":" + pad(m % 60);
  }

  function hm(min) {
    const m = ((Math.floor(min) % DAY) + DAY) % DAY;
    return pad(Math.floor(m / 60)) + ":" + pad(m % 60);
  }

  function at(epoch) {
    const d = new Date(epoch * 1000);
    return pad(d.getHours()) + ":" + pad(d.getMinutes());
  }

  function view(data) {
    if (!data) return { stale: true, head: "Немає даних", big: "—", sub: "сервер не відповідає", foot: "Сервер не відповідає" };
    const j = data.j, t = nowSec(), shift = (t - data.at) / 60;
    if ((j.age || 0) + (t - data.at) > STALE_SEC) {
      const from = at(data.at - (j.age || 0));
      return { stale: true, head: "Немає зв'язку", big: "Дані від " + from, sub: "пристрій або сервер не відповідає", foot: "Немає зв'язку · дані від " + from };
    }
    let nowMin = (j.nm || 0) + shift;
    let today = j.today || [], tomorrow = j.tomorrow || [], tm = !!j.tm, nx = j.nx;
    let ys = !!j.ys;
    if (nowMin >= DAY) {
      nowMin -= DAY;
      today = tm ? tomorrow : [];
      ys = ys && tm;
      tomorrow = [];
      tm = false;
      nx = nx && nx.m >= DAY ? { k: nx.k, m: nx.m - DAY } : null;
    }
    const on = !!j.pw;
    const v = {
      stale: false, on: on, color: on ? C.ok : C.bad, ys: ys, today: today, tomorrow: tomorrow, tm: tm, nowMin: nowMin,
      volt: on && typeof j.volt === "number" ? Math.round(j.volt) + " В" : "",
      len: typeof j.since === "number" && j.since > 0 ? t - j.since : null
    };
    const state = on ? "Світло є" : "Світла немає";
    v.head = v.len != null ? state + " · " + dur(v.len) : state;
    v.short = v.len != null ? clock(v.len) : "—";
    if (nx && typeof nx.m === "number") {
      const rem = Math.max(0, nx.m - nowMin);
      const when = (nx.m >= DAY ? "завтра " : "") + hm(nx.m);
      const what = nx.k === "off" ? "відключення" : "світло за графіком";
      v.big = when;
      v.sub = rem < 1 ? what + " очікується" : what + " через " + dur(rem * 60);
      v.foot = "Далі: " + (nx.k === "off" ? "відключення о " : "світло за графіком о ") + when + (rem < 1 ? "" : ", через " + dur(rem * 60));
      v.next = (nx.k === "off" ? "відкл. " : "світло ") + when;
    } else if (ys) {
      v.big = on ? "Без відключень" : "Поза графіком";
      v.sub = on ? (tm ? "до кінця завтра за графіком" : "до кінця дня за графіком") : "за графіком світло мало бути";
      v.foot = on ? "Далі: відключень за графіком немає" : "Відключення поза графіком";
      v.next = on ? "без відключень" : "поза графіком";
    } else {
      v.big = v.len != null ? dur(v.len) : state;
      v.sub = "графіка YASNO немає";
      v.foot = "Графіка YASNO немає";
      v.next = "";
    }
    return v;
  }

  function bar(slots, w, h, nowMin) {
    const dc = new DrawContext();
    dc.size = new Size(w, h + 6);
    dc.opaque = false;
    dc.respectScreenScale = true;
    dc.setFillColor(C.ok);
    dc.fillRect(new Rect(0, 3, w, h));
    dc.setFillColor(C.bad);
    for (const s of slots) dc.fillRect(new Rect(s[0] / DAY * w, 3, (s[1] - s[0]) / DAY * w, h));
    if (nowMin != null) {
      dc.setFillColor(C.past);
      dc.fillRect(new Rect(0, 3, nowMin / DAY * w, h));
    }
    dc.setFillColor(C.bg);
    for (let i = 1; i < 24; i++) dc.fillRect(new Rect(i / 24 * w - 0.75, 3, 1.5, h));
    if (nowMin != null) {
      dc.setFillColor(C.now);
      dc.fillRect(new Rect(Math.max(0, Math.min(w - 2, nowMin / DAY * w - 1)), 0, 2, h + 6));
    }
    return dc.getImage();
  }

  function txt(stack, s, size, color, weight) {
    const t = stack.addText(s);
    t.font = weight === "b" ? Font.boldSystemFont(size) : weight === "s" ? Font.semiboldSystemFont(size) : Font.systemFont(size);
    t.textColor = color;
    t.lineLimit = 1;
    t.minimumScaleFactor = 0.6;
    return t;
  }

  function head(stack, v, size) {
    const h = stack.addStack();
    h.layoutHorizontally();
    h.centerAlignContent();
    txt(h, "●", size, v.stale ? C.off : v.color, "b");
    h.addSpacer(5);
    txt(h, v.head, size, C.fg, "b");
    return h;
  }

  function addBar(stack, slots, w, nowMin) {
    const img = stack.addImage(bar(slots, w, 12, nowMin));
    img.imageSize = new Size(w, 18);
  }

  function ticks(stack, w) {
    const r = stack.addStack();
    r.layoutHorizontally();
    r.size = new Size(w, 0);
    ["0", "6", "12", "18", "24"].forEach((s, i) => {
      if (i) r.addSpacer();
      txt(r, s, 9, C.tick, "");
    });
  }

  function small(w, v) {
    head(w, v, 12);
    w.addSpacer();
    if (!v.stale && v.ys) {
      addBar(w, v.today, 130, v.nowMin);
      ticks(w, 130);
      w.addSpacer();
    }
    txt(w, v.big, v.stale ? 16 : 19, C.fg, "b");
    txt(w, v.sub, 11.5, v.stale ? C.warn : C.sub, "").lineLimit = 2;
  }

  function medium(w, v) {
    const top = w.addStack();
    top.layoutHorizontally();
    top.centerAlignContent();
    head(top, v, 14);
    top.addSpacer();
    if (v.volt) txt(top, v.volt, 12, C.sub, "");
    w.addSpacer();
    if (v.stale) {
      txt(w, v.big, 22, C.fg, "b");
      txt(w, v.sub, 12.5, C.warn, "");
      return;
    }
    if (v.ys) {
      txt(w, "Сьогодні", 11, C.lbl, "s");
      addBar(w, v.today, 310, v.nowMin);
      w.addSpacer(4);
      if (v.tm) {
        txt(w, "Завтра", 11, C.lbl, "s");
        addBar(w, v.tomorrow, 310, null);
      } else {
        txt(w, "Завтра: графіка ще немає", 11, C.lbl, "s");
      }
      ticks(w, 310);
    } else {
      txt(w, v.big, 26, C.fg, "b");
    }
    w.addSpacer();
    txt(w, v.foot, 12.5, C.sub, "");
  }

  function circular(w, v) {
    w.addAccessoryWidgetBackground = true;
    const t1 = txt(w, v.stale ? "—" : v.short, 16, C.fg, "b");
    t1.centerAlignText();
    const t2 = txt(w, v.stale ? "немає зв'язку" : v.on ? "світло є" : "немає", 9, C.fg, "");
    t2.centerAlignText();
  }

  function rectangular(w, v) {
    txt(w, v.head, 14, C.fg, "b");
    const line = v.stale ? v.big : [v.next, v.volt].filter((s) => s).join(" · ");
    if (line) txt(w, line, 12, C.fg, "");
  }

  function inline(w, v) {
    const s = v.stale ? "Немає зв'язку" : (v.on ? "💡 " : "🕯 ") + v.short + (v.next ? " · " + v.next : "");
    txt(w, s, 12, C.fg, "");
  }

  function build(fam, v) {
    const w = new ListWidget();
    w.url = BASE;
    w.refreshAfterDate = new Date(Date.now() + 15 * 60 * 1000);
    if (fam === "accessoryCircular") circular(w, v);
    else if (fam === "accessoryRectangular") rectangular(w, v);
    else if (fam === "accessoryInline") inline(w, v);
    else {
      w.backgroundColor = C.bg;
      w.setPadding(14, 14, 14, 14);
      if (fam === "small") small(w, v);
      else medium(w, v);
    }
    return w;
  }

  const v = view(await load());
  const fam = config.widgetFamily || "medium";
  const w = build(fam, v);
  if (config.runsInWidget || config.runsInAccessoryWidget) Script.setWidget(w);
  else await w.presentMedium();
  Script.complete();
})();
