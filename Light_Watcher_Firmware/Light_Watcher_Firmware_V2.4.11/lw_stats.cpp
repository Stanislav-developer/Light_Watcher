#include "lw.h"

static HistBuffer hist;
static bool statOnSinceBoot = true;
static bool statResetDuringOff = false;
static uint32_t statOnFixId = 0;
static int statHistFixIdx[HIST_FIX_MAX];
static uint32_t statsBackupOff = 0xFFFFFFFFUL;
static bool statsBackupDone = false;
static bool statsBackupForce = false;
static bool statsInCommit = false;

void statOnFixDone(uint32_t dur) {
  lastOffDurationText = formatShortDuration(dur);
  preferences.putString("lastOffDur", lastOffDurationText);
  Serial.println("Тривалість відключення дораховано після синхронізації часу: " + lastOffDurationText);
}

uint32_t statsCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

uint32_t statsNowEpoch() {
  time_t t;
  time(&t);
  if (t < (time_t)EPOCH_MIN_VALID) return 0;
  uint64_t epoch = (uint64_t)t;
  return epoch <= UINT32_MAX ? (uint32_t)epoch : 0;
}

uint32_t statsEdgeEpoch(unsigned long ms) {
  const uint32_t now = statsNowEpoch();
  if (now == 0) return 0;
  const uint32_t ago = (uint32_t)((millis() - ms) / 1000UL);
  return now > ago ? now - ago : 0;
}

void statsSave() {

  if (statsTxnBlocked) {
    statsSaveOk = false;
    statsBackupOk = false;
    return;
  }
  if (!statsInCommit && statsPairDirty && preferences.isKey("statsTxn")) {
    statsCommitPair();
    return;
  }
  if (statsUnread && !statsReread()) {
    statsSaveOk = false;
    return;
  }
  stats.magic = STATS_MAGIC;
  stats.crc = statsCrc32((const uint8_t*)&stats, sizeof(PowerStats) - sizeof(uint32_t));
  statsSaveOk = nvsPutVerified("statsV1", &stats, sizeof(PowerStats));

  if (statsSaveOk && (statsBackupForce || !statsBackupDone || !statsBackupOk || stats.offCount != statsBackupOff || millis() - statsBackupMs >= STATS_BACKUP_MS)) {

    statsBackupOk = nvsPutVerified("statsB", &stats, sizeof(PowerStats));
    if (statsBackupOk) {
      statsBackupDone = true;
      statsBackupOff = stats.offCount;
      statsBackupMs = millis();
      statsBackupForce = false;
    }
  }
}

bool statsValid(const PowerStats& t) {
  return t.magic == STATS_MAGIC && t.crc == statsCrc32((const uint8_t*)&t, sizeof(PowerStats) - sizeof(uint32_t));
}

bool histValid(const HistBuffer& h) {
  return h.magic == HIST_MAGIC && h.head < HIST_SIZE && h.count <= HIST_SIZE &&
         h.crc == statsCrc32((const uint8_t*)&h, sizeof(HistBuffer) - sizeof(uint32_t));
}

bool statsRepair(PowerStats& t) {
  const PowerStats before = t;
  if (t.offOpen > 1U) t.offOpen = 1U;
  if (t.curOffStartEpoch != 0 && t.curOffStartEpoch < EPOCH_MIN_VALID) t.curOffStartEpoch = 0;
  if (!t.offOpen) t.curOffStartEpoch = 0;
  if (t.exactCount > t.offCount) t.offCount = t.exactCount;
  if (t.curOffId > t.offCount) t.offCount = t.curOffId;
  if (t.offOpen && t.curOffId == 0) {
    if (t.offCount == 0) t.offCount = 1;
    t.curOffId = t.offCount;
  }
  return memcmp(&before, &t, sizeof(PowerStats)) != 0;
}

bool histRepair(HistBuffer& h) {
  HistRecord kept[HIST_SIZE];
  uint32_t n = 0;
  bool changed = false;
  for (uint32_t i = 0; i < h.count; i++) {
    HistRecord r = h.rec[(h.head + HIST_SIZE - h.count + i) % HIST_SIZE];
    if (r.id == 0) { changed = true; continue; }
    if (r.offEpoch != 0 && r.offEpoch < EPOCH_MIN_VALID) { r.offEpoch = 0; changed = true; }
    if (r.onEpoch != 0 && r.onEpoch < EPOCH_MIN_VALID) { r.onEpoch = 0; changed = true; }
    if (r.offEpoch != 0 && r.onEpoch != 0 && r.onEpoch < r.offEpoch) { r.offEpoch = 0; changed = true; }
    bool replaced = false;
    for (uint32_t j = 0; j < n; j++) {
      if (kept[j].id == r.id) {
        memmove(&kept[j], &kept[j + 1], (n - j - 1) * sizeof(HistRecord));
        kept[n - 1] = r;
        replaced = true;
        changed = true;
        break;
      }
    }
    if (!replaced) kept[n++] = r;
  }
  if (!changed) return false;
  memset(h.rec, 0, sizeof(h.rec));
  for (uint32_t i = 0; i < n; i++) h.rec[i] = kept[i];
  h.count = n;
  h.head = n % HIST_SIZE;
  return true;
}

void statsBackupRestore(bool mainUnread) {
  PowerStats tmp;
  if (nvsGetExact("statsB", &tmp, sizeof(PowerStats))) {
    if (tmp.magic == STATS_MAGIC && tmp.crc == statsCrc32((const uint8_t*)&tmp, sizeof(PowerStats) - sizeof(uint32_t))) {
      stats = tmp;
      logEvent(LC_STORE, LS_WARN, "Статистику відновлено з резервної копії");
      statsLoadNote += "статистику відновлено з резервної копії; ";
      statsSaveOk = false;
      return;
    }
  }
  if (mainUnread) {
    return;
  }
  logEvent(LC_STORE, LS_ERR, "Статистика пошкоджена, резервної копії немає - облік заново");
  statsLoadNote += "статистика пошкоджена, придатної резервної копії немає - облік заново; ";
}

