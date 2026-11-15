#include "lw.h"

static String srvUrlV = "";
static String srvHost = "";
static uint16_t srvPort = SRV_PORT_DEF;
static uint8_t srvPartsV = 0;
static uint16_t srvHbOnV = SRV_HB_ON_DEF;
static uint16_t srvHbOffV = SRV_HB_OFF_DEF;
static String srvKeyHex = "";
static String srvDevId = "";
static SrvQueue srvQ;
static bool srvQDirty = false;
static uint32_t srvN = 0;
static uint8_t srvPtSrv = 0;
static unsigned long srvLastTryMs = 0;
static unsigned long srvLastOkMs = 0;
static unsigned long srvFirstFailMs = 0;
static uint8_t srvFailN = 0;
static uint8_t srvLastErr = SRV_OK;
static unsigned long srvFastUntil = 0;
static uint16_t srvFastSec = 0;
static bool srvWantData = false;
static unsigned long srvLastDataMs = 0;
static int32_t srvKeepMax = 0;
static uint8_t srvNote = 0;
static String srvCheckUrl = "";
static int srvCheckMsg = 0;
static bool srvCheckPending = false;
static unsigned long srvCheckSetMs = 0;
static String srvFwUrl = "";
static bool srvFwBye = false;
static uint8_t srvFwTries = 0;
static unsigned long srvFwNextMs = 0;
static uint8_t srvDataFailN = 0;
static String srvNonce = "";
static unsigned long srvNonceMs = 0;
static bool srvQStuck = false;
static String srvYsVer = "";
static String srvYsGroup = "";
static String srvYsImg = "";
static String srvYsImgV = "";
static unsigned long srvImgWaitMs = 0;
static String srvStoreV = "";
static uint8_t srvWkH = 2;
static String srvWkImg = "";
static String srvWkVer = "";
static String srvWkCap = "";
static uint32_t bkSentHash = 0;
static uint32_t bkSavedEpoch = 0;
static unsigned long bkCheckMs = 0;
static uint8_t bkAct = 0;
static uint32_t bkActId = 0;
static int bkWaitMsg = 0;
static BkItem bkItems[BK_LIST_MAX];
static uint8_t bkItemN = 0;
static uint16_t bkOwnN = 0;
static uint16_t bkOtherN = 0;
static int8_t bkListSt = 0;
static bool bkAutoV = true;
static BkData* bkPending = nullptr;
static String bkNonce = "";
static unsigned long bkNonceMs = 0;
static unsigned long srvYsOkMs = 0;
static bool srvPickOn = true;

static bool srvSetParts(uint8_t p);
static void srvDetach();

static const uint16_t SRV_HB_OPTS[] = { 10, 15, 30, 60, 120, 300, 600, 900, 1800 };
static const uint16_t SRV_RETRY_SEC[] = { 5, 10, 20, 30 };

bool srvHostOk(const String& h) {
  if (h.length() == 0 || h.length() > 63 || h[0] == '.' || h[0] == '-' || h[h.length() - 1] == '.' || h.indexOf('.') < 0) return false;
  for (unsigned i = 0; i < h.length(); i++) {
    const char c = h[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    if (c == '.' && i + 1 < h.length() && h[i + 1] == '.') return false;
  }
  return true;
}

String srvParseUrl(const String& in, String& out) {
  out = "";
  String s = in;
  s.trim();
  String low = s;
  low.toLowerCase();
  if (low.startsWith("https://")) return "HTTPS поки не підтримується. Вкажіть IP або адресу з http://";
  if (low.startsWith("http://")) low = low.substring(7);
  else if (low.indexOf("://") >= 0) return "Адреса має починатися з http:// або бути просто IP.";
  while (low.endsWith("/")) low.remove(low.length() - 1);
  if (low.length() == 0 || low.length() > SRV_URL_MAX) return "Адреса порожня або задовга.";
  if (low.indexOf('/') >= 0 || low.indexOf('?') >= 0 || low.indexOf('@') >= 0) return "Лише IP або домен і порт, без шляху.";
  String host = low;
  long port = SRV_PORT_DEF;
  const int colon = low.lastIndexOf(':');
  if (colon >= 0) {
    if (low.indexOf(':') != colon) return "IPv6 не підтримується. Вкажіть IPv4 або домен.";
    host = low.substring(0, colon);
    const String p = low.substring(colon + 1);
    if (p.length() == 0 || p.length() > 5) return "Порт - число від 1 до 65535.";
    for (unsigned i = 0; i < p.length(); i++) {
      if (p[i] < '0' || p[i] > '9') return "Порт - число від 1 до 65535.";
    }
    port = p.toInt();
    if (port < 1 || port > 65535) return "Порт - число від 1 до 65535.";
  }
  if (!srvHostOk(host)) return "Некоректна адреса. Приклад: 203.0.113.10 або 203.0.113.10:8000";
  out = "http://" + host + ":" + String(port);
  return "";
}

bool srvSplitUrl(const String& url, String& host, uint16_t& port) {
  if (!url.startsWith("http://")) return false;
  const String rest = url.substring(7);
  const int colon = rest.lastIndexOf(':');
  if (colon <= 0) return false;
  host = rest.substring(0, colon);
  const long p = rest.substring(colon + 1).toInt();
  if (p < 1 || p > 65535 || !srvHostOk(host)) return false;
  port = (uint16_t)p;
  return true;
}

String srvShortUrl(const String& url) {
  return url.startsWith("http://") ? url.substring(7) : url;
}

bool srvIsLocalHost(const String& host) {
  IPAddress ip;
  if (!ip.fromString(host)) return host.endsWith(".local") || host.endsWith(".lan") || host.endsWith(".home");
  return ip[0] == 10 || ip[0] == 127 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) || (ip[0] == 192 && ip[1] == 168) ||
         (ip[0] == 169 && ip[1] == 254);
}

String srvHmacHex(const String& key, const String& a, const String& b, const String& c) {
  uint8_t out[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  bool ok = info != nullptr && mbedtls_md_setup(&ctx, info, 1) == 0 &&
            mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length()) == 0 &&
            mbedtls_md_hmac_update(&ctx, (const unsigned char*)a.c_str(), a.length()) == 0 &&
            mbedtls_md_hmac_update(&ctx, (const unsigned char*)b.c_str(), b.length()) == 0 &&
            mbedtls_md_hmac_update(&ctx, (const unsigned char*)c.c_str(), c.length()) == 0 &&
            mbedtls_md_hmac_finish(&ctx, out) == 0;
  mbedtls_md_free(&ctx);
  if (!ok) return "";
  static const char hx[] = "0123456789abcdef";
  String s;
  s.reserve(64);
  for (int i = 0; i < 32; i++) {
    s += hx[out[i] >> 4];
    s += hx[out[i] & 15];
  }
  return s;
}

bool srvSigEqual(const String& a, const String& b) {
  if (a.length() != 64 || b.length() != 64) return false;
  uint8_t diff = 0;
  for (int i = 0; i < 64; i++) {
    char x = b[i];
    if (x >= 'A' && x <= 'F') x = (char)(x - 'A' + 'a');
    diff |= (uint8_t)(a[i] ^ x);
  }
  return diff == 0;
}

uint32_t srvQCrc(const SrvQueue& q) {
  return statsCrc32((const uint8_t*)&q, sizeof(SrvQueue) - sizeof(uint32_t));
}

bool srvQValid(const SrvQueue& q) {
  return q.magic == SRV_MAGIC && q.count <= SRV_Q_MAX && q.sid != 0 && q.crc == srvQCrc(q);
}

void srvQInit(SrvQueue& q, uint32_t sid) {
  memset(&q, 0, sizeof(SrvQueue));
  q.magic = SRV_MAGIC;
  q.sid = sid == 0 ? 1 : sid;
}

bool srvQPush(SrvQueue& q, uint8_t k, uint32_t ep, uint32_t up, uint32_t boot, uint32_t d, uint8_t pw) {
  bool dropped = false;
  if (q.count >= SRV_Q_MAX) {
    int victim = 0;
    for (int i = 0; i < q.count; i++) {
      if (q.e[i].k == SRV_EV_BLIP || q.e[i].k == SRV_EV_BOOT) {
        victim = i;
        break;
      }
    }
    for (int i = victim; i + 1 < q.count; i++) q.e[i] = q.e[i + 1];
    q.count--;
    dropped = true;
  }
  SrvEvent& e = q.e[q.count];
  memset(&e, 0, sizeof(SrvEvent));
  e.q = ++q.seq;
  e.k = k;
  e.ep = ep;
  e.up = up;
  e.boot = boot;
  e.d = d;
  e.pw = pw;
  q.count++;
  return !dropped;
}

int srvQAck(SrvQueue& q, uint32_t ack) {
  int keep = 0;
  for (int i = 0; i < q.count; i++) {
    if (q.e[i].q > ack) q.e[keep++] = q.e[i];
  }
  const int removed = q.count - keep;
  q.count = (uint8_t)keep;
  return removed;
}

bool srvQFixTimes(SrvQueue& q, uint32_t nowEpoch, uint32_t nowUp, uint32_t boot) {
  bool changed = false;
  if (nowEpoch < EPOCH_MIN_VALID) return false;
  for (int i = 0; i < q.count; i++) {
    SrvEvent& e = q.e[i];
    if (e.ep != 0 || e.boot != boot || e.up > nowUp || nowEpoch - EPOCH_MIN_VALID < nowUp - e.up) continue;
    e.ep = nowEpoch - (nowUp - e.up);
    changed = true;
  }
  return changed;
}

