#include "lw.h"

static String menuRestartNonce = "";
static bool menuLoginThenPass = false;
static int menuChatIdx = -1;
static TgCleanState tgc;
static bool tgcDirty = false;
static unsigned long tgcSaveMs = 0;

String admStatusText() {
  String t = "🔑 <b>Пароль адміністратора</b>\n";
  t += "Логін: " + (admUser.length() > 0 ? "<code>" + htmlEsc(admUser) + "</code>" : String("не створено")) + "\n";
  t += String("Пароль: ") + (admHasPass ? "🟢 створено (зберігається лише у вигляді хешу)" : "🔴 не створено - адмінпанель закрита") + "\n";
  t += "Адмінпанель (лише у вашій локальній мережі): http://" + WiFi.localIP().toString() + "/";
  return t;
}

String webMenuText() {
  String t = "🌐 <b>Вебдоступ</b>\n";
  t += String("🔌 Вебсервер: ") + (webStarted ? "🟢 працює на пристрої, порт " + String(WEB_PUBLIC_PORT) : String("🔴 не запущено")) + "\n";
  t += "🌐 Використання домену: " + onOff(webDomOn) + "\n";
  t += "🌍 Доменне ім'я: " + (webDomain.length() > 0 ? "<code>" + htmlEsc(webDomain) + "</code>" : String("не задано")) + "\n";
  t += "🔒 HTTPS: " + webHttpsStatusText() + "\n";
  t += "☁️ Cloudflare: " + webCfStatusText() + "\n";
  t += "🛡 Лише через Cloudflare: " + onOff(webCfOnly) + (webCfOnly && !webDomOn ? " (не діє: домен вимкнено)" : String("")) + "\n";
  if (webCheckAt != 0 && webDomOn) {
    t += "   (перевірено " + formatEpochLocal((time_t)webCheckAt, "%d.%m %H:%M") + ")\n";
  }
  t += "🌐 Поточна публічна IP: ";
  if (webIpManual) {
    t += "<code>" + htmlEsc(webIpVal) + "</code> (вручну)";
  } else {
    t += pubIpAuto.length() > 0 ? "<code>" + pubIpAuto + "</code> (автоматично" + (pubIpAt ? ", " + formatEpochLocal((time_t)pubIpAt, "%d.%m %H:%M") : String("")) + ")" : String("ще не визначено");
  }
  t += "\n";
  String link = webPublicLink();
  t += "🔗 Посилання: " + (link.length() > 0 ? link : String("не сформовано - задайте домен або IP"));
  if (!webDomOn && link.length() > 0) {
    t += "\n⚠️ Це звичайне HTTP-посилання без шифрування: для IP-адреси сертифіката немає. HTTPS - лише через домен у Cloudflare (📖 Інструкція).";
  }
  return t;
}

static String tgDeleteIds(const int* ids, int count) {
  String json = "{\"chat_id\":\"" + chatId + "\",\"message_ids\":[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += String(ids[i]);
  }
  json += "]}";
  wdtFeed();
  String resp = telegramPostRaw("deleteMessages", json);
  wdtFeed();
  return resp;
}

void menuDeleteIds(int* ids, int count) {
  if (count <= 0) {
    return;
  }
  tgDeleteIds(ids, count);
}

void tgKeepPush(TgCleanState& s, int32_t id) {
  if (id <= 0 || (s.from > 0 && id < s.from)) return;
  for (int i = 0; i < s.n; i++) {
    if (s.keep[i] == id) return;
  }
  if (s.n >= TG_KEEP_MAX) {
    const int32_t dropped = s.keep[0];
    for (int i = 1; i < s.n; i++) s.keep[i - 1] = s.keep[i];
    s.n--;
    if (s.from <= dropped) s.from = dropped + 1;
  }
  int pos = s.n;
  while (pos > 0 && s.keep[pos - 1] > id) {
    s.keep[pos] = s.keep[pos - 1];
    pos--;
  }
  s.keep[pos] = id;
  s.n++;
}

void tgKeepPrune(TgCleanState& s) {
  int k = 0;
  for (int i = 0; i < s.n; i++) {
    if (s.keep[i] >= s.from) s.keep[k++] = s.keep[i];
  }
  s.n = (uint8_t)k;
}

int tgCleanCollect(const TgCleanState& s, int32_t from, int32_t to, int* out, int maxOut, int32_t* next) {
  int count = 0;
  int32_t id = from;
  for (; id <= to && count < maxOut; id++) {
    if (id == s.startId) continue;
    bool keep = false;
    for (int i = 0; i < s.n && !keep; i++) keep = (s.keep[i] == id);
    if (!keep) out[count++] = (int)id;
  }
  *next = id;
  return count;
}

static void tgCleanSave() {
  tgc.magic = TG_CLEAN_MAGIC;
  tgc.crc = statsCrc32((const uint8_t*)&tgc, offsetof(TgCleanState, crc));
  if (preferences.putBytes("tgClean", &tgc, sizeof(tgc)) == sizeof(tgc)) tgcDirty = false;
  tgcSaveMs = millis() ? millis() : 1;
}

void tgCleanBegin() {
  TgCleanState t;
  memset(&tgc, 0, sizeof(tgc));
  tgc.magic = TG_CLEAN_MAGIC;
  if (preferences.getBytesLength("tgClean") == sizeof(t) &&
      preferences.getBytes("tgClean", &t, sizeof(t)) == sizeof(t) &&
      t.magic == TG_CLEAN_MAGIC && t.n <= TG_KEEP_MAX &&
      t.crc == statsCrc32((const uint8_t*)&t, offsetof(TgCleanState, crc))) {
    tgc = t;
  }
}

void tgCleanTick() {
  if (tgcDirty && (tgcSaveMs == 0 || millis() - tgcSaveMs >= TG_CLEAN_SAVE_MS)) tgCleanSave();
}

void tgCleanFlush() {
  if (tgcDirty) tgCleanSave();
}

void tgKeepAdd(int32_t id) {
  if (id <= 0) return;
  tgKeepPush(tgc, id);
  tgcDirty = true;
}

void tgCleanNoteStart(int32_t id) {
  if (id <= 0) return;
  tgc.startId = id;
  if (tgc.from <= 0) tgc.from = id;
  tgcDirty = true;
}

void tgCleanNoteSeen(int32_t id) {
  if (id > 0 && tgc.from <= 0) {
    tgc.from = id;
    tgcDirty = true;
  }
}

bool tgCleanUpTo(int32_t upTo) {
  if (upTo <= 0) return false;
  int32_t from = tgc.from > 0 ? tgc.from : upTo;
  if (upTo - from > TG_CLEAN_SPAN) from = upTo - TG_CLEAN_SPAN;
  int ids[TG_CLEAN_BATCH];
  bool ok = true;
  while (from < upTo) {
    if (powerChangePending()) {
      ok = false;
      break;
    }
    int32_t next = from;
    const int n = tgCleanCollect(tgc, from, upTo - 1, ids, TG_CLEAN_BATCH, &next);
    if (n > 0) {
      const String resp = tgDeleteIds(ids, n);
      if (resp.length() == 0) {
        ok = false;
        break;
      }
      if (resp.indexOf("\"ok\":true") < 0) ok = false;
    }
    from = next;
  }
  if (from > tgc.from) tgc.from = from;
  tgKeepPrune(tgc);
  tgcDirty = true;
  return ok;
}

bool tgSendOwnerNotice(const String& text) {
  if (!bot.sendMessage(chatId, text, "HTML")) return false;
  tgKeepAdd((int32_t)bot.last_sent_message_id);
  return true;
}

void menuSend(const String& text, const String& parseMode) {
  bot.sendMessage(chatId, text, parseMode);
}

bool menuGoMain() {
  menuCurrentUserMsg = 0;
  menuState = "main";
  if (!bot.sendMessageWithReplyKeyboard(chatId, "🏠 <b>Головне меню</b>", "HTML", menuKeyboard("main"), true)) {
    return false;
  }
  return tgCleanUpTo((int32_t)bot.last_sent_message_id);
}

void menuClearChat() {
  menuInput = "";
  menuInputBack = "";
  menuPendingLogin = "";
  menuPendingPass = "";
  if (!menuGoMain()) {
    menuSend("⚠️ Частину повідомлень не видалено: немає зв'язку з Telegram або щойно змінився стан світла. "
             "Натисніть «🧹 Очистити чат» ще раз.", "");
  }
}

String onOff(bool v) {
  return v ? "🟢 ON" : "🔴 OFF";
}

