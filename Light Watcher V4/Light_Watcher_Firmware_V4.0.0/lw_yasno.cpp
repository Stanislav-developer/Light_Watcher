#include "lw.h"

static uint8_t yasnoTaskKind = 0;
static String yasnoTaskQuery = "";
static String yasnoTaskStreet = "";
static String yasnoTaskHouse = "";
static String yasnoTaskGroup = "";
static int32_t yasnoTaskStreetId = -1;
static bool yasnoTaskOk = false;
static int32_t yasnoTaskResultId = -1;
static String yasnoTaskResultName = "";
static String yasnoTaskResultGroup = "";
static String yasnoTaskError = "";
static uint32_t yasnoScheduleLastAttemptMs = 0;
static uint8_t yasnoScheduleFails = 0;
static uint32_t yasnoWarnSent[MAX_CHATS + 1] = {0};
static String yasnoTaskTodaySlotsJson = "[]";
static String yasnoTaskTomorrowSlotsJson = "[]";
static String yasnoTaskTodayDate = "";
static String yasnoTaskTomorrowDate = "";
static String yasnoTaskTodayStatus = "";
static String yasnoTaskTomorrowStatus = "";
static String yasnoTaskUpdatedOn = "";
static uint8_t yasnoPubFails[MAX_CHATS + 1];

static const char* YASNO_PLANNED_URL = "https://app.yasno.ua/api/blackout-service/public/shutdowns/regions/25/dsos/902/planned-outages";
static const uint32_t YASNO_HTTP_READ_MS = 5000UL;
static const uint32_t YASNO_HTTP_CONNECT_MS = 4000UL;
static const int YASNO_KYIV_DSO_ID = 902;
static const int YASNO_KYIV_REGION_ID = 25;

static const char* YASNO_API_BASE = "https://app.yasno.ua/api/blackout-service/public/shutdowns/addresses/v2/";

static const char YASNO_CA_BUNDLE[] PROGMEM = R"LWYASNO(
-----BEGIN CERTIFICATE-----
MIIFWjCCA0KgAwIBAgISEdK7udcjGJ5AXwqdLdDfJWfRMA0GCSqGSIb3DQEBDAUA
MEYxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9iYWxTaWduIG52LXNhMRwwGgYD
VQQDExNHbG9iYWxTaWduIFJvb3QgUjQ2MB4XDTE5MDMyMDAwMDAwMFoXDTQ2MDMy
MDAwMDAwMFowRjELMAkGA1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYt
c2ExHDAaBgNVBAMTE0dsb2JhbFNpZ24gUm9vdCBSNDYwggIiMA0GCSqGSIb3DQEB
AQUAA4ICDwAwggIKAoICAQCsrHQy6LNl5brtQyYdpokNRbopiLKkHWPd08EsCVeJ
OaFV6Wc0dwxu5FUdUiXSE2te4R2pt32JMl8Nnp8semNgQB+msLZ4j5lUlghYruQG
vGIFAha/r6gjA7aUD7xubMLL1aa7DOn2wQL7Id5m3RerdELv8HQvJfTqa1VbkNud
316HCkD7rRlr+/fKYIje2sGP1q7Vf9Q8g+7XFkyDRTNrJ9CG0Bwta/OrffGFqfUo
0q3v84RLHIf8E6M6cqJaESvWJ3En7YEtbWaBkoe0G1h6zD8K+kZPTXhc+CtI4wSE
y132tGqzZfxCnlEmIyDLPRT5ge1lFgBPGmSXZgjPjHvjK8Cd+RTyG/FWaha/LIWF
zXg4mutCagI0GIMXTpRW+LaCtfOW3T3zvn8gdz57GSNrLNRyc0NXfeD412lPFzYE
+cCQYDdF3uYM2HSNrpyibXRdQr4G9dlkbgIQrImwTDsHTUB+JMWKmIJ5jqSngiCN
I/onccnfxkF0oE32kRbcRoxfKWMxWXEM2G/CtjJ9++ZdU6Z+Ffy7dXxd7Pj2Fxzs
x2sZy/N78CsHpdlseVR2bJ0cpm4O6XkMqCNqo98bMDGfsVR7/mrLZqrcZdCinkqa
ByFrgY/bxFn63iLABJzjqls2k+g9vXqhnQt2sQvHnf3PmKgGwvgqo6GDoLclcqUC
4wIDAQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNV
HQ4EFgQUA1yrc4GHqMywptWU4jaWSf8FmSwwDQYJKoZIhvcNAQEMBQADggIBAHx4
7PYCLLtbfpIrXTncvtgdokIzTfnvpCo7RGkerNlFo048p9gkUbJUHJNOxO97k4Vg
JuoJSOD1u8fpaNK7ajFxzHmuEajwmf3lH7wvqMxX63bEIaZHU1VNaL8FpO7XJqti
2kM3S+LGteWygxk6x9PbTZ4IevPuzz5i+6zoYMzRx6Fcg0XERczzF2sUyQQCPtIk
pnnpHs6i58FZFZ8d4kuaPp92CC1r2LpXFNqD6v6MVenQTqnMdzGxRBF6XLE+0xRF
FRhiJBPSy03OXIPBNvIQtQ6IbbjhVp+J3pZmOUdkLG5NrmJ7v2B0GbhWrJKsFjLt
rWhV/pi60zTe9Mlhww6G9kuEYO4Ne7UyWHmRVSyBQ7N0H3qqJZ4d16GLuc1CLgSk
ZoNNiTW2bKg2SnkheCLQQrzRQDGQob4Ez8pn7fXwgNNgyYMqIgXQBztSvwyeqiv5
u+YfjyW6hY0XHgL+XVAEV8/+LbzvXMAaq7afJMbfc2hIkCwU9D9SGuTSyxTDYWnP
4vkYxboznxSjBF25cfe1lNj2M8FawTSLfJvdkzrnE6JwYZ+vj+vYxXX4M2bUdGc6
N3ec592kD3ZDZopD8p/7DEJ4Y9HiD2971KE9dJeFt0g5QdYg/NA6s/rob8SKunE3
vouXsXgxT7PntgMTzlSdriVZzH81Xwj3QEUxeCp6
-----END CERTIFICATE-----
)LWYASNO";