const char* srvEvName(uint8_t k) {
  switch (k) {
    case SRV_EV_OFF: return "off";
    case SRV_EV_ON: return "on";
    case SRV_EV_BLIP: return "blip";
    case SRV_EV_BOOT: return "boot";
  }
  return "";
}

String srvEventsJson(const SrvQueue& q, int maxN) {
  String j = "[";
  int n = 0;
  for (int i = 0; i < q.count && n < maxN; i++) {
    const SrvEvent& e = q.e[i];
    j += String(n ? "," : "") + "{\"q\":" + String(e.q) + ",\"k\":\"" + srvEvName(e.k) + "\",\"ep\":" + String(e.ep) +
         ",\"up\":" + String(e.up) + ",\"d\":" + String(e.d) + ",\"pw\":" + String(e.pw) + "}";
    n++;
  }
  return j + "]";
}

uint8_t srvPartsFix(uint8_t parts, uint8_t bit, bool on) {
  uint8_t p = on ? (uint8_t)(parts | bit) : (uint8_t)(parts & ~bit);
  if (on && (bit & SRV_P_IMG)) p |= SRV_P_YASNO;
  if (!(p & SRV_P_YASNO)) p &= (uint8_t)~SRV_P_IMG;
  if (on && (bit & SRV_P_WEEK)) p |= SRV_P_HIST;
  if (!(p & SRV_P_HIST)) p &= (uint8_t)~SRV_P_WEEK;
  return (uint8_t)(p & SRV_P_READY);
}

uint8_t srvPartsMigrate(uint8_t stored, bool marked) {
  uint8_t p = (uint8_t)(stored & SRV_P_READY);
  if (!marked && p != 0) p |= SRV_P_ALIVE;
  return p;
}

String srvHbLabel(uint16_t sec) {
  if (sec < 60) return String(sec) + " с";
  return String(sec / 60) + " хв";
}

String srvAliveText(uint16_t sec) {
  const uint32_t t = srvOfflineSec(sec);
  if (t < 3600UL) return String((t + 59UL) / 60UL) + " хв";
  const uint32_t m = (t + 59UL) / 60UL;
  return String(m / 60UL) + " год" + (m % 60UL ? " " + String(m % 60UL) + " хв" : String(""));
}

uint32_t srvOfflineSec(uint16_t hb) {
  const uint32_t t = 3UL * hb + 60UL;
  return t < 180UL ? 180UL : t;
}

bool srvHbAllowed(bool on, uint16_t sec) {
  (void)on;
  for (unsigned i = 0; i < sizeof(SRV_HB_OPTS) / sizeof(uint16_t); i++) {
    if (SRV_HB_OPTS[i] == sec) return true;
  }
  return false;
}

uint32_t srvDataDelayMs(uint8_t fails) {
  uint32_t d = SRV_DATA_MIN_MS;
  for (uint8_t i = 0; i < fails && d < SRV_DATA_MAX_DELAY_MS; i++) d *= 2;
  return d < SRV_DATA_MAX_DELAY_MS ? d : SRV_DATA_MAX_DELAY_MS;
}

uint32_t srvRetrySec(uint8_t fails, uint16_t hb) {
  const int n = (int)(sizeof(SRV_RETRY_SEC) / sizeof(uint16_t));
  if (fails == 0 || fails > n) return hb;
  const uint32_t r = SRV_RETRY_SEC[fails - 1];
  return r < hb ? r : hb;
}

String srvPartsName(uint8_t p) {
  p &= SRV_P_READY;
  if (p == 0) return "📟 нічого";
  String s = (p & SRV_P_MIRROR) == SRV_P_MIRROR ? "🪞 усе: " : "";
  bool first = true;
  if (p & SRV_P_HIST) { s += "📊 історія"; first = false; }
  if (p & SRV_P_WEB) { s += String(first ? "" : ", ") + "🌐 сайт"; first = false; }
  if (p & SRV_P_ALIVE) { s += String(first ? "" : ", ") + "📵 стеження за зв'язком"; first = false; }
  if (p & SRV_P_YASNO) { s += String(first ? "" : ", ") + "📅 графік YASNO"; first = false; }
  if (p & SRV_P_IMG) { s += String(first ? "" : ", ") + "🖼 картинка графіка"; first = false; }
  if (p & SRV_P_WEEK) s += String(first ? "" : ", ") + "📈 тиждень";
  return s;
}

String srvErrText(uint8_t e) {
  switch (e) {
    case SRV_E_CONN: return "Сервер не відповідає. Перевірте IP, порт і фаєрвол (порт відкрито?).";
    case SRV_E_NOTLW: return "За цією адресою не сервер Light Watcher.";
    case SRV_E_API: return "Сервер іншої версії API - оновіть сервер.";
    case SRV_E_SIG: return "Сервер не впізнав пристрій: у .env сервера інший токен бота (LW_BOT_TOKEN).";
    case SRV_E_TS: return "Час пристрою й сервера різниться більше ніж на 5 хв.";
    case SRV_E_RSIG: return "Відповідь не підписана - це не ваш сервер.";
    case SRV_E_HTTP: return "Сервер відповів помилкою.";
    case SRV_E_NOKEY: return "Немає ключа: перевірте токен бота.";
  }
  return "";
}

uint8_t srvErrFrom401(const String& body) {
  if (body.indexOf("\"ts\"") >= 0) return SRV_E_TS;
  return SRV_E_SIG;
}

bool srvActive() {
  return srvUrlV.length() > 0 && srvPartsV != 0;
}

bool srvConfigured() {
  return srvUrlV.length() > 0;
}

bool srvYasnoCovered() {
  return srvActive() && (srvPartsV & SRV_P_YASNO) && srvYsOkMs != 0 && millis() - srvYsOkMs < SRV_YS_FRESH_MS &&
         srvYsGroup.length() > 0 && srvYsGroup == yasnoGroup;
}

int8_t srvYasnoImgState(String& fid) {
  fid = "";
  if (!srvActive() || (srvPartsV & (SRV_P_YASNO | SRV_P_IMG)) != (SRV_P_YASNO | SRV_P_IMG)) return 0;
  if (srvYasnoCovered() && (srvPtSrv & SRV_P_IMG) && srvYsImg.length() > 0 && srvYsImgV == srvYsVer) {
    srvImgWaitMs = 0;
    fid = srvYsImg;
    return 1;
  }
  if (srvLastOkMs != 0 && (srvPtSrv & SRV_P_YASNO) && !(srvPtSrv & SRV_P_IMG)) return -2;
  if (srvImgWaitMs == 0) srvImgWaitMs = millis() ? millis() : 1UL;
  return (long)(millis() - srvImgWaitMs) < (long)SRV_IMG_WAIT_MS ? -1 : -2;
}

uint16_t srvYasnoHbCap() {
  if (!(srvPartsV & SRV_P_YASNO) || yasnoGroup.length() == 0) return 0xFFFF;
  const time_t now = (time_t)statsNowEpoch();
  struct tm lt;
  const int hour = now != 0 && localtime_r(&now, &lt) ? lt.tm_hour : 0;
  const uint32_t cap = yasnoScheduleInterval(0, yasnoTomorrowReady(), hour, messageFlag) / 1000UL;
  return cap > 0xFFFF ? 0xFFFF : (uint16_t)cap;
}

static uint16_t srvHbNow() {
  uint16_t hb = messageFlag ? srvHbOffV : srvHbOnV;
  const uint16_t cap = srvYasnoHbCap();
  if (cap < hb) hb = cap;
  if (srvFastSec != 0 && (long)(millis() - srvFastUntil) < 0 && srvFastSec < hb) return srvFastSec;
  return hb;
}

static void srvQSave() {
  srvQ.magic = SRV_MAGIC;
  srvQ.crc = srvQCrc(srvQ);
  srvQDirty = !nvsPutVerified("srvQ", &srvQ, sizeof(SrvQueue));
}

static void srvQLoad() {
  const uint8_t st = nvsReadState("srvQ", &srvQ, sizeof(SrvQueue));
  if (st != 0 || !srvQValid(srvQ)) {
    srvQInit(srvQ, esp_random());
    srvQDirty = true;
  }
}

static void srvKeyMake() {
  srvDevId = WiFi.macAddress();
  srvDevId.toUpperCase();
  srvKeyHex = botToken.length() > 0 ? srvHmacHex(botToken, "lw3:", srvDevId, "") : String("");
}

bool srvWeekOn() {
  return srvActive() && (srvPartsV & SRV_P_WEEK) != 0;
}

bool srvWeekImg(String& fid, String& ver, String& cap) {
  fid = ver = cap = "";
  if (!srvWeekOn() || !(srvPtSrv & SRV_P_WEEK) || srvWkImg.length() == 0 || srvWkVer.length() == 0) return false;
  fid = srvWkImg;
  ver = srvWkVer;
  cap = srvWkCap;
  return true;
}

bool srvImgOn() {
  return (srvPartsV & SRV_P_IMG) != 0;
}

bool srvStoreIs(const String& id) {
  return srvStoreV.length() > 0 && id == srvStoreV;
}

bool srvStoreSet(const String& id) {
  if (id.length() == 0) {
    preferences.remove("imgStore");
    srvStoreV = "";
    return true;
  }
  if (!prefPutStrVerified("imgStore", id)) return false;
  srvStoreV = id;
  return true;
}

String srvStoreForSync() {
  if (srvStoreV.length() == 0) return "";
  for (uint8_t i = 0; i < MAX_CHATS; i++) {
    const ChatEntry& c = chats.e[i];
    if (c.used && c.status != 0 && srvStoreV == c.id) return srvStoreV;
  }
  return "";
}

