/* Light-Watcher firmware V1.6
Repository: https://github.com/Stanislav-developer/Light_Watcher
Authors: 
  Stanislav Turii (GitHub: https://github.com/Stanislav-developer || Youtube: https://www.youtube.com/@TehnoMaisterna)
  Noctyon (GitHub: https://github.com/Noctyon)
Date: 2026.10.20

НАЛАШТУВАННЯ ДЛЯ ЗАЛИВКИ ПРОШИВКИ (ESP32-C3):
У Tools:
1. Board: "ESP32C3 Dev Module"
2. USB CDC On Boot: "Enabled" (Обов'язково для Serial Monitor)
3. Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
4. Решта налаштувань: за замовчуванням

ЯК УВІЙТИ В РЕЖИМ ПРОШИВКИ:
1. Під'єднайте ESP32 до комп'ютера.
2. Затисніть кнопку BOOT.
3. Утримуючи BOOT, натисніть кнопку RESET (1 сек).
4. Відпустіть RESET, а потім відпустіть BOOT.

ВАЖЛИВО: На ESP32-C3 використовуйте ADC1 піни (GPIO 0, 1, 2, 3, 4, 5).
*/

//Підключення бібліотек
#include <WiFi.h> // Для роботи з WiFi(Вбудована)
#include <WiFiClientSecure.h>
#include <time.h> // Для роботи з часом(Вбудована)
#include <Preferences.h> // Для роботи з внутрішньою енергонезалежною пам'ятю(Вбудована)
#include <UniversalTelegramBot.h> // Telegram API(Потрібно встановити з пошуку Arduino IDE)
#include <WebServer.h> // Для веб-сервера
#include <DNSServer.h> // Для перенаправлення (DNS)

// Конфігурація пінів
#define readPin 3 // Аналоговий пін, вимірює рівень заряду на вході(детектує наявність електромережі). Можна використовувати будь який пін на 1АЦП

// Дані для конфігурації(треба заповнити при налаштуванні у WEB інтерфейсі)
String  ssid = ""; // Назва WiFi мережі  
String  password = ""; // Пароль до WiFi мережі
String  botToken = ""; // API Токен бота
String  chatId = ""; // Чат ID власника бота 
String  groupId = ""; // Чат ID групи

const char* ntp1 = "pool.ntp.org"; // 1 NTP сервер реального часу
const char* ntp2 = "time.google.com"; // 2 NTP сервер реального часу(резерв)
const char* ntp3 = "time.cloudflare.com"; // 3 NTP сервер реального часу(резерв)

//Об'єкти:
Preferences preferences; // Створюємо об'єкт для роботи з preferences
WiFiClientSecure client; // Створюємо об'єкт для передачі та отримання повідомлень з Telegram.
UniversalTelegramBot bot(botToken, client); // Об'єкт для бота
WebServer server(80); // Об'єкт для веб-сервера
DNSServer dnsServer;  // Об'єкт для DNS сервера

// Глобальні змінні
bool powerStatus = true; // true - мережа присутня, false - мережа відсутня
bool messageFlag = false; // true - повідомлення про відключення вже надіслано, false - повідомлення ще не надіслано
bool lastOutageDetect; // Останнє детектування зникнення електромережі, потрібне на випадок якщо батарея розрядиться
bool missMessage; // Прапорець який вказує що повідомлення про появу світла не надіслалось
uint16_t powerOutageCount = 0; // К-сть відключень електроенергії за весь час(зберігається у Preferences)

uint32_t powerOffTime = 0; // Час відключення світла в мілісекундах (millis)
uint32_t powerOnTime = 0; // Час появи світла в мілісекундах (millis)
time_t powerOffTimestamp = 0; // Unix timestamp відключення (секунди з 1970)
time_t powerOnTimestamp = 0; // Unix timestamp появи світла (секунди з 1970)
time_t currentTimestamp = 0; // Поточний Unix timestamp для розрахунків (секунди з 1970)

String currentTZ = ""; // Поточний часовий пояс (EEST-2 або EEST-3)
String powerOffFormattedTime = ""; // Відформатований час відключення (дд.мм.рррр чч:хх:сс)
String powerOnFormattedTime = ""; // Відформатований час появи світла (дд.мм.рррр чч:хх:сс)
String lastMissMessage = ""; // Змінна у якій лежить сформоване повідомлення про появу світла(якщо wifi мережа була відсутня)

