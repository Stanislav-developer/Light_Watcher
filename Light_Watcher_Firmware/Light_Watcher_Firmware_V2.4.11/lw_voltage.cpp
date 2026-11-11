#include "lw.h"

static bool voltPolledOnce = false;
static uint8_t vmState = 0;
static uint8_t vmBadKind = 0;
static int vmBadCount = 0;
static int vmFailCount = 0;
static bool vmChecked = false;
static bool vmAlertedOnce = false;
static int vmSpkIdx = -1;
static VoltDay vDay = { 0, 0, 0, 0, 0, 0, 0 };
static bool vDayLoaded = false;

bool voltLogValid(const VoltLog& l) {
  return l.magic == VOLT_MAGIC && l.head < VOLT_SIZE && l.count <= VOLT_SIZE && l.crc == statsCrc32((const uint8_t*)&l, sizeof(VoltLog) - sizeof(uint32_t));
}

bool spkLogValid(const SpkLog& l) {
  return l.magic == SPK_MAGIC && l.head < SPK_SIZE && l.count <= SPK_SIZE && l.crc == statsCrc32((const uint8_t*)&l, sizeof(SpkLog) - sizeof(uint32_t));
}

bool voltReread() {
  VoltLog* l = (VoltLog*)malloc(sizeof(VoltLog));
  if (l == NULL) {
    return false;
  }
  uint8_t st = nvsReadState("voltV1", l, sizeof(VoltLog));
  if (st == 3) {
    free(l);
    return false;
  }
  if (st == 0 && voltLogValid(*l)) {
    int start = (voltLog.head + VOLT_SIZE - voltLog.count) % VOLT_SIZE;
    for (int i = 0; i < voltLog.count; i++) {
      int from = (start + i) % VOLT_SIZE;
      l->t[l->head] = voltLog.t[from];
      l->dv[l->head] = voltLog.dv[from];
      l->head = (l->head + 1) % VOLT_SIZE;
      if (l->count < VOLT_SIZE) l->count++;
    }
    voltLog = *l;
  }
  free(l);
  voltUnread = false;
  return true;
}
bool spkReread() {
  SpkLog* l = (SpkLog*)malloc(sizeof(SpkLog));
  if (l == NULL) {
    return false;
  }
  uint8_t st = nvsReadState("vSpkV1", l, sizeof(SpkLog));
  if (st == 3) {
    free(l);
    return false;
  }
  if (st == 0 && spkLogValid(*l)) {
    int start = (spkLog.head + SPK_SIZE - spkLog.count) % SPK_SIZE;
    int newIdx = -1;
    for (int i = 0; i < spkLog.count; i++) {
      int from = (start + i) % SPK_SIZE;
      l->t[l->head] = spkLog.t[from];
      l->dv[l->head] = spkLog.dv[from];
      l->lim[l->head] = spkLog.lim[from];
      if (from == vmSpkIdx) newIdx = l->head;
      l->head = (l->head + 1) % SPK_SIZE;
      if (l->count < SPK_SIZE) l->count++;
    }
    if (vmSpkIdx >= 0) vmSpkIdx = newIdx;
    spkLog = *l;
  }
  free(l);
  spkUnread = false;
  return true;
}

void voltSave() {

  if (voltUnread && !voltReread()) {
    voltSaveFailed = true;
    voltLastAttemptMs = millis();
    if (voltLastAttemptMs == 0) voltLastAttemptMs = 1;
    return;
  }
  voltDaySave();
  voltLog.magic = VOLT_MAGIC;
  voltLog.crc = statsCrc32((const uint8_t*)&voltLog, sizeof(VoltLog) - sizeof(uint32_t));
  voltLastAttemptMs = millis();
  if (voltLastAttemptMs == 0) voltLastAttemptMs = 1;
  voltSaveFailed = !nvsPutVerified("voltV1", &voltLog, sizeof(VoltLog));
  if (!voltSaveFailed) {
    voltDirty = false;
    voltDirtySinceMs = 0;
    voltLastSaveMs = millis();
    Serial.println(String("[VOLT] Історію напруги збережено: ") + String(voltLog.count) + " вимірювань");
  } else {
    Serial.println("[VOLT] Помилка збереження історії напруги; повтор через 60 с");
  }
}

