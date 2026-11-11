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
    error = "YASNO відкладено до відновлення RAM: вільно " + String(heapBeforeTls) +
            " Б, найбільший блок " + String(maxBlockBeforeTls) + " Б.";
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
    error = "YASNO: недостатньо RAM після TLS; вільно " + String(heapAfterTls) +
            " Б, найбільший блок " + String(maxBlockAfterTls) + " Б.";
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
  String t = "⚡ <b>Графік відключень YASNO</b>\n";
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
  t += "\nГрафік оновлюється автоматично приблизно кожні 5 хвилин.";
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
  uint32_t freeBeforeTask = ESP.getFreeHeap();
  uint32_t maxBeforeTask = ESP.getMaxAllocHeap();
  if (freeBeforeTask < 52000UL || maxBeforeTask < 20000UL) {
    error = "YASNO: завдання не запущено через малий запас RAM; вільно " +
            String(freeBeforeTask) + " Б, найбільший блок " +
            String(maxBeforeTask) + " Б.";
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
    logEvent(LC_TG, LS_INFO, "YASNO: адресу перевірено, чергу визначено");
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
      logEvent(LC_TG, LS_WARN, "YASNO: адресу змінено під час перевірки - результат не застосовано");
      if (waiting) menuOpen("yasno", "ℹ️ Адресу змінено під час перевірки - результат не застосовано.\n\n" + yasnoMenuText());
      return;
    }
    if (!yasnoSaveAddress(yasnoCity, yasnoStreet, resultName, resultGroup)) {
      if (waiting) menuOpen("yasno", "❌ Не вдалося підтвердити збереження в Preferences. Попередні налаштування відновлено.\n\n" + yasnoMenuText());
      return;
    }
    logEvent(LC_TG, LS_INFO, "YASNO: збережену адресу перевірено, черга " + resultGroup);
    if (waiting) menuOpen("yasno", "✅ Адресу перевірено. Черга: <b>" + htmlEsc(resultGroup) + "</b>\n\n" + yasnoMenuText());
    return;
  }

  if (kind == 4) {
    if (resultGroupKey.length() == 0 || resultGroupKey != yasnoGroup) return;
    if (!ok) {
      yasnoTaskErrorCopy = error.length() ? error : "Причину збою не повернуто.";
      String diagnostic = "YASNO: не вдалося оновити графік: " + yasnoTaskErrorCopy;
      if (diagnostic.length() > 220) diagnostic = diagnostic.substring(0, 220);
      logEvent(LC_TG, LS_WARN, diagnostic);
      return;
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
      yasnoTaskErrorCopy = "Не вдалося зберегти новий графік. Виконано спробу відновити попередній кеш.";
      return;
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
    yasnoScheduleValid = true;
    if (contentChanged) logEvent(LC_TG, LS_INFO, "YASNO: графік оновлено");
  }
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

uint32_t yasnoScheduleSignature() {
  String source = "compact-publish-v2|" + yasnoGroup + "|" + yasnoTodayDate + "|" + yasnoTodayStatus + "|" + yasnoTodaySlotsJson;
  if (yasnoTomorrowReady()) source += "|" + yasnoTomorrowDate + "|" + yasnoTomorrowStatus + "|" + yasnoTomorrowSlotsJson;
  uint32_t sig = statsCrc32((const uint8_t*)source.c_str(), source.length());
  return sig ? sig : 1;
}

bool yasnoTelegramDeleteMessage(const String& targetChat, uint32_t messageId) {
  if (targetChat.length() == 0 || messageId == 0) return false;
  String payload = "{\"chat_id\":\"" + jsonEsc(targetChat) + "\",\"message_id\":" + String(messageId) + "}";
  String response = telegramPostRaw("deleteMessage", payload);
  JsonDocument d;
  if (response.length() == 0 || deserializeJson(d, response)) return false;
  return d["ok"].as<bool>();
}

bool yasnoTelegramEditMessage(const String& targetChat, uint32_t messageId, const String& text) {
  if (targetChat.length() == 0 || messageId == 0 || text.length() == 0) return false;
  String payload = "{\"chat_id\":\"" + jsonEsc(targetChat) +
                   "\",\"message_id\":" + String(messageId) +
                   ",\"text\":\"" + jsonEscText(text) +
                   "\",\"parse_mode\":\"HTML\",\"disable_web_page_preview\":true}";
  String response = telegramPostRaw("editMessageText", payload);
  JsonDocument d;
  if (response.length() == 0 || deserializeJson(d, response)) return false;
  return d["ok"].as<bool>();
}
bool yasnoPubDue(uint8_t i) { return yasnoPubFails[i] == 0 || (long)(millis() - yasnoPubNextMs[i]) >= 0; }
void yasnoPubOk(uint8_t i) { yasnoPubFails[i] = 0; }
void yasnoPubFailed(uint8_t i) {
  if (yasnoPubFails[i] < 6) yasnoPubFails[i]++;
  yasnoPubNextMs[i] = millis() + (60000UL << (yasnoPubFails[i] - 1));
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
  if (ownerYasnoPublishOn && scheduleReady && ownerDue) {
    uint32_t newSig = yasnoScheduleSignature();
    uint32_t oldSig = preferences.getUInt("ypOwnerSig", 0);
    if (newSig != oldSig) {
      tgSendAttemptedThisPass = true;
      String message = yasnoBuildPublishMessage();
      uint32_t oldMessageId = preferences.getUInt("ypOwnerMid", 0);
      bool delivered = oldMessageId && yasnoTelegramEditMessage(chatId, oldMessageId, message);
      if (delivered) {
        preferences.putUInt("ypOwnerSig", newSig);
        logEvent(LC_TG, LS_INFO, "YASNO: попередню приватну публікацію оновлено");
      } else if (tgSendOwnerNotice(message)) {
        uint32_t newMessageId = (uint32_t)bot.last_sent_message_id;
        preferences.putUInt("ypOwnerMid", newMessageId);
        preferences.putUInt("ypOwnerSig", newSig);
        if (oldMessageId && oldMessageId != newMessageId && !yasnoTelegramDeleteMessage(chatId, oldMessageId)) {
          logEvent(LC_TG, LS_WARN, "YASNO: стару приватну публікацію не вдалося видалити");
        }
        logEvent(LC_TG, LS_INFO, "YASNO: нову публікацію надіслано власнику");
      } else {
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
      logEvent(LC_TG, LS_INFO, "YASNO: екстрений статус надіслано власнику");
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
    if (!publishOn && !emergencyOn) continue;
    if (!yasnoPubDue(i)) continue;

    String targetChat = String(c.id);
    String suffix = String(i);
    String cidKey = "yp" + suffix + "cid";
    String midKey = "yp" + suffix + "mid";
    String sigKey = "yp" + suffix + "sig";
    String emKey = "ye" + suffix + "st";
    String savedChat = preferences.getString(cidKey.c_str(), "");
    if (savedChat != targetChat) {
      preferences.putString(cidKey.c_str(), targetChat);
      preferences.putUInt(midKey.c_str(), 0);
      preferences.putUInt(sigKey.c_str(), 0);
      preferences.putBool(emKey.c_str(), false);
    }

    if (publishOn && scheduleReady) {
      uint32_t newSig = yasnoScheduleSignature();
      uint32_t oldSig = preferences.getUInt(sigKey.c_str(), 0);
      if (newSig != oldSig) {
        tgSendAttemptedThisPass = true;
        String message = yasnoBuildPublishMessage();
        uint32_t oldMessageId = preferences.getUInt(midKey.c_str(), 0);
        bool delivered = oldMessageId && yasnoTelegramEditMessage(targetChat, oldMessageId, message);
        if (delivered) {
          preferences.putUInt(sigKey.c_str(), newSig);
          logEvent(LC_TG, LS_INFO, "YASNO: публікацію оновлено в " + String(c.title));
        } else if (bot.sendMessage(targetChat, message, "HTML")) {
          uint32_t newMessageId = (uint32_t)bot.last_sent_message_id;

          preferences.putUInt(midKey.c_str(), newMessageId);
          preferences.putUInt(sigKey.c_str(), newSig);
          if (oldMessageId && oldMessageId != newMessageId) {
            if (!yasnoTelegramDeleteMessage(targetChat, oldMessageId)) {
              logEvent(LC_TG, LS_WARN, "YASNO: стару публікацію не вдалося видалити в " + String(c.title));
            }
          }
          logEvent(LC_TG, LS_INFO, "YASNO: нову публікацію надіслано в " + String(c.title));
        } else {
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
      if (bot.sendMessage(targetChat, alert, "HTML")) {
        preferences.putBool(emKey.c_str(), emergencyNow);
        logEvent(LC_TG, LS_INFO, emergencyNow ? "YASNO: екстрений статус опубліковано" : "YASNO: завершення екстреного статусу опубліковано");
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
void yasnoScheduleTick() {
  if (yasnoGroup.length() == 0 || WiFi.status() != WL_CONNECTED || !timeTrusted()) return;
  if (yasnoTaskRunning || yasnoTaskDone) return;
  uint32_t nowMs = millis();
  uint32_t retryInterval = yasnoTaskErrorCopy.length() ? 300000UL : YASNO_REFRESH_INTERVAL_MS;
  if (yasnoScheduleLastAttemptMs != 0 &&
      (uint32_t)(nowMs - yasnoScheduleLastAttemptMs) < retryInterval) return;
  String error;
  if (yasnoStartTask(4, "", error)) yasnoScheduleLastAttemptMs = nowMs;
  else {
    yasnoScheduleLastAttemptMs = nowMs;
    if (error.length()) yasnoTaskErrorCopy = error;
  }
}