// HTML код розмітки WEB Інтерфейсу
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Light Watcher Setup</title>
  <style>
    body {
      font-family: system-ui, -apple-system, sans-serif;
      background: #ffffff;
      margin: 0;
      padding: 0;
      display: flex;
      flex-direction: column;
      min-height: 100vh;
    }
    
    form {
      display: flex;
      flex-direction: column;
      flex: 1;
    }
    
    .header {
      background: #0057B8;
      color: #fff;
      padding: 30px 20px;
      text-align: center;
      border-bottom: 4px solid #FFD700;
    }
    
    .header h2 {
      margin: 0;
      font-size: 24px;
    }
    
    .header p {
      margin: 8px 0 0;
      font-size: 14px;
      opacity: 0.9;
    }
    
    .content {
      padding: 30px 20px;
      width: 100%;
      max-width: 400px;
      margin: 0 auto;
      box-sizing: border-box;
      flex: 1;
    }
    
    label {
      font-size: 13px;
      font-weight: 600;
      color: #555;
      display: block;
      margin-bottom: 6px;
    }
    
    input {
      width: 100%;
      padding: 12px 15px;
      margin-bottom: 18px;
      box-sizing: border-box;
      border: 1px solid #dcdcdc;
      border-radius: 8px;
      font-size: 15px;
      outline: none;
      transition: 0.2s;
      background: #fafafa;
    }
    
    input:focus {
      border-color: #0057B8;
      background: #fff;
      box-shadow: 0 0 0 3px rgba(0, 87, 184, 0.12);
    }
    
    .btn {
      width: 100%;
      background: #0057B8;
      color: #fff;
      border: none;
      border-radius: 8px;
      padding: 15px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      margin-top: 10px;
      transition: 0.2s;
    }
    
    .btn:hover {
      background: #004494;
    }
    
    .footer {
      margin-top: 40px;
      text-align: center;
      font-size: 13px;
      color: #888;
      border-top: 1px solid #eee;
      padding-top: 20px;
      padding-bottom: 20px;
    }
    
    .footer a {
      color: #0057B8;
      text-decoration: none;
      font-weight: 600;
    }
  </style>
</head>
<body>

  <form action="/save" method="POST">
    <div class="header">
      <h2>Light Watcher V1.6</h2>
    </div>
    
    <div class="content">
      <label>WiFi Назва (SSID)</label>
      <input type="text" name="ssid" placeholder="Назва мережі" required>
      
      <label>WiFi Пароль</label>
      <input type="password" name="pass" placeholder="Пароль">
      
      <label>Telegram Bot Token</label>
      <input type="text" name="token" placeholder="Токен бота" required>
      
      <label>Власник (Chat ID)</label>
      <input type="text" name="owner" placeholder="Telegram ID власника" required>
      
      <label>Група (Group ID)</label>
      <input type="text" name="group" placeholder="Telegram ID групи (необов'язково)">
      
      <input type="submit" class="btn" value="ЗБЕРЕГТИ">
      
      <div class="footer">
        Developed by <a href="https://github.com/Stanislav-developer" target="_blank">Stanislav Turii</a> and <a href="https://github.com/Noctyon" target="_blank">Noctyon</a><br>
        <a href="https://github.com/Stanislav-developer/Light_Watcher" target="_blank">GitHub Repository</a>
      </div>
    </div>
  </form>

</body>
</html>
)rawliteral";

// Функція відображення сторінки
void handleRoot() {
  server.send(200, "text/html", index_html);
}

// Функція збереження даних
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("token") && server.hasArg("owner")) {
    if (server.arg("ssid").length() > 0 && server.arg("pass").length() > 0) {
      preferences.putString("ssid", server.arg("ssid"));
      preferences.putString("pass", server.arg("pass"));
      preferences.putString("token", server.arg("token"));
      preferences.putString("chatId", server.arg("owner"));
      preferences.putString("groupId", server.arg("group"));

      String response = "<html><head><meta charset='utf-8'></head>"
        "<body style='text-align:center; font-family:Arial;'>"
        "<h1 style='color:green;'>✅ Данні збережено! ✅</h1>"
        "<p>Пристрій перезавантажується...</p>"
        "<p>Закрийте цю сторінку</p>"
        "</body></html>";
      server.send(200, "text/html", response);
      delay(2000);
      ESP.restart();
    }
  }
  server.send(418, "text/plain", "Error: Missing data");
}

