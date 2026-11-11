#include "lw.h"

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

  return result;
}

String getFormattedTime() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 100)) {
    return "Час не синхронізовано";
  }

  return formatEpochLocal(mktime(&timeinfo), "%d.%m.%Y %H:%M:%S");
}

String shortTimeAt(unsigned long ms) {
  const uint32_t now = statsNowEpoch();
  if (now == 0) return getShortTime();
  return formatEpochLocal((time_t)(now - (millis() - ms) / 1000UL), "%H:%M");
}

String getShortTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    return "--:--";
  }
  char timeStr[8];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  return String(timeStr);
}

String formatDateUkLocal(time_t e) {
  struct tm t;
  if (!localtime_r(&e, &t)) return "дата невідома";
  static const char* months[] = {
    "січня", "лютого", "березня", "квітня", "травня", "червня",
    "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"
  };
  if (t.tm_mon < 0 || t.tm_mon > 11) return "дата невідома";
  return String(t.tm_mday) + " " + months[t.tm_mon] + " " + String(t.tm_year + 1900);
}

String formatEpochLocal(time_t e, const char* fmt) {
  struct tm t;
  if (!localtime_r(&e, &t)) return "дата невідома";
  if (strstr(fmt, "%d.%m") != nullptr) {
    String out = formatDateUkLocal(e);
    if (strstr(fmt, "%H:%M:%S") != nullptr) {
      char tb[12]; strftime(tb, sizeof(tb), "%H:%M:%S", &t);
      out += (strstr(fmt, "о ") != nullptr) ? " о " : " "; out += tb;
    } else if (strstr(fmt, "%H:%M") != nullptr) {
      char tb[8]; strftime(tb, sizeof(tb), "%H:%M", &t);
      out += (strstr(fmt, "о ") != nullptr) ? " о " : " "; out += tb;
    }
    return out;
  }
  char buf[24];
  strftime(buf, sizeof(buf), fmt, &t);
  return String(buf);
}

bool isSummerTimeNow() {
  struct tm tzInfo;
  return getLocalTime(&tzInfo, 100) && tzInfo.tm_isdst > 0;
}

String formatShortDuration(unsigned long seconds) {
  unsigned long days = seconds / 86400;
  unsigned long hours = (seconds % 86400) / 3600;
  unsigned long minutes = (seconds % 3600) / 60;

  if (days == 0 && hours == 0 && minutes == 0) {
    return "менше 1хв";
  }

  String result = "";
  if (days > 0) {
    result += String(days) + "д ";
  }
  if (hours > 0 || days > 0) {
    result += String(hours) + "год ";
  }
  result += String(minutes) + "хв";
  return result;
}

String timeToken(unsigned long ms) {
  return "{T:" + String(bootId, HEX) + ":" + String(ms) + "}";
}

String durToken(unsigned long ms, uint32_t offStart) {
  return "{D:" + String(bootId, HEX) + ":" + String(ms) + ":" + String(offStart) + "}";
}

bool hasWaitingTokens(const String& t) {
  if (statsNowEpoch() != 0) {
    return false;
  }
  String mine = ":" + String(bootId, HEX) + ":";
  int p = t.indexOf("{T" + mine);
  if (p < 0) p = t.indexOf("{D" + mine);
  return p >= 0;
}

String resolveTimeTokens(const String& in) {
  String t = in;
  uint32_t now = statsNowEpoch();
  String mine = String(bootId, HEX);
  for (int guard = 0; guard < 8; guard++) {
    int a = t.indexOf("{T:");
    int b = t.indexOf("{D:");
    int p = (a >= 0 && (b < 0 || a < b)) ? a : b;
    if (p < 0) {
      break;
    }
    bool isT = (p == a);
    int e = t.indexOf('}', p);
    if (e < 0) {
      break;
    }
    String body = t.substring(p + 3, e);
    int c1 = body.indexOf(':');
    int c2 = (c1 >= 0) ? body.indexOf(':', c1 + 1) : -1;
    String boot = (c1 >= 0) ? body.substring(0, c1) : String("");
    unsigned long ms = (c1 >= 0) ? strtoul(body.substring(c1 + 1, c2 < 0 ? body.length() : c2).c_str(), NULL, 10) : 0;
    uint32_t start = (c2 >= 0) ? strtoul(body.substring(c2 + 1).c_str(), NULL, 10) : 0;
    bool ok = (boot == mine) && now != 0;
    uint32_t ev = ok ? now - (millis() - ms) / 1000 : 0;
    String val;
    if (isT) {
      val = ok ? formatEpochLocal((time_t)ev, "%H:%M") : String("--:--");
    } else {
      val = (ok && ev >= start) ? " " + formatShortDuration(ev - start) : String(": невідомо");
    }
    t = t.substring(0, p) + val + t.substring(e + 1);
  }
  return t;
}

void eventsTimeFixTick() {
  if ((!offShortFix && !onShortFix) || statsNowEpoch() == 0) {
    return;
  }
  uint32_t now = statsNowEpoch();
  if (offShortFix) {
    offShortFix = false;
    if (powerOffShortTime == "--:--") {
      powerOffShortTime = formatEpochLocal((time_t)(now - (millis() - offShortFixMs) / 1000), "%H:%M");
    }
  }
  if (onShortFix) {
    onShortFix = false;
    if (powerOnShortTime == "--:--") {
      powerOnShortTime = formatEpochLocal((time_t)(now - (millis() - onShortFixMs) / 1000), "%H:%M");
    }
  }
}
