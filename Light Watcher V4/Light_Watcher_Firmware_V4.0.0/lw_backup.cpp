#include "lw.h"

static const char* const BK_BOOL_KEYS[] = { "oNotify", "blipOn", "oVInOn", "oYsEvt", "oVAlert", "oYsPub", "oYsPin", "oYsEm", "oYsQry", "oWeek", "vAlert", "webDomOn", "webCfOnly", "webIpMan", "autoDst" };
static const char* const BK_INT_KEYS[] = { "oYsWarn", "vLow", "vHigh", "vStLow", "vStHigh", "vIntSec", "vConfirm", "vPauseMin", "srvHbOn", "srvHbOff", "srvParts", "wkH" };
static const char* const BK_STR_KEYS[] = { "ysCity", "ysStreet", "ysHouse", "ysGroup", "webDomain", "webIpVal", "tz-rule", "imgStore" };

static bool bkInList(const char* const* list, size_t n, const char* key) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(list[i], key) == 0) return true;
  }
  return false;
}

String bkBuildJson() {
  JsonDocument d;
  d["v"] = BK_FORMAT;
  d["fw"] = LW_VERSION;
  JsonObject b = d["b"].to<JsonObject>();
  for (size_t i = 0; i < sizeof(BK_BOOL_KEYS) / sizeof(BK_BOOL_KEYS[0]); i++) {
    if (preferences.isKey(BK_BOOL_KEYS[i])) b[BK_BOOL_KEYS[i]] = preferences.getBool(BK_BOOL_KEYS[i], false) ? 1 : 0;
  }
  JsonObject n = d["i"].to<JsonObject>();
  for (size_t i = 0; i < sizeof(BK_INT_KEYS) / sizeof(BK_INT_KEYS[0]); i++) {
    if (preferences.isKey(BK_INT_KEYS[i])) n[BK_INT_KEYS[i]] = preferences.getInt(BK_INT_KEYS[i], 0);
  }
  JsonObject t = d["t"].to<JsonObject>();
  for (size_t i = 0; i < sizeof(BK_STR_KEYS) / sizeof(BK_STR_KEYS[0]); i++) {
    if (preferences.isKey(BK_STR_KEYS[i])) t[BK_STR_KEYS[i]] = preferences.getString(BK_STR_KEYS[i], "");
  }
  JsonArray c = d["c"].to<JsonArray>();
  for (int i = 0; i < MAX_CHATS; i++) {
    const ChatEntry& e = chats.e[i];
    if (!e.used) continue;
    JsonObject o = c.add<JsonObject>();
    o["id"] = e.id;
    o["n"] = e.title;
    o["ty"] = e.type;
    o["s"] = e.status;
    o["f"] = e.flags;
    o["cp"] = e.canPost;
    JsonArray p = o["p"].to<JsonArray>();
    for (int k = 0; k < 3; k++) p.add(e.pad[k]);
  }
  String out;
  serializeJson(d, out);
  return out;
}

String bkParse(const String& json, BkData& out) {
  memset(&out.chats, 0, sizeof(ChatRegistry));
  out.nb = out.ni = out.nt = 0;
  out.chatCount = 0;
  JsonDocument d;
  if (deserializeJson(d, json) || !d.is<JsonObject>()) return "копія пошкоджена";
  if ((d["v"] | 0) != BK_FORMAT) return "копія іншого формату - оновіть прошивку";
  for (JsonPairConst kv : d["b"].as<JsonObjectConst>()) {
    if (!bkInList(BK_BOOL_KEYS, sizeof(BK_BOOL_KEYS) / sizeof(BK_BOOL_KEYS[0]), kv.key().c_str()) || out.nb >= BK_MAX_KEYS) continue;
    if (!kv.value().is<int>()) return "некоректне значення " + String(kv.key().c_str());
    out.bk[out.nb] = kv.key().c_str();
    out.bv[out.nb++] = (kv.value().as<int>() != 0);
  }
  for (JsonPairConst kv : d["i"].as<JsonObjectConst>()) {
    if (!bkInList(BK_INT_KEYS, sizeof(BK_INT_KEYS) / sizeof(BK_INT_KEYS[0]), kv.key().c_str()) || out.ni >= BK_MAX_KEYS) continue;
    if (!kv.value().is<int32_t>()) return "некоректне значення " + String(kv.key().c_str());
    out.ik[out.ni] = kv.key().c_str();
    out.iv[out.ni++] = kv.value().as<int32_t>();
  }
  for (JsonPairConst kv : d["t"].as<JsonObjectConst>()) {
    if (!bkInList(BK_STR_KEYS, sizeof(BK_STR_KEYS) / sizeof(BK_STR_KEYS[0]), kv.key().c_str()) || out.nt >= BK_MAX_KEYS) continue;
    if (!kv.value().is<const char*>()) return "некоректне значення " + String(kv.key().c_str());
    const String v = kv.value().as<const char*>();
    if (v.length() > BK_STR_MAX) return "задовге значення " + String(kv.key().c_str());
    out.tk[out.nt] = kv.key().c_str();
    out.tv[out.nt++] = v;
  }
  int slot = 0;
  for (JsonObjectConst o : d["c"].as<JsonArrayConst>()) {
    if (slot >= MAX_CHATS) return "забагато груп у копії";
    const char* id = o["id"] | "";
    const char* title = o["n"] | "";
    if (strlen(id) == 0 || strlen(id) >= sizeof(out.chats.e[0].id) || strlen(title) >= sizeof(out.chats.e[0].title)) return "некоректна група в копії";
    ChatEntry& e = out.chats.e[slot++];
    strncpy(e.id, id, sizeof(e.id) - 1);
    strncpy(e.title, title, sizeof(e.title) - 1);
    e.used = 1;
    e.type = (uint8_t)(o["ty"] | 0);
    e.status = (uint8_t)(o["s"] | 0);
    e.flags = (uint8_t)(o["f"] | 0);
    e.canPost = (uint8_t)(o["cp"] | 0);
    JsonArrayConst p = o["p"].as<JsonArrayConst>();
    for (int k = 0; k < 3 && k < (int)p.size(); k++) e.pad[k] = (uint8_t)(p[k] | 0);
  }
  out.chatCount = slot;
  return "";
}

