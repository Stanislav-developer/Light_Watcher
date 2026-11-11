#include "lw.h"

static uint8_t crashSeq = 0;
static bool ecoNeedServerRestart = false;
static uint32_t memMinMaxBlock = 0;
static bool safeNotified = false;
static bool crashSeqCleared = false;
static bool crashWasMemory = false;
static bool crashWasCrash = false;
static bool ecoWindow = false;
static bool ecoNoWifiTiming = false;
static bool wifiCountersDirty = false;

void checkMemoryHealth() {
  static unsigned long lastCheck = 0;
  static int lowCount = 0;
  if (millis() - lastCheck < 60000UL) {
    return;
  }
  if (yasnoTaskRunning) {
    return;
  }
  lastCheck = millis();
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxBlock = ESP.getMaxAllocHeap();
  if (memMinMaxBlock == 0 || maxBlock < memMinMaxBlock) {
    memMinMaxBlock = maxBlock;
  }
  if (freeHeap < 30000UL || maxBlock < 25000UL) {
    lowCount++;
    logEvent(LC_MEM, LS_WARN, "Мало пам'яті: вільно " + String(freeHeap / 1024) + " КБ, блок " + String(maxBlock / 1024) + " КБ (" + String(lowCount) + "/3)");
    if (lowCount >= 3) {
      logEvent(LC_MEM, LS_ERR, "Критична нестача пам'яті - перезапуск");
      prefPutBoolVerified("memRst", true);
      prepareForRestart("критична нестача оперативної пам'яті");
      delay(200);
      ESP.restart();
    }
  } else {
    lowCount = 0;
  }
}

void prepareForRestart(const char* reason) {
  const bool afterStatsReset = reason != NULL && strstr(reason, "після успішного очищення статистики") != NULL;
  if (!afterStatsReset) {
    statsSaveBeforeRestart();
    voltSaveIfDirty();
    evAdd(EV_STOP, millis());
  } else {
    Serial.println("[RESET] restart: verified reset snapshot retained; duplicate NVS writes skipped");
  }
  preferences.putString("rstWhy", reason ? reason : "перезапуск");
  telegramAckBeforeRestart();
  tgCleanFlush();
  wifiCountersFlush(true);
  logFlush();
}

void wdtBegin() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = { .timeout_ms = WDT_TIMEOUT_S * 1000, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
    esp_task_wdt_init(&cfg);
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_err_t e = esp_task_wdt_add(NULL);
  wdtOn = (e == ESP_OK || e == ESP_ERR_INVALID_ARG);
  Serial.println(wdtOn ? "Watchdog: 60 с" : "Watchdog: не вдалося увімкнути");
}

void wdtFeed() {
  if (wdtOn) {
    esp_task_wdt_reset();
  }
}

bool resetIsCrash(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
}

void crashCount() {
  esp_reset_reason_t r = esp_reset_reason();
  crashWasMemory = r == ESP_RST_SW && preferences.getBool("memRst", false);
  if (preferences.isKey("memRst")) preferences.remove("memRst");
  crashWasCrash = resetIsCrash(r) || crashWasMemory;
  crashTotal = preferences.getUInt("crashN", 0);
  crashSeq = preferences.getUChar("crashSeq", 0);
  if (crashWasCrash) {
    crashTotal++;
    crashSeq++;
    preferences.putUInt("crashN", crashTotal);
    preferences.putUChar("crashSeq", crashSeq);
  } else if (crashSeq != 0) {
    crashSeq = 0;
    preferences.putUChar("crashSeq", 0);
  }
  if (crashSeq >= 3) safeMode = true;
}

void crashBegin() {
  esp_reset_reason_t r = esp_reset_reason();
  if (crashWasMemory) {
    logEvent(LC_MEM, LS_ERR, "Аварійний перезапуск: критична нестача оперативної пам'яті");
  } else if (crashWasCrash) {
    logEvent(r == ESP_RST_BROWNOUT ? LC_SYS : LC_WDT, LS_ERR, "Аварійний перезапуск: " + resetReasonText());
  } else {
    logEvent(LC_SYS, LS_INFO, "Запуск: " + resetReasonText());
  }
  if (safeMode) {
    logEvent(LC_SYS, LS_ERR, String("Безпечний режим на 1 год: ") + String(crashSeq) + " аварійні перезапуски поспіль");
  }
}

