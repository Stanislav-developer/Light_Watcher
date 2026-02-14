/* Light-Watcher firmware V1.5
Repository: https://github.com/Stanislav-developer/Light_Watcher
Author: Stanislav Turii (GitHub: https://github.com/Stanislav-developer || Youtube: https://www.youtube.com/@TehnoMaisterna)
Date: 2026.02.13

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

// Дані для конфігурації(можна прописати зразу або заповнити пізніше при налаштуванні у Веб Інтерфейсі)
String ssid = ""; // Назва WiFi мережі  
String password = ""; // Пароль до WiFi мережі
String botToken = ""; // API Токен бота
String chatId = ""; // Чат ID власника бота 
String groupId = ""; // Чат ID групи

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
bool powerStatus = true; // true - присутня мережа, false - відсутня
bool messageFlag = false; // true - повідомлення про відключення вже надіслано, false - ще не надіслано
bool lastOutageDetect = false; // Останнє детектування зникнення електромережі, потрібне на випадок якщо батарея розрядиться
bool missMessage = false; // Прапорець який вказує що повідомлення про появу світла не надіслалось
int readValue = 0; // Значення АЦП з аналогового піну (0-4095)
int powerOutageCount = 0; // К-сть відключень електроенергії за весь час(зберігається у Preferences)

unsigned long powerOffTime = 0; // Час відключення світла в мілісекундах (millis)
unsigned long powerOnTime = 0; // Час появи світла в мілісекундах (millis)
time_t powerOffTimestamp = 0; // Unix timestamp відключення (секунди з 1970)
time_t powerOnTimestamp = 0; // Unix timestamp появи світла (секунди з 1970)
time_t currentTimestamp = 0; // Поточний Unix timestamp для розрахунків

String currentTZ = ""; // Поточний часовий пояс (EEST-2 або EEST-3)
String powerOffFormattedTime; // Відформатований час відключення (дд.мм.рррр чч:хх:сс)
String powerOnFormattedTime; // Відформатований час появи світла (дд.мм.рррр чч:хх:сс)
String lastMissMessage; // Змінна у якій лежить сформоване повідомлення про появу світла(якщо wifi мережа була відсутня)

// HTML код розмітки WEB Інтерфейсу
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
  <div class="header"><h1>Light Watcher V1.5</h1></div>
  <div class="container">
    <h3>Налаштування пристрою</h3>
    <h4>Будь ласка, коректно введіть усі дані!</h4>
    <form action="/save" method="POST">
      <label>WiFi Назва (SSID)</label><input type="text" name="ssid" placeholder="Назва мережі">
      <label>WiFi Пароль</label><input type="password" name="pass" placeholder="Пароль">
      <label>Telegram Bot Token</label><input type="text" name="token" placeholder="Токен бота">
      <label>Власник (Chat ID)</label><input type="text" name="owner" placeholder="ID власника">
      <label>Група (Group ID)</label><input type="text" name="group" placeholder="ID групи (необов'язково)">
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

// Функція відображення сторінки
void handleRoot() {
  server.send(200, "text/html", index_html);
}

// Функція збереження даних
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("token") && server.hasArg("owner")) {
    String n_ssid = server.arg("ssid");
    String n_pass = server.arg("pass");
    String n_token = server.arg("token");
    String n_owner = server.arg("owner");
    String n_group = server.arg("group");

    if (n_ssid.length() > 0 && n_pass.length() > 0) {
      preferences.putString("ssid", n_ssid);
      preferences.putString("pass", n_pass);
      preferences.putString("token", n_token);
      preferences.putString("chatId", n_owner);
      preferences.putString("groupId", n_group);

      String response = "<html><head><meta charset='utf-8'></head><body style='text-align:center; font-family:Arial;'>";
      response += "<h1 style='color:green;'>Збережено! ✅</h1>";
      response += "<p>Пристрій перезавантажується...</p></body></html>";
      server.send(200, "text/html", response);
      
      delay(2000);
      ESP.restart();
    }
  }
  server.send(400, "text/plain", "Error: Missing data");
}

// Функція перевірки стану електромережі
bool checkPowerStatus() {
  readValue = analogRead(readPin);
  return readValue >= 3000; // Повертає true якщо електромережа присутня
}

//Налаштування літнього/зимового часу
void applyTimezone(String tz) {
  setenv("TZ", tz.c_str(), 1); // Встановлюємо часовий пояс у системі (конвертуємо String → const char*)
  tzset(); // Застосовуємо зміни часового поясу
  currentTZ = tz; // Зберігаємо поточний часовий пояс
}

// Конвертація секунд Unix timestamp у кількість днів, годин, хвилин.
String formatDuration(time_t seconds) {
  unsigned long days = seconds / 86400;
  unsigned long hours = (seconds % 86400) / 3600;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long secs = seconds % 60;
  
  String result = "";
  
  if (days > 0) {
    result += String(days) + " д. ";
  }
  if (hours > 0 || days > 0) {
    result += String(hours) + " год. ";
  }
  if (minutes > 0 || hours > 0 || days > 0) {
    result += String(minutes) + " хв. ";
  }
  result += String(secs) + " сек.";
  
  return result; // повертаємо рядок із вже відформатованим часом, приклад: 7 хв. 22 сек
}

//Отримання вже відформатованої дати та часу для відстеження моменту вимкнення електроенергії
String getFormattedTime() {
  struct tm timeinfo;
  // Отримуємо синхронізований час з внутрішнього RTC esp32
  if (!getLocalTime(&timeinfo)) {
    return "Час не синхронізовано";
  }
  
  char timeStr[32]; // Створюємо масив символів для запису дати та часу
  strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M:%S", &timeinfo); // Заповнюємо масив

  return String(timeStr);
}

// Функція перевірки та підтримки стабільного WiFi з'єднання
void checkWiFi() {

  static unsigned long lastWiFiCheck = 0;
  unsigned long interval = 30000; // Перевіряємо кожні 30 секунд

  if (millis() - lastWiFiCheck > interval) {
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
      else 
      {
        Serial.println("Не вдалося відправити. Ще одна спроба через 30 сек...");
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
    while (true) {
      dnsServer.processNextRequest();
      server.handleClient();
      delay(5); // Невелика затримка для стабільності
    }
}

// Функція обробки вхідних повідомлень
void handleNewMessages() {
  String chat_id = String(bot.messages[0].chat_id); // Отримуємо chatID користувача або групи
  String text = bot.messages[0].text; // Отримуємо текст повідомлення
  String from_name = bot.messages[0].from_name; // Отримуємо ім'я користувача
  
  Serial.println("Отримано: " + text + " від " + from_name + " ID: " + chat_id);

  //Повідомлення "Світло є чи нема?" публічне.
  if((chat_id == chatId || chat_id == groupId) && text == "Світло є чи нема?"){
    if(checkPowerStatus()){
      String ask_message = "🟢 <b>СВІТЛО Є!</b>\n\n";
      ask_message += "🕐 Час відновлення: " + powerOnFormattedTime + "\n";
      bot.sendMessage(chat_id, ask_message, "HTML");
    }
    else{
      String ask_message = "🔴 <b>СВІТЛА НЕМАЄ</b>\n\n";
      ask_message += "⏱ Тривалість відключення: " + formatDuration(time(&currentTimestamp) - powerOffTimestamp) + "\n";
      ask_message += "🕐 Час відключення: " + powerOffFormattedTime;
      bot.sendMessage(chat_id, ask_message, "HTML");
    }
  }

  // Приватні повідомлення, може писати тільки власник бота.
  if (chat_id == chatId && text != "Світло є чи нема?") {
    if (text == "/help") {
      String help_message = "👋 Привіт, " + from_name + "!\n\n";
      help_message += "Доступні команди:\n";
      help_message += "/info - Про бота\n";
      help_message += "/status - Стан системи\n";
      help_message += "/set_summer_time - Встановити літній час\n";
      help_message += "/set_winter_time - Встановити зимовий час\n";
      help_message += "/clear_data  - Очистити данні конфігураціїї та статистику\n";
      help_message += "/restart - Віддалений перезапуск бота";
      bot.sendMessage(chat_id, help_message, "");
    }
    
    else if (text == "/info") {
      String info = "⚡ <b>Light Watcher</b> v1.5\n\n";
      info += "<b>Автоматичний моніторинг електромережі</b>\n\n";
      
      info += "🤖 Що робить бот:\n";
      info += "• Повідомляє про відключення світла\n";
      info += "• Рахує час без електрики\n";
      info += "• Працює від акумулятора\n";
      info += "• Зберігає статистику\n\n";
      info += "• Сторінка проекту:\n";

      info += "📦 <a href='https://github.com/Stanislav-developer/Light_Watcher'>GitHub Repository</a>\n";
      bot.sendMessage(chat_id, info, "HTML");
    }
    
    else if (text == "/status") {
      String status_message = "Стан системи: \n";
      if (checkPowerStatus()){
        status_message += "Електромережа: присутня\n";
      }
      else{
        status_message += "Електромережа: відсутня\n";
      }
      status_message += "К-сть вимкнень електроенергії: " + String(powerOutageCount) + "\n";
      status_message += "Поточний час: " + getFormattedTime() + "\n";
      if(currentTZ == "EEST-3"){
        status_message += "Встановлено літній час (UTC+3)\n";
      }
      else{
        status_message += "Встановлено зимовий час (UTC+2)\n";
      }

      unsigned long uptime = millis() / 1000; // Отримуємо значення millis() у мілісекундах та перетворюємо у секунди
      status_message += "Пристрій працює: " + formatDuration(uptime); // К-сть секунд конвертуємо у дні, години, хвлини...
      bot.sendMessage(chat_id, status_message, "");
    }
    
    else if (text == "/set_summer_time") {
      currentTZ = "EEST-3";
      preferences.putString("tz-rule", currentTZ);
      applyTimezone(currentTZ);
      String set_summer_time_msg = "Встановлено літній час(UTC+3)";
      bot.sendMessage(chat_id, set_summer_time_msg, "");
    }

    else if (text == "/set_winter_time") {
      currentTZ = "EEST-2";
      preferences.putString("tz-rule", currentTZ);
      applyTimezone(currentTZ);
      String set_winter_time_msg = "Встановлено зимовий час (UTC+2)";
      bot.sendMessage(chat_id, set_winter_time_msg, "");
    }
    
    else if (text == "/clear_data") {
      preferences.clear(); // Очищення даних у енергонезалежній пам'яті
      // Записуємо 1 як дефолтне значення
      preferences.putString("ssid", "1");
      preferences.putString("pass", "1");
      preferences.putString("token", "1");
      preferences.putString("chatId", "1");
      preferences.putString("groupId", "1");
      String clear_data_message = "Дані конфігурації та статистика очищені.Перезапуск у режим конфігурації";
      bot.sendMessage(chat_id, clear_data_message, "");
      delay(1000);
      ESP.restart(); 
    }
    
    else if (text == "/restart") {
      bot.sendMessage(chat_id, "Перезапуск...", "");
      delay(1000);
      ESP.restart(); // Кидаємо ESP32 у reset
    }
    
    else {
      bot.sendMessage(chat_id, "❓ Невідома команда. Список команд: /help", "");
    }
  }
}

// Функція у якій код виконується лише раз при старті програми
void setup() {

  pinMode(readPin, INPUT);
  analogSetAttenuation(ADC_11db); // За верхню межу АЦП беремо 3.3В

  // Налаштування запасної кнопки скидання скидання конфігураціїї (кнопка Boot на ESP32-C3 зазвичай GPIO 9, на інших платах ESP це GPIO 0)
  pinMode(9, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println("Start");
  
  preferences.begin("light-watcher", false); // Ініціалізуємо namespace для роботи з preferences

  delay(3000); // Невелика затримка перед тим як користувач затисне кнопку boot для оновлення конфігурації

  // Якщо при запуску затиснута кнопка BOOT (GPIO 9) - запускаємо WEB Сервер для кофігурації
  if (digitalRead(9) == LOW) {
    Serial.println("Кнопка Boot затиснута. Запуск конфігурації...");
    launchWebServer();
  }

  // Завантаження даних у змінні(Якщо у preferences ніякої інформації немає, залишаємо те що ми прописали у коді)
  ssid = preferences.getString("ssid", ssid);
  password = preferences.getString("pass", password);
  botToken = preferences.getString("token", botToken);
  chatId = preferences.getString("chatId", chatId);
  groupId = preferences.getString("groupId", groupId);

  // Перевіряємо, чи дійсно є дані конфігурації
  if (ssid == "1" || password == "1" || botToken == "1" || chatId == "1" || ssid == "" || password == "" || botToken == "" || chatId == "") {
    Serial.println("Дані конфігурації відсутні. Запуск Web Server...");
    launchWebServer(); // Запускаємо WEB Сервер для конфігурації
  }

  //Якщо ми тут - значить конфігурація завершилася
  Serial.println("Конфігурацію завантажено.");
  
  // Оновлюємо токен бота (бо бібліотека могла ініціалізуватись з пустим)
  bot.updateToken(botToken);

  // Завантажуємо збережені дані змінних з енергонезалежної пам'яті
  powerOutageCount = preferences.getInt("powerOutageCnt", 0);
  currentTZ = preferences.getString("tz-rule", "EEST-2");
  powerOffTimestamp = preferences.getLong64("pwrOffTmstmp", 0);
  lastOutageDetect = preferences.getBool("lastUotDetect", false);

  Serial.println("Дані з preferences завантажено.");

  WiFi.mode(WIFI_STA); // Перемикаємось на стандартний режим Клієнта

  //Під'єднуємось до WiFi
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.print("Під'єднання до WiFi...");

  int wifi_attempts = 0;
  // Даємо 50 спроб на під'єднання до WiFi
  while (WiFi.status() != WL_CONNECTED && wifi_attempts <= 50) {
    wifi_attempts++;
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  if(WiFi.status() != WL_CONNECTED){
    Serial.println("WiFi connection error!");
  }
  else{
    Serial.println("WiFi Connected!");
  }
  
  // Синхронізуємо час з NTP
  configTime(0, 0, ntp1, ntp2, ntp3);
  applyTimezone(currentTZ);

  Serial.print("Синхронізація часу...");

  struct tm timeinfo;

  int attempts = 0; 

  while (!getLocalTime(&timeinfo) && attempts < 5) { 
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println("");

  if(!getLocalTime(&timeinfo)){
    Serial.println("Час не синхронізовано");
  }
  else{
    Serial.println("Час синхронізовано");
  }
  
  client.setInsecure();

  client.setTimeout(4); // Зменшуємо Timeout на відправку повідомлень, щоб не переповнювати Watchdog
  
  powerStatus = checkPowerStatus(); // Перевіряємо статус живлення
  
  // Якщо було замічено вимкнення електроенергії але світло наявне(Вірогідно акумулятор розрядився)
  if (lastOutageDetect && powerStatus){
    messageFlag = false;
    time(&powerOnTimestamp);

    unsigned long outageSeconds = powerOnTimestamp - powerOffTimestamp;
    powerOnFormattedTime = getFormattedTime();
    
    String message = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ</b>\n\n";
    message += "🕐 Час відновлення: " + powerOnFormattedTime + "\n";
    message += "⏱ Тривалість відключення: " + formatDuration(outageSeconds) + "\n";
    message += "⚠️ Заряд акумулятора був критичний та пристрій вимкнувся...\n";
    message += "🔌 Живлення від мережі відновлено\n";

    lastOutageDetect = false;
    preferences.putBool("lastUotDetect", lastOutageDetect);
    
    bot.sendMessage(chatId, message, "HTML");
    bot.sendMessage(groupId, message, "HTML");
    Serial.println("Повідомлення про відновлення надіслано");
  } 

  else {
    String startMessage = "⚡ <b>Light Watcher активовано</b>\n\n";
    startMessage += "🕐 Час запуску: " + getFormattedTime() + "\n";
    startMessage += "💡 Електромережа: ";
    
    if (powerStatus) {
      startMessage += "<b>ПРИСУТНЯ</b> ✅\n";
    } 
    else {
      startMessage += "<b>ВІДСУТНЯ</b> ⚠️\n";
      startMessage += "⚠️ Пристрій запущено на резервному живленні";
      powerOffTime = millis();
      messageFlag = true;
    }
    
    if(bot.sendMessage(chatId, startMessage, "HTML")){
      Serial.println("Стартове повідомлення надіслано");
    }
    else{
      Serial.println("Стартове повідомлення не надіслано, введена інформація не правильна");
      launchWebServer(); // Запускаємо WEB Server для конфігураціїї (Вірогідно дані WiFi та Telegram API було введено невірно)
    }
  }

  // Щоб бот не відповідав на всі попередні повідомлення, чистимо чергу
  int newMessage = bot.getUpdates(-1);
  if (newMessage > 0) {
    bot.last_message_received = bot.messages[0].update_id;
  }
}

// Головна функція у якій код виконується постійно
void loop() {

  // Перевіряємо WiFi з'єднання
  checkWiFi();

  // Перевіряємо стан електромережі
  bool currentPowerStatus = checkPowerStatus();

  // Якщо світло вимкнули
  if (!currentPowerStatus && messageFlag == false) {
    messageFlag = true;
    powerOffTime = millis(); 

    time(&powerOffTimestamp); // Зберігаємо Unix timestamp коли вимкнули світло для подальших розрахунків
    preferences.putLong64("pwrOffTmstmp", (int64_t)powerOffTimestamp);
    lastOutageDetect = true; // Встановлюємо на true що вимкнення вже відбулося
    preferences.putBool("lastUotDetect", lastOutageDetect);

    powerOffFormattedTime = getFormattedTime(); // Отримуємо точну дату та час коли вимкнули світло

    powerOutageCount++; // Збільшуємо к-сть вимкнень світла на 1
    preferences.putInt("powerOutageCnt", powerOutageCount); 

    String message = "🔴 <b>СВІТЛО ВИМКНУЛИ</b>\n\n";
    message += "🕐 Час відключення: " + getFormattedTime() + "\n";
    
    bot.sendMessage(chatId, message, "HTML"); //Відсилаємо повідомлення у бота
    bot.sendMessage(groupId, message, "HTML"); //Відсилаємо повідомлення у групу
    Serial.println("Повідомлення про відключення надіслано");
  }
  
  // Якщо світло увімкнули
  else if (currentPowerStatus && messageFlag == true) {
    messageFlag = false;
    powerOnTime = millis();
    time(&powerOnTimestamp);// Зберігаємо Unix timestamp коли увімкнули світло для подальших розрахунків

    lastOutageDetect = false; // Скидаємо прапорець
    preferences.putBool("lastUotDetect", lastOutageDetect);

    unsigned long outageSeconds = (powerOnTime - powerOffTime) / 1000; // Рахуємо к-сть пройдених секунд з моменту відключення

    powerOnFormattedTime = getFormattedTime(); // Розраховуємо тривалість вимкнення
    
    String message = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ</b>\n\n";
    message += "🕐 Час відновлення: " + powerOnFormattedTime + "\n";
    message += "⏱ Тривалість відключення: " + formatDuration(outageSeconds) + "\n";

    // Перевіряємо наявність WiFi мережі
    if (WiFi.status() != WL_CONNECTED){
      missMessage = true;
      message += "Світло вимкнули: " + powerOffFormattedTime + "\n";
      message += "📵 Мережа WiFi була відсутня\n";
      lastMissMessage = message;// Архівуємо повідомлення, щоб відправити його як тільки WiFi з'явиться
      Serial.println("Повідомлення про відновлення не надіслано");
    } 
    else{
      bot.sendMessage(chatId, message, "HTML");
      bot.sendMessage(groupId, message, "HTML");
      Serial.println("Повідомлення про відновлення надіслано");
    }
  }

  static unsigned long lastBotCheck = 0;

  // Перевіряємо наявність нових повідомлень кожну секунду
  if (millis() - lastBotCheck > 1000) {
    int newMessage = bot.getUpdates(-1); // Читаємо тільки останнє надіслане повідомлння з черги
    
    // Якщо повідомлення є, відсилаємо його на обробку
    if (newMessage) {
      handleNewMessages();
    }
    
    lastBotCheck = millis();
  }
}

/*
ІСТОРІЯ ВЕРСІЙ:
v1.0 (25.01.2026) - Базовий функціонал.
v1.5 (13.02.2026) - WEB Інтерфейс, функція відкладених повідомлень.

v1.5 (13.02.2026)
- [NEW] Web-інтерфейс для налаштування (WiFi, Token, ChatID) без перепрошивки.
- [NEW] Режим "Точка доступу" (AP) при першому запуску або відсутності конфігурації.
- [FIX] Система відкладених повідомлень: сповіщення про відновлення світла надсилається після підключення до WiFi (актуально, коли роутер довго вантажиться).
- [NEW] Функція скидання до заводських налаштувань (утримання кнопки BOOT при старті).

TO DO:
1. Забезпечити стабільну роботу коду(якщо роутер перезавантажується при відключеннях світла)
2. Краще оптимізувати код, щоб повідомлення відсилались швидше. Забезпечити щоб алгоритми були стабільними
3. Краще організувати дебаг у Serial port

*/