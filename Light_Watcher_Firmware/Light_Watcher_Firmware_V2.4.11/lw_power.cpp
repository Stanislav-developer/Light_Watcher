#include "lw.h"

static bool powerStable = false;
static bool powerStateInit = false;
static AdcWindow adcCur = { 4095, 0, 0, 0, 0, 0 };

struct PowerSampler {
  bool stable;
  bool cand;
  unsigned long candSince;
  unsigned long edgeMs;
  int last;
  unsigned long winStart;
  uint16_t winN, winGrey;
  bool fault;
  uint8_t faultEvents;
  uint8_t evN;
  bool evOn[POWER_EV_MAX];
  unsigned long evMs[POWER_EV_MAX];
};
static PowerSampler ps = {};
static portMUX_TYPE psMux = portMUX_INITIALIZER_UNLOCKED;
static bool samplerStarted = false;
static bool psReady = false;
static uint8_t faultEventsSeen = 0;
static bool sensorNotifyPending = false;
static bool sensorNotifyFault = false;

void powerSamplerStep(PowerSampler& s, int raw, unsigned long now) {
  s.last = raw;
  if (s.winStart == 0) s.winStart = now ? now : 1;
  s.winN++;
  const bool grey = raw >= POWER_GREY_LOW && raw < POWER_ON_THRESHOLD;
  if (grey) s.winGrey++;
  if (now - s.winStart >= POWER_FAULT_WINDOW_MS) {
    const unsigned pct = s.winN ? 100U * s.winGrey / s.winN : 0;
    if (!s.fault && pct >= POWER_FAULT_ON_PCT) { s.fault = true; s.faultEvents++; s.candSince = 0; }
    else if (s.fault && pct <= POWER_FAULT_OFF_PCT) { s.fault = false; s.faultEvents++; }
    s.winStart = now ? now : 1; s.winN = 0; s.winGrey = 0;
  }
  if (s.fault) return;
  if (grey) {
    s.candSince = 0;
    return;
  }
  const bool on = raw >= POWER_ON_THRESHOLD;
  if (on == s.stable) {
    s.candSince = 0;
  } else if (s.candSince == 0 || on != s.cand) {
    s.cand = on;
    s.candSince = now ? now : 1;
  } else if (now - s.candSince >= POWER_STABLE_MS) {
    s.stable = on;
    s.edgeMs = s.candSince;
    if (s.evN < POWER_EV_MAX) {
      s.evOn[s.evN] = on;
      s.evMs[s.evN] = s.candSince;
      s.evN++;
    } else {
      s.evN--;
    }
    s.candSince = 0;
  }
}

static void powerSamplerTask(void*) {
  for (;;) {
    const int raw = readPowerAdc();
    portENTER_CRITICAL(&psMux);
    powerSamplerStep(ps, raw, millis());
    portEXIT_CRITICAL(&psMux);
    vTaskDelay(pdMS_TO_TICKS(POWER_SAMPLE_MS));
  }
}

static PowerSampler powerSnapshot() {
  portENTER_CRITICAL(&psMux);
  PowerSampler s = ps;
  portEXIT_CRITICAL(&psMux);
  return s;
}

static PowerSampler powerView() {
  portENTER_CRITICAL(&psMux);
  const bool applied = !messageFlag;
  while (ps.evN > 0 && ps.evOn[0] == applied) {
    for (uint8_t i = 1; i < ps.evN; i++) {
      ps.evOn[i - 1] = ps.evOn[i];
      ps.evMs[i - 1] = ps.evMs[i];
    }
    ps.evN--;
  }
  PowerSampler s = ps;
  portEXIT_CRITICAL(&psMux);
  return s;
}

unsigned long powerEdgeMs() {
  if (!psReady) return millis();
  const PowerSampler s = powerSnapshot();
  return s.evN > 0 ? s.evMs[0] : s.edgeMs;
}

bool powerSensorFault() {
  return psReady && powerSnapshot().fault;
}