void crashTick() {
  if (!crashSeqCleared && millis() >= CRASH_SEQ_RESET_MS) {
    crashSeqCleared = true;
    if (crashSeq != 0) {
      crashSeq = 0;
      preferences.putUChar("crashSeq", 0);
    }
  }
  static unsigned long safeNotifyTryMs = 0;
  static bool safeNotifyTried = false;
  if (safeMode && !safeNotified && (!safeNotifyTried || millis() - safeNotifyTryMs >= 60000UL) &&
      !tgSendAttemptedThisPass && tgInitDone && telegramOnline && WiFi.status() == WL_CONNECTED && tgNetAllowed()) {
    safeNotifyTried = true;
    safeNotifyTryMs = millis();
    tgSendAttemptedThisPass = true;
    safeNotified = tgSendOwnerNotice("⚠️ <b>Light Watcher: безпечний режим</b>\n"
                    "Пристрій кілька разів поспіль аварійно перезапустився (" + resetReasonText() + ").\n"
                    "На 1 год вимкнено вебсторінки, Shelly і контроль напруги, YASNO та визначення публічної IP. Світло й повідомлення працюють.\n"
                    "Подробиці: 💡 Система → 📜 Журнал.");
  }
  if (safeMode && millis() >= SAFE_MODE_MS) {
    logEvent(LC_SYS, LS_INFO, "Кінець безпечного режиму - перезапуск");
    prepareForRestart("завершення безпечного режиму (1 год)");
    delay(200);
    ESP.restart();
  }
}

String resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "увімкнення живлення (живлення зникало повністю)";
    case ESP_RST_EXT: return "зовнішній сигнал скидання";
    case ESP_RST_SW: return "програмний перезапуск";
    case ESP_RST_PANIC: return "збій програми (аварійний перезапуск)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "сторожовий таймер (програма зависла)";
    case ESP_RST_DEEPSLEEP: return "вихід зі сну";
    case ESP_RST_BROWNOUT: return "просідання напруги живлення (слабка батарея або блок живлення)";
    case ESP_RST_SDIO: return "SDIO";
    default: return "інша (код " + String((int)esp_reset_reason()) + ")";
  }
}

String diagAgo(unsigned long ms) {
  if (ms == 0) {
    return "ще не було";
  }
  return formatShortDuration((millis() - ms) / 1000) + " тому";
}

