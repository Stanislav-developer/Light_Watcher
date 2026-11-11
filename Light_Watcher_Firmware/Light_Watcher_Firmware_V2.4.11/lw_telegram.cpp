#include "lw.h"

static bool tgPollSynced = false;
static String tgDocId = "";
static String tgDocName = "";
static long tgDocSize = 0;

String normalizeCommand(String s) {
  s.trim();
  String out = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c == 0xD0 && i + 1 < s.length()) {
      uint8_t n = (uint8_t)s[i + 1];
      if (n >= 0x90 && n <= 0x9F) { out += (char)0xD0; out += (char)(n + 0x20); i++; continue; }
      if (n >= 0xA0 && n <= 0xAF) { out += (char)0xD1; out += (char)(n - 0x20); i++; continue; }
      if (n == 0x81 || n == 0x84 || n == 0x86 || n == 0x87) { out += (char)0xD1; out += (char)(n + 0x10); i++; continue; }
    }
    if (c == 0xD2 && i + 1 < s.length() && (uint8_t)s[i + 1] == 0x90) { out += (char)0xD2; out += (char)0x91; i++; continue; }
    if (c >= 'A' && c <= 'Z') { out += (char)(c + 32); continue; }
    out += (char)c;
  }

  while (out.length() > 0) {
    char last = out[out.length() - 1];
    if (last == '?' || last == '!' || last == '.' || last == ' ') {
      out.remove(out.length() - 1);
    } else {
      break;
    }
  }
  while (out.indexOf("  ") >= 0) {
    out.replace("  ", " ");
  }
  return out;
}

bool telegramPostJson(const char* method, const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    Serial.println(String("Telegram ") + method + ": помилка JSON");
    return false;
  }
  wdtFeed();
  String resp = bot.sendPostToTelegram(bot.buildCommand(method), doc.as<JsonObject>());
  wdtFeed();
  bool ok = resp.indexOf("\"ok\":true") >= 0;
  Serial.println(String("Telegram ") + method + (ok ? ": OK" : ": помилка"));
  return ok;
}

void setupTelegramCommandMenus() {
  const String id = botToken + "|" + chatId;
  const uint32_t sig = statsCrc32((const uint8_t*)id.c_str(), id.length());
  if (preferences.getUInt("tgCmdSig", 0) == sig) return;

  bool ok = telegramPostJson("deleteMyCommands", "{\"scope\":{\"type\":\"default\"}}");
  ok = telegramPostJson("deleteMyCommands", "{\"scope\":{\"type\":\"all_group_chats\"}}") && ok;
  ok = telegramPostJson("deleteMyCommands", "{\"scope\":{\"type\":\"all_chat_administrators\"}}") && ok;
  ok = telegramPostJson("deleteMyCommands", "{\"scope\":{\"type\":\"all_private_chats\"}}") && ok;

  if (chatId.length() > 0) {
    String scope = "{\"scope\":{\"type\":\"chat\",\"chat_id\":\"" + jsonEsc(chatId) + "\"}}";
    ok = telegramPostJson("deleteMyCommands", scope) && ok;
  }
  if (ok) preferences.putUInt("tgCmdSig", sig);
}

String telegramPostRaw(const char* method, const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return "";
  }
  return bot.sendPostToTelegram(bot.buildCommand(method), doc.as<JsonObject>());
}

bool broadcastMessageFlag(const String& message, uint8_t flag) {
  return broadcastMessageTo(message, flag, RCPT_ALL, 0xFF, nullptr) == 0;
}

uint16_t broadcastNotifyTo(const String& message, uint16_t pending, uint8_t maxSends, uint16_t* tried) {
  return broadcastMessageTo(message, CHAT_F_NOTIFY, pending, maxSends, tried);
}

void grpNoteFail(const String& name) {
  grpFailTotal++;
  grpFailLast = name;
  grpFailLastMs = millis();
  if (grpFailLastMs == 0) grpFailLastMs = 1;
  logEvent(LC_TG, LS_WARN, "Не доставлено в групу/канал: " + name);
}

