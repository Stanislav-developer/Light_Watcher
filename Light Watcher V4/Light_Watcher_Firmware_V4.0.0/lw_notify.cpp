#include "lw.h"

static String pendingOnText = "";
static String pendingOnNote = "";
static unsigned long blipEdge = 0;
static String blipKey = "";
static int32_t blipMid[MAX_CHATS + 1];
static int32_t blipEditMid[MAX_CHATS + 1];
static uint8_t blipEditTries[MAX_CHATS + 1];
static String blipEditText = "";
static unsigned long blipEditNextMs = 0;
static unsigned long blipLogMs[BLIP_LOG_MAX];
static uint16_t blipLogSec[BLIP_LOG_MAX];
static uint8_t blipLogHead = 0;
static uint8_t blipLogCount = 0;

String powerOffText(const String& timeStr, const String& onDuration, const String& scheduleLine) {
  String m = "🔴 <b>" + (timeStr.length() > 0 ? timeStr + " " : String("")) + "Світло зникло</b>";
  m += onDuration.length() > 0 ? "\n🕓 Воно було " + onDuration : String("\n🕓 Скільки воно було - невідомо");
  if (scheduleLine.length() > 0) m += "\n" + scheduleLine;
  return m;
}

String powerOnText(const String& timeStr, const String& durPart, const String& scheduleLine) {
  String m = "🟢 <b>" + (timeStr.length() > 0 ? timeStr + " " : String("")) + "Світло з'явилося</b>";
  m += "\n🕓 Його не було" + durPart;
  if (scheduleLine.length() > 0) m += "\n" + scheduleLine;
  return m;
}

void sendPendingOnMessage(float v) {
  pendingOnMessage = false;
  bool safe = false;
  bool sentDirect = false;

  if (WiFi.status() != WL_CONNECTED || mqCount > 0 || !telegramOnline || tgSendAttemptedThisPass) {
    String note = pendingOnNote;
    if (WiFi.status() != WL_CONNECTED) {
      note = (note.length() > 0 ? note + "\n" : String("")) + "📵 Мережа WiFi була відсутня";
    }
    safe = mqPush(String("N") + pendingOnText + MQ_SEP + note);
  } else {
    String message = withVoltageLine(pendingOnText, v);
    if (pendingOnNote.length() > 0) {
      message += "\n\n" + pendingOnNote;
    }
    uint16_t tried = 0;
    const uint16_t left = tgNetAllowed() ? broadcastNotifyTo(message, RCPT_ALL, 1, &tried) : RCPT_ALL;
    if (left == 0) {
      Serial.println("Повідомлення про відновлення надіслано");
      safe = true;
      sentDirect = true;
    } else {
      const String item = String("N") + pendingOnText + MQ_SEP + pendingOnNote;
      safe = mqPush(item);
      if (left != RCPT_ALL) mqSetHeadPending(item, left, tried, message);
    }
  }

  if (safe) {

    if (sentDirect) {
      pendRelease(pendSlot);
    }
  } else if (pendSlot >= 0) {
    logEvent(LC_STORE, LS_ERR, "Поява світла: черга не записана, є копія");
  } else {
    logEvent(LC_STORE, LS_ERR, "Поява світла: флеш недоступна, лише RAM");
  }
  pendSlot = -1;
  pendSaveFailed = false;
}

void startPendingOnMessage(const String& text, const String& note) {
  pendingOnText = text;
  pendingOnNote = note;
  pendingOnSince = millis();

  String item = String("N") + text + MQ_SEP + note;
  pendSlot = -1;
  pendSaveFailed = !pendStore(item);
  pendingOnMessage = true;
  pendSaveLastMs = millis();
  if (pendSaveFailed) {
    logEvent(LC_STORE, LS_ERR, "Поява світла: копія не записана, повтор");
  }
}
void processBatteryRecovery(bool force) {
  if (!batteryRecPending) {
    return;
  }

  if (pendingOnMessage) {
    batteryRecPending = false;
    batteryRecReleaseMarker();
    return;
  }

  bool timeOk = statsNowEpoch() != 0;

  bool onlineLong = WiFi.status() == WL_CONNECTED && wifiUpSinceMs != 0 &&
                    millis() - wifiUpSinceMs >= 120000UL && telegramOnline;
  if (!timeOk && !force && !onlineLong && millis() - batteryRecMs < 3600000UL) {
    return;
  }

  const uint32_t nowEpoch = statsNowEpoch();
  const uint32_t bootEpoch = nowEpoch > uptimeSec() ? nowEpoch - uptimeSec() : 0;
  const String onTime = bootEpoch >= EPOCH_MIN_VALID ? "≈" + formatEpochLocal((time_t)bootEpoch, "%H:%M") : String("");
  const String durPart = (bootEpoch >= EPOCH_MIN_VALID && powerOffTimestamp >= (time_t)EPOCH_MIN_VALID && (time_t)bootEpoch > powerOffTimestamp)
    ? " ≈" + formatShortDuration((unsigned long)(bootEpoch - (uint32_t)powerOffTimestamp))
    : String(": невідомо");
  String message = powerOnText(onTime, durPart, "");
  lastOffDurationText = "";
  preferences.putString("lastOffDur", lastOffDurationText);
  const esp_reset_reason_t rr = esp_reset_reason();
  String note = (rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT)
    ? String("⚠️ Заряд акумулятора був критичний та пристрій вимкнувся")
    : "🔄 Світло з'явилося, поки пристрій перезапускався (" + resetReasonText() + ")";

  startPendingOnMessage(message, note);
  batteryRecPending = false;
  batteryRecReleaseMarker();
}

