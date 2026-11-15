import json
import sys
import time

from . import weekimg
from .checkphoto import _call, by_upload
from .config import Settings
from .telegram import largest_file_id


def demo_png(now: float, tz: str, group: str) -> bytes:
    events, plans = weekimg.demo(now, tz)
    return weekimg.render(weekimg.build(events, plans, now, int(now), tz, group))


def run(token: str, chat: str, store: str, tz: str) -> tuple[bool, list[str]]:
    out = []
    now = time.time()
    r = by_upload(token, chat, demo_png(now, tz, "10.1"), "📈 Перевірка: картинка тижня, крок 1 — надіслано.")
    if not r.get("ok"):
        return False, out + [f"❌ 1: надіслати фото: {r.get('description', 'невідома помилка')}"]
    mid = (r.get("result") or {}).get("message_id")
    out.append("✅ 1: фото надіслано.")
    p = _call(token, "pinChatMessage", json.dumps({"chat_id": chat, "message_id": mid, "disable_notification": True}).encode(),
              "application/json")
    if not p.get("ok"):
        return False, out + [f"❌ 2: закріпити: {p.get('description', 'невідома помилка')}"]
    out.append("✅ 2: закріплено без сповіщення.")
    u = by_upload(token, store, demo_png(now + 3600, tz, ""), "📈 Тимчасове вивантаження — зараз буде видалено.")
    fid = largest_file_id(u)
    if not fid:
        return False, out + [f"❌ 3: вивантажити нову картинку: {u.get('description', 'немає file_id')}"]
    _call(token, "deleteMessage", json.dumps({"chat_id": store, "message_id": (u.get("result") or {}).get("message_id")}).encode(),
          "application/json")
    media = {"type": "photo", "media": fid, "caption": "📈 Перевірка: картинка тижня, крок 3 — фото замінено за номером."}
    e = _call(token, "editMessageMedia", json.dumps({"chat_id": chat, "message_id": mid, "media": media}).encode(),
              "application/json")
    if not e.get("ok"):
        return False, out + [f"❌ 3: замінити фото: {e.get('description', 'невідома помилка')}"]
    out.append("✅ 3: фото в тому самому повідомленні замінено (editMessageMedia за file_id).")
    again = _call(token, "editMessageMedia", json.dumps({"chat_id": chat, "message_id": mid, "media": media}).encode(),
                  "application/json")
    same = again.get("ok") or "not modified" in str(again.get("description", ""))
    out.append("✅ 4: повторна заміна тим самим фото — «not modified», як очікує прошивка." if same else
               f"⚠️ 4: повторна заміна: {again.get('description', 'невідома відповідь')}")
    c = _call(token, "getChat", json.dumps({"chat_id": chat}).encode(), "application/json")
    pinned = ((c.get("result") or {}).get("pinned_message") or {}).get("message_id")
    ok = pinned == mid
    out.append("✅ 5: після заміни повідомлення лишилося закріпленим." if ok else
               f"❌ 5: закріплене повідомлення інше ({pinned}), очікувалося {mid}.")
    _call(token, "unpinChatMessage", json.dumps({"chat_id": chat, "message_id": mid}).encode(), "application/json")
    out.append("Перевірочне повідомлення відкріплено, його можна видалити.")
    return ok, out


def main(argv: list[str]) -> int:
    cfg = Settings.from_env()
    if not cfg.bot_token or not cfg.owner_id:
        print("❌ Немає LW_BOT_TOKEN або LW_OWNER_ID у .env сервера.")
        return 2
    store = cfg.store_chat or cfg.owner_id
    print(f"Перевірка картинки тижня в чаті власника. Сховище: {'канал ' + cfg.store_chat if cfg.store_chat else 'чат власника'}")
    ok, lines = run(cfg.bot_token, cfg.owner_id, store, cfg.tz)
    for line in lines:
        print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