void histBackupRestore(bool mainUnread) {
  HistBuffer tmpH;
  if (nvsGetExact("histB", &tmpH, sizeof(HistBuffer))) {
    if (tmpH.magic == HIST_MAGIC && tmpH.head < HIST_SIZE && tmpH.count <= HIST_SIZE &&
        tmpH.crc == statsCrc32((const uint8_t*)&tmpH, sizeof(HistBuffer) - sizeof(uint32_t))) {
      hist = tmpH;
      logEvent(LC_STORE, LS_WARN, "Історію відключень відновлено з резервної копії");
      statsLoadNote += "історію відновлено з резервної копії; ";
      histSaveOk = false;
      return;
    }
  }
  if (mainUnread) {
    return;
  }
  logEvent(LC_STORE, LS_ERR, "Історія відключень пошкоджена, резервної копії немає");
  statsLoadNote += "історія пошкоджена, придатної резервної копії немає; ";
}
void histSave() {

  if (statsTxnBlocked) {
    histSaveOk = false;
    return;
  }
  if (!statsInCommit && statsPairDirty && preferences.isKey("statsTxn")) {
    statsCommitPair();
    return;
  }
  if (histUnread && !histReread()) {
    histSaveOk = false;
    return;
  }
  hist.magic = HIST_MAGIC;
  hist.crc = statsCrc32((const uint8_t*)&hist, sizeof(HistBuffer) - sizeof(uint32_t));
  bool okMain = nvsPutVerified("histV1", &hist, sizeof(HistBuffer));

  bool okB = okMain && nvsPutVerified("histB", &hist, sizeof(HistBuffer));
  histSaveOk = okMain && okB;
}

bool statsCommitPair() {

  if (statsTxnBlocked) {
    statsPairDirty = true;
    statsSaveOk = false;
    histSaveOk = false;
    return false;
  }
  if ((statsUnread && !statsReread()) || (histUnread && !histReread())) {
    statsPairDirty = true;
    statsSaveOk = false;
    histSaveOk = false;
    return false;
  }

  stats.magic = STATS_MAGIC;
  stats.crc = statsCrc32((const uint8_t*)&stats, sizeof(PowerStats) - sizeof(uint32_t));
  hist.magic = HIST_MAGIC;
  hist.crc = statsCrc32((const uint8_t*)&hist, sizeof(HistBuffer) - sizeof(uint32_t));

  StatsTxn tx;
  memset(&tx, 0, sizeof(tx));
  tx.magic = STATS_TXN_MAGIC;
  tx.statsData = stats;
  tx.histData = hist;
  tx.crc = statsCrc32((const uint8_t*)&tx, sizeof(StatsTxn) - sizeof(uint32_t));

  if (!nvsPutVerified("statsTxn", &tx, sizeof(tx))) {
    statsPairDirty = true;
    statsSaveOk = false;
    histSaveOk = false;
    return false;
  }

  statsUnread = false;
  histUnread = false;
  statsBackupForce = true;
  statsInCommit = true;
  statsSave();
  histSave();
  statsInCommit = false;
  bool ok = statsSaveOk && statsBackupOk && histSaveOk;
  if (ok) {
    preferences.remove("statsTxn");
    ok = !preferences.isKey("statsTxn");
  }
  statsPairDirty = !ok;
  return ok;
}

bool statsDiscardCorruptTxn(const char* reason) {
  preferences.remove("statsTxn");
  if (preferences.isKey("statsTxn")) return false;
  statsTxnBlocked = false;
  statsPairDirty = false;
  statsBackupForce = true;
  logEvent(LC_STORE, LS_ERR, String("Журнал транзакції статистики ") + reason + " - видалено; використано основні блоки");
  statsLoadNote += String("журнал транзакції ") + reason + " - видалено; ";
  return true;
}

void statsRecoverPair() {
  StatsTxn tx;

  uint8_t readState = nvsReadState("statsTxn", &tx, sizeof(tx));
  if (readState == 1) {
    statsTxnBlocked = false;
    return;
  }
  if (readState == 2 && statsDiscardCorruptTxn("неправильного розміру")) {
    return;
  }
  if (readState != 0) {
    statsTxnBlocked = true;
    statsPairDirty = true;
    statsUnread = true;
    histUnread = true;
    const char* reason = (readState == 2) ? "неправильний розмір" : "тимчасово не читається";
    logEvent(LC_STORE, LS_ERR, String("Журнал транзакції статистики ") + reason + "; запис заблоковано до наступного запуску або явного скидання");
    statsLoadNote += String("журнал транзакції ") + reason + "; " ;
    return;
  }
  bool valid = tx.magic == STATS_TXN_MAGIC &&
    tx.crc == statsCrc32((const uint8_t*)&tx, sizeof(StatsTxn) - sizeof(uint32_t)) &&
    statsValid(tx.statsData) && histValid(tx.histData);
  if (!valid && statsDiscardCorruptTxn("пошкоджений")) {
    return;
  }
  if (!valid) {
    logEvent(LC_STORE, LS_ERR, "Журнал транзакції статистики пошкоджений; збережені блоки не перезаписано");
    statsLoadNote += "журнал транзакції статистики пошкоджений; запис заблоковано; ";

    statsTxnBlocked = true;
    statsPairDirty = true;
    statsUnread = true;
    histUnread = true;
    return;
  }

  statsTxnBlocked = false;
  statsUnread = false;
  histUnread = false;
  stats = tx.statsData;
  hist = tx.histData;
  statsBackupForce = true;
  statsSave();
  histSave();
  bool ok = statsSaveOk && statsBackupOk && histSaveOk;
  if (ok) {
    preferences.remove("statsTxn");
    ok = !preferences.isKey("statsTxn");
  }
  statsPairDirty = !ok;
  if (ok) {
    logEvent(LC_STORE, LS_WARN, "Незавершену транзакцію статистики відновлено");
    statsLoadNote += "незавершену транзакцію відновлено; ";
  }
}