// Функція перевірки стану електромережі
bool checkPowerStatus() {
  return analogRead(readPin) >= 3000; // Повертає true якщо електромережа присутня
}

//Налаштування літнього/зимового часу
void applyTimezone(String tz) {
  setenv("TZ", tz.c_str(), 1); // Встановлюємо часовий пояс у системі (конвертуємо String → const char*)
  tzset(); // Застосовуємо зміни часового поясу
  currentTZ = tz; // Зберігаємо поточний часовий пояс
}

// Конвертація секунд Unix timestamp у кількість днів, годин, хвилин.
String formatDuration(time_t seconds) {
  String result = "";
  uint8_t days = seconds / 86400,
    hours = (seconds % 86400) / 3600,
    minutes = (seconds % 3600) / 60,
    secs = seconds % 60;
  
  if (days > 0) {
    result += String(days) + " д. ";
  }
  if (hours > 0 || days > 0) {
    result += String(hours) + " год. ";
  }
  if (minutes > 0 || hours > 0 || days > 0) {
    result += String(minutes) + " хв. ";
  }
  if (minutes < 10 && hours == 0 && days == 0) {
    result += String(secs) + " сек.";
  }
  
  return result; // повертаємо рядок із вже відформатованим часом, приклад: 7 год. 22 хв.
}

//Отримання вже відформатованої дати та часу для відстеження моменту вимкнення електроенергії
String getFormattedTime() {
  struct tm timeinfo;
  // Отримуємо синхронізований час з внутрішнього RTC esp32
  if (!getLocalTime(&timeinfo)) {
    return "??:??";
  }
  
  char timeStr[32]; // Створюємо масив символів для запису дати та часу
  strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M:%S", &timeinfo); // Заповнюємо масив

  return String(timeStr);
}

// Функція перевірки та підтримки стабільного WiFi з'єднання
void checkWiFi() {
  static uint32_t lastWiFiCheck = 0;
  uint8_t interval = 30; // Інтервал перевірки в секундах

  if (millis() - lastWiFiCheck > interval * 1000) {
    lastWiFiCheck = millis(); // Оновлюємо таймер

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi втрачено");
      Serial.println("Перепід'єднання до WiFi... ");
      
      WiFi.reconnect(); // Функція перепід'єднання
    }

    // Перевірка: якщо у нас є пропущене повідомлення під час того як WiFi був відсутній
    if (missMessage) {
      bool ok1 = bot.sendMessage(chatId, lastMissMessage, "HTML"); //Записуємо true якщо повідомлення надіслалось успішно
      bool ok2 = bot.sendMessage(groupId, lastMissMessage, "HTML"); //Записуємо true якщо повідомлення надіслалось успішно

      if (ok1 || ok2) {
        missMessage = false; // Скидаємо прапорець, щоб знову не надсилати це повідомлення
        Serial.println("Повідомлення про відновлення надіслано (із затримкою)");
      } 
      else {
        Serial.println("Не вдалося відправити. Ще одна спроба через " + String(interval) + " сек...");
      }
      return;
    }
  }
}

// Функція запуску точки доступу ESP32 та відображення WEB сервера
void launchWebServer() {
   // Запуск точки доступу
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Light-Watcher-Setup"); // Назва точки доступу(без паролю)
    
    // DNS сервер для автоматичного перенаправлення (Captive Portal)
    dnsServer.start(53, "*", WiFi.softAPIP());

    // Веб сервер
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.onNotFound(handleRoot); // Будь-яка адреса перекидає на головну
    server.begin();
    
    Serial.println("Точка доступу створена. IP: " + WiFi.softAPIP().toString());

    // Вічний цикл веб-сервера (код далі не піде, поки не збережуться дані)
    while (1) {
      dnsServer.processNextRequest();
      server.handleClient();
      delay(5); // Невелика затримка для стабільності
    }
}