void spkSave() {
  if (spkUnread && !spkReread()) {
    spkSaveOk = false;
    return;
  }
  spkLog.magic = SPK_MAGIC;
  spkLog.crc = statsCrc32((const uint8_t*)&spkLog, sizeof(SpkLog) - sizeof(uint32_t));
  spkSaveOk = nvsPutVerified("vSpkV1", &spkLog, sizeof(SpkLog));
}

bool voltThresholdsConsistent(int low, int high, int stableLow, int stableHigh) {
  return low <= stableLow && stableLow < stableHigh && stableHigh <= high;
}

void voltBegin() {

  voltDirty = false;
  voltSaveFailed = false;
  voltDirtySinceMs = 0;
  voltLastAttemptMs = 0;
  voltLastSaveMs = 0;
  voltLow = preferences.getInt("vLow", VOLT_LOW_DEFAULT);
  voltHigh = preferences.getInt("vHigh", VOLT_HIGH_DEFAULT);
  voltStableLow = preferences.getInt("vStLow", 220);
  voltStableHigh = preferences.getInt("vStHigh", 230);
  if (voltLow < 150 || voltLow > 240 || voltHigh < 220 || voltHigh > 280 || voltLow >= voltHigh) {
    logEvent(LC_STORE, LS_WARN, "Налаштування меж напруги некоректні; відновлено заводські аварійні межі");
    voltLow = VOLT_LOW_DEFAULT;
    voltHigh = VOLT_HIGH_DEFAULT;
    prefPutIntVerified("vLow", voltLow);
    prefPutIntVerified("vHigh", voltHigh);
  }
  if (voltStableLow < 150 || voltStableLow > 279 || voltStableHigh < 151 || voltStableHigh > 280 || voltStableLow >= voltStableHigh) {
    logEvent(LC_STORE, LS_WARN, "Налаштування стабільного діапазону напруги некоректні; відновлено значення за замовчуванням");
    voltStableLow = 220;
    voltStableHigh = 230;
    prefPutIntVerified("vStLow", voltStableLow);
    prefPutIntVerified("vStHigh", voltStableHigh);
  }
  if (!voltThresholdsConsistent(voltLow, voltHigh, voltStableLow, voltStableHigh)) {
    logEvent(LC_STORE, LS_WARN, "Стабільний діапазон напруги виходив за аварійні межі; відновлено значення за замовчуванням");
    voltLow = VOLT_LOW_DEFAULT; voltHigh = VOLT_HIGH_DEFAULT; voltStableLow = 220; voltStableHigh = 230;
    prefPutIntVerified("vLow", voltLow); prefPutIntVerified("vHigh", voltHigh);
    prefPutIntVerified("vStLow", voltStableLow); prefPutIntVerified("vStHigh", voltStableHigh);
  }
  memset(&voltLog, 0, sizeof(VoltLog));
  uint8_t st = nvsReadState("voltV1", &voltLog, sizeof(VoltLog));
  if (st == 0 && !voltLogValid(voltLog)) {
    st = 4;
  }
  if (st != 0) {
    memset(&voltLog, 0, sizeof(VoltLog));
    if (st != 1) {
      nvsNoteLoad("Історія напруги", st);
    }
    voltUnread = (st == 3);
  }
  memset(&spkLog, 0, sizeof(SpkLog));
  st = nvsReadState("vSpkV1", &spkLog, sizeof(SpkLog));
  if (st == 0 && !spkLogValid(spkLog)) {
    st = 4;
  }
  if (st != 0) {
    memset(&spkLog, 0, sizeof(SpkLog));
    if (st != 1) {
      nvsNoteLoad("Історія стрибків", st);
    }
    spkUnread = (st == 3);
  }
}