void statsLoad() {

  statsLoadNote = "";
  memset(&stats, 0, sizeof(PowerStats));
  {
    PowerStats tmp;
    uint8_t st = nvsReadState("statsV1", &tmp, sizeof(PowerStats));
    if (st == 0 && statsValid(tmp)) {
      stats = tmp;
      Serial.println("Статистику завантажено");

      PowerStats b;
      if (preferences.isKey("statsB") && !(nvsReadState("statsB", &b, sizeof(PowerStats)) == 0 && statsValid(b))) {
        statsBackupOk = false;
      }
    } else if (preferences.isKey("statsV1") || preferences.isKey("statsB")) {
      statsBackupRestore(st == 3);
      if (st == 3 && statsSaveOk) {

        statsUnread = true;
        logEvent(LC_STORE, LS_WARN, "Статистику не прочитано - запис призупинено до успішного читання");
        statsLoadNote += "основний блок статистики не прочитано - запис призупинено до успішного читання; ";
      }
    }
  }

  memset(&hist, 0, sizeof(HistBuffer));
  {
    HistBuffer tmpH;
    uint8_t st = nvsReadState("histV1", &tmpH, sizeof(HistBuffer));
    if (st == 0 && histValid(tmpH)) {
      hist = tmpH;
      HistBuffer b;
      if (preferences.isKey("histB") && !(nvsReadState("histB", &b, sizeof(HistBuffer)) == 0 && histValid(b))) {
        histSaveOk = false;
      }
    } else if (preferences.isKey("histV1") || preferences.isKey("histB")) {
      histBackupRestore(st == 3);
      if (st == 3 && histSaveOk) {
        histUnread = true;
        logEvent(LC_STORE, LS_WARN, "Історію відключень не прочитано - запис призупинено до успішного читання");
        statsLoadNote += "основний блок історії не прочитано - запис призупинено до успішного читання; ";
      }
    }
  }
  statsRecoverPair();
  bool statsFixed = statsRepair(stats);
  bool histFixed = histRepair(hist);
  if (statsFixed || histFixed) {
    statsBackupForce = true;
    if (histFixed) histSaveOk = false;
    logEvent(LC_STORE, LS_WARN, String("Виправлено неузгоджені дані: ") +
             (statsFixed ? "статистика " : "") + (histFixed ? "історія" : ""));
    statsLoadNote += "неузгоджені дані статистики виправлено; ";
  }
}

bool statsReread() {
  PowerStats tmp;
  uint8_t st = nvsReadState("statsV1", &tmp, sizeof(PowerStats));
  if (st == 3) {
    return false;
  }
  if (st == 0 && statsValid(tmp)) {
    const PowerStats session = stats;
    stats = tmp;
    statsRepair(stats);
    if (session.offOpen && statOffInSession &&
        !(stats.offOpen && stats.curOffStartEpoch == session.curOffStartEpoch)) {
      stats.offCount = statsNextOutageCount(stats.offCount, powerOutageCount);
      stats.curOffId = stats.offCount;
      stats.offOpen = 1;
      stats.curOffStartEpoch = session.curOffStartEpoch;
      if (session.lastOffEpoch > stats.lastOffEpoch) stats.lastOffEpoch = session.lastOffEpoch;
    }
    logEvent(LC_STORE, LS_WARN, "Статистику прочитано повторно - використано збережену");
  }
  statsUnread = false;
  return true;
}

bool histReread() {
  HistBuffer tmpH;
  uint8_t st = nvsReadState("histV1", &tmpH, sizeof(HistBuffer));
  if (st == 3) {
    return false;
  }
  if (st == 0 && histValid(tmpH)) {
    const HistBuffer session = hist;
    hist = tmpH;
    histRepair(hist);
    for (uint32_t i = 0; i < session.count; i++) {
      const HistRecord& r = session.rec[(session.head + HIST_SIZE - session.count + i) % HIST_SIZE];
      if (r.id == 0 || statsHistoryHasId(r.id)) continue;
      hist.rec[hist.head] = r;
      hist.head = (hist.head + 1) % HIST_SIZE;
      if (hist.count < HIST_SIZE) hist.count++;
    }
    logEvent(LC_STORE, LS_WARN, "Історію відключень прочитано повторно - використано збережену");
  }
  histUnread = false;
  return true;
}

void statsAddOnTime() {
  if (!statOnObserving) {
    return;
  }
  uint32_t s = (millis() - statOnSegMs) / 1000;
  stats.totalOnSec += s;
  statOnSegMs += s * 1000UL;
}

bool statsHistoryHasId(uint32_t id) {
  if (id == 0) return false;
  for (uint32_t i = 0; i < hist.count; i++) {
    uint32_t idx = (hist.head + HIST_SIZE - hist.count + i) % HIST_SIZE;
    if (hist.rec[idx].id == id) return true;
  }
  return false;
}

void statsAddSaturated(uint32_t& target, uint32_t amount) {
  target = UINT32_MAX - target < amount ? UINT32_MAX : target + amount;
}

uint32_t statsNextOutageCount(uint32_t statsCount, int32_t markerCount) {
  const uint32_t marker = markerCount > 0 ? (uint32_t)markerCount : 0U;
  const uint32_t current = statsCount > marker ? statsCount : marker;
  return current == UINT32_MAX ? UINT32_MAX : current + 1U;
}