void srvBegin() {
  srvStoreV = preferences.getString("imgStore", "");
  srvUrlV = preferences.getString("srvUrl", "");
  bkAutoV = preferences.getBool("bkAuto", true);
  if (srvUrlV.length() > 0 && !srvSplitUrl(srvUrlV, srvHost, srvPort)) srvUrlV = "";
  const bool partsMarked = preferences.isKey("srvPv");
  const int storedParts = preferences.getInt("srvParts", 0);
  srvPartsV = srvPartsMigrate((uint8_t)(storedParts & 0xFF), partsMarked);
  if (!partsMarked) {
    if (srvPartsV != storedParts) prefPutIntVerified("srvParts", srvPartsV);
    preferences.putUChar("srvPv", 1);
  }
  const int on = preferences.getInt("srvHbOn", SRV_HB_ON_DEF);
  const int off = preferences.getInt("srvHbOff", SRV_HB_OFF_DEF);
  srvHbOnV = srvHbAllowed(true, (uint16_t)on) ? (uint16_t)on : SRV_HB_ON_DEF;
  srvHbOffV = srvHbAllowed(false, (uint16_t)off) ? (uint16_t)off : SRV_HB_OFF_DEF;
  srvWkH = preferences.getInt("wkH", 2) == 1 ? 1 : 2;
  const String fromPortal = preferences.getString("srvNew", "");
  if (fromPortal.length() > 0) {
    preferences.remove("srvNew");
    String url;
    if (srvParseUrl(fromPortal, url).length() == 0) {
      srvCheckUrl = url;
      srvCheckMsg = 0;
      srvCheckPending = true;
      srvCheckSetMs = millis();
    }
  }
  if (srvUrlV.length() == 0 && !srvCheckPending) return;
  srvKeyMake();
  srvQLoad();
  if (srvActive() && (srvPartsV & SRV_P_HIST)) {
    srvQPush(srvQ, SRV_EV_BOOT, statsNowEpoch(), uptimeSec(), bootId, 0, messageFlag ? 0 : 1);
    srvQSave();
  }
  srvWantData = srvActive() && (srvPartsV & SRV_P_WEB) != 0;
}

void srvEventAt(uint8_t k, unsigned long edgeMs, uint32_t d) {
  if (!srvActive()) return;
  if (!(srvPartsV & SRV_P_HIST)) {
    if (srvPartsV & SRV_P_WEB) srvWantData = true;
    srvLastTryMs = 0;
    return;
  }
  const unsigned long ageMs = millis() - edgeMs;
  const uint32_t now = statsNowEpoch();
  const uint32_t ep = now != 0 && now > ageMs / 1000UL ? now - (uint32_t)(ageMs / 1000UL) : 0;
  const uint32_t nowUp = uptimeSec();
  const uint32_t up = nowUp > ageMs / 1000UL ? nowUp - (uint32_t)(ageMs / 1000UL) : 0;
  srvQPush(srvQ, k, ep, up, bootId, d, k == SRV_EV_OFF ? 0 : 1);
  srvQSave();
  srvQStuck = false;
  if (srvPartsV & SRV_P_WEB) srvWantData = true;
}

static int srvRequest(const char* method, const String& path, const String& body, bool sign, String& resp) {
  resp = "";
  WiFiClient c;
  wdtFeed();
  IPAddress ip;
  if (!netDnsQuick(srvHost.c_str(), SRV_DNS_WAIT_MS, &ip)) return -1;
  if (!c.connect(ip, srvPort, SRV_CONNECT_MS)) return -1;
  String head = String(method) + " " + path + " HTTP/1.1\r\nHost: " + srvHost + ":" + String(srvPort) +
                "\r\nConnection: close\r\nUser-Agent: LightWatcher/" LW_VERSION "\r\n";
  String nSent = "";
  if (sign) {
    const bool tt = timeTrusted();
    const String ts = String(tt ? statsNowEpoch() : uptimeSec());
    char nb[24];
    snprintf(nb, sizeof(nb), "%08lx.%lu", (unsigned long)bootId, (unsigned long)(++srvN));
    const String n = nb;
    head += "Content-Type: application/json\r\nX-LW-Dev: " + srvDevId + "\r\nX-LW-Ts: " + ts + "\r\nX-LW-N: " + n +
            "\r\nX-LW-Sig: " + srvHmacHex(srvKeyHex, ts + "." + n + ".", body, "") + "\r\n";
    nSent = n;
  }
  head += "Content-Length: " + String(body.length()) + "\r\n\r\n";
  if (c.print(head) != head.length()) {
    c.stop();
    return -2;
  }
  size_t off = 0;
  const unsigned long t0 = millis();
  while (off < body.length()) {
    const size_t part = body.length() - off > 1024 ? 1024 : body.length() - off;
    const size_t w = c.write((const uint8_t*)body.c_str() + off, part);
    if (w == 0) {
      if (millis() - t0 > SRV_READ_MS || !c.connected()) {
        c.stop();
        return -2;
      }
      delay(5);
      continue;
    }
    off += w;
    wdtFeed();
  }
  String line = "";
  int status = 0;
  long len = -1;
  bool headDone = false;
  String sig = "";
  const unsigned long t1 = millis();
  while (millis() - t1 < SRV_READ_MS) {
    if (!c.available()) {
      if (!c.connected()) break;
      delay(5);
      wdtFeed();
      continue;
    }
    const char ch = (char)c.read();
    if (!headDone) {
      if (ch == '\r') continue;
      if (ch != '\n') {
        if (line.length() < 200) line += ch;
        continue;
      }
      if (status == 0) {
        const int sp = line.indexOf(' ');
        status = sp > 0 ? line.substring(sp + 1, sp + 4).toInt() : -3;
      } else if (line.length() == 0) {
        headDone = true;
        if (len == 0) break;
      } else {
        String low = line;
        low.toLowerCase();
        if (low.startsWith("content-length:")) len = line.substring(15).toInt();
        else if (low.startsWith("x-lw-sig:")) {
          sig = line.substring(9);
          sig.trim();
        }
      }
      line = "";
      continue;
    }
    if (resp.length() < SRV_RESP_MAX) resp += ch;
    if (len >= 0 && (long)resp.length() >= len) break;
  }
  c.stop();
  if (status <= 0) return -3;
  if (!headDone) return -2;
  if (sign && status == 200) {
    if (!srvSigEqual(srvHmacHex(srvKeyHex, nSent + ".", resp, ""), sig)) return -4;
  }
  return status;
}

static void srvWeekFromSync(JsonDocument& doc) {
  const char* img = doc["wki"] | "";
  const char* ver = doc["wkv"] | "";
  const char* cap = doc["wkt"] | "";
  if (strlen(img) == 0 || strlen(img) > SRV_IMG_ID_MAX || strlen(ver) == 0 || strlen(ver) > 16) return;
  srvWkImg = img;
  srvWkVer = ver;
  srvWkCap = strlen(cap) <= WK_CAP_MAX ? String(cap) : String("");
}

static void srvYasnoFromSync(JsonDocument& doc) {
  const char* img = doc["ysi"] | "";
  srvYsImg = strlen(img) <= SRV_IMG_ID_MAX ? String(img) : String("");
  srvYsImgV = doc["yiv"] | "";
  if ((doc["ysf"] | 0) != 1) {
    srvYsOkMs = 0;
    return;
  }
  JsonObjectConst ys = doc["ys"].as<JsonObjectConst>();
  if (ys.isNull()) {
    if (srvYsVer.length() > 0 && srvYsGroup == yasnoGroup) srvYsOkMs = millis();
    return;
  }
  const String ver = ys["v"] | "";
  const String group = ys["g"] | "";
  if (ver.length() == 0 || group != yasnoGroup) return;
  if (yasnoApplyFromServer(group, ys["td"] | "", ys["tm"] | "", ys["dt"] | "", ys["dm"] | "", ys["st"] | "", ys["sm"] | "", ys["up"] | "")) {
    srvYsVer = ver;
    srvYsGroup = group;
    srvYsOkMs = millis();
    srvImgWaitMs = 0;
  }
}

