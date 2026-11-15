import asyncio
import json
import logging
import time
import urllib.error
import urllib.request
import uuid

log = logging.getLogger("lw.telegram")
DELETE_TRIES = 3
DELETE_PAUSE_SEC = 1.0
PENDING_MAX = 200
PENDING_GAP_SEC = 60
PENDING_GIVE_UP_SEC = 47 * 3600
RECOVER_WINDOW = 15

API = "https://api.telegram.org/bot{}/{}"


def multipart_photo(fields: dict, data: bytes) -> tuple[bytes, str]:
    b = uuid.uuid4().hex
    parts = [f'--{b}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n{v}\r\n'.encode() for k, v in fields.items()]
    parts.append(f'--{b}\r\nContent-Disposition: form-data; name="photo"; filename="schedule.png"\r\n'
                 f"Content-Type: image/png\r\n\r\n".encode() + data + b"\r\n")
    parts.append(f"--{b}--\r\n".encode())
    return b"".join(parts), f"multipart/form-data; boundary={b}"


def largest_file_id(resp: dict) -> str:
    photos = ((resp.get("result") or {}).get("photo") or []) if resp.get("ok") else []
    return str(photos[-1].get("file_id") or "") if photos else ""


class Notifier:
    def __init__(self, token: str, chat_id: str, store_chat: str = ""):
        self.token = token
        self.chat_id = chat_id
        self.store_chat = store_chat
        self.pending_del: list[dict] = []
        self.last_id: dict[str, int] = {}

    @property
    def enabled(self) -> bool:
        return bool(self.token and self.chat_id)

    def _post(self, method: str, body: bytes, ctype: str, timeout: int = 15) -> dict:
        req = urllib.request.Request(API.format(self.token, method), data=body, headers={"Content-Type": ctype})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())

    def _send(self, text: str) -> int | None:
        data = json.dumps({"chat_id": self.chat_id, "text": text, "parse_mode": "HTML",
                           "disable_web_page_preview": True}).encode()
        resp = self._post("sendMessage", data, "application/json")
        if resp.get("ok"):
            return int(resp["result"]["message_id"])
        return None

    async def send(self, text: str) -> int | None:
        if not self.enabled:
            return None
        try:
            return await asyncio.to_thread(self._send, text)
        except Exception:
            return None

    def _upload(self, png: bytes, store: str = "") -> str | None:
        chats = [c for c in (store or self.store_chat, self.chat_id) if c]
        for chat in dict.fromkeys(chats):
            try:
                fid = self._upload_to(chat, png)
            except Exception:
                fid = None
            if fid:
                return fid
        return None

    def _upload_to(self, chat: str, png: bytes) -> str | None:
        body, ctype = multipart_photo({"chat_id": chat, "disable_notification": "true"}, png)
        try:
            resp = self._post("sendPhoto", body, ctype, 60)
        except urllib.error.HTTPError as e:
            if e.code >= 500:
                self._recover(chat)
            raise
        except Exception:
            self._recover(chat)
            raise
        fid = largest_file_id(resp)
        if not fid:
            return None
        mid = (resp.get("result") or {}).get("message_id")
        if isinstance(mid, int):
            self.last_id[chat] = max(self.last_id.get(chat, 0), mid)
        if mid and not self._delete(chat, mid, DELETE_TRIES):
            self.pending_del.append({"chat": chat, "mid": mid, "at": time.time(), "next": time.time() + PENDING_GAP_SEC})
            del self.pending_del[:-PENDING_MAX]
        return fid

    def _delete(self, chat: str, mid: int, tries: int) -> bool:
        why = ""
        for k in range(tries):
            if k:
                time.sleep(DELETE_PAUSE_SEC * k)
            try:
                if self._post("deleteMessage", json.dumps({"chat_id": chat, "message_id": mid}).encode(),
                              "application/json").get("ok"):
                    return True
                why = "ok=false"
            except urllib.error.HTTPError as e:
                try:
                    why = f"HTTP {e.code} {e.read().decode()[:160]}"
                except Exception:
                    why = f"HTTP {e.code}"
                if e.code == 400 and "not found" in why:
                    return True
            except Exception as e:
                why = type(e).__name__
        log.warning("temporary photo %s in %s not deleted: %s", mid, chat, why)
        return False

    def _recover(self, chat: str) -> int:
        try:
            probe = self._post("sendMessage", json.dumps({"chat_id": chat, "text": "⏳", "disable_notification": True}).encode(),
                               "application/json")
            top = int((probe.get("result") or {})["message_id"])
        except Exception:
            log.warning("photo upload to %s timed out; cannot look for the leftover", chat)
            return 0
        self._delete(chat, top, 2)
        low = max(self.last_id.get(chat, 0), top - RECOVER_WINDOW - 1)
        self.last_id[chat] = max(self.last_id.get(chat, 0), top)
        found = 0
        for mid in range(top - 1, low, -1):
            try:
                fwd = self._post("forwardMessage", json.dumps({"chat_id": chat, "from_chat_id": chat, "message_id": mid,
                                                              "disable_notification": True}).encode(), "application/json")
            except Exception:
                continue
            res = fwd.get("result") or {}
            if res.get("message_id"):
                self._delete(chat, res["message_id"], 2)
            if res.get("photo") and not res.get("caption"):
                if not self._delete(chat, mid, DELETE_TRIES):
                    self.pending_del.append({"chat": chat, "mid": mid, "at": time.time(), "next": time.time() + PENDING_GAP_SEC})
                log.warning("deleted leftover temporary photo %s in %s after a timeout", mid, chat)
                found += 1
                break
        return found

    def retry_deletes(self, now: float | None = None) -> int:
        now = time.time() if now is None else now
        left = []
        for it in self.pending_del:
            if now < it["next"]:
                left.append(it)
            elif not self._delete(it["chat"], it["mid"], 1) and now - it["at"] < PENDING_GIVE_UP_SEC:
                it["next"] = now + min(PENDING_GAP_SEC * 2 ** min(int((now - it["at"]) // 600), 5), 3600)
                left.append(it)
        self.pending_del = left
        return len(left)

    async def upload_photo(self, png: bytes, store: str = "") -> str | None:
        if not self.enabled:
            return None
        try:
            return await asyncio.to_thread(self._upload, png, store)
        except Exception:
            return None