void statsBegin(bool powerNow) {
  statsLoad();
  statLastCheckpointMs = millis();
  if (statsTxnBlocked) {
    if (powerNow) {
      stats.offOpen = 0;
      stats.curOffStartEpoch = 0;
      statOffInSession = false;
      statOnObserving = true;
      statOnSegMs = millis();
      statOnPeriodStartMs = statOnSegMs;
      statOnSinceBoot = true;
    } else {
      stats.offOpen = 1;
      if (stats.curOffStartEpoch == 0 && powerOffTimestamp >= (time_t)EPOCH_MIN_VALID) {
        stats.curOffStartEpoch = (uint32_t)powerOffTimestamp;
      }
      statOnObserving = false;
      statOffInSession = false;
    }
    Serial.println("Статистика тимчасово заблокована: журнал NVS не вдалося безпечно відновити");
    return;
  }

  if (!powerNow && lastOutageDetect) {
    uint32_t markerEpoch = (powerOffTimestamp >= (time_t)EPOCH_MIN_VALID) ? (uint32_t)powerOffTimestamp : 0;
    uint32_t persistedCount = (powerOutageCount > 0) ? (uint32_t)powerOutageCount : 0;
    const uint32_t storedCount = stats.offCount;
    if (stats.offCount < persistedCount) stats.offCount = persistedCount;
    if (!stats.offOpen) {
      stats.offCount = persistedCount > storedCount ? persistedCount : statsNextOutageCount(storedCount, 0);
      stats.offOpen = 1;
      stats.curOffId = stats.offCount;
      stats.curOffStartEpoch = markerEpoch;
      if (markerEpoch != 0) stats.lastOffEpoch = markerEpoch;
      statsBackupForce = true;
      statsSave();
      Serial.println(markerEpoch ? "Статистика: активне відключення відновлено з pwrOffTmstmp"
                                 : "Статистика: активне відключення відновлено; точний час початку невідомий");
    } else if (stats.curOffStartEpoch == 0 && markerEpoch != 0) {
      stats.curOffStartEpoch = markerEpoch;
      stats.lastOffEpoch = markerEpoch;
      statsBackupForce = true;
      statsSave();
      Serial.println("Статистика: час початку активного відключення відновлено з pwrOffTmstmp");
    }
  }

  if (powerNow) {
    if (stats.offOpen) {

      if (!statsHistoryHasId(stats.curOffId)) {
        HistRecord r;
        r.id = stats.curOffId;
        r.offEpoch = stats.curOffStartEpoch;
        r.onEpoch = 0;
        r.durSec = 0;
        hist.rec[hist.head] = r;
        hist.head = (hist.head + 1) % HIST_SIZE;
        if (hist.count < HIST_SIZE) hist.count++;
      }

      stats.offOpen = 0;
      stats.curOffStartEpoch = 0;
      stats.lastOnEpoch = 0;

      statsBackupForce = true;
      histSaveOk = false;
      statsCommitPair();
      Serial.println("Статистика: відключення під час вимкнення ESP32 записано; точна тривалість невідома");
    }
    statOnObserving = true;
    statOnSegMs = millis();
    statOnPeriodStartMs = millis();
    statOnSinceBoot = true;
  } else {

    statOffInSession = false;
    if (!stats.offOpen) {
      stats.offCount = statsNextOutageCount(stats.offCount, powerOutageCount);
      stats.curOffId = stats.offCount;
      stats.offOpen = 1;
      stats.curOffStartEpoch = 0;
      powerOutageCount = (int32_t)(stats.offCount > 2147483647UL ? 2147483647UL : stats.offCount);
      prefPutIntVerified("powerOutageCnt", powerOutageCount);
      statsBackupForce = true;
      statsSave();
    }
    Serial.println(statOffInSession || stats.curOffStartEpoch ? "Статистика: продовжуємо облік відключення, що почалося до перезапуску"
                                 : "Статистика: пристрій запущено без світла, початок відключення невідомий");
  }
}

void statsOnPowerOff() {
  statLastOffKnown = false;
  statLastOffDurSec = 0;
  const unsigned long transitionMs = powerEdgeMs();
  if (statOnObserving && (long)(transitionMs - statOnSegMs) >= 0) {
    const uint32_t onS = (transitionMs - statOnSegMs) / 1000UL;
    stats.totalOnSec += onS;
    statOnSegMs += onS * 1000UL;
  } else {
    statsAddOnTime();
  }
  statOnObserving = false;
  statOffInSession = true;
  statOffStartMs = transitionMs;
  statResetDuringOff = false;
  stats.offCount = statsNextOutageCount(stats.offCount, powerOutageCount);
  stats.curOffId = stats.offCount;
  stats.offOpen = 1;
  stats.curOffStartEpoch = 0;
  if (statsTxnBlocked) return;

  if (voltDirty) voltSaveIfDirty();

  uint32_t now = statsEdgeEpoch(transitionMs);
  stats.offOpen = 1;
  stats.curOffStartEpoch = now;
  stats.lastOffEpoch = now;
  statsSave();
}