static uint8_t srvSyncOnce(bool hello, bool bye) {
  if (srvKeyHex.length() == 0) return SRV_E_NOKEY;
  if (timeTrusted() && srvQFixTimes(srvQ, statsNowEpoch(), uptimeSec(), bootId)) srvQSave();
  String b;
  b.reserve(1200);
  b += "{\"fw\":\"" LW_VERSION "\",\"up\":" + String(uptimeSec()) + ",\"ep\":" + String(statsNowEpoch()) +
       ",\"tt\":" + String(timeTrusted() ? 1 : 0) + ",\"hb\":" + String(srvHbNow()) + ",\"hbOn\":" + String(srvHbOnV) +
       ",\"hbOff\":" + String(srvHbOffV) + ",\"pt\":" + String(bye ? 0 : srvPartsV) + ",\"sid\":" + String(srvQ.sid) +
       ",\"kp\":" + String(srvKeepMax);
  if (!hello && !bye && (srvPartsV & (SRV_P_YASNO | SRV_P_WEEK)) && yasnoGroup.length() > 0) {
    b += ",\"ysg\":\"" + jsonEsc(yasnoGroup) + "\",\"ysv\":\"" + jsonEsc(srvYsVer) + "\"";
  }
  if (!hello && !bye && (srvPartsV & SRV_P_WEEK) && wkWanted()) b += ",\"wkh\":" + String(srvWkH);
  if (!hello && !bye && (srvPartsV & (SRV_P_IMG | SRV_P_WEEK))) {
    const String store = srvStoreForSync();
    if (store.length() > 0) b += ",\"stc\":\"" + jsonEsc(store) + "\"";
  }
  if (hello) b += ",\"hello\":1";
  if (bye) b += ",\"bye\":1";
  const unsigned long vAge = vmLastVMs ? (millis() - vmLastVMs) / 1000UL : 0xFFFFFFFFUL;
  b += ",\"st\":{\"pw\":" + String(messageFlag ? 0 : 1) + ",\"v\":" +
       (!messageFlag && !isnan(vmLastV) && vAge <= 900UL ? String(vmLastV, 1) : String("null")) +
       ",\"rssi\":" + String(WiFi.RSSI()) + ",\"heap\":" + String(ESP.getFreeHeap()) + "}";
  b += ",\"ev\":" + ((hello || bye) ? String("[]") : srvEventsJson(srvQ, SRV_EV_SEND_MAX)) + "}";
  String resp;
  const int st = srvRequest("POST", "/api/v1/dev/sync", b, true, resp);
  if (st == -1 || st == -2 || st == -3) return SRV_E_CONN;
  if (st == -4) return SRV_E_RSIG;
  if (st == 401) return srvErrFrom401(resp);
  if (st == 404) return SRV_E_NOTLW;
  if (st != 200) return SRV_E_HTTP;
  JsonDocument doc;
  if (deserializeJson(doc, resp) || (doc["ok"] | 0) != 1) return SRV_E_NOTLW;
  if (bye) return SRV_OK;
  const uint32_t ack = doc["ack"] | 0;
  if (srvQAck(srvQ, ack) > 0) srvQSave();
  srvPtSrv = (uint8_t)((doc["pt"] | 0) & 0xFF);
  const uint16_t fast = doc["fast"] | 0;
  if (fast >= SRV_FAST_MIN_SEC) {
    srvFastSec = fast;
    srvFastUntil = millis() + SRV_FAST_MAX_MS;
  }
  if ((doc["dat"] | 0) == 1 && (srvPartsV & SRV_P_WEB)) srvWantData = true;
  if (srvPartsV & SRV_P_YASNO) srvYasnoFromSync(doc);
  if (srvPartsV & SRV_P_WEEK) srvWeekFromSync(doc);
  JsonArrayConst keep = doc["keep"].as<JsonArrayConst>();
  for (JsonVariantConst v : keep) {
    const int32_t id = v | 0;
    if (id > 0) {
      tgKeepAdd(id);
      if (id > srvKeepMax) srvKeepMax = id;
    }
  }
  return SRV_OK;
}

static uint8_t srvHealth() {
  String resp;
  const int st = srvRequest("GET", "/healthz", "", false, resp);
  if (st < 0) return SRV_E_CONN;
  if (st != 200) return SRV_E_NOTLW;
  JsonDocument doc;
  if (deserializeJson(doc, resp) || (doc["ok"] | 0) != 1 || !doc["api"].is<int>()) return SRV_E_NOTLW;
  if ((doc["api"] | 0) != SRV_API) return SRV_E_API;
  return SRV_OK;
}

static int srvDataUpload() {
  String body = webDataJson(false, true);
  String resp;
  return srvRequest("POST", "/api/v1/dev/data", body, true, resp);
}

static void srvFarewell(const String& url, bool bye) {
  srvFwUrl = url;
  srvFwBye = bye;
  srvFwTries = 0;
  srvFwNextMs = 0;
}

static bool srvFarewellTick() {
  if (srvFwUrl.length() == 0) return false;
  if (srvFwNextMs != 0 && (long)(millis() - srvFwNextMs) < 0) return false;
  String host;
  uint16_t port = SRV_PORT_DEF;
  if (!srvSplitUrl(srvFwUrl, host, port) || srvKeyHex.length() == 0) {
    srvFwUrl = "";
    return false;
  }
  const String keepHost = srvHost;
  const uint16_t keepPort = srvPort;
  srvHost = host;
  srvPort = port;
  tgSendAttemptedThisPass = true;
  const uint8_t r = srvSyncOnce(false, srvFwBye);
  srvHost = keepHost;
  srvPort = keepPort;
  if (r == SRV_OK || r == SRV_E_SIG || r == SRV_E_NOTLW || ++srvFwTries >= SRV_FW_TRIES) {
    srvFwUrl = "";
    return true;
  }
  srvFwNextMs = millis() + srvRetrySec(srvFwTries < 4 ? srvFwTries : 4, 300) * 1000UL;
  return true;
}

static bool srvBackupTick(unsigned long now) {
  if (!bkAutoV) return false;
  if (bkCheckMs != 0 && now - bkCheckMs < BK_CHECK_MS) return false;
  bkCheckMs = now;
  const String json = bkBuildJson();
  const uint32_t h = statsCrc32((const uint8_t*)json.c_str(), json.length());
  if (h == bkSentHash) return false;
  tgSendAttemptedThisPass = true;
  String resp;
  const int st = srvRequest("POST", "/api/v1/dev/cfg", json, true, resp);
  if (st == 200) {
    bkSentHash = h;
    bkSavedEpoch = statsNowEpoch();
  } else {
    bkCheckMs = now - BK_CHECK_MS + bkRetryMs(st);
  }
  return true;
}

static void bkDropPending() {
  if (bkPending != nullptr) {
    delete bkPending;
    bkPending = nullptr;
  }
  bkNonce = "";
}

static void bkWait(const String& text) {
  bkWaitMsg = tgSendText(chatId, text, "", "") ? (int)bot.last_sent_message_id : 0;
}

static String bkCopies(uint16_t n) {
  if (n % 10 == 1 && n % 100 != 11) return "копія";
  if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14)) return "копії";
  return "копій";
}

static bool bkListFetch() {
  String resp;
  const int st = srvRequest("POST", "/api/v1/dev/cfg/list", bkRestoreBody(), true, resp);
  bkItemN = 0;
  bkOwnN = 0;
  bkOtherN = 0;
  if (st == 404) {
    bkListSt = -2;
    return false;
  }
  JsonDocument doc;
  if (st != 200 || deserializeJson(doc, resp) || (doc["ok"] | 0) != 1) {
    bkListSt = -1;
    return false;
  }
  bkOwnN = doc["own"] | 0;
  bkOtherN = doc["other"] | 0;
  for (JsonObjectConst it : doc["items"].as<JsonArrayConst>()) {
    if (bkItemN >= BK_LIST_MAX) break;
    BkItem& x = bkItems[bkItemN];
    x.id = it["i"] | 0;
    x.at = it["at"] | 0;
    x.own = (it["o"] | 0) == 1;
    x.same = (it["s"] | 0) == 1;
    if (x.id != 0) bkItemN++;
  }
  bkListSt = 1;
  return true;
}

String bkCardText() {
  String t;
  if (bkListSt == -2) {
    t = "💾 Сервер ще не вміє показувати й видаляти копії - оновіть сервер. «♻️ Відновити» поверне найновішу копію, як раніше.";
  } else if (bkListSt != 1) {
    t = "💾 Не вдалося отримати список копій: сервер не відповідає.";
  } else {
    t = "💾 На сервері: <b>" + String(bkOwnN) + " " + bkCopies(bkOwnN) + "</b> цієї плати";
    if (bkOtherN) t += " і " + String(bkOtherN) + " " + bkCopies(bkOtherN) + " інших плат";
    for (uint8_t k = 0; k < bkItemN; k++) {
      if (!bkItems[k].own) continue;
      t += " · остання " + formatEpochLocal((time_t)bkItems[k].at, "%d.%m %H:%M");
      break;
    }
    bool same = false;
    for (uint8_t k = 0; k < bkItemN; k++) same = same || bkItems[k].same;
    t += same ? "\n✅ Поточні налаштування збережено" : "\n⚠️ Поточних налаштувань на сервері ще немає";
  }
  t += bkAutoV ? "\n🔄 Автозбереження: кожні 10 хв, якщо налаштування змінилися" : "\n❌ Автозбереження вимкнено";
  return t;
}

String bkItemLabel(uint8_t k) {
  if (k >= bkItemN) return "";
  const BkItem& x = bkItems[k];
  return String(k + 1) + " · " + formatEpochLocal((time_t)x.at, "%d.%m %H:%M") + (x.own ? String("") : String(" · інша плата")) +
         (x.same ? String(" · як зараз") : String(""));
}

static int bkItemPick(const String& text) {
  if (text.length() == 0 || !isDigit(text[0])) return -1;
  const int k = text.toInt() - 1;
  return k >= 0 && k < bkItemN && text == bkItemLabel((uint8_t)k) ? k : -1;
}

String bkListText(const String& st) {
  if (bkListSt != 1) return bkCardText();
  if (bkItemN == 0) return "На сервері немає копій.";
  if (st == "bkd") return "Оберіть копію, яку видалити з сервера.";
  return "Оберіть копію. Відновлення замінить групи й канали, мої сповіщення, адресу YASNO, напругу, вебсторінку, час і частоту сервера, потім пристрій перезапуститься.";
}

static void bkShow(const String& note) {
  bkWaitMsg = 0;
  menuOpenClean("bk", (note.length() > 0 ? note + "\n\n" : String("")) + bkCardText());
}

