#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <time.h>
#include <LittleFS.h> 

/* Light-Watcher firmware V1.0 (Adapted for ESP8266)
Repository: https://github.com/Stanislav-developer/Light_Watcher
Author: Stanislav Turii
Date: 2026.01.25
*/

// Конфігурація пінів
#define readPin A0

// Константи:
const char* ssid = " "; 
const char* password = " "; 
const char* botToken = " "; 
const char* chatId = " "; 
const char* groupId = " "; 

const char* ntp1 = "pool.ntp.org"; 
const char* ntp2 = "time.google.com"; 
const char* ntp3 = "time.cloudflare.com"; 

// Об'єкти:
WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Глобальні змінні
bool powerStatus = true; 
bool messageFlag = false; 
bool lastOutageDetect = false; 
int readValue = 0; 
int powerOutageCount = 0; 

unsigned long powerOffTime = 0; 
unsigned long powerOnTime = 0; 
time_t powerOffTimestamp = 0; 
time_t powerOnTimestamp = 0; 
time_t currentTimestamp = 0; 

String currentTZ = "EEST-2"; 
String powerOffFormattedTime; 
String powerOnFormattedTime; 

void saveData() {
  File f = LittleFS.open("/config.dat", "w");
  if (f) {
    f.println(powerOutageCount);
    f.println(currentTZ);
    f.println((long long)powerOffTimestamp);
    f.println(lastOutageDetect);
    f.close();
  }
}

void loadData() {
  if (LittleFS.exists("/config.dat")) {
    File f = LittleFS.open("/config.dat", "r");
    if (f) {
      powerOutageCount = f.readStringUntil('\n').toInt();
      currentTZ = f.readStringUntil('\n'); currentTZ.trim();
      powerOffTimestamp = (time_t)f.readStringUntil('\n').toInt();
      lastOutageDetect = f.readStringUntil('\n').toInt();
      f.close();
    }
  }
}

// Функція перевірки стану електромережі
bool checkPowerStatus() {
  readValue = analogRead(readPin);
  return readValue >= 700; 
}

void applyTimezone(String tz) {
  setenv("TZ", tz.c_str(), 1); 
  tzset(); 
  currentTZ = tz; 
}

String formatDuration(time_t seconds) {
  unsigned long days = seconds / 86400;
  unsigned long hours = (seconds % 86400) / 3600;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long secs = seconds % 60;
  String result = "";
  if (days > 0) result += String(days) + " д. ";
  if (hours > 0 || days > 0) result += String(hours) + " год. ";
  if (minutes > 0 || hours > 0 || days > 0) result += String(minutes) + " хв. ";
  result += String(secs) + " сек.";
  return result; 
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Час не синхронізовано";
  char timeStr[32]; 
  strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M:%S", &timeinfo); 
  return String(timeStr);
}

void checkWiFi() {
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
    lastWiFiCheck = millis();
  }
}

void handleNewMessages() {
  String chat_id = String(bot.messages[0].chat_id); 
  String text = bot.messages[0].text; 
  String from_name = bot.messages[0].from_name; 

  if((chat_id == chatId || chat_id == groupId) && text == "Світло є чи нема?"){
    if(checkPowerStatus()){
      String msg = "🟢 <b>СВІТЛО Є! 👍 </b>\n\n🕐 Час відновлення: " + powerOnFormattedTime;
      bot.sendMessage(chat_id, msg, "HTML");
    } else {
      String msg = "🔴 <b>СВІТЛА НЕМАЄ 😡</b>\n\n⏱ Тривалість: " + formatDuration(time(nullptr) - powerOffTimestamp);
      msg += "\n🕐 Час відключення: " + powerOffFormattedTime;
      bot.sendMessage(chat_id, msg, "HTML");
    }
  }

  if (chat_id == chatId && text != "Світло є чи нема?") {
    if (text == "/help") {
      String msg = "👋 Привіт, " + from_name + "!\n\nДоступні команди:\n/info\n/status\n/set_summer_time\n/set_winter_time\n/clear_data\n/restart";
      bot.sendMessage(chat_id, msg, "");
    }
    else if (text == "/info") {
      String info = "⚡ <b>Light Watcher</b> v1.0\n📦 <a href='https://github.com/Stanislav-developer/Light_Watcher'>GitHub</a>";
      bot.sendMessage(chat_id, info, "HTML");
    }
    else if (text == "/status") {
      String msg = "Стан системи: \nЕлектромережа: " + String(checkPowerStatus() ? "присутня" : "відсутня") + "\n";
      msg += "Вимкнень: " + String(powerOutageCount) + "\nПоточний час: " + getFormattedTime() + "\n";
      msg += (currentTZ == "EEST-3") ? "Літній час (UTC+3)\n" : "Зимовий час (UTC+2)\n";
      msg += "Uptime: " + formatDuration(millis() / 1000);
      bot.sendMessage(chat_id, msg, "");
    }
    else if (text == "/set_summer_time") {
      applyTimezone("EEST-3"); saveData();
      bot.sendMessage(chat_id, "Встановлено літній час (UTC+3)", "");
    }
    else if (text == "/set_winter_time") {
      applyTimezone("EEST-2"); saveData();
      bot.sendMessage(chat_id, "Встановлено зимовий час (UTC+2)", "");
    }
    else if (text == "/clear_data") {
      powerOutageCount = 0; lastOutageDetect = false; saveData();
      bot.sendMessage(chat_id, "Статистика очищена", "");
    }
    else if (text == "/restart") {
      bot.sendMessage(chat_id, "Перезапуск...", "");
      delay(1000); ESP.restart();
    }
  }
}