void statsOnPowerOn() {
  statLastOffKnown = false;
  statLastOffDurSec = 0;
  const unsigned long edgeMs = powerEdgeMs();
  if (statsTxnBlocked) {
    if (statOffInSession) {
      statLastOffKnown = true;
      statLastOffDurSec = (edgeMs - statOffStartMs) / 1000UL;
      statsAddSaturated(stats.totalOffSec, statLastOffDurSec);
      statsAddSaturated(stats.exactCount, 1U);
      if (statLastOffDurSec > stats.maxOffSec) stats.maxOffSec = statLastOffDurSec;
      if (stats.minOffSec == 0 || statLastOffDurSec < stats.minOffSec) stats.minOffSec = statLastOffDurSec;
      const uint32_t now = statsEdgeEpoch(edgeMs);
      const uint32_t offEpoch = powerOffTimestamp >= (time_t)EPOCH_MIN_VALID
        ? (uint32_t)powerOffTimestamp
        : (now >= statLastOffDurSec ? now - statLastOffDurSec : 0U);
      stats.lastOffEpoch = offEpoch;
      stats.lastOnEpoch = now;
      if (stats.curOffId != 0 && !statsHistoryHasId(stats.curOffId)) {
        HistRecord record = { stats.curOffId, offEpoch, now, statLastOffDurSec };
        hist.rec[hist.head] = record;
        hist.head = (hist.head + 1U) % HIST_SIZE;
        if (hist.count < HIST_SIZE) hist.count++;
      }
    }
    stats.offOpen = 0;
    stats.curOffStartEpoch = 0;
    statOffInSession = false;
    statResetDuringOff = false;
    statOnFixPending = false;
    statOnObserving = true;
    statOnSegMs = edgeMs;
    statOnPeriodStartMs = statOnSegMs;
    statOnSinceBoot = false;
    return;
  }
  uint32_t now = statsEdgeEpoch(edgeMs);
  bool exact = false;
  bool unknownHistoryAdded = false;
  uint32_t dur = 0;

  if (stats.offOpen) {
    if (statResetDuringOff) {
      dur = (long)(edgeMs - statResetMs) > 0 ? (edgeMs - statResetMs) / 1000 : 0;
      exact = true;
    } else if (statOffInSession) {
      dur = (edgeMs - statOffStartMs) / 1000;
      exact = true;
    } else if (stats.curOffStartEpoch != 0 && now != 0 && now >= stats.curOffStartEpoch) {
      dur = now - stats.curOffStartEpoch;
      exact = true;
    } else if (stats.curOffStartEpoch != 0 && now == 0) {

      statOnFixPending = true;
      statOnFixMs = edgeMs;
      statOnFixStart = stats.curOffStartEpoch;
      statOnFixId = stats.curOffId;
    }

    if (!exact && !statOnFixPending && stats.curOffId != 0 && !statsHistoryHasId(stats.curOffId)) {

      HistRecord unknownRecord;
      unknownRecord.id = stats.curOffId;
      unknownRecord.offEpoch = stats.curOffStartEpoch;
      unknownRecord.onEpoch = now;
      unknownRecord.durSec = 0;
      hist.rec[hist.head] = unknownRecord;
      hist.head = (hist.head + 1) % HIST_SIZE;
      if (hist.count < HIST_SIZE) hist.count++;
      unknownHistoryAdded = true;
    }

    if (exact) {
      statLastOffKnown = true;
      statLastOffDurSec = dur;
      stats.exactCount++;
      stats.totalOffSec += dur;
      if (dur > stats.maxOffSec) {
        stats.maxOffSec = dur;
      }
      if (stats.exactCount == 1 || dur < stats.minOffSec) {
        stats.minOffSec = dur;
      }
      HistRecord r;
      r.id = stats.curOffId;
      r.offEpoch = stats.curOffStartEpoch ? stats.curOffStartEpoch : (now ? now - dur : 0);
      r.onEpoch = now;
      r.durSec = dur;
      int slot = -1;
      for (uint32_t i = 0; i < hist.count && slot < 0; i++) {
        const uint32_t idx = (hist.head + HIST_SIZE - hist.count + i) % HIST_SIZE;
        if (r.id != 0 && hist.rec[idx].id == r.id) slot = (int)idx;
      }
      const bool append = slot < 0;
      if (append) slot = (int)hist.head;
      if (now == 0 && statHistFixCount < HIST_FIX_MAX) {
        statHistFixIdx[statHistFixCount] = slot;
        statHistFixMs[statHistFixCount] = edgeMs;
        statHistFixCount++;
      }
      hist.rec[slot] = r;
      if (append) {
        hist.head = (hist.head + 1) % HIST_SIZE;
        if (hist.count < HIST_SIZE) {
          hist.count++;
        }
      }
    }
    if (!statOnFixPending || statOnFixId != stats.curOffId) {
      stats.offOpen = 0;
      stats.curOffStartEpoch = 0;
    }
  }

  stats.lastOnEpoch = now;
  statOffInSession = false;
  statResetDuringOff = false;

  statOnObserving = true;
  statOnSegMs = edgeMs;
  statOnPeriodStartMs = statOnSegMs;
  statOnSinceBoot = false;

  statsBackupForce = true;
  if (exact || unknownHistoryAdded) {
    statsCommitPair();
  } else {
    statsSave();
  }
}

void restoreOnPeriodFromStats() {
  if (!onRestorePending) {
    return;
  }
  if (!statOnObserving || stats.offOpen || stats.lastOnEpoch == 0) {
    onRestorePending = false;
    return;
  }
  uint32_t now = statsNowEpoch();
  if (now == 0) {
    return;
  }
  onRestorePending = false;
  if (now < stats.lastOnEpoch || now - stats.lastOnEpoch > 40UL * 86400UL) {
    return;
  }
  unsigned long elapsedMs = (unsigned long)(now - stats.lastOnEpoch) * 1000UL;

  unsigned long startMs = millis() - elapsedMs;
  statOnPeriodStartMs = startMs;
  statOnSinceBoot = false;
  powerOnSinceMs = startMs;
  powerOnSinceBoot = false;

  bool sameDay = formatEpochLocal((time_t)stats.lastOnEpoch, "%Y%j") == formatEpochLocal((time_t)now, "%Y%j");
  powerOnShortTime = formatEpochLocal((time_t)stats.lastOnEpoch, sameDay ? "%H:%M" : "%d.%m %H:%M");
  Serial.println("Період зі світлом відновлено зі статистики: з " + powerOnShortTime);
}