void batteryRecReleaseMarker() {
  if (!pendSaveFailed && prefPutBoolVerified("lastUotDetect", false)) {
    lastOutageDetect = false;
    outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_FLAG_DIRTY;
    Serial.println("Відновлення після розряду: повідомлення збережено, маркер очищено");
  } else {
    lastOutageDetect = true;
    outageMarkersDirty |= OUTAGE_MARKER_FLAG_DIRTY;
    Serial.println("Відновлення після розряду: маркер не очищено, повтор пізніше");
  }
}

void processPendingOnMessage() {
  if (!pendingOnMessage || powerChangePending()) {
    return;
  }

  if (pendSaveFailed && millis() - pendSaveLastMs >= SHELLY_RETRY_MS) {
    pendSaveLastMs = millis();
    pendSaveFailed = !pendStore(String("N") + pendingOnText + MQ_SEP + pendingOnNote);
    if (!pendSaveFailed) {
      logEvent(LC_STORE, LS_INFO, "«Світло з'явилося»: резервну копію записано");
    }
  }

  static unsigned long lastTry = 0;
  if (lastTry != 0 && millis() - lastTry < SHELLY_RETRY_MS) {
    return;
  }
  lastTry = millis();

  const bool wantVolt = voltInLightWanted();
  float v = NAN;
  if (wantVolt) {
    shellyTimeoutMs = SHELLY_PEND_TIMEOUT_MS;
    v = getShellyVoltage();
    shellyTimeoutMs = SHELLY_TIMEOUT_MS;
  }
  bool timeout = (millis() - pendingOnSince) >= SHELLY_WAIT_MAX_MS;

  if (!isnan(v) || !wantVolt || timeout || WiFi.status() != WL_CONNECTED) {
    sendPendingOnMessage(v);
    lastTry = 0;
  }
}

String blipDurText(unsigned long sec) {
  if (sec < 1) sec = 1;
  if (sec < 60) return String(sec) + " с";
  return String(sec / 60) + " хв" + (sec % 60 ? " " + String(sec % 60) + " с" : String(""));
}

String blipText(const String& timeStr, unsigned long sec) {
  return "⚡ <b>" + (timeStr.length() > 0 ? timeStr + " " : String("")) + "Світло зникало на " + blipDurText(sec) + "</b> · уже є";
}

String powerOffMessage(unsigned long edgeMs) {
  String onDuration = formatShortDuration((edgeMs - powerOnSinceMs) / 1000);
  if (powerOnSinceBoot) onDuration += " (від запуску пристрою)";
  const String when = statsNowEpoch() != 0 ? htmlEsc(shortTimeAt(edgeMs)) : timeToken(edgeMs);
  return powerOffText(when, htmlEsc(onDuration), yasnoPowerEventText(false));
}

static void blipTrackClear() {
  blipEdge = 0;
  blipKey = "";
  powerHoldEdge(0);
  for (int i = 0; i <= MAX_CHATS; i++) blipMid[i] = 0;
}

static void blipSendOff(unsigned long edgeMs) {
  processBatteryRecovery(true);
  if (pendingOnMessage) sendPendingOnMessage(NAN);
  blipTrackClear();
  blipEdge = edgeMs;
  powerHoldEdge(edgeMs);
  powerOffShortTime = shortTimeAt(edgeMs);
  const String item = String("F") + powerOffMessage(edgeMs);
  blipKey = mqEventKey(item);
  if (statsNowEpoch() != 0) {
    sendOrQueue(item);
  } else {
    mqPush(item);
  }
}

void blipNoteSent(const String& key, uint16_t delivered) {
  if (blipEdge == 0 || delivered == 0 || key != blipKey) return;
  int slot = MAX_CHATS;
  if (!(delivered & RCPT_OWNER)) {
    slot = 0;
    while (slot < MAX_CHATS && !(delivered & (1U << slot))) slot++;
  }
  if (slot <= MAX_CHATS) blipMid[slot] = tgLastMsgId;
}