static void srvRestoreFetch(uint32_t id) {
  bkDropPending();
  String text;
  String resp;
  const String body = id ? "{\"i\":" + String(id) + "}" : bkRestoreBody();
  const int st = srvKeyHex.length() ? srvRequest("POST", "/api/v1/dev/cfg/get", body, true, resp) : -1;
  JsonDocument doc;
  if (st != 200 || deserializeJson(doc, resp) || (doc["ok"] | 0) != 1) {
    text = "❌ Не вдалося отримати копію: сервер не відповідає або це не ваш сервер.";
  } else if ((doc["found"] | 0) != 1) {
    text = id ? String("Цієї копії вже немає на сервері.")
              : String("На сервері немає копії, що відрізнялася б від поточних налаштувань. Копія з'являється сама через кілька хвилин після підключення сервера або зміни налаштувань.");
  } else {
    String data;
    serializeJson(doc["data"], data);
    bkPending = new BkData();
    const String err = bkParse(data, *bkPending);
    if (err.length() > 0) {
      bkDropPending();
      text = "❌ Копію не можна застосувати: " + htmlEsc(err) + ".";
    } else {
      const uint32_t at = doc["at"] | 0;
      const String from = doc["from"] | "";
      char nb[9];
      snprintf(nb, sizeof(nb), "%08lx", (unsigned long)esp_random());
      bkNonce = nb;
      bkNonceMs = millis();
      text = "♻️ <b>Відновити налаштування з копії?</b>\nКопія від " + (at ? formatEpochLocal((time_t)at, "%d.%m.%Y %H:%M") : String("?")) +
             (from.length() && from != srvDevId ? " (інший пристрій " + htmlEsc(from) + ")" : String("")) + "\n" + bkSummary(*bkPending) +
             "\n\nБуде замінено: групи й канали, мої сповіщення, адреса YASNO, напруга, вебсторінка, час, частота сервера. "
             "Wi-Fi, токен, власник і пароль адмінпанелі не змінюються. Після відновлення пристрій перезапуститься.";
      if (bkWaitMsg > 0) {
        int wid = bkWaitMsg;
        menuDeleteIds(&wid, 1);
        bkWaitMsg = 0;
      }
      menuConfirm("bk", "", "bk", text, "✅ Так, відновити");
      return;
    }
  }
  bkShow(text);
}

static void bkRun() {
  const uint8_t a = bkAct;
  bkAct = 0;
  if (a == BK_ACT_GET) {
    srvRestoreFetch(bkActId);
    return;
  }
  String note = "";
  String resp;
  if (a == BK_ACT_SYNC) {
    const String json = bkBuildJson();
    const uint32_t h = statsCrc32((const uint8_t*)json.c_str(), json.length());
    const int st = srvRequest("POST", "/api/v1/dev/cfg", json, true, resp);
    if (st == 200) {
      bkSentHash = h;
      bkSavedEpoch = statsNowEpoch();
      bkCheckMs = millis();
      note = "✅ Збережено на сервер" + (bkSavedEpoch ? " о " + formatEpochLocal((time_t)bkSavedEpoch, "%H:%M") : String("")) + ".";
    } else {
      note = "❌ Не вдалося зберегти: сервер не відповідає.";
    }
  } else if (a == BK_ACT_DEL || a == BK_ACT_DELALL) {
    const String body = a == BK_ACT_DEL ? "{\"i\":" + String(bkActId) + "}" : String("{\"all\":1}");
    const int st = srvRequest("POST", "/api/v1/dev/cfg/del", body, true, resp);
    JsonDocument doc;
    if (st == 404) {
      note = "❌ Сервер ще не вміє видаляти копії - оновіть сервер.";
    } else if (st != 200 || deserializeJson(doc, resp) || (doc["ok"] | 0) != 1) {
      note = "❌ Не вдалося видалити: сервер не відповідає.";
    } else {
      const int n = doc["n"] | 0;
      if (a == BK_ACT_DELALL) {
        if (bkAutoV && prefPutBoolVerified("bkAuto", false)) bkAutoV = false;
        note = "🗑 Видалено " + String(n) + " " + bkCopies((uint16_t)n) + " цієї плати." + (bkAutoV ? String("") : String(" Автозбереження вимкнено."));
      } else {
        note = n > 0 ? String("🗑 Копію видалено.") : String("Цієї копії вже немає на сервері.");
      }
    }
  }
  bkListFetch();
  bkShow(note);
}

static bool srvSafeBlocked() {
  if (!safeMode) return false;
  tgSendText(chatId, "🛡 Безпечний режим: зв'язок із сервером вимкнено, поки він триває (до 1 год). Спробуйте пізніше.", "", "");
  return true;
}

void srvMenuBackup() {
  if (srvSafeBlocked()) return;
  if (!srvConfigured()) {
    menuOpen("srv", srvMenuText());
    return;
  }
  bkWait("⏳ Зв'язуюся із сервером…");
  bkAct = BK_ACT_OPEN;
}

static void srvNoticeTick() {
  if (srvNote != 1 && srvNote != 3) return;
  if (tgSendAttemptedThisPass || !telegramOnline || powerChangePending() || !tgNetAllowed()) return;
  tgSendAttemptedThisPass = true;
  const String host = htmlEsc(srvShortUrl(srvUrlV));
  const bool ok = srvNote == 1
    ? tgSendOwnerInfo("🖥 Сервер <code>" + host + "</code> недоступний понад 1 год. Пристрій працює сам, події чекають у черзі.")
    : tgSendOwnerInfo("🖥 Сервер <code>" + host + "</code> знову на зв'язку.");
  if (ok) srvNote = srvNote == 1 ? 2 : 0;
}

String srvCheckKb() {
  return "[[{\"text\":\"🪞 Увімкнути все\",\"callback_data\":\"x:sm:" + srvNonce + "\"},{\"text\":\"🧩 Вибрати\",\"callback_data\":\"x:sc\"}],"
         "[{\"text\":\"Пізніше\",\"callback_data\":\"x:sl\"}]]";
}

static void srvNewNonce() {
  char nb[9];
  snprintf(nb, sizeof(nb), "%08lx", (unsigned long)esp_random());
  srvNonce = nb;
  srvNonceMs = millis();
}

static void srvCheckRun() {
  srvCheckPending = false;
  String host;
  uint16_t port = SRV_PORT_DEF;
  if (!srvSplitUrl(srvCheckUrl, host, port)) return;
  const String oldHost = srvHost;
  const uint16_t oldPort = srvPort;
  srvHost = host;
  srvPort = port;
  if (srvKeyHex.length() == 0) srvKeyMake();
  if (srvQ.magic != SRV_MAGIC) srvQLoad();
  const unsigned long t0 = millis();
  uint8_t r = srvHealth();
  if (r == SRV_OK) r = srvSyncOnce(true, false);
  const unsigned long ms = millis() - t0;
  const String shown = htmlEsc(srvShortUrl(srvCheckUrl));
  String text;
  String kb = "[]";
  if (r != SRV_OK) {
    srvHost = oldHost;
    srvPort = oldPort;
    text = "❌ <b>" + shown + "</b>\n" + htmlEsc(srvErrText(r)) + (srvUrlV.length() > 0 && srvCheckUrl != srvUrlV ? "\nАдресу не змінено." : "");
  } else {
    const bool changed = srvCheckUrl != srvUrlV;
    bool saved = true;
    if (changed) {
      saved = prefPutStrVerified("srvUrl", srvCheckUrl);
      if (saved) {
        if (srvUrlV.length() > 0) srvFarewell(srvUrlV, true);
        srvUrlV = srvCheckUrl;
        srvFailN = 0;
        srvFirstFailMs = 0;
        srvNote = 0;
        srvLastTryMs = 0;
        srvQStuck = false;
        srvDataFailN = 0;
        srvYsOkMs = 0;
        srvYsVer = "";
        srvYsGroup = "";
        bkSentHash = 0;
        if (srvPartsV & SRV_P_WEB) srvWantData = true;
      } else {
        srvHost = oldHost;
        srvPort = oldPort;
        if (srvPartsV != 0) srvFarewell(srvCheckUrl, true);
      }
    }
    if (!saved) {
      text = "⚠️ Сервер відповів, але адресу не збережено: запис у пам'ять пристрою не підтвердився. Спробуйте ще раз.";
    } else {
      srvLastOkMs = millis();
      srvLastErr = SRV_OK;
      text = "✅ Сервер <code>" + shown + "</code> на зв'язку (" + String(ms) + " мс), API v" + String(SRV_API) + ".";
      if (srvIsLocalHost(srvHost)) text += "\n⚠️ Сервер у домашній мережі вимкнеться разом зі світлом.";
      if (changed && srvPartsV != 0) text += "\nАдресу змінено: дані йдуть на новий сервер (" + srvPartsName(srvPartsV) + "), старий сервер відв'язано.";
      if (srvPartsV == 0) {
        text += "\nПоки на сервері нічого - пристрій працює як раніше.";
        srvNewNonce();
        kb = srvCheckKb();
      }
    }
  }
  tgSendAttemptedThisPass = true;
  if (srvCheckMsg > 0) {
    tgEditText(chatId, srvCheckMsg, text, "HTML", tgInlineKb(kb));
  } else {
    tgSendText(chatId, "🖥 <b>Сервер з порталу налаштування</b>\n" + text, "HTML", tgInlineKb(kb));
  }
  srvCheckMsg = 0;
}

void srvCheckStart(const String& url) {
  if (srvSafeBlocked()) return;
  srvCheckUrl = url;
  srvCheckPending = true;
  srvCheckSetMs = millis();
  srvCheckMsg = 0;
  if (tgSendText(chatId, "⏳ Перевіряю <code>" + htmlEsc(srvShortUrl(url)) + "</code>…", "HTML", "")) {
    srvCheckMsg = (int)bot.last_sent_message_id;
  }
}