String powerScheduleMessageForRecipient(const String& message, bool includeSchedule) {
  if (includeSchedule) return message;
  int start = message.indexOf("📅 ");
  if (start < 0) return message;
  int end = message.indexOf('\n', start);
  String head = message.substring(0, start);
  while (head.endsWith("\n\n")) head.remove(head.length() - 1);
  if (end < 0) {
    while (head.endsWith("\n")) head.remove(head.length() - 1);
    return head;
  }
  return head + message.substring(end + 1);
}

String powerOnMessageForRecipient(const String& message, bool includeVoltage) {
  if (includeVoltage || message.indexOf("Світло з'явилося") < 0) return message;
  int start = message.indexOf("⚡ Напруга:");
  if (start < 0) return message;
  int end = message.indexOf('\n', start);
  if (end < 0) return message.substring(0, start);
  return message.substring(0, start) + message.substring(end + 1);
}

uint16_t broadcastMessageTo(const String& message, uint8_t flag, uint16_t pending, uint8_t maxSends, uint16_t* tried) {
  uint16_t left = 0;
  uint8_t sends = 0;
  if (tried) *tried = 0;

  if ((pending & RCPT_OWNER) && chatAllows(chatId, flag) && sends >= maxSends) {
    left |= RCPT_OWNER;
  } else if ((pending & RCPT_OWNER) && chatAllows(chatId, flag)) {
    sends++;
    if (tried) *tried |= RCPT_OWNER;
    String ownerMessage = message;
    if (flag == CHAT_F_NOTIFY) {
      ownerMessage = powerOnMessageForRecipient(ownerMessage, ownerVoltInLightOn);
      ownerMessage = powerScheduleMessageForRecipient(ownerMessage, ownerYasnoEventOn);
    }
    if (tgSendOwnerNotice(ownerMessage)) {
      telegramOnline = true;
      telegramFailCount = 0;
      tgLastOkMs = millis();
      tlsNoteSuccess();
    } else {
      left |= RCPT_OWNER;
      telegramOnline = false;
      tlsNoteFailure();
      logEvent(LC_TG, LS_WARN, "Не доставлено особисте сповіщення власнику");
    }
  }

  for (int i = 0; i < MAX_CHATS; i++) {
    if (!(pending & (1U << i))) continue;
    ChatEntry& entry = chats.e[i];
    if (!entry.used || entry.status == 0 || !entry.canPost || String(entry.id) == chatId) continue;
    if (!chatAllows(String(entry.id), flag)) continue;
    if (sends >= maxSends) {
      left |= (uint16_t)(1U << i);
      continue;
    }
    sends++;
    if (tried) *tried |= (uint16_t)(1U << i);

    wdtFeed();
    String groupMessage = message;
    if (flag == CHAT_F_NOTIFY) {
      groupMessage = powerOnMessageForRecipient(groupMessage, (entry.flags & CHAT_F_VOLT_IN_LIGHT) != 0);
      groupMessage = powerScheduleMessageForRecipient(groupMessage, (entry.pad[0] & 0x01) != 0);
    }

    if (!bot.sendMessage(String(entry.id), groupMessage, "HTML")) {
      left |= (uint16_t)(1U << i);
      grpNoteFail(strlen(entry.title) > 0 ? String(entry.title) : String(entry.id));
    }
  }

  if (sends > 0) tgSendAttemptedThisPass = true;
  return left;
}

void chatSendWelcome(int i) {
  if (i >= 0 && chats.e[i].used && chats.e[i].status > 0 && chats.e[i].canPost && chatApproved(chats.e[i])) {
    bot.sendMessage(String(chats.e[i].id), TEXT_BOT_ADDED, "HTML");
  }
}

