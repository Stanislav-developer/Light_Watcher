#include "lw.h"

static DNSServer dnsServer;
static bool apOpenFallback = false;

const char* const CFG_STG[6] = { "stgSsid", "stgPass", "stgToken", "stgChat", "stgGroup", "stgShelly" };

const char* const CFG_KEYS[6] = { "ssid", "pass", "token", "chatId", "groupId", "shellyIP" };

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Light Watcher Setup</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background-color: #f0f2f5; text-align: center; margin: 0; }
    .header { background-color: #0057B8; color: white; padding: 20px; }
    .container { padding: 20px; max-width: 400px; margin: 0 auto; }
    input { width: 100%; padding: 12px; margin: 8px 0; box-sizing: border-box; border: 2px solid #FFD700; border-radius: 4px; }
    input[type=submit] { background-color: #0057B8; color: white; border: none; cursor: pointer; font-weight: bold; font-size: 16px; margin-top: 10px; }
    input[type=submit]:hover { background-color: #004494; }
    .footer { margin-top: 30px; font-size: 12px; color: #555; border-top: 1px solid #ddd; padding-top: 15px; }
    a { color: #0057B8; text-decoration: none; word-wrap: break-word; display: block; margin-bottom: 10px; }
    p { margin: 5px 0; font-weight: bold; }
  </style>
</head><body>
  <div class="header"><h1>Light Watcher V2.4.11</h1></div>
  <div class="container">
    <h3>Налаштування пристрою</h3>
    <h4>Будь ласка, коректно введіть усі дані!</h4>
    <form action="/save" method="POST">
      <label>WiFi Назва (SSID)</label><input type="text" name="ssid" placeholder="Назва мережі">
      <label>WiFi Пароль</label><input type="password" name="pass" placeholder="Пароль (порожньо - відкрита мережа)">
      <label>Telegram Bot Token</label><input type="text" name="token" placeholder="Токен бота">
      <label>Власник (Chat ID)</label><input type="text" name="owner" placeholder="Ваш особистий Telegram ID (число)">
      <label>Група (Group ID)</label><input type="text" name="group" placeholder="ID групи (необов'язково)">
      <label>Shelly IP (напруга)</label><input type="text" name="shelly" placeholder="Напр. 192.168.1.50 (необов'язково)">
      <input type="submit" value="ЗБЕРЕГТИ">
    </form>
    <div class="footer">
      <h3>About Project</h3>
      
      <h4>Developed by Stanislav Turii</h4>

      <p>GitHub:</p>
      <a href='https://github.com/Stanislav-developer/Light_Watcher'>https://github.com/Stanislav-developer/Light_Watcher</a>
      
    </div>
  </div>
</body></html>)rawliteral";

void handleRoot() {
  if (apOpenFallback) {
    String page = String(index_html);
    page.replace("<h3>Налаштування пристрою</h3>", "<p style='color:#b00020;'>⚠️ Точка доступу працює БЕЗ пароля: захист WPA2 не вдалося увімкнути (пароль збереженої мережі Wi-Fi не 8–63 символи або помилка Wi-Fi).</p><h3>Налаштування пристрою</h3>");
    server.send(200, "text/html", page);
    return;
  }
  server.send(200, "text/html", index_html);
}

bool cfgPutStrVerified(const char* key, const String& val) {
  return prefPutStrVerified(key, val);
}

bool cfgCommitPending() {
  return preferences.getUChar("cfgCommit", 0) == 1;
}

String cfgGet(int i, const String& def) {
  return preferences.getString(cfgCommitPending() ? CFG_STG[i] : CFG_KEYS[i], def.c_str());
}

void cfgStageClear() {
  if (preferences.isKey("cfgCommit")) preferences.remove("cfgCommit");
  for (int i = 0; i < 6; i++) {
    if (preferences.isKey(CFG_STG[i])) preferences.remove(CFG_STG[i]);
  }
}

bool cfgCommitApply() {
  for (int i = 0; i < 6; i++) {
    if (!preferences.isKey(CFG_STG[i])) {
      return false;
    }
  }
  for (int i = 0; i < 6; i++) {
    if (!cfgPutStrVerified(CFG_KEYS[i], preferences.getString(CFG_STG[i], ""))) {
      return false;
    }
  }
  preferences.remove("cfgCommit");
  if (preferences.isKey("cfgCommit")) {
    return false;
  }
  cfgStageClear();
  return true;
}
static bool cfgAllDigits(const String& s, unsigned from) {
  if (s.length() <= from) return false;
  for (unsigned i = from; i < s.length(); i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

String cfgValidate(const String& ssid, const String& pass, const String& token, const String& owner,
                   const String& group, const String& shelly) {
  if (ssid.length() == 0) return "Вкажіть назву мережі Wi-Fi.";
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) {
    return "Пароль Wi-Fi: 8-63 символи (або порожньо для відкритої мережі).";
  }
  int colon = token.indexOf(':');
  bool tokenOk = colon >= 6 && colon <= 12 && cfgAllDigits(token.substring(0, colon), 0) && token.length() >= (unsigned)colon + 30;
  for (unsigned i = colon + 1; tokenOk && i < token.length(); i++) {
    char c = token[i];
    tokenOk = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  }
  if (!tokenOk) return "Токен бота має вигляд 123456789:AA... (як видав @BotFather).";
  if (!cfgAllDigits(owner, 0) || owner.length() > 20) return "ID власника - додатне число (ваш особистий Telegram ID), не ID групи.";
  if (group.length() > 0 && !(group[0] == '-' ? cfgAllDigits(group, 1) : cfgAllDigits(group, 0))) {
    return "ID групи - число (для груп зазвичай починається з -100).";
  }
  for (unsigned i = 0; i < shelly.length(); i++) {
    char c = shelly[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':' || c == '/')) {
      return "Адреса Shelly: IP або ім'я вузла, без пробілів.";
    }
  }
  return "";
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("token") && server.hasArg("owner")) {
    String n_ssid = server.arg("ssid");
    String n_pass = server.arg("pass");
    String n_token = server.arg("token");
    String n_owner = server.arg("owner");
    String n_group = server.arg("group");
    String n_shelly = server.arg("shelly");

    if (n_ssid.length() > 32 || n_pass.length() > 64 || n_token.length() > 120 || n_owner.length() > 23 ||
        n_group.length() > 23 || n_shelly.length() > 63) {
      server.send(400, "text/html", "<html><head><meta charset='utf-8'></head><body style='text-align:center; font-family:Arial;'>"
                  "<h1 style='color:red;'>Некоректні дані ❌</h1><p>Одне з полів задовге (SSID до 32, пароль до 64, ID до 23, Shelly до 63 символів). Поверніться назад.</p></body></html>");
      return;
    }
    const String invalid = cfgValidate(n_ssid, n_pass, n_token, n_owner, n_group, n_shelly);
    if (invalid.length() > 0) {
      server.send(400, "text/html", "<html><head><meta charset='utf-8'></head><body style='text-align:center; font-family:Arial;'>"
                  "<h1 style='color:red;'>Некоректні дані ❌</h1><p>" + htmlEsc(invalid) + " Поверніться назад.</p></body></html>");
      return;
    }
    {

      const String vals[6] = { n_ssid, n_pass, n_token, n_owner, n_group, n_shelly };
      cfgStageClear();
      bool okAll = true;
      for (int i = 0; i < 6; i++) {
        okAll = cfgPutStrVerified(CFG_STG[i], vals[i]) && okAll;
      }
      if (okAll) {
        preferences.putUChar("cfgCommit", 1);
        okAll = cfgCommitPending();
      }
      if (!okAll) {
        cfgStageClear();
        server.send(500, "text/html", "<html><head><meta charset='utf-8'></head><body style='text-align:center; font-family:Arial;'>"
                    "<h1 style='color:red;'>Не вдалося зберегти ❌</h1><p>Запис у пам'ять пристрою не підтвердився. Поверніться назад і спробуйте ще раз.</p></body></html>");
        return;
      }
      cfgCommitApply();

      String response = "<html><head><meta charset='utf-8'></head><body style='text-align:center; font-family:Arial;'>";
      response += "<h1 style='color:green;'>Збережено! ✅</h1>";
      response += "<p>Пристрій перезавантажується...</p></body></html>";
      server.send(200, "text/html", response);

      prepareForRestart("збережено налаштування на сторінці налаштування");
      delay(2000);
      ESP.restart();
    }
  }
  server.send(400, "text/plain", "Error: Missing data");
}
void launchWebServer(bool protect) {

    WiFi.mode(WIFI_AP);
    String apPass = protect ? cfgGet(1, "") : String("");
    bool secured = false;
    if (apPass.length() >= 8 && apPass.length() <= 63) {
      secured = WiFi.softAP("Light-Watcher-Setup", apPass.c_str());
    }
    if (protect && !secured && apPass.length() >= 8 && apPass.length() <= 63) {
      delay(200);
      secured = WiFi.softAP("Light-Watcher-Setup", apPass.c_str());
    }
    if (!secured) {
      WiFi.softAP("Light-Watcher-Setup");
    }

    apOpenFallback = protect && !secured;
    if (apOpenFallback) {
      logEvent(LC_WIFI, LS_ERR, apPass.length() >= 8 && apPass.length() <= 63 ? "Точка доступу: WPA2 не запустилась - відкрита"
                                                                                : "Точка доступу: пароль Wi-Fi не 8-63 символи - відкрита");
      logFlush();
    }
    Serial.println(secured ? "Точка доступу захищена WPA2: пароль - пароль збереженої мережі Wi-Fi" : "Точка доступу відкрита (без пароля)");

    dnsServer.start(53, "*", WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.onNotFound(handleRoot);
    server.begin();

    Serial.println("Точка доступу створена. IP: " + WiFi.softAPIP().toString());

    unsigned long apIdleSince = millis();
    while (true) {
      dnsServer.processNextRequest();
      server.handleClient();
      wdtFeed();
      if (WiFi.softAPgetStationNum() > 0) {
        apIdleSince = millis();
      } else if (protect && millis() - apIdleSince >= AP_IDLE_RESTART_MS) {
        prepareForRestart("режим налаштування: 15 хв без підключень");
        delay(100);
        ESP.restart();
      }
      delay(5);
    }
}

void cfgCommitRecover() {
  if (cfgCommitPending()) {
    if (cfgCommitApply()) {
      logEvent(LC_STORE, LS_INFO, "Налаштування: застосування нового набору завершено після перезапуску");
    } else {
      logEvent(LC_STORE, LS_ERR, "Налаштування: новий набір не застосовано - використовується з тимчасових ключів, повтор при запуску");
    }
    return;
  }
  bool leftover = false;
  for (int i = 0; i < 6; i++) {
    leftover = leftover || preferences.isKey(CFG_STG[i]);
  }
  if (leftover) {
    cfgStageClear();
    logEvent(LC_STORE, LS_WARN, "Налаштування: незавершене збереження відкинуто, діє попередній набір");
  }
}

bool nvsOpenWithRetry(uint8_t attempts) {
  for (uint8_t a = 0; a < attempts; a++) {
    if (a > 0) delay(200U * a);
    if (preferences.begin("light-watcher", false)) return true;
    Serial.println("NVS: спроба відкриття " + String(a + 1) + " не вдалася");
  }
  return false;
}