String yasnoUrlEncode(const String& input) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(input.length() * 3);
  for (unsigned int i = 0; i < input.length(); i++) {
    uint8_t c = (uint8_t)input[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String yasnoComparableName(String value) {
  value.trim();
  value.toLowerCase();

  const char* upper[] = {"А","Б","В","Г","Ґ","Д","Е","Є","Ж","З","И","І","Ї","Й","К","Л","М","Н","О","П","Р","С","Т","У","Ф","Х","Ц","Ч","Ш","Щ","Ь","Ю","Я","Ё","Ы","Э"};
  const char* lower[] = {"а","б","в","г","ґ","д","е","є","ж","з","и","і","ї","й","к","л","м","н","о","п","р","с","т","у","ф","х","ц","ч","ш","щ","ь","ю","я","ё","ы","э"};
  for (size_t i = 0; i < sizeof(upper) / sizeof(upper[0]); i++) value.replace(upper[i], lower[i]);
  const char* prefixes[] = {"вул. ", "вулиця ", "просп. ", "проспект ", "пров. ", "провулок ", "б-р ", "бульвар "};
  for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
    String p = prefixes[i];
    if (value.startsWith(p)) {
      value = value.substring(p.length());
      value.trim();
      break;
    }
  }
  return value;
}

bool yasnoHttpGetJson(const String& url, JsonDocument& doc, String& error, JsonDocument* filter) {
  error = "";
  if (doc.overflowed()) {
    error = "YASNO: JSON-документ перебуває у стані переповнення до HTTPS-запиту.";
    return false;
  }
  if (filter && filter->overflowed()) {
    error = "YASNO: JSON-фільтр не отримав потрібну пам'ять.";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "ESP32 не підключений до Wi-Fi.";
    return false;
  }
  if (!timeTrusted()) {
    error = "Немає підтвердженої синхронізації часу NTP. Спробуйте пізніше.";
    return false;
  }

  uint32_t heapBeforeTls = ESP.getFreeHeap();
  uint32_t maxBlockBeforeTls = ESP.getMaxAllocHeap();
  if (heapBeforeTls < 32000UL || maxBlockBeforeTls < 12000UL) {
    error = "YASNO: мало RAM до TLS: " + String(heapBeforeTls) +
            " Б, блок " + String(maxBlockBeforeTls) + " Б";
    return false;
  }

  WiFiClientSecure secure;
  secure.setCACert(YASNO_CA_BUNDLE);
  secure.setHandshakeTimeout(8);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  secure.setTimeout(YASNO_HTTP_READ_MS);
#else
  secure.setTimeout((YASNO_HTTP_READ_MS + 999UL) / 1000UL);
#endif

  HTTPClient http;
  http.setConnectTimeout(YASNO_HTTP_CONNECT_MS);
  http.setTimeout(YASNO_HTTP_READ_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(secure, url)) {
    error = "Не вдалося створити HTTPS-запит до YASNO.";
    return false;
  }

  const char* responseHeaders[] = {"Content-Type", "Content-Length", "Transfer-Encoding"};
  http.collectHeaders(responseHeaders, sizeof(responseHeaders) / sizeof(responseHeaders[0]));
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Cache-Control", "no-cache");
  http.addHeader("User-Agent", "Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36 Chrome/120.0.0.0 Mobile Safari/537.36");
  http.addHeader("Origin", "https://app.yasno.ua");
  http.addHeader("Referer", "https://app.yasno.ua/");
  int code = http.GET();

  bool certOk = false;
  const mbedtls_x509_crt* peer = secure.getPeerCertificate();
  uint32_t now = statsNowEpoch();
  uint32_t certFrom = 0, certTo = 0;
  if (peer != NULL) {
    certFrom = webX509Epoch(peer->valid_from);
    certTo = webX509Epoch(peer->valid_to);
    certOk = timeTrusted() && certFrom != 0 && certTo != 0 &&
             now >= certFrom && now <= certTo;
  }
  if (code < 0 && peer == NULL) {
    http.end();
    error = "YASNO недоступний: " + HTTPClient::errorToString(code) + ".";
    return false;
  }
  if (!certOk) {
    char tlsErrText[96] = {0};
    int tlsErr = secure.lastError(tlsErrText, sizeof(tlsErrText));
    http.end();
    error = "TLS YASNO: HTTP=" + String(code) +
            "; peer=" + String(peer != NULL ? "yes" : "no") +
            "; NTP=" + String(timeTrusted() ? "ok" : "no") +
            "; mbedTLS=" + String(tlsErr) +
            "; detail=" + String(tlsErrText) +
            "; RAM=" + String(ESP.getFreeHeap()) +
            "; maxBlock=" + String(ESP.getMaxAllocHeap()) + ".";
    return false;
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    error = "YASNO повернув HTTP " + String(code) +
            "; RAM=" + String(ESP.getFreeHeap()) +
            "; maxBlock=" + String(ESP.getMaxAllocHeap()) + ".";
    return false;
  }

  int32_t expectedBodySize = http.getSize();
  String transferEncoding = http.header("Transfer-Encoding");
  String contentType = http.header("Content-Type");
  String transferLower = transferEncoding;
  transferLower.toLowerCase();
  bool chunked = transferLower.indexOf("chunked") >= 0;

  if (expectedBodySize > 65536) {
    http.end();
    error = "YASNO: HTTP body перевищує обмеження 64 КБ (" +
            String(expectedBodySize) + " Б).";
    return false;
  }
  if (expectedBodySize < 0 && !chunked) {
    http.end();
    error = "YASNO: сервер не передав Content-Length або chunked Transfer-Encoding; тіло відхилено.";
    return false;
  }
  String contentTypeLower = contentType;
  contentTypeLower.toLowerCase();
  if (contentType.length() && contentTypeLower.indexOf("application/json") < 0) {
    http.end();
    error = "YASNO: неочікуваний Content-Type: " + contentType;
    return false;
  }

  uint32_t heapAfterTls = ESP.getFreeHeap();
  uint32_t maxBlockAfterTls = ESP.getMaxAllocHeap();
  if (heapAfterTls < 10000UL || maxBlockAfterTls < 4096UL) {
    http.end();
    error = "YASNO: мало RAM після TLS: " + String(heapAfterTls) +
            " Б, блок " + String(maxBlockAfterTls) + " Б";
    return false;
  }

  doc.clear();
  YasnoHttpBodyStream body(http.getStream(), expectedBodySize, chunked, YASNO_HTTP_READ_MS);
  body.setTimeout(0);
  DeserializationError jsonErr = filter
      ? deserializeJson(doc, body, DeserializationOption::Filter(*filter),
                        DeserializationOption::NestingLimit(8))
      : deserializeJson(doc, body, DeserializationOption::NestingLimit(8));

  size_t parserBytes = body.decodedBytes();
  bool bodyValid = body.drainAndValidate();
  http.end();

  if (!bodyValid) {
    String expectedInfo = expectedBodySize >= 0 ? (String(" із ") + String(expectedBodySize)) : String("");
    error = "YASNO: HTTP body неповне/пошкоджене; отримано " +
            String((unsigned long)body.decodedBytes()) + expectedInfo +
            " Б; chunked=" + String(chunked ? "так" : "ні") +
            "; parser прочитав " + String((unsigned long)parserBytes) +
            " Б; timeout/decoder=" + String(body.failed() ? "помилка" : "ні") + ".";
    return false;
  }
  if (body.trailingGarbage()) {
    error = "YASNO: після JSON-документа знайдено зайві непорожні байти.";
    return false;
  }
  if (jsonErr) {
    error = "JSON YASNO: " + String(jsonErr.c_str()) +
            "; body=" + String((unsigned long)body.decodedBytes()) +
            " Б; RAM=" + String(ESP.getFreeHeap()) +
            "; maxBlock=" + String(ESP.getMaxAllocHeap()) + " Б.";
    return false;
  }
  if (doc.overflowed()) {
    error = "YASNO: JSON-документ переповнений.";
    return false;
  }
  return true;
}

bool yasnoFindAddressItem(const String& path, const String& query, int32_t streetId,
                          int32_t& foundId, String& foundName, String& error) {
  String url = String(YASNO_API_BASE) + path + "?regionId=" + String(YASNO_KYIV_REGION_ID) +
               "&dsoId=" + String(YASNO_KYIV_DSO_ID);
  if (streetId >= 0) url += "&streetId=" + String(streetId);
  url += "&query=" + yasnoUrlEncode(query);

  JsonDocument doc;
  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["value"] = true;
  if (!yasnoHttpGetJson(url, doc, error, &filter)) return false;
  if (!doc.is<JsonArray>()) {
    error = "Формат відповіді пошуку YASNO змінився (очікувався список).";
    return false;
  }
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) {
    error = "YASNO не знайшов таку адресу. Перевірте написання.";
    return false;
  }

  String wanted = yasnoComparableName(query);
  int exactCount = 0;
  int32_t exactId = -1;
  String exactName;
  int validCount = 0;
  int32_t onlyId = -1;
  String onlyName;
  for (JsonVariant item : arr) {
    if (!item.is<JsonObject>() || !item["id"].is<int>() || !item["value"].is<const char*>()) continue;
    int32_t id = item["id"].as<int32_t>();
    String value = item["value"].as<String>();
    value.trim();
    if (value.length() == 0 || id <= 0) continue;
    validCount++;
    onlyId = id;
    onlyName = value;
    if (yasnoComparableName(value) == wanted) {
      exactCount++;
      exactId = id;
      exactName = value;
    }
  }
  if (exactCount == 1) {
    foundId = exactId;
    foundName = exactName;
    return true;
  }
  if (exactCount > 1) {
    error = "YASNO повернув кілька однакових назв. Автоматичний вибір небезпечний; уточніть адресу вручну.";
    return false;
  }

  if (validCount == 1 && path != "houses") {
    foundId = onlyId;
    foundName = onlyName;
    return true;
  }
  if (validCount == 1) {
    error = "Точного збігу немає. Знайдено лише «" + onlyName + "» - якщо це ваш будинок, введіть саме так.";
    return false;
  }
  error = "Знайдено кілька схожих варіантів. Введіть повну назву так, як її показує YASNO.";
  return false;
}

bool yasnoGetGroup(int32_t streetId, int32_t houseId, String& group, String& error) {
  String url = String(YASNO_API_BASE) + "group?regionId=" + String(YASNO_KYIV_REGION_ID) +
               "&dsoId=" + String(YASNO_KYIV_DSO_ID) + "&streetId=" + String(streetId) +
               "&houseId=" + String(houseId);
  JsonDocument doc;
  JsonDocument filter;
  filter["group"] = true;
  filter["subgroup"] = true;
  if (!yasnoHttpGetJson(url, doc, error, &filter)) return false;
  if (!doc.is<JsonObject>() || !doc["group"].is<int>() || !doc["subgroup"].is<int>()) {
    error = "YASNO не повернув однозначну групу для цього будинку.";
    return false;
  }
  int g = doc["group"].as<int>();
  int sg = doc["subgroup"].as<int>();
  if (g < 1 || g > 99 || sg < 1 || sg > 99) {
    error = "YASNO повернув некоректний номер групи.";
    return false;
  }
  group = String(g) + "." + String(sg);
  return true;
}

bool yasnoSaveAddress(const String& city, const String& street, const String& house, const String& group) {
  String oldCity = preferences.getString("ysCity", "");
  String oldStreet = preferences.getString("ysStreet", "");
  String oldHouse = preferences.getString("ysHouse", "");
  String oldGroup = preferences.getString("ysGroup", "");
  bool changed = oldCity != city || oldStreet != street || oldHouse != house || oldGroup != group;
  bool ok = prefPutStrVerified("ysCity", city);
  ok = ok && prefPutStrVerified("ysStreet", street);
  ok = ok && prefPutStrVerified("ysHouse", house);
  ok = ok && prefPutStrVerified("ysGroup", group);
  if (!ok) {
    preferences.putString("ysCity", oldCity);
    preferences.putString("ysStreet", oldStreet);
    preferences.putString("ysHouse", oldHouse);
    preferences.putString("ysGroup", oldGroup);
    return false;
  }
  yasnoCity = city;
  yasnoStreet = street;
  yasnoHouse = house;
  yasnoGroup = group;
  if (changed) yasnoClearScheduleCache();
  return true;
}

String yasnoMenuText() {
  String t = "";
  if (yasnoGroup.length() == 0) {
    t += "Адресу ще не налаштовано.\n";
  } else {
    t += "Місто: " + htmlEsc(yasnoCity) + "\nВулиця: " + htmlEsc(yasnoStreet) +
         "\nБудинок: " + htmlEsc(yasnoHouse) + "\nЧерга: <b>" + htmlEsc(yasnoGroup) + "</b>\n";
  }
  if (yasnoScheduleValid) {
    t += "\nГрафік: завантажено";
    if (yasnoScheduleFetchedAt) t += " (" + formatEpochLocal((time_t)yasnoScheduleFetchedAt, "%d.%m %H:%M") + ")";
  }
  else if (yasnoGroup.length()) t += "\nГрафік: очікує першого завантаження.";
  t += "\nГрафік оновлюється автоматично кожні 15 хв, увечері до появи графіка на завтра - кожні 5 хв. Коли світла немає - раз на 30 хв.";
  return t;
}