void handleMyChatMember(JsonObject u) {
  String id = jsonStr(u["chat"]["id"]);
  uint8_t type = chatTypeCode(jsonStr(u["chat"]["type"]));
  if (type == 0) {
    return;
  }
  String title = jsonStr(u["chat"]["title"]);
  JsonObject m = u["new_chat_member"];
  String st = jsonStr(m["status"]);
  uint8_t status = chatStatusCode(st, m["is_member"].as<bool>());
  uint8_t canPost = 1;
  if (type == 3) {
    canPost = (st == "creator" || m["can_post_messages"].as<bool>()) ? 1 : 0;
  }
  if (status == 0) {
    int i = chatFind(id);
    if (i >= 0) {
      chats.e[i].status = 0;
      chatsSave();
      Serial.println("Бота видалено з чату: " + id);
    }
    return;
  }
  int before = chatFind(id);
  bool wasActive = (before >= 0 && chats.e[before].status > 0);
  const String byId = jsonStr(u["from"]["id"]);
  const bool byOwner = byId.length() > 0 && byId == chatId;
  int i = chatRegister(id, title, type, status, canPost, byOwner, jsonStr(u["from"]["first_name"]));
  if (!wasActive) {
    chatSendWelcome(i);
  }
}

int pollTelegramUpdates() {
  const bool syncPoll = !tgPollSynced || tgStaleSkip;
  const String cmd = syncPoll ? String("getUpdates?offset=-1&limit=1")
                              : "getUpdates?offset=" + String(bot.last_message_received + 1) + "&limit=1";
  String resp = bot.sendGetToTelegram(bot.buildCommand(cmd));
  if (resp.length() == 0) {

    telegramFailCount++;
    if (telegramFailCount >= 3 && telegramOnline) {
      telegramOnline = false;
      Serial.println("Telegram недоступний - рідші спроби");
    }
    if (telegramFailCount >= 3) {
      tgStaleSkip = true;
    }
    tlsNoteFailure();
    if (telegramFailCount == 3) {
      logEvent(LC_TG, LS_WARN, "Telegram недоступний");
    }
    return 0;
  }
  if (!telegramOnline) {
    Serial.println("Telegram знову доступний");
    mqLastTry = 0;
  }
  if (telegramFailCount >= 3) {
    logEvent(LC_TG, LS_INFO, "Telegram знову доступний");
  }
  telegramFailCount = 0;
  telegramOnline = true;
  tgLastOkMs = millis();
  tlsNoteSuccess();
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    client.stop();
    int p = resp.indexOf("\"update_id\":");
    long skipId = p >= 0 ? strtol(resp.c_str() + p + 12, nullptr, 10) : 0;
    if (skipId > bot.last_message_received) {
      bot.last_message_received = skipId;
      tgPollSynced = true;
      logEvent(LC_TG, LS_WARN, "Оновлення Telegram #" + String(skipId) + " не вдалося прочитати (задовге) - пропущено");
    }
    return 0;
  }
  JsonArray res = doc["result"];
  const bool bootSync = !tgPollSynced;
  tgPollSynced = true;
  if (res.isNull() || res.size() == 0) {
    tgStaleSkip = false;
    return 0;
  }
  JsonObject u = res[0];
  long uid = u["update_id"].as<long>();
  if (uid == bot.last_message_received) {
    tgStaleSkip = false;
    return 0;
  }
  bot.last_message_received = uid;

  bool staleByOutage = tgStaleSkip || bootSync;
  tgStaleSkip = false;

  telegramMessage& m = bot.messages[0];
  m.update_id = uid;
  m.text = "";
  m.from_id = "";
  m.from_name = "";
  m.chat_id = "";
  m.chat_title = "";
  m.query_id = "";
  m.type = "";
  m.message_id = 0;
  tgDocId = "";
  tgDocName = "";
  tgDocSize = 0;

  if (u["my_chat_member"].is<JsonObject>()) {
    handleMyChatMember(u["my_chat_member"]);
    return 0;
  }

  if (u["callback_query"].is<JsonObject>()) {
    JsonObject q = u["callback_query"];
    m.type = "callback_query";
    m.from_id = jsonStr(q["from"]["id"]);
    m.from_name = jsonStr(q["from"]["first_name"]);
    m.text = jsonStr(q["data"]);
    m.chat_id = jsonStr(q["message"]["chat"]["id"]);
    m.query_id = jsonStr(q["id"]);
    m.message_id = q["message"]["message_id"].as<int>();
    if (staleByOutage) {
      Serial.println("Пропущено натискання кнопки, що надійшло без зв'язку");
      return 0;
    }
    return 1;
  }

  JsonObject msg;
  if (u["message"].is<JsonObject>()) {
    msg = u["message"];
    m.type = "message";
  } else if (u["channel_post"].is<JsonObject>()) {
    msg = u["channel_post"];
    m.type = "channel_post";
  } else {
    return 0;
  }

  m.from_id = jsonStr(msg["from"]["id"]);
  m.from_name = jsonStr(msg["from"]["first_name"]);
  m.chat_id = jsonStr(msg["chat"]["id"]);
  m.chat_title = jsonStr(msg["chat"]["title"]);
  m.message_id = msg["message_id"].as<int>();
  m.text = jsonStr(msg["text"]);
  if (m.type == "message" && msg["document"].is<JsonObject>()) {
    tgDocId = jsonStr(msg["document"]["file_id"]);
    tgDocName = jsonStr(msg["document"]["file_name"]);
    tgDocSize = msg["document"]["file_size"] | 0L;
  }

  uint8_t type = chatTypeCode(jsonStr(msg["chat"]["type"]));
  if (type != 0) {
    bool botLeft = botIdStr.length() > 0 && jsonStr(msg["left_chat_member"]["id"]) == botIdStr;
    if (botLeft) {
      int i = chatFind(m.chat_id);
      if (i >= 0) {
        chats.e[i].status = 0;
        chatsSave();
      }
      return 0;
    }
    int i = chatFind(m.chat_id);
    if (i < 0) {
      i = chatRegister(m.chat_id, m.chat_title, type, (type == 3) ? 2 : 1, 1,
                       m.from_id.length() > 0 && m.from_id == chatId, m.from_name);
      chatRefreshAccess(i);
      chatSendWelcome(i);
    } else if (chats.e[i].status == 0) {
      chatRefreshAccess(i);
      chatSendWelcome(i);
    } else if (m.chat_title.length() > 0 && m.chat_title != String(chats.e[i].title)) {
      chatCopyTitle(chats.e[i].title, sizeof(chats.e[i].title), m.chat_title);
      chatsSave();
    }
  }

  if (m.text.length() == 0 && tgDocId.length() == 0) {
    return 0;
  }

  uint32_t mdate = msg["edit_date"].is<uint32_t>() ? msg["edit_date"].as<uint32_t>() : msg["date"].as<uint32_t>();
  uint32_t nowEpoch = statsNowEpoch();
  bool tooOld = nowEpoch != 0 && mdate != 0 && nowEpoch > mdate && nowEpoch - mdate > TG_STALE_SEC;
  if (staleByOutage || tooOld) {
    Serial.println("Пропущено застаріле повідомлення: " + (tgDocId.length() > 0 ? "[файл] " + tgDocName : m.text));
    m.text = "";
    tgDocId = "";
    return 0;
  }
  return 1;
}