static void blipResolve(unsigned long offMs, unsigned long onMs) {
  const unsigned long sec = (onMs - offMs + 500UL) / 1000UL;
  blipBootCount++;
  blipLogMs[blipLogHead] = offMs;
  blipLogSec[blipLogHead] = (uint16_t)(sec > 0xFFFF ? 0xFFFF : sec);
  blipLogHead = (uint8_t)((blipLogHead + 1) % BLIP_LOG_MAX);
  if (blipLogCount < BLIP_LOG_MAX) blipLogCount++;
  logEvent(LC_ADC, LS_INFO, "Коротке зникнення світла: " + blipDurText(sec));
  srvEventAt(SRV_EV_BLIP, offMs, (uint32_t)sec);
  const bool timeKnown = statsNowEpoch() != 0;
  const bool tracked = blipEdge != 0 && blipEdge == offMs;
  if (tracked) mqDropEvent(blipKey);
  bool ownerHas = false;
  if (tracked) {
    blipEditText = blipText(timeKnown ? shortTimeAt(offMs) : String(""), sec);
    blipEditNextMs = 0;
    for (int i = 0; i <= MAX_CHATS; i++) {
      blipEditMid[i] = blipMid[i];
      blipEditTries[i] = 0;
    }
    ownerHas = blipMid[MAX_CHATS] != 0;
  }
  blipTrackClear();
  if (!ownerHas && chatAllows(chatId, CHAT_F_NOTIFY)) {
    const String item = String("B") + blipText(timeKnown ? shortTimeAt(offMs) : timeToken(offMs), sec);
    if (timeKnown) {
      sendOrQueue(item);
    } else {
      mqPush(item);
    }
  }
}

bool blipFilter() {
  if (messageFlag || !blipOn) return false;
  for (uint8_t guard = 0; guard < POWER_EV_MAX; guard++) {
    unsigned long fromMs = 0, toMs = 0;
    const uint8_t r = powerBlipCheck((unsigned long)BLIP_LIMIT_SEC * 1000UL, fromMs, toMs);
    if (r == BLIP_TAKEN) {
      blipResolve(fromMs, toMs);
      continue;
    }
    if (r == BLIP_HOLD) {
      const unsigned long edge = powerEdgeMs();
      if (blipEdge != edge) blipSendOff(edge);
      return true;
    }
    return r != BLIP_REAL;
  }
  return true;
}

bool blipHolding() {
  return blipEdge != 0;
}

bool blipOffNotified(unsigned long edgeMs) {
  const bool sent = blipEdge != 0 && blipEdge == edgeMs;
  blipTrackClear();
  return sent;
}

void blipTick() {
  if (tgSendAttemptedThisPass || WiFi.status() != WL_CONNECTED || powerChangePending()) return;
  if (blipEditNextMs != 0 && (long)(millis() - blipEditNextMs) < 0) return;
  for (int i = 0; i <= MAX_CHATS; i++) {
    if (blipEditMid[i] == 0) continue;
    const bool owner = i == MAX_CHATS;
    if (!owner && (!chats.e[i].used || chats.e[i].status == 0)) {
      blipEditMid[i] = 0;
      continue;
    }
    if (!tgNetAllowed()) return;
    tgSendAttemptedThisPass = true;
    if (tgEditText(owner ? chatId : String(chats.e[i].id), blipEditMid[i], blipEditText, "HTML", "")) {
      blipEditMid[i] = 0;
      blipEditNextMs = 0;
    } else if (++blipEditTries[i] >= BLIP_EDIT_TRIES) {
      blipEditMid[i] = 0;
      logEvent(LC_TG, LS_WARN, "Коротке зникнення: правку не надіслано");
    } else {
      blipEditNextMs = millis() + BLIP_EDIT_RETRY_MS;
    }
    return;
  }
}

String blipJson() {
  const unsigned long now = millis();
  const uint32_t nowEpoch = statsNowEpoch();
  uint16_t day = 0;
  String l = "";
  for (uint8_t i = 0; i < blipLogCount; i++) {
    const uint8_t idx = (uint8_t)((blipLogHead + BLIP_LOG_MAX - 1 - i) % BLIP_LOG_MAX);
    const unsigned long ago = now - blipLogMs[idx];
    if (ago < BLIP_DAY_MS) day++;
    const uint32_t agoSec = (uint32_t)(ago / 1000UL);
    const uint32_t t = (nowEpoch != 0 && nowEpoch > agoSec) ? nowEpoch - agoSec : 0;
    l += String(i ? "," : "") + "[" + String(t) + "," + String(blipLogSec[idx]) + "]";
  }
  return "{\"on\":" + String(blipOn ? 1 : 0) + ",\"lim\":" + String(BLIP_LIMIT_SEC) + ",\"d\":" + String(day)
         + ",\"n\":" + String(blipBootCount) + ",\"l\":[" + l + "]}";
}

bool voltInLightWanted() {
  if (!shellyConfigured()) return false;
  if (ownerNotifyOn && ownerVoltInLightOn) return true;
  for (int i = 0; i < MAX_CHATS; i++) {
    const ChatEntry& c = chats.e[i];
    if (c.used && c.status != 0 && c.canPost && chatApproved(c) && (c.flags & CHAT_F_NOTIFY) && (c.flags & CHAT_F_VOLT_IN_LIGHT)) return true;
  }
  return false;
}
