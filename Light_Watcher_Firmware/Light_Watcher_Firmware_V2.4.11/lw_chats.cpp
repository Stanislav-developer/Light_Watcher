#include "lw.h"

static bool chatLimitNoticed = false;

bool chatApproved(const ChatEntry& c) { return (c.pad[1] & CHAT_P_PENDING) == 0; }

String jsonStr(JsonVariant v) {
  return v.isNull() ? String("") : v.as<String>();
}

String jsonEsc(const String& s) {
  String o = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { o += ' '; }
    else { o += c; }
  }
  return o;
}

String jsonEscText(const String& s) {
  String o;
  o.reserve(s.length() + 16);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') { o += "\\n"; }
    else if (c == '\r') { o += "\\r"; }
    else if (c == '\t') { o += "\\t"; }
    else if ((uint8_t)c < 0x20) { o += ' '; }
    else { o += c; }
  }
  return o;
}

String htmlEsc(const String& s) {
  String o = s;
  o.replace("&", "&amp;");
  o.replace("<", "&lt;");
  o.replace(">", "&gt;");
  return o;
}

void chatCopyTitle(char* dst, size_t dstSize, const String& src) {
  size_t n = src.length();
  if (n > dstSize - 1) {
    n = dstSize - 1;
    while (n > 0 && (((uint8_t)src[n]) & 0xC0) == 0x80) {
      n--;
    }
  }
  memcpy(dst, src.c_str(), n);
  dst[n] = 0;
}

String chatShortTitle(const char* title, int maxChars) {
  String t = String(title);
  if (t.length() == 0) {
    return "Без назви";
  }
  int chars = 0;
  for (unsigned int i = 0; i < t.length(); i++) {
    if ((((uint8_t)t[i]) & 0xC0) != 0x80) {
      if (chars == maxChars) {
        return t.substring(0, i) + "...";
      }
      chars++;
    }
  }
  return t;
}

bool chatsValid(const ChatRegistry& t) {
  return t.magic == CHATS_MAGIC && t.crc == statsCrc32((const uint8_t*)&t, sizeof(ChatRegistry) - sizeof(uint32_t));
}

bool chatsReread() {
  ChatRegistry tmp;
  uint8_t st = nvsReadState("chatsV1", &tmp, sizeof(ChatRegistry));
  if (st == 3) {
    return false;
  }
  if (st == 0 && chatsValid(tmp)) {
    chats = tmp;
    logEvent(LC_STORE, LS_WARN, "Реєстр груп прочитано повторно - використано збережений");
  }
  chatsUnread = false;
  return true;
}

void chatsSave() {
  if (chatsUnread && !chatsReread()) {
    chatsSaveOk = false;
    return;
  }
  chats.magic = CHATS_MAGIC;
  chats.crc = statsCrc32((const uint8_t*)&chats, sizeof(ChatRegistry) - sizeof(uint32_t));
  bool okMain = nvsPutVerified("chatsV1", &chats, sizeof(ChatRegistry));

  bool okB = okMain && nvsPutVerified("chatsB", &chats, sizeof(ChatRegistry));
  chatsSaveOk = okMain && okB;
}

void chatsLoad() {
  memset(&chats, 0, sizeof(ChatRegistry));

  ChatRegistry tmp;
  uint8_t st = nvsReadState("chatsV1", &tmp, sizeof(ChatRegistry));
  if (st == 0 && chatsValid(tmp)) {
    chats = tmp;
    ChatRegistry b;
    if (preferences.isKey("chatsB") && !(nvsReadState("chatsB", &b, sizeof(ChatRegistry)) == 0 && chatsValid(b))) {
      chatsSaveOk = false;
    }
    return;
  }
  if (!preferences.isKey("chatsV1") && !preferences.isKey("chatsB")) {
    return;
  }
  if (nvsGetExact("chatsB", &tmp, sizeof(ChatRegistry)) && tmp.magic == CHATS_MAGIC && tmp.crc == statsCrc32((const uint8_t*)&tmp, sizeof(ChatRegistry) - sizeof(uint32_t))) {
    chats = tmp;
    logEvent(LC_STORE, LS_WARN, "Реєстр груп відновлено з резервної копії");
    statsLoadNote += "реєстр груп відновлено з резервної копії; ";
    chatsSaveOk = false;
    return;
  }
  if (st == 3) {
    chatsUnread = true;
    logEvent(LC_STORE, LS_WARN, "Реєстр груп не прочитано - запис призупинено до успішного читання");
    statsLoadNote += "реєстр груп не прочитано - запис призупинено; ";
    return;
  }
  if (preferences.isKey("chatsV1")) {
    Serial.println("Реєстр груп пошкоджений - починаємо заново");
    logEvent(LC_STORE, LS_ERR, "Реєстр груп пошкоджений - починаємо заново");
    statsLoadNote += "реєстр груп пошкоджений, придатної резервної копії немає; ";
  }
}