void powerSensorTick() {
  if (!psReady) return;
  const PowerSampler s = powerSnapshot();
  if (s.faultEvents != faultEventsSeen) {
    faultEventsSeen = s.faultEvents;
    logEvent(LC_ADC, s.fault ? LS_ERR : LS_INFO, s.fault
      ? "Датчик світла несправний: сигнал GPIO3 у сірій зоні (" + String(s.last) + "). Стан мережі не змінюється до відновлення сигналу"
      : "Сигнал датчика світла знову чіткий");
    sensorNotifyPending = true;
    sensorNotifyFault = s.fault;
  }
  if (!sensorNotifyPending || tgSendAttemptedThisPass || WiFi.status() != WL_CONNECTED || !tgInitDone || !tgNetAllowed()) return;
  static unsigned long lastTry = 0;
  if (lastTry != 0 && millis() - lastTry < 60000UL) return;
  lastTry = millis() ? millis() : 1;
  tgSendAttemptedThisPass = true;
  const String msg = sensorNotifyFault
    ? "⚠️ <b>Датчик світла несправний</b>\nСигнал на вході датчика тривалий час не відповідає ні «світло є», ні «світла немає» (АЦП: " + String(s.last) + ").\n"
      "Перевірте дроти та резистори подільника 10 кОм / 20 кОм. Поки сигнал не відновиться, сповіщення про світло не надсилаються."
    : "✅ <b>Сигнал датчика світла відновився</b>\nКонтроль світла працює.";
  if (tgSendOwnerNotice(msg)) sensorNotifyPending = false;
}

bool powerChangePending() {
  if (!psReady) return false;
  const PowerSampler s = powerSnapshot();
  const uint8_t applied = (s.evN > 0 && s.evOn[0] == !messageFlag) ? 1 : 0;
  return s.candSince != 0 || s.evN > applied;
}

float adcAvg(const AdcWindow& w) {
  return w.n ? (float)(w.sum / w.n) : 0;
}

float adcStd(const AdcWindow& w) {
  if (w.n < 2) return 0;
  double m = w.sum / w.n;
  double var = w.sumSq / w.n - m * m;
  return var > 0 ? (float)sqrt(var) : 0;
}

void adcDiagSample(int v) {
  if (adcWinStart == 0) {
    adcWinStart = millis();
  }
  if (v < adcCur.minV) adcCur.minV = v;
  if (v > adcCur.maxV) adcCur.maxV = v;
  adcCur.n++;
  adcCur.sum += v;
  adcCur.sumSq += (double)v * v;
  if (v >= POWER_GREY_LOW && v < POWER_ON_THRESHOLD) adcCur.grey++;
  if (millis() - adcWinStart < 60000UL) {
    return;
  }

  adcLast = adcCur;
  adcHaveLast = true;
  adcCur = { 4095, 0, 0, 0, 0, 0 };
  adcWinStart = millis();
  float sd = adcStd(adcLast);
  float greyPct = adcLast.n ? 100.0f * adcLast.grey / adcLast.n : 0;
  uint8_t f = 0;
  if (sd > 300) f |= 1;
  if (greyPct > 20) f |= 2;
  if (adcLast.n > 100 && adcLast.minV == adcLast.maxV && adcLast.minV != 0 && adcLast.minV != 4095) f |= 4;
  if (adcLast.minV == 4095) f |= 8;
  adcFlags = f;
  static const char* txt[4] = { "нестабільний сигнал датчика світла (великий розкид)", "сигнал датчика світла біля порогів (сіра зона)",
                                "значення датчика не змінюються (можлива несправність)", "насичення АЦП 4095 (перевірте подільник напруги)" };
  for (int i = 0; i < 4; i++) {
    if ((f & (1 << i)) && (adcFlagLogMs[i] == 0 || millis() - adcFlagLogMs[i] >= 3600000UL)) {
      adcFlagLogMs[i] = millis();
      if (i == 3) continue;
      logEvent(LC_ADC, LS_WARN, String(txt[i]) + ": сер. " + String((int)adcAvg(adcLast)) + ", розкид " + String((int)sd));
    }
  }
}