void srvTick() {
  if (safeMode) return;
  srvNoticeTick();
  if (srvUrlV.length() == 0 && !srvCheckPending && srvFwUrl.length() == 0) return;
  if (tgSendAttemptedThisPass || WiFi.status() != WL_CONNECTED || powerChangePending() || yasnoTaskRunning) return;
  if (srvFarewellTick()) return;
  if (bkAct) {
    tgSendAttemptedThisPass = true;
    bkRun();
    return;
  }
  if (srvCheckPending) {
    if (srvCheckMsg == 0 && !telegramOnline) {
      if (millis() - srvCheckSetMs > 10UL * SRV_CHECK_WAIT_MS) srvCheckPending = false;
      return;
    }
    tgSendAttemptedThisPass = true;
    srvCheckRun();
    return;
  }
  if (!srvActive()) return;
  if (srvQDirty) srvQSave();
  const unsigned long now = millis();
  if (srvWantData && srvFailN == 0 && srvLastOkMs != 0 && (srvPartsV & SRV_P_WEB) &&
      (srvLastDataMs == 0 || now - srvLastDataMs >= srvDataDelayMs(srvDataFailN)) && now - srvLastTryMs >= 1000UL) {
    tgSendAttemptedThisPass = true;
    srvLastTryMs = now;
    srvLastDataMs = now;
    const int st = srvDataUpload();
    if (st == 200) {
      srvWantData = false;
      srvDataFailN = 0;
    } else if (st == 400 || st == 413) {
      srvWantData = false;
      srvDataFailN = 0;
      logEvent(LC_SYS, LS_WARN, "Сервер не прийняв дані сторінки: " + String(st));
    } else if (srvDataFailN < 250) {
      srvDataFailN++;
    }
    return;
  }
  if (srvFailN == 0 && srvLastOkMs != 0 && now - srvLastTryMs >= 1000UL && srvBackupTick(now)) return;
  const uint32_t waitSec = srvRetrySec(srvFailN, srvHbNow());
  const bool queued = srvQ.count > 0 && srvFailN == 0 && !srvQStuck;
  if (srvLastTryMs != 0 && now - srvLastTryMs < (queued ? 1000UL : waitSec * 1000UL)) return;
  tgSendAttemptedThisPass = true;
  srvLastTryMs = now;
  const uint8_t before = srvQ.count;
  const uint8_t r = srvSyncOnce(false, false);
  srvLastErr = r;
  if (r == SRV_OK) {
    srvQStuck = srvQ.count > 0 && srvQ.count >= before;
    if (srvFailN >= 3) logEvent(LC_SYS, LS_INFO, "Сервер знову на зв'язку");
    srvFailN = 0;
    srvFirstFailMs = 0;
    srvLastOkMs = now;
    if (srvNote == 2) srvNote = 3;
    else if (srvNote == 1) srvNote = 0;
    return;
  }
  if (srvFailN == 0) srvFirstFailMs = now;
  if (srvFailN < 250) srvFailN++;
  if (srvFailN == 3) logEvent(LC_SYS, LS_WARN, "Сервер не відповідає: " + String(r));
  if (srvNote == 0 && now - srvFirstFailMs >= SRV_DOWN_NOTE_MS) srvNote = 1;
}

String srvStateLine() {
  if (srvUrlV.length() == 0) return "не підключено";
  const String host = "<code>" + htmlEsc(srvShortUrl(srvUrlV)) + "</code>";
  if (srvPartsV == 0) return host + " - збережено, не використовується";
  const String ys = (srvPartsV & SRV_P_YASNO) ? (srvYasnoCovered() ? String("; графік - із сервера") : String("; графік - поки завантажує пристрій")) : String("");
  if (srvFailN == 0 && srvLastOkMs != 0) return host + " - ✅ на зв'язку, " + formatShortDuration((millis() - srvLastOkMs) / 1000UL) + " тому" + ys;
  if (srvFailN > 0) return host + " - ❌ " + htmlEsc(srvErrText(srvLastErr)) + " Спроб: " + String(srvFailN) + ys;
  return host + " - ⏳ ще не було зв'язку" + ys;
}

String srvShortState() {
  if (srvUrlV.length() == 0) return "не підключено";
  if (srvPartsV == 0) return "не використовується";
  if (srvFailN == 0 && srvLastOkMs != 0) return "на зв'язку";
  if (srvFailN > 0) return "немає зв'язку";
  return "чекаю зв'язку";
}

String srvMenuText() {
  if (srvUrlV.length() == 0) return "Не підключено - пристрій працює сам, як завжди.";
  String t = "Сервер: " + srvStateLine() + "\nНа сервері: " + srvPartsName(srvPartsV);
  if (srvPartsV != 0) {
    t += "\nЗв'язок: ☀️ " + srvHbLabel(srvHbOnV) + " / 🔋 " + srvHbLabel(srvHbOffV);
    if (srvPartsV & SRV_P_HIST) t += " · Черга подій: " + String(srvQ.count);
    if (srvPartsV & SRV_P_WEB) t += "\nСторінка: " + htmlEsc(srvUrlV) + "/";
    t += "\n💾 Копія налаштувань: " + (bkSavedEpoch ? "збережено " + formatEpochLocal((time_t)bkSavedEpoch, "%d.%m %H:%M") : bkSentHash ? String("збережено") : String("ще не надіслано")) + (bkAutoV ? String("") : String(" (автозбереження вимкнено)"));
  }
  return t;
}

String srvDiagLine() {
  return "Сервер: " + srvStateLine() + (srvActive() ? ", черга " + String(srvQ.count) : String("")) + "\n";
}

String srvWeekLabel() {
  return (srvPartsV & SRV_P_WEEK) ? String(srvWkH) + " год" : String("❌");
}

String srvPartsText(uint8_t parts) {
  String t = "🧩 На сервері: " + srvPartsName(parts);
  if (parts & SRV_P_WEEK) {
    t += "\n📈 Картинка за 7 днів оновлюється кожні " + String(srvWkH) + " год в одному закріпленому повідомленні.";
    if (!wkWanted()) t += " Кому - ⚙️ › 🔔 Мої сповіщення або картка групи: «📈 Тиждень».";
  }
  return t;
}

String srvPartsKb(uint8_t draft) {
  return "[[{\"text\":\"📊 Історія: " + onOff(draft & SRV_P_HIST) + "\",\"callback_data\":\"x:sp:h\"}],"
         "[{\"text\":\"🌐 Сайт: " + onOff(draft & SRV_P_WEB) + "\",\"callback_data\":\"x:sp:w\"}],"
         "[{\"text\":\"📵 Стеження за зв'язком: " + onOff(draft & SRV_P_ALIVE) + "\",\"callback_data\":\"x:sp:a\"}],"
         "[{\"text\":\"📅 Графік YASNO: " + onOff(draft & SRV_P_YASNO) + "\",\"callback_data\":\"x:sp:y\"}],"
         "[{\"text\":\"🖼 Картинка графіка: " + onOff(draft & SRV_P_IMG) + "\",\"callback_data\":\"x:sp:i\"}]]";
}

String srvHbText() {
  String t = "☀️ Зі світлом: " + srvHbLabel(srvHbOnV) + "\n🔋 Без світла: " + srvHbLabel(srvHbOffV) +
             "\nПро світло пристрій повідомляє одразу, незалежно від частоти.";
  if (srvPartsV & SRV_P_YASNO) t += "\n📅 З графіком на сервері зв'язок не рідше ніж раз на 15 хв (з 18:00, поки немає графіка на завтра, - 5 хв), як оновлював би графік сам пристрій.";
  if (srvPartsV & SRV_P_ALIVE) {
    t += "\nЯкщо пристрій замовкне, сервер напише через ~" + srvAliveText(srvHbOnV) +
         (srvHbOffV != srvHbOnV ? " (без світла ~" + srvAliveText(srvHbOffV) + ")" : String("")) + ".";
  } else {
    t += "\n📵 Стеження за зв'язком вимкнено (🧩 Що на сервері).";
  }
  return t;
}

String srvHbKb() {
  return "[[{\"text\":\"☀️ Зі світлом: " + srvHbLabel(srvHbOnV) + "\",\"callback_data\":\"x:sh:O\"}],"
         "[{\"text\":\"🔋 Без світла: " + srvHbLabel(srvHbOffV) + "\",\"callback_data\":\"x:sh:F\"}]]";
}

String srvHbPickText(bool on) {
  return String(on ? "☀️ <b>Зі світлом</b>" : "🔋 <b>Без світла</b>") + " - як часто виходити на зв'язок із сервером. Рідше - менше трафіку" +
         (on ? String("") : String(" і заряду акумулятора")) + ", але сервер пізніше помітить, що пристрій замовк.";
}

String srvHbPickKb(bool on) {
  const uint16_t cur = on ? srvHbOnV : srvHbOffV;
  String kb = "[[";
  for (unsigned i = 0; i < sizeof(SRV_HB_OPTS) / sizeof(uint16_t); i++) {
    const uint16_t v = SRV_HB_OPTS[i];
    if (i > 0 && i % 3 == 0) kb += "],[";
    kb += String(i % 3 ? "," : "") + "{\"text\":\"" + srvHbLabel(v) + (v == cur ? " ✓" : "") + "\",\"callback_data\":\"x:sh:" + (on ? "o" : "f") + ":" + String(v) + "\"}";
  }
  return kb + "],[{\"text\":\"◀️ Назад\",\"callback_data\":\"x:sh:B\"}]]";
}

void srvMenuParts() {
  menuOpen("srvp", srvPartsText(srvPartsV));
}

void srvMenuHb() {
  menuOpen("srvh", srvHbText());
}