void statsTick() {
  if (statsTxnBlocked) return;
  static unsigned long rereadMs = 0;
  if ((statsUnread || histUnread) && millis() - rereadMs >= 10000UL) {
    rereadMs = millis();
    if (statsUnread) statsReread();
    if (histUnread) histReread();
  }
  restoreOnPeriodFromStats();
  if (millis() - statLastTickMs < 10000UL) {
    return;
  }
  statLastTickMs = millis();
  bool changed = false;

  uint32_t now = statsNowEpoch();
  if (now != 0) {
    if (statResetDuringOff && stats.offOpen && stats.curOffStartEpoch == 0) {
      stats.curOffStartEpoch = now - (millis() - statResetMs) / 1000;
      changed = true;
    }
    else if (statOffInSession && stats.offOpen && stats.curOffStartEpoch == 0) {
      stats.curOffStartEpoch = now - (millis() - statOffStartMs) / 1000;
      stats.lastOffEpoch = stats.curOffStartEpoch;
      powerOffTimestamp = (time_t)stats.curOffStartEpoch;
      prefPutLong64Verified("pwrOffTmstmp", (int64_t)powerOffTimestamp);
      changed = true;
    }

    if (statOnObserving && stats.lastOnEpoch == 0) {
      uint32_t elapsed = (millis() - statOnPeriodStartMs) / 1000UL;
      stats.lastOnEpoch = (now >= elapsed) ? now - elapsed : now;
      statOnSinceBoot = false;
      powerOnSinceMs = statOnPeriodStartMs;
      powerOnSinceBoot = false;
      powerOnShortTime = formatEpochLocal((time_t)stats.lastOnEpoch, "%H:%M");
      changed = true;
    }

    bool histChanged = false;
    for (int i = 0; i < statHistFixCount; i++) {
      HistRecord& hr = hist.rec[statHistFixIdx[i]];
      uint32_t onE = now - (millis() - statHistFixMs[i]) / 1000;
      if (hr.onEpoch == 0) hr.onEpoch = onE;
      if (hr.offEpoch == 0 && onE > hr.durSec) hr.offEpoch = onE - hr.durSec;
      if (stats.lastOffEpoch == 0) stats.lastOffEpoch = hr.offEpoch;
      histChanged = true;
    }
    statHistFixCount = 0;

    if (statOnFixPending) {
      uint32_t onE = now - (millis() - statOnFixMs) / 1000;
      if (onE >= statOnFixStart) {
        uint32_t dur = onE - statOnFixStart;
        bool alreadyInHistory = statsHistoryHasId(statOnFixId);
        if (!alreadyInHistory) {
          stats.exactCount++;
          stats.totalOffSec += dur;
          if (dur > stats.maxOffSec) stats.maxOffSec = dur;
          if (stats.exactCount == 1 || dur < stats.minOffSec) stats.minOffSec = dur;
          HistRecord hr;
          hr.id = statOnFixId;
          hr.offEpoch = statOnFixStart;
          hr.onEpoch = onE;
          hr.durSec = dur;
          hist.rec[hist.head] = hr;
          hist.head = (hist.head + 1) % HIST_SIZE;
          if (hist.count < HIST_SIZE) hist.count++;
          histChanged = true;
        }
        statLastOffKnown = true;
        statLastOffDurSec = dur;
        if (stats.lastOnEpoch == 0 || stats.lastOnEpoch < onE) stats.lastOnEpoch = onE;
        changed = true;
        if (!alreadyInHistory) statOnFixDone(dur);
      }
    }
    if (statOnFixPending && statsHistoryHasId(statOnFixId)) {
      if (stats.curOffId == statOnFixId) {
        stats.offOpen = 0;
        stats.curOffStartEpoch = 0;
      }
      if (statsCommitPair()) {
        statOnFixPending = false;
      } else {

      }
      changed = false;
    } else if (histChanged) {
      histSave();
      changed = true;
    }
  }

  if (statOnObserving && millis() - statLastCheckpointMs >= STATS_CHECKPOINT_MS) {
    statLastCheckpointMs = millis();
    statsAddOnTime();
    changed = true;
  }

  if (changed) {
    statsSave();
  }
}

bool outageEndRecorded() {
  return !statsTxnBlocked && (!stats.offOpen || (statOnFixPending && statOnFixId == stats.curOffId));
}

void statsSaveBeforeRestart() {
  if (statsTxnBlocked) return;
  if (statOnObserving && stats.lastOnEpoch == 0) {
    uint32_t now = statsNowEpoch();
    if (now != 0) {
      uint32_t elapsed = (millis() - statOnPeriodStartMs) / 1000UL;
      stats.lastOnEpoch = (now >= elapsed) ? now - elapsed : now;
    }
  }
  statsAddOnTime();
  statsSave();
}

String formatStatDuration(uint32_t sec) {
  if (sec < 60) {
    return "менше 1хв";
  }
  uint32_t h = sec / 3600;
  uint32_t m = (sec % 3600) / 60;
  if (h == 0) {
    return String(m) + "хв";
  }
  return String(h) + "год " + String(m) + "хв";
}

String formatStatEpoch(uint32_t e) {
  if (e == 0) {
    return "невідомо";
  }
  time_t t = (time_t)e;
  struct tm ti;
  localtime_r(&t, &ti);
  return formatEpochLocal(t, "%d.%m.%Y о %H:%M");
}

String currentOffDurationText() {
  uint32_t now = statsNowEpoch();
  if (statOffInSession) {
    return formatShortDuration((millis() - statOffStartMs) / 1000);
  }
  if (!statResetDuringOff && stats.offOpen && stats.curOffStartEpoch != 0 && now >= stats.curOffStartEpoch) {
    return formatShortDuration(now - stats.curOffStartEpoch);
  }
  return "";
}

String currentOnDurationText() {
  if (!statOnObserving) {
    return "";
  }
  String t = formatShortDuration((millis() - statOnPeriodStartMs) / 1000);
  if (statOnSinceBoot) {
    t += " (від запуску пристрою)";
  }
  return t;
}