void voltSaveIfDirty() {
  if (voltDirty) {
    voltSave();
  }
}
bool webVoltageRefreshNow() {
  const unsigned long ms = millis();
  if (webVoltRefreshMs != 0 && ms - webVoltRefreshMs < 25000UL) return false;
  if (!shellyConfigured() || WiFi.status() != WL_CONNECTED || messageFlag) return false;
  webVoltRefreshMs = ms ? ms : 1;
  const unsigned long oldTimeout = shellyTimeoutMs;
  shellyTimeoutMs = SHELLY_BG_TIMEOUT_MS;
  const float v = getShellyVoltage();
  shellyTimeoutMs = oldTimeout;
  if (isnan(v)) {
    Serial.println("[WEB][VOLT] Не вдалося отримати свіже вимірювання Shelly");
    return false;
  }
  const uint32_t now = statsNowEpoch();
  if (now == 0) {
    Serial.println("[WEB][VOLT] Вимірювання отримано, але час ще не синхронізовано");
    return false;
  }
  vmLastV = v;
  vmLastVMs = millis();
  voltDayUpdate(v);
  return true;
}

void voltTick() {
  unsigned long saveNow = millis();
  if (voltDirty) {
    if (voltSaveFailed) {

      if (voltLastAttemptMs == 0 || saveNow - voltLastAttemptMs >= VOLT_RETRY_MS) {
        voltSave();
      }
    } else if (voltDirtySinceMs != 0 && saveNow - voltDirtySinceMs >= VOLT_SAVE_MS) {

      voltSave();
    }
  }
  unsigned long interval = voltPolledOnce ? VOLT_POLL_MS : 60000UL;
  if (millis() - voltLastPollMs < interval) {
    return;
  }
  voltLastPollMs = millis();
  voltPolledOnce = true;
  uint32_t now = statsNowEpoch();

  if (messageFlag || pendingOnMessage || now == 0 || !shellyConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  float v = vmLastV;
  if (vmLastVMs == 0 || millis() - vmLastVMs >= 120000UL) {
    shellyTimeoutMs = SHELLY_BG_TIMEOUT_MS;
    v = getShellyVoltage();
    shellyTimeoutMs = SHELLY_TIMEOUT_MS;
  }
  if (isnan(v)) {
    return;
  }
  if (vmLastVMs == 0 || millis() - vmLastVMs >= 120000UL) {
    voltDayUpdate(v);
  }
  uint16_t dv = (uint16_t)(v * 10.0f + 0.5f);
  voltLog.t[voltLog.head] = now;
  voltLog.dv[voltLog.head] = dv;
  voltLog.head = (voltLog.head + 1) % VOLT_SIZE;
  if (voltLog.count < VOLT_SIZE) {
    voltLog.count++;
  }
  if (!voltDirty) {
    voltDirtySinceMs = millis();
    if (voltDirtySinceMs == 0) voltDirtySinceMs = 1;
  }
  voltDirty = true;

}

void voltAlertLoad() {
  vAlertOn = preferences.getBool("vAlert", true);
  vIntSec = preferences.getInt("vIntSec", 30);
  vConfirm = preferences.getInt("vConfirm", 2);
  vPauseMin = preferences.getInt("vPauseMin", 0);
  if (vIntSec < 10 || vIntSec > 600) {
    logEvent(LC_STORE, LS_WARN, "Інтервал контролю напруги в NVS некоректний; встановлено 30 с");
    vIntSec = 30;
    prefPutIntVerified("vIntSec", vIntSec);
  }
  if (vConfirm < 1 || vConfirm > 10) {
    logEvent(LC_STORE, LS_WARN, "Кількість підтверджень напруги в NVS некоректна; встановлено 2");
    vConfirm = 2;
    prefPutIntVerified("vConfirm", vConfirm);
  }
  if (vPauseMin < 0 || vPauseMin > 120) {
    logEvent(LC_STORE, LS_WARN, "Пауза між тривогами напруги в NVS некоректна; встановлено 0 хв");
    vPauseMin = 0;
    prefPutIntVerified("vPauseMin", vPauseMin);
  }
}

struct VoltAlertItem {
  String text;
  uint32_t epoch;
  unsigned long ms;
  uint16_t pending;
};
static VoltAlertItem valertBox[VALERT_MAX];
static int valertCount = 0;
static uint16_t valertHeadRoundFailed = 0;
static bool valertHeadRoundActive = false;
static bool valertHeadRoundDelivered = false;
static int valertHeadFails = 0;
static unsigned long valertLastTry = 0;

static void voltAlertDrop() {
  for (int i = 1; i < valertCount; i++) valertBox[i - 1] = valertBox[i];
  if (valertCount > 0) valertBox[--valertCount] = VoltAlertItem();
  valertHeadRoundActive = false;
  valertHeadRoundFailed = 0;
  valertHeadRoundDelivered = false;
  valertHeadFails = 0;
}

void voltAlertSend(const String& msg) {
  if (!vAlertOn) {
    return;
  }
  if (valertCount >= VALERT_MAX) {
    logEvent(LC_TG, LS_WARN, "Черга сповіщень про напругу переповнена - найстаріше відкинуто");
    voltAlertDrop();
  }
  VoltAlertItem& it = valertBox[valertCount++];
  it.text = msg;
  it.epoch = statsNowEpoch();
  it.ms = millis();
  it.pending = RCPT_ALL;
  Serial.println("Сповіщення про напругу поставлено в чергу");
}

void voltAlertTick() {
  if (valertCount == 0) return;
  if (!vAlertOn) { while (valertCount > 0) voltAlertDrop(); return; }
  if (tgSendAttemptedThisPass) return;
  if (WiFi.status() != WL_CONNECTED || powerChangePending()) return;
  VoltAlertItem& it = valertBox[0];
  if ((millis() - it.ms) / 1000UL > VALERT_EXPIRE_SEC) {
    logEvent(LC_TG, LS_WARN, "Сповіщення про напругу не доставлено за 6 год - відкинуто");
    voltAlertDrop();
    return;
  }
  if (!valertHeadRoundActive) {
    if (valertLastTry != 0 && millis() - valertLastTry < (telegramOnline ? 5000UL : 30000UL)) return;
    if (!tgNetAllowed()) return;
    valertHeadRoundActive = true;
    valertHeadRoundFailed = 0;
    valertHeadRoundDelivered = false;
  } else if (!tgNetAllowed()) {
    return;
  }
  String msg = it.text;
  if (it.epoch != 0) msg += "\n🕒 Час: " + formatEpochLocal((time_t)it.epoch, "%H:%M");
  if (millis() - it.ms > 120000UL) msg += "\n📵 Надіслано із затримкою: не було зв'язку";
  const uint16_t attempt = it.pending & (uint16_t)~valertHeadRoundFailed;
  uint16_t tried = 0;
  const uint16_t left = broadcastMessageTo(msg, CHAT_F_VALERT, attempt, 1, &tried);
  it.pending = (uint16_t)((it.pending & ~attempt) | left);
  valertHeadRoundFailed |= (uint16_t)(tried & left);
  if (tried & ~left) valertHeadRoundDelivered = true;
  if (it.pending == 0) {
    Serial.println("Сповіщення про напругу надіслано");
    voltAlertDrop();
    valertLastTry = 0;
    return;
  }
  if ((it.pending & (uint16_t)~valertHeadRoundFailed) != 0) return;
  valertHeadRoundActive = false;
  valertLastTry = millis() ? millis() : 1;
  if (valertHeadRoundDelivered) valertHeadFails = 0;
  const bool telegramReachable = tgLastOkMs != 0 && millis() - tgLastOkMs < 120000UL && !tlsCertFailing;
  if (telegramReachable && ++valertHeadFails >= 10) {
    logEvent(LC_TG, LS_WARN, "Сповіщення про напругу 10 разів відхилено - видалено з черги");
    voltAlertDrop();
  }
}

void voltSpikeStart(float v, uint8_t kind) {
  uint32_t now = statsNowEpoch();
  if (now == 0) {
    vmSpkIdx = -1;
    return;
  }
  vmSpkIdx = spkLog.head;
  spkLog.t[spkLog.head] = now;
  spkLog.dv[spkLog.head] = (uint16_t)(v * 10.0f + 0.5f);
  spkLog.lim[spkLog.head] = (uint16_t)((kind == 1) ? voltLow : voltHigh);
  spkLog.head = (spkLog.head + 1) % SPK_SIZE;
  if (spkLog.count < SPK_SIZE) {
    spkLog.count++;
  }
  spkSave();
}

void voltMonitorTick() {
  if (messageFlag) {
    if (vmState != 0 && vmSpkIdx >= 0) {
      spkSave();
    }
    vmState = 0;
    vmBadCount = 0;
    vmSpkIdx = -1;
    return;
  }
  if (pendingOnMessage || !shellyConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  unsigned long interval = (vmFailCount >= 3) ? 300000UL : (unsigned long)vIntSec * 1000UL;
  if (vmChecked && millis() - vmLastCheckMs < interval) {
    return;
  }
  vmChecked = true;
  vmLastCheckMs = millis();
  shellyTimeoutMs = SHELLY_BG_TIMEOUT_MS;
  float v = getShellyVoltage();
  shellyTimeoutMs = SHELLY_TIMEOUT_MS;
  if (isnan(v)) {
    vmFailCount++;
    if (vmFailCount == 3) {
      logEvent(LC_SHELLY, LS_WARN, "Shelly не відповідає (" + shellyIP + ")");
    }
    return;
  }
  if (vmFailCount >= 3) {
    logEvent(LC_SHELLY, LS_INFO, "Shelly знову відповідає");
  }
  vmFailCount = 0;
  vmLastV = v;
  vmLastVMs = millis();
  voltDayUpdate(v);
  uint8_t kind = (v < voltLow) ? 1 : ((v > voltHigh) ? 2 : 0);
  String vs = String(v, 1);

  if (vmState == 0) {
    if (kind == 0) {
      vmBadCount = 0;
      return;
    }
    if (kind != vmBadKind) {
      vmBadKind = kind;
      vmBadCount = 0;
    }
    vmBadCount++;
    if (vmBadCount < vConfirm) {
      return;
    }
    if (vPauseMin > 0 && vmAlertedOnce && millis() - vmLastAlertMs < (unsigned long)vPauseMin * 60000UL) {
      return;
    }
    vmState = kind;
    vmBadCount = 0;
    vmLastAlertMs = millis();
    vmAlertedOnce = true;
    voltSpikeStart(v, kind);
    voltAlertSend(kind == 1
      ? "🔻 <b>Низька напруга</b>\n\n⚡ Виміряно: <b>" + vs + " В</b>\n📉 Нижня межа: " + String(voltLow) + " В"
      : "🔺 <b>Висока напруга</b>\n\n⚡ Виміряно: <b>" + vs + " В</b>\n📈 Верхня межа: " + String(voltHigh) + " В");
    return;
  }

  if (vmSpkIdx >= 0 && kind == vmState) {
    uint16_t dv = (uint16_t)(v * 10.0f + 0.5f);
    if ((kind == 1 && dv < spkLog.dv[vmSpkIdx]) || (kind == 2 && dv > spkLog.dv[vmSpkIdx])) {
      spkLog.dv[vmSpkIdx] = dv;
    }
  }
  if (kind != 0 && kind != vmState) {
    if (vmSpkIdx >= 0) {
      spkSave();
    }
    vmState = kind;
    vmLastAlertMs = millis();
    voltSpikeStart(v, kind);
    voltAlertSend(kind == 1
      ? "🔻 <b>Низька напруга</b>\n\n⚡ Виміряно: <b>" + vs + " В</b>\n📉 Нижня межа: " + String(voltLow) + " В"
      : "🔺 <b>Висока напруга</b>\n\n⚡ Виміряно: <b>" + vs + " В</b>\n📈 Верхня межа: " + String(voltHigh) + " В");
    return;
  }

  if (v >= voltStableLow && v <= voltStableHigh) {
    vmState = 0;
    vmBadCount = 0;
    if (vmSpkIdx >= 0) {
      spkSave();
    }
    vmSpkIdx = -1;
    voltAlertSend("✅ <b>Напруга нормалізувалася</b>\n\n⚡ Поточне значення: <b>" + vs + " В</b>\n📊 Діапазон стабільності: " + String(voltStableLow) + "–" + String(voltStableHigh) + " В");
  }
}

String voltMenuText() {
  String t = "⚡ <b>Контроль напруги</b>\n";
  t += "🔔 Контроль і виявлення аварій: " + onOff(vAlertOn) + "\n";
  t += "Отримувачі налаштовуються окремо в меню «Сповіщення» — власник або конкретна група.\n";
  t += "📉 Нижня межа: " + String(voltLow) + " В\n";
  t += "📈 Верхня межа: " + String(voltHigh) + " В\n";
  t += "✅ Діапазон нормалізації: " + String(voltStableLow) + "–" + String(voltStableHigh) + " В\n";
  t += "⏱ Інтервал перевірки: " + String(vIntSec) + " с\n";
  t += "🔁 Підтвердження: " + String(vConfirm) + " вимір. поспіль поза межами\n";
  t += "⏸ Пауза між тривогами: " + (vPauseMin > 0 ? String(vPauseMin) + " хв" : String("немає")) + "\n";
  if (vmLastVMs != 0) {
    t += "\nОстаннє вимірювання: " + String(vmLastV, 1) + " В, " + formatShortDuration((millis() - vmLastVMs) / 1000) + " тому";
  } else if (!shellyConfigured()) {
    t += "\n⚠️ Shelly не налаштовано (Пристрої → Shelly) - контроль не працює.";
  }
  t += "\nАварійні межі та діапазон нормалізації налаштовуються окремо.";
  return t;
}

uint32_t voltDayKey(uint32_t e) {
  time_t t = (time_t)e;
  struct tm x;
  localtime_r(&t, &x);
  return (uint32_t)x.tm_year * 1000UL + (uint32_t)x.tm_yday;
}

bool voltDayWriteVerified(VoltDay& d) {
  d.magic = VDAY_MAGIC;
  d.crc = statsCrc32((const uint8_t*)&d, sizeof(VoltDay) - sizeof(uint32_t));
  {
    VoltDay cur;
    if (preferences.getBytesLength("vDayV2") == sizeof(VoltDay) && preferences.getBytes("vDayV2", &cur, sizeof(VoltDay)) == sizeof(VoltDay)
        && memcmp(&cur, &d, sizeof(VoltDay)) == 0) {
      return true;
    }
  }
  if (preferences.putBytes("vDayV2", &d, sizeof(VoltDay)) != sizeof(VoltDay)) {
    nvsFailCount++;
    nvsFailKey = "vDayV2";
    nvsFailMs = millis() ? millis() : 1;
    return false;
  }
  VoltDay chk;
  if (preferences.getBytesLength("vDayV2") != sizeof(VoltDay) || preferences.getBytes("vDayV2", &chk, sizeof(VoltDay)) != sizeof(VoltDay)) {
    return false;
  }
  return chk.magic == VDAY_MAGIC && chk.crc == statsCrc32((const uint8_t*)&chk, sizeof(VoltDay) - sizeof(uint32_t))
         && memcmp(&chk, &d, sizeof(VoltDay)) == 0;
}

void voltDayEnsureLoaded() {
  if (vDayLoaded) {
    return;
  }
  static unsigned long lastTryMs = 0;
  if (vDayUnread && millis() - lastTryMs < 60000UL) {
    return;
  }
  lastTryMs = millis();
  VoltDay tmp;
  uint8_t vst = nvsReadState("vDayV2", &tmp, sizeof(VoltDay));
  if (vst == 2) {
    static bool loggedSize = false;
    if (!loggedSize) {
      loggedSize = true;
      logEvent(LC_STORE, LS_WARN, "Добова напруга: vDayV2 неправильного розміру - почато заново");
    }
  }
  if (vst == 0 || vst == 3) {

    if (vst == 3) {
      vDayUnread = true;
      static bool logged = false;
      if (!logged) {
        logged = true;
        logEvent(LC_STORE, LS_WARN, "Добова напруга: vDayV2 не прочитано - повтор пізніше, без перезапису");
      }
      return;
    }
    if (tmp.magic == VDAY_MAGIC && tmp.crc == statsCrc32((const uint8_t*)&tmp, sizeof(VoltDay) - sizeof(uint32_t))) {

      if (vDay.minDv != 0 && vDay.dayKey == tmp.dayKey && tmp.minDv != 0) {
        if (vDay.minDv < tmp.minDv) { tmp.minDv = vDay.minDv; tmp.minT = vDay.minT; }
        if (vDay.maxDv > tmp.maxDv) { tmp.maxDv = vDay.maxDv; tmp.maxT = vDay.maxT; }
      }
      if (!(vDay.minDv != 0 && vDay.dayKey > tmp.dayKey)) {
        vDay = tmp;
      }
    }
  }
  vDayLoaded = true;
  vDayUnread = false;
}

void voltDaySave() {
  voltDayEnsureLoaded();
  if (!vDayLoaded) {
    return;
  }
  vDaySaveOk = voltDayWriteVerified(vDay);
}

bool voltResetAll() {
  memset(&voltLog, 0, sizeof(VoltLog));
  memset(&spkLog, 0, sizeof(SpkLog));
  voltLog.magic = VOLT_MAGIC;
  voltLog.crc = statsCrc32((const uint8_t*)&voltLog, sizeof(VoltLog) - sizeof(uint32_t));
  spkLog.magic = SPK_MAGIC;
  spkLog.crc = statsCrc32((const uint8_t*)&spkLog, sizeof(SpkLog) - sizeof(uint32_t));
  voltUnread = false;
  spkUnread = false;
  voltDirty = false;
  voltSaveFailed = false;
  voltDirtySinceMs = 0;
  voltLastSaveMs = millis();
  voltLastAttemptMs = millis() ? millis() : 1;
  spkSaveOk = false;
  vmSpkIdx = -1;

  memset(&vDay, 0, sizeof(VoltDay));
  vDay.magic = VDAY_MAGIC;
  vDay.crc = statsCrc32((const uint8_t*)&vDay, sizeof(VoltDay) - sizeof(uint32_t));
  vDayLoaded = true;
  vDayUnread = false;
  voltSaveFailed = false;
  spkSaveOk = true;
  vDaySaveOk = true;

  const bool removeVolt = resetRemoveKeyVerified("voltV1");
  const bool removeSpike = resetRemoveKeyVerified("vSpkV1");
  const bool removeDay = resetRemoveKeyVerified("vDayV2");
  if (!(removeVolt && removeSpike && removeDay)) {
    Serial.println("[RESET] voltage reset aborted: could not remove previous NVS blobs");
    return false;
  }

  const bool histOk = nvsPutVerified("voltV1", &voltLog, sizeof(VoltLog));
  const bool spikeOk = nvsPutVerified("vSpkV1", &spkLog, sizeof(SpkLog));
  spkSaveOk = spikeOk;
  const bool dayOk = voltDayWriteVerified(vDay);
  vDaySaveOk = dayOk;

  VoltLog checkVolt;
  SpkLog checkSpk;
  VoltDay checkDay;
  const bool histReadback = nvsReadState("voltV1", &checkVolt, sizeof(checkVolt)) == 0 &&
    voltLogValid(checkVolt) && checkVolt.count == 0 && checkVolt.head == 0;
  const bool spikeReadback = nvsReadState("vSpkV1", &checkSpk, sizeof(checkSpk)) == 0 &&
    spkLogValid(checkSpk) && checkSpk.count == 0 && checkSpk.head == 0;
  const bool dayReadback = preferences.getBytesLength("vDayV2") == sizeof(VoltDay) &&
    preferences.getBytes("vDayV2", &checkDay, sizeof(checkDay)) == sizeof(VoltDay) &&
    checkDay.magic == VDAY_MAGIC &&
    checkDay.crc == statsCrc32((const uint8_t*)&checkDay, sizeof(VoltDay) - sizeof(uint32_t)) &&
    checkDay.minDv == 0 && checkDay.maxDv == 0 && checkDay.dayKey == 0;
  const bool saved = histOk && spikeOk && dayOk &&
    histReadback && spikeReadback && dayReadback;

  if (saved) {
    Serial.println("[RESET] voltage history, spike history and daily min/max cleared and verified");
  } else {
    Serial.printf("[RESET] voltage clear failed: volt=%s spike=%s day=%s readback=%s/%s/%s\n",
      histOk ? "OK" : "FAIL", spikeOk ? "OK" : "FAIL", dayOk ? "OK" : "FAIL",
      histReadback ? "OK" : "FAIL",
      spikeReadback ? "OK" : "FAIL", dayReadback ? "OK" : "FAIL");
  }
  return saved;
}

void voltDayUpdate(float v) {
  uint32_t now = statsNowEpoch();
  if (now == 0 || isnan(v)) {
    return;
  }
  voltDayEnsureLoaded();
  uint16_t dv = (uint16_t)(v * 10.0f + 0.5f);
  uint32_t key = voltDayKey(now);
  bool changed = false;
  if (vDay.dayKey != key || vDay.minDv == 0) {
    vDay.dayKey = key;
    vDay.minDv = dv;
    vDay.maxDv = dv;
    vDay.minT = now;
    vDay.maxT = now;
    changed = true;
  } else {
    if (dv < vDay.minDv) { vDay.minDv = dv; vDay.minT = now; changed = true; }
    if (dv > vDay.maxDv) { vDay.maxDv = dv; vDay.maxT = now; changed = true; }
  }
  if (changed && (v < voltLow || v > voltHigh)) {
    voltDaySave();
  }
}

String statsVoltageBlock() {
  voltDayEnsureLoaded();
  uint32_t now = statsNowEpoch();
  if (!shellyConfigured() && voltLog.count == 0 && spkLog.count == 0) {
    return "";
  }
  String b = "\n\n⚡ <b>НАПРУГА</b>\n";
  if (now != 0 && vDay.minDv != 0 && vDay.dayKey == voltDayKey(now)) {
    b += "• Сьогодні: мін. " + String(vDay.minDv / 10.0f, 1) + " В (" + formatEpochLocal((time_t)vDay.minT, "%H:%M") + "), макс. "
         + String(vDay.maxDv / 10.0f, 1) + " В (" + formatEpochLocal((time_t)vDay.maxT, "%H:%M") + ")\n";
  } else {
    b += "• Сьогодні: вимірювань ще немає\n";
  }
  int n24 = 0;
  for (int i = 0; i < voltLog.count; i++) {
    int idx = (voltLog.head + VOLT_SIZE - voltLog.count + i) % VOLT_SIZE;
    if (now != 0 && voltLog.t[idx] + 86400UL >= now) n24++;
  }
  b += "• Вимірювань для графіка за 24 год: " + String(n24) + "\n";
  b += "• Межі: " + String(voltLow) + "–" + String(voltHigh) + " В\n";
  int lo = 0, hi = 0;
  int last = -1;
  for (int i = 0; i < spkLog.count; i++) {
    int idx = (spkLog.head + SPK_SIZE - spkLog.count + i) % SPK_SIZE;
    if (now != 0 && spkLog.t[idx] + 86400UL >= now) {
      if (spkLog.dv[idx] < spkLog.lim[idx] * 10) lo++; else hi++;
    }
    last = idx;
  }
  b += "• Тривог за 24 год: " + String(lo + hi);
  if (lo + hi > 0) {
    b += " (низька " + String(lo) + ", висока " + String(hi) + ")";
  }
  if (last >= 0) {
    float v = spkLog.dv[last] / 10.0f;
    bool low = spkLog.dv[last] < spkLog.lim[last] * 10;
    b += "\n• Останній стрибок: " + formatEpochLocal((time_t)spkLog.t[last], "%d.%m %H:%M") + " — " + String(v, 1) + " В ("
         + (low ? "нижче" : "вище") + " межі на " + String(fabsf(v - spkLog.lim[last]), 1) + " В)";
  }
  return b;
}