void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  loadData();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  configTime(0, 0, ntp1, ntp2, ntp3);
  applyTimezone(currentTZ);
  client.setInsecure(); // Важливо для ESP8266

  powerStatus = checkPowerStatus();
  
  if (lastOutageDetect && powerStatus){
    messageFlag = false;
    time(&powerOnTimestamp);
    powerOnFormattedTime = getFormattedTime();
    String message = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ 👍</b>\n\n🕐 Відновлено: " + powerOnFormattedTime;
    message += "\n⏱ Тривалість: " + formatDuration(powerOnTimestamp - powerOffTimestamp);
    message += "\n🔌 Живлення відновлено (після розряду АКБ)";
    lastOutageDetect = false; saveData();
    bot.sendMessage(chatId, message, "HTML");
    bot.sendMessage(groupId, message, "HTML");
  } else {
    String msg = "⚡ <b>Watcher активовано</b>\n🕐 " + getFormattedTime();
    msg += "\n💡 Мережа: " + String(powerStatus ? "<b>ПРИСУТНЯ 👍</b>" : "<b>ВІДСУТНЯ 😡</b>");
    if(!powerStatus) { messageFlag = true; powerOffTime = millis(); }
    bot.sendMessage(chatId, msg, "HTML");
  }

  int newMessage = bot.getUpdates(-1);
  if (newMessage > 0) bot.last_message_received = bot.messages[0].update_id;
}

void loop() {
  checkWiFi();
  bool currentPowerStatus = checkPowerStatus();

  if (!currentPowerStatus && !messageFlag) {
    messageFlag = true;
    powerOffTime = millis(); 
    time(&powerOffTimestamp);
    lastOutageDetect = true;
    powerOutageCount++;
    saveData();
    powerOffFormattedTime = getFormattedTime();
    String msg = "🔴 <b>СВІТЛО ВИМКНУЛИ 🤬</b>\n🕐 " + powerOffFormattedTime;
    bot.sendMessage(chatId, msg, "HTML");
    bot.sendMessage(groupId, msg, "HTML");
  }
  
  else if (currentPowerStatus && messageFlag) {
    messageFlag = false;
    time(&powerOnTimestamp);
    lastOutageDetect = false; saveData();
    unsigned long outageSeconds = (millis() - powerOffTime) / 1000; 
    powerOnFormattedTime = getFormattedTime();
    String msg = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ 👍</b>\n🕐 " + powerOnFormattedTime;
    msg += "\n⏱ Тривалість: " + formatDuration(outageSeconds);
    bot.sendMessage(chatId, msg, "HTML");
    bot.sendMessage(groupId, msg, "HTML");
  }

  static unsigned long lastBotCheck = 0;
  if (millis() - lastBotCheck > 1000) {
    int newMessage = bot.getUpdates(bot.last_message_received + 1);
    if (newMessage) handleNewMessages();
    lastBotCheck = millis();
  }
}