#include "lw.h"

bool prefPutBoolVerified(const char* key, bool value) {
  size_t n = preferences.putBool(key, value);
  bool ok = n == sizeof(bool) && preferences.getBool(key, !value) == value;
  if (!ok) Serial.println(String("NVS: не підтверджено bool ") + key);
  return ok;
}
bool toggleNotificationPref(bool& target, const char* key) {
  bool nextValue = !target;
  if (!prefPutBoolVerified(key, nextValue)) return false;
  target = nextValue;
  return true;
}
bool prefPutStrVerified(const char* key, const String& value) {
  preferences.putString(key, value);
  bool ok = preferences.isKey(key) && preferences.getString(key, "") == value;
  if (!ok) Serial.println(String("NVS: не підтверджено string ") + key);
  return ok;
}
bool prefPutIntVerified(const char* key, int32_t value) {
  size_t n = preferences.putInt(key, value);
  bool ok = n == sizeof(int32_t) && preferences.getInt(key, value ^ 0x5A5A5A5A) == value;
  if (!ok) Serial.println(String("NVS: не підтверджено int ") + key);
  return ok;
}
bool prefPutLong64Verified(const char* key, int64_t value) {
  size_t n = preferences.putLong64(key, value);
  bool ok = n == sizeof(int64_t) && preferences.getLong64(key, value ^ 0x5A5A5A5A5A5A5A5ALL) == value;
  if (!ok) Serial.println(String("NVS: не підтверджено int64 ") + key);
  return ok;
}
uint32_t uptimeSec() { return (uint32_t)(esp_timer_get_time() / 1000000LL); }

bool nvsPutVerified(const char* key, const void* data, size_t len) {

  if (preferences.getBytesLength(key) == len) {
    uint8_t* cur = (uint8_t*)malloc(len);
    bool same = cur != NULL && preferences.getBytes(key, cur, len) == len && memcmp(cur, data, len) == 0;
    free(cur);
    if (same) {
      return true;
    }
  }
  const char* failStage = "putBytes";
  size_t written = preferences.putBytes(key, data, len);
  bool ok = written == len;
  if (ok) {
    failStage = "length";
    ok = preferences.getBytesLength(key) == len;
  }
  if (ok) {
    failStage = "malloc/readback";
    uint8_t* chk = (uint8_t*)malloc(len);
    ok = chk != NULL && preferences.getBytes(key, chk, len) == len;
    if (ok) {
      failStage = "memcmp";
      ok = memcmp(chk, data, len) == 0;
    }
    free(chk);
  }
  if (!ok) {
    nvsFailCount++;
    nvsFailKey = key;
    nvsFailMs = millis();
    if (nvsFailMs == 0) nvsFailMs = 1;
    nvs_stats_t ns;
    String capacity = nvs_get_stats(NULL, &ns) == ESP_OK
      ? String("; NVS вільно ") + String((unsigned)(ns.total_entries - ns.used_entries)) + "/" + String((unsigned)ns.total_entries)
      : String("; NVS stats недоступна");
    logEvent(LC_STORE, LS_ERR, String("NVS ") + key + ": етап " + failStage + ", байтів " + String((unsigned)written) + "/" + String((unsigned)len) + capacity);
  }
  return ok;
}

bool nvsGetExact(const char* key, void* data, size_t len) {
  static const uint16_t pauseMs[3] = { 0, 20, 100 };
  for (int a = 0; a < 3; a++) {
    if (pauseMs[a] > 0) {
      delay(pauseMs[a]);
    }
    if (preferences.isKey(key) && preferences.getBytesLength(key) == len && preferences.getBytes(key, data, len) == len) {
      return true;
    }
  }
  return false;
}

uint8_t nvsReadState(const char* key, void* data, size_t len) {
  if (!preferences.isKey(key)) {
    return 1;
  }
  size_t have = preferences.getBytesLength(key);
  if (have == 0) {
    return 3;
  }
  if (have != len) {
    return 2;
  }
  return nvsGetExact(key, data, len) ? 0 : 3;
}

void nvsNoteLoad(const char* what, uint8_t st) {
  const char* why = st == 2 ? " неправильного розміру - почато заново" : st == 3 ? " не прочитано - запис призупинено до успішного читання"
                                                                           : " пошкоджено (структура/CRC) - почато заново";
  nvsLoadNote += String(what) + why + "; ";
  logEvent(LC_STORE, st == 3 ? LS_WARN : LS_ERR, String(what) + ":" + why);
}