String buildDiagMessage() {
  uint32_t now = statsNowEpoch();
  String t = "🩺 <b>Діагностика</b> · V2.4.11\n\n";

  t += "<b>⚙️ Пристрій</b>\n";
  t += "Працює без перезапуску: " + formatDuration(uptimeSec()) + "\n";
  t += "Запусків усього: " + String(bootCount) + "\n";
  t += "Причина останнього запуску: " + resetReasonText() + "\n";
  t += "Аварійних перезапусків: " + String(crashTotal) + " усього" + (crashSeq > 0 ? ", " + String(crashSeq) + " поспіль" : String("")) + "\n";
  t += String("Watchdog: ") + (wdtOn ? "активний (60 с)" : "вимкнений") + "\n";
  if (safeMode) {
    t += "⚠️ Безпечний режим: вебсторінки, контроль напруги й визначення IP вимкнені до " + formatShortDuration((SAFE_MODE_MS - millis()) / 1000) + "\n";
  }
  if (bootRstWhy.length() > 0) {
    t += "Перезапуск ініційовано: " + htmlEsc(bootRstWhy) + "\n";
  }
  if (prevAliveEpoch != 0) {
    t += "Попередній сеанс працював щонайменше до: " + formatEpochLocal((time_t)prevAliveEpoch, "%d.%m %H:%M") + "\n";
  }
  t += "Процесор: " + String(getCpuFrequencyMhz()) + " МГц" + (ecoActive ? " (економний режим)" : "") + "\n\n";

  t += "<b>🧠 Пам'ять</b>\n";
  t += "Вільно зараз: " + String(ESP.getFreeHeap() / 1024) + " КБ, найбільший блок: " + String(ESP.getMaxAllocHeap() / 1024) + " КБ\n";
  t += "Найменше вільної за весь час: " + String(ESP.getMinFreeHeap() / 1024) + " КБ\n";
  t += "Найменший найбільший блок (раз на хв): " + String(memMinMaxBlock / 1024) + " КБ\n";
  nvs_stats_t ns;
  if (nvs_get_stats(NULL, &ns) == ESP_OK) {
    t += "Сховище налаштувань: зайнято " + String((unsigned)ns.used_entries) + " з " + String((unsigned)ns.total_entries) + " записів\n";
  } else {
    t += "Сховище налаштувань: заповненість невідома\n";
  }

  if (nvsFailCount == 0) {
    t += "Записи даних у флеш-пам'ять: непідтверджених немає\n";
  } else {
    t += "⚠️ Записи у флеш-пам'ять: непідтверджених " + String(nvsFailCount) + ", останній - " + htmlEsc(nvsFailKey) + ", " + diagAgo(nvsFailMs) + "\n";
  }
  t += String("Статистика: ") + (statsSaveOk ? "записано" : "⚠️ останній запис не підтверджено") + ", резервна копія "
       + (statsBackupOk ? "записана" : "⚠️ не підтверджена") + "; історія " + (histSaveOk ? "записана" : "⚠️ не підтверджена") + "\n";
  if (statsLoadNote.length() > 0 || nvsLoadNote.length() > 0) {
    t += "⚠️ При запуску: " + htmlEsc(statsLoadNote + nvsLoadNote) + "\n";
  }
  {
    String w = "";
    if (!statsSaveOk || !statsBackupOk) w += "статистика, ";
    if (!histSaveOk) w += "історія, ";
    if (!evSaveOk) w += "журнал станів, ";
    if (!spkSaveOk) w += "стрибки напруги, ";
    if (!vDaySaveOk) w += "добова напруга, ";
    if (!chatsSaveOk) w += "реєстр груп, ";
    if (voltDirty && voltSaveFailed) w += "вимірювання напруги, ";

    String u = "";
    if (statsUnread) u += "статистика, ";
    if (histUnread) u += "історія, ";
    if (chatsUnread) u += "реєстр груп, ";
    if (evUnread) u += "журнал станів, ";
    if (voltUnread) u += "історія напруги, ";
    if (spkUnread) u += "стрибки напруги, ";
    if (logUnread) u += "журнал подій, ";
    if (vDayUnread) u += "добова напруга, ";
    if (u.length() > 0) {
      t += "⚠️ Не прочитано, запис призупинено (повтор): " + u.substring(0, u.length() - 2) + "\n";
    }
    if (logDirty && logSaveFailed) w += "журнал подій, ";
    if (w.length() > 0) {
      t += "⚠️ Очікують повторного запису: " + w.substring(0, w.length() - 2) + "\n";
    }
  }
  t += "Прошивка: " + String(ESP.getSketchSize() / 1024) + " КБ, вільно для прошивки: " + String(ESP.getFreeSketchSpace() / 1024) + " КБ\n\n";

  t += "<b>💡 Світло</b>\n";
  t += String("Стан: ") + (messageFlag ? "немає" : "є") + "\n";
  t += "Сигнал датчика: " + String(readValue) + " (світло є від " + String(POWER_ON_THRESHOLD) + ", немає нижче " + String(POWER_GREY_LOW) + ", між ними - сіра зона)\n";
  t += adcDiagText() + "\n\n";

  t += "<b>📶 Wi-Fi</b>\n";
  if (WiFi.status() == WL_CONNECTED) {
    t += "Мережа: " + htmlEsc(WiFi.SSID()) + ", сигнал " + String(WiFi.RSSI()) + " dBm, канал " + String(WiFi.channel()) + "\n";
    t += "IP: " + WiFi.localIP().toString() + ", роутер: " + WiFi.gatewayIP().toString() + "\n";
    t += "Підключено: " + diagAgo(wifiUpSinceMs) + "\n";
  } else {
    t += "Немає з'єднання\n";
  }
  t += "Відключень: " + String(wifiDiscBoot) + " (з запуску) / " + String(wifiDiscTotal) + " (усього)\n";
  t += "Підключень: " + String(wifiConnBoot) + " (з запуску) / " + String(wifiConnTotal) + " (усього)\n";
  t += String("Економний режим: ") + (ecoActive ? "увімкнено" : "вимкнено") + "\n\n";

  t += "<b>✈️ Telegram</b>\n";
  t += String("Стан: ") + (!timeTrusted() ? "очікує синхронізації часу (NTP)" : telegramOnline ? "на зв'язку" : "недоступний")
       + (tgInitDone ? "" : " (початкове налаштування ще не виконано)") + "\n";
  t += "Останній успішний зв'язок: " + diagAgo(tgLastOkMs) + "\n";
  if (grpFailTotal > 0) {
    t += "⚠️ Не доставлено в групи/канали: " + String(grpFailTotal) + " (з запуску), останнє - «" + htmlEsc(grpFailLast) + "», " + diagAgo(grpFailLastMs) + "\n";
  }
  t += String("Сертифікат: перевіряється") + (tlsCertFailing ? " ⚠️ зараз не проходить перевірку (спроб " + String(tlsFailTotal) + ", наступна через "
       + String((tlsBackoffMs - min(tlsBackoffMs, millis() - tlsLastFailMs)) / 1000UL) + " с)" : String("")) + "\n";
  t += "Недоставлених повідомлень у черзі: " + String(mqCount) + "\n";
  {
    int pend = 0;
    for (int i = 0; i < PEND_SLOTS; i++) {
      if (preferences.isKey(PEND_KEYS[i])) pend++;
    }
    t += "Резервні копії: " + String(pend) + "/" + String(PEND_SLOTS) + "\n";
    if (mqLoadLost) {
      t += String("⚠️ При запуску записану чергу не вдалося прочитати")
           + (mqLoadBlocked ? " - збережені повідомлення не перезаписуються, повтор читання раз на хвилину; нові доставляються з пам'яті" : "") + "\n";
    }
    if (mqRejected > 0) {
      t += "⚠️ Не прийнято в чергу (довше " + String(MQ_ITEM_MAX) + " байт): " + String(mqRejected) + "\n";
    }
    if (mqDirty) {
      int unsaved = mqUnsavedCount();
      mqUnsaved = unsaved;
      t += "⚠️ Черга не записана у флеш-пам'ять (повтор раз на хвилину)"
           + (unsaved < 0 ? String("; не вдалося перевірити, що збережено")
              : unsaved > 0 ? "; лише в оперативній пам'яті: " + String(unsaved) + " - буде втрачено при перезапуску"
              : String("; кожне повідомлення є у флеш-пам'яті або в резервній копії")) + "\n";
    }
    int dn = mqDoneCount();
    if (dn > 0 && !mqDoneClearFailed) {
      t += "Доставлено, вилучення ще не записано: " + String(dn) + " (після перезапуску не повторяться)\n";
    }
    if (mqDoneWaitN > 0) {
      t += "⚠️ Доставлено, але не позначено у флеш-пам'яті: " + String(mqDoneWaitN)
           + " - копії збережено, після перезапуску можливий повтор (повтор запису раз на хвилину)\n";
    }
    if (mqDoneClearFailed) {
      t += "⚠️ Список доставлених не очищено після запису черги (повтор раз на хвилину)\n";
    }
    if (pendOrderNote.length() > 0) {
      t += "⚠️ Порядок резервних копій при запуску неточний: " + pendOrderNote + "\n";
    }
    if (pendingOnMessage && pendSaveFailed) {
      t += "⚠️ «Світло з'явилося» чекає напругу Shelly без резервної копії (повтор запису раз на 2 с)\n";
    }
  }
  t += "Найдовший прохід циклу: " + String(loopMaxMs) + " мс (з запуску)\n";
  if (pendingOnMessage || batteryRecPending || startMsgPending) {
    t += "Очікують: " + String(pendingOnMessage ? "«Світло з'явилося» (напруга Shelly) " : "") + (batteryRecPending ? "«Світло з'явилося» після розряду " : "")
         + (startMsgPending ? "стартове повідомлення" : "") + "\n";
  }
  t += "\n";

  t += "<b>🕐 Час</b>\n";
  if (now != 0) {
    t += "Синхронізовано: " + formatEpochLocal((time_t)now, "%d.%m.%Y %H:%M:%S") + "\n";
    t += "Запуск пристрою: " + formatEpochLocal((time_t)(now - uptimeSec()), "%d.%m %H:%M") + "\n";
  } else {
    t += "Ще не синхронізовано (немає інтернету)\n";
  }
  if (statOnFixPending || statHistFixCount > 0) {
    t += "Події, що чекають на час: " + String(statHistFixCount + (statOnFixPending ? 1 : 0)) + "\n";
  }
  t += "\n";

  t += "<b>⚡ Shelly</b>\n";
  if (!shellyConfigured()) {
    t += "Не налаштовано\n";
  } else {
    t += "IP: " + htmlEsc(shellyIP) + "\n";
    int vi = (voltLog.head + VOLT_SIZE - 1) % VOLT_SIZE;
    if (voltLog.count > 0 && voltLog.t[vi] != 0) {
      t += "Останнє вимірювання: " + String(voltLog.dv[vi] / 10.0f, 1) + " В о " + formatEpochLocal((time_t)voltLog.t[vi], "%d.%m %H:%M") + "\n";
    } else {
      t += "Вимірювань ще немає\n";
    }
    if (shellyLastFailMs != 0) {
      t += "Остання невдала відповідь: " + diagAgo(shellyLastFailMs) + "\n";
    }
  }
  t += "\n";

  t += "<b>🌐 Вебсторінки</b>\n";
  t += String("Адмінпанель: ") + (webStarted ? "http://" + WiFi.localIP().toString() + "/" : String("не запущено")) + "\n";
  String link = webPublicLink();
  t += "Публічна сторінка: " + (link.length() > 0 ? link : String("посилання не налаштовано")) + "\n";
  t += "Публічна IP: " + (pubIpAuto.length() > 0 ? pubIpAuto : String("не визначено")) + "\n";
  if (webDomOn) {
    t += "HTTPS домену: " + webHttpsStatusText() + (webCheckAt ? " (перевірено " + formatEpochLocal((time_t)webCheckAt, "%d.%m %H:%M") + ")" : String("")) + "\n";
  }
  t += "\n";
  t += "<b>📜 Журнал</b>\nПопереджень і помилок за цей сеанс: " + String(logIssues24) + " (💡 Система → 📜 Журнал)";
  return t;
}

