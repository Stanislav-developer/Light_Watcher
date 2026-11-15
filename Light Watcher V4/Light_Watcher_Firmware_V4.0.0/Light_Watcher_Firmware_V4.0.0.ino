#include "lw.h"

void setup() {

  pinMode(readPin, INPUT);
  analogSetAttenuation(ADC_11db);

  pinMode(9, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println("Start");

  const int nvsMigrated = otaMigrateNvs();
  const bool nvsReady = nvsOpenWithRetry(5);
  wdtBegin();
  if (!nvsReady) {
    Serial.println("NVS недоступне: налаштування неможливо прочитати. Перезапуск через 30 с.");
    unsigned long waitStart = millis();
    while (millis() - waitStart < 30000UL) {
      wdtFeed();
      delay(100);
    }
    ESP.restart();
  }
  crashCount();
  logBegin();
  if (nvsMigrated > 0) {
    logEvent(LC_STORE, LS_INFO, "Таблиця розділів з OTA: налаштування й статистику перенесено (" + String(nvsMigrated) + " записів)");
  } else if (nvsMigrated < 0) {
    logEvent(LC_STORE, LS_ERR, "Перенесення NVS у нову таблицю неповне");
  }
  otaReportBegin();
  cfgCommitRecover();
  crashBegin();

  delay(3000);

  if (digitalRead(9) == LOW) {
    Serial.println("Кнопка Boot затиснута. Запуск конфігурації...");

    bool longHold = true;
    unsigned long holdStart = millis();
    while (millis() - holdStart < 10000UL) {
      wdtFeed();
      if (digitalRead(9) != LOW) {
        longHold = false;
        break;
      }
      delay(50);
    }
    launchWebServer(!longHold && cfgGet(0, "").length() > 0);
  }

  ssid = cfgGet(0, ssid);
  password = cfgGet(1, password);
  botToken = cfgGet(2, botToken);
  chatId = cfgGet(3, chatId);
  groupId = cfgGet(4, groupId);
  shellyIP = cfgGet(5, shellyIP);

  if (ssid == "" || botToken == "" || chatId == "") {
    Serial.println("Дані конфігурації відсутні. Запуск Web Server...");
    launchWebServer(false);
  }

  Serial.println("Конфігурацію завантажено.");

  bot.updateToken(botToken);
  bot.maxMessageLength = 4096;

  powerOutageCount = preferences.getInt("powerOutageCnt", 0);
  currentTZ = preferences.getString("tz-rule", "EEST-2");
  autoDst = preferences.getBool("autoDst", true);
  if (autoDst) {
    currentTZ = TZ_EUROPE_KYIV;
  }
  powerOffTimestamp = preferences.getLong64("pwrOffTmstmp", 0);
  lastOutageDetect = preferences.getBool("lastUotDetect", false);
  mqLoad();
  lastOnDurationText = preferences.getString("lastOnDur", "");
  lastOffDurationText = preferences.getString("lastOffDur", "");

  yasnoCity = preferences.getString("ysCity", "");
  yasnoStreet = preferences.getString("ysStreet", "");
  yasnoHouse = preferences.getString("ysHouse", "");
  yasnoGroup = preferences.getString("ysGroup", "");
  yasnoTodaySlotsJson = preferences.getString("ysToday", "[]");
  yasnoTomorrowSlotsJson = preferences.getString("ysTom", "[]");
  yasnoTodayDate = preferences.getString("ysDateT", "");
  yasnoTomorrowDate = preferences.getString("ysDateM", "");
  yasnoTodayStatus = preferences.getString("ysStatT", "");
  yasnoTomorrowStatus = preferences.getString("ysStatM", "");
  yasnoScheduleUpdatedOn = preferences.getString("ysUpd", "");
  yasnoScheduleFetchedAt = preferences.getUInt("ysFetch", 0);
  yasnoScheduleValid = yasnoGroup.length() && yasnoTodayDate.length() &&
                       yasnoTodaySlotsJson.startsWith("[") && yasnoTomorrowSlotsJson.startsWith("[");

  Serial.println("Дані з preferences завантажено.");

  bootId = esp_random();
  bootCount = preferences.getUInt("bootCnt", 0) + 1;
  preferences.putUInt("bootCnt", bootCount);
  bootRstWhy = preferences.getString("rstWhy", "");
  if (bootRstWhy.length() > 0) {
    preferences.remove("rstWhy");
  }
  prevAliveEpoch = preferences.getUInt("evAlive", 0);
  wifiDiscTotal = preferences.getUInt("wifiDiscN", 0);
  wifiConnTotal = preferences.getUInt("wifiConnN", 0);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  WiFi.begin(ssid.c_str(), password.c_str());
  WiFi.setSleep(false);

  Serial.print("Під'єднання до WiFi...");

  int wifi_attempts = 0;

  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 16) {
    wdtFeed();
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

  sntp_set_time_sync_notification_cb(ntpSyncCb);
  configTime(0, 0, ntp1, ntp2, ntp3);
  applyTimezone(currentTZ);

  Serial.print("Синхронізація часу...");

  struct tm timeinfo;

  bool timeSynced = (WiFi.status() == WL_CONNECTED) ? getLocalTime(&timeinfo, 3000) : getLocalTime(&timeinfo, 10);

  Serial.println("");

  if(!timeSynced){
    Serial.println("Час не синхронізовано");
  }
  else{
    Serial.println("Час синхронізовано");
  }

  wdtFeed();
  tlsSetup();

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  client.setTimeout(4000);
#else
  client.setTimeout(4);
#endif
  client.setHandshakeTimeout(10);

  tgCleanBegin();
  powerStatus = checkPowerStatus();
  statsBegin(powerStatus);
  evBegin(powerStatus);

  if (lastOutageDetect && powerStatus){
    messageFlag = false;

    batteryRecPending = true;
    batteryRecMs = millis();
    powerOnSinceBoot = false;
    Serial.println("Світло з'явилося, поки пристрій був вимкнений - повідомлення очікує синхронізації часу");
  }

  else {
    String startMessage = "⚡ <b>Light Watcher активний</b> · V" LW_VERSION "\n";
    startMessage += "🕒 Запущено: %BOOT%\n";

    if (powerStatus) {
      startMessage += "🟢 Світло є";
    }
    else {
      startMessage += "🔴 Світла немає\n";
      startMessage += "🔋 Пристрій працює від резервного живлення";
      powerOffTime = millis();
      messageFlag = true;
      offShortFix = false;
      offShortFixMs = 0;
      uint32_t savedOffEpoch = (stats.offOpen && stats.curOffStartEpoch != 0)
        ? stats.curOffStartEpoch
        : ((lastOutageDetect && powerOffTimestamp >= (time_t)EPOCH_MIN_VALID) ? (uint32_t)powerOffTimestamp : 0);
      if (savedOffEpoch != 0) {
        uint32_t nowEpoch = statsNowEpoch();
        bool sameDay = nowEpoch != 0 &&
          formatEpochLocal((time_t)savedOffEpoch, "%Y%j") == formatEpochLocal((time_t)nowEpoch, "%Y%j");
        powerOffShortTime = formatEpochLocal((time_t)savedOffEpoch, sameDay ? "%H:%M" : "%d.%m %H:%M");
      } else {
        powerOffShortTime = "невідомо";
      }
    }

    menuState = "main";
    startMsgText = startMessage;
    if (WiFi.status() != WL_CONNECTED || !timeSynced) {

      startMsgPending = true;
      Serial.println("Стартове повідомлення відкладено до появи зв'язку");
    }
    else if(tgSendText(chatId, startMessageText(), "HTML", tgReplyKbHint(menuKeyboard("main"), menuHint("main")))){
      tgCleanNoteStart((int32_t)bot.last_sent_message_id);
      Serial.println("Стартове повідомлення надіслано");
    }
    else{
      Serial.println("Стартове повідомлення не надіслано");

      const int tgErr = WiFi.status() == WL_CONNECTED ? telegramCredentialsError() : 0;
      if (tgErr == 401) {
        logEvent(LC_TG, LS_ERR, "Токен відхилено (401) - змініть у порталі");
      } else if (tgErr != 0) {
        logEvent(LC_TG, LS_ERR, "Чат власника не знайдено (" + String(tgErr) +
                 "): /start або Chat ID");
      }
      startMsgPending = true;
    }
  }

  if (powerStatus) {
    powerOnSinceMs = millis();
    powerOnShortTime = getShortTime();
    onShortFix = (powerOnShortTime == "--:--");
    onShortFixMs = millis();
    onRestorePending = true;
    restoreOnPeriodFromStats();
  }

  chatsBegin();

  webBegin();

  srvBegin();
}

void loop() {
  tgSendAttemptedThisPass = false;
  wdtFeed();
  static unsigned long loopPrevMs = 0;
  unsigned long loopNowMs = millis();
  if (loopPrevMs != 0 && loopNowMs - loopPrevMs > loopMaxMs) {
    loopMaxMs = loopNowMs - loopPrevMs;
  }
  loopPrevMs = loopNowMs;
  logTick();
  crashTick();

  checkWiFi();
  ecoTick();

  statsTick();
  eventsTimeFixTick();

  bool currentPowerStatus = checkPowerStatus();
  if (!currentPowerStatus && !messageFlag && blipFilter()) {
    currentPowerStatus = true;
  }
  powerStatus = currentPowerStatus;

  if (!currentPowerStatus && messageFlag == false) {
    processBatteryRecovery(true);

    powerOffTime = powerEdgeMs();
    const bool offNotified = blipOffNotified(powerOffTime);
    messageFlag = true;

    time(&powerOffTimestamp);
    if (powerOffTimestamp >= (time_t)EPOCH_MIN_VALID) powerOffTimestamp -= (time_t)((millis() - powerOffTime) / 1000UL);
    bool markerTimeOk = prefPutLong64Verified("pwrOffTmstmp", (int64_t)powerOffTimestamp);
    if (markerTimeOk) outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_TIME_DIRTY;
    else outageMarkersDirty |= OUTAGE_MARKER_TIME_DIRTY;
    lastOutageDetect = true;
    bool markerFlagOk = prefPutBoolVerified("lastUotDetect", true);
    if (markerFlagOk) outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_FLAG_DIRTY;
    else outageMarkersDirty |= OUTAGE_MARKER_FLAG_DIRTY;

    statsOnPowerOff();
    powerOutageCount = (int32_t)(stats.offCount > 2147483647UL ? 2147483647UL : stats.offCount);
    bool countOk = prefPutIntVerified("powerOutageCnt", powerOutageCount);
    if (countOk) outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_COUNT_DIRTY;
    else outageMarkersDirty |= OUTAGE_MARKER_COUNT_DIRTY;
    if (!markerTimeOk || !markerFlagOk || !countOk) logEvent(LC_STORE, LS_ERR, "Маркери відключення: запис не підтверджено");
    evAdd(EV_OFF, powerOffTime);
    srvEventAt(SRV_EV_OFF, powerOffTime, 0);

    if (pendingOnMessage) {
      sendPendingOnMessage(NAN);
    }

    bool offTimeKnown = statsNowEpoch() != 0;
    powerOffShortTime = shortTimeAt(powerOffTime);
    offShortFix = !offTimeKnown;
    offShortFixMs = powerOffTime;
    String onDuration = formatShortDuration((powerOffTime - powerOnSinceMs) / 1000);
    if (powerOnSinceBoot) {
      onDuration += " (від запуску пристрою)";
    }
    lastOnDurationText = onDuration;
    preferences.putString("lastOnDur", lastOnDurationText);

    if (!offNotified) {
      const String item = String("F") + powerOffMessage(powerOffTime);
      if (offTimeKnown) {
        sendOrQueue(item);
      } else {
        mqPush(item);
      }
    }
    Serial.println("Повідомлення про відключення оброблено");
  }

  else if (currentPowerStatus && messageFlag == true) {
    messageFlag = false;
    powerOnTime = powerEdgeMs();

    statsOnPowerOn();
    evAdd(EV_ON, powerOnTime);

    if (outageEndRecorded() && prefPutBoolVerified("lastUotDetect", false)) {
      lastOutageDetect = false;
      outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_FLAG_DIRTY;
    } else {
      lastOutageDetect = true;
      outageMarkersDirty |= OUTAGE_MARKER_FLAG_DIRTY;
      logEvent(LC_STORE, LS_WARN, "Маркер відключення чекає статистики");
    }

    unsigned long outageSeconds = (powerOnTime - powerOffTime) / 1000;

    if (statLastOffKnown) {
      outageSeconds = statLastOffDurSec;
    }

    srvEventAt(SRV_EV_ON, powerOnTime, statLastOffKnown ? (uint32_t)outageSeconds : 0);

    powerOnSinceMs = powerOnTime;
    bool onTimeKnown = statsNowEpoch() != 0;
    powerOnShortTime = shortTimeAt(powerOnTime);
    onShortFix = !onTimeKnown;
    onShortFixMs = powerOnTime;
    lastOffDurationText = statLastOffKnown ? formatShortDuration(outageSeconds) : "";
    preferences.putString("lastOffDur", lastOffDurationText);
    powerOnSinceBoot = false;

    const String durPart = statLastOffKnown ? " " + formatShortDuration(outageSeconds)
                         : statOnFixPending ? durToken(statOnFixMs, statOnFixStart)
                         : String(": невідомо");
    String message = powerOnText(onTimeKnown ? htmlEsc(powerOnShortTime) : timeToken(powerOnTime),
                                 durPart, yasnoPowerEventText(true));

    if (!onTimeKnown) {

      mqPush(String("N") + message + MQ_SEP + (WiFi.status() != WL_CONNECTED ? "📵 Мережа WiFi була відсутня" : ""));
      Serial.println("Повідомлення про відновлення чекає синхронізації часу");
    }
    else if (WiFi.status() != WL_CONNECTED){
      mqPush(String("N") + message + MQ_SEP + "📵 Мережа WiFi була відсутня");
      Serial.println("Повідомлення про відновлення не надіслано");
    }
    else{

      startPendingOnMessage(message, "");
      Serial.println("Повідомлення про відновлення поставлено в чергу (очікування напруги від Shelly)");
    }
  }

  otaRestartTick();

  nvsRetryTick();

  processPendingOnMessage();

  processBatteryRecovery(false);
  telegramDeferredInit();
  retryStartMessage();
  mqProcess();
  blipTick();
  voltAlertTick();
  powerSensorTick();
  otaReportTick();
  otaCleanTick();
  otaConfirmTick();
  checkMemoryHealth();
  tgCleanTick();
  webTick();
  srvTick();

  static unsigned long lastBotCheck = 0;

  if (!safeMode) {
    yasnoTick();
    yasnoScheduleTick();
    yasnoPublishTick();
    wkPublishTick();
  }

  if (tgInitDone && WiFi.status() == WL_CONNECTED && millis() - lastBotCheck > (telegramOnline ? 1000UL : 15000UL)
      && !powerChangePending() && tgNetAllowed()) {
    int newMessage = pollTelegramUpdates();

    if (newMessage) {
      handleNewMessages();
    }

    lastBotCheck = millis();
  }

  if (ecoActive) {
    delay(50);
  }
}