String yasnoSlotsCompact(JsonArrayConst slots, bool& ok) {
  String out;
  out.reserve((slots.size() * 42U) + 2U);
  out = "[";
  bool first = true;
  ok = slots.size() == 0;
  for (JsonVariantConst value : slots) {
    if (!value.is<JsonObjectConst>()) {
      ok = false;
      return "[]";
    }
    JsonObjectConst slot = value.as<JsonObjectConst>();
    const char* type = slot["type"] | "";
    if (strcmp(type, "NotPlanned") == 0 || strcmp(type, "NotPlaned") == 0) {
      ok = true;
      continue;
    }
    if (strcmp(type, "Definite") != 0 && strcmp(type, "Possible") != 0) {
      ok = false;
      return "[]";
    }
    int start = slot["start"] | -1;
    int end = slot["end"] | -1;
    if (start < 0 || end <= start || end > 1440) {
      ok = false;
      return "[]";
    }
    ok = true;
    if (!first) out += ",";
    out += "{\"s\":" + String(start) + ",\"e\":" + String(end) + ",\"t\":\"" + String(type) + "\"}";
    first = false;
  }
  out += "]";
  return out;
}

bool yasnoFetchPlannedSchedule(const String& group, String& todayJson, String& tomorrowJson,
                               String& todayDate, String& tomorrowDate, String& todayStatus,
                               String& tomorrowStatus, String& updatedOn, String& error) {
  JsonDocument filter;
  filter[group.c_str()]["today"]["date"] = true;
  filter[group.c_str()]["today"]["status"] = true;
  filter[group.c_str()]["today"]["slots"][0]["type"] = true;
  filter[group.c_str()]["today"]["slots"][0]["start"] = true;
  filter[group.c_str()]["today"]["slots"][0]["end"] = true;
  filter[group.c_str()]["tomorrow"]["date"] = true;
  filter[group.c_str()]["tomorrow"]["status"] = true;
  filter[group.c_str()]["tomorrow"]["slots"][0]["type"] = true;
  filter[group.c_str()]["tomorrow"]["slots"][0]["start"] = true;
  filter[group.c_str()]["tomorrow"]["slots"][0]["end"] = true;
  filter[group.c_str()]["updatedOn"] = true;
  if (filter.overflowed()) {
    error = "Не вистачило пам'яті для фільтра відповіді YASNO.";
    return false;
  }

  JsonDocument doc;
  if (!yasnoHttpGetJson(String(YASNO_PLANNED_URL), doc, error, &filter)) return false;
  if (doc.overflowed()) {
    error = "Не вистачило пам'яті для відповіді YASNO.";
    return false;
  }
  JsonVariantConst g = doc[group.c_str()];
  if (g.isNull()) {
    error = "YASNO не повернув дані для черги " + group + ". Перевірте, чи формат API не змінився.";
    return false;
  }
  JsonObjectConst td = g["today"].as<JsonObjectConst>();
  JsonObjectConst tm = g["tomorrow"].as<JsonObjectConst>();
  if (td.isNull() || !td["slots"].is<JsonArrayConst>()) {
    error = "YASNO не повернув коректний графік на сьогодні.";
    return false;
  }
  todayDate = td["date"] | "";
  todayStatus = td["status"] | "";
  if (!todayDate.length()) {
    error = "У відповіді YASNO відсутня дата сьогоднішнього графіка.";
    return false;
  }
  bool a = false, b = false;
  todayJson = yasnoSlotsCompact(td["slots"].as<JsonArrayConst>(), a);
  if (!tm.isNull() && tm["slots"].is<JsonArrayConst>()) {
    tomorrowDate = tm["date"] | "";
    tomorrowStatus = tm["status"] | "";
    if (tomorrowDate.length()) tomorrowJson = yasnoSlotsCompact(tm["slots"].as<JsonArrayConst>(), b);
  }
  if (!b) {
    tomorrowJson = "[]";
    tomorrowDate = "";
    tomorrowStatus = "Ще не опубліковано";
    b = true;
  }
  if (!a || !b) {
    error = "YASNO повернув невідомий тип або некоректний часовий інтервал. Кеш попереднього графіка збережено.";
    return false;
  }
  updatedOn = g["updatedOn"] | "";
  return true;
}

void yasnoClearScheduleCache() {
  yasnoTodaySlotsJson = "[]";
  yasnoTomorrowSlotsJson = "[]";
  yasnoTodayDate = yasnoTomorrowDate = "";
  yasnoTodayStatus = yasnoTomorrowStatus = "";
  yasnoScheduleUpdatedOn = "";
  yasnoScheduleFetchedAt = 0;
  yasnoScheduleLastAttemptMs = 0;
  yasnoScheduleFails = 0;
  yasnoTaskErrorCopy = "";
  yasnoScheduleValid = false;
  preferences.remove("ysToday"); preferences.remove("ysTom");
  preferences.remove("ysDateT"); preferences.remove("ysDateM");
  preferences.remove("ysStatT"); preferences.remove("ysStatM");
  preferences.remove("ysUpd"); preferences.remove("ysFetch");
}

static void yasnoWorkerRun() {
  uint8_t kind = yasnoTaskKind;
  String query = yasnoTaskQuery;
  String street = yasnoTaskStreet;
  String house = yasnoTaskHouse;
  String groupForTask = yasnoTaskGroup;
  int32_t streetId = yasnoTaskStreetId;
  String error, foundName, group;
  int32_t foundId = -1;
  bool ok = false;

  if (kind == 1) {
    ok = yasnoFindAddressItem("streets", query, -1, foundId, foundName, error);
  } else if (kind == 2) {
    ok = yasnoFindAddressItem("houses", query, streetId, foundId, foundName, error);
    if (ok) ok = yasnoGetGroup(streetId, foundId, group, error);
  } else if (kind == 3) {
    int32_t storedStreetId = -1, storedHouseId = -1;
    ok = yasnoFindAddressItem("streets", street, -1, storedStreetId, foundName, error);
    if (ok) {
      String canonicalHouse;
      ok = yasnoFindAddressItem("houses", house, storedStreetId, storedHouseId, canonicalHouse, error);
      if (ok) {
        foundName = canonicalHouse;
        ok = yasnoGetGroup(storedStreetId, storedHouseId, group, error);
      }
    }
  } else if (kind == 4) {
    ok = yasnoFetchPlannedSchedule(groupForTask, yasnoTaskTodaySlotsJson, yasnoTaskTomorrowSlotsJson,
                                   yasnoTaskTodayDate, yasnoTaskTomorrowDate, yasnoTaskTodayStatus,
                                   yasnoTaskTomorrowStatus, yasnoTaskUpdatedOn, error);
  } else {
    error = "Невідомий тип завдання YASNO.";
  }

  yasnoTaskOk = ok;
  yasnoTaskResultId = foundId;
  yasnoTaskResultName = foundName;
  yasnoTaskResultGroup = group;
  yasnoTaskError = error;
}

