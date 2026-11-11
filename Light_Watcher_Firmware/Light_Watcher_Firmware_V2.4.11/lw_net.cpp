#include "lw.h"

static volatile bool ntpSynced = false;
static volatile uint8_t netDnsState = 0;
static volatile uint32_t netDnsSeq = 0;
static ip_addr_t netDnsAddr;
static char netDnsName[96];

void tlsApplyClient() {
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  client.setTimeout(4000);
#else
  client.setTimeout(4);
#endif
  client.setHandshakeTimeout(10);
}

void tlsSetup() {

  if (preferences.isKey("tlsFb") || preferences.isKey("tlsFbAt")) {
    bool wasFb = preferences.getBool("tlsFb", false);
    preferences.remove("tlsFb");
    preferences.remove("tlsFbAt");
    if (wasFb) {
      logEvent(LC_TG, LS_INFO, "Telegram: запасний режим без перевірки вимкнено, сертифікат перевіряється");
    }
  }
  tlsApplyClient();
}

bool tlsRetryDue() {
  return !tlsCertFailing || millis() - tlsLastFailMs >= tlsBackoffMs;
}

void tlsCertFailed(const String& why) {
  tlsFailTotal++;
  tlsLastFailMs = millis();
  if (!tlsCertFailing) {
    tlsCertFailing = true;
    tlsCertFailSince = millis();
    tlsLastRemindMs = millis();
    tlsBackoffMs = TLS_RETRY_MIN_MS;
    logEvent(LC_TG, LS_ERR, "Telegram: сертифікат не пройшов перевірку (" + why + "), повтор через 1 хв");
    return;
  }
  tlsBackoffMs = (tlsBackoffMs >= TLS_RETRY_MAX_MS / 2) ? TLS_RETRY_MAX_MS : tlsBackoffMs * 2;
  if (millis() - tlsLastRemindMs >= TLS_REMIND_MS) {
    tlsLastRemindMs = millis();
    logEvent(LC_TG, LS_WARN, "Telegram: сертифікат досі не проходить перевірку (" + String((millis() - tlsCertFailSince) / 3600000UL)
                             + " год, спроб " + String(tlsFailTotal) + ")");
  }
}

void tlsNoteFailure() {
  char buf[8];
  int err = client.lastError(buf, sizeof(buf));
  if (err != TLS_CERT_ERR) {
    return;
  }
  client.stop();
  tlsCertFailed("невідомий центр сертифікації або чуже ім'я");
}

void tlsNoteSuccess() {
  if (tlsCertFailing) {
    tlsCertFailing = false;
    tlsBackoffMs = 0;
    logEvent(LC_TG, LS_INFO, "Telegram: сертифікат знову проходить перевірку (невдалих спроб: " + String(tlsFailTotal) + ")");
    tlsFailTotal = 0;
  }
}

void netDnsCb(const char* name, const ip_addr_t* ip, void* arg) {
  (void)name;
  if ((uint32_t)(uintptr_t)arg != netDnsSeq) {
    return;
  }
  if (ip != NULL) {
    netDnsAddr = *ip;
    __sync_synchronize();
    netDnsState = 1;
  } else {
    netDnsState = 2;
  }
}

void netDnsStart(void* arg) {
  ip_addr_t a;
  err_t e = dns_gethostbyname(netDnsName, &a, netDnsCb, arg);
  if ((uint32_t)(uintptr_t)arg != netDnsSeq) {
    return;
  }
  if (e == ERR_OK) {
    netDnsAddr = a;
    __sync_synchronize();
    netDnsState = 1;
  } else if (e != ERR_INPROGRESS) {
    netDnsState = 2;
  }
}

bool netDnsQuick(const char* host, uint32_t waitMs, IPAddress* out) {
  IPAddress lit;
  if (host == NULL || host[0] == 0 || strlen(host) >= sizeof(netDnsName)) {
    return false;
  }
  if (lit.fromString(host)) {
    if (out != NULL) *out = lit;
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  uint32_t seq = netDnsSeq + 1;
  netDnsSeq = seq;
  netDnsState = 0;
  strncpy(netDnsName, host, sizeof(netDnsName) - 1);
  netDnsName[sizeof(netDnsName) - 1] = 0;
  if (tcpip_callback(netDnsStart, (void*)(uintptr_t)seq) != ERR_OK) {
    return false;
  }
  unsigned long st = millis();
  while (netDnsState == 0 && millis() - st < waitMs) {
    delay(10);
  }
  if (netDnsState != 1) {
    return false;
  }
  __sync_synchronize();
  if (!IP_IS_V4(&netDnsAddr)) {
    return false;
  }
  if (out != NULL) {
    *out = IPAddress(ip4_addr_get_u32(ip_2_ip4(&netDnsAddr)));
  }
  return true;
}

void ntpSyncCb(struct timeval* tv) {
  (void)tv;
  ntpSynced = true;
}

bool timeTrusted() {
  return ntpSynced && statsNowEpoch() != 0;
}

void tgTimeWaitLog() {
  static bool logged = false;
  if (!logged) {
    logged = true;
    logEvent(LC_TG, LS_WARN, "Telegram: очікування синхронізації часу (NTP) - без неї захищене з'єднання не встановлюється");
  }
}

bool tlsPeerDatesOk() {
  const mbedtls_x509_crt* pc = client.getPeerCertificate();
  if (pc == NULL) {
    client.stop();
    tlsCertFailed("сертифікат сервера недоступний");
    return false;
  }
  uint32_t now = statsNowEpoch();
  if (!timeTrusted()) {
    client.stop();
    tgTimeWaitLog();
    return false;
  }
  uint32_t from = webX509Epoch(pc->valid_from);
  uint32_t to = webX509Epoch(pc->valid_to);
  if (from != 0 && to != 0 && now >= from && now <= to) {
    return true;
  }
  client.stop();
  tlsCertFailed("строк дії закінчився або ще не почався");
  return false;
}

int LWTelegramClient::connect(const char* host, uint16_t port) {
  if (!timeTrusted()) {
    tgTimeWaitLog();
    return 0;
  }
  wdtFeed();
  if (!netDnsQuick(host, NET_DNS_WAIT_MS, NULL)) {
    return 0;
  }
  const bool connected = WiFiClientSecure::connect(host, port);
  wdtFeed();
  if (!connected) {
    return 0;
  }
  return tlsPeerDatesOk() ? 1 : 0;
}

void tgConnectFailed() {
  tlsNoteFailure();
  telegramFailCount++;
  if (telegramFailCount >= 3 && telegramOnline) {
    telegramOnline = false;
    tgStaleSkip = true;
    logEvent(LC_TG, LS_WARN, "Telegram недоступний");
  }
}

bool tgNetAllowed() {
  if (!tlsRetryDue()) {
    return false;
  }
  if (!timeTrusted()) {
    tgTimeWaitLog();
    return false;
  }
  if (client.connected()) {
    return true;
  }
  static unsigned long lastTry = 0;
  static bool lastFailed = false;
  unsigned long gap = telegramOnline ? (lastFailed ? TG_ONLINE_GAP_MS : 0UL) : TG_OFFLINE_GAP_MS;
  if (lastTry != 0 && millis() - lastTry < gap) {
    return false;
  }
  lastTry = millis();
  if (lastTry == 0) {
    lastTry = 1;
  }
  wdtFeed();
  if (!client.connect(TG_HOST, 443)) {
    lastFailed = true;
    tgConnectFailed();
    return false;
  }
  lastFailed = false;
  return true;
}