// Функція обробки вхідних повідомлень
void handleNewMessages() {
  String chat_id = String(bot.messages[0].chat_id), // Отримуємо chatID користувача або групи
    text = bot.messages[0].text, // Отримуємо текст повідомлення
    from_name = bot.messages[0].from_name, // Отримуємо ім'я користувача
    message;
  bool reset = false;
  
  Serial.println("Отримано: " + text + " від " + from_name + " ID: " + chat_id);

  //Повідомлення "Світло є чи нема?" публічне.
  if((chat_id == chatId || chat_id == groupId) && text == "Світло є чи нема?"){
    if(checkPowerStatus()){
      message = "🟢 <b>СВІТЛО Є!</b>\n\n"
        "🕐 Час відновлення: " + powerOnFormattedTime + "\n";
    }
    else{
      message = "🔴 <b>СВІТЛА НЕМАЄ :(</b>\n\n"
        "⏱ Тривалість відключення: " + formatDuration(time(&currentTimestamp) - powerOffTimestamp) + "\n"
        "🕐 Час відключення: " + powerOffFormattedTime;
    }
  }

  // Приватні повідомлення, може писати тільки власник бота.
  if (chat_id == chatId && text != "Світло є чи нема?") {
    if (text == "/help") {
      message = "👋 Привіт, " + from_name + "!\n\n"
      "Доступні команди:\n"
      "/info - Про бота\n"
      "/status - Стан системи\n"
      "/set_summer_time - Встановити літній час\n"
      "/set_winter_time - Встановити зимовий час\n"
      "/clear_data  - Очистити конфігурацію та статистику\n"
      "/restart - Віддалений перезапуск бота";
    }
    
    else if (text == "/info") {
      message = "⚡ <b>Light Watcher</b> v1.6\n\n"
      " <b>Автоматичний моніторинг електромережі</b>\n"
      " 🤖 Що робить бот:\n"
      " • Повідомляє про відключення світла\n"
      " • Рахує час без електрики\n"
      " • Працює від акумулятора\n"
      " • Зберігає статистику\n\n"
      " • Сторінка проєкту:\n"
      " 📦 <a href='https://github.com/Stanislav-developer/Light_Watcher'>GitHub Repository</a>";
    }
    
    else if (text == "/status") {
      message = "📟 Стан системи: \n";
      if (checkPowerStatus()) {
        message += "💡 Електромережа: <b>ПРИСУТНЯ</b> ✅\n";
      }
      else {
        message += "💡 Електромережа: <b>ВІДСУТНЯ</b> ⚠️\n";
      }
      message += "К-сть вимкнень електроенергії: " + String(powerOutageCount) + "\n"
        "🕐 Поточний час: " + getFormattedTime() + "\n";
      if(currentTZ == "EEST-3") {
        message += "☀️ Встановлено літній час.\n";
      }
      else {
        message += "❄️ Встановлено зимовий час.\n";
      }

      message += "⏱ Пристрій працює: " + formatDuration(millis() / 1000); // К-сть секунд конвертуємо у дні, години, хвлини...
    }
    
    else if (text == "/set_summer_time") {
      currentTZ = "EEST-3";
      preferences.putString("tz-rule", currentTZ);
      applyTimezone(currentTZ);
      message = "Встановлено літній час ☀️";
    }

    else if (text == "/set_winter_time") {
      currentTZ = "EEST-2";
      preferences.putString("tz-rule", currentTZ);
      applyTimezone(currentTZ);
      message = "Встановлено зимовий час ❄️";
    }
    
    else if (text == "/clear_data") {
      preferences.clear(); // Очищення даних у енергонезалежній пам'яті
      message = "Конфігурацію та статистику очищено. Запуск у режим конфігурації";
      reset = true; // Кидаємо ESP32 у reset
    }
    
    else if (text == "/restart") {
      message = "Перезапуск...";
      reset = true; // Кидаємо ESP32 у reset
    }
    
    else {
      message = "❓ Невідома команда. Список команд: /help";
    }

    if (reset) {
      reset = false;
      delay(1000);
      ESP.restart(); 
    }
  }
  bot.sendMessage(chat_id, message, "HTML");
}

