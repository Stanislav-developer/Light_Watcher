import json
import sys
import time
import urllib.error
import urllib.request

from .config import Settings
from .schedimg import DEMO, image_params, render
from .telegram import largest_file_id, multipart_photo

API = "https://api.telegram.org/bot{}/{}"


def _call(token: str, method: str, body: bytes, ctype: str) -> dict:
    req = urllib.request.Request(API.format(token, method), data=body, headers={"Content-Type": ctype})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        try:
            return json.loads(e.read().decode())
        except ValueError:
            return {"ok": False, "description": f"HTTP {e.code}"}
    except Exception as e:
        return {"ok": False, "description": type(e).__name__}


def by_url(token: str, chat: str, url: str) -> dict:
    body = json.dumps({"chat_id": chat, "photo": url, "disable_notification": True,
                       "caption": "🖼 Перевірка 1: Telegram сам забрав картинку з сервера за посиланням."}).encode()
    return _call(token, "sendPhoto", body, "application/json")


def by_upload(token: str, chat: str, data: bytes, caption: str) -> dict:
    body, ctype = multipart_photo({"chat_id": chat, "disable_notification": "true", "caption": caption}, data)
    return _call(token, "sendPhoto", body, ctype)


def by_file_id(token: str, chat: str, data: bytes, store: str) -> tuple[bool, str]:
    r = by_upload(token, store, data, "🖼 Тимчасове вивантаження — зараз буде видалено.")
    if not r.get("ok"):
        return False, f"вивантаження: {r.get('description', 'невідома помилка')}"
    res = r.get("result") or {}
    fid = largest_file_id(r)
    if not fid:
        return False, "Telegram не повернув file_id"
    d = _call(token, "deleteMessage", json.dumps({"chat_id": store, "message_id": res.get("message_id")}).encode(),
              "application/json")
    if not d.get("ok"):
        return False, f"видалення: {d.get('description', 'невідома помилка')}"
    body = json.dumps({"chat_id": chat, "photo": fid, "disable_notification": True,
                       "caption": "🖼 Перевірка 2: фото за номером (file_id), тимчасове вивантаження вже видалено."}).encode()
    r = _call(token, "sendPhoto", body, "application/json")
    if not r.get("ok"):
        return False, f"надсилання за номером: {r.get('description', 'невідома помилка')}"
    return True, ""


def main(argv: list[str]) -> int:
    if len(argv) != 1 or not argv[0].startswith("http://") and not argv[0].startswith("https://"):
        print("Як запустити: python -m app.checkphoto http://IP:8000")
        return 2
    cfg = Settings.from_env()
    if not cfg.bot_token or not cfg.owner_id:
        print("❌ Немає LW_BOT_TOKEN або LW_OWNER_ID у .env сервера.")
        return 2
    url = argv[0].rstrip("/") + f"/i/test.png?t={int(time.time())}"
    print(f"Перевірка 1: sendPhoto за посиланням {url}")
    r = by_url(cfg.bot_token, cfg.owner_id, url)
    url_ok = bool(r.get("ok"))
    print("✅ 1: Telegram забрав картинку за посиланням." if url_ok else
          f"❌ 1: Telegram не забрав картинку: {r.get('description', 'невідома помилка')}")
    print("Перевірка 2: вивантажити, видалити, надіслати за номером (file_id)")
    store = cfg.store_chat or cfg.owner_id
    print(f"Сховище картинок: {'канал ' + cfg.store_chat if cfg.store_chat else 'чат власника (LW_STORE_CHAT не задано)'}")
    fid_ok, why = by_file_id(cfg.bot_token, cfg.owner_id, render(image_params(DEMO), cfg.tz), store)
    print("✅ 2: фото за номером працює." if fid_ok else f"❌ 2: {why}")
    return 0 if url_ok or fid_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