void ecoExit(const char* why) {
  ecoActive = false;
  ecoWindow = false;
  ecoNoWifiTiming = false;
  setCpuFrequencyMhz(160);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
  }
  logEvent(LC_SYS, LS_INFO, String("Економний режим вимкнено: ") + why);
}

void ecoTick() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  bool mainsOff = messageFlag;
  if (!ecoActive) {
    if (!mainsOff || wifiOk) {
      ecoNoWifiTiming = false;
      return;
    }
    if (!ecoNoWifiTiming) {
      ecoNoWifiTiming = true;
      ecoNoWifiSince = millis();
      return;
    }
    if (millis() - ecoNoWifiSince < ECO_ENTER_MS) {
      return;
    }
    if (yasnoTaskRunning) return;
    ecoActive = true;
    ecoWindow = false;
    ecoLastAttempt = millis();
    ecoNeedServerRestart = true;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    setCpuFrequencyMhz(80);
    logEvent(LC_SYS, LS_INFO, "Економний режим увімкнено: немає світла і Wi-Fi");
    return;
  }
  if (!mainsOff) {
    ecoExit("світло з'явилося");
    return;
  }
  if (wifiOk) {
    ecoExit("Wi-Fi з'явився");
    return;
  }
  if (!ecoWindow && millis() - ecoLastAttempt >= ECO_RETRY_MS) {
    ecoWindow = true;
    ecoWindowStart = millis();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.begin(ssid.c_str(), password.c_str());
  } else if (ecoWindow && millis() - ecoWindowStart >= ECO_WINDOW_MS && !yasnoTaskRunning) {
    ecoWindow = false;
    ecoLastAttempt = millis();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
  }
}