String kbRow(const String& a, const String& b) {
  String r = "[\"" + jsonEsc(a) + "\"";
  if (b.length() > 0) {
    r += ",\"" + jsonEsc(b) + "\"";
  }
  return r + "]";
}

String confirmKeyboard(const String& yesText, const String& yesData, const String& noText, const String& noData) {
  return "[[{\"text\":\"" + yesText + "\",\"callback_data\":\"" + yesData + "\"},"
         "{\"text\":\"" + noText + "\",\"callback_data\":\"" + noData + "\"}]]";
}

int chatByListNumber(int n) {
  int num = 0;
  for (int i = 0; i < MAX_CHATS; i++) {
    if (chats.e[i].used && chats.e[i].status > 0) {
      num++;
      if (num == n) {
        return i;
      }
    }
  }
  return -1;
}

String menuKeyboard(const String& state) {
  if (state == "sys") {
    return "[" + kbRow("📟 Стан системи", "ℹ️ Інформація") + "," + kbRow("🔢 Версія прошивки", "🌐 Вебналаштування") + ","
           + kbRow("🩺 Діагностика", "📜 Журнал") + ","
           + kbRow("🔁 Перезапуск", "⬆️ Оновити прошивку") + "," + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "st") {
    return "[" + kbRow("📊 Загальна статистика", "") + "," + kbRow("🗑 Очистити статистику", "") + "," + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "set") {
    return "[" + kbRow("🔔 Сповіщення", "⚡ Контроль напруги") + ","
           + kbRow("📅 Графік YASNO", "🕐 Час і регіон") + ","
           + kbRow("🌐 Мережа та вебдоступ", "🔐 Безпека") + ","
           + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "notify") {
    return "[" + kbRow("📱 Особисті сповіщення", "") + ","
           + kbRow("👥 Групи й канали: " + String(chatActiveCount()), "") + ","
           + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "net") {
    return "[" + kbRow("✈️ Telegram: інформація", "🌐 Вебдоступ") + ","
           + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "time") {
    return "[" + kbRow("🕐 Автоперехід часу: " + onOff(autoDst), "") + "," + kbRow("☀️ Літній вручну", "❄️ Зимовий вручну") + ","
           + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "yasno") {
    return "[" + kbRow("📍 Налаштувати адресу", "") + "," + kbRow("🔄 Перевірити адресу", "") + ","
           + kbRow("🗑 Очистити адресу", "") + "," + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "route") {
    return "[" + kbRow("🔔 Мені: світло зникло/з'явилося: " + onOff(ownerNotifyOn), "") + ","
            + kbRow("⚡ Мені: аварії напруги: " + onOff(ownerVoltAlertOn), "") + ","
           + kbRow("⚡ Мені: напруга після появи світла: " + onOff(ownerVoltInLightOn), "") + ","
           + kbRow("📅 Мені: розклад у повідомленні про світло: " + onOff(ownerYasnoEventOn), "") + ","
           + kbRow("📊 Мені: команда «Графік»: " + onOff(ownerYasnoQueryOn), "") + ","
           + kbRow("📅 Мені: автопублікація графіка: " + onOff(ownerYasnoPublishOn), "") + ","
           + kbRow("🚨 Мені: сповіщення про екстрений режим: " + onOff(ownerYasnoEmergencyOn), "") + ","
           + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "grp") {
    String kb = "[";
    int num = 0;
    for (int i = 0; i < MAX_CHATS; i++) {
      ChatEntry& c = chats.e[i];
      if (!c.used || c.status == 0) {
        continue;
      }
      num++;
      bool limited = (c.type == 3 && !c.canPost) || (c.type != 3 && c.status == 1 && !botCanReadAllGroup);
      kb += kbRow(String(num) + ". " + (!chatApproved(c) ? "⏳ " : limited ? "🟡 " : "🟢 ") + chatShortTitle(c.title, 22), "") + ",";
    }
    return kb + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "chat" && menuChatIdx >= 0) {
    ChatEntry& c = chats.e[menuChatIdx];

    String offMain = "🔴 OFF";
    if (!chatApproved(c)) {
      return "[" + kbRow("✅ Дозволити цей чат", "") + "," + kbRow("🚪 Вийти з цього чату", "") + ","
             + kbRow("◀️ Назад", "") + "]";
    }
    return "[" + kbRow("🔔 Зникнення / поява світла: " + onOff(c.flags & CHAT_F_NOTIFY), "") + ","
           + kbRow("💡 Команда «Світло є?»: " + onOff(c.flags & CHAT_F_LIGHT), "") + ","
           + kbRow("⚡ Команда «Яка напруга?»: " + onOff(c.flags & CHAT_F_VOLT), "") + ","
            + kbRow("🚨 Аварії напруги: " + onOff(c.flags & CHAT_F_VALERT), "") + ","
           + kbRow("⚡ Напруга у відповіді про світло: " + onOff(c.flags & CHAT_F_VOLT_IN_LIGHT), "") + ","
           + kbRow("📅 Розклад у повідомленні про світло: " + onOff(c.pad[0] & 0x01), "") + ","
           + kbRow("📊 Команда «Графік»: " + onOff(c.flags & CHAT_F_YASNO_QUERY), "") + ","
           + kbRow("📅 Автопублікація графіка: " + onOff(c.flags & CHAT_F_YASNO_PUBLISH), "") + ","
           + kbRow("🚨 Екстрений режим YASNO: " + onOff(c.flags & CHAT_F_YASNO_EMERGENCY), "") + ","
           + kbRow("🚪 Вийти з цього чату", "") + ","
           + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "dev") {
    return "[" + kbRow("⚡ Shelly", "") + "," + kbRow("◀️ Назад", "") + "]";
  }
  String kb17 = menuKeyboardV17(state);
  if (kb17.length() > 0) {
    return kb17;
  }

  return "[" + kbRow("💡 Система", "📊 Статистика") + "," + kbRow("⚙️ Налаштування", "🔌 Пристрої") + ","
         + kbRow("🧹 Очистити чат", "") + "]";
}

void menuOpen(const String& state, const String& prompt) {
  menuState = state;
  menuCurrentUserMsg = 0;
  bot.sendMessageWithReplyKeyboard(chatId, prompt, "HTML", menuKeyboard(state), true);
}

void menuOpenClean(const String& state, const String& prompt) {
  menuState = state;
  menuCurrentUserMsg = 0;
  if (bot.sendMessageWithReplyKeyboard(chatId, prompt, "HTML", menuKeyboard(state), true)) {
    tgCleanUpTo((int32_t)bot.last_sent_message_id);
  }
}

String menuTimeText() {
  String t = "🕐 <b>Час</b>\nПоточний час: " + getFormattedTime() + "\n";
  if (autoDst) {
    bool summerNow = isSummerTimeNow();
    t += String("Режим: автоперехід (Europe/Kyiv), зараз ") + (summerNow ? "літній час (UTC+3)" : "зимовий час (UTC+2)");
  } else {
    t += String("Режим: вручну, ") + (currentTZ == "EEST-3" ? "літній час (UTC+3)" : "зимовий час (UTC+2)");
  }
  return t;
}

String menuChatText(int i) {
  ChatEntry& c = chats.e[i];
  String t = "👥 <b>" + htmlEsc(chatShortTitle(c.title, 40)) + "</b>\n";
  if (!chatApproved(c)) {
    t += "⏳ <b>Очікує вашого підтвердження</b> - сповіщення й відповіді сюди не надсилаються.\n";
  }
  t += "Тип: " + chatTypeName(c.type) + "\n";
  t += "Chat ID: <code>" + htmlEsc(String(c.id)) + "</code>\n";
  t += String("Доступ бота: ") + (c.status == 2 ? "адміністратор" : "учасник");
  if (c.type == 3) {
    t += c.canPost ? "\nКоманди в каналі можуть писати лише адміністратори каналу." : "\n⚠️ Бот не має права публікувати в каналі — відповіді та сповіщення не працюватимуть.";
  } else if (c.status == 1 && !botCanReadAllGroup) {
    t += "\n⚠️ Бот не бачить звичайних повідомлень: зробіть його адміністратором групи або вимкніть Group Privacy у @BotFather.";
  }
  t += "\n📅 Автопублікація YASNO: " + onOff(c.flags & CHAT_F_YASNO_PUBLISH);
  t += "\n🚨 Екстрений режим YASNO: " + onOff(c.flags & CHAT_F_YASNO_EMERGENCY);
  t += "\n📅 Розклад у повідомленні про світло: " + onOff(c.pad[0] & 0x01);
  t += "\n📊 Команда «Графік»: " + onOff(c.flags & CHAT_F_YASNO_QUERY);
  t += "\nОсновні налаштування - у ⚙️ Налаштування; тут функцію можна вимкнути чи ввімкнути лише для цього чату.";
  return t;
}