String adcDiagText() {
  if (!adcHaveLast) {
    return "Сигнал датчика: " + String(readValue) + " (статистика за хвилину ще збирається)";
  }
  String t = "Сигнал датчика за хвилину: мін. " + String(adcLast.minV) + ", макс. " + String(adcLast.maxV) + ", сер. " + String((int)adcAvg(adcLast))
             + ", розкид " + String((int)adcStd(adcLast)) + ", сіра зона " + String(adcLast.n ? (int)(100.0f * adcLast.grey / adcLast.n) : 0) + "%";
  t += String("\nСтан датчика: ") + ((adcFlags & 7) ? "⚠️ " : (adcFlags & 8) ? "ℹ️ " : "✅ ")
       + ((adcFlags & 1) ? "нестабільний; " : "") + ((adcFlags & 2) ? "біля порогів; " : "") + ((adcFlags & 4) ? "не змінюється; " : "")
       + ((adcFlags & 8) ? "4095 - штатний рівень «світло є» (подільник 10к/20к від 5 В)" : "") + (adcFlags == 0 ? "норма" : "")
       + (powerSensorFault() ? "\n⚠️ Датчик несправний: стан мережі не змінюється до відновлення сигналу" : "");
  return t;
}

int readPowerAdc() {
  long sum = 0;
  for (int i = 0; i < 4; i++) {
    sum += analogRead(readPin);
  }
  return (int)(sum / 4);
}

bool checkPowerStatus() {
  if (samplerStarted) {
    const PowerSampler s = powerView();
    readValue = s.last;
    adcDiagSample(readValue);
    powerCandidateSince = s.candSince;
    powerStable = s.evN > 0 ? s.evOn[0] : s.stable;
    return powerStable;
  }
  readValue = readPowerAdc();
  adcDiagSample(readValue);
  if (!powerStateInit) {

    bool cand = readValue >= POWER_ON_THRESHOLD;
    unsigned long candSince = millis();
    unsigned long startMs = millis();
    int onVotes = 0, total = 0;
    bool settled = false;
    while (millis() - startMs < 6000UL) {
      bool raw = readValue >= POWER_ON_THRESHOLD;
      const bool grey = readValue >= POWER_GREY_LOW && !raw;
      total++;
      if (raw) onVotes++;
      if (grey) {
        candSince = millis();
      } else if (raw != cand) {
        cand = raw;
        candSince = millis();
      } else if (millis() - candSince >= POWER_STABLE_MS) {
        settled = true;
        break;
      }
      wdtFeed();
      delay(100);
      readValue = readPowerAdc();
      adcDiagSample(readValue);
    }
    powerStateInit = true;
    powerStable = settled ? cand : (onVotes * 2 > total);
    if (!settled) {
      logEvent(LC_ADC, LS_WARN, "Запуск: сигнал датчика нестабільний 6 с - стан визначено більшістю вимірювань");
    }
    ps = PowerSampler{};
    ps.stable = powerStable;
    ps.edgeMs = millis();
    ps.last = readValue;
    psReady = true;
    if (xTaskCreate(powerSamplerTask, "power_adc", 4096, nullptr, 2, nullptr) == pdPASS) {
      samplerStarted = true;
    } else {
      logEvent(LC_ADC, LS_ERR, "Не вдалося запустити задачу опитування датчика - опитування з loop()");
    }
    return powerStable;
  }
  portENTER_CRITICAL(&psMux);
  powerSamplerStep(ps, readValue, millis());
  portEXIT_CRITICAL(&psMux);
  const PowerSampler s = powerView();
  powerStable = s.evN > 0 ? s.evOn[0] : s.stable;
  powerCandidateSince = s.candSince;
  return powerStable;
}
