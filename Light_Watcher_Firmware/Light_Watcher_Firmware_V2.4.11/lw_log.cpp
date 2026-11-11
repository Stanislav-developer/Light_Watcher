#include "lw.h"

static LogEntry logRam[LOG_RAM];
static int logHead = 0;
static int logCount = 0;
static LogStore logStore;

const char* logCatName(uint8_t c) {
  static const char* n[] = { "WIFI", "TELEGRAM", "SHELLY", "ADC", "NTP", "WEB", "MEMORY", "WATCHDOG", "STORAGE", "SYSTEM" };
  return (c < 10) ? n[c] : "?";
}

void logCopyMsg(char* dst, size_t cap, const String& src) {
  size_t n = src.length();
  if (n > cap - 1) {
    n = cap - 1;
    while (n > 0 && (((uint8_t)src[n]) & 0xC0) == 0x80) {
      n--;
    }
  }
  memcpy(dst, src.c_str(), n);
  dst[n] = 0;
}

void logPushRam(const LogEntry& e) {
  logRam[logHead] = e;
  logHead = (logHead + 1) % LOG_RAM;
  if (logCount < LOG_RAM) {
    logCount++;
  }
}

bool logStoreValid(const LogStore& l) {
  return l.magic == LOG_MAGIC && l.head < LOG_NVS && l.count <= LOG_NVS && l.crc == statsCrc32((const uint8_t*)&l, sizeof(LogStore) - sizeof(uint32_t));
}

bool logReread() {
  LogStore* l = (LogStore*)malloc(sizeof(LogStore));
  if (l == NULL) {
    return false;
  }
  uint8_t st = nvsReadState("logV1", l, sizeof(LogStore));
  if (st == 3) {
    free(l);
    return false;
  }
  if (st == 0 && logStoreValid(*l)) {
    for (int i = 0; i < LOG_NVS; i++) l->e[i].prev = 1;
    for (int i = 0; i < logStore.count; i++) {
      l->e[l->head] = logStore.e[(logStore.head + LOG_NVS - logStore.count + i) % LOG_NVS];
      l->head = (l->head + 1) % LOG_NVS;
      if (l->count < LOG_NVS) l->count++;
    }
    logStore = *l;
  }
  free(l);
  logUnread = false;
  return true;
}

void logSave() {
  if (logUnread && !logReread()) {
    logLastSave = millis();
    logSaveFailed = true;
    return;
  }
  logStore.magic = LOG_MAGIC;
  logStore.crc = statsCrc32((const uint8_t*)&logStore, sizeof(LogStore) - sizeof(uint32_t));
  logLastSave = millis();
  logSaveFailed = !nvsPutVerified("logV1", &logStore, sizeof(LogStore));
  if (!logSaveFailed) {
    logDirty = false;
  }
}

void logBegin() {
  memset(&logStore, 0, sizeof(LogStore));
  uint8_t st = nvsReadState("logV1", &logStore, sizeof(LogStore));
  if (st == 0 && !logStoreValid(logStore)) {
    st = 4;
  }
  if (st != 0) {
    memset(&logStore, 0, sizeof(LogStore));
    if (st != 1) {

      nvsLoadNote += String("журнал подій") + (st == 2 ? " неправильного розміру - почато заново" : st == 3 ? " не прочитано - запис призупинено" : " пошкоджено - почато заново") + "; ";
    }
    logUnread = (st == 3);
  }
  for (int i = 0; i < LOG_NVS; i++) logStore.e[i].prev = 1;
  for (int i = 0; i < logStore.count; i++) {
    int idx = (logStore.head + LOG_NVS - logStore.count + i) % LOG_NVS;
    logPushRam(logStore.e[idx]);
  }
}

void logEvent(uint8_t cat, uint8_t sev, const String& msg) {
  LogEntry e;
  memset(&e, 0, sizeof(e));
  e.t = statsNowEpoch();
  e.up = uptimeSec();
  e.cat = cat;
  e.sev = sev;
  logCopyMsg(e.msg, sizeof(e.msg), msg);
  if (logCount > 0) {
    const LogEntry& last = logRam[(logHead + LOG_RAM - 1) % LOG_RAM];
    if (!last.prev && last.cat == e.cat && strcmp(last.msg, e.msg) == 0 && e.up - last.up < 60) {
      return;
    }
  }
  logPushRam(e);
  Serial.println(String("[") + logCatName(cat) + "] " + msg);
  if (sev >= LS_WARN) {
    logIssues24++;
    logStore.e[logStore.head] = e;
    logStore.head = (logStore.head + 1) % LOG_NVS;
    if (logStore.count < LOG_NVS) {
      logStore.count++;
    }
    logDirty = true;
  }
}

void logFlush() {
  if (logDirty) {
    logSave();
  }
}

void logTick() {
  static unsigned long lastFix = 0;
  static bool ntpLogged = false;
  if (millis() - lastFix >= 10000UL) {
    lastFix = millis();
    uint32_t now = statsNowEpoch();
    if (now != 0 && !ntpLogged) {
      ntpLogged = true;
      logEvent(LC_NTP, LS_INFO, "Час синхронізовано через " + String(uptimeSec()) + " с після запуску");
    }
    if (now != 0) {
      uint32_t upNow = uptimeSec();
      for (int i = 0; i < LOG_RAM; i++) {
        if (logRam[i].t == 0 && !logRam[i].prev && logRam[i].up <= upNow && (logRam[i].msg[0] != 0)) {
          logRam[i].t = now - (upNow - logRam[i].up);
        }
      }
      for (int i = 0; i < LOG_NVS; i++) {
        if (logStore.e[i].t == 0 && !logStore.e[i].prev && logStore.e[i].msg[0] != 0 && logStore.e[i].up <= upNow) {
          logStore.e[i].t = now - (upNow - logStore.e[i].up);
          logDirty = true;
        }
      }
    }
  }
  if (logDirty && millis() - logLastSave >= LOG_SAVE_MS) {
    logSave();
  }
}

String logTimeLabel(const LogEntry& e) {
  if (e.t != 0) {
    return formatEpochLocal((time_t)e.t, "%d.%m %H:%M");
  }
  return String(e.prev ? "попер. сеанс, " : "") + "+" + String(e.up) + " с від запуску";
}

String logIssuesText(int max) {
  String t = "📜 <b>Журнал: попередження й помилки</b>\n";
  int shown = 0;
  for (int i = 0; i < logCount && shown < max; i++) {
    const LogEntry& e = logRam[(logHead + LOG_RAM - 1 - i) % LOG_RAM];
    if (e.sev < LS_WARN) {
      continue;
    }
    t += String(e.sev == LS_ERR ? "🔴 " : "🟡 ") + logTimeLabel(e) + " [" + logCatName(e.cat) + "] " + htmlEsc(String(e.msg)) + "\n";
    shown++;
  }
  if (shown == 0) {
    t += "Попереджень і помилок немає ✅\n";
  }
  t += "\nПовний журнал (з інформаційними подіями) - в адмінпанелі.";
  return t;
}

String logJson() {
  String j = "[";
  for (int i = 0; i < logCount; i++) {
    const LogEntry& e = logRam[(logHead + LOG_RAM - 1 - i) % LOG_RAM];
    j += String(i ? "," : "") + "{\"t\":" + String(e.t) + ",\"up\":" + String(e.up) + ",\"c\":\"" + logCatName(e.cat) + "\",\"s\":" + String(e.sev)
         + ",\"p\":" + String(e.prev) + ",\"m\":\"" + jsonEsc(String(e.msg)) + "\"}";
  }
  return j + "]";
}
