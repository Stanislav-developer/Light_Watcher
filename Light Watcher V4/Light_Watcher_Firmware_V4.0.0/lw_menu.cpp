#include "lw.h"

static String menuRestartNonce = "";
static String menuLogNonce = "";
static unsigned long menuLogMs = 0;
static bool menuLoginThenPass = false;
static int menuChatIdx = -1;
static int menuLastMsg = 0;
static int menuTapMsg = 0;
static String cfmAction = "";
static String cfmArg = "";
static String cfmBack = "";
static String cfmYes = "";
static unsigned long cfmMs = 0;
static String webCheckDetail = "";

static bool chatToggle(int i, char code);
static String chatNotifyText(const ChatEntry& c);
static TgCleanState tgc;
static bool tgcDirty = false;
static unsigned long tgcSaveMs = 0;


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
  if (!tgSendText(chatId, text, "HTML", "")) return false;
  tgKeepAdd((int32_t)bot.last_sent_message_id);
  return true;
}

bool tgSendOwnerInfo(const String& text) {
  return tgSendText(chatId, text, "HTML", "");
}

void menuSend(const String& text, const String& parseMode) {
  tgSendText(chatId, text, parseMode, "");
}

bool menuGoMain() {
  menuCurrentUserMsg = 0;
  menuState = "main";
  if (!tgSendText(chatId, menuMainCard(), "HTML", tgReplyKbHint(menuKeyboard("main"), menuHint("main")))) {
    return false;
  }
  menuLastMsg = (int)bot.last_sent_message_id;
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
  return v ? "✅" : "❌";
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

String admStatusText() {
  String t = "🔗 http://" + WiFi.localIP().toString() + "/ (лише з домашньої мережі)\n";
  t += "👤 Логін: " + (admUser.length() > 0 ? "<code>" + htmlEsc(admUser) + "</code>" : String("не створено")) + "\n";
  t += String("🔐 Пароль: ") + (admHasPass ? "✅ створено" : "❌ не створено - вхід закрито");
  return t;
}

String webMenuText() {
  String t = "";
  if (!webStarted) {
    return t + "❌ Вебсервер не запущено" + (safeMode ? String(" (безпечний режим)") : String(""));
  }
  const String link = webPublicLink();
  if (link.length() == 0) {
    return t + "Посилання ще немає: публічну IP не визначено.\nПідключіть домен (📖 Як підключити) або задайте IP (⚙️ Додатково).";
  }
  t += "🔗 " + link + "\n";
  if (!webDomOn) {
    return t + "⚪ Без HTTPS" + (webIpManual ? String(" · IP вручну") : String("")) + "\nДля посилання із замком підключіть домен: 📖 Як підключити.";
  }
  const String when = webCheckAt ? " · перевірено " + formatEpochLocal((time_t)webCheckAt, "%d.%m %H:%M") : String("");
  if (webHttpsState == 1) {
    t += "🟢 Працює через Cloudflare" + when;
  } else if (webHttpsState == 0) {
    t += "⚪ Ще не перевірено - натисніть 🔄 Перевірити";
  } else {
    t += webHttpsStatusText() + when;
  }
  if (webCfOnly) t += "\n🛡 Лише через Cloudflare";
  return t;
}

String webxText() {
  String t = "🌍 Домен: " + (webDomain.length() > 0 ? "<code>" + htmlEsc(webDomain) + "</code>" + (webDomOn ? String("") : String(" (не використовується)")) : String("не задано")) + "\n";
  t += "🌐 Публічна IP: ";
  if (webIpManual) {
    t += "<code>" + htmlEsc(webIpVal) + "</code> (вручну)";
  } else {
    t += pubIpAuto.length() > 0 ? "<code>" + pubIpAuto + "</code> (автоматично)" : String("ще не визначено");
  }
  t += "\n🛡 Лише через Cloudflare: " + onOff(webCfOnly) + "\n";
  t += "Коли увімкнено, сторінка відповідає лише Cloudflare і вашій домашній мережі.";
  return t;
}

static String kbRow3(const String& a, const String& b, const String& c) {
  return "[\"" + jsonEsc(a) + "\",\"" + jsonEsc(b) + "\",\"" + jsonEsc(c) + "\"]";
}

String menuKeyboard(const String& state) {
  const String back = kbRow("◀️ Назад", "");
  if (state == "set") {
    return "[" + kbRow("🔔 Мої сповіщення", "👥 Групи й канали") + "," + kbRow("📅 Адреса YASNO", "⚡ Напруга і Shelly") + ","
           + kbRow("🖥 Сервер", "🌐 Вебсторінка") + "," + kbRow("🔑 Адмінпанель", "🕐 Час") + "," + back + "]";
  }
  if (state == "dev") {
    return "[" + kbRow("📊 Статистика", "📜 Журнал") + "," + kbRow("🩺 Діагностика", "ℹ️ Про бота") + ","
           + kbRow("⬆️ Оновити прошивку", "🔁 Перезапуск") + "," + back + "]";
  }
  if (state == "ntf") {
    return ownerReplyKb();
  }
  if (state == "grpc") {
    return chatReplyKb(menuChatIdx);
  }
  if (state == "cfm") {
    return "[" + kbRow(cfmYes, "❌ Скасувати") + "]";
  }
  if (state == "srvp") {
    return srvMenuKeyboard(state);
  }
  if (state == "srvh") {
    return srvMenuKeyboard(state);
  }
  if (state == "srvhv" || state == "bk" || state == "bkr" || state == "bkd") {
    return srvMenuKeyboard(state);
  }
  if (state == "time") {
    return "[" + kbRow3("🔄 Авто", "☀️ Літній", "❄️ Зимовий") + "," + back + "]";
  }
  if (state == "yasno") {
    return "[" + kbRow("📍 Змінити адресу", "🔄 Перевірити адресу") + "," + kbRow("🗑 Видалити адресу", "◀️ Назад") + "]";
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
    return kb + back + "]";
  }
  if (state == "volt") {
    return "[" + kbRow("🔔 Тривоги напруги: " + onOff(vAlertOn), "") + ","
           + kbRow("📉 Нижня: " + String(voltLow) + " В", "📈 Верхня: " + String(voltHigh) + " В") + ","
           + kbRow("🟩 Норма від: " + String(voltStableLow) + " В", "🟩 Норма до: " + String(voltStableHigh) + " В") + ","
           + kbRow("⏱ Інтервал: " + String(vIntSec) + " с", "🔁 Підтвердження: " + String(vConfirm)) + ","
           + kbRow("⏸ Пауза: " + String(vPauseMin) + " хв", "◀️ Назад") + "]";
  }
  if (state == "web") {
    return "[" + (webCheckDetail.length() > 0 ? kbRow("📄 Детально", "") + "," : String(""))
           + kbRow("🔄 Перевірити", "📤 Поділитися") + "," + kbRow("🌍 Домен", "📖 Як підключити") + ","
           + kbRow("⚙️ Додатково", "◀️ Назад") + "]";
  }
  if (state == "webx") {
    return "[" + kbRow(webIpManual ? "🔄 IP автоматично" : "✏️ IP вручну", "") + ","
           + kbRow("🛡 Лише через Cloudflare: " + onOff(webCfOnly), "") + ","
           + (webDomain.length() > 0 ? kbRow("🗑 Прибрати домен", "") + "," : String("")) + back + "]";
  }
  if (state == "srv") {
    if (!srvConfigured()) {
      return "[" + kbRow("🌍 Підключити сервер", "◀️ Назад") + "]";
    }
    return "[" + kbRow("🧩 Що на сервері", "⏱ Частота") + "," + kbRow("🔄 Перевірити сервер", "🌍 Змінити адресу") + ","
           + kbRow("💾 Копія налаштувань", "") + "," + kbRow("🗑 Відв'язати", "◀️ Назад") + "]";
  }
  if (state == "adm") {
    if (admHasPass) {
      return "[" + kbRow("🔑 Змінити пароль", "👤 Змінити логін") + "," + back + "]";
    }
    return "[" + kbRow("🔑 Створити пароль", "") + "," + back + "]";
  }
  if (state == "inp") {
    return "[" + kbRow("❌ Скасувати", "") + "]";
  }
  if (state == "stat") {
    return "[" + kbRow("🗑 Очистити статистику", "◀️ Назад") + "]";
  }
  if (state == "log") {
    return "[" + kbRow("🗑 Очистити журнал", "◀️ Назад") + "]";
  }
  return "[" + kbRow("⚙️ Налаштування", "🛠 Пристрій") + "," + kbRow("🧹 Очистити чат", "") + "]";
}

String menuHint(const String& state) {
  if (state == "set") return "⚙️ Налаштування";
  if (state == "dev") return "🛠 Пристрій";
  if (state == "stat") return "🛠 › 📊 Статистика";
  if (state == "log") return "🛠 › 📜 Журнал";
  if (state == "ntf") return "⚙️ › 🔔 Мої сповіщення";
  if (state == "grp") return "⚙️ › 👥 Групи й канали";
  if (state == "grpc") {
    if (menuChatIdx >= 0 && menuChatIdx < MAX_CHATS && chats.e[menuChatIdx].used) return "⚙️ › 👥 › " + chatShortTitle(chats.e[menuChatIdx].title, 28);
    return "⚙️ › 👥 Групи й канали";
  }
  if (state == "volt") return "⚙️ › ⚡ Напруга і Shelly";
  if (state == "yasno") return "⚙️ › 📅 Адреса YASNO";
  if (state == "web") return "⚙️ › 🌐 Вебсторінка";
  if (state == "webx") return "⚙️ › 🌐 › ⚙️ Додатково";
  if (state == "adm") return "⚙️ › 🔑 Адмінпанель";
  if (state == "time") return "⚙️ › 🕐 Час";
  if (state == "srv") return "⚙️ › 🖥 Сервер";
  if (state == "srvp" || state == "srvh" || state == "srvhv" || state == "bk" || state == "bkr" || state == "bkd") return srvMenuHint(state);
  if (state == "cfm") return "Підтвердьте або скасуйте";
  if (state == "inp") return "Введіть значення";
  return "🏠 Головне меню";
}

void menuOpen(const String& state, const String& prompt) {
  menuRefresh(state, prompt);
}

void menuOpenClean(const String& state, const String& prompt) {
  menuState = state;
  menuCurrentUserMsg = 0;
  if (tgSendText(chatId, prompt, "HTML", tgReplyKbHint(menuKeyboard(state), menuHint(state)))) {
    menuLastMsg = (int)bot.last_sent_message_id;
    tgCleanUpTo((int32_t)bot.last_sent_message_id);
  }
}

void menuRefresh(const String& state, const String& prompt) {
  const int prev = menuLastMsg;
  const int tap = menuTapMsg;
  menuTapMsg = 0;
  menuState = state;
  menuCurrentUserMsg = 0;
  if (!tgSendText(chatId, prompt, "HTML", tgReplyKbHint(menuKeyboard(state), menuHint(state)))) {
    return;
  }
  menuLastMsg = (int)bot.last_sent_message_id;
  int ids[2];
  int n = 0;
  if (prev > 0 && prev != menuLastMsg) ids[n++] = prev;
  if (tap > 0) ids[n++] = tap;
  menuDeleteIds(ids, n);
}

String menuTimeText() {
  String t = "Зараз: " + getFormattedTime() + "\n";
  if (autoDst) {
    t += String("Режим: 🔄 автоматично (Europe/Kyiv), зараз ") + (isSummerTimeNow() ? "літній час (UTC+3)" : "зимовий час (UTC+2)");
  } else {
    t += String("Режим: ") + (currentTZ == "EEST-3" ? "☀️ літній час вручну (UTC+3)" : "❄️ зимовий час вручну (UTC+2)");
  }
  return t;
}

static bool chatStoreToggle(int i) {
  ChatEntry& c = chats.e[i];
  const String id = String(c.id);
  if (srvStoreIs(id)) return srvStoreSet("");
  if (c.type != 3 || !srvConfigured()) return true;
  const uint8_t oldFlags = c.flags, oldPad = c.pad[0];
  c.flags = 0;
  c.pad[0] = 0;
  chatsSave();
  if (!chatsSaveOk || !srvStoreSet(id)) {
    c.flags = oldFlags;
    c.pad[0] = oldPad;
    chatsSave();
    return false;
  }
  logEvent(LC_TG, LS_INFO, "Сховище картинок: " + id);
  return true;
}

String menuChatText(int i) {
  ChatEntry& c = chats.e[i];
  String t = chatTypeName(c.type) + " · бот: " + (c.status == 2 ? "адміністратор" : "учасник");
  if (chatApproved(c) && !srvStoreIs(String(c.id))) t += "\n" + chatNotifyText(c);
  if (!chatApproved(c)) {
    t += "\n⏳ <b>Чекає вашого дозволу</b> - сюди нічого не надсилається.";
  } else if (c.type == 3 && !c.canPost) {
    t += "\n⚠️ Бот не має права публікувати в каналі.";
  } else if (c.type != 3 && c.status == 1 && !botCanReadAllGroup) {
    t += "\n⚠️ Бот не бачить звичайних повідомлень: зробіть його адміністратором або вимкніть Group Privacy у @BotFather.";
  }
  if (chatApproved(c) && srvStoreIs(String(c.id))) {
    t += "\n📦 <b>Сховище картинок графіка.</b> Сюди сервер на мить вивантажує картинки й одразу видаляє; сповіщення й графік сюди не надсилаються.";
    if (c.status != 2) t += "\n⚠️ Зробіть бота адміністратором каналу з правом видаляти повідомлення.";
    if (!srvImgOn() && !srvWeekOn()) t += "\nДіє, коли ввімкнено 🖥 Сервер → 🧩 → 🖼 Картинка або 📈 Тиждень.";
  } else if (chatApproved(c) && c.type == 3 && srvConfigured() && c.flags == 0) {
    t += "\n📦 Щоб сервер вивантажував картинки графіка сюди, а не у ваш чат, натисніть «📦 Сховище».";
  }
  if (chatApproved(c) && (c.pad[0] & CHAT_P0_WEEK) != 0 && !srvWeekOn()) {
    t += "\n📈 Тиждень діє, коли ввімкнено 🖥 Сервер › 🧩 › 📈 Тиждень.";
  }
  if (chatApproved(c) && (c.pad[0] & (CHAT_P0_PIN | CHAT_P0_WEEK)) != 0) {
    t += c.type == 3 ? "\n📌 Щоб закріплювати графік чи тиждень, боту потрібне право адміністратора «Редагувати повідомлення»."
                     : "\n📌 Щоб закріплювати графік чи тиждень, боту потрібне право адміністратора «Закріплювати повідомлення».";
  }
  return t;
}

static String inlineBtn(const String& text, const String& data) {
  return "[{\"text\":\"" + jsonEsc(text) + "\",\"callback_data\":\"" + jsonEsc(data) + "\"}]";
}

static const char CHAT_TOGGLE_CODES[] = "nvsalwpkteyq";

static uint8_t chatToggleBit(char code) {
  switch (code) {
    case 'n': return CHAT_F_NOTIFY;
    case 'v': return CHAT_F_VOLT_IN_LIGHT;
    case 'a': return CHAT_F_VALERT;
    case 'p': return CHAT_F_YASNO_PUBLISH;
    case 'e': return CHAT_F_YASNO_EMERGENCY;
    case 'l': return CHAT_F_LIGHT;
    case 'w': return CHAT_F_VOLT;
    case 'q': return CHAT_F_YASNO_QUERY;
  }
  return 0;
}

static String toggleName(char code) {
  switch (code) {
    case 'n': return "🔔 Світло зникло / з'явилося";
    case 'b': return "⚡ Короткі зникнення";
    case 'v': return "⚡ Напруга в повідомленні про світло";
    case 's': return "🗓 Розклад у повідомленні про світло";
    case 't': return "⏰ Попередження за графіком";
    case 'a': return "🚨 Тривоги напруги";
    case 'p': return "📢 Автопублікація графіка";
    case 'k': return "📌 Закріплювати графік";
    case 'e': return "🆘 Екстрені відключення";
    case 'l': return "💬 Відповідати «Світло є?»";
    case 'w': return "💬 Відповідати «Напруга?»";
    case 'q': return "💬 Відповідати «Графік»";
    case 'y': return "📈 Тиждень (картинка)";
  }
  return "";
}

static uint8_t warnNext(uint8_t minutes) {
  return minutes == 0 ? 15 : (minutes == 15 ? 30 : 0);
}

static String toggleLabel(char code, bool on, uint8_t warnMin) {
  return toggleName(code) + ": " + (code == 't' ? (warnMin ? String(warnMin) + " хв" : String("❌")) : onOff(on));
}

static String toggleShort(char code) {
  switch (code) {
    case 'n': return "🔔 Світло";
    case 'b': return "⚡ Короткі";
    case 'v': return "🔌 З напругою";
    case 's': return "🗓 З розкладом";
    case 't': return "⏰ Попередж.";
    case 'a': return "🚨 Тривоги";
    case 'p': return "📢 Публікація";
    case 'k': return "📌 Закріплення";
    case 'e': return "🆘 Екстрені";
    case 'l': return "💬 «Світло є?»";
    case 'w': return "💬 «Напруга?»";
    case 'q': return "💬 «Графік»";
    case 'y': return "📈 Тиждень";
  }
  return "";
}

static String toggleShortLabel(char code, bool on, uint8_t warnMin) {
  return toggleShort(code) + ": " + (code == 't' ? (warnMin ? String(warnMin) + " хв" : String("❌")) : onOff(on));
}

static char toggleCodeFor(const String& text, const char* codes) {
  for (const char* p = codes; *p; p++) {
    if (text.startsWith(toggleShort(*p) + ":")) return *p;
  }
  return 0;
}

static String pairRows(const String* labels, int n) {
  String kb = "[";
  for (int i = 0; i < n; i += 2) {
    kb += kbRow(labels[i], i + 1 < n ? labels[i + 1] : String("")) + ",";
  }
  return kb + kbRow("◀️ Назад", "") + "]";
}

static bool chatToggleOn(const ChatEntry& c, char code) {
  if (code == 's') return (c.pad[0] & 0x01) != 0;
  if (code == 'k') return (c.pad[0] & CHAT_P0_PIN) != 0;
  if (code == 'y') return (c.pad[0] & CHAT_P0_WEEK) != 0;
  return (c.flags & chatToggleBit(code)) != 0;
}

static const char NOTIFY_LIGHT_CODES[] = "nvsbalw";
static const char* const TOGGLE_ROWS[] = { "n", "vs", "ba", "lw", "pk", "te", "yq" };

static String toggleRowsKb(const char* codes, const String* labels) {
  String kb = "";
  for (const char* row : TOGGLE_ROWS) {
    String cell[2];
    int k = 0;
    for (const char* r = row; *r && k < 2; r++) {
      const char* hit = strchr(codes, *r);
      if (hit != nullptr) cell[k++] = labels[hit - codes];
    }
    if (k > 0) kb += kbRow(cell[0], k > 1 ? cell[1] : String("")) + ",";
  }
  return kb;
}

static String notifySectText(const char* codes, const bool* on) {
  int lightOn = 0, lightAll = 0, ysOn = 0, ysAll = 0;
  for (int k = 0; codes[k]; k++) {
    if (strchr(NOTIFY_LIGHT_CODES, codes[k]) != nullptr) {
      lightAll++;
      if (on[k]) lightOn++;
    } else {
      ysAll++;
      if (on[k]) ysOn++;
    }
  }
  return "💡 Світло: " + String(lightOn) + " з " + String(lightAll) + " · 📅 Графік: " + String(ysOn) + " з " + String(ysAll);
}

static String chatNotifyText(const ChatEntry& c) {
  bool on[12];
  int n = 0;
  for (const char* p = CHAT_TOGGLE_CODES; *p && n < 12; p++) on[n++] = *p == 't' ? chatWarnMin(c) != 0 : chatToggleOn(c, *p);
  return notifySectText(CHAT_TOGGLE_CODES, on);
}

String chatInlineKb(int i) {
  ChatEntry& c = chats.e[i];
  const String id = String(c.id);
  if (!chatApproved(c)) {
    return "[[{\"text\":\"✅ Дозволити\",\"callback_data\":\"x:apY:" + id + "\"},{\"text\":\"🚫 Відхилити\",\"callback_data\":\"x:apN:" + id + "\"}]]";
  }
  String kb = "[";
  for (const char* p = CHAT_TOGGLE_CODES; *p; p++) {
    kb += inlineBtn(toggleLabel(*p, chatToggleOn(c, *p), chatWarnMin(c)), "x:ct:" + id + ":" + String(*p)) + ",";
  }
  return kb + inlineBtn("🚪 Вийти з чату", "x:lv:" + id) + "]";
}

static const char OWNER_TOGGLE_CODES[] = "nvsbapkteyq";

static bool* ownerToggleVar(char code) {
  switch (code) {
    case 'n': return &ownerNotifyOn;
    case 'b': return &blipOn;
    case 'v': return &ownerVoltInLightOn;
    case 's': return &ownerYasnoEventOn;
    case 'a': return &ownerVoltAlertOn;
    case 'p': return &ownerYasnoPublishOn;
    case 'k': return &ownerYasnoPinOn;
    case 'e': return &ownerYasnoEmergencyOn;
    case 'q': return &ownerYasnoQueryOn;
    case 'y': return &ownerWeekOn;
  }
  return nullptr;
}

static const char* ownerToggleKey(char code) {
  switch (code) {
    case 'n': return "oNotify";
    case 'b': return "blipOn";
    case 'v': return "oVInOn";
    case 's': return "oYsEvt";
    case 'a': return "oVAlert";
    case 'p': return "oYsPub";
    case 'k': return "oYsPin";
    case 'e': return "oYsEm";
    case 'q': return "oYsQry";
    case 'y': return "oWeek";
  }
  return "";
}

static bool ownerToggleIsOn(char code) {
  if (code == 't') return ownerYasnoWarnMin != 0;
  const bool* v = ownerToggleVar(code);
  return v != nullptr && *v;
}

static int ownerOnCount() {
  int n = 0;
  for (const char* p = OWNER_TOGGLE_CODES; *p; p++) {
    const bool* v = ownerToggleVar(*p);
    if (*p == 't' ? ownerYasnoWarnMin != 0 : (v != nullptr && *v)) n++;
  }
  return n;
}

String ownerNotifyText() {
  bool on[12];
  int n = 0;
  for (const char* p = OWNER_TOGGLE_CODES; *p && n < 12; p++) on[n++] = ownerToggleIsOn(*p);
  String t = notifySectText(OWNER_TOGGLE_CODES, on);
  if (!vAlertOn) t += "\n⚠️ Тривоги напруги вимкнено в ⚡ Напруга і Shelly.";
  if (ownerWeekOn && !srvWeekOn()) t += "\n📈 Тиждень діє, коли ввімкнено 🖥 Сервер › 🧩 › 📈 Тиждень.";
  return t;
}

String menuMainCard() {
  const bool on = checkPowerStatus();
  const String dur = on ? currentOnDurationText() : currentOffDurationText();
  String t = on ? "🟢 Світло є" : "🔴 Світла немає";
  if (dur.length() > 0) t += " <b>" + htmlEsc(dur) + "</b>";
  if (on && shellyConfigured() && vmLastVMs != 0 && millis() - vmLastVMs < MENU_VOLT_FRESH_MS) t += " · ⚡ " + voltageText(vmLastV);
  const String sched = yasnoPowerEventText(on);
  if (sched.length() > 0) t += "\n" + sched;
  return t;
}

String menuSetCard() {
  const String tz = autoDst ? String("авто") : (currentTZ == "EEST-3" ? String("літній") : String("зимовий"));
  return "🔔 Сповіщень: " + String(ownerOnCount()) + " з " + String((int)strlen(OWNER_TOGGLE_CODES)) + " · 👥 Груп: " + String(chatActiveCount()) +
         "\n🖥 Сервер: " + srvShortState() + " · 🕐 Час: " + tz;
}

String menuDevCard() {
  String t = "ℹ️ V" LW_VERSION " · працює " + formatShortDuration(uptimeSec());
  t += "\n📶 Wi-Fi: " + (WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : String("немає")) +
       " · 💾 Вільно: " + String(ESP.getFreeHeap() / 1024) + " КБ";
  if (safeMode) t += "\n🛡 Безпечний режим";
  return t;
}

String ownerNotifyKb() {
  String kb = "[";
  for (const char* p = OWNER_TOGGLE_CODES; *p; p++) {
    const bool* v = ownerToggleVar(*p);
    kb += String(p == OWNER_TOGGLE_CODES ? "" : ",") + inlineBtn(toggleLabel(*p, v != nullptr && *v, ownerYasnoWarnMin), "x:ot:" + String(*p));
  }
  return kb + "]";
}

String ownerReplyKb() {
  String labels[12];
  int n = 0;
  for (const char* p = OWNER_TOGGLE_CODES; *p && n < 12; p++) labels[n++] = toggleShortLabel(*p, ownerToggleIsOn(*p), ownerYasnoWarnMin);
  return "[" + toggleRowsKb(OWNER_TOGGLE_CODES, labels) + kbRow("◀️ Назад", "") + "]";
}

String chatReplyKb(int i) {
  if (i < 0 || i >= MAX_CHATS || !chats.e[i].used || chats.e[i].status == 0) return "[" + kbRow("◀️ Назад", "") + "]";
  ChatEntry& c = chats.e[i];
  if (!chatApproved(c)) {
    const String pending[2] = { "✅ Дозволити", "🚫 Відхилити" };
    return pairRows(pending, 2);
  }
  if (srvStoreIs(String(c.id))) {
    const String store[2] = { "📦 Сховище: ✅", "🚪 Вийти з чату" };
    return pairRows(store, 2);
  }
  String labels[12];
  int n = 0;
  for (const char* p = CHAT_TOGGLE_CODES; *p && n < 12; p++) labels[n++] = toggleShortLabel(*p, chatToggleOn(c, *p), chatWarnMin(c));
  const String kb = "[" + toggleRowsKb(CHAT_TOGGLE_CODES, labels);
  if (c.type == 3 && srvConfigured()) return kb + kbRow("📦 Сховище: ❌", "🚪 Вийти з чату") + "," + kbRow("◀️ Назад", "") + "]";
  return kb + kbRow("🚪 Вийти з чату", "◀️ Назад") + "]";
}

static bool ownerToggle(char code) {
  bool* v = ownerToggleVar(code);
  if (v == nullptr && code != 't') return true;
  bool saved = false;
  if (code == 't') {
    const uint8_t next = warnNext(ownerYasnoWarnMin);
    saved = prefPutIntVerified("oYsWarn", next);
    if (saved) ownerYasnoWarnMin = next;
  } else {
    saved = toggleNotificationPref(*v, ownerToggleKey(code));
  }
  if (saved && code == 'p' && ownerYasnoPublishOn) preferences.putUInt("ypOwnerSig", 0);
  if (saved && code == 'e' && ownerYasnoEmergencyOn) preferences.putBool("yeOwnerSt", false);
  if (saved && code == 'k') yasnoPinChanged(-1, ownerYasnoPinOn);
  return saved;
}

void startResetStatsConfirm(const String& chat, const String& fromId) {
  resetPending = true;
  resetPendingChat = chat;
  resetPendingUser = fromId;
  resetPendingCode = String((uint32_t)esp_random(), HEX);
  resetPendingMs = millis();
  String ask = "🗑 <b>Очистити статистику?</b>\n";
  ask += "Буде видалено історію відключень, лічильники, накопичену тривалість і історію напруги.\n";
  ask += "Налаштування не зміняться.";
  String keyboard = confirmKeyboard("✅ Так, очистити", "rst_yes_" + resetPendingCode, "❌ Скасувати", "rst_no_" + resetPendingCode);
  tgSendText(chat, ask, "HTML", tgInlineKb(keyboard));
}

static void webCheckRun() {
  menuSend("⏳ Перевіряю, це може зайняти до 25 с…", "");
  webCheckDetail = webCheckText();
  menuOpen("web", webCheckBrief());
}

static void webShareSend() {
  const String link = webPublicLink();
  if (link.length() == 0) {
    menuSend("Посилання ще немає: підключіть домен або задайте IP (⚙️ Додатково).", "");
    return;
  }
  String kb = "[[{\"text\":\"📋 Скопіювати\",\"copy_text\":{\"text\":\"" + jsonEsc(link) + "\"}}]";
  for (int i = 0; i < MAX_CHATS; i++) {
    ChatEntry& c = chats.e[i];
    if (!c.used || c.status == 0 || !c.canPost || !chatApproved(c)) continue;
    kb += "," + inlineBtn("📤 " + chatShortTitle(c.title, 28), "x:wsh:" + String(c.id));
  }
  tgSendText(chatId, "🔗 " + link + "\nСкопіюйте або надішліть у групу:", "", tgInlineKb(kb + "]"));
}

void webMenuShowClean(const String& note) {
  menuOpenClean("web", (note.length() > 0 ? note + "\n\n" : "") + webMenuText());
}

void webMenuShow(const String& note) {
  menuOpen("web", (note.length() > 0 ? note + "\n\n" : "") + webMenuText());
}


static String menuGroupsText() {
  int n = chatActiveCount();
  return n == 0 ? String("Чатів поки немає. Додайте бота в групу або канал - він з'явиться тут, а вам прийде запит на дозвіл.")
                : "Чатів: " + String(n) + ". Оберіть чат, щоб налаштувати його сповіщення.";
}

static void menuOpenGroups(bool clean) {
  if (clean) menuOpenClean("grp", menuGroupsText());
  else menuOpen("grp", menuGroupsText());
}

static bool menuChatOk(int i) {
  return i >= 0 && i < MAX_CHATS && chats.e[i].used && chats.e[i].status != 0;
}

static String menuStatsText() {
  const String t = buildStatsMessage();
  const int cut = t.indexOf("\n\n");
  return cut > 0 && cut < 80 ? t.substring(cut + 2) : t;
}

static void menuBackTo(const String& st, const String& note) {
  const String pre = note.length() > 0 ? note + "\n\n" : String("");
  if (st == "dev") menuOpenClean("dev", note.length() > 0 ? note + "\n\n" + menuDevCard() : menuDevCard());
  else if (st == "grpc" && menuChatOk(menuChatIdx)) menuOpenClean("grpc", pre + menuChatText(menuChatIdx));
  else if (st == "grp" || st == "grpc") menuOpenClean("grp", pre + menuGroupsText());
  else if (st == "srv") menuOpenClean("srv", pre + srvMenuText());
  else if (st == "bk") menuOpenClean("bk", pre + bkCardText());
  else if (st == "bkr" || st == "bkd") menuOpenClean(st, pre + bkListText(st));
  else if (st == "yasno") menuOpenClean("yasno", pre + yasnoMenuText());
  else if (st == "stat") menuOpenClean("stat", pre + menuStatsText());
  else if (st == "log") menuOpenClean("log", pre + logIssuesText(15));
  else if (st == "webx") menuOpenClean("webx", pre + webxText());
  else menuOpenClean("set", note.length() > 0 ? note + "\n\n" + menuSetCard() : menuSetCard());
}

void menuConfirm(const String& action, const String& arg, const String& back, const String& question, const String& yes) {
  cfmAction = action;
  cfmArg = arg;
  cfmBack = back;
  cfmYes = yes;
  cfmMs = millis();
  menuInput = "";
  menuInputBack = "";
  menuOpen("cfm", question + "\nПідтвердження діє 2 хв.");
}

static String chatApproveRun(int i, bool allow) {
  const String title = htmlEsc(chatShortTitle(chats.e[i].title, 40));
  if (allow) {
    bool ok = chatApprove(i);
    if (ok) chatSendWelcome(i);
    return ok ? "✅ «" + title + "» дозволено. Налаштування чату: ⚙️ Налаштування → 👥 Групи й канали."
              : String("❌ Не вдалося зберегти дозвіл. Спробуйте ще раз.");
  }
  if (chatApproved(chats.e[i])) {
    return "«" + title + "» уже дозволено. Вийти з чату можна в його налаштуваннях.";
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
  return left ? "🚫 «" + title + "» відхилено, Light Watcher вийшов із чату."
              : String("❌ Не вдалося вийти з чату. Спробуйте ще раз.");
}

static String chatLeaveRun(int i, bool* ok) {
  String id = String(chats.e[i].id);
  String title = chatShortTitle(chats.e[i].title, 40);
  if (chats.e[i].canPost) {
    tgSendText(id, TEXT_BOT_LEFT, "HTML", "");
  }
  String resp = telegramPostRaw("leaveChat", "{\"chat_id\":\"" + id + "\"}");
  *ok = resp.indexOf("\"ok\":true") >= 0;
  if (*ok && srvStoreIs(id)) srvStoreSet("");
  if (*ok) {
    chats.e[i].status = 0;
    chatsSave();
    if (menuChatIdx == i) {
      menuChatIdx = -1;
    }
  }
  return *ok ? "✅ Light Watcher вийшов з «" + htmlEsc(title) + "»." : String("❌ Не вдалося вийти з чату. Спробуйте ще раз.");
}

static String logClearRun() {
  const bool saved = logClear();
  logEvent(LC_SYS, LS_INFO, "Журнал подій очищено власником");
  return saved ? String("✅ <b>Журнал подій очищено</b>")
               : String("⚠️ <b>Журнал очищено лише до перезапуску</b>\nЗапис у пам'ять пристрою не підтвердився, повтор - автоматично.");
}

static void menuConfirmEnd(bool yes) {
  const String a = cfmAction;
  const String arg = cfmArg;
  const String back = cfmBack;
  const bool fresh = a.length() > 0 && millis() - cfmMs <= MENU_CFM_MS;
  cfmAction = "";
  cfmArg = "";
  cfmYes = "";
  const bool srvAction = a == "sd" || a.startsWith("bk");
  if (srvAction) {
    if (!yes || !fresh) srvConfirmRun(a, arg, false);
  }
  if (!yes) {
    menuBackTo(back, "Скасовано. Нічого не змінено.");
    return;
  }
  if (!fresh) {
    menuBackTo(back, "⌛ Підтвердження застаріло. Спробуйте ще раз.");
    return;
  }
  if (a == "rst") {
    menuState = "main";
    tgSendText(chatId, "🔁 Перезапуск...", "", tgReplyKbHint(menuKeyboard("main"), menuHint("main")));
    prepareForRestart("кнопка «🔁 Перезапуск» у меню");
    delay(1000);
    ESP.restart();
    return;
  }
  if (a == "stc") {
    bool allOk = false;
    const String result = statsResetRun(&allOk);
    menuBackTo("stat", result);
    if (allOk) {
      Serial.println("[RESET] all stores verified; restarting to reload the cleared state");
      delay(1200);
      prepareForRestart("після успішного очищення статистики");
      delay(350);
      ESP.restart();
    }
    return;
  }
  if (a == "lgc") {
    menuBackTo("log", logClearRun());
    return;
  }
  if (a == "lv") {
    const int i = chatFind(arg);
    if (!menuChatOk(i)) {
      menuBackTo("grp", "Бота в цьому чаті вже немає.");
      return;
    }
    bool ok = false;
    const String result = chatLeaveRun(i, &ok);
    menuBackTo(ok ? "grp" : "grpc", result);
    return;
  }
  if (a == "ysd") {
    preferences.remove("ysCity");
    preferences.remove("ysStreet");
    preferences.remove("ysHouse");
    preferences.remove("ysGroup");
    yasnoClearScheduleCache();
    yasnoCity = yasnoStreet = yasnoHouse = yasnoGroup = "";
    menuBackTo("yasno", "🗑 Адресу видалено.");
    return;
  }
  if (a == "wdm") {
    webDomOn = false;
    webDomain = "";
    webCfOnly = false;
    webCfState = 0;
    webHttpsState = 0;
    webCheckAt = 0;
    preferences.remove("webDomain");
    preferences.putBool("webDomOn", webDomOn);
    preferences.putBool("webCfOnly", webCfOnly);
    if (!webIpManual) publicIpRefresh();
    menuBackTo("webx", "🗑 Домен прибрано, посилання - за IP.");
    return;
  }
  if (srvAction) {
    const String note = srvConfirmRun(a, arg, true);
    if (note.length() > 0) menuBackTo("srv", note);
    return;
  }
  menuBackTo(back, "");
}

static bool menuButtonMain(const String& text) {
  if (text == "⚙️ Налаштування") { menuOpen("set", menuSetCard()); return true; }
  if (text == "🛠 Пристрій") { menuOpen("dev", menuDevCard()); return true; }
  if (text == "🧹 Очистити чат") { menuClearChat(); return true; }
  if (text == "◀️ Назад") {
    if (menuState == "webx") webMenuShowClean("");
    else if (menuState == "cfm") menuConfirmEnd(false);
    else if (menuState == "grpc") menuOpenGroups(true);
    else if (menuState == "srvp" || menuState == "srvh" || menuState == "bk") menuOpenClean("srv", srvMenuText());
    else if (menuState == "bkr" || menuState == "bkd") menuOpenClean("bk", bkCardText());
    else if (menuState == "srvhv") menuRefresh("srvh", srvHbText());
    else if (menuState == "stat" || menuState == "log") menuOpenClean("dev", menuDevCard());
    else if (menuState == "time" || menuState == "yasno" || menuState == "grp" || menuState == "volt" ||
             menuState == "web" || menuState == "adm" || menuState == "srv" || menuState == "ntf") menuOpenClean("set", menuSetCard());
    else menuGoMain();
    return true;
  }
  return false;
}

static bool menuButtonDevice(const String& text) {
  if (text == "📊 Статистика") { menuOpen("stat", menuStatsText()); return true; }
  if (text == "🩺 Діагностика") { menuSend(buildDiagMessage(), "HTML"); return true; }
  if (text == "📜 Журнал") { menuOpen("log", logIssuesText(15)); return true; }
  if (text == "ℹ️ Про бота") { menuSend(buildInfoMessage(), "HTML"); return true; }
  if (text == "⬆️ Оновити прошивку") { otaStart(); return true; }
  if (text == "🔁 Перезапуск") {
    menuConfirm("rst", "", "dev", "🔁 <b>Перезапустити пристрій?</b>", "✅ Так, перезапустити");
    return true;
  }
  if (text == "🗑 Очистити статистику") {
    menuConfirm("stc", "", "stat", "🗑 <b>Очистити статистику?</b>\nБуде видалено історію відключень, лічильники, накопичену тривалість і історію напруги.\n"
                "Налаштування не зміняться.", "✅ Так, очистити");
    return true;
  }
  if (text == "🗑 Очистити журнал") {
    menuConfirm("lgc", "", "log", "🗑 <b>Очистити журнал подій?</b>\n"
                "Буде видалено всі записи: попередження, помилки й інформаційні події (і в боті, і в адмінпанелі).\n"
                "Статистика й налаштування не зміняться.", "✅ Так, очистити");
    return true;
  }
  return false;
}

static bool menuButtonSettings(const String& text) {
  if (text == "🔔 Мої сповіщення") { menuOpen("ntf", ownerNotifyText()); return true; }
  if (text == "👥 Групи й канали") { menuOpenGroups(false); return true; }
  if (text == "⚡ Напруга і Shelly") { menuOpen("volt", voltMenuText()); return true; }
  if (text == "📅 Адреса YASNO") { menuOpen("yasno", yasnoMenuText()); return true; }
  if (text == "🌐 Вебсторінка") {
    if (!webDomOn && !webIpManual && (pubIpAuto.length() == 0 || millis() - pubIpLastTry > 1800000UL)) {
      publicIpRefresh();
    }
    webMenuShow("");
    return true;
  }
  if (text == "🔑 Адмінпанель") { menuOpen("adm", admStatusText()); return true; }
  if (text == "🕐 Час") { menuOpen("time", menuTimeText()); return true; }
  if (text == "🖥 Сервер") { menuOpen("srv", srvMenuText()); return true; }
  return false;
}

static bool menuButtonScreens(const String& text) {
  if (menuState == "cfm") {
    if (text == "❌ Скасувати") { menuConfirmEnd(false); return true; }
    if (cfmYes.length() > 0 && text == cfmYes) { menuConfirmEnd(true); return true; }
  }

  if (menuState == "ntf") {
    const char code = toggleCodeFor(text, OWNER_TOGGLE_CODES);
    if (code) {
      const bool saved = ownerToggle(code);
      menuRefresh("ntf", saved ? ownerNotifyText() : String("❌ Не вдалося зберегти, залишено попереднє значення."));
      return true;
    }
  }

  if (menuState == "grpc") {
    const int i = menuChatIdx;
    if (!menuChatOk(i)) {
      menuOpenClean("grp", "Цього чату вже немає в списку.\n\n" + menuGroupsText());
      return true;
    }
    ChatEntry& c = chats.e[i];
    if (!chatApproved(c)) {
      if (text == "✅ Дозволити" || text == "🚫 Відхилити") {
        const String result = chatApproveRun(i, text == "✅ Дозволити");
        if (menuChatOk(i) && menuChatIdx == i) menuOpenClean("grpc", result + "\n\n" + menuChatText(i));
        else menuOpenClean("grp", result + "\n\n" + menuGroupsText());
        return true;
      }
    } else {
      if (text.startsWith("📦 Сховище:")) {
        menuRefresh("grpc", (chatStoreToggle(i) ? String("") : String("❌ Не вдалося зберегти, залишено попереднє значення.\n\n")) + menuChatText(i));
        return true;
      }
      const char code = toggleCodeFor(text, CHAT_TOGGLE_CODES);
      if (code && srvStoreIs(String(c.id))) {
        menuRefresh("grpc", menuChatText(i));
        return true;
      }
      if (code) {
        const bool saved = chatToggle(i, code);
        menuRefresh("grpc", (saved ? String("") : String("❌ Не вдалося зберегти, залишено попереднє значення.\n\n")) + menuChatText(i));
        return true;
      }
      if (text == "🚪 Вийти з чату") {
        menuConfirm("lv", String(c.id), "grpc", "🚪 <b>Вийти з «" + htmlEsc(chatShortTitle(c.title, 40)) + "»?</b>\n"
                    "У чат прийде прощальне повідомлення, сповіщення туди більше не надходитимуть.", "✅ Так, вийти");
        return true;
      }
    }
  }

  if (srvMenuButton(text)) return true;

  if (menuState == "time") {
    if (text == "🔄 Авто") { if (!autoDst) timeAutoOn(); menuRefresh("time", menuTimeText()); return true; }
    if (text == "☀️ Літній") { timeSetSummer(); menuRefresh("time", menuTimeText()); return true; }
    if (text == "❄️ Зимовий") { timeSetWinter(); menuRefresh("time", menuTimeText()); return true; }
  }

  if (menuState == "yasno") {
    if (text == "📍 Змінити адресу") {
      yasnoPendingCity = "";
      yasnoPendingStreet = "";
      yasnoPendingStreetId = -1;
      menuInputStart("yasno_city", "yasno", "🏙 Введіть місто. Поки підтримується Київ (YASNO / ДТЕК Київські електромережі).\nНаприклад: Київ");
      return true;
    }
    if (text == "🔄 Перевірити адресу") {
      if (yasnoStreet.length() == 0 || yasnoHouse.length() == 0) {
        menuOpen("yasno", "Спочатку задайте адресу.\n\n" + yasnoMenuText());
        return true;
      }
      String error;
      if (!yasnoStartTask(3, "", error)) {
        menuOpen("yasno", "❌ " + htmlEsc(error) + "\n\n" + yasnoMenuText());
        return true;
      }
      menuInputStart("yasno_wait", "yasno", "⏳ Перевіряю адресу в YASNO. Моніторинг світла при цьому працює.");
      return true;
    }
    if (text == "🗑 Видалити адресу") {
      menuConfirm("ysd", "", "yasno", "🗑 <b>Видалити адресу YASNO?</b>\nГрафік, попередження й публікація графіка не працюватимуть, доки ви не задасте адресу знову.",
                  "✅ Так, видалити");
      return true;
    }
  }

  if (menuState == "grp" && text.length() > 2 && isDigit(text[0])) {
    int dot = text.indexOf('.');
    int i = (dot > 0) ? chatByListNumber(text.substring(0, dot).toInt()) : -1;
    if (i >= 0) {
      chatRefreshAccess(i);
      if (chats.e[i].status > 0) {
        menuChatIdx = i;
        menuOpen("grpc", menuChatText(i));
      } else {
        menuOpen("grp", "Бота вже видалено з цього чату.");
      }
      return true;
    }
  }

  if (menuState == "adm") {
    if (text == "🔑 Створити пароль" || text == "🔑 Змінити пароль") {
      if (admUser.length() == 0) {
        menuLoginThenPass = true;
        menuInputStart("login", "adm", "👤 Введіть логін адміністратора (3–32 символи: латинські літери, цифри, крапка, _ або -).");
      } else {
        menuLoginThenPass = false;
        menuInputStart("pass1", "adm", "🔑 Введіть новий пароль (8–64 символи, без пробілів на початку й у кінці).\nБот одразу видалить повідомлення з паролем.");
      }
      return true;
    }
    if (text == "👤 Змінити логін") {
      menuLoginThenPass = false;
      menuInputStart("login", "adm", "👤 Введіть новий логін (3–32 символи: латинські літери, цифри, крапка, _ або -). Пароль не зміниться.");
      return true;
    }
  }

  if (menuState == "volt") {
    if (text.startsWith("🔔 Тривоги напруги")) {
      vAlertOn = !vAlertOn;
      preferences.putBool("vAlert", vAlertOn);
      menuRefresh("volt", voltMenuText());
      return true;
    }
    if (text.startsWith("📉 Нижня")) {
      menuInputStart("vlo", "volt", "📉 Нижня аварійна межа зараз " + String(voltLow) + " В. Введіть 150–240, напр.: 195");
      return true;
    }
    if (text.startsWith("📈 Верхня")) {
      menuInputStart("vhi", "volt", "📈 Верхня аварійна межа зараз " + String(voltHigh) + " В. Введіть 220–280, напр.: 255");
      return true;
    }
    if (text.startsWith("🟩 Норма від")) {
      menuInputStart("vsl", "volt", "🟩 Норма від " + String(voltStableLow) + " В: з цього значення напруга вважається нормальною. Введіть 150–279.");
      return true;
    }
    if (text.startsWith("🟩 Норма до")) {
      menuInputStart("vsh", "volt", "🟩 Норма до " + String(voltStableHigh) + " В: до цього значення напруга вважається нормальною. Введіть 151–280.");
      return true;
    }
    if (text.startsWith("⏱ Інтервал")) {
      menuInputStart("vint", "volt", "⏱ Інтервал перевірки зараз " + String(vIntSec) + " с. Введіть 10–600, напр.: 30");
      return true;
    }
    if (text.startsWith("🔁 Підтвердження")) {
      menuInputStart("vcnf", "volt", "🔁 Скільки вимірювань поспіль поза межами потрібно для тривоги (зараз " + String(vConfirm) + "). Введіть 1–10: 1 - одразу, більше - менше хибних тривог.");
      return true;
    }
    if (text.startsWith("⏸ Пауза")) {
      menuInputStart("vpau", "volt", "⏸ Мінімальна пауза між тривогами (зараз " + String(vPauseMin) + " хв). Введіть 0–120, 0 - без паузи.");
      return true;
    }
  }

  if (menuState == "web") {
    if (text == "🔄 Перевірити") { webCheckRun(); return true; }
    if (text == "📄 Детально") {
      if (webCheckDetail.length() == 0) {
        menuSend("Результат застарів. Натисніть «🔄 Перевірити» ще раз.", "");
      } else {
        menuSend(webCheckDetail, "HTML");
        webCheckDetail = "";
      }
      return true;
    }
    if (text == "📤 Поділитися") { webShareSend(); return true; }
    if (text == "🌍 Домен") {
      menuInputStart("domain", "web", "🌍 Надішліть домен, підключений до Cloudflare, без https:// і порту. Напр.: light.example.com"
                     + (webDomain.length() > 0 ? "\nЗараз: " + webDomain : String("")));
      return true;
    }
    if (text == "📖 Як підключити") { guideShow(1, 0); return true; }
    if (text == "⚙️ Додатково") { menuOpen("webx", webxText()); return true; }
  }

  if (menuState == "srv") {
    if (text == "🌍 Підключити сервер" || text == "🌍 Змінити адресу") {
      menuInputStart("srv_url", "srv", "🌍 Надішліть адресу сервера. Приклади: 203.0.113.10 · 203.0.113.10:8000 · http://lw.example.com"
                     "\nПорт за замовчуванням - " + String(SRV_PORT_DEF) + ". Адреса зміниться лише після успішної перевірки.");
      return true;
    }
    if (text == "🔄 Перевірити сервер") {
      if (srvConfigured()) srvCheckStart(preferences.getString("srvUrl", ""));
      else menuOpen("srv", srvMenuText());
      return true;
    }
    if (text == "🧩 Що на сервері") { srvMenuParts(); return true; }
    if (text == "⏱ Частота") { srvMenuHb(); return true; }
    if (text == "🗑 Відв'язати") { srvMenuDetach(); return true; }
    if (text == "💾 Копія налаштувань" || text == "♻️ Відновити налаштування") { srvMenuBackup(); return true; }
  }

  if (menuState == "webx") {
    if (text == "✏️ IP вручну") {
      menuInputStart("ip", "webx", "✏️ Надішліть публічну IP-адресу вашого підключення, напр.: 93.184.216.34");
      return true;
    }
    if (text == "🔄 IP автоматично") {
      webIpManual = false;
      preferences.putBool("webIpMan", webIpManual);
      publicIpRefresh();
      menuOpen("webx", "🔄 IP визначається автоматично.\n\n" + webxText());
      return true;
    }
    if (text.startsWith("🛡 Лише через Cloudflare")) {
      if (!webCfOnly && (!webDomOn || webCfState != 1)) {
        menuOpen("webx", "⚠️ Спершу підключіть домен і натисніть «🔄 Перевірити»: Cloudflare має бути 🟢.\n\n" + webxText());
        return true;
      }
      webCfOnly = !webCfOnly;
      preferences.putBool("webCfOnly", webCfOnly);
      menuRefresh("webx", webxText());
      return true;
    }
    if (text == "🗑 Прибрати домен") {
      menuConfirm("wdm", "", "webx", "🗑 <b>Прибрати домен?</b>\nПосилання на сторінку знову буде за IP, «🛡 Лише через Cloudflare» вимкнеться.", "✅ Так, прибрати");
      return true;
    }
  }
  return false;
}

bool handleMenuButton(const String& text) {
  return menuButtonMain(text) || menuButtonDevice(text) || menuButtonSettings(text) || menuButtonScreens(text);
}

bool menuTapRun(int messageId, const String& text) {
  menuTapMsg = messageId;
  const bool handled = handleMenuButton(text);
  menuTapMsg = 0;
  return handled;
}

static bool logClearCallback(const String& data, int messageId) {
  if (data == "x:lgc") {
    char nonce[9];
    snprintf(nonce, sizeof(nonce), "%08lx", (unsigned long)esp_random());
    menuLogNonce = nonce;
    menuLogMs = millis();
    tgEditText(chatId, messageId, "🗑 <b>Очистити журнал подій?</b>\n"
               "Буде видалено всі записи: попередження, помилки й інформаційні події (і в боті, і в адмінпанелі).\n"
               "Статистика й налаштування не зміняться.", "HTML",
               tgInlineKb(confirmKeyboard("✅ Так, очистити", "x:lgY:" + menuLogNonce, "❌ Скасувати", "x:lgN")));
    return true;
  }
  if (data.startsWith("x:lgY")) {
    const bool fresh = menuLogNonce.length() > 0 && data == "x:lgY:" + menuLogNonce && millis() - menuLogMs <= 120000UL;
    menuLogNonce = "";
    if (!fresh) {
      tgEditText(chatId, messageId, "⌛ Підтвердження застаріло. Відкрийте «📜 Журнал» ще раз.", "", tgInlineKb("[]"));
      return true;
    }
    tgEditText(chatId, messageId, logClearRun(), "HTML", tgInlineKb("[]"));
    return true;
  }
  if (data == "x:lgN") {
    menuLogNonce = "";
    tgEditText(chatId, messageId, "Очищення журналу скасовано.", "", tgInlineKb("[]"));
    return true;
  }
  return false;
}

static bool chatToggle(int i, char code) {
  ChatEntry& c = chats.e[i];
  if (code == 't') {
    const uint8_t oldPad = c.pad[0];
    const uint8_t next = warnNext(chatWarnMin(c));
    c.pad[0] = (uint8_t)((c.pad[0] & ~CHAT_P0_WARN_MASK) | (next == 30 ? CHAT_P0_WARN30 : (next == 15 ? CHAT_P0_WARN15 : 0)));
    chatsSave();
    if (chatsSaveOk) return true;
    c.pad[0] = oldPad;
    chatsSave();
    return false;
  }
  if (code == 's' || code == 'k' || code == 'y') {
    const uint8_t mask = code == 's' ? 0x01 : (code == 'k' ? CHAT_P0_PIN : CHAT_P0_WEEK);
    c.pad[0] ^= mask;
    chatsSave();
    if (!chatsSaveOk) {
      c.pad[0] ^= mask;
      chatsSave();
      return false;
    }
    if (code == 'k') yasnoPinChanged(i, (c.pad[0] & CHAT_P0_PIN) != 0);
    return true;
  }
  const uint8_t bit = chatToggleBit(code);
  if (bit == 0) return false;
  c.flags ^= bit;
  chatsSave();
  if (!chatsSaveOk) {
    c.flags ^= bit;
    chatsSave();
    return false;
  }
  if (bit == CHAT_F_YASNO_PUBLISH || bit == CHAT_F_YASNO_EMERGENCY) {
    if (bit == CHAT_F_YASNO_PUBLISH && (c.flags & CHAT_F_YASNO_PUBLISH)) {
      String sigKey = "yp" + String(i) + "sig";
      preferences.putUInt(sigKey.c_str(), 0);
    }
    if (bit == CHAT_F_YASNO_EMERGENCY && (c.flags & CHAT_F_YASNO_EMERGENCY)) {
      String emKey = "ye" + String(i) + "st";
      preferences.putBool(emKey.c_str(), false);
    }
    yasnoPublishCursor = (uint8_t)i;
    yasnoPublishLastMs = 0;
  }
  return true;
}

static bool menuInlineCallback(const String& data, int messageId) {
  if (data.startsWith("x:ot:") && data.length() == 6) {
    const char code = data[5];
    if (ownerToggleVar(code) == nullptr && code != 't') return true;
    const bool saved = ownerToggle(code);
    tgEditText(chatId, messageId, (saved ? String("") : String("❌ Не вдалося зберегти, залишено попереднє значення.\n\n")) + ownerNotifyText(),
               "HTML", tgInlineKb(ownerNotifyKb()));
    return true;
  }
  if (data.startsWith("x:ct:")) {
    const int colon = data.lastIndexOf(':');
    const int i = colon > 5 ? chatFind(data.substring(5, colon)) : -1;
    if (i < 0 || !chats.e[i].used || chats.e[i].status == 0) {
      tgEditText(chatId, messageId, "Цього чату вже немає в списку.", "", tgInlineKb("[]"));
      return true;
    }
    bool saved = true;
    if (chatApproved(chats.e[i]) && colon + 2 == (int)data.length()) saved = chatToggle(i, data[colon + 1]);
    tgEditText(chatId, messageId, (saved ? String("") : String("❌ Не вдалося зберегти, залишено попереднє значення.\n\n")) + menuChatText(i),
               "HTML", tgInlineKb(chatInlineKb(i)));
    return true;
  }
  if (data.startsWith("x:lv:")) {
    const int i = chatFind(data.substring(5));
    if (i < 0 || !chats.e[i].used || chats.e[i].status == 0) {
      tgEditText(chatId, messageId, "Бота в цьому чаті вже немає.", "", tgInlineKb("[]"));
      return true;
    }
    tgEditText(chatId, messageId, "🚪 <b>Вийти з «" + htmlEsc(chatShortTitle(chats.e[i].title, 40)) + "»?</b>\n"
               "У чат прийде прощальне повідомлення, сповіщення туди більше не надходитимуть.", "HTML",
               tgInlineKb(confirmKeyboard("✅ Так, вийти", "x:lvY:" + String(chats.e[i].id), "❌ Скасувати", "x:lvN")));
    return true;
  }
  if (data == "x:stc") {
    startResetStatsConfirm(chatId, bot.messages[0].from_id);
    return true;
  }
  if (data == "x:wdet") {
    if (webCheckDetail.length() == 0) {
      menuSend("Результат застарів. Натисніть «🔄 Перевірити» ще раз.", "");
    } else {
      menuSend(webCheckDetail, "HTML");
      webCheckDetail = "";
    }
    return true;
  }
  if (data.startsWith("x:wsh:")) {
    const int i = chatFind(data.substring(6));
    const String link = webPublicLink();
    if (i < 0 || !chats.e[i].used || chats.e[i].status == 0 || !chats.e[i].canPost || !chatApproved(chats.e[i]) || link.length() == 0) {
      menuSend("❌ Цей чат недоступний або посилання ще немає.", "");
      return true;
    }
    const String title = htmlEsc(chatShortTitle(chats.e[i].title, 40));
    const bool ok = tgSendText(String(chats.e[i].id), "💡 <b>Light Watcher</b>\nСтан світла, статистика й графіки:\n" + link, "HTML", "");
    menuSend(ok ? "✅ Посилання надіслано в «" + title + "»." : "❌ Не вдалося надіслати в «" + title + "». Перевірте, чи бот може там писати.", "HTML");
    return true;
  }
  return false;
}

void handleMenuCallback(const String& data, int messageId) {
  if (guideCallback(data, messageId)) {
    return;
  }
  if (srvMenuCallback(data, messageId)) {
    return;
  }
  if (logClearCallback(data, messageId)) {
    return;
  }
  if (menuInlineCallback(data, messageId)) {
    return;
  }
  if (data.startsWith("x:rstY")) {
    const bool fresh = menuRestartNonce.length() > 0 && data == "x:rstY:" + menuRestartNonce &&
                       millis() - menuRestartMs <= 120000UL;
    menuRestartNonce = "";
    if (!fresh) {
      tgEditText(chatId, messageId, "⌛ Підтвердження застаріло. Натисніть «🔁 Перезапуск» ще раз.", "", tgInlineKb("[]"));
      return;
    }
    tgEditText(chatId, messageId, "🔁 Перезапуск...", "", tgInlineKb("[]"));
    prepareForRestart("кнопка «🔁 Перезапуск» у меню");
    delay(1000);
    ESP.restart();
  }
  else if (data == "x:rstN") {
    tgEditText(chatId, messageId, "Перезапуск скасовано.", "", tgInlineKb("[]"));
  }
  else if (data.startsWith("x:apY:") || data.startsWith("x:apN:")) {
    const bool allow = data.startsWith("x:apY:");
    int i = chatFind(data.substring(6));
    if (i < 0 || !chats.e[i].used) {
      tgEditText(chatId, messageId, "Цього чату вже немає в реєстрі.", "", tgInlineKb("[]"));
      return;
    }
    tgEditText(chatId, messageId, chatApproveRun(i, allow), "HTML", tgInlineKb("[]"));
  }
  else if (data == "x:lvN") {
    tgEditText(chatId, messageId, "Вихід скасовано.", "", tgInlineKb("[]"));
  }
  else if (data.startsWith("x:lvY:")) {
    int i = chatFind(data.substring(6));
    if (i < 0 || i >= MAX_CHATS || !chats.e[i].used || chats.e[i].status == 0) {
      tgEditText(chatId, messageId, "Бота в цьому чаті вже немає.", "", tgInlineKb("[]"));
      return;
    }
    bool ok = false;
    tgEditText(chatId, messageId, chatLeaveRun(i, &ok), "HTML", tgInlineKb("[]"));
    if (ok) {
      menuOpen("grp", "👥 Групи й канали: " + String(chatActiveCount()));
    }
  }
}


String guideText(int step) {
  const String dom = webDomain.length() > 0 ? htmlEsc(webDomain) : String("light.example.com");
  const String ip = webIpManual ? webIpVal : pubIpAuto;
  const String lan = WiFi.localIP().toString();
  const String head = "📖 <b>Домен і HTTPS</b> · крок " + String(step) + "/" + String(GUIDE_STEPS) + "\n\n";
  switch (step) {
    case 1:
      return head + "<b>Домен у Cloudflare</b>\n"
             "1. dash.cloudflare.com → <b>Add a domain</b> → ваш домен → тариф <b>Free</b>.\n"
             "2. У реєстратора домену замініть NS на два сервери, які показав Cloudflare.\n"
             "3. Дочекайтеся статусу <b>Active</b> (зазвичай до години).\n\n"
             "Потрібен власний домен (напр. .pp.ua). Безкоштовні піддомени на кшталт duckdns.org не підійдуть.";
    case 2:
      return head + "<b>Запис DNS</b>\n"
             "Cloudflare → <b>DNS → Records → Add record</b>:\n"
             "• Type <code>A</code> · Name <code>light</code>\n"
             "• IPv4 " + (ip.length() > 0 ? "<code>" + ip + "</code>" : String("- ваша публічна IP")) + "\n"
             "• Proxy status: <b>Proxied</b> 🟠\n\n"
             "Потім натисніть «🌍 Ввести домен» і надішліть <code>" + dom + "</code>.";
    case 3:
      return head + "<b>Налаштування Cloudflare</b>\n"
             "• <b>SSL/TLS → Overview</b> → <b>Flexible</b>\n"
             "• <b>SSL/TLS → Edge Certificates</b> → Always Use HTTPS: <b>On</b>\n"
             "• <b>Rules → Origin Rules → Create rule</b>: Hostname equals <code>" + dom + "</code> → Destination Port <code>8080</code> → Deploy";
    default:
      return head + "<b>Роутер</b>\n"
             "• Закріпіть за пристроєм IP <code>" + lan + "</code> (DHCP-резервування).\n"
             "• Перенаправте порт: зовнішній TCP <code>8080</code> → <code>" + lan + ":8080</code>.\n"
             "⚠️ Порт 80 пристрою ніколи не відкривайте - це адмінпанель.\n\n"
             "Потім натисніть «✅ Перевірити все».";
  }
}

static String guideCopyBtn(const String& text, const String& value) {
  return "{\"text\":\"" + jsonEsc(text) + "\",\"copy_text\":{\"text\":\"" + jsonEsc(value) + "\"}}";
}

String guideKeyboard(int step) {
  String kb = "[";
  const String ip = webIpManual ? webIpVal : pubIpAuto;
  if (step == 2) {
    kb += "[" + (ip.length() > 0 ? guideCopyBtn("📋 IP", ip) + "," : String("")) + "{\"text\":\"🌍 Ввести домен\",\"callback_data\":\"x:gdom\"}],";
  }
  if (step == 3 && webDomain.length() > 0) {
    kb += "[" + guideCopyBtn("📋 Домен", webDomain) + "],";
  }
  kb += "[{\"text\":\"" + String(step >= GUIDE_STEPS ? "✅ Перевірити все" : "✅ Перевірити") + "\",\"callback_data\":\"x:gchk\"}],[";
  if (step > 1) kb += "{\"text\":\"◀️ Назад\",\"callback_data\":\"x:g:" + String(step - 1) + "\"},";
  if (step < GUIDE_STEPS) kb += "{\"text\":\"Далі ▶️\",\"callback_data\":\"x:g:" + String(step + 1) + "\"},";
  return kb + "{\"text\":\"🏠 Меню\",\"callback_data\":\"x:ghome\"}]]";
}

void guideShow(int step, int messageId) {
  if (step < 1) step = 1;
  if (step > GUIDE_STEPS) step = GUIDE_STEPS;
  if (messageId > 0) {
    tgEditText(chatId, messageId, guideText(step), "HTML", tgInlineKb(guideKeyboard(step)));
  } else {
    tgSendText(chatId, guideText(step), "HTML", tgInlineKb(guideKeyboard(step)));
  }
}

bool guideCallback(const String& data, int messageId) {
  if (data.startsWith("x:g:")) {
    guideShow(data.substring(4).toInt(), messageId);
    return true;
  }
  if (data == "x:gchk") {
    webCheckRun();
    return true;
  }
  if (data == "x:gdom") {
    menuInputStart("domain", "web", "🌍 Надішліть домен, підключений до Cloudflare, без https:// і порту. Напр.: light.example.com"
                   + (webDomain.length() > 0 ? "\nЗараз: " + webDomain : String("")));
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
  const String pre = note.length() > 0 ? note + "\n\n" : String("");
  if (back == "adm") {
    menuOpenClean("adm", pre + admStatusText());
  } else if (back == "web") {
    webMenuShowClean(note);
  } else if (back == "webx") {
    menuOpenClean("webx", pre + webxText());
  } else if (back == "volt") {
    menuOpenClean("volt", pre + voltMenuText());
  } else if (back == "srv") {
    menuOpenClean("srv", pre + srvMenuText());
  } else if (back == "dev") {
    menuOpenClean("dev", note.length() > 0 ? note + "\n\n" + menuDevCard() : menuDevCard());
  } else if (back == "yasno") {
    yasnoPendingCity = "";
    yasnoPendingStreet = "";
    yasnoPendingStreetId = -1;
    menuOpenClean("yasno", pre + yasnoMenuText());
  } else {
    menuOpenClean("set", note.length() > 0 ? note + "\n\n" + menuSetCard() : menuSetCard());
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
    menuInputReturn("✅ Домен збережено. Натисніть «🔄 Перевірити».");
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

  if (mode == "srv_url") {
    String url;
    const String err = srvParseUrl(text, url);
    if (err.length() > 0) {
      menuSend("❌ " + err + " Введіть ще раз або натисніть ❌ Скасувати.", "");
      return;
    }
    menuInputReturn("");
    srvCheckStart(url);
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