String bkSummary(const BkData& b) {
  String street = "", house = "", group = "";
  for (uint8_t i = 0; i < b.nt; i++) {
    if (b.tk[i] == "ysStreet") street = b.tv[i];
    else if (b.tk[i] == "ysHouse") house = b.tv[i];
    else if (b.tk[i] == "ysGroup") group = b.tv[i];
  }
  int32_t lo = 0, hi = 0;
  for (uint8_t i = 0; i < b.ni; i++) {
    if (b.ik[i] == "vLow") lo = b.iv[i];
    else if (b.ik[i] == "vHigh") hi = b.iv[i];
  }
  String s = "Групи й канали: " + String(b.chatCount);
  s += "\nАдреса YASNO: " + (street.length() ? htmlEsc(street + ", " + house) + (group.length() ? " · черга " + htmlEsc(group) : String("")) : String("немає"));
  if (lo && hi) s += "\nНапруга: " + String(lo) + "–" + String(hi) + " В";
  return s;
}

static bool bkHas(const String* keys, uint8_t n, const char* key) {
  for (uint8_t i = 0; i < n; i++) {
    if (keys[i] == key) return true;
  }
  return false;
}

static bool bkRemoveAbsent(const char* const* list, size_t n, const String* keys, uint8_t got) {
  bool ok = true;
  for (size_t i = 0; i < n; i++) {
    if (!bkHas(keys, got, list[i]) && preferences.isKey(list[i])) ok = preferences.remove(list[i]) && ok;
  }
  return ok;
}

String bkRestoreBody() {
  const String json = bkBuildJson();
  return "{\"c\":" + String(statsCrc32((const uint8_t*)json.c_str(), json.length())) + "}";
}

uint32_t bkRetryMs(int status) {
  return (status >= 400 && status < 500) ? BK_CHECK_MS : BK_RETRY_MS;
}

bool bkApply(const BkData& b) {
  bool ok = true;
  String group = "";
  for (uint8_t i = 0; i < b.nt; i++) {
    if (b.tk[i] == "ysGroup") group = b.tv[i];
  }
  const bool groupChanged = group != preferences.getString("ysGroup", "");
  ok = bkRemoveAbsent(BK_BOOL_KEYS, sizeof(BK_BOOL_KEYS) / sizeof(BK_BOOL_KEYS[0]), b.bk, b.nb) && ok;
  ok = bkRemoveAbsent(BK_INT_KEYS, sizeof(BK_INT_KEYS) / sizeof(BK_INT_KEYS[0]), b.ik, b.ni) && ok;
  ok = bkRemoveAbsent(BK_STR_KEYS, sizeof(BK_STR_KEYS) / sizeof(BK_STR_KEYS[0]), b.tk, b.nt) && ok;
  for (uint8_t i = 0; i < b.nb; i++) ok = prefPutBoolVerified(b.bk[i].c_str(), b.bv[i]) && ok;
  for (uint8_t i = 0; i < b.ni; i++) ok = prefPutIntVerified(b.ik[i].c_str(), b.iv[i]) && ok;
  for (uint8_t i = 0; i < b.nt; i++) ok = prefPutStrVerified(b.tk[i].c_str(), b.tv[i]) && ok;
  for (uint8_t i = 0; i < b.ni; i++) {
    if (b.ik[i] == "srvParts") preferences.putUChar("srvPv", 1);
  }
  if (groupChanged) yasnoClearScheduleCache();
  if (chatsUnread) chatsReread();
  chats = b.chats;
  chatsSave();
  return ok && chatsSaveOk;
}