String lightStatusText(bool powerOn, const String& since, const String& duration, const String& extra) {
  String t = powerOn ? "🟢 <b>Світло є</b>\n" : "🔴 <b>Світла немає</b>\n";
  const bool sinceKnown = since.length() > 0 && since != "невідомо" && since != "--:--";
  if (sinceKnown && duration.length() > 0) {
    t += "🕓 З " + htmlEsc(since) + ", уже " + htmlEsc(duration);
  } else if (sinceKnown) {
    t += "🕓 З " + htmlEsc(since);
  } else if (duration.length() > 0) {
    t += "🕓 Уже " + htmlEsc(duration);
  } else {
    t += powerOn ? "🕓 Тривалість невідома" : "🕓 Тривалість невідома: пристрій запущено без світла";
  }
  return t + extra;
}

static bool scheduleLineAllowed(const String& id) {
  if (id == chatId) return ownerYasnoEventOn;
  const int i = chatFind(id);
  return i >= 0 && (chats.e[i].pad[0] & 0x01) != 0;
}

String buildStatusMessage() {
  String t = "📋 <b>Стан Light Watcher</b>\n\n";
  bool powerNow = checkPowerStatus();
  t += "<b>💡 Електромережа</b>\n";
  t += powerNow ? "🟢 Світло є\n" : "🔴 Світла немає\n";
  uint32_t count = stats.offCount;
  if (powerOutageCount > 0 && count < (uint32_t)powerOutageCount) count = (uint32_t)powerOutageCount;
  t += "🔁 Відключень: " + String(count) + "\n";
  t += "🕒 Поточний час: " + htmlEsc(getFormattedTime()) + "\n\n";

  t += "<b>🕐 Часовий пояс</b>\n";
  if (autoDst) {
    t += "Автоперехід: 🟢 увімкнено (Europe/Kyiv), зараз ";
    t += isSummerTimeNow() ? "UTC+3\n" : "UTC+2\n";
  } else {
    t += "Автоперехід: 🔴 вимкнено\n";
    t += currentTZ == "EEST-3" ? "Встановлено UTC+3\n" : "Встановлено UTC+2\n";
  }

  t += "\n<b>📶 Підключення</b>\n";
  t += String("Wi-Fi: ") + (WiFi.status() == WL_CONNECTED ? "🟢 підключено\n" : "🔴 немає з'єднання\n");
  t += "Відключень Wi-Fi: " + String(wifiDiscBoot) + " за сеанс / " + String(wifiDiscTotal) + " усього\n";
  t += "Підключень Wi-Fi: " + String(wifiConnBoot) + " за сеанс / " + String(wifiConnTotal) + " усього\n";
  t += String("🔋 Економний режим: ") + (ecoActive ? "🟢 активний" : "⚪ неактивний");
  return t;
}