void srvMenuDetach() {
  menuConfirm("sd", "", "srv", "🗑 <b>Відв'язати сервер?</b>\nПристрій перестане надсилати дані на сервер і працюватиме сам, як до підключення. Історія на сервері лишиться.",
              "✅ Так, відв'язати");
}

String srvMenuKeyboard(const String& state) {
  const String back = kbRow("◀️ Назад", "");
  if (state == "bk") {
    return "[" + kbRow("♻️ Відновити", "🔄 Синхронізувати") + "," + kbRow("🗑 Видалити", "💾 Автозбереження: " + onOff(bkAutoV)) + "," + back + "]";
  }
  if (state == "bkr" || state == "bkd") {
    String kb = "[";
    if (bkListSt == 1) {
      for (uint8_t k = 0; k < bkItemN; k++) kb += kbRow(bkItemLabel(k), "") + ",";
      if (state == "bkd" && bkOwnN > 0) kb += kbRow("🗑 Усі копії цієї плати", "") + ",";
    }
    return kb + back + "]";
  }
  if (state == "srvp") {
    return "[" + kbRow("📊 Історія: " + onOff(srvPartsV & SRV_P_HIST), "🌐 Сайт: " + onOff(srvPartsV & SRV_P_WEB)) + ","
           + kbRow("📵 Стеження: " + onOff(srvPartsV & SRV_P_ALIVE), "📅 YASNO: " + onOff(srvPartsV & SRV_P_YASNO)) + ","
           + kbRow("🖼 Картинка: " + onOff(srvPartsV & SRV_P_IMG), "📈 Тиждень: " + srvWeekLabel()) + "," + back + "]";
  }
  if (state == "srvh") {
    return "[" + kbRow("☀️ Зі світлом: " + srvHbLabel(srvHbOnV), "🔋 Без світла: " + srvHbLabel(srvHbOffV)) + "," + back + "]";
  }
  if (state == "srvhv") {
    const uint16_t cur = srvPickOn ? srvHbOnV : srvHbOffV;
    String kb = "[[";
    for (unsigned i = 0; i < sizeof(SRV_HB_OPTS) / sizeof(uint16_t); i++) {
      const uint16_t v = SRV_HB_OPTS[i];
      if (i > 0 && i % 3 == 0) kb += "],[";
      kb += String(i % 3 ? "," : "") + "\"" + jsonEsc(srvHbLabel(v) + (v == cur ? " ✓" : "")) + "\"";
    }
    return kb + "]," + back + "]";
  }
  return "[" + back + "]";
}

String srvMenuHint(const String& state) {
  if (state == "bk") return "⚙️ › 🖥 › 💾 Копія налаштувань";
  if (state == "bkr") return "⚙️ › 🖥 › 💾 › ♻️ Відновити";
  if (state == "bkd") return "⚙️ › 🖥 › 💾 › 🗑 Видалити";
  if (state == "srvp") return "⚙️ › 🖥 › 🧩 Що на сервері";
  if (state == "srvh") return "⚙️ › 🖥 › ⏱ Частота";
  return srvPickOn ? "⚙️ › 🖥 › ⏱ › ☀️ Зі світлом" : "⚙️ › 🖥 › ⏱ › 🔋 Без світла";
}

static bool bkMenuButton(const String& text) {
  if (menuState == "bk") {
    if (text == "♻️ Відновити") {
      if (bkListSt == -2) {
        if (srvSafeBlocked()) return true;
        bkWait("⏳ Шукаю копію налаштувань на сервері…");
        bkActId = 0;
        bkAct = BK_ACT_GET;
      } else {
        menuOpen("bkr", bkListText("bkr"));
      }
      return true;
    }
    if (text == "🔄 Синхронізувати") {
      if (srvSafeBlocked()) return true;
      bkWait("⏳ Зберігаю налаштування на сервер…");
      bkAct = BK_ACT_SYNC;
      return true;
    }
    if (text == "🗑 Видалити") {
      if (bkListSt == -2) menuRefresh("bk", "❌ Сервер ще не вміє видаляти копії - оновіть сервер.\n\n" + bkCardText());
      else menuOpen("bkd", bkListText("bkd"));
      return true;
    }
    if (text.startsWith("💾 Автозбереження:")) {
      const bool want = !bkAutoV;
      const bool saved = prefPutBoolVerified("bkAuto", want);
      if (saved) {
        bkAutoV = want;
        if (want) {
          bkSentHash = 0;
          bkCheckMs = 0;
        }
      }
      menuRefresh("bk", saved ? bkCardText() : String("❌ Не вдалося зберегти, залишено попереднє значення."));
      return true;
    }
    return false;
  }
  if (menuState == "bkr") {
    const int k = bkItemPick(text);
    if (k < 0) return false;
    if (srvSafeBlocked()) return true;
    bkWait("⏳ Завантажую копію…");
    bkActId = bkItems[k].id;
    bkAct = BK_ACT_GET;
    return true;
  }
  if (menuState == "bkd") {
    if (text == "🗑 Усі копії цієї плати") {
      menuConfirm("bkall", "", "bkd", "🗑 <b>Видалити " + String(bkOwnN) + " " + bkCopies(bkOwnN) + " цієї плати з сервера?</b>\n"
                  "Автозбереження вимкнеться, щоб копія не з'явилася знову. Увімкнути - перемикач «💾 Автозбереження».", "✅ Так, видалити");
      return true;
    }
    const int k = bkItemPick(text);
    if (k < 0) return false;
    const BkItem& x = bkItems[k];
    menuConfirm("bkdel", String(x.id), "bkd", "🗑 <b>Видалити копію від " + formatEpochLocal((time_t)x.at, "%d.%m.%Y %H:%M") +
                (x.own ? String("") : String(" (інша плата)")) + "?</b>" + (x.same ? String("\nЦе копія поточних налаштувань.") : String("")),
                "✅ Так, видалити");
    return true;
  }
  return false;
}

bool srvMenuButton(const String& text) {
  if (bkMenuButton(text)) return true;
  if (menuState == "srvp") {
    uint8_t bit = 0;
    if (text.startsWith("📊 Історія:")) bit = SRV_P_HIST;
    else if (text.startsWith("🌐 Сайт:")) bit = SRV_P_WEB;
    else if (text.startsWith("📵 Стеження:")) bit = SRV_P_ALIVE;
    else if (text.startsWith("📅 YASNO:")) bit = SRV_P_YASNO;
    else if (text.startsWith("🖼 Картинка:")) bit = SRV_P_IMG;
    else if (text.startsWith("📈 Тиждень:")) bit = SRV_P_WEEK;
    if (bit == 0) return false;
    if (srvUrlV.length() == 0) {
      menuOpenClean("srv", srvMenuText());
      return true;
    }
    if (bit == SRV_P_WEEK && (srvPartsV & SRV_P_WEEK) && srvWkH == 2) {
      const bool saved = prefPutIntVerified("wkH", 1);
      if (saved) {
        srvWkH = 1;
        srvLastTryMs = 0;
      }
      menuRefresh("srvp", saved ? srvPartsText(srvPartsV) : String("❌ Не вдалося зберегти, залишено попереднє значення."));
      return true;
    }
    if (bit == SRV_P_WEEK && !(srvPartsV & SRV_P_WEEK) && srvWkH != 2) {
      if (!prefPutIntVerified("wkH", 2)) {
        menuRefresh("srvp", "❌ Не вдалося зберегти, залишено попереднє значення.");
        return true;
      }
      srvWkH = 2;
    }
    const uint8_t want = srvPartsFix(srvPartsV, bit, !(srvPartsV & bit));
    const bool saved = want == srvPartsV || srvSetParts(want);
    menuRefresh("srvp", saved ? srvPartsText(srvPartsV) : String("❌ Не вдалося зберегти, залишено попереднє значення."));
    return true;
  }
  if (menuState == "srvh") {
    if (text.startsWith("☀️ Зі світлом") || text.startsWith("🔋 Без світла")) {
      srvPickOn = text.startsWith("☀️");
      menuRefresh("srvhv", srvHbPickText(srvPickOn));
      return true;
    }
    return false;
  }
  if (menuState == "srvhv") {
    for (unsigned i = 0; i < sizeof(SRV_HB_OPTS) / sizeof(uint16_t); i++) {
      const uint16_t v = SRV_HB_OPTS[i];
      const String l = srvHbLabel(v);
      if (text != l && text != l + " ✓") continue;
      const bool saved = prefPutIntVerified(srvPickOn ? "srvHbOn" : "srvHbOff", (int32_t)v);
      if (saved) {
        if (srvPickOn) srvHbOnV = v;
        else srvHbOffV = v;
        if (srvActive()) srvLastTryMs = 0;
      }
      menuRefresh("srvh", (saved ? String("") : String("❌ Не вдалося зберегти.\n\n")) + srvHbText());
      return true;
    }
    return false;
  }
  return false;
}

