#include "lw.h"

static DNSServer dnsServer;
static bool apOpenFallback = false;

const char* const CFG_STG[6] = { "stgSsid", "stgPass", "stgToken", "stgChat", "stgGroup", "stgShelly" };

const char* const CFG_KEYS[6] = { "ssid", "pass", "token", "chatId", "groupId", "shellyIP" };

const char cfg_css[] PROGMEM = R"rawliteral(
:root{--bg:#0f141a;--card:#18202a;--ink:#e6ebf1;--mut:#8e9bab;--line:#263140;--on:#33c072;--onS:#14301f;--off:#ef5f5f;--offS:#3a1b1d;--warn:#f0a83c;--warnS:#382a14;--acc:#6f9bff;--btx:#0f141a;color-scheme:dark}
@media(prefers-color-scheme:light){:root{--bg:#f2f4f7;--card:#fff;--ink:#17202b;--mut:#647386;--line:#d8dee6;--on:#1f9d55;--onS:#e3f5ea;--off:#d64545;--offS:#fbe7e7;--warn:#c27a12;--warnS:#fdf1de;--acc:#2a62d4;--btx:#fff;color-scheme:light}}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 -apple-system,"Segoe UI",Roboto,Arial,sans-serif}
.w{max-width:460px;margin:0 auto;padding:18px 16px 40px}a{color:var(--acc)}
.top{display:flex;align-items:center;gap:12px;margin:4px 2px 16px}.bolt{font-size:34px;line-height:1}.top h1{font-size:19px;margin:0}.sub{color:var(--mut);font-size:13px}
.c{background:var(--card);border-radius:16px;padding:18px 16px;margin-bottom:12px}
.sec{display:flex;align-items:center;gap:8px;font-weight:700;font-size:15px;margin:4px 0 10px}.sec i{font-style:normal;width:22px;height:22px;border-radius:50%;background:var(--acc);color:var(--btx);font-size:12px;display:inline-flex;align-items:center;justify-content:center}
.sec.o{margin-top:22px;padding-top:18px;border-top:1px solid var(--line)}
label{display:block;font-size:13px;color:var(--mut);margin-bottom:12px}
input{display:block;width:100%;margin-top:5px;padding:12px;border:1px solid var(--line);border-radius:10px;background:var(--bg);color:var(--ink);font-size:16px}
input:focus{outline:none;border-color:var(--acc)}input:invalid:not(:placeholder-shown){border-color:var(--off)}
small{display:block;margin-top:5px;font-size:12px;color:var(--mut)}
.pw{position:relative}.pw input{padding-right:48px}.pw button{position:absolute;right:4px;top:4px;bottom:0;margin-top:5px;width:40px;border:0;background:none;color:var(--mut);font-size:18px;cursor:pointer}
.btn{display:block;width:100%;margin-top:18px;padding:14px;border:0;border-radius:12px;background:var(--acc);color:var(--btx);font-size:16px;font-weight:700;cursor:pointer}.btn:disabled{opacity:.6}
.warn{background:var(--offS);color:var(--off);border-radius:12px;padding:10px 14px;margin-bottom:12px;font-size:14px}
.foot{text-align:center;color:var(--mut);font-size:12px;margin-top:18px}
.msg{text-align:center;padding:28px 18px}.msg .ic{font-size:44px}.msg h2{margin:8px 0 6px;font-size:20px}.msg p{color:var(--mut);margin:0 0 6px}
)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light dark"><title>Light Watcher · налаштування</title>
<style>%CSS%</style></head><body><div class="w">
<div class="top"><span class="bolt">⚡</span><div><h1>Light Watcher V4.0.0</h1><div class="sub">Перше налаштування пристрою</div></div></div>
<div id="apw"></div>
<form class="c" action="/save" method="POST" id="f">
<div class="sec"><i>1</i>Wi-Fi</div>
<label>Назва мережі<input name="ssid" required maxlength="32" autocapitalize="none" autocorrect="off" spellcheck="false" placeholder="Назва вашої мережі Wi-Fi"></label>
<label>Пароль<div class="pw"><input type="password" name="pass" id="pw" maxlength="63" pattern=".{8,63}" autocapitalize="none" autocorrect="off" placeholder="Порожньо — відкрита мережа"><button type="button" id="eye" aria-label="Показати пароль">👁</button></div><small>8–63 символи. Пристрій працює лише з Wi-Fi 2,4 ГГц.</small></label>
<div class="sec o"><i>2</i>Telegram</div>
<label>Токен бота<input name="token" required maxlength="120" pattern="[0-9]{6,12}:[A-Za-z0-9_\-]{30,}" autocapitalize="none" autocorrect="off" spellcheck="false" placeholder="123456789:AA…"><small>Створіть бота в @BotFather і вставте токен, який він видав.</small></label>
<label>Ваш Telegram ID<input name="owner" required maxlength="20" inputmode="numeric" pattern="[0-9]{1,20}" placeholder="Напр. 123456789"><small>Число — ваш особистий ID (не групи). Його показує @userinfobot.</small></label>
<div class="sec o"><i>3</i>Напруга <span class="sub">— необов'язково</span></div>
<label>Адреса Shelly<input name="shelly" maxlength="63" autocapitalize="none" autocorrect="off" spellcheck="false" placeholder="Напр. 192.168.1.50"><small>Реле Shelly в тій самій мережі. Можна залишити порожнім і додати пізніше.</small></label>
<div class="sec o"><i>4</i>Сервер <span class="sub">— необов'язково</span></div>
<label>Адреса сервера<input name="srv" maxlength="80" autocapitalize="none" autocorrect="off" spellcheck="false" placeholder="Напр. 203.0.113.10:8000"><small>Порожньо — пристрій працює сам. Адресу перевірить бот після запуску, режим сам не зміниться.</small></label>
<button class="btn" id="sv">Зберегти й перезапустити</button>
</form>
<div class="foot">Developed by Stanislav Turii · <a href="https://github.com/Stanislav-developer/Light_Watcher">GitHub</a></div>
</div><script>
document.getElementById('eye').onclick=function(){var p=document.getElementById('pw');p.type=p.type==='password'?'text':'password'};
document.getElementById('f').onsubmit=function(){var b=document.getElementById('sv');b.disabled=true;b.textContent='Зберігаю…'};
</script></body></html>)rawliteral";

String cfgMsgPage(const String& icon, const String& title, const String& text, bool back) {
  String h = F("<!DOCTYPE html><html lang=\"uk\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"color-scheme\" content=\"light dark\"><title>Light Watcher</title><style>");
  h += FPSTR(cfg_css);
  h += F("</style></head><body><div class=\"w\"><div class=\"c msg\"><div class=\"ic\">");
  h += icon + "</div><h2>" + title + "</h2><p>" + text + "</p>";
  if (back) {
    h += F("<button class=\"btn\" onclick=\"history.back()\">Повернутися до форми</button>");
  }
  h += F("</div></div></body></html>");
  return h;
}

void handleRoot() {
  String page = FPSTR(index_html);
  page.replace("%CSS%", FPSTR(cfg_css));
  if (apOpenFallback) {
    page.replace("<div id=\"apw\"></div>", "<div class=\"warn\">⚠️ Точка доступу працює БЕЗ пароля: захист WPA2 не вдалося увімкнути (пароль збереженої мережі Wi-Fi не 8–63 символи або помилка Wi-Fi).</div>");
  }
  server.send(200, "text/html; charset=utf-8", page);
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
    String n_srv = server.arg("srv");
    n_srv.trim();

    if (n_ssid.length() > 32 || n_pass.length() > 64 || n_token.length() > 120 || n_owner.length() > 23 ||
        n_group.length() > 23 || n_shelly.length() > 63) {
      server.send(400, "text/html; charset=utf-8", cfgMsgPage("❌", "Некоректні дані", "Одне з полів задовге (SSID до 32, пароль до 64, ID до 23, Shelly до 63 символів).", true));
      return;
    }
    String srvUrl;
    const String invalid = cfgValidate(n_ssid, n_pass, n_token, n_owner, n_group, n_shelly);
    const String srvInvalid = n_srv.length() > 0 ? srvParseUrl(n_srv, srvUrl) : String("");
    if (invalid.length() == 0 && srvInvalid.length() > 0) {
      server.send(400, "text/html; charset=utf-8", cfgMsgPage("❌", "Некоректні дані", "Адреса сервера: " + htmlEsc(srvInvalid), true));
      return;
    }
    if (invalid.length() > 0) {
      server.send(400, "text/html; charset=utf-8", cfgMsgPage("❌", "Некоректні дані", htmlEsc(invalid), true));
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
        server.send(500, "text/html; charset=utf-8", cfgMsgPage("❌", "Не вдалося зберегти", "Запис у пам'ять пристрою не підтвердився. Спробуйте ще раз.", true));
        return;
      }
      cfgCommitApply();
      if (srvUrl.length() > 0) preferences.putString("srvNew", srvUrl);

      server.send(200, "text/html; charset=utf-8", cfgMsgPage("✅", "Збережено!", "Пристрій перезапускається й підключається до Wi-Fi. За хвилину бот надішле вам у Telegram «⚡ Light Watcher активний».", false));

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
      logEvent(LC_WIFI, LS_ERR, apPass.length() >= 8 && apPass.length() <= 63 ? "AP: WPA2 не запустилась - мережа відкрита"
                                                                                : "AP: пароль не 8-63 символи - мережа відкрита");
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
      logEvent(LC_STORE, LS_ERR, "Налаштування не застосовано, повтор");
    }
    return;
  }
  bool leftover = false;
  for (int i = 0; i < 6; i++) {
    leftover = leftover || preferences.isKey(CFG_STG[i]);
  }
  if (leftover) {
    cfgStageClear();
    logEvent(LC_STORE, LS_WARN, "Незавершені налаштування відкинуто");
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
