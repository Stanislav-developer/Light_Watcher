#include "lw.h"

static String pendingOnText = "";
static String pendingOnNote = "";

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
    logEvent(LC_STORE, LS_ERR, "«Світло з'явилося»: запис черги не підтверджено - резервна копія збережена");
  } else {
    logEvent(LC_STORE, LS_ERR, "«Світло з'явилося»: флеш-пам'ять недоступна - повідомлення лише в оперативній пам'яті, повтор запису");
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
    logEvent(LC_STORE, LS_ERR, "«Світло з'явилося»: резервну копію не записано, повтор кожні 2 с");
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

  shellyTimeoutMs = SHELLY_PEND_TIMEOUT_MS;
  float v = getShellyVoltage();
  shellyTimeoutMs = SHELLY_TIMEOUT_MS;
  bool timeout = (millis() - pendingOnSince) >= SHELLY_WAIT_MAX_MS;

  if (!isnan(v) || !shellyConfigured() || timeout || WiFi.status() != WL_CONNECTED) {
    sendPendingOnMessage(v);
    lastTry = 0;
  }
}