void startResetStatsConfirm(const String& chat, const String& fromId) {
  resetPending = true;
  resetPendingChat = chat;
  resetPendingUser = fromId;
  resetPendingCode = String((uint32_t)esp_random(), HEX);
  resetPendingMs = millis();
  String ask = "⚠️ Ви дійсно хочете очистити статистику Light Watcher?\n\n";
  ask += "Буде видалено:\n";
  ask += "- Історію відключень.\n";
  ask += "- Лічильники.\n";
  ask += "- Накопичену тривалість.\n\n";
  ask += "Налаштування пристрою залишаться без змін.";
  String keyboard = confirmKeyboard("✅ Так, очистити", "rst_yes_" + resetPendingCode, "❌ Скасувати", "rst_no_" + resetPendingCode);
  bot.sendMessageWithInlineKeyboard(chat, ask, "", keyboard);
}

bool handleMenuButton(const String& text) {

  if (text == "💡 Система") { menuOpen("sys", "💡 <b>Система</b>\nОберіть дію"); return true; }
  if (text == "📊 Статистика") { menuOpen("st", "📊 <b>Статистика</b>\nОберіть дію"); return true; }
  if (text == "⚙️ Налаштування") { menuOpen("set", "⚙️ <b>Налаштування</b>\nОберіть категорію"); return true; }
  if (text == "🔌 Пристрої") { menuOpen("dev", "🔌 <b>Пристрої</b>\nОберіть пристрій"); return true; }
  if (text == "🧹 Очистити чат") { menuClearChat(); return true; }
  if (text == "◀️ Назад") {
    if (menuState == "time" || menuState == "notify" || menuState == "route" || menuState == "yasno" || menuState == "grp" || menuState == "net") menuOpenClean("set", "⚙️ <b>Налаштування</b>\nОберіть категорію");
    else if (menuState == "chat") menuOpenClean("grp", "👥 <b>Групи й канали: " + String(chatActiveCount()) + "</b>");
    else if (menuState == "sec" || menuState == "web" || menuState == "volt") menuOpenClean("set", "⚙️ <b>Налаштування</b>\nОберіть категорію");
    else if (menuState == "pwd") menuOpenClean("sec", "🔐 <b>Безпека</b>\nДоступ до адмінпанелі (порт 80, лише локальна мережа).");
    else if (menuState == "wgrp") webMenuShowClean("");
    else menuGoMain();
    return true;
  }

  if (text == "📟 Стан системи") { menuSend(buildStatusMessage(), "HTML"); return true; }
  if (text == "🩺 Діагностика") { menuSend(buildDiagMessage(), "HTML"); return true; }
  if (text == "📜 Журнал") { menuSend(logIssuesText(15), "HTML"); return true; }
  if (text == "ℹ️ Інформація") { menuSend(buildInfoMessage(), "HTML"); return true; }
  if (text == "🔢 Версія прошивки") { menuSend("🔢 Версія прошивки: V2.4.11", ""); return true; }
  if (text == "🌐 Вебналаштування") {
    menuSend("🌐 <b>Вебналаштування</b>\n"
                            "Відкривається в режимі налаштування: утримуйте кнопку BOOT під час увімкнення пристрою, "
                            "підключіться до Wi-Fi «Light-Watcher-Setup» і відкрийте 192.168.4.1.\n"
                            "Там: Wi-Fi, Telegram (токен, Chat ID, Group ID), Shelly IP.", "HTML");
    return true;
  }
  if (text == "⬆️ Оновити прошивку") { otaStart(); return true; }
  if (text == "🔁 Перезапуск") {
    char nonce[9];
    snprintf(nonce, sizeof(nonce), "%08lx", (unsigned long)esp_random());
    menuRestartNonce = nonce;
    menuRestartMs = millis();
    bot.sendMessageWithInlineKeyboard(chatId, "🔁 Перезапустити пристрій?", "",
      confirmKeyboard("✅ Так, перезапустити", "x:rstY:" + menuRestartNonce, "❌ Скасувати", "x:rstN"));
    return true;
  }

  if (text == "📊 Загальна статистика") { menuSend(buildStatsMessage(), "HTML"); return true; }
  if (text == "🗑 Очистити статистику") { startResetStatsConfirm(chatId, bot.messages[0].from_id); return true; }

  if (text == "🔔 Сповіщення") {
    String t = "🔔 <b>Сповіщення для груп і каналів</b>\n";
    t += "Це загальні дозволи. Для окремого чату налаштуйте події в «Групи й канали».\n";
    t += "Особисті сповіщення власника налаштовуються окремо.";
    menuOpen("notify", t);
    return true;
  }
  if (text == "⚡ Контроль напруги") {
    menuOpen("volt", voltMenuText());
    return true;
  }
  if (text == "📅 Графік YASNO") {
    menuOpen("yasno", yasnoMenuText());
    return true;
  }
  if (menuState == "yasno") {
    if (text == "📍 Налаштувати адресу") {
      yasnoPendingCity = "";
      yasnoPendingStreet = "";
      yasnoPendingStreetId = -1;
      menuInputStart("yasno_city", "yasno", "🏙 Введіть місто. Тестова збірка наразі підтримує Київ (YASNO / ДТЕК Київські електромережі).\nНаприклад: Київ");
      return true;
    }
    if (text == "🔄 Перевірити адресу") {
      if (yasnoStreet.length() == 0 || yasnoHouse.length() == 0) {
        menuOpen("yasno", "Спочатку налаштуйте адресу.\n\n" + yasnoMenuText());
        return true;
      }
      String error;
      if (!yasnoStartTask(3, "", error)) {
        menuOpen("yasno", "❌ " + htmlEsc(error) + "\n\n" + yasnoMenuText());
        return true;
      }
      menuInputStart("yasno_wait", "yasno", "⏳ Перевіряю збережену адресу в YASNO у фоновому режимі. Це не зупиняє основний моніторинг світла.");
      return true;
    }
    if (text == "🗑 Очистити адресу") {
      preferences.remove("ysCity");
      preferences.remove("ysStreet");
      preferences.remove("ysHouse");
      preferences.remove("ysGroup");
      yasnoClearScheduleCache();
      yasnoCity = yasnoStreet = yasnoHouse = yasnoGroup = "";
      menuOpen("yasno", "🗑 Адресу YASNO очищено.\n\n" + yasnoMenuText());
      return true;
    }
  }
  if (text == "🌐 Мережа та вебдоступ") {
    menuOpen("net", "🌐 <b>Мережа та вебдоступ</b>\nНалаштування підключення Telegram і публічного вебінтерфейсу.");
    return true;
  }
  if (text == "✈️ Telegram" || text == "✈️ Telegram: інформація") {
    String t = "✈️ <b>Telegram</b>\n";
    t += "Власник (Chat ID): <code>" + htmlEsc(chatId) + "</code>\n";
    t += "Group ID з налаштувань: " + ((groupId.length() > 0 && groupId != "1") ? "<code>" + htmlEsc(groupId) + "</code>" : String("не задано")) + "\n";
    t += "Бот бачить звичайні повідомлення в групах: ";
    t += botCanReadAllGroup ? "так\n" : "лише як адміністратор (Group Privacy увімкнено)\n";
    t += "Змінити токен або ID: 💡 Система → 🌐 Вебналаштування.";
    menuSend(t, "HTML");
    return true;
  }
  if (text == "📱 Особисті сповіщення") {
    String t = "📱 <b>Особисті сповіщення</b>\n";
    t += "Керуйте повідомленнями, які надходять лише у приватний чат власника.\n";
    t += "Ці перемикачі не впливають на групи й канали.\n";
    t += "Для аварій напруги додатково має бути увімкнений загальний контроль напруги.";
    menuOpen("route", t);
    return true;
  }
  if (text.startsWith("👥 Групи й канали")) {
    int n = chatActiveCount();
    String t = "👥 <b>Сповіщення груп і каналів: " + String(n) + "</b>\n";
    t += "Оберіть чат і налаштуйте кожну подію окремо.";
    if (n == 0) t += "\nДодайте бота до групи або каналу — він з'явиться тут автоматично.";
    menuOpen("grp", t);
    return true;
  }
  if (text == "🕐 Час і регіон") { menuOpen("time", menuTimeText()); return true; }
  if (text.startsWith("🕐 Автоперехід часу")) {
    if (autoDst) timeAutoOff(); else timeAutoOn();
    menuOpen("time", menuTimeText());
    return true;
  }
  if (text == "☀️ Літній вручну") { timeSetSummer(); menuOpen("time", menuTimeText()); return true; }
  if (text == "❄️ Зимовий вручну") { timeSetWinter(); menuOpen("time", menuTimeText()); return true; }

  if (menuState == "grp" && text.length() > 2 && isDigit(text[0])) {
    int dot = text.indexOf('.');
    int i = (dot > 0) ? chatByListNumber(text.substring(0, dot).toInt()) : -1;
    if (i >= 0) {
      chatRefreshAccess(i);
      if (chats.e[i].status > 0) {
        menuChatIdx = i;
        menuOpen("chat", menuChatText(i));
      } else {
        menuOpen("grp", "Бота вже видалено з цього чату.");
      }
      return true;
    }
  }

  if (menuState == "route") {
    bool saved = true;
    if (text.startsWith("🔔 Мені: світло зникло/з'явилося")) {
      saved = toggleNotificationPref(ownerNotifyOn, "oNotify");
    } else if (text.startsWith("⚡ Мені: аварії напруги")) {
      saved = toggleNotificationPref(ownerVoltAlertOn, "oVAlert");
    } else if (text.startsWith("⚡ Мені: напруга після появи світла")) {
      saved = toggleNotificationPref(ownerVoltInLightOn, "oVInOn");
    } else if (text.startsWith("📅 Мені: розклад у повідомленні про світло")) {
      saved = toggleNotificationPref(ownerYasnoEventOn, "oYsEvt");
    } else if (text.startsWith("📊 Мені: команда «Графік»")) {
      saved = toggleNotificationPref(ownerYasnoQueryOn, "oYsQry");
    } else if (text.startsWith("📅 Мені: автопублікація графіка")) {
      saved = toggleNotificationPref(ownerYasnoPublishOn, "oYsPub");
      if (saved && ownerYasnoPublishOn) preferences.putUInt("ypOwnerSig", 0);
    } else if (text.startsWith("🚨 Мені: сповіщення про екстрений режим")) {
      saved = toggleNotificationPref(ownerYasnoEmergencyOn, "oYsEm");
      if (saved && ownerYasnoEmergencyOn) preferences.putBool("yeOwnerSt", false);
    } else {
      return false;
    }
    String note = saved ? "Налаштування збережено." :
                          "❌ Не вдалося зберегти налаштування. Попереднє значення залишено.";
    menuOpen("route", "📣 <b>Особисті сповіщення</b>\n" + note);
    return true;
  }

  if (menuState == "chat" && menuChatIdx >= 0 && text == "✅ Дозволити цей чат") {
    bool ok = chatApprove(menuChatIdx);
    if (ok) chatSendWelcome(menuChatIdx);
    menuOpen("chat", ok ? menuChatText(menuChatIdx) : String("❌ Не вдалося зберегти дозвіл. Спробуйте ще раз."));
    return true;
  }

  if (menuState == "chat" && menuChatIdx >= 0 && text == "🚪 Вийти з цього чату") {
    String ask = "🚪 Light Watcher вийде з «" + htmlEsc(chatShortTitle(chats.e[menuChatIdx].title, 40)) + "».\n"
                 "У чат буде надіслано прощальне повідомлення, сповіщення туди більше не надходитимуть.";
    String kb = confirmKeyboard("✅ Вийти", "x:lvY:" + String(chats.e[menuChatIdx].id), "❌ Скасувати", "x:lvN");
    bot.sendMessageWithInlineKeyboard(chatId, ask, "HTML", kb);
    return true;
  }

  if (menuState == "chat" && menuChatIdx >= 0) {
    uint8_t bit = 0;
    if (text.startsWith("💡 Команда «Світло є?»")) bit = CHAT_F_LIGHT;
    else if (text.startsWith("⚡ Команда «Яка напруга?»")) bit = CHAT_F_VOLT;
    else if (text.startsWith("⚡ Напруга у відповіді про світло")) bit = CHAT_F_VOLT_IN_LIGHT;
    else if (text.startsWith("🔔 Зникнення / поява світла")) bit = CHAT_F_NOTIFY;
    else if (text.startsWith("🚨 Аварії напруги")) bit = CHAT_F_VALERT;
    else if (text.startsWith("📊 Команда «Графік»")) bit = CHAT_F_YASNO_QUERY;
    else if (text.startsWith("📅 Автопублікація графіка")) bit = CHAT_F_YASNO_PUBLISH;
    else if (text.startsWith("🚨 Екстрений режим YASNO")) bit = CHAT_F_YASNO_EMERGENCY;
    const bool scheduleToggle = text.startsWith("📅 Розклад у повідомленні про світло");
    if ((bit != 0 || scheduleToggle) && !chatApproved(chats.e[menuChatIdx])) {
      menuOpen("chat", menuChatText(menuChatIdx));
      return true;
    }
    if (scheduleToggle) {
      chats.e[menuChatIdx].pad[0] ^= 0x01;
      chatsSave();
      if (!chatsSaveOk) {
        chats.e[menuChatIdx].pad[0] ^= 0x01;
        chatsSave();
        menuOpen("chat", "❌ Не вдалося зберегти налаштування цього чату. Попереднє значення залишено.");
      } else {
        menuOpen("chat", menuChatText(menuChatIdx));
      }
      return true;
    }
    if (bit != 0) {
      chats.e[menuChatIdx].flags ^= bit;
      chatsSave();
      if (!chatsSaveOk) {
        chats.e[menuChatIdx].flags ^= bit;
        chatsSave();
        menuOpen("chat", "❌ Не вдалося зберегти налаштування цього чату. Попереднє значення залишено.");
        return true;
      }
      if (bit == CHAT_F_YASNO_PUBLISH || bit == CHAT_F_YASNO_EMERGENCY) {
        if (bit == CHAT_F_YASNO_PUBLISH && (chats.e[menuChatIdx].flags & CHAT_F_YASNO_PUBLISH)) {
          String sigKey = "yp" + String(menuChatIdx) + "sig";
          preferences.putUInt(sigKey.c_str(), 0);
        }
        if (bit == CHAT_F_YASNO_EMERGENCY && (chats.e[menuChatIdx].flags & CHAT_F_YASNO_EMERGENCY)) {
          String emKey = "ye" + String(menuChatIdx) + "st";
          preferences.putBool(emKey.c_str(), false);
        }
        yasnoPublishCursor = (uint8_t)menuChatIdx;
        yasnoPublishLastMs = 0;
      }
      menuOpen("chat", menuChatText(menuChatIdx));
      return true;
    }
  }

  if (handleMenuButtonV17(text)) {
    return true;
  }

  if (text == "⚡ Shelly") {
    String ip = shellyIP;
    ip.trim();
    String t = "🔌 <b>Shelly</b>\n";
    if (ip.length() == 0) {
      t += "IP: не задано (вебналаштування)\nЗв'язок: —";
    } else {
      float v = getShellyVoltage();
      t += "IP: <code>" + htmlEsc(ip) + "</code>\n";
      t += isnan(v) ? String("Зв'язок: 🔴 не відповідає") : "Зв'язок: 🟢 на зв'язку\n⚡ Напруга: " + voltageText(v);
    }
    menuSend(t, "HTML");
    return true;
  }
  return false;
}