void yasnoWorker(void* parameter) {
  (void)parameter;
  yasnoWorkerRun();
  __sync_synchronize();
  UBaseType_t yasnoStackHighWater = uxTaskGetStackHighWaterMark(NULL);
  Serial.printf("[YASNO] worker завершено; stack high-water=%u; heap=%u; max-block=%u\n",
                (unsigned)yasnoStackHighWater,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  __sync_synchronize();
  yasnoTaskDone = true;
  yasnoTaskRunning = false;
  vTaskDelete(NULL);
}

bool yasnoStartTask(uint8_t kind, const String& query, String& error) {
  error = "";
  if (yasnoTaskRunning || yasnoTaskDone) {
    error = "Попередній запит YASNO ще виконується. Зачекайте.";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "ESP32 не підключений до Wi-Fi.";
    return false;
  }
  if (client.connected()) client.stop();
  uint32_t freeBeforeTask = ESP.getFreeHeap();
  uint32_t maxBeforeTask = ESP.getMaxAllocHeap();
  if (freeBeforeTask < 52000UL || maxBeforeTask < 20000UL) {
    error = "YASNO: мало RAM для запуску: " +
            String(freeBeforeTask) + " Б, блок " +
            String(maxBeforeTask) + " Б";
    return false;
  }
  yasnoTaskKind = kind;
  yasnoTaskQuery = query;
  yasnoTaskStreet = yasnoStreet;
  yasnoTaskHouse = yasnoHouse;
  yasnoTaskGroup = yasnoGroup;
  yasnoTaskStreetId = yasnoPendingStreetId;
  yasnoTaskOk = false;
  yasnoTaskResultId = -1;
  yasnoTaskResultName = "";
  yasnoTaskResultGroup = "";
  yasnoTaskError = "";
  yasnoTaskTodaySlotsJson = "[]"; yasnoTaskTomorrowSlotsJson = "[]";
  yasnoTaskTodayDate = yasnoTaskTomorrowDate = "";
  yasnoTaskTodayStatus = yasnoTaskTomorrowStatus = "";
  yasnoTaskUpdatedOn = "";
  yasnoTaskCancelled = false;
  yasnoTaskDone = false;
  yasnoTaskRunning = true;
  BaseType_t created = xTaskCreate(yasnoWorker, "yasno_lookup", 12288, NULL, 1, NULL);
  if (created != pdPASS) {
    yasnoTaskRunning = false;
    error = "Не вдалося запустити фонове завдання YASNO (недостатньо ресурсів).";
    return false;
  }
  return true;
}

void yasnoScheduleFailLog(const String& error) {
  bool repeat = yasnoTaskErrorCopy.length() > 0;
  yasnoTaskErrorCopy = error;
  if (yasnoScheduleFails < 255) yasnoScheduleFails++;
  String e = error.startsWith("YASNO: ") ? error.substring(7) : error;
  logEvent(LC_YASNO, repeat ? LS_INFO : LS_WARN, "YASNO графік: " + e);
}

static bool yasnoApplyFetched(const String& resultGroupKey, bool ok, const String& error) {
  if (resultGroupKey.length() == 0 || resultGroupKey != yasnoGroup) return false;
  if (!ok) {
    yasnoScheduleFailLog(error.length() ? error : String("Причину збою не повернуто."));
    return false;
  }

  String oldToday = preferences.getString("ysToday", "[]"), oldTom = preferences.getString("ysTom", "[]");
  String oldDateT = preferences.getString("ysDateT", ""), oldDateM = preferences.getString("ysDateM", "");
  String oldStatT = preferences.getString("ysStatT", ""), oldStatM = preferences.getString("ysStatM", "");
  String oldUpd = preferences.getString("ysUpd", ""); uint32_t oldFetch = preferences.getUInt("ysFetch", 0);

  const bool todayStatusHasNoUsablePlan =
    yasnoTaskTodayStatus == "EmergencyShutdowns" ||
    yasnoTaskTodayStatus == "WaitingForSchedule";
  const bool tomorrowStatusHasNoUsablePlan =
    yasnoTaskTomorrowStatus == "EmergencyShutdowns" ||
    yasnoTaskTomorrowStatus == "WaitingForSchedule";
  bool keepTodaySchedule = oldToday != "[]" &&
    yasnoTaskTodayDate.length() && yasnoTaskTodayDate == oldDateT &&
    (todayStatusHasNoUsablePlan ||
     (yasnoTaskTodaySlotsJson == "[]" && yasnoTaskUpdatedOn == oldUpd));
  bool keepTomorrowSchedule = oldTom != "[]" &&
    yasnoTaskTomorrowDate.length() && yasnoTaskTomorrowDate == oldDateM &&
    (tomorrowStatusHasNoUsablePlan ||
     (yasnoTaskTomorrowSlotsJson == "[]" && yasnoTaskUpdatedOn == oldUpd));
  if (keepTodaySchedule) {
    yasnoTaskTodaySlotsJson = oldToday;
    yasnoTaskTodayDate = oldDateT;
  }
  if (keepTomorrowSchedule) {
    yasnoTaskTomorrowSlotsJson = oldTom;
    yasnoTaskTomorrowDate = oldDateM;
  }
  String scheduleUpdatedOn = (keepTodaySchedule || keepTomorrowSchedule) ? oldUpd : yasnoTaskUpdatedOn;
  const bool contentChanged = yasnoTaskTodaySlotsJson != oldToday || yasnoTaskTomorrowSlotsJson != oldTom ||
    yasnoTaskTodayDate != oldDateT || yasnoTaskTomorrowDate != oldDateM ||
    yasnoTaskTodayStatus != oldStatT || yasnoTaskTomorrowStatus != oldStatM || scheduleUpdatedOn != oldUpd ||
    !preferences.isKey("ysToday");
  uint32_t fetched = statsNowEpoch();
  bool saved = true;
  if (contentChanged) {
    saved = prefPutStrVerified("ysToday", yasnoTaskTodaySlotsJson);
    saved = saved && prefPutStrVerified("ysTom", yasnoTaskTomorrowSlotsJson);
    saved = saved && prefPutStrVerified("ysDateT", yasnoTaskTodayDate);
    saved = saved && prefPutStrVerified("ysDateM", yasnoTaskTomorrowDate);
    saved = saved && prefPutStrVerified("ysStatT", yasnoTaskTodayStatus);
    saved = saved && prefPutStrVerified("ysStatM", yasnoTaskTomorrowStatus);
    saved = saved && prefPutStrVerified("ysUpd", scheduleUpdatedOn);
  }
  if (saved && (contentChanged || fetched < oldFetch || fetched - oldFetch >= 1800UL)) {
    saved = preferences.putUInt("ysFetch", fetched) == sizeof(uint32_t);
  }
  if (!saved) {

    preferences.putString("ysToday", oldToday); preferences.putString("ysTom", oldTom);
    preferences.putString("ysDateT", oldDateT); preferences.putString("ysDateM", oldDateM);
    preferences.putString("ysStatT", oldStatT); preferences.putString("ysStatM", oldStatM);
    preferences.putString("ysUpd", oldUpd); preferences.putUInt("ysFetch", oldFetch);
    yasnoScheduleFailLog("Не вдалося зберегти новий графік. Виконано спробу відновити попередній кеш.");
    return false;
  }
  yasnoTodaySlotsJson = yasnoTaskTodaySlotsJson;
  yasnoTomorrowSlotsJson = yasnoTaskTomorrowSlotsJson;
  yasnoTodayDate = yasnoTaskTodayDate;
  yasnoTomorrowDate = yasnoTaskTomorrowDate;
  yasnoTodayStatus = yasnoTaskTodayStatus;
  yasnoTomorrowStatus = yasnoTaskTomorrowStatus;
  yasnoScheduleUpdatedOn = scheduleUpdatedOn;
  yasnoScheduleFetchedAt = fetched;
  yasnoTaskErrorCopy = "";
  yasnoScheduleFails = 0;
  yasnoScheduleValid = true;
  if (contentChanged) logEvent(LC_YASNO, LS_INFO, "YASNO: графік оновлено");
  return true;
}

bool yasnoApplyFromServer(const String& group, const String& todayJson, const String& tomorrowJson, const String& todayDate,
                          const String& tomorrowDate, const String& todayStatus, const String& tomorrowStatus, const String& updatedOn) {
  if (yasnoTaskRunning || yasnoTaskDone) return false;
  if (!todayJson.startsWith("[") || !tomorrowJson.startsWith("[") || todayDate.length() == 0) return false;
  yasnoTaskTodaySlotsJson = todayJson;
  yasnoTaskTomorrowSlotsJson = tomorrowJson;
  yasnoTaskTodayDate = todayDate;
  yasnoTaskTomorrowDate = tomorrowDate;
  yasnoTaskTodayStatus = todayStatus;
  yasnoTaskTomorrowStatus = tomorrowStatus;
  yasnoTaskUpdatedOn = updatedOn;
  return yasnoApplyFetched(group, true, "");
}

void yasnoTick() {
  if (!yasnoTaskDone) return;
  __sync_synchronize();
  uint8_t kind = yasnoTaskKind;
  bool ok = yasnoTaskOk;
  int32_t resultId = yasnoTaskResultId;
  String resultName = yasnoTaskResultName;
  String resultGroup = yasnoTaskResultGroup;
  String error = yasnoTaskError;
  String resultGroupKey = yasnoTaskGroup;
  const String taskStreet = yasnoTaskStreet;
  const String taskHouse = yasnoTaskHouse;
  bool cancelled = yasnoTaskCancelled;
  yasnoTaskDone = false;
  yasnoTaskKind = 0;
  yasnoTaskQuery = "";
  yasnoTaskStreet = "";
  yasnoTaskHouse = "";
  yasnoTaskGroup = "";
  yasnoTaskStreetId = -1;
  yasnoTaskError = "";
  if (cancelled) return;
  if ((kind == 1 || kind == 2) && menuInput != "yasno_wait") return;

  if (kind == 1) {
    if (!ok) {
      menuInputStart("yasno_street", "yasno", "❌ " + htmlEsc(error) + "\nВведіть повну назву вулиці ще раз або скасуйте.");
      return;
    }
    yasnoPendingStreetId = resultId;
    yasnoPendingStreet = resultName;
    menuInputStart("yasno_house", "yasno", "🏠 Вулицю знайдено: <b>" + htmlEsc(resultName) + "</b>\nВведіть номер будинку (наприклад, 10 або 10Б).");
    return;
  }

  if (kind == 2) {
    if (!ok) {
      menuInputStart("yasno_house", "yasno", "❌ " + htmlEsc(error) + "\nВведіть номер будинку ще раз або скасуйте.");
      return;
    }
    if (!yasnoSaveAddress(yasnoPendingCity, yasnoPendingStreet, resultName, resultGroup)) {
      menuInputReturn("❌ Чергу " + htmlEsc(resultGroup) + " знайдено, але запис у Preferences не підтверджено. Попередні налаштування залишено.");
      return;
    }
    logEvent(LC_YASNO, LS_INFO, "YASNO: адресу перевірено, чергу визначено");
    menuInputReturn("✅ <b>YASNO успішно визначив чергу!</b>\nМісто: " + htmlEsc(yasnoCity) +
                    "\nВулиця: " + htmlEsc(yasnoStreet) + "\nБудинок: " + htmlEsc(yasnoHouse) +
                    "\nЧерга: <b>" + htmlEsc(yasnoGroup) + "</b>\n\nАдресу та чергу збережено в ESP32.");
    return;
  }

  if (kind == 3) {
    const bool waiting = menuInput == "yasno_wait";
    if (waiting) {
      menuInput = "";
      menuInputBack = "";
    }
    if (!ok) {
      if (waiting) menuOpen("yasno", "❌ " + htmlEsc(error) + "\nЗбережена адреса та черга не змінені.\n\n" + yasnoMenuText());
      return;
    }
    if (yasnoStreet != taskStreet || yasnoHouse != taskHouse) {
      logEvent(LC_YASNO, LS_WARN, "YASNO: адресу змінено під час перевірки");
      if (waiting) menuOpen("yasno", "ℹ️ Адресу змінено під час перевірки - результат не застосовано.\n\n" + yasnoMenuText());
      return;
    }
    if (!yasnoSaveAddress(yasnoCity, yasnoStreet, resultName, resultGroup)) {
      if (waiting) menuOpen("yasno", "❌ Не вдалося підтвердити збереження в Preferences. Попередні налаштування відновлено.\n\n" + yasnoMenuText());
      return;
    }
    logEvent(LC_YASNO, LS_INFO, "YASNO: збережену адресу перевірено, черга " + resultGroup);
    if (waiting) menuOpen("yasno", "✅ Адресу перевірено. Черга: <b>" + htmlEsc(resultGroup) + "</b>\n\n" + yasnoMenuText());
    return;
  }

  if (kind == 4) yasnoApplyFetched(resultGroupKey, ok, error);
}

String yasnoMinuteText(int value) {
  if (value < 0) value = 0;
  if (value > 1440) value = 1440;
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", value / 60, value % 60);
  return String(b);
}

bool yasnoIsToday(const String& isoDate, uint32_t nowEpoch) {
  if (!nowEpoch || isoDate.length() < 10) return false;
  return isoDate.substring(0, 10) == formatEpochLocal((time_t)nowEpoch, "%Y-%m-%d");
}

String yasnoPowerEventText(bool powerOn) {
  if (!yasnoScheduleValid || yasnoTodayStatus == "EmergencyShutdowns" ||
      yasnoTodayStatus == "WaitingForSchedule") return "";

  uint32_t nowEpoch = statsNowEpoch();
  if (!nowEpoch || !yasnoScheduleFetchedAt || nowEpoch < yasnoScheduleFetchedAt ||
      nowEpoch - yasnoScheduleFetchedAt > 3UL * 3600UL) return "";

  if (!yasnoIsToday(yasnoTodayDate, nowEpoch)) return "";

  struct tm localNow;
  if (!getLocalTime(&localNow, 100)) return "";
  int minuteNow = localNow.tm_hour * 60 + localNow.tm_min;

  JsonDocument d;
  if (deserializeJson(d, yasnoTodaySlotsJson) || !d.is<JsonArray>()) return "";

  int nextStart = 1441;
  int nextEnd = -1;
  int activeEnd = -1;
  for (JsonObject slot : d.as<JsonArray>()) {
    int st = slot["s"] | -1;
    int en = slot["e"] | -1;
    if (st < 0 || en <= st || en > 1440) continue;

    if (!powerOn && st <= minuteNow && minuteNow < en) {
      if (en > activeEnd) activeEnd = en;
    }
    if (st > minuteNow && st < nextStart) {
      nextStart = st;
      nextEnd = en;
    }
  }

  if (powerOn) {
    if (nextEnd < 0) return "";
    return "📅 Наступне планове: <b>" + yasnoMinuteText(nextStart) + " - " + yasnoMinuteText(nextEnd) + "</b>";
  }
  if (activeEnd >= 0) {
    return "📅 Очікуємо за розкладом о <b>" + yasnoMinuteText(activeEnd) + "</b>";
  }
  return "";
}

String yasnoDateCaption(const String& isoDate) {
  if (isoDate.length() < 10) return "Сьогодні";
  int y = isoDate.substring(0, 4).toInt();
  int m = isoDate.substring(5, 7).toInt();
  int d = isoDate.substring(8, 10).toInt();
  static const char* months[] = {
    "", "січня", "лютого", "березня", "квітня", "травня", "червня",
    "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"
  };
  if (m < 1 || m > 12 || d < 1 || d > 31 || y < 2000 || y > 2099) return "дата невідома";
  return String(d) + " " + String(months[m]) + " " + String(y);
}

String yasnoUpdatedCaption(const String& isoTimestamp) {
  if (isoTimestamp.length() < 10) return "час невідомий";
  String out = yasnoDateCaption(isoTimestamp.substring(0, 10));
  if (isoTimestamp.length() >= 16 && isoTimestamp.charAt(10) == 'T') {
    out += " о " + isoTimestamp.substring(11, 16);
    if (isoTimestamp.endsWith("Z") || isoTimestamp.endsWith("+00:00")) {
      out += " UTC";
    } else {
      int zonePos = -1;
      for (int i = 16; i < (int)isoTimestamp.length(); i++) {
        if (isoTimestamp.charAt(i) == '+' || isoTimestamp.charAt(i) == '-') zonePos = i;
      }
      if (zonePos >= 0) out += " (UTC" + isoTimestamp.substring(zonePos) + ")";
    }
  }
  return out;
}

String yasnoDayCaption(const String& isoDate) {
  if (isoDate.length() < 10) return "дата невідома";
  const int y = isoDate.substring(0, 4).toInt();
  const int m = isoDate.substring(5, 7).toInt();
  const int d = isoDate.substring(8, 10).toInt();
  static const char* months[] = {
    "", "січня", "лютого", "березня", "квітня", "травня", "червня",
    "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"
  };
  static const char* days[] = { "нд", "пн", "вт", "ср", "чт", "пт", "сб" };
  if (m < 1 || m > 12 || d < 1 || d > 31 || y < 2000 || y > 2099) return "дата невідома";
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  const int yy = m < 3 ? y - 1 : y;
  const int wd = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7;
  return String(days[wd]) + ", " + String(d) + " " + String(months[m]);
}

String yasnoSlotLines(const String& slotsJson) {
  JsonDocument d;
  if (deserializeJson(d, slotsJson) || !d.is<JsonArray>()) return "Дані інтервалів недоступні\n";
  String out;
  for (JsonObject slot : d.as<JsonArray>()) {
    int st = slot["s"] | -1;
    int en = slot["e"] | -1;
    String type = slot["t"] | "";
    if (st < 0 || en <= st || en > 1440) continue;
    int duration = en - st;
    int hours = duration / 60;
    int minutes = duration % 60;
    out += (type == "Possible" ? "⚠️ " : "❗ ");
    out += yasnoMinuteText(st) + " → " + yasnoMinuteText(en) + " (";
    if (hours > 0) out += String(hours) + " год";
    if (hours > 0 && minutes > 0) out += " ";
    if (minutes > 0) out += String(minutes) + " хв";
    out += ")\n";
  }
  return out.length() ? out : String("✅ Відключень за графіком немає\n");
}

bool yasnoTomorrowReady() {
  return yasnoTomorrowDate.length() >= 10 && yasnoTodayDate.length() >= 10 &&
         yasnoTomorrowDate.substring(0, 10).compareTo(yasnoTodayDate.substring(0, 10)) > 0 &&
         (yasnoTomorrowStatus == "ScheduleApplies" || yasnoTomorrowStatus == "NoShutdowns" ||
          yasnoTomorrowStatus == "EmergencyShutdowns");
}

String yasnoDayBlock(const char* label, const String& isoDate, const String& status, const String& slotsJson) {
  String b = String(label) + ", " + htmlEsc(yasnoDayCaption(isoDate)) + "\n";
  if (status == "EmergencyShutdowns") return b + "🚨 Екстрені відключення - плановий графік не діє\n";
  if (status == "WaitingForSchedule") return b + "⏳ YASNO ще не підтвердило графік\n";
  if (status == "NoShutdowns") return b + "✅ Відключень за графіком немає\n";
  return b + yasnoSlotLines(slotsJson);
}

String yasnoBuildPublishMessage() {
  String msg = "📅 <b>Графік відключень</b> · черга " + htmlEsc(yasnoGroup) + "\n";
  msg += yasnoDayBlock("Сьогодні", yasnoTodayDate, yasnoTodayStatus, yasnoTodaySlotsJson);
  if (yasnoTomorrowReady()) {
    msg += "\n" + yasnoDayBlock("Завтра", yasnoTomorrowDate, yasnoTomorrowStatus, yasnoTomorrowSlotsJson);
  }
  msg += "<i>Оновлено о " + htmlEsc(getShortTime()) + "</i>";
  return msg;
}

String yasnoEmergencyText(bool active) {
  if (active) {
    return "🚨 <b>Екстрені відключення</b>\n🔢 Черга " + htmlEsc(yasnoGroup) +
           "\n⚠️ Плановий графік наразі не діє\n<i>Джерело: YASNO</i>";
  }
  return "🟢 <b>Екстрені відключення скасовано</b>\n🔢 Черга " + htmlEsc(yasnoGroup) +
         "\n📅 Знову діє плановий графік\n<i>Джерело: YASNO</i>";
}

String yasnoBuildQueryMessage(bool emergencyOnly) {
  if (!yasnoScheduleValid) {
    return "⚠️ <b>Графік YASNO недоступний</b>\n"
           "Не вдалося завантажити графік, тож не можу сказати, чи діють екстрені відключення. Спробуйте пізніше.";
  }

  uint32_t nowEpoch = statsNowEpoch();
  uint32_t ageSec = (nowEpoch > yasnoScheduleFetchedAt) ? (nowEpoch - yasnoScheduleFetchedAt) : 0;
  bool stale = (nowEpoch == 0 || yasnoScheduleFetchedAt == 0 || ageSec > 3UL * 3600UL);

  if (stale) {
    String staleMsg = "⚠️ <b>Графік YASNO застарів</b>\n";
    staleMsg += "Дані давно не оновлювалися, тож не роблю висновок, чи діють екстрені відключення.\n";
    staleMsg += "<i>Останнє оновлення: ";
    staleMsg += htmlEsc(yasnoScheduleUpdatedOn.length() ? yasnoUpdatedCaption(yasnoScheduleUpdatedOn) : String("невідомо"));
    return staleMsg + "</i>";
  }

  String msg;
  if (yasnoTodayStatus == "EmergencyShutdowns") {
    msg = "🚨 <b>Діють екстрені відключення</b>\nПлановий графік YASNO зараз не діє.\n";
  } else if (yasnoTodayStatus == "WaitingForSchedule") {
    msg = "🟡 <b>YASNO ще не підтвердило графік</b>\nПланові інтервали поки не остаточні.\n";
  } else if (yasnoTodayStatus == "ScheduleApplies" || yasnoTodayStatus == "NoShutdowns") {
    msg = "🟢 <b>Екстрених відключень немає</b>\n";
  } else {
    msg = "ℹ️ <b>Статус YASNO:</b> " + htmlEsc(yasnoTodayStatus) + "\n";
  }

  if (!emergencyOnly) {
    msg += "\n📅 <b>Графік відключень</b> · черга " + htmlEsc(yasnoGroup) + "\n";
    msg += yasnoDayBlock("Сьогодні", yasnoTodayDate, yasnoTodayStatus, yasnoTodaySlotsJson);
    if (yasnoTomorrowReady()) {
      msg += "\n" + yasnoDayBlock("Завтра", yasnoTomorrowDate, yasnoTomorrowStatus, yasnoTomorrowSlotsJson);
    } else {
      msg += "\nЗавтра: графік ще не опубліковано\n";
    }
  }

  msg += "<i>Оновлено YASNO: ";
  msg += htmlEsc(yasnoScheduleUpdatedOn.length() ? yasnoUpdatedCaption(yasnoScheduleUpdatedOn) : String("час невідомий"));
  return msg + "</i>";
}

bool yasnoSendQuery(const String& chat) {
  const String text = yasnoBuildQueryMessage(false);
  String photo;
  if (yasnoCaptionFits(text) && srvYasnoImgState(photo) == 1 && tgSendPhoto(chat, photo, text, "HTML")) return true;
  return tgSendText(chat, text, "HTML", "");
}

uint32_t yasnoScheduleSignature() {
  String source = "compact-publish-v2|" + yasnoGroup + "|" + yasnoTodayDate + "|" + yasnoTodayStatus + "|" + yasnoTodaySlotsJson;
  if (yasnoTomorrowReady()) source += "|" + yasnoTomorrowDate + "|" + yasnoTomorrowStatus + "|" + yasnoTomorrowSlotsJson;
  uint32_t sig = statsCrc32((const uint8_t*)source.c_str(), source.length());
  return sig ? sig : 1;
}

bool yasnoTelegramDeleteMessage(const String& targetChat, uint32_t messageId) {
  if (targetChat.length() == 0 || messageId == 0) return false;
  String payload = "{\"chat_id\":\"" + jsonEsc(targetChat) + "\",\"message_id\":" + String(messageId) + "}";
  return tgResultOk(telegramPostRaw("deleteMessage", payload));
}

bool yasnoTelegramEditMessage(const String& targetChat, uint32_t messageId, const String& text) {
  if (targetChat.length() == 0 || messageId == 0 || text.length() == 0) return false;
  String payload = "{\"chat_id\":\"" + jsonEsc(targetChat) +
                   "\",\"message_id\":" + String(messageId) +
                   ",\"text\":\"" + jsonEscText(text) +
                   "\",\"parse_mode\":\"HTML\",\"disable_web_page_preview\":true}";
  const String response = telegramPostRaw("editMessageText", payload);
  return tgResultOk(response) || response.indexOf("message is not modified") >= 0;
}
bool yasnoTelegramEditPhoto(const String& targetChat, uint32_t messageId, const String& photo, const String& caption) {
  if (targetChat.length() == 0 || messageId == 0 || photo.length() == 0) return false;
  String payload = "{\"chat_id\":\"" + jsonEsc(targetChat) +
                   "\",\"message_id\":" + String(messageId) +
                   ",\"media\":{\"type\":\"photo\",\"media\":\"" + jsonEsc(photo) +
                   "\",\"caption\":\"" + jsonEscText(caption) + "\",\"parse_mode\":\"HTML\"}}";
  const String response = telegramPostRaw("editMessageMedia", payload);
  return tgResultOk(response) || response.indexOf("message is not modified") >= 0;
}

bool yasnoTelegramPinMessage(const String& targetChat, uint32_t messageId, bool pin) {
  if (targetChat.length() == 0 || messageId == 0) return false;
  String payload = "{\"chat_id\":\"" + jsonEsc(targetChat) + "\",\"message_id\":" + String(messageId) +
                   (pin ? ",\"disable_notification\":true}" : "}");
  return tgResultOk(telegramPostRaw(pin ? "pinChatMessage" : "unpinChatMessage", payload));
}

uint32_t yasnoDaySig(const String& isoDate, const String& status, const String& slotsJson) {
  const String source = yasnoGroup + "|" + isoDate.substring(0, 10) + "|" + status + "|" + slotsJson;
  return statsCrc32((const uint8_t*)source.c_str(), source.length()) | 1UL;
}

void yasnoPubDays(uint32_t out[2]) {
  out[0] = yasnoDaySig(yasnoTodayDate, yasnoTodayStatus, yasnoTodaySlotsJson);
  out[1] = yasnoTomorrowReady() ? yasnoDaySig(yasnoTomorrowDate, yasnoTomorrowStatus, yasnoTomorrowSlotsJson) : 0;
}

bool yasnoPubHasNewDay(const uint32_t oldDays[2], const uint32_t newDays[2]) {
  for (int i = 0; i < 2; i++) {
    if (newDays[i] != 0 && newDays[i] != oldDays[0] && newDays[i] != oldDays[1]) return true;
  }
  return false;
}

void yasnoPubDaysLoad(const char* key, uint32_t out[2]) {
  out[0] = out[1] = 0;
  if (preferences.getBytesLength(key) != 2 * sizeof(uint32_t) ||
      preferences.getBytes(key, out, 2 * sizeof(uint32_t)) != 2 * sizeof(uint32_t)) {
    out[0] = out[1] = 0;
  }
}

void yasnoPubDaysSave(const char* key, const uint32_t days[2]) {
  preferences.putBytes(key, days, 2 * sizeof(uint32_t));
}

void yasnoPinChanged(int i, bool on) {
  const String prefix = i < 0 ? String("ypOwner") : "yp" + String(i);
  const String target = i < 0 ? chatId : String(chats.e[i].id);
  if (i >= 0 && preferences.getString((prefix + "cid").c_str(), "") != target) return;
  if (on) {
    preferences.remove((prefix + (i < 0 ? "Day" : "day")).c_str());
    preferences.putUInt((prefix + (i < 0 ? "Sig" : "sig")).c_str(), 0);
    if (i >= 0) yasnoPublishCursor = (uint8_t)i;
    yasnoPublishLastMs = 0;
    return;
  }
  const uint32_t mid = preferences.getUInt((prefix + (i < 0 ? "Mid" : "mid")).c_str(), 0);
  if (mid && !yasnoTelegramPinMessage(target, mid, false)) {
    logEvent(LC_YASNO, LS_WARN, "YASNO: графік не відкріплено");
  }
}

bool yasnoWarnFind(const String& todayJson, const String& tomorrowJson, bool tomorrowKnown, int minuteNow, bool powerOn, int maxLead, YasnoWarn& out) {
  out.kind = 0;
  out.start = out.end = out.left = 0;
  out.definite = false;
  int16_t st[YASNO_WARN_SLOTS];
  int16_t en[YASNO_WARN_SLOTS];
  bool df[YASNO_WARN_SLOTS];
  int n = 0;
  for (int day = 0; day < 2; day++) {
    if (day == 1 && !tomorrowKnown) break;
    JsonDocument d;
    if (deserializeJson(d, day == 0 ? todayJson : tomorrowJson) || !d.is<JsonArray>()) {
      if (day == 0) return false;
      tomorrowKnown = false;
      break;
    }
    for (JsonObject slot : d.as<JsonArray>()) {
      const int s = slot["s"] | -1;
      const int e = slot["e"] | -1;
      if (s < 0 || e <= s || e > 1440 || n >= YASNO_WARN_SLOTS) continue;
      const bool definite = strcmp(slot["t"] | "", "Possible") != 0;
      int k = n++;
      while (k > 0 && st[k - 1] > s + day * 1440) {
        st[k] = st[k - 1]; en[k] = en[k - 1]; df[k] = df[k - 1];
        k--;
      }
      st[k] = (int16_t)(s + day * 1440);
      en[k] = (int16_t)(e + day * 1440);
      df[k] = definite;
    }
  }
  int i = 0;
  while (i < n) {
    const int s = st[i];
    int e = en[i];
    bool definite = df[i];
    i++;
    while (i < n && st[i] <= e) {
      if (en[i] > e) e = en[i];
      definite = definite || df[i];
      i++;
    }
    if (powerOn) {
      if (s > minuteNow && s - minuteNow <= maxLead) {
        out.kind = 1;
        out.start = (int16_t)s;
        out.end = (int16_t)e;
        out.left = (int16_t)(s - minuteNow);
        out.definite = definite;
        return true;
      }
    } else if (s <= minuteNow && minuteNow < e) {
      if ((e == 1440 && !tomorrowKnown) || e >= 2880 || e - minuteNow > maxLead) return false;
      out.kind = 2;
      out.start = (int16_t)s;
      out.end = (int16_t)e;
      out.left = (int16_t)(e - minuteNow);
      out.definite = definite;
      return true;
    }
  }
  return false;
}

uint32_t yasnoWarnKey(const YasnoWarn& w, const String& todayDate, const String& tomorrowDate) {
  const int minute = w.kind == 1 ? w.start : w.end;
  const bool nextDay = minute >= 1440;
  const String keySrc = (nextDay ? tomorrowDate : todayDate).substring(0, 10) + "|" + String(w.kind) + "|" +
                        String(nextDay ? minute - 1440 : minute);
  return statsCrc32((const uint8_t*)keySrc.c_str(), keySrc.length()) | 1UL;
}

String yasnoWarnText(const YasnoWarn& w) {
  const String startText = yasnoMinuteText(w.start >= 1440 ? w.start - 1440 : w.start);
  const String endText = yasnoMinuteText(w.end > 1440 ? w.end - 1440 : w.end);
  String t;
  if (w.kind == 1) {
    t = "⏰ <b>О " + startText + (w.definite ? " відключення за графіком" : " можливе відключення за графіком") + "</b>\n";
    t += "Через " + String(w.left) + " хв · до " + endText;
  } else {
    t = "🔜 <b>О " + endText + " за графіком мають дати світло</b>\n";
    t += "Через " + String(w.left) + " хв";
  }
  t += " · черга " + htmlEsc(yasnoGroup) + "\n<i>Джерело: YASNO</i>";
  return t;
}

bool yasnoPubDue(uint8_t i) { return yasnoPubFails[i] == 0 || (long)(millis() - yasnoPubNextMs[i]) >= 0; }
void yasnoPubOk(uint8_t i) { yasnoPubFails[i] = 0; }
void yasnoPubFailed(uint8_t i) {
  if (yasnoPubFails[i] < 6) yasnoPubFails[i]++;
  yasnoPubNextMs[i] = millis() + (60000UL << (yasnoPubFails[i] - 1));
}

static uint16_t yasnoImgBad = 0;
static uint32_t yasnoImgBadSig = 0;

bool yasnoCaptionFits(const String& text) {
  unsigned n = 0;
  const char* p = text.c_str();
  for (; *p; p++) {
    if ((*p & 0xC0) != 0x80) n++;
  }
  return n <= YASNO_CAPTION_MAX;
}

static int8_t yasnoPubImg(uint8_t slot, String& photo) {
  if (!yasnoCaptionFits(yasnoBuildPublishMessage())) {
    photo = "";
    return 0;
  }
  const int8_t st = srvYasnoImgState(photo);
  if (st == 1 && (yasnoImgBad & (1U << slot)) && yasnoImgBadSig == yasnoScheduleSignature()) {
    photo = "";
    return -2;
  }
  if (st != 1) photo = "";
  return st;
}

static uint32_t yasnoPubSig(bool photo) {
  const uint32_t sig = yasnoScheduleSignature() ^ (photo ? YASNO_IMG_SIG : 0UL);
  return sig ? sig : 1;
}

static bool yasnoPubSend(const String& chat, const String& text, const String& photo, bool owner) {
  if (photo.length() == 0) return owner ? tgSendOwnerNotice(text) : tgSendText(chat, text, "HTML", "");
  if (!tgSendPhoto(chat, photo, text, "HTML")) return false;
  if (owner) tgKeepAdd((int32_t)bot.last_sent_message_id);
  return true;
}

static bool yasnoPubEdit(const String& chat, uint32_t messageId, const String& text, const String& photo) {
  return photo.length() == 0 ? yasnoTelegramEditMessage(chat, messageId, text) : yasnoTelegramEditPhoto(chat, messageId, photo, text);
}

static void yasnoImgFailed(uint8_t slot) {
  const uint32_t sig = yasnoScheduleSignature();
  if (yasnoImgBadSig != sig) {
    yasnoImgBadSig = sig;
    yasnoImgBad = 0;
  }
  yasnoImgBad |= (uint16_t)(1U << slot);
  logEvent(LC_YASNO, LS_WARN, "YASNO: фото графіка не прийнято, далі текстом");
}

void yasnoPublishTick() {
  if (yasnoGroup.length() == 0 || WiFi.status() != WL_CONNECTED || !telegramOnline) return;
  if (tgSendAttemptedThisPass) return;
  uint32_t nowMs = millis();
  if (yasnoPublishLastMs && (uint32_t)(nowMs - yasnoPublishLastMs) < 1500UL) return;

  const uint32_t statusNowEpoch = statsNowEpoch();
  const bool statusFresh = statusNowEpoch != 0 && yasnoScheduleFetchedAt != 0 &&
      statusNowEpoch >= yasnoScheduleFetchedAt &&
      (statusNowEpoch - yasnoScheduleFetchedAt) <= 3UL * 3600UL;
  const bool scheduleReady = yasnoScheduleValid && statusFresh &&
      yasnoIsToday(yasnoTodayDate, statusNowEpoch) &&
      yasnoTodayStatus != "EmergencyShutdowns" &&
      yasnoTodayStatus != "WaitingForSchedule";
  const bool emergencyStatusKnown = statusFresh &&
      (yasnoTodayStatus == "EmergencyShutdowns" ||
       yasnoTodayStatus == "ScheduleApplies" ||
       yasnoTodayStatus == "NoShutdowns");

  const bool ownerDue = yasnoPubDue(MAX_CHATS);
  bool warnWanted = ownerYasnoWarnMin > 0;
  for (uint8_t i = 0; i < MAX_CHATS && !warnWanted; i++) warnWanted = chats.e[i].used && chatWarnMin(chats.e[i]) > 0;
  YasnoWarn warn = {0, 0, 0, 0, false};
  uint32_t warnKey = 0;
  if (warnWanted && scheduleReady) {
    time_t nowLocal = (time_t)statusNowEpoch;
    struct tm lt;
    if (localtime_r(&nowLocal, &lt)) {
      const bool tomorrowKnown = yasnoTomorrowReady() &&
          (yasnoTomorrowStatus == "ScheduleApplies" || yasnoTomorrowStatus == "NoShutdowns");
      const String tomorrowJson = yasnoTomorrowStatus == "ScheduleApplies" ? yasnoTomorrowSlotsJson : String("[]");
      if (yasnoWarnFind(yasnoTodaySlotsJson, tomorrowJson, tomorrowKnown, lt.tm_hour * 60 + lt.tm_min,
                        !messageFlag, YASNO_WARN_MAX_MIN, warn)) {
        warnKey = yasnoWarnKey(warn, yasnoTodayDate, yasnoTomorrowDate);
      }
    }
  }

  if (warn.kind && ownerYasnoWarnMin && warn.left <= ownerYasnoWarnMin && yasnoWarnSent[MAX_CHATS] != warnKey && ownerDue) {
    tgSendAttemptedThisPass = true;
    if (tgSendOwnerNotice(yasnoWarnText(warn))) {
      yasnoWarnSent[MAX_CHATS] = warnKey;
      logEvent(LC_YASNO, LS_INFO, "YASNO: попередження за графіком надіслано власнику");
      yasnoPubOk(MAX_CHATS);
    } else {
      yasnoPubFailed(MAX_CHATS);
    }
    yasnoPublishLastMs = millis();
    return;
  }
  if (ownerYasnoPublishOn && scheduleReady && ownerDue) {
    String photo;
    const int8_t img = yasnoPubImg(MAX_CHATS, photo);
    uint32_t newSig = yasnoPubSig(img == 1);
    uint32_t oldSig = preferences.getUInt("ypOwnerSig", 0);
    const bool hold = img == -1 || (img == -2 && oldSig == yasnoPubSig(true));
    if (!hold && newSig != oldSig) {
      tgSendAttemptedThisPass = true;
      String message = yasnoBuildPublishMessage();
      uint32_t oldMessageId = preferences.getUInt("ypOwnerMid", 0);
      uint32_t oldDays[2], newDays[2];
      yasnoPubDaysLoad("ypOwnerDay", oldDays);
      yasnoPubDays(newDays);
      const bool fresh = ownerYasnoPinOn && yasnoPubHasNewDay(oldDays, newDays);
      bool delivered = !fresh && oldMessageId && yasnoPubEdit(chatId, oldMessageId, message, photo);
      if (delivered) {
        preferences.putUInt("ypOwnerSig", newSig);
        yasnoPubDaysSave("ypOwnerDay", newDays);
        logEvent(LC_YASNO, LS_INFO, "YASNO: попередню приватну публікацію оновлено");
      } else if (yasnoPubSend(chatId, message, photo, true)) {
        uint32_t newMessageId = (uint32_t)bot.last_sent_message_id;
        preferences.putUInt("ypOwnerMid", newMessageId);
        preferences.putUInt("ypOwnerSig", newSig);
        yasnoPubDaysSave("ypOwnerDay", newDays);
        if (ownerYasnoPinOn && !yasnoTelegramPinMessage(chatId, newMessageId, true)) {
          logEvent(LC_YASNO, LS_WARN, "YASNO: графік не закріплено у власника");
        }
        if (oldMessageId && oldMessageId != newMessageId && !yasnoTelegramDeleteMessage(chatId, oldMessageId)) {
          logEvent(LC_YASNO, LS_WARN, "YASNO: стару публікацію не видалено");
        }
        logEvent(LC_YASNO, LS_INFO, "YASNO: нову публікацію надіслано власнику");
      } else {
        if (photo.length() > 0) yasnoImgFailed(MAX_CHATS);
        yasnoPubFailed(MAX_CHATS);
        yasnoPublishLastMs = millis();
        return;
      }
      yasnoPubOk(MAX_CHATS);
      yasnoPublishLastMs = millis();
      return;
    }
  }

  bool emergencyOwnerNow = (yasnoTodayStatus == "EmergencyShutdowns");
  bool emergencyOwnerWas = preferences.getBool("yeOwnerSt", false);
  if (emergencyStatusKnown && ownerYasnoEmergencyOn && ownerDue && emergencyOwnerNow != emergencyOwnerWas) {
    String alert = yasnoEmergencyText(emergencyOwnerNow);
    tgSendAttemptedThisPass = true;
    if (tgSendOwnerNotice(alert)) {
      preferences.putBool("yeOwnerSt", emergencyOwnerNow);
      logEvent(LC_YASNO, LS_INFO, "YASNO: екстрений статус надіслано власнику");
      yasnoPubOk(MAX_CHATS);
    } else {
      yasnoPubFailed(MAX_CHATS);
      yasnoPublishLastMs = millis();
      return;
    }
    yasnoPublishLastMs = millis();
    return;
  } else if (emergencyStatusKnown && !ownerYasnoEmergencyOn && emergencyOwnerNow != emergencyOwnerWas) {
    preferences.putBool("yeOwnerSt", emergencyOwnerNow);
  }

  for (uint8_t n = 0; n < MAX_CHATS; n++) {
    uint8_t i = (uint8_t)((yasnoPublishCursor + n) % MAX_CHATS);
    ChatEntry& c = chats.e[i];
    if (!c.used || c.status == 0 || !c.canPost || !chatApproved(c)) continue;
    bool publishOn = (c.flags & CHAT_F_YASNO_PUBLISH) != 0;
    bool emergencyOn = (c.flags & CHAT_F_YASNO_EMERGENCY) != 0;
    const uint8_t warnMin = chatWarnMin(c);
    if (!publishOn && !emergencyOn && warnMin == 0) continue;
    if (!yasnoPubDue(i)) continue;

    String targetChat = String(c.id);
    String suffix = String(i);
    String cidKey = "yp" + suffix + "cid";
    String midKey = "yp" + suffix + "mid";
    String sigKey = "yp" + suffix + "sig";
    String emKey = "ye" + suffix + "st";
    String dayKey = "yp" + suffix + "day";
    String savedChat = preferences.getString(cidKey.c_str(), "");
    if (savedChat != targetChat) {
      preferences.putString(cidKey.c_str(), targetChat);
      preferences.putUInt(midKey.c_str(), 0);
      preferences.putUInt(sigKey.c_str(), 0);
      preferences.remove(dayKey.c_str());
      preferences.putBool(emKey.c_str(), false);
      yasnoWarnSent[i] = 0;
    }

    if (warn.kind && warnMin && warn.left <= warnMin && yasnoWarnSent[i] != warnKey) {
      tgSendAttemptedThisPass = true;
      if (tgSendText(targetChat, yasnoWarnText(warn), "HTML", "")) {
        yasnoWarnSent[i] = warnKey;
        logEvent(LC_YASNO, LS_INFO, "YASNO: попередження надіслано в " + String(c.title));
        yasnoPubOk(i);
      } else {
        grpNoteFail(String(c.title));
        yasnoPubFailed(i);
      }
      yasnoPublishLastMs = millis();
      yasnoPublishCursor = (uint8_t)((i + 1) % MAX_CHATS);
      return;
    }

    if (publishOn && scheduleReady) {
      String photo;
      const int8_t img = yasnoPubImg(i, photo);
      uint32_t newSig = yasnoPubSig(img == 1);
      uint32_t oldSig = preferences.getUInt(sigKey.c_str(), 0);
      const bool hold = img == -1 || (img == -2 && oldSig == yasnoPubSig(true));
      if (!hold && newSig != oldSig) {
        tgSendAttemptedThisPass = true;
        String message = yasnoBuildPublishMessage();
        uint32_t oldMessageId = preferences.getUInt(midKey.c_str(), 0);
        const bool pinOn = (c.pad[0] & CHAT_P0_PIN) != 0;
        uint32_t oldDays[2], newDays[2];
        yasnoPubDaysLoad(dayKey.c_str(), oldDays);
        yasnoPubDays(newDays);
        const bool fresh = pinOn && yasnoPubHasNewDay(oldDays, newDays);
        bool delivered = !fresh && oldMessageId && yasnoPubEdit(targetChat, oldMessageId, message, photo);
        if (delivered) {
          preferences.putUInt(sigKey.c_str(), newSig);
          yasnoPubDaysSave(dayKey.c_str(), newDays);
          logEvent(LC_YASNO, LS_INFO, "YASNO: публікацію оновлено в " + String(c.title));
        } else if (yasnoPubSend(targetChat, message, photo, false)) {
          uint32_t newMessageId = (uint32_t)bot.last_sent_message_id;

          preferences.putUInt(midKey.c_str(), newMessageId);
          preferences.putUInt(sigKey.c_str(), newSig);
          yasnoPubDaysSave(dayKey.c_str(), newDays);
          if (pinOn && !yasnoTelegramPinMessage(targetChat, newMessageId, true)) {
            logEvent(LC_YASNO, LS_WARN, "YASNO: графік не закріплено в " + String(c.title));
          }
          if (oldMessageId && oldMessageId != newMessageId) {
            if (!yasnoTelegramDeleteMessage(targetChat, oldMessageId)) {
              logEvent(LC_YASNO, LS_WARN, "YASNO: стару публікацію не вдалося видалити в " + String(c.title));
            }
          }
          logEvent(LC_YASNO, LS_INFO, "YASNO: нову публікацію надіслано в " + String(c.title));
        } else {
          if (photo.length() > 0) yasnoImgFailed(i);
          grpNoteFail(String(c.title));
          yasnoPubFailed(i);
          yasnoPublishLastMs = millis();
          yasnoPublishCursor = (uint8_t)((i + 1) % MAX_CHATS);
          return;
        }
      }
    }

    bool emergencyNow = (yasnoTodayStatus == "EmergencyShutdowns");
    bool emergencyWas = preferences.getBool(emKey.c_str(), false);
    if (emergencyStatusKnown && emergencyOn && emergencyNow != emergencyWas) {
      String alert = yasnoEmergencyText(emergencyNow);
      tgSendAttemptedThisPass = true;
      if (tgSendText(targetChat, alert, "HTML", "")) {
        preferences.putBool(emKey.c_str(), emergencyNow);
        logEvent(LC_YASNO, LS_INFO, emergencyNow ? "YASNO: екстрений статус опубліковано" : "YASNO: завершення екстреного статусу опубліковано");
      } else {
        grpNoteFail(String(c.title));
        yasnoPubFailed(i);
        yasnoPublishLastMs = millis();
        yasnoPublishCursor = (uint8_t)((i + 1) % MAX_CHATS);
        return;
      }
    } else if (emergencyStatusKnown && !emergencyOn && emergencyNow != emergencyWas) {
      preferences.putBool(emKey.c_str(), emergencyNow);
    }

    yasnoPubOk(i);
    yasnoPublishCursor = (uint8_t)((i + 1) % MAX_CHATS);
    yasnoPublishLastMs = millis();
    return;
  }
  yasnoPublishCursor = 0;
  yasnoPublishLastMs = millis();
}
uint32_t yasnoScheduleInterval(uint8_t fails, bool tomorrowReady, int localHour, bool mainsOff) {
  uint32_t ms = YASNO_REFRESH_INTERVAL_MS;
  if (fails > 0) {
    ms = YASNO_REFRESH_FAST_MS;
    for (uint8_t i = 1; i < fails && ms < YASNO_RETRY_MAX_MS; i++) ms *= 2;
    if (ms > YASNO_RETRY_MAX_MS) ms = YASNO_RETRY_MAX_MS;
  } else if (localHour >= YASNO_EVENING_HOUR && !tomorrowReady) {
    ms = YASNO_REFRESH_FAST_MS;
  }
  if (mainsOff && ms < YASNO_OUTAGE_MS) ms = YASNO_OUTAGE_MS;
  return ms;
}

bool yasnoScheduleBusy() {
  return powerChangePending() || pendingOnMessage || batteryRecPending ||
         (mqCount > 0 && telegramOnline);
}

void yasnoScheduleTick() {
  if (yasnoGroup.length() == 0 || WiFi.status() != WL_CONNECTED || !timeTrusted()) return;
  if (srvYasnoCovered()) return;
  if (yasnoTaskRunning || yasnoTaskDone) return;
  uint32_t nowMs = millis();
  time_t nowEpoch = (time_t)statsNowEpoch();
  struct tm lt;
  int localHour = localtime_r(&nowEpoch, &lt) ? lt.tm_hour : 0;
  uint32_t retryInterval = yasnoScheduleInterval(yasnoScheduleFails, yasnoTomorrowReady(), localHour, messageFlag);
  if (yasnoScheduleLastAttemptMs != 0 &&
      (uint32_t)(nowMs - yasnoScheduleLastAttemptMs) < retryInterval) return;
  if (yasnoScheduleBusy()) return;
  String error;
  if (yasnoStartTask(4, "", error)) yasnoScheduleLastAttemptMs = nowMs;
  else {
    yasnoScheduleLastAttemptMs = nowMs;
    if (error.length()) yasnoScheduleFailLog(error);
  }
}
