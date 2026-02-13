/* Light-Watcher firmware V1.0
Repository: https://github.com/Stanislav-developer/Light_Watcher
Author: Stanislav Turii (GitHub: https://github.com/Stanislav-developer || Youtube: https://www.youtube.com/@TehnoMaisterna)
Date: 2026.01.25

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

// Конфігурація пінів
#define readPin 3 // Аналоговий пін, вимірює рівень заряду на вході(детектує наявність електромережі). Можна використовувати будь який пін на 1АЦП

// Константи:
const char* ssid = " "; // Назва домашньої WiFi мережі
const char* password = " "; // Пароль домашньої WiFi мережі
const char* botToken = " "; // Токен бота
const char* chatId = " "; // ChatID Власника бота
const char* groupId = " "; // ChatID Загальної групи

const char* ntp1 = "pool.ntp.org"; // 1 NTP сервер реального часу
const char* ntp2 = "time.google.com"; // 2 NTP сервер реального часу(резерв)
const char* ntp3 = "time.cloudflare.com"; // 3 NTP сервер реального часу(резерв)

//Об'єкти:
Preferences preferences; // Створюємо об'єкт для роботи з preferences
WiFiClientSecure client; // Створюємо об'єкт для передачі та отримання повідомлень з Telegram.
UniversalTelegramBot bot(botToken, client);

// Глобальні змінні
bool powerStatus = true; // true - присутня мережа, false - відсутня
bool messageFlag = false; // true - повідомлення про відключення вже надіслано, false - ще не надіслано
bool lastOutageDetect = false; // Останнє детектування зникнення електромережі, потрібне на випадок якщо батарея розрядиться
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

// Функція перевірку стану підключення до WiFi та при потребі перепідключення задля стабільного з'єднання
void checkWiFi() {
  static unsigned long lastWiFiCheck = 0;
  unsigned long interval = 30000; // Перевіряємо наявність WiFi кожні 30 сек

  if (millis() - lastWiFiCheck > interval) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("Wi-Fi втрачено! Перепідключення...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      
      int retry = 0; // К-сть спроб перепід'єднання
      while (WiFi.status() != WL_CONNECTED && retry < 3) {
        delay(500);
        Serial.print(".");
        retry++;
      }
      Serial.println("");
      Serial.println("WiFi підключення відновлено");
    }
    lastWiFiCheck = millis();
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
      ask_message += "🕐 Час відновлення: " + powerOnFormattedTime.substring(11) + "\n";
      ask_message += "⏱ Воно є: " + formatDuration(time(&currentTimestamp) - powerOnTimestamp) + "\n";
      bot.sendMessage(chat_id, ask_message, "HTML");
    }
    else{
      String ask_message = "🔴 <b>СВІТЛА НЕМАЄ</b>\n\n";
      ask_message += "🕐 Час відключення: " + powerOffFormattedTime.substring(11);
      ask_message += "⏱ Його нема: " + formatDuration(time(&currentTimestamp) - powerOffTimestamp) + "\n";
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
      help_message += "/clear_data  - Очистити статистику\n";
      help_message += "/restart - Віддалений перезапуск бота";
      bot.sendMessage(chat_id, help_message, "");
    }
    
    else if (text == "/info") {
      String info = "⚡ <b>Light Watcher</b> v1.0\n\n";
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
      String clear_data_message = "Статистика очищена";
      bot.sendMessage(chat_id, clear_data_message, "");
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

void setup() {

  pinMode(readPin, INPUT);
  analogSetAttenuation(ADC_11db); // За верхню межу АЦП беремо 3.3В

  Serial.begin(115200);
  Serial.println("Start");

  preferences.begin("light-watcher", false); // Створюємо namespace для роботи з preferences
  // Одразу завантажуємо збережені дані змінних з енергонезалежної пам'яті
  powerOutageCount = preferences.getInt("powerOutageCnt", 0);
  currentTZ = preferences.getString("tz-rule", "EEST-2");
  powerOffTimestamp = preferences.getLong64("pwrOffTmstmp", 0);
  lastOutageDetect = preferences.getBool("lastUotDetect", false);
  Serial.println("Дані з preferences завантажено.");
  
  //Під'єднуємось до WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Connected!");

  // Синхронізуємо час з NTP
  configTime(0, 0, ntp1, ntp2, ntp3);
  applyTimezone(currentTZ);
  Serial.print("Синхронізація часу...");
  struct tm timeinfo;
  int attempts = 0; 
  while (!getLocalTime(&timeinfo) && attempts < 3) { 
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println("");

  Serial.println("Час синхронізовано.");
  
  client.setInsecure();
  
  powerStatus = checkPowerStatus();
  
  if (lastOutageDetect && powerStatus){
    messageFlag = false;
    time(&powerOnTimestamp);

    unsigned long outageSeconds = powerOnTimestamp - powerOffTimestamp;
    powerOnFormattedTime = getFormattedTime();
    
    String message = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ</b>\n\n";
    message += "🕐 Час відновлення: " + powerOnFormattedTime.substring(11) + "\n";
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
    bot.sendMessage(chatId, startMessage, "HTML");
    Serial.println("Стартове повідомлення надіслано");

  }

  // Щоб бот не відповідав на всі попередні повідомлення, чистимо чергу
  int newMessage = bot.getUpdates(-1);
  if (newMessage > 0) {
    bot.last_message_received = bot.messages[0].update_id;
  }

}

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

    unsigned long durationOnSeconds = (unsigned long)difftime(powerOffTimestamp, powerOnTimestamp);

    String message = "🔴 <b>СВІТЛО ВИМКНУЛИ</b>\n\n";
    message += "🕐 Час відключення: " + getFormattedTime().substring(11) + "\n";
    message += "⏱ Воно було: " + formatDuration(durationOnSeconds) + "\n";
    
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

    unsigned long outageSeconds = (powerOnTime - powerOffTime) / 1000; 

    powerOnFormattedTime = getFormattedTime(); // Розраховуємо тривалість вимкнення
    
    String message = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ</b>\n\n";
    message += "🕐 Час відновлення: " + powerOnFormattedTime.substring(11) + "\n";
    message += "⏱ Його не було: " + formatDuration(outageSeconds) + "\n";
    
    bot.sendMessage(chatId, message, "HTML");
    bot.sendMessage(groupId, message, "HTML");
    Serial.println("Повідомлення про відновлення надіслано");
  }

  // Перевіряємо наявність нових повідомлень кожну секунду
  static unsigned long lastBotCheck = 0;
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

TO DO:
1.
2.
3.

*/