String buildInfoMessage() {
  String t = "⚡ <b>Light Watcher · V2.4.11</b>\n\n";
  t += "Автоматичний моніторинг електромережі.\n\n";
  t += "<b>Можливості</b>\n";
  t += "• Сповіщення про зникнення та появу світла\n";
  t += "• Облік тривалості відключень\n";
  t += "• Робота від резервного живлення\n";
  t += "• Збереження статистики та історії\n";
  t += "• Моніторинг напруги й графіків YASNO\n\n";
  t += "📦 <a href='https://github.com/Stanislav-developer/Light_Watcher'>Проєкт на GitHub</a>";
  return t;
}

String timeSetSummer() {
  autoDst = false;
  preferences.putBool("autoDst", autoDst);
  currentTZ = "EEST-3";
  preferences.putString("tz-rule", currentTZ);
  applyTimezone(currentTZ);
  return "✅ <b>Часовий пояс оновлено</b>\nВстановлено літній час (UTC+3).";
}

String timeSetWinter() {
  autoDst = false;
  preferences.putBool("autoDst", autoDst);
  currentTZ = "EEST-2";
  preferences.putString("tz-rule", currentTZ);
  applyTimezone(currentTZ);
  return "✅ <b>Часовий пояс оновлено</b>\nВстановлено зимовий час (UTC+2).";
}

String timeAutoOn() {
  autoDst = true;
  preferences.putBool("autoDst", autoDst);
  applyTimezone(TZ_EUROPE_KYIV);
  return "✅ <b>Автоматичний перехід часу увімкнено</b>\n\n🌍 Часовий пояс: Europe/Kyiv\n🕒 Поточний час: " + htmlEsc(getFormattedTime());
}

String timeAutoOff() {
  bool summerNow = isSummerTimeNow();
  autoDst = false;
  preferences.putBool("autoDst", autoDst);
  currentTZ = summerNow ? "EEST-3" : "EEST-2";
  preferences.putString("tz-rule", currentTZ);
  applyTimezone(currentTZ);
  return String("⚪ <b>Автоматичний перехід часу вимкнено</b>\n\n🕐 Зафіксовано ") +
         (summerNow ? "літній час (UTC+3)" : "зимовий час (UTC+2)") +
         ". Для ручної зміни використовуйте /set_summer_time або /set_winter_time.";
}