void checkWiFi() {

  static unsigned long lastWiFiCheck = 0;

  unsigned long interval = (WiFi.status() == WL_CONNECTED) ? 30000UL : 15000UL;

  if (!ecoActive && millis() - lastWiFiCheck > interval) {
    lastWiFiCheck = millis();

    static int failedChecks = 0;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi втрачено");
      Serial.println("Перепід'єднання до WiFi... ");
      failedChecks++;
      if (failedChecks % 2 == 0) {

        WiFi.disconnect();
        WiFi.begin(ssid.c_str(), password.c_str());
        WiFi.setSleep(false);
      } else {
        WiFi.reconnect();
      }
    } else {
      failedChecks = 0;
    }

  }

  static bool wasConnected = false;
  bool connectedNow = (WiFi.status() == WL_CONNECTED);
  if (connectedNow && !wasConnected) {
    logEvent(LC_WIFI, LS_INFO, "Wi-Fi підключено, сигнал " + String(WiFi.RSSI()) + " dBm");
    client.stop();
    if (ecoNeedServerRestart && webStarted) {
      server.stop();
      server.begin();
      pubServer.stop();
      pubServer.begin();
    }
    ecoNeedServerRestart = false;
    mqLastTry = 0;
    wifiUpSinceMs = millis();
    wifiConnBoot++;
    wifiConnTotal++;
    wifiCountersDirty = true;

    configTime(0, 0, ntp1, ntp2, ntp3);
    applyTimezone(currentTZ);
    Serial.println("Синхронізація часу запущена");
  } else if (!connectedNow && wasConnected) {
    logEvent(LC_WIFI, LS_WARN, "Wi-Fi втрачено");
    wifiUpSinceMs = 0;
    tgStaleSkip = true;
    wifiDiscBoot++;
    wifiDiscTotal++;
    wifiCountersDirty = true;
  }
  wasConnected = connectedNow;
  wifiCountersFlush(false);
}