void handleMenuCallback(const String& data, int messageId) {
  if (guideCallback(data, messageId)) {
    return;
  }
  if (data.startsWith("x:rstY")) {
    const bool fresh = menuRestartNonce.length() > 0 && data == "x:rstY:" + menuRestartNonce &&
                       millis() - menuRestartMs <= 120000UL;
    menuRestartNonce = "";
    if (!fresh) {
      bot.sendMessageWithInlineKeyboard(chatId, "⌛ Підтвердження застаріло. Натисніть «🔁 Перезапуск» ще раз.", "", "[]", messageId);
      return;
    }
    bot.sendMessageWithInlineKeyboard(chatId, "🔁 Перезапуск...", "", "[]", messageId);
    prepareForRestart("кнопка «🔁 Перезапуск» у меню");
    delay(1000);
    ESP.restart();
  }
  else if (data == "x:rstN") {
    bot.sendMessageWithInlineKeyboard(chatId, "Перезапуск скасовано.", "", "[]", messageId);
  }
  else if (data.startsWith("x:apY:") || data.startsWith("x:apN:")) {
    const bool allow = data.startsWith("x:apY:");
    int i = chatFind(data.substring(6));
    if (i < 0 || !chats.e[i].used) {
      bot.sendMessageWithInlineKeyboard(chatId, "Цього чату вже немає в реєстрі.", "", "[]", messageId);
      return;
    }
    const String title = htmlEsc(chatShortTitle(chats.e[i].title, 40));
    if (allow) {
      bool ok = chatApprove(i);
      if (ok) chatSendWelcome(i);
      bot.sendMessageWithInlineKeyboard(chatId, ok ? "✅ «" + title + "» дозволено. Налаштування чату: 👥 Групи й канали."
                                                  : String("❌ Не вдалося зберегти дозвіл. Спробуйте ще раз."), "HTML", "[]", messageId);
      return;
    }
    if (chatApproved(chats.e[i])) {
      bot.sendMessageWithInlineKeyboard(chatId, "«" + title + "» уже дозволено. Вийти з чату можна в його налаштуваннях.", "HTML", "[]", messageId);
      return;
    }
    const String id = String(chats.e[i].id);
    bool left = chats.e[i].status == 0 ||
                telegramPostRaw("leaveChat", "{\"chat_id\":\"" + id + "\"}").indexOf("\"ok\":true") >= 0;
    if (left) {
      memset(&chats.e[i], 0, sizeof(ChatEntry));
      chatsSave();
      if (menuChatIdx == i) menuChatIdx = -1;
      logEvent(LC_TG, LS_INFO, "Власник відхилив чат " + id);
    }
    bot.sendMessageWithInlineKeyboard(chatId, left ? "🚫 «" + title + "» відхилено, Light Watcher вийшов із чату."
                                                   : String("❌ Не вдалося вийти з чату. Спробуйте ще раз."), "HTML", "[]", messageId);
  }
  else if (data == "x:lvN") {
    bot.sendMessageWithInlineKeyboard(chatId, "Вихід скасовано.", "", "[]", messageId);
  }
  else if (data.startsWith("x:lvY:")) {
    int i = chatFind(data.substring(6));
    if (i < 0 || i >= MAX_CHATS || !chats.e[i].used || chats.e[i].status == 0) {
      bot.sendMessageWithInlineKeyboard(chatId, "Бота в цьому чаті вже немає.", "", "[]", messageId);
      return;
    }
    String id = String(chats.e[i].id);
    String title = chatShortTitle(chats.e[i].title, 40);
    if (chats.e[i].canPost) {
      bot.sendMessage(id, TEXT_BOT_LEFT, "HTML");
    }
    String resp = telegramPostRaw("leaveChat", "{\"chat_id\":\"" + id + "\"}");
    bool ok = resp.indexOf("\"ok\":true") >= 0;
    if (ok) {
      chats.e[i].status = 0;
      chatsSave();
      if (menuChatIdx == i) {
        menuChatIdx = -1;
      }
    }
    bot.sendMessageWithInlineKeyboard(chatId, ok ? "✅ Light Watcher вийшов з «" + htmlEsc(title) + "»." : String("❌ Не вдалося вийти з чату. Спробуйте ще раз."), "HTML", "[]", messageId);
    if (ok) {
      menuOpen("grp", "👥 Групи й канали: " + String(chatActiveCount()));
    }
  }
}