void telegramAckBeforeRestart() {
  if (bot.last_message_received <= 0 || WiFi.status() != WL_CONNECTED || !client.connected()) return;
  wdtFeed();
  bot.sendGetToTelegram(bot.buildCommand("getUpdates?offset=" + String(bot.last_message_received + 1) + "&limit=1&timeout=0"));
  wdtFeed();
}

int telegramCredentialsError() {
  String resp = telegramPostRaw("getChat", "{\"chat_id\":\"" + chatId + "\"}");
  if (resp.indexOf("\"error_code\":401") >= 0) return 401;
  if (resp.indexOf("\"error_code\":404") >= 0) return 404;
  if (resp.indexOf("\"error_code\":400") >= 0) return 400;
  return 0;
}

String startMessageText() {
  time_t now;
  time(&now);
  String boot = statsNowEpoch() != 0 ? formatEpochLocal(now - (time_t)uptimeSec(), "%d.%m.%Y %H:%M:%S") : String("Час не синхронізовано");
  String t = startMsgText;
  t.replace("%BOOT%", boot);
  return t;
}

void retryStartMessage() {
  static unsigned long lastTry = 0;
  static bool tried = false;
  if (!startMsgPending || WiFi.status() != WL_CONNECTED || (tried && millis() - lastTry < 30000UL)) {
    return;
  }
  if (powerChangePending() || tgSendAttemptedThisPass) {
    return;
  }

  if (statsNowEpoch() == 0 && wifiUpSinceMs != 0 && millis() - wifiUpSinceMs < 120000UL) {
    return;
  }
  if (!tgNetAllowed()) {
    return;
  }
  tried = true;
  lastTry = millis();
  menuState = "main";
  tgSendAttemptedThisPass = true;
  if (bot.sendMessageWithReplyKeyboard(chatId, startMessageText(), "HTML", menuKeyboard("main"), true)) {
    tgCleanNoteStart((int32_t)bot.last_sent_message_id);
    startMsgPending = false;
    Serial.println("Стартове повідомлення надіслано (із затримкою)");
  }
}

void telegramDeferredInit() {
  static unsigned long lastTry = 0;
  static bool tried = false;
  if (tgInitDone || WiFi.status() != WL_CONNECTED || (tried && millis() - lastTry < 30000UL)) {
    return;
  }
  if (powerChangePending() || !tgNetAllowed()) {
    return;
  }
  tried = true;
  lastTry = millis();
  if (!telegramLoadBotInfo()) {
    return;
  }

  wdtFeed();
  chatsOnlineInit();
  wdtFeed();
  setupTelegramCommandMenus();
  tgInitDone = true;
  Serial.println("Telegram: початкове налаштування виконано");
}