// Функція у якій код виконується лише раз при старті програми
void setup() {

  pinMode(readPin, INPUT);
  analogSetAttenuation(ADC_11db); // За верхню межу АЦП беремо 3.3В

  // Налаштування запасної кнопки скидання скидання конфігураціїї (кнопка Boot на ESP32-C3 зазвичай GPIO 9, на інших платах ESP це GPIO 0)
  pinMode(9, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println("Запуск Light Watcher");
  
  preferences.begin("light-watcher"); // Ініціалізуємо namespace для роботи з preferences

  delay(3000); // Невелика затримка перед тим як користувач затисне кнопку boot для оновлення конфігурації

  // Якщо при запуску затиснута кнопка BOOT (GPIO 9) - запускаємо WEB Сервер для кофігурації
  if (!digitalRead(9)) {
    Serial.println("Кнопка Boot затиснута. Запуск конфігурації...");
    launchWebServer();
  }

  // Завантаження даних у змінні
  ssid = preferences.getString("ssid", ssid);
  password = preferences.getString("pass", password);
  botToken = preferences.getString("token", botToken);
  chatId = preferences.getString("chatId", chatId);
  groupId = preferences.getString("groupId", groupId);

  // Перевіряємо, чи дійсно є дані конфігурації
  if (ssid == "" || password == "" || botToken == "" || chatId == "") {
    Serial.println("Дані конфігурації відсутні. Запуск Web Server");
    launchWebServer(); // Запускаємо WEB Сервер для конфігурації
  }

  //Якщо ми тут - значить конфігурація завершилася
  Serial.println("Конфігурацію завантажено.");
  
  // Оновлюємо токен бота (бо бібліотека могла ініціалізуватись з пустим)
  bot.updateToken(botToken);

  // Завантажуємо збережені дані змінних з енергонезалежної пам'яті
  powerOutageCount = preferences.getUInt("powerOutageCnt");
  currentTZ = preferences.getString("tz-rule", "EEST-2");
  powerOffTimestamp = preferences.getUInt("pwrOffTmstmp");
  lastOutageDetect = preferences.getBool("lastUotDetect");

  Serial.println("Дані з preferences завантажено.");

  WiFi.mode(WIFI_STA); // Перемикаємось на стандартний режим Клієнта

  //Під'єднуємось до WiFi
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.print("Під'єднання до WiFi: ");

  for (uint8_t wifi_attempts = 0; WiFi.status() != WL_CONNECTED && wifi_attempts <= 50; wifi_attempts++) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection error!");
  } else {
    Serial.println("WiFi Connected!");
  }

  // Якщо є WiFi то синхронізуємо час з NTP
  configTime(0, 0, ntp1, ntp2, ntp3);
  applyTimezone(currentTZ);

  Serial.print("Синхронізація часу: ");

  struct tm timeinfo;

  for (uint8_t attempts; !getLocalTime(&timeinfo) && attempts < 5; attempts++) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (!getLocalTime(&timeinfo)) {
    Serial.println("Час НЕ синхронізовано!");
  } else  {
    Serial.println("Час синхронізовано!");
  }

  client.setInsecure();
  client.setTimeout(2); // Зменшуємо Timeout на відправку повідомлень, щоб не переповнювати Watchdog

  powerStatus = checkPowerStatus(); // Перевіряємо статус живлення

  // Якщо було замічено вимкнення електроенергії але світло наявне(Вірогідно акумулятор розрядився)
  if (lastOutageDetect && powerStatus) {
    messageFlag = false;
    time(&powerOnTimestamp);
    uint32_t outageSeconds = powerOnTimestamp - powerOffTimestamp;
    powerOnFormattedTime = getFormattedTime();

    String message = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ</b>\n\n"
      "🕐 Час відновлення: " + powerOnFormattedTime + "\n"
      "⏱ Тривалість відключення: " + formatDuration(outageSeconds) + "\n"
      "⚠️ Заряд акумулятора був критичний та пристрій вимкнувся\n"
      "🔌 Живлення від мережі відновлено";

    lastOutageDetect = false;
    preferences.putBool("lastUotDetect", lastOutageDetect);

    bot.sendMessage(chatId, message, "HTML");
    if (groupId.length() > 0) {bot.sendMessage(groupId, message, "HTML");}
    Serial.println("Повідомлення про відновлення надіслано");
  } else {
    String startMessage = "⚡ <b>Light Watcher активовано</b>\n\n"
      "🕐 Час запуску: " + getFormattedTime() + "\n"
      "💡 Електромережа: ";
    if (powerStatus) {
      startMessage += "<b>ПРИСУТНЯ</b> ✅";
    } else {
      powerOffTime = millis();
      messageFlag = true;
      startMessage += "<b>ВІДСУТНЯ</b> ⚠️\n"
        "⚠️ Пристрій запущено на резервному живленні";
    }

    if (bot.sendMessage(chatId, startMessage, "HTML")) {
      Serial.println("Стартове повідомлення надіслано :)");
    } else {
      Serial.println("Стартове повідомлення НЕ надіслано, введена НЕ правильна інформація!!!");
      launchWebServer(); // Запускаємо WEB Server для конфігураціїї (ймовірно дані Telegram API було введено НЕВІРНО)
    }
  }

  // Щоб бот не відповідав на всі попередні повідомлення, чистимо чергу
  if (bot.getUpdates(-1) > 0) {
    bot.last_message_received = bot.messages[0].update_id;
  }
}