static bool srvSetParts(uint8_t p) {
  p &= SRV_P_READY;
  if (!prefPutIntVerified("srvParts", p)) return false;
  preferences.putUChar("srvPv", 1);
  const bool wasOff = srvPartsV == 0;
  const bool histWas = (srvPartsV & SRV_P_HIST) != 0;
  srvPartsV = p;
  if (histWas && !(p & SRV_P_HIST)) {
    srvQ.count = 0;
    srvQSave();
  }
  if (p != 0 && !wasOff) srvLastTryMs = 0;
  if (!(p & SRV_P_YASNO)) {
    srvYsOkMs = 0;
    srvYsVer = "";
    srvYsGroup = "";
    bkSentHash = 0;
  }
  if (!(p & SRV_P_IMG)) {
    srvYsImg = "";
    srvYsImgV = "";
  }
  if (!(p & SRV_P_WEEK)) {
    srvWkImg = "";
    srvWkVer = "";
    srvWkCap = "";
  }
  srvImgWaitMs = 0;
  if (p == 0) {
    srvQ.count = 0;
    srvQSave();
    srvNote = 0;
    srvWantData = false;
    if (!wasOff && srvUrlV.length() > 0) srvFarewell(srvUrlV, false);
    return true;
  }
  if (srvFwUrl == srvUrlV && !srvFwBye) srvFwUrl = "";
  if (wasOff) {
    if (srvKeyHex.length() == 0) srvKeyMake();
    if (srvQ.magic != SRV_MAGIC) srvQLoad();
    srvFailN = 0;
    srvLastTryMs = 0;
    srvLastOkMs = 0;
  }
  if (p & SRV_P_WEB) srvWantData = true;
  return true;
}

static void srvDetach() {
  if (srvUrlV.length() > 0) srvFarewell(srvUrlV, true);
  preferences.remove("srvUrl");
  preferences.remove("srvParts");
  preferences.remove("srvQ");
  srvUrlV = "";
  srvPartsV = 0;
  srvQInit(srvQ, esp_random());
  srvQ.magic = 0;
  srvFailN = 0;
  srvLastOkMs = 0;
  srvLastTryMs = 0;
  srvNote = 0;
  srvWantData = false;
  srvDataFailN = 0;
  srvYsOkMs = 0;
  srvYsVer = "";
  srvYsGroup = "";
  bkSentHash = 0;
  logEvent(LC_SYS, LS_INFO, "Сервер відв'язано власником");
}

static bool srvNonceFresh(const String& got) {
  const bool ok = srvNonce.length() > 0 && got == srvNonce && millis() - srvNonceMs <= 600000UL;
  return ok;
}

bool srvMenuCallback(const String& data, int messageId) {
  if (!data.startsWith("x:s")) return false;
  if (data.startsWith("x:sm:")) {
    if (!srvNonceFresh(data.substring(5)) || srvUrlV.length() == 0) {
      tgEditText(chatId, messageId, "⌛ Підтвердження застаріло. Відкрийте ⚙️ → 🖥 Сервер → 🧩 Що на сервері.", "", tgInlineKb("[]"));
      return true;
    }
    srvNonce = "";
    const bool ok = srvSetParts(SRV_P_MIRROR);
    tgEditText(chatId, messageId, ok ? "✅ <b>Увімкнено все:</b> історія, сайт і стеження за зв'язком. Бот лишається на пристрої.\nСторінка: " + htmlEsc(srvUrlV) + "/"
                                     : String("❌ Не вдалося зберегти. Спробуйте ще раз."), "HTML", tgInlineKb("[]"));
    return true;
  }
  if (data == "x:sc") {
    if (srvUrlV.length() == 0) {
      tgEditText(chatId, messageId, "Сервер не підключено.", "", tgInlineKb("[]"));
      return true;
    }
    tgEditText(chatId, messageId, "🧩 Виберіть, що буде на сервері, - кнопки внизу.", "", tgInlineKb("[]"));
    srvMenuParts();
    return true;
  }
  if (data == "x:sl") {
    tgEditText(chatId, messageId, "Сервер збережено, поки на ньому нічого. Увімкнути: ⚙️ → 🖥 Сервер → 🧩 Що на сервері.", "", tgInlineKb("[]"));
    return true;
  }
  if (data.startsWith("x:sp:") || data.startsWith("x:sa:") || data == "x:sx") {
    if (srvUrlV.length() == 0) {
      tgEditText(chatId, messageId, "Сервер не підключено.", "", tgInlineKb("[]"));
      return true;
    }
    uint8_t want = srvPartsV;
    if (data.length() == 6 && data.startsWith("x:sp:")) {
      const char c = data[5];
      if (c == 'h') want = srvPartsFix(want, SRV_P_HIST, !(want & SRV_P_HIST));
      else if (c == 'w') want = srvPartsFix(want, SRV_P_WEB, !(want & SRV_P_WEB));
      else if (c == 'a') want = srvPartsFix(want, SRV_P_ALIVE, !(want & SRV_P_ALIVE));
      else if (c == 'y') want = srvPartsFix(want, SRV_P_YASNO, !(want & SRV_P_YASNO));
      else if (c == 'i') want = srvPartsFix(want, SRV_P_IMG, !(want & SRV_P_IMG));
      else if (c == '0') want = 0;
      else if (c == 'm') want = SRV_P_MIRROR;
    }
    const bool saved = want == srvPartsV || srvSetParts(want);
    tgEditText(chatId, messageId, (saved ? String("") : String("❌ Не вдалося зберегти, залишено попереднє значення.\n\n")) + srvPartsText(srvPartsV),
               "HTML", tgInlineKb(srvPartsKb(srvPartsV)));
    return true;
  }
  if (data == "x:sh:O" || data == "x:sh:F") {
    const bool on = data[5] == 'O';
    tgEditText(chatId, messageId, srvHbPickText(on), "HTML", tgInlineKb(srvHbPickKb(on)));
    return true;
  }
  if (data == "x:sh:B") {
    tgEditText(chatId, messageId, srvHbText(), "HTML", tgInlineKb(srvHbKb()));
    return true;
  }
  if (data.startsWith("x:sh:") && data.length() > 7) {
    const bool on = data[5] == 'o';
    const long v = data.substring(7).toInt();
    bool saved = true;
    if (v > 0 && v <= 0xFFFF && srvHbAllowed(on, (uint16_t)v)) {
      saved = prefPutIntVerified(on ? "srvHbOn" : "srvHbOff", (int32_t)v);
      if (saved) {
        if (on) srvHbOnV = (uint16_t)v;
        else srvHbOffV = (uint16_t)v;
        if (srvActive()) srvLastTryMs = 0;
      }
    }
    tgEditText(chatId, messageId, (saved ? String("") : String("❌ Не вдалося зберегти.\n\n")) + srvHbText(), "HTML", tgInlineKb(srvHbKb()));
    return true;
  }
  if (data.startsWith("x:sdY:")) {
    if (!srvNonceFresh(data.substring(6))) {
      tgEditText(chatId, messageId, "⌛ Підтвердження застаріло. Натисніть «🗑 Відв'язати» ще раз.", "", tgInlineKb("[]"));
      return true;
    }
    srvNonce = "";
    srvDetach();
    tgEditText(chatId, messageId, "✅ Сервер відв'язано. Пристрій працює сам, як завжди.", "", tgInlineKb("[]"));
    if (menuState == "srv") menuOpen("srv", srvMenuText());
    return true;
  }
  if (data.startsWith("x:bkY:")) {
    const bool fresh = bkPending != nullptr && bkNonce.length() > 0 && data.substring(6) == bkNonce && millis() - bkNonceMs <= 600000UL;
    if (!fresh) {
      bkDropPending();
      tgEditText(chatId, messageId, "⌛ Підтвердження застаріло. Відкрийте «💾 Копія налаштувань → ♻️ Відновити» ще раз.", "", tgInlineKb("[]"));
      return true;
    }
    const bool ok = bkApply(*bkPending);
    bkDropPending();
    logEvent(LC_SYS, ok ? LS_INFO : LS_WARN, ok ? "Налаштування відновлено з сервера" : "Відновлення: не все записано");
    tgEditText(chatId, messageId, ok ? String("✅ Налаштування відновлено. Перезапуск…") : String("⚠️ Частину налаштувань не вдалося записати. Перезапуск…"), "", tgInlineKb("[]"));
    prepareForRestart("відновлення налаштувань з сервера");
    delay(1000);
    ESP.restart();
    return true;
  }
  if (data == "x:bkN") {
    bkDropPending();
    tgEditText(chatId, messageId, "Відновлення скасовано, нічого не змінено.", "", tgInlineKb("[]"));
    return true;
  }
  if (data == "x:sdN") {
    srvNonce = "";
    tgEditText(chatId, messageId, "Відв'язку скасовано.", "", tgInlineKb("[]"));
    return true;
  }
  return false;
}

String srvConfirmRun(const String& action, const String& arg, bool yes) {
  if (action == "bkdel" || action == "bkall") {
    if (!yes) return "";
    if (srvSafeBlocked()) return "";
    bkWait("⏳ Видаляю…");
    bkActId = (uint32_t)arg.toInt();
    bkAct = action == "bkdel" ? BK_ACT_DEL : BK_ACT_DELALL;
    return "";
  }
  if (action == "bk") {
    if (!yes) {
      bkDropPending();
      return "";
    }
    if (bkPending == nullptr) return "⌛ Копію вже скинуто. Відкрийте «💾 Копія налаштувань → ♻️ Відновити» ще раз.";
    const bool ok = bkApply(*bkPending);
    bkDropPending();
    logEvent(LC_SYS, ok ? LS_INFO : LS_WARN, ok ? "Налаштування відновлено з сервера" : "Відновлення: не все записано");
    menuState = "main";
    tgSendText(chatId, ok ? String("✅ Налаштування відновлено. Перезапуск…") : String("⚠️ Частину налаштувань не вдалося записати. Перезапуск…"), "",
               tgReplyKbHint(menuKeyboard("main"), menuHint("main")));
    prepareForRestart("відновлення налаштувань з сервера");
    delay(1000);
    ESP.restart();
    return "";
  }
  if (action == "sd") {
    if (!yes) return "";
    srvDetach();
    return "✅ Сервер відв'язано. Пристрій працює сам, як завжди.";
  }
  return "";
}