int chatFind(const String& id) {
  for (int i = 0; i < MAX_CHATS; i++) {
    if (chats.e[i].used && id == String(chats.e[i].id)) {
      return i;
    }
  }
  return -1;
}

int chatActiveCount() {
  int n = 0;
  for (int i = 0; i < MAX_CHATS; i++) {
    if (chats.e[i].used && chats.e[i].status > 0) {
      n++;
    }
  }
  return n;
}

uint8_t chatTypeCode(const String& t) {
  if (t == "group") return 1;
  if (t == "supergroup") return 2;
  if (t == "channel") return 3;
  return 0;
}

String chatTypeName(uint8_t t) {
  if (t == 1) return "група";
  if (t == 2) return "супергрупа";
  if (t == 3) return "канал";
  return "невідомо";
}

uint8_t chatStatusCode(const String& st, bool isMember) {
  if (st == "creator" || st == "administrator") return 2;
  if (st == "member") return 1;
  if (st == "restricted") return isMember ? 1 : 0;
  return 0;
}

int chatRegister(const String& id, const String& title, uint8_t type, uint8_t status, uint8_t canPost,
                 bool ownerApproved, const String& addedBy) {
  if (id.length() == 0 || id == chatId || type == 0) {
    return -1;
  }
  int i = chatFind(id);
  bool askOwner = false;
  if (i < 0) {
    for (int k = 0; k < MAX_CHATS && i < 0; k++) {
      if (!chats.e[k].used) i = k;
    }
    for (int k = 0; k < MAX_CHATS && i < 0; k++) {
      if (chats.e[k].status == 0) i = k;
    }
    for (int k = 0; k < MAX_CHATS && i < 0; k++) {
      if (!chatApproved(chats.e[k])) i = k;
    }
    if (i < 0) {
      if (!chatLimitNoticed || millis() - chatLimitNoticeMs >= CHAT_LIMIT_NOTICE_MS) {
        chatLimitNoticed = true;
        chatLimitNoticeMs = millis();
        tgSendOwnerNotice("⚠️ <b>Ліміт чатів досягнуто</b>\nМаксимум: " + String(MAX_CHATS) +
                          " груп і каналів. Чат «" + htmlEsc(title) + "» не зареєстровано.");
      }
      return -1;
    }
    memset(&chats.e[i], 0, sizeof(ChatEntry));
    chatCopyTitle(chats.e[i].id, sizeof(chats.e[i].id), id);
    chats.e[i].used = 1;

    if (ownerApproved) {
      chats.e[i].flags = CHAT_F_NOTIFY;
    } else {
      chats.e[i].flags = 0;
      chats.e[i].pad[1] |= CHAT_P_PENDING;
      askOwner = true;
    }
    Serial.println("Зареєстровано чат: " + title + " (" + id + ")" + (ownerApproved ? "" : " - очікує підтвердження"));
  } else if (ownerApproved && !chatApproved(chats.e[i])) {
    chats.e[i].pad[1] &= (uint8_t)~CHAT_P_PENDING;
    chats.e[i].flags = CHAT_F_NOTIFY;
  }
  if (title.length() > 0) {
    chatCopyTitle(chats.e[i].title, sizeof(chats.e[i].title), title);
  }
  chats.e[i].type = type;

  chats.e[i].status = status;
  chats.e[i].canPost = canPost;
  chatsSave();
  if (askOwner) chatAskApproval(i, addedBy);
  return i;
}

void chatAskApproval(int i, const String& addedBy) {
  if (i < 0 || i >= MAX_CHATS || !chats.e[i].used) return;
  const String id = String(chats.e[i].id);
  String t = "🔔 <b>Light Watcher додано в новий чат</b>\n";
  t += "«" + htmlEsc(chatShortTitle(chats.e[i].title, 40)) + "» (" + chatTypeName(chats.e[i].type) + ")\n";
  t += "Chat ID: <code>" + htmlEsc(id) + "</code>\n";
  if (addedBy.length() > 0) t += "Додав: " + htmlEsc(addedBy) + "\n";
  t += "\nСповіщення й відповіді туди не надсилатимуться, доки ви не дозволите.";
  if (bot.sendMessageWithInlineKeyboard(chatId, t, "HTML",
        confirmKeyboard("✅ Дозволити", "x:apY:" + id, "🚫 Відхилити", "x:apN:" + id))) {
    tgKeepAdd((int32_t)bot.last_sent_message_id);
  }
}