// Головна функція у якій код виконується постійно
void loop() {
  checkWiFi(); // Перевіряємо WiFi з'єднання

  bool currentPowerStatus = checkPowerStatus(); // Перевіряємо стан електромережі

  if (!currentPowerStatus && !messageFlag) { // Якщо світло вимкнули
    messageFlag = true;
    powerOffTime = millis(); 

    time(&powerOffTimestamp); // Зберігаємо Unix timestamp коли вимкнули світло для подальших розрахунків
    preferences.putUInt("pwrOffTmstmp", powerOffTimestamp);
    lastOutageDetect = true; // Встановлюємо на true що вимкнення вже відбулося
    preferences.putBool("lastUotDetect", lastOutageDetect);

    powerOffFormattedTime = getFormattedTime(); // Отримуємо точну дату та час коли вимкнули світло
    
    powerOutageCount++; // Збільшуємо к-сть вимкнень світла на 1
    preferences.putUInt("powerOutageCnt", powerOutageCount); 

    String message = "🔴 " + (powerOffFormattedTime.length() > 6 ? powerOffFormattedTime.substring(11, 16) : powerOffFormattedTime) +" <b>СВІТЛО ВИМКНУЛИ</b>";
    
    bot.sendMessage(chatId, message, "HTML"); //Відсилаємо повідомлення у бота
    if (groupId.length() > 0) {bot.sendMessage(groupId, message, "HTML");} //Відсилаємо повідомлення у групу
    Serial.println("Повідомлення про відключення надіслано");
  }

  else if (currentPowerStatus && messageFlag) { // Якщо світло увімкнули
    messageFlag = false;
    powerOnTime = millis();
    time(&powerOnTimestamp);// Зберігаємо Unix timestamp коли увімкнули світло для подальших розрахунків

    lastOutageDetect = false; // Скидаємо прапорець
    preferences.putBool("lastUotDetect", lastOutageDetect);

    uint32_t outageSeconds = (powerOnTime - powerOffTime) / 1000; // Рахуємо к-сть секунд з моменту відключення
    powerOnFormattedTime = getFormattedTime();

    String message = "🟢 " + (powerOnFormattedTime.length() > 6 ? powerOnFormattedTime.substring(11, 16) : powerOnFormattedTime) + " <b>СВІТЛО З'ЯВИЛОСЯ</b>\n\n"
      "⏱ Тривалість відключення: " + formatDuration(outageSeconds) + "\n";

    // Перевіряємо наявність WiFi мережі
    if (WiFi.status() != WL_CONNECTED) {
      missMessage = true;
      message += "Світло вимкнули: " + powerOffFormattedTime + "\n"
        "📵 Мережа WiFi була відсутня";
      lastMissMessage = message; // Архівуємо повідомлення, щоб відправити його як тільки WiFi з'явиться
      Serial.println("Повідомлення про відновлення НЕ надіслано");
    } else {
      bot.sendMessage(chatId, message, "HTML");
      if (groupId.length() > 0) {bot.sendMessage(groupId, message, "HTML");}
      Serial.println("Повідомлення про відновлення надіслано");
    }
  }

  // Перевіряємо наявність нових повідомлень кожну секунду
  static uint32_t lastBotCheck;
  if (millis() - lastBotCheck > 1000) {
    lastBotCheck = millis();
    if (bot.getUpdates(-1)) { // Читаємо тільки останнє надіслане повідомлння з черги
      handleNewMessages();  // Якщо повідомлення є, відсилаємо його на обробку
    }
  }
}

/*
ІСТОРІЯ ВЕРСІЙ:
v1.0 (25.01.2026) - Базовий функціонал.
v1.5 (13.02.2026) - WEB Інтерфейс, функція відкладених повідомлень.
v1.6 (20.10.2026) - змінено дизайн WEB Інтерфейса, оптимізація, покращення debug у Serial. (Update developed by Noctyon)

v1.6
- [NEW] Зміна дизайну Web-інтерфейса.
- [FIX] Оптимізація коду.
- [FIX] Покращено debug у Serial.

TO DO:
1. Забезпечити стабільну роботу якщо роутер перезавантажується при відключеннях світла
2. Додати можливість кастомізації
*/