void handleNewMessages() {
  String chat_id = String(bot.messages[0].chat_id);
  String text = bot.messages[0].text;
  String from_name = bot.messages[0].from_name;

  bool statsOk = false;
  bool eventsOk = false;
  bool voltageOk = false;

  if (bot.messages[0].type == "callback_query" && bot.messages[0].query_id.length() > 0) {
    bot.answerCallbackQuery(bot.messages[0].query_id);
  }

  if (chat_id == chatId) tgCleanNoteSeen((int32_t)bot.messages[0].message_id);

  bool hiddenInput = (chat_id == chatId && (menuInput == "pass1" || menuInput == "pass2"));
  Serial.println("Отримано: " + (hiddenInput ? String("[приховано]") : text) + " від " + from_name + " ID: " + chat_id);

  if (tgDocId.length() > 0) {
    if (chat_id == chatId && bot.messages[0].type == "message") {
      otaHandleDocument(tgDocId, tgDocName, tgDocSize);
    }
    return;
  }

  if (text.length() == 0) {
    return;
  }

  String cmd = normalizeCommand(text);
  bool isLightCmd = (cmd == "світло є" || cmd == "світло є чи нема" || cmd == "світло є чи ні" ||
                     cmd == "є світло" || cmd == "чи є світло");
  bool isVoltageCmd = (cmd == "напруга" || cmd == "яка напруга");
  bool isStatsCmd = (text == "/stats" || text.startsWith("/stats@"));

  bool isYasnoCmd = (cmd == "графік" || cmd == "який графік" || cmd == "графік сьогодні" ||
                     cmd == "графік на завтра" || cmd == "графік завтра" ||
                     cmd == "який сьогодні графік" || text == "/grafik" || text.startsWith("/grafik@") ||
                     cmd == "аварія" || cmd == "аварійний режим" || cmd == "екстрено" ||
                     cmd == "екстрений режим" || cmd == "екстрені відключення" ||
                     cmd == "чи є екстрені відключення" || cmd == "чи діють екстрені відключення" ||
                     cmd == "аварійні відключення" || text == "/avaria" || text.startsWith("/avaria@") ||
                     text == "/emergency" || text.startsWith("/emergency@"));
  bool isPublicCmd = isLightCmd || isVoltageCmd || isStatsCmd || isYasnoCmd;

  if(chat_id == chatId && isStatsCmd){
    bot.sendMessage(chat_id, buildStatsMessage(), "HTML");
  }

  bool yasnoQueryAllowed = (chat_id == chatId) ? ownerYasnoQueryOn : chatAllows(chat_id, CHAT_F_YASNO_QUERY);
  if (isYasnoCmd && yasnoQueryAllowed) {
    bot.sendMessage(chat_id, yasnoBuildQueryMessage(false), "HTML");
  }

  if(chatAllows(chat_id, CHAT_F_LIGHT) && isLightCmd){
    const bool powerNow = checkPowerStatus();
    String extra = "";
    if (powerNow && chatAllows(chat_id, CHAT_F_VOLT_IN_LIGHT)) {
      extra += "\n⚡ Напруга: " + voltageText(getShellyVoltageFresh());
    }
    if (scheduleLineAllowed(chat_id)) {
      const String scheduleLine = yasnoPowerEventText(powerNow);
      if (scheduleLine.length() > 0) extra += "\n" + scheduleLine;
    }
    bot.sendMessage(chat_id, powerNow
      ? lightStatusText(true, powerOnShortTime, currentOnDurationText(), extra)
      : lightStatusText(false, powerOffShortTime, currentOffDurationText(), extra), "HTML");
  }

  if(chatAllows(chat_id, CHAT_F_VOLT) && isVoltageCmd){
    String volt_message;
    if (!checkPowerStatus()) {
      volt_message = "⚡ <b>Напруга недоступна</b>\n🔴 Світла немає, вимірювати нічого.";
    }
    else {
      float v = getShellyVoltageFresh();
      if (!isnan(v)) {
        volt_message = "⚡ <b>Напруга: " + voltageText(v) + "</b>\n🟢 Світло є";
      }
      else {
        volt_message = "⚠️ <b>Напруга недоступна</b>\nShelly не відповідає, спробуйте пізніше.";
      }
    }
    bot.sendMessage(chat_id, volt_message, "HTML");
  }

  if (chat_id == chatId && !isPublicCmd) {
    if (text == "/help") {
      menuInput = "";
      menuPendingLogin = "";
      menuPendingPass = "";
      menuGoMain();
    }

    else if (text == "/clear" || text.startsWith("/clear@") || cmd == "очистити чат") {
      menuClearChat();
    }

    else if (bot.messages[0].type != "callback_query" && menuInput.length() > 0) {
      menuCurrentUserMsg = bot.messages[0].message_id;
      handleMenuInput(text);
      menuCurrentUserMsg = 0;
    }

    else if (bot.messages[0].type != "callback_query" && handleMenuButton(text)) {
    }

    else if (bot.messages[0].type == "callback_query" && text.startsWith("x:")) {
      handleMenuCallback(text, bot.messages[0].message_id);
    }

    else if (text == "/info") {
      bot.sendMessage(chat_id, buildInfoMessage(), "HTML");
    }

    else if (text == "/status") {
      bot.sendMessage(chat_id, buildStatusMessage(), "HTML");
    }

    else if (text == "/set_summer_time") {
      bot.sendMessage(chat_id, timeSetSummer(), "HTML");
    }

    else if (text == "/set_winter_time") {
      bot.sendMessage(chat_id, timeSetWinter(), "HTML");
    }

    else if (text == "/auto_time_on") {
      bot.sendMessage(chat_id, timeAutoOn(), "HTML");
    }

    else if (text == "/auto_time_off") {
      bot.sendMessage(chat_id, timeAutoOff(), "HTML");
    }

    else if (text == "/update") {
      otaStart();
    }

    else if (text == "/restart") {
      bot.sendMessage(chat_id, "🔄 <b>Перезапуск пристрою</b>\nLight Watcher зараз перезавантажиться.", "HTML");
      prepareForRestart("команда /restart");
      delay(1000);
      ESP.restart();
    }

    else if (text == "/reset_stats") {
      startResetStatsConfirm(chat_id, bot.messages[0].from_id);
    }

    else if (bot.messages[0].type == "callback_query" && text.startsWith("rst_")) {
      bool valid = resetPending
                   && chat_id == resetPendingChat
                   && bot.messages[0].from_id == resetPendingUser
                   && millis() - resetPendingMs <= 120000UL
                   && (text == "rst_yes_" + resetPendingCode || text == "rst_no_" + resetPendingCode);
      String result;
      if (!valid) {
        result = "⌛ Запит на очищення застарів. Надішліть /reset_stats ще раз.";
      }
      else if (text.startsWith("rst_yes_")) {
        const bool powerNow = checkPowerStatus();
        statsOk = statsResetAll(powerNow);
        eventsOk = evReset(powerNow);
        voltageOk = voltResetAll();
        if (statsOk && eventsOk && voltageOk) {
          result = "✅ <b>Статистику очищено</b>\n\n📊 Лічильники, історію відключень і історію напруги скинуто. Фактичний стан мережі збережено; тривалість поточного стану рахується від моменту очищення.";
        } else {
          result = "⚠️ <b>Очищення не підтверджено</b>\n\nНе всі сховища пройшли перевірку:";
          if (!statsOk) result += "\n• Статистика відключень";
          if (!eventsOk) result += "\n• Журнал подій";
          if (!voltageOk) result += "\n• Історія напруги та добові min/max";
          nvs_stats_t resetNs;
          if (nvs_get_stats(NULL, &resetNs) == ESP_OK) {
            result += "\n\nВільні записи NVS: " + String((unsigned)(resetNs.total_entries - resetNs.used_entries)) + "/" + String((unsigned)resetNs.total_entries) + ".";
          }
          result += "\nПеревірте журнал Serial. Не вважайте очищення повним.";
          Serial.printf("[RESET] result: stats=%s events=%s voltage=%s\n", statsOk ? "OK" : "FAIL", eventsOk ? "OK" : "FAIL", voltageOk ? "OK" : "FAIL");
        }
      }
      else {
        result = "❌ Очищення скасовано. Статистику не змінено.";
      }
      if (valid) {
        resetPending = false;
      }

      bot.sendMessageWithInlineKeyboard(chat_id, result, "HTML", "[]", bot.messages[0].message_id);
      if (valid && text.startsWith("rst_yes_") && statsOk && eventsOk && voltageOk) {
        Serial.println("[RESET] all stores verified; restarting to reload the cleared state");
        delay(1200);
        prepareForRestart("після успішного очищення статистики");
        delay(350);
        ESP.restart();
      }
    }

    else {
      bot.sendMessage(chat_id, "❔ <b>Команду не розпізнано</b>\n/help - меню, /clear - очистити чат.", "HTML");
    }
  }
}