String guideText(int step) {
  String dom = webDomain.length() > 0 ? htmlEsc(webDomain) : String("light.ваш-домен.ua");
  String ip = webIpManual ? webIpVal : pubIpAuto;
  String head = "📖 <b>Інструкція: домен і HTTPS</b> · етап " + String(step) + "/" + String(GUIDE_STEPS) + "\n\n";
  switch (step) {
    case 1:
      return head + "<b>1. Підготовка домену</b>\n"
             "• Потрібен власний домен у реєстратора, який дозволяє змінити NS-сервери (напр. NIC.UA, домени .pp.ua тощо). "
             "Чужі безкоштовні піддомени (duckdns.org тощо) до Cloudflare не підключаються.\n"
             "• dash.cloudflare.com → <b>Add a domain</b> → введіть домен (напр. example.com) → тариф <b>Free</b>.\n"
             "• Cloudflare покаже 2 NS-сервери (…ns.cloudflare.com). У кабінеті реєстратора замініть ними поточні NS.\n"
             "• Зачекайте активації (від кількох хвилин до 24 год): у Cloudflare статус домену стане <b>Active</b>, прийде лист.\n"
             "Для Light Watcher зручно використати піддомен, напр. <code>light.example.com</code>.";
    case 2:
      return head + "<b>2. Налаштування DNS</b>\n"
             "• Cloudflare → ваш домен → <b>DNS → Records → Add record</b>.\n"
             "• Type: <b>A</b>; Name: <code>light</code> (або @ для самого домену); IPv4: "
             + (ip.length() > 0 ? "<code>" + ip + "</code> (ваша поточна публічна IP)" : String("ваша публічна IP (бот покаже її в 🌐 Вебдоступ)")) + ".\n"
             "• Proxy status: <b>Proxied</b> - помаранчева хмаринка. Так відвідувачі заходять на Cloudflare: він видає сертифікат HTTPS і ховає вашу IP. "
             "Сіра хмаринка (DNS only) - HTTPS не працюватиме.\n"
             "• TTL: Auto → Save.\n"
             "• У боті: 🌐 Вебдоступ → 🌍 Доменне ім'я → <code>" + dom + "</code>.\n"
             "⚠️ Якщо провайдер змінить публічну IP, запис A треба оновити.";
    case 3:
      return head + "<b>3. Cloudflare SSL</b>\n"
             "• <b>SSL/TLS → Overview</b> → режим <b>Flexible</b>.\n"
             "  - Flexible: браузер ↔ Cloudflare по HTTPS, Cloudflare ↔ ваш дім по HTTP. Підходить: ESP32 працює лише по HTTP.\n"
             "  - Full / Full (strict): Cloudflare вимагає HTTPS від пристрою - з ESP32 буде помилка 525/526.\n"
             "• <b>SSL/TLS → Edge Certificates</b> → <b>Always Use HTTPS: On</b> (http:// автоматично стане https://; ESP32 переадресацій не робить, циклу не буде). "
             "Там само статус сертифіката має бути Active (видається автоматично).\n"
             "• <b>Rules → Origin Rules → Create rule</b>: Hostname equals <code>" + dom + "</code> → Destination Port → Rewrite to <b>8080</b> → Deploy.\n"
             "  У режимі Flexible Cloudflare звертається до роутера на порт 80; правило змінює його на 8080, і наявне перенаправлення 8080 залишається без змін.";
    case 4:
      return head + "<b>4. Роутер</b>\n"
             "• Закріпіть IP за ESP32: DHCP → резервування адрес (Address Reservation) → пристрій esp32c3-… → IP <code>" + WiFi.localIP().toString() + "</code>.\n"
             "• Port Forwarding / Virtual Server:\n"
             "  - Варіант А (з Origin Rule з етапу 3): зовнішній <b>8080</b> TCP → <code>" + WiFi.localIP().toString() + "</code>:<b>8080</b>. Якщо таке правило вже є - нічого не змінюйте.\n"
             "  - Варіант Б (без Origin Rule): зовнішній <b>80</b> TCP → <code>" + WiFi.localIP().toString() + "</code>:<b>8080</b>.\n"
             "• Порт 443 на роутері відкривати не потрібно: HTTPS 443 приймає Cloudflare, а до вас він ходить по HTTP.\n"
             "• Ніколи не перенаправляйте на внутрішній порт 80 - це адмінпанель. Вона й так відповідає лише пристроям з вашої локальної мережі.";
    case 5:
      return head + "<b>5. Перевірка</b>\n"
             "• Натисніть 🔄 Перевірити налаштування: пристрій перевірить сервер 8080, інтернет, DNS, проксі Cloudflare і відкриє https://" + dom + " через інтернет "
             "(Cloudflare звертається до вашого роутера ззовні - це справжня перевірка перенаправлення).\n"
             "• Потім на телефоні вимкніть Wi-Fi, залиште мобільний інтернет і відкрийте <code>https://" + dom + "/</code>: має бути замок і сторінка Light Watcher.\n"
             "• Помилки Cloudflare: 521/522 - перенаправлення порту або IP у записі A; 525/526 - режим не Flexible; сторінка не відкривається зовсім - DNS ще не оновився.";
    default:
      return head + "<b>6. Готово</b>\n"
             "Публічна сторінка:\n" + (webPublicLink().length() > 0 && webDomOn ? webPublicLink() : String("https://" + dom + "/")) + "\n\n"
             "Надіслати посилання в групу: 🌐 Вебдоступ → 📤 Надіслати посилання в групу.\n"
             "Інструкція доступна будь-коли: 🌐 Вебдоступ → 📖 Інструкція налаштування.";
  }
}