void wifiCountersFlush(bool force) {
  static unsigned long lastMs = 0;
  if (!wifiCountersDirty || (!force && millis() - lastMs < 600000UL)) return;
  lastMs = millis();
  preferences.putUInt("wifiConnN", wifiConnTotal);
  preferences.putUInt("wifiDiscN", wifiDiscTotal);
  wifiCountersDirty = false;
}

void nvsRetryTick() {
  static unsigned long last = 0;
  if (millis() - last < NVS_RETRY_MS) {
    return;
  }
  last = millis();
  if (outageMarkersDirty & OUTAGE_MARKER_COUNT_DIRTY) {
    if (prefPutIntVerified("powerOutageCnt", powerOutageCount)) outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_COUNT_DIRTY;
  }
  if (outageMarkersDirty & OUTAGE_MARKER_TIME_DIRTY) {
    if (prefPutLong64Verified("pwrOffTmstmp", (int64_t)powerOffTimestamp)) outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_TIME_DIRTY;
  }
  if (outageMarkersDirty & OUTAGE_MARKER_FLAG_DIRTY) {
    if (powerStatus && outageEndRecorded()) lastOutageDetect = false;
    if (prefPutBoolVerified("lastUotDetect", lastOutageDetect)) outageMarkersDirty &= (uint8_t)~OUTAGE_MARKER_FLAG_DIRTY;
  }
  if (!statsTxnBlocked) {
    if (statsPairDirty) {
      statsCommitPair();
    }
    if (!statsSaveOk || !statsBackupOk) statsSave();

    if (!histSaveOk && statsSaveOk) histSave();
  }
  if (!evSaveOk) evSave();
  if (!spkSaveOk) spkSave();
  if (!chatsSaveOk) chatsSave();
  if (!vDaySaveOk) voltDaySave();
}
