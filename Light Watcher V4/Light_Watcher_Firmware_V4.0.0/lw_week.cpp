#include "lw.h"

static String wkDone[MAX_CHATS + 1];
static uint8_t wkFails[MAX_CHATS + 1];
static unsigned long wkNextMs[MAX_CHATS + 1];
static unsigned long wkLastMs = 0;
static uint8_t wkCursor = 0;

static String wkKey(int i, char k) {
  return (i >= MAX_CHATS ? String("wkO") : "wk" + String(i)) + k;
}

static bool wkWantsSlot(int i) {
  if (i >= MAX_CHATS) return ownerWeekOn && chatId.length() > 0;
  const ChatEntry& c = chats.e[i];
  return c.used && c.status != 0 && c.canPost && chatApproved(c) && (c.pad[0] & CHAT_P0_WEEK) != 0 && !srvStoreIs(String(c.id));
}

bool wkWanted() {
  for (int i = 0; i <= MAX_CHATS; i++) {
    if (wkWantsSlot(i)) return true;
  }
  return false;
}

uint8_t wkStep(bool want, uint32_t mid, bool sameChat, bool ready, bool done) {
  if (mid != 0 && (!want || !sameChat)) return WK_DROP;
  if (!want || !ready) return WK_NONE;
  if (mid == 0) return WK_SEND;
  return done ? WK_NONE : WK_EDIT;
}

int8_t wkEditResult(const String& resp) {
  if (tgResultOk(resp) || resp.indexOf("message is not modified") >= 0) return 1;
  if (resp.indexOf("message to edit not found") >= 0 || resp.indexOf("MESSAGE_ID_INVALID") >= 0 ||
      resp.indexOf("message can't be edited") >= 0) {
    return -1;
  }
  return 0;
}

String wkEditJson(const String& chat, uint32_t mid, const String& fid, const String& caption) {
  return "{\"chat_id\":\"" + jsonEsc(chat) + "\",\"message_id\":" + String(mid) +
         ",\"media\":{\"type\":\"photo\",\"media\":\"" + jsonEsc(fid) + "\",\"caption\":\"" + jsonEscText(caption) +
         "\",\"parse_mode\":\"HTML\"}}";
}

static bool wkDue(int i) {
  return wkFails[i] == 0 || (long)(millis() - wkNextMs[i]) >= 0;
}

static void wkFailed(int i) {
  if (wkFails[i] < 6) wkFails[i]++;
  wkNextMs[i] = millis() + (60000UL << (wkFails[i] - 1));
}

void wkPublishTick() {
  if (WiFi.status() != WL_CONNECTED || !telegramOnline || tgSendAttemptedThisPass) return;
  if (wkLastMs && millis() - wkLastMs < WK_GAP_MS) return;
  String fid, ver, cap;
  const bool ready = srvWeekImg(fid, ver, cap);
  const bool on = srvWeekOn();
  for (int n = 0; n <= MAX_CHATS; n++) {
    const int i = (wkCursor + n) % (MAX_CHATS + 1);
    const String midKey = wkKey(i, 'm');
    const String cidKey = wkKey(i, 'c');
    const uint32_t mid = preferences.getUInt(midKey.c_str(), 0);
    const bool want = on && wkWantsSlot(i);
    if ((mid == 0 && !want) || !wkDue(i)) continue;
    const String saved = mid ? preferences.getString(cidKey.c_str(), "") : String("");
    const String target = want ? (i >= MAX_CHATS ? chatId : String(chats.e[i].id)) : saved;
    const uint8_t step = wkStep(want, mid, saved == target, ready, wkDone[i] == ver);
    if (step == WK_NONE) continue;
    tgSendAttemptedThisPass = true;
    wkLastMs = millis() ? millis() : 1;
    wkCursor = (uint8_t)((i + 1) % (MAX_CHATS + 1));
    if (step == WK_DROP) {
      if (saved.length() > 0 && !yasnoTelegramDeleteMessage(saved, mid)) yasnoTelegramPinMessage(saved, mid, false);
      preferences.remove(midKey.c_str());
      preferences.remove(cidKey.c_str());
      wkDone[i] = "";
      wkFails[i] = 0;
      logEvent(LC_TG, LS_INFO, "Тиждень: повідомлення прибрано");
      return;
    }
    if (step == WK_SEND) {
      if (!tgSendPhoto(target, fid, htmlEsc(cap), "HTML")) {
        wkFailed(i);
        if (i < MAX_CHATS) grpNoteFail(String(chats.e[i].title));
        return;
      }
      const uint32_t newId = (uint32_t)bot.last_sent_message_id;
      preferences.putString(cidKey.c_str(), target);
      preferences.putUInt(midKey.c_str(), newId);
      if (i >= MAX_CHATS) tgKeepAdd((int32_t)newId);
      if (!yasnoTelegramPinMessage(target, newId, true)) logEvent(LC_TG, LS_WARN, "Тиждень: не закріплено");
      wkDone[i] = ver;
      wkFails[i] = 0;
      logEvent(LC_TG, LS_INFO, "Тиждень: картинку надіслано");
      return;
    }
    const int8_t r = wkEditResult(telegramPostRaw("editMessageMedia", wkEditJson(target, mid, fid, htmlEsc(cap))));
    if (r == 1) {
      wkDone[i] = ver;
      wkFails[i] = 0;
    } else if (r < 0) {
      preferences.remove(midKey.c_str());
      wkDone[i] = "";
      logEvent(LC_TG, LS_WARN, "Тиждень: повідомлення зникло, нове");
    } else {
      wkFailed(i);
    }
    return;
  }
}