String guideKeyboard(int step) {
  if (step >= GUIDE_STEPS) {
    String link = webDomOn ? webPublicLink() : String("");
    String row1 = link.length() > 0 ? "{\"text\":\"📋 Скопіювати посилання\",\"copy_text\":{\"text\":\"" + jsonEsc(link) + "\"}}," : String("");
    return "[[" + row1 + "{\"text\":\"🔄 Перевірити доступність\",\"callback_data\":\"x:gchk\"}],"
           "[{\"text\":\"⬅️ Назад\",\"callback_data\":\"x:g:" + String(step - 1) + "\"},{\"text\":\"🏠 Головне меню\",\"callback_data\":\"x:ghome\"}]]";
  }
  String nav = (step > 1) ? "{\"text\":\"⬅️ Назад\",\"callback_data\":\"x:g:" + String(step - 1) + "\"}," : String("");
  return "[[" + nav + "{\"text\":\"Далі ➡️\",\"callback_data\":\"x:g:" + String(step + 1) + "\"}],"
         "[{\"text\":\"🔄 Перевірити налаштування\",\"callback_data\":\"x:gchk\"}],"
         "[{\"text\":\"🏠 Головне меню\",\"callback_data\":\"x:ghome\"}]]";
}

void guideShow(int step, int messageId) {
  if (step < 1) step = 1;
  if (step > GUIDE_STEPS) step = GUIDE_STEPS;
  if (messageId > 0) {
    bot.sendMessageWithInlineKeyboard(chatId, guideText(step), "HTML", guideKeyboard(step), messageId);
  } else {
    bot.sendMessageWithInlineKeyboard(chatId, guideText(step), "HTML", guideKeyboard(step));
  }
}

bool guideCallback(const String& data, int messageId) {
  if (data.startsWith("x:g:")) {
    guideShow(data.substring(4).toInt(), messageId);
    return true;
  }
  if (data == "x:gchk") {
    menuSend("⏳ Перевіряю, це може зайняти до 25 с…", "");
    menuSend(webCheckText(), "HTML");
    return true;
  }
  if (data == "x:ghome") {
    menuInput = "";
    menuGoMain();
    return true;
  }
  return false;
}

void menuInputStart(const String& mode, const String& back, const String& prompt) {
  menuInput = mode;
  menuInputBack = back;
  menuOpen("inp", prompt);
}

void menuInputReturn(const String& note) {
  String back = menuInputBack;
  menuInput = "";
  menuInputBack = "";
  menuPendingLogin = "";
  menuPendingPass = "";
  if (back == "pwd") {
    menuOpenClean("pwd", (note.length() > 0 ? note + "\n\n" : "") + admStatusText());
  } else if (back == "web") {
    webMenuShowClean(note);
  } else if (back == "volt") {
    menuOpenClean("volt", (note.length() > 0 ? note + "\n\n" : "") + voltMenuText());
  } else if (back == "sys") {
    menuOpenClean("sys", note.length() > 0 ? note : String("💡 <b>Система</b>\nОберіть дію"));
  } else if (back == "yasno") {
    yasnoPendingCity = "";
    yasnoPendingStreet = "";
    yasnoPendingStreetId = -1;
    menuOpenClean("yasno", (note.length() > 0 ? note + "\n\n" : "") + yasnoMenuText());
  } else {
    menuOpenClean("set", note.length() > 0 ? note : String("⚙️ <b>Налаштування</b>\nОберіть категорію"));
  }
}

int utf8Chars(const String& s) {
  int n = 0;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (((uint8_t)s[i] & 0xC0) != 0x80) {
      n++;
    }
  }
  return n;
}