String buildStatsMessage() {
  bool powerNow = checkPowerStatus();
  String msg = "📊 <b>Статистика Light Watcher</b>\n\n";

  msg += "<b>💡 Поточний стан</b>\n";
  msg += powerNow ? "🟢 Світло є\n" : "🔴 Світла немає\n";

  msg += "⏱ Тривалість поточного стану: ";
  if (powerNow && statOnObserving) {
    msg += formatStatDuration((millis() - statOnPeriodStartMs) / 1000);
    if (statOnSinceBoot) {
      msg += " (від запуску пристрою)";
    }
  } else if (!powerNow && statOffInSession) {
    msg += formatStatDuration((millis() - statOffStartMs) / 1000);
  } else if (!powerNow && !statResetDuringOff && stats.offOpen && stats.curOffStartEpoch != 0 && statsNowEpoch() >= stats.curOffStartEpoch) {
    msg += formatStatDuration(statsNowEpoch() - stats.curOffStartEpoch);
  } else {
    msg += "невідомо (початок не зафіксовано)";
  }
  msg += "\n\n";

  uint32_t totalOn = stats.totalOnSec;
  if (statOnObserving) {
    totalOn += (millis() - statOnSegMs) / 1000;
  }

  uint32_t displayedOffCount = stats.offCount;
  if (powerOutageCount > 0 && displayedOffCount < (uint32_t)powerOutageCount) displayedOffCount = (uint32_t)powerOutageCount;
  const uint32_t completedOffCount = displayedOffCount > (stats.offOpen ? 1U : 0U)
      ? displayedOffCount - (stats.offOpen ? 1U : 0U) : 0U;
  if (displayedOffCount == 0 && stats.exactCount == 0 && !stats.offOpen) {
    msg += "Статистика ще накопичується.";
    return msg;
  }

  msg += "📉 <b>Відключення</b>\n";
  msg += "• Завершених відключень: " + String(completedOffCount) + "\n";
  if (stats.offOpen) {
    msg += "• Поточне відключення: " + (currentOffDurationText().length() ? currentOffDurationText() : String("початок невідомий")) + "\n";
  }
  if (stats.exactCount != displayedOffCount) {
    msg += "• З точною тривалістю: " + String(stats.exactCount) + "\n";
  }
  if (stats.exactCount > 0) {
    msg += "• Загальний час без світла: " + formatStatDuration(stats.totalOffSec) + "\n";
    msg += "• Середнє відключення: " + formatStatDuration(stats.totalOffSec / stats.exactCount) + "\n";
    msg += "• Найдовше відключення: " + formatStatDuration(stats.maxOffSec) + "\n";
    msg += "• Найкоротше відключення: " + formatStatDuration(stats.minOffSec) + "\n";
  } else {
    msg += "• Тривалість: ще немає завершених відключень\n";
  }

  msg += "\n⚡ <b>Електроенергія</b>\n";
  msg += "• Загальний час зі світлом: " + formatStatDuration(totalOn) + "\n";
  msg += "• Останнє відключення: " + formatStatEpoch(stats.lastOffEpoch) + "\n";
  msg += "• Останнє відновлення: " + formatStatEpoch(stats.lastOnEpoch);
  msg += statsVoltageBlock();

  if (hist.count > 0) {
    msg += "\n\n🗂 <b>Останні відключення</b>\n";
    uint32_t shown = (hist.count < 10) ? hist.count : 10;
    for (uint32_t i = 0; i < shown; i++) {
      uint32_t idx = (hist.head + HIST_SIZE - 1 - i) % HIST_SIZE;
      HistRecord r = hist.rec[idx];
      String day = (r.offEpoch != 0) ? formatEpochLocal((time_t)r.offEpoch, "%d.%m") : String("--.--");
      String tOff = (r.offEpoch != 0) ? formatEpochLocal((time_t)r.offEpoch, "%H:%M") : String("--:--");
      String tOn = (r.onEpoch != 0) ? formatEpochLocal((time_t)r.onEpoch, "%H:%M") : String("--:--");
      String durText = (r.onEpoch == 0 && r.durSec == 0) ? String("тривалість невідома") : formatStatDuration(r.durSec);
      msg += "#" + String(r.id) + "  " + day + "  " + tOff + " – " + tOn + "  (" + durText + ")\n";
    }
  }
  return msg;
}

bool resetRemoveKeyVerified(const char* key) {
  if (!preferences.isKey(key)) return true;
  const bool removed = preferences.remove(key);
  const bool absent = !preferences.isKey(key);
  if (!removed || !absent) {
    Serial.println(String("[RESET] не вдалося звільнити NVS ключ: ") + key);
  }
  return removed && absent;
}

bool resetWriteBlobVerified(const char* key, const void* data, size_t len) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open("light-watcher", NVS_READWRITE, &handle);
  const char* stage = "open";
  bool ok = (err == ESP_OK);
  if (ok) {
    stage = "set_blob";
    err = nvs_set_blob(handle, key, data, len);
    ok = (err == ESP_OK);
  }
  if (ok) {
    stage = "commit";
    err = nvs_commit(handle);
    ok = (err == ESP_OK);
  }
  if (ok) {
    stage = "get_blob_length";
    size_t storedLen = 0;
    err = nvs_get_blob(handle, key, NULL, &storedLen);
    ok = (err == ESP_OK && storedLen == len);
    if (!ok && err == ESP_OK) err = ESP_ERR_INVALID_SIZE;
  }
  uint8_t* check = NULL;
  if (ok) {
    stage = "readback";
    check = (uint8_t*)malloc(len);
    if (check == NULL) {
      err = ESP_ERR_NO_MEM;
      ok = false;
    } else {
      size_t readLen = len;
      err = nvs_get_blob(handle, key, check, &readLen);
      ok = (err == ESP_OK && readLen == len && memcmp(check, data, len) == 0);
      if (!ok && err == ESP_OK) err = ESP_ERR_INVALID_SIZE;
    }
  }
  free(check);
  if (handle) nvs_close(handle);
  if (!ok) {
    Serial.printf("[RESET][NVS] %s failed at %s: %s (0x%X), bytes=%u\n",
                  key, stage, esp_err_to_name(err), (unsigned)err, (unsigned)len);
  } else {
    Serial.printf("[RESET][NVS] %s committed and read back (%u bytes)\n", key, (unsigned)len);
  }
  return ok;
}