bool chatApprove(int i) {
  if (i < 0 || i >= MAX_CHATS || !chats.e[i].used) return false;
  if (chatApproved(chats.e[i])) return true;
  const uint8_t oldPad = chats.e[i].pad[1], oldFlags = chats.e[i].flags;
  chats.e[i].pad[1] &= (uint8_t)~CHAT_P_PENDING;
  chats.e[i].flags = CHAT_F_NOTIFY;
  chatsSave();
  if (!chatsSaveOk) {
    chats.e[i].pad[1] = oldPad;
    chats.e[i].flags = oldFlags;
    chatsSave();
    return false;
  }
  logEvent(LC_TG, LS_INFO, "Власник дозволив чат " + String(chats.e[i].id));
  return true;
}

void chatRefreshAccess(int i) {
  if (i < 0 || botIdStr.length() == 0) {
    return;
  }
  String resp = telegramPostRaw("getChatMember", "{\"chat_id\":\"" + String(chats.e[i].id) + "\",\"user_id\":" + botIdStr + "}");
  if (resp.length() == 0) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    return;
  }
  if (doc["ok"].as<bool>()) {
    JsonObject r = doc["result"];
    String st = jsonStr(r["status"]);
    chats.e[i].status = chatStatusCode(st, r["is_member"].as<bool>());
    if (chats.e[i].type == 3) {
      chats.e[i].canPost = (st == "creator" || r["can_post_messages"].as<bool>()) ? 1 : 0;
    } else {
      chats.e[i].canPost = 1;
    }
  } else {
    int code = doc["error_code"].as<int>();
    if (code == 400 || code == 403) {
      chats.e[i].status = 0;
    }
  }
  chatsSave();
}

void chatRefreshInfo(int i) {
  if (i < 0) {
    return;
  }
  String resp = telegramPostRaw("getChat", "{\"chat_id\":\"" + String(chats.e[i].id) + "\"}");
  JsonDocument doc;
  if (resp.length() == 0 || deserializeJson(doc, resp) || !doc["ok"].as<bool>()) {
    return;
  }
  JsonObject r = doc["result"];
  String title = jsonStr(r["title"]);
  if (title.length() > 0) {
    chatCopyTitle(chats.e[i].title, sizeof(chats.e[i].title), title);
  }
  uint8_t t = chatTypeCode(jsonStr(r["type"]));
  if (t != 0) {
    chats.e[i].type = t;
  }
  chatsSave();
}

bool telegramLoadBotInfo() {
  String resp = bot.sendGetToTelegram(bot.buildCommand("getMe"));
  JsonDocument doc;
  if (resp.length() == 0 || deserializeJson(doc, resp) || !doc["ok"].as<bool>()) {
    Serial.println("getMe: помилка");
    return false;
  }
  botIdStr = jsonStr(doc["result"]["id"]);
  botCanReadAllGroup = doc["result"]["can_read_all_group_messages"].as<bool>();
  return true;
}

void chatsBegin() {
  chatsLoad();
  ownerNotifyOn = preferences.getBool("oNotify", true);
  ownerVoltAlertOn = preferences.getBool("oVAlert", false);
  ownerVoltInLightOn = preferences.getBool("oVInOn", false);
  ownerYasnoPublishOn = preferences.getBool("oYsPub", false);
  ownerYasnoEmergencyOn = preferences.getBool("oYsEm", false);
  ownerYasnoQueryOn = preferences.getBool("oYsQry", false);
  ownerYasnoEventOn = preferences.getBool("oYsEvt", false);
}

void chatsOnlineInit() {
  if (groupId.length() > 0 && groupId != "1" && groupId != chatId && chatFind(groupId) < 0) {
    int i = chatRegister(groupId, "", 2, 1, 1, true, "");
    if (i >= 0) {
      chats.e[i].flags = CHAT_F_NOTIFY;
      chatRefreshInfo(i);
      wdtFeed();
      chatRefreshAccess(i);
    }
  }
}

bool chatAllows(const String& id, uint8_t flag) {
  if (id == chatId) {
    if (flag == CHAT_F_NOTIFY) return ownerNotifyOn;
    if (flag == CHAT_F_VALERT) return vAlertOn && ownerVoltAlertOn;
    if (flag == CHAT_F_VOLT_IN_LIGHT) return ownerVoltInLightOn;
    if (flag == CHAT_F_YASNO_QUERY) return ownerYasnoQueryOn;
    if (flag == CHAT_F_YASNO_PUBLISH) return ownerYasnoPublishOn;
    if (flag == CHAT_F_YASNO_EMERGENCY) return ownerYasnoEmergencyOn;
    if (flag == CHAT_F_LIGHT || flag == CHAT_F_VOLT) return true;
    return false;
  }

  int i = chatFind(id);
  if (i < 0 || !chats.e[i].used || chats.e[i].status == 0 || !chatApproved(chats.e[i])) return false;
  if (flag == CHAT_F_VALERT && !vAlertOn) return false;
  return (chats.e[i].flags & flag) != 0;
}