void handleMenuInput(const String& textIn) {
  String text = textIn;
  String mode = menuInput;
  if (mode == "pass1" || mode == "pass2") {
    int id = menuCurrentUserMsg;
    menuDeleteIds(&id, 1);
    menuCurrentUserMsg = 0;
  }
  if (text == "❌ Скасувати" || text == "◀️ Назад") {
    if (mode == "yasno_wait" && (yasnoTaskRunning || yasnoTaskDone)) yasnoTaskCancelled = true;
    menuInputReturn("Скасовано. Нічого не змінено.");
    return;
  }
  if (mode == "yasno_wait") {
    menuSend("⏳ Запит YASNO ще виконується. Дочекайтеся результату або натисніть ❌ Скасувати.", "");
    return;
  }
  if (mode == "fw") {
    otaInputText(text);
    return;
  }

  if (mode == "login") {
    text.trim();
    if (!admValidLogin(text)) {
      menuSend("❌ Логін має містити 3–32 символи: латинські літери, цифри, крапку, _ або -. Введіть ще раз або натисніть ❌ Скасувати.", "");
      return;
    }
    if (menuLoginThenPass) {
      menuPendingLogin = text;
      menuInput = "pass1";
      menuSend("🔑 Введіть пароль (8–64 символи, без пробілів на початку й у кінці).\nБот одразу видалить повідомлення з паролем із чату.", "");
    } else {
      if (!admSetLogin(text)) {
        menuInputReturn("❌ Логін не збережено: запис у пам'ять пристрою не підтвердився. Діє попередній логін.");
        return;
      }
      menuInputReturn("✅ Логін змінено. Усі відкриті входи у вебпанель завершено.");
    }
    return;
  }

  if (mode == "pass1") {
    String trimmed = text;
    trimmed.trim();
    int chars = utf8Chars(text);
    if (chars < 8 || chars > 64 || text.length() > 200 || trimmed != text) {
      menuSend("❌ Пароль має містити 8–64 символи без пробілів на початку й у кінці. Введіть ще раз або натисніть ❌ Скасувати.", "");
      return;
    }
    menuPendingPass = text;
    menuInput = "pass2";
    menuSend("🔁 Повторіть пароль для підтвердження.", "");
    return;
  }

  if (mode == "pass2") {
    if (text != menuPendingPass) {
      menuPendingPass = "";
      menuInput = "pass1";
      menuSend("❌ Паролі не збігаються. Введіть новий пароль ще раз.", "");
      return;
    }

    if (menuPendingLogin.length() > 0 && !admSetLogin(menuPendingLogin)) {
      menuInputReturn("❌ Логін і пароль не збережено: запис у пам'ять пристрою не підтвердився.");
      return;
    }
    if (!admSetPassword(text)) {
      menuInputReturn(String("❌ Пароль не збережено: запис у пам'ять пристрою не підтвердився. Діє попередній пароль")
                      + (menuPendingLogin.length() > 0 ? " (логін уже змінено)." : "."));
      return;
    }
    menuInputReturn("✅ Пароль збережено. Він одразу діє для адмінпанелі; старий пароль більше не працює, усі відкриті входи завершено.");
    return;
  }

  if (mode == "yasno_city") {
    String city = text;
    city.trim();
    String cityLower = city;
    cityLower.toLowerCase();
    if (!(city == "Київ" || city == "Киев" || cityLower == "kyiv" || city == "м. Київ" || city == "м.Київ")) {
      menuSend("❌ У тестовій збірці поки підтримується лише Київ. Введіть «Київ» або натисніть ❌ Скасувати.", "");
      return;
    }
    yasnoPendingCity = "Київ";
    menuInputStart("yasno_street", "yasno", "🏘 Введіть назву вулиці так, як вона записана в YASNO (мінімум 3 символи).\nНаприклад: Хрещатик");
    return;
  }

  if (mode == "yasno_street") {
    String street = text;
    street.trim();
    if (utf8Chars(street) < 3 || street.length() > 100) {
      menuSend("❌ Назва вулиці має містити щонайменше 3 символи й не перевищувати 100 символів. Введіть ще раз або скасуйте.", "");
      return;
    }
    String error;
    if (!yasnoStartTask(1, street, error)) {
      menuSend("❌ " + htmlEsc(error) + " Введіть назву вулиці ще раз або скасуйте.", "HTML");
      return;
    }
    menuInputStart("yasno_wait", "yasno", "⏳ Шукаю вулицю в YASNO у фоновому режимі. Основний моніторинг електроенергії продовжує працювати.");
    return;
  }

  if (mode == "yasno_house") {
    String house = text;
    house.trim();
    if (house.length() == 0 || house.length() > 20) {
      menuSend("❌ Введіть коректний номер будинку (до 20 символів).", "");
      return;
    }
    if (yasnoPendingStreetId <= 0 || yasnoPendingStreet.length() == 0 || yasnoPendingCity.length() == 0) {
      menuInputReturn("❌ Стан налаштування адреси втрачено. Почніть введення адреси заново.");
      return;
    }
    String error;
    if (!yasnoStartTask(2, house, error)) {
      menuSend("❌ " + htmlEsc(error) + " Введіть номер будинку ще раз або скасуйте.", "HTML");
      return;
    }
    menuInputStart("yasno_wait", "yasno", "⏳ Шукаю будинок і визначаю чергу YASNO у фоновому режимі. Основний моніторинг продовжує працювати.");
    return;
  }

  if (mode == "domain") {
    String d = webCleanHost(text);
    if (!webValidDomain(d)) {
      menuSend("❌ Некоректне доменне ім'я. Приклад: light.example.com (без https:// і без порту). Введіть ще раз або натисніть ❌ Скасувати.", "");
      return;
    }
    if (d != webDomain) {
      webCfState = 0;
      webHttpsState = 0;
      webCheckAt = 0;
    }
    webDomain = d;
    webDomOn = true;
    preferences.putString("webDomain", webDomain);
    preferences.putBool("webDomOn", webDomOn);
    menuInputReturn("✅ Домен збережено, використання домену: 🟢 ON.");
    return;
  }

  if (mode == "ip") {
    String s = webCleanHost(text);
    IPAddress ip;
    if (!ip.fromString(s) || s.indexOf('.') < 0) {
      menuSend("❌ Некоректна IP-адреса. Приклад: 93.184.216.34. Введіть ще раз або натисніть ❌ Скасувати.", "");
      return;
    }
    if (webIsPrivateIp(ip)) {
      menuSend("❌ Це локальна або службова адреса (10.x, 172.16–31.x, 192.168.x, 100.64–127.x тощо), вона не працює з інтернету. Введіть публічну IP або натисніть ❌ Скасувати.", "");
      return;
    }
    webIpVal = ip.toString();
    webIpManual = true;
    preferences.putString("webIpVal", webIpVal);
    preferences.putBool("webIpMan", webIpManual);
    menuInputReturn("✅ IP збережено (вручну).");
    return;
  }

  if (mode == "vlo" || mode == "vhi" || mode == "vsl" || mode == "vsh" || mode == "vint" || mode == "vcnf" || mode == "vpau") {
    text.trim();
    bool digits = text.length() > 0 && text.length() <= 4;
    for (unsigned int k = 0; k < text.length(); k++) {
      if (!isDigit(text[k])) digits = false;
    }
    int n = digits ? text.toInt() : -1;
    String err = "";
    const String nestErr = "\nСтабільний діапазон (" + String(voltStableLow) + "–" + String(voltStableHigh) +
                           " В) має лежати всередині аварійних меж (" + String(voltLow) + "–" + String(voltHigh) + " В).";
    if (mode == "vlo") {
      if (n < 150 || n > 240) err = "Нижня межа: ціле число від 150 до 240 В.";
      else if (n >= voltHigh) err = "Нижня аварійна межа має бути меншою за верхню (" + String(voltHigh) + " В).";
      else if (!voltThresholdsConsistent(n, voltHigh, voltStableLow, voltStableHigh)) err = "Нижня аварійна межа не може бути вищою за стабільний діапазон." + nestErr;
      else { voltLow = n; preferences.putInt("vLow", voltLow); }
    } else if (mode == "vhi") {
      if (n < 220 || n > 280) err = "Верхня межа: ціле число від 220 до 280 В.";
      else if (n <= voltLow) err = "Верхня аварійна межа має бути більшою за нижню (" + String(voltLow) + " В).";
      else if (!voltThresholdsConsistent(voltLow, n, voltStableLow, voltStableHigh)) err = "Верхня аварійна межа не може бути нижчою за стабільний діапазон." + nestErr;
      else { voltHigh = n; preferences.putInt("vHigh", voltHigh); }
    } else if (mode == "vsl") {
      if (n < 150 || n > 279) err = "Нижня межа стабільної напруги: ціле число від 150 до 279 В.";
      else if (n >= voltStableHigh) err = "Нижня межа має бути меншою за верхню (" + String(voltStableHigh) + " В).";
      else if (!voltThresholdsConsistent(voltLow, voltHigh, n, voltStableHigh)) err = "Стабільна напруга не може починатися нижче аварійної межі." + nestErr;
      else { voltStableLow = n; preferences.putInt("vStLow", voltStableLow); }
    } else if (mode == "vsh") {
      if (n < 151 || n > 280) err = "Верхня межа стабільної напруги: ціле число від 151 до 280 В.";
      else if (n <= voltStableLow) err = "Верхня межа має бути більшою за нижню (" + String(voltStableLow) + " В).";
      else if (!voltThresholdsConsistent(voltLow, voltHigh, voltStableLow, n)) err = "Стабільна напруга не може закінчуватися вище аварійної межі." + nestErr;
      else { voltStableHigh = n; preferences.putInt("vStHigh", voltStableHigh); }
    } else if (mode == "vint") {
      if (n < 10 || n > 600) err = "Інтервал: ціле число від 10 до 600 с.";
      else { vIntSec = n; preferences.putInt("vIntSec", vIntSec); }
    } else if (mode == "vcnf") {
      if (n < 1 || n > 10) err = "Підтвердження: ціле число від 1 до 10.";
      else { vConfirm = n; preferences.putInt("vConfirm", vConfirm); }
    } else {
      if (n < 0 || n > 120) err = "Пауза: ціле число від 0 до 120 хв.";
      else { vPauseMin = n; preferences.putInt("vPauseMin", vPauseMin); }
    }
    if (err.length() > 0) {
      menuSend("❌ " + err + " Введіть ще раз або натисніть ❌ Скасувати.", "");
      return;
    }
    menuInputReturn("✅ Збережено.");
    return;
  }
  menuInputReturn("");
}

static void webLinkSend() {
  String link = webPublicLink();
  if (link.length() > 0) {
    String kb = "[[{\"text\":\"📋 Скопіювати посилання\",\"copy_text\":{\"text\":\"" + jsonEsc(link) + "\"}}]]";
    bot.sendMessageWithInlineKeyboard(chatId, "🔗 " + link, "", kb);
  }
}

void webMenuShowClean(const String& note) {
  menuOpenClean("web", (note.length() > 0 ? note + "\n\n" : "") + webMenuText());
  webLinkSend();
}

void webMenuShow(const String& note) {
  menuOpen("web", (note.length() > 0 ? note + "\n\n" : "") + webMenuText());
  webLinkSend();
}

String menuKeyboardV17(const String& state) {
  if (state == "sec") {
    return "[" + kbRow("🔑 Пароль адміністратора", "") + "," + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "pwd") {
    if (admHasPass) {
      return "[" + kbRow("🔑 Змінити пароль", "") + "," + kbRow("👤 Змінити логін", "") + "," + kbRow("◀️ Назад", "") + "]";
    }
    return "[" + kbRow("🔑 Створити пароль", "") + "," + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "web") {
    return "[" + kbRow("🌐 Використання домену: " + onOff(webDomOn), "") + ","
           + kbRow("🌍 Доменне ім'я", webIpManual ? "🔄 IP автоматично" : "✏️ Ввести IP вручну") + ","
           + kbRow("🔄 Перевірити налаштування", "") + "," + kbRow("📖 Інструкція налаштування", "") + ","
           + kbRow("🛡 Лише через Cloudflare: " + onOff(webCfOnly), "") + ","
           + kbRow("📤 Надіслати посилання в групу", "") + "," + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "wgrp") {
    String kb = "[";
    int num = 0;
    for (int i = 0; i < MAX_CHATS; i++) {
      ChatEntry& c = chats.e[i];
      if (!c.used || c.status == 0) {
        continue;
      }
      num++;
      kb += kbRow(String(num) + ". " + chatShortTitle(c.title, 24), "") + ",";
    }
    return kb + kbRow("◀️ Назад", "") + "]";
  }
  if (state == "inp") {
    return "[" + kbRow("❌ Скасувати", "") + "]";
  }
  if (state == "volt") {
    return "[" + kbRow("🔔 Сповіщення про напругу: " + onOff(vAlertOn), "") + ","
           + kbRow("📉 Нижня межа: " + String(voltLow) + " В", "📈 Верхня межа: " + String(voltHigh) + " В") + ","
           + kbRow("✅ Стабільна напруга від: " + String(voltStableLow) + " В", "✅ Стабільна напруга до: " + String(voltStableHigh) + " В") + ","
           + kbRow("⏱ Інтервал: " + String(vIntSec) + " с", "") + ","
           + kbRow("🔁 Підтвердження: " + String(vConfirm), "⏸ Пауза: " + String(vPauseMin) + " хв") + ","
           + kbRow("◀️ Назад", "") + "]";
  }
  return "";
}