bool statsResetAll(bool powerNow) {
  const uint32_t now = statsNowEpoch();

  uint32_t activeOffStart = 0;
  if (!powerNow) {
    if (now >= EPOCH_MIN_VALID) {
      activeOffStart = now;
      Serial.println("[RESET] active outage remains OFF; duration baseline starts at reset time");
    } else if (stats.offOpen && stats.curOffStartEpoch >= EPOCH_MIN_VALID) {
      activeOffStart = stats.curOffStartEpoch;
      Serial.println("[RESET] NTP unavailable; preserved valid active outage start");
    } else if (powerOffTimestamp >= (time_t)EPOCH_MIN_VALID) {
      activeOffStart = (uint32_t)powerOffTimestamp;
      Serial.println("[RESET] NTP unavailable; preserved valid power-off marker");
    } else {
      activeOffStart = 0;
      Serial.println("[RESET] NTP unavailable and no valid outage timestamp; duration remains unknown");
    }
  }

  statsTxnBlocked = false;
  statsPairDirty = false;
  statsUnread = false;
  histUnread = false;
  memset(&stats, 0, sizeof(PowerStats));
  memset(&hist, 0, sizeof(HistBuffer));
  statHistFixCount = 0;
  statOnFixPending = false;

  if (powerNow) {
    if (!statOnObserving) {
      statOnObserving = true;
      statOnPeriodStartMs = millis();
      statOnSinceBoot = true;
    }
    statOnSegMs = millis();
    statResetDuringOff = false;
    statOffInSession = false;
  } else {
    stats.offCount = 1;
    stats.curOffId = 1;
    stats.offOpen = 1;
    stats.curOffStartEpoch = activeOffStart;
    stats.lastOffEpoch = activeOffStart;
    statResetDuringOff = activeOffStart == 0;
    statResetMs = millis();
    statOffInSession = false;
  }

  statLastCheckpointMs = millis();
  statsBackupForce = true;

  lastOnDurationText = "";
  lastOffDurationText = "";
  const bool lastOnDurationRemoved = resetRemoveKeyVerified("lastOnDur");
  const bool lastOffDurationRemoved = resetRemoveKeyVerified("lastOffDur");

  const int32_t resetOutageCount = powerNow ? 0 : 1;
  powerOutageCount = resetOutageCount;
  powerOffTimestamp = powerNow ? 0 : (time_t)activeOffStart;
  lastOutageDetect = !powerNow;
  const bool markerCountOk = prefPutIntVerified("powerOutageCnt", resetOutageCount);
  const bool markerTimeOk = prefPutLong64Verified("pwrOffTmstmp", (int64_t)powerOffTimestamp);
  const bool markerFlagOk = prefPutBoolVerified("lastUotDetect", !powerNow);

  statsBackupDone = false;
  statsBackupOk = true;
  statsSaveOk = true;
  histSaveOk = true;
  const bool removeTxn = resetRemoveKeyVerified("statsTxn");
  const bool removeStats = resetRemoveKeyVerified("statsV1") && resetRemoveKeyVerified("statsB");
  const bool removeHist = resetRemoveKeyVerified("histV1") && resetRemoveKeyVerified("histB");

  const bool removedAll = removeTxn && removeStats && removeHist;
  statsBackupDone = true;
  statsBackupOff = stats.offCount;
  statsBackupMs = millis();
  statsBackupForce = false;
  statsBackupOk = true;
  statsSaveOk = false;
  histSaveOk = false;

  bool statsMainOk = false;
  bool histMainOk = false;
  if (removedAll) {
    stats.magic = STATS_MAGIC;
    stats.crc = statsCrc32((const uint8_t*)&stats, sizeof(PowerStats) - sizeof(uint32_t));
    hist.magic = HIST_MAGIC;
    hist.crc = statsCrc32((const uint8_t*)&hist, sizeof(HistBuffer) - sizeof(uint32_t));
    statsMainOk = resetWriteBlobVerified("statsV1", &stats, sizeof(PowerStats));
    histMainOk = resetWriteBlobVerified("histV1", &hist, sizeof(HistBuffer));
    statsSaveOk = statsMainOk;
    histSaveOk = histMainOk;
    statsBackupOk = !preferences.isKey("statsB") && !preferences.isKey("histB") &&
                    !preferences.isKey("statsTxn");
  }

  bool readbackOk = false;
  if (statsMainOk && histMainOk && statsBackupOk) {
    PowerStats checkStats;
    HistBuffer checkHist;
    readbackOk = nvsReadState("statsV1", &checkStats, sizeof(checkStats)) == 0 &&
      statsValid(checkStats) && memcmp(&checkStats, &stats, sizeof(PowerStats)) == 0 &&
      nvsReadState("histV1", &checkHist, sizeof(checkHist)) == 0 &&
      histValid(checkHist) && memcmp(&checkHist, &hist, sizeof(HistBuffer)) == 0 &&
      preferences.getInt("powerOutageCnt", -999) == resetOutageCount &&
      preferences.getLong64("pwrOffTmstmp", -1) == (int64_t)powerOffTimestamp &&
      preferences.getBool("lastUotDetect", !powerNow) == !powerNow &&
      !preferences.isKey("lastOnDur") && !preferences.isKey("lastOffDur");
  }
  const bool saved = removedAll && statsMainOk && histMainOk && statsBackupOk &&
    markerCountOk && markerTimeOk && markerFlagOk &&
    lastOnDurationRemoved && lastOffDurationRemoved && readbackOk;
  if (saved) {
    Serial.println("[RESET] primary statistics/history committed and verified; stale backups/transaction removed; active outage duration baseline reset to cleanup time");
  } else {
    Serial.printf("[RESET] power clear failed: removed=%s markers=%s/%s/%s durations=%s/%s readback=%s txn=%s statsV1=%s histV1=%s backupsAbsent=%s count=%ld timestamp=%lld flag=%d\n",
      removedAll ? "OK" : "FAIL", markerCountOk ? "OK" : "FAIL",
      markerTimeOk ? "OK" : "FAIL", markerFlagOk ? "OK" : "FAIL",
      lastOnDurationRemoved ? "OK" : "FAIL", lastOffDurationRemoved ? "OK" : "FAIL",
      readbackOk ? "OK" : "FAIL", preferences.isKey("statsTxn") ? "PRESENT" : "absent",
      statsMainOk ? "OK" : "FAIL", histMainOk ? "OK" : "FAIL", statsBackupOk ? "OK" : "FAIL",
      (long)preferences.getInt("powerOutageCnt", -999),
      (long long)preferences.getLong64("pwrOffTmstmp", -1),
      preferences.getBool("lastUotDetect", !powerNow) ? 1 : 0);
  }
  return saved;
}
