#include "lw.h"

bool shellyConfigured() {
  String ip = shellyIP;
  ip.trim();
  return ip.length() > 0;
}

void httpGetBodyBounded(HTTPClient& http, int statusCode, size_t maxBytes, const char* source, String& body) {
  body = String();
  if (statusCode != HTTP_CODE_OK) return;
  int bodySize = http.getSize();
  if (bodySize < 0 || (size_t)bodySize > maxBytes) {
    Serial.println(String(source) + ": HTTP-відповідь має невідомий або надмірний розмір; відхилено");
    return;
  }
  body = http.getString();
  if (body.length() > maxBytes || body.length() != (size_t)bodySize) {
    Serial.println(String(source) + ": HTTP body неповний або перевищує ліміт; відхилено");
    body = String();
  }
}

float shellyJsonFindVoltage(JsonVariantConst v, int depth) {
  if (depth > 6) {
    return NAN;
  }
  if (v.is<JsonObjectConst>()) {
    for (JsonPairConst kv : v.as<JsonObjectConst>()) {
      const char* k = kv.key().c_str();
      size_t kl = strlen(k);

      bool key = (kl == 7 && strcmp(k, "voltage") == 0) || (kl == 9 && (k[0] == 'a' || k[0] == 'b' || k[0] == 'c') && strcmp(k + 1, "_voltage") == 0);
      if (key && (kv.value().is<float>() || kv.value().is<int>())) {
        float x = kv.value().as<float>();
        if (!isnan(x) && x >= 50 && x < 500) {
          return x;
        }
      }
      float r = shellyJsonFindVoltage(kv.value(), depth + 1);
      if (!isnan(r)) {
        return r;
      }
    }
  } else if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst e : v.as<JsonArrayConst>()) {
      float r = shellyJsonFindVoltage(e, depth + 1);
      if (!isnan(r)) {
        return r;
      }
    }
  }
  return NAN;
}
float parseShellyVoltage(const String& body) {
  if (body.length() == 0) {
    return NAN;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (!err) {
    return shellyJsonFindVoltage(doc.as<JsonVariantConst>(), 0);
  }
  return parseShellyVoltageText(body);
}

float parseShellyVoltageText(const String& body) {
  int from = 0;
  while (true) {
    int p = body.indexOf("voltage\":", from);
    if (p < 0) {
      return NAN;
    }
    from = p + 9;
    String rest = body.substring(from, from + 12);
    rest.trim();
    if (rest.length() > 0 && isDigit(rest[0])) {
      float v = rest.toFloat();
      if (v >= 50 && v < 500) {
        return v;
      }
    }
  }
}
float getShellyVoltage() {
  static bool busy = false;
  if (busy) {
    return NAN;
  }
  busy = true;
  float v = getShellyVoltageReq();
  busy = false;
  if (isnan(v) && shellyConfigured() && WiFi.status() == WL_CONNECTED) {
    shellyLastFailMs = millis();
    if (shellyLastFailMs == 0) shellyLastFailMs = 1;
  }
  return v;
}

float getShellyVoltageReq() {
  if (safeMode) return NAN;
  String ip = shellyIP;
  ip.trim();
  if (ip.startsWith("http://")) {
    ip = ip.substring(7);
  }
  while (ip.endsWith("/")) {
    ip.remove(ip.length() - 1);
  }
  if (ip.length() == 0 || WiFi.status() != WL_CONNECTED) {
    return NAN;
  }

  {
    String host = ip;
    int cut = host.indexOf(':');
    if (cut >= 0) host = host.substring(0, cut);
    cut = host.indexOf('/');
    if (cut >= 0) host = host.substring(0, cut);
    IPAddress lit;
    if (!lit.fromString(host.c_str()) && !netDnsQuick(host.c_str(), SHELLY_DNS_WAIT_MS, NULL)) {
      Serial.println("Shelly: ім'я " + host + " не визначено (DNS)");
      return NAN;
    }
  }

  static int workingEndpoint = 0;
  const int endpointCount = 3;
  const char* endpoints[endpointCount] = { "/rpc/Switch.GetStatus?id=0", "/rpc/Shelly.GetStatus", "/status" };

  for (int i = 0; i < endpointCount; i++) {
    int idx = (workingEndpoint + i) % endpointCount;
    WiFiClient httpClient;
    HTTPClient http;
    http.setConnectTimeout(shellyTimeoutMs);
    http.setTimeout(shellyTimeoutMs);
    http.setReuse(false);

    if (!http.begin(httpClient, "http://" + ip + endpoints[idx])) {
      return NAN;
    }
    int code = http.GET();
    if (code <= 0) {

      http.end();
      Serial.println("Shelly не відповідає (" + http.errorToString(code) + ")");
      return NAN;
    }

    String body;
    httpGetBodyBounded(http, code, 16384, "Shelly", body);
    if (code != 200) {
      Serial.println("Shelly: HTTP " + String(code) + " на " + String(endpoints[idx]));
    }
    http.end();

    float v = parseShellyVoltage(body);
    if (!isnan(v)) {
      workingEndpoint = idx;
      return v;
    }
  }
  Serial.println("Shelly: у відповіді немає коректного значення напруги");
  return NAN;
}

float getShellyVoltageFresh() {

  bool recentFail = shellyLastFailMs != 0 && millis() - shellyLastFailMs < 60000UL;
  float v = getShellyVoltage();
  if (isnan(v) && !recentFail) {
    if (shellyConfigured() && WiFi.status() == WL_CONNECTED) {
      delay(300);
      v = getShellyVoltage();
    }
  }
  return v;
}

String voltageText(float v) {
  if (isnan(v)) {
    return "вимірювання недоступне";
  }
  return String(v, 1) + " В";
}