bool handleMenuButtonV17(const String& text) {
  if (text == "🔐 Безпека") {
    menuOpen("sec", "🔐 <b>Безпека</b>\nДоступ до адмінпанелі (порт 80, лише локальна мережа).");
    return true;
  }
  if (text == "🔑 Пароль адміністратора") {
    menuOpen("pwd", admStatusText());
    return true;
  }
  if (menuState == "pwd" && (text == "🔑 Створити пароль" || text == "🔑 Змінити пароль")) {
    if (admUser.length() == 0) {
      menuLoginThenPass = true;
      menuInputStart("login", "pwd", "👤 Введіть логін адміністратора (3–32 символи: латинські літери, цифри, крапка, _ або -).");
    } else {
      menuLoginThenPass = false;
      menuInputStart("pass1", "pwd", "🔑 Введіть новий пароль (8–64 символи, без пробілів на початку й у кінці).\nБот одразу видалить повідомлення з паролем із чату.");
    }
    return true;
  }
  if (menuState == "pwd" && text == "👤 Змінити логін") {
    menuLoginThenPass = false;
    menuInputStart("login", "pwd", "👤 Введіть новий логін (3–32 символи: латинські літери, цифри, крапка, _ або -). Пароль не зміниться.");
    return true;
  }
  if (text == "🌐 Вебдоступ") {
    if (!webDomOn && !webIpManual && (pubIpAuto.length() == 0 || millis() - pubIpLastTry > 1800000UL)) {
      publicIpRefresh();
    }
    webMenuShow("");
    return true;
  }
  if (text == "⚡ Контроль напруги") {
    menuOpen("volt", voltMenuText());
    return true;
  }
  if (menuState == "volt") {
    if (text.startsWith("🔔 Сповіщення про напругу")) {
      vAlertOn = !vAlertOn;
      preferences.putBool("vAlert", vAlertOn);
      menuOpen("volt", voltMenuText());
      return true;
    }
    if (text.startsWith("📉 Нижня межа")) {
      menuInputStart("vlo", "volt", "📉 Нижня межа зараз " + String(voltLow) + " В. Введіть нове значення (150–240 В), напр.: 195");
      return true;
    }
    if (text.startsWith("📈 Верхня межа")) {
      menuInputStart("vhi", "volt", "📈 Верхня межа зараз " + String(voltHigh) + " В. Введіть нове значення (220–280 В), напр.: 255");
      return true;
    }
    if (text.startsWith("✅ Стабільна напруга від")) {
      menuInputStart("vsl", "volt", "✅ Нижня межа стабільної напруги зараз " + String(voltStableLow) + " В. Введіть ціле число 150–279.");
      return true;
    }
    if (text.startsWith("✅ Стабільна напруга до")) {
      menuInputStart("vsh", "volt", "✅ Верхня межа стабільної напруги зараз " + String(voltStableHigh) + " В. Введіть ціле число 151–280.");
      return true;
    }
    if (text.startsWith("⏱ Інтервал")) {
      menuInputStart("vint", "volt", "⏱ Інтервал перевірки зараз " + String(vIntSec) + " с. Введіть 10–600 с, напр.: 30");
      return true;
    }
    if (text.startsWith("🔁 Підтвердження")) {
      menuInputStart("vcnf", "volt", "🔁 Підтвердження зараз " + String(vConfirm) + ". Скільки вимірювань поспіль поза межами потрібно для тривоги (1–10). 1 - одразу, більше - менше хибних тривог.");
      return true;
    }
    if (text.startsWith("⏸ Пауза")) {
      menuInputStart("vpau", "volt", "⏸ Пауза зараз " + String(vPauseMin) + " хв. Мінімальний час між тривогами, щоб не було частих повідомлень (0–120 хв, 0 - без паузи).");
      return true;
    }
  }
  if (menuState == "web") {
    if (text.startsWith("🌐 Використання домену")) {
      if (!webDomOn && webDomain.length() == 0) {
        menuInputStart("domain", "web", "🌍 Введіть доменне ім'я, підключене до Cloudflare (без https:// і без порту), напр.: light.example.com");
        return true;
      }
      webDomOn = !webDomOn;
      preferences.putBool("webDomOn", webDomOn);
      if (!webDomOn && !webIpManual) {
        publicIpRefresh();
      }
      webMenuShow("🌐 Використання домену: " + onOff(webDomOn));
      return true;
    }
    if (text == "🌍 Доменне ім'я") {
      menuInputStart("domain", "web", "🌍 Введіть доменне ім'я, підключене до Cloudflare (без https:// і без порту), напр.: light.example.com"
                     + (webDomain.length() > 0 ? "\nЗараз: " + webDomain : String("")));
      return true;
    }
    if (text == "📖 Інструкція налаштування") {
      guideShow(1, 0);
      return true;
    }
    if (text.startsWith("🛡 Лише через Cloudflare")) {
      if (!webCfOnly && (!webDomOn || webCfState != 1)) {

        webMenuShow("⚠️ Увімкнути не можна: спершу увімкніть домен і натисніть «🔄 Перевірити налаштування» - "
                    "має бути «☁️ Cloudflare: 🟢 проксі увімкнено». Якщо домен працює без Cloudflare (напр. DuckDNS напряму), цей захист не потрібен.");
        return true;
      }
      webCfOnly = !webCfOnly;
      preferences.putBool("webCfOnly", webCfOnly);
      webMenuShow(webCfOnly ? "🛡 Увімкнено: публічна сторінка відповідає лише Cloudflare і вашій домашній мережі."
                            : "🛡 Вимкнено: публічна сторінка відповідає всім.");
      return true;
    }
    if (text == "✏️ Ввести IP вручну") {
      menuInputStart("ip", "web", "✏️ Введіть публічну IP-адресу вашого підключення, напр.: 93.184.216.34");
      return true;
    }
    if (text == "🔄 IP автоматично") {
      webIpManual = false;
      preferences.putBool("webIpMan", webIpManual);
      publicIpRefresh();
      webMenuShow("🔄 Повернуто автоматичне визначення IP.");
      return true;
    }
    if (text == "🔄 Перевірити налаштування") {
      menuSend("⏳ Перевіряю, це може зайняти до 25 с…", "");
      menuSend(webCheckText(), "HTML");
      return true;
    }
    if (text == "📤 Надіслати посилання в групу") {
      String link = webPublicLink();
      if (link.length() == 0) {
        menuSend("Спершу задайте домен або IP - посилання ще не сформовано.", "");
      } else if (chatActiveCount() == 0) {
        menuSend("Немає груп чи каналів. Додайте бота до групи - він з'явиться тут автоматично.", "");
      } else {
        menuOpen("wgrp", "📤 Оберіть групу або канал. Буде надіслано посилання:\n" + link);
      }
      return true;
    }
  }
  if (menuState == "wgrp" && text.length() > 2 && isDigit(text[0])) {
    int dot = text.indexOf('.');
    int i = (dot > 0) ? chatByListNumber(text.substring(0, dot).toInt()) : -1;
    String link = webPublicLink();
    if (i >= 0 && link.length() > 0) {
      String title = chatShortTitle(chats.e[i].title, 40);
      bool ok = bot.sendMessage(String(chats.e[i].id),
        "💡 <b>Light Watcher підключено</b>\nСтан електромережі, статистика та графіки доступні за посиланням:\n" + link, "HTML");
      webMenuShow(ok ? "✅ Посилання надіслано в «" + htmlEsc(title) + "»." : "❌ Не вдалося надіслати в «" + htmlEsc(title) + "». Перевірте, чи бот може писати в цьому чаті.");
      return true;
    }
  }
  return false;
}
