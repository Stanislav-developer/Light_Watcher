#include "lw.h"

static String mqHeadMsg = "";
static uint16_t mqHeadRoundFailed = 0;
static bool mqHeadRoundActive = false;
static bool mqHeadRoundDelivered = false;
static String mqItems[MQ_MAX];
static bool mqBlobTooLong = false;
static uint8_t pendReleaseWanted = 0;
static String mqDoneWait[MQ_MAX];
static uint8_t mqLoadFail = 0;
static String mqHeadKey = "";
static uint16_t mqHeadPending = 0xFFFFU;
static int mqHeadFails = 0;

const char* const PEND_SEQ_KEYS[PEND_SLOTS] = { "mqPendS", "mqPendS2" };
uint8_t* mqBlob(size_t& total) {
  total = 8 + 4;
  mqBlobTooLong = false;
  for (int i = 0; i < mqCount; i++) {
    if (mqItems[i].length() > MQ_ITEM_MAX) {
      mqBlobTooLong = true;
      return NULL;
    }
    total += 2 + mqItems[i].length();
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (buf == NULL) {
    return NULL;
  }
  uint32_t magic = MQ_MAGIC;
  memcpy(buf, &magic, 4);
  buf[4] = (uint8_t)mqCount;
  buf[5] = buf[6] = buf[7] = 0;
  size_t pos = 8;
  for (int i = 0; i < mqCount; i++) {
    uint16_t len = (uint16_t)mqItems[i].length();
    memcpy(buf + pos, &len, 2);
    memcpy(buf + pos + 2, mqItems[i].c_str(), len);
    pos += 2 + len;
  }
  uint32_t crc = statsCrc32(buf, pos);
  memcpy(buf + pos, &crc, 4);
  return buf;
}

bool mqSave() {
  size_t total;
  uint8_t* buf = mqBlob(total);
  if (buf == NULL) {
    logEvent(LC_STORE, LS_ERR, mqBlobTooLong ? "Черга: повідомлення довше MQ_ITEM_MAX - запис не виконано (не обрізається)"
                                             : "Черга: не вистачило пам'яті для збереження");
    return false;
  }
  bool ok = preferences.putBytes("mqV2", buf, total) == total;
  free(buf);
  if (!ok) {
    static unsigned long lastLog = 0;
    if (lastLog == 0 || millis() - lastLog >= 600000UL) {
      lastLog = millis();
      logEvent(LC_STORE, LS_ERR, "Черга: запис у флеш-пам'ять не вдався (повідомлення залишаються в оперативній пам'яті)");
    }
  }
  return ok;
}

bool mqStoredOk() {
  size_t total;
  uint8_t* buf = mqBlob(total);
  if (buf == NULL) {
    return false;
  }
  bool ok = preferences.getBytesLength("mqV2") == total;
  if (ok) {
    uint8_t* chk = (uint8_t*)malloc(total);
    ok = chk != NULL && preferences.getBytes("mqV2", chk, total) == total && memcmp(chk, buf, total) == 0;
    free(chk);
  }
  free(buf);
  return ok;
}

String mqEventKey(const String& item) {
  int p = item.indexOf(MQ_SEP);
  return (p >= 0) ? item.substring(0, p) : item;
}

bool mqHasEvent(const String& key) {
  for (int i = 0; i < mqCount; i++) {
    if (mqEventKey(mqItems[i]) == key) {
      return true;
    }
  }
  return false;
}
bool pendRelease(int slot) {
  if (slot < 0 || slot >= PEND_SLOTS) {
    return true;
  }
  if (!preferences.isKey(PEND_KEYS[slot]) && !preferences.isKey(PEND_SEQ_KEYS[slot])) {
    pendReleaseWanted &= ~(1 << slot);
    return true;
  }
  preferences.remove(PEND_KEYS[slot]);
  if (!preferences.isKey(PEND_KEYS[slot])) {

    if (preferences.isKey(PEND_SEQ_KEYS[slot])) {
      preferences.remove(PEND_SEQ_KEYS[slot]);
    }
    if (!preferences.isKey(PEND_SEQ_KEYS[slot])) {
      pendReleaseWanted &= ~(1 << slot);
      return true;
    }
  }
  pendReleaseWanted |= (1 << slot);
  {
    static unsigned long lastLog = 0;
    if (lastLog == 0 || millis() - lastLog >= 600000UL) {
      lastLog = millis();
      logEvent(LC_STORE, LS_ERR, String("Резервна копія ") + PEND_KEYS[slot] + ": видалення не підтверджено, повтор");
    }
    return false;
  }
  return true;
}

void pendReleaseEvent(const String& key) {
  for (int i = 0; i < PEND_SLOTS; i++) {
    if (pendingOnMessage && i == pendSlot) {
      continue;
    }
    if (preferences.isKey(PEND_KEYS[i]) && mqEventKey(preferences.getString(PEND_KEYS[i], "")) == key) {
      pendRelease(i);
    }
  }
}

void pendReleaseQueued() {
  if (mqDirty) {
    return;
  }
  for (int i = 0; i < PEND_SLOTS; i++) {
    if ((pendingOnMessage && i == pendSlot) || !preferences.isKey(PEND_KEYS[i])) {
      continue;
    }
    String item = preferences.getString(PEND_KEYS[i], "");
    if (item.length() > 0 && mqHasEvent(mqEventKey(item))) {
      pendRelease(i);
    }
  }
}

bool mqGuarded(const String& key) {
  for (int k = 0; k < PEND_SLOTS; k++) {
    if (preferences.isKey(PEND_KEYS[k]) && mqEventKey(preferences.getString(PEND_KEYS[k], "")) == key) {
      return true;
    }
  }
  return false;
}

int mqDropIndex() {
  int firstFree = -1;
  for (int i = 0; i < mqCount; i++) {
    if (mqGuarded(mqEventKey(mqItems[i]))) {
      continue;
    }
    if (mqItems[i].length() > 0 && mqItems[i][0] != 'N') {
      return i;
    }
    if (firstFree < 0) {
      firstFree = i;
    }
  }

  return firstFree;
}
int mqUnsavedCount() {
  if (!mqDirty) {
    return 0;
  }

  String flash[MQ_MAX];
  int fn = 0;
  if (preferences.isKey("mqV2")) {
    size_t total = preferences.getBytesLength("mqV2");
    if (total < 12 || total > 8 + 4 + MQ_MAX * (2 + MQ_ITEM_MAX)) {
      return -1;
    }
    uint8_t* buf = (uint8_t*)malloc(total);
    if (buf == NULL) {
      return -1;
    }
    bool ok = preferences.getBytes("mqV2", buf, total) == total;
    uint32_t magic = 0, crc = 0;
    if (ok) {
      memcpy(&magic, buf, 4);
      memcpy(&crc, buf + total - 4, 4);
      ok = magic == MQ_MAGIC && buf[4] <= MQ_MAX && crc == statsCrc32(buf, total - 4);
    }
    size_t pos = 8;
    int cnt = ok ? buf[4] : 0;
    for (int i = 0; ok && i < cnt; i++) {
      uint16_t len;
      if (pos + 2 > total - 4) {
        ok = false;
        break;
      }
      memcpy(&len, buf + pos, 2);
      if (pos + 2 + len > total - 4) {
        ok = false;
        break;
      }
      String it;
      it.reserve(len);
      for (uint16_t c = 0; c < len; c++) it += (char)buf[pos + 2 + c];
      flash[fn++] = mqEventKey(it);
      pos += 2 + len;
    }
    free(buf);
    if (!ok || pos != total - 4) {
      return -1;
    }
  }
  int n = 0;
  for (int i = 0; i < mqCount; i++) {
    String key = mqEventKey(mqItems[i]);
    bool inFlash = false;
    for (int j = 0; j < fn && !inFlash; j++) inFlash = flash[j] == key;
    if (!inFlash && !mqGuarded(key)) n++;
  }
  return n;
}
bool mqDoneHas(const String& done, const String& key) {
  int start = 0;
  while (start <= (int)done.length()) {
    int e = done.indexOf(MQ_DONE_SEP, start);
    String part = (e < 0) ? done.substring(start) : done.substring(start, e);
    if (part.length() > 0 && part == key) {
      return true;
    }
    if (e < 0) break;
    start = e + 1;
  }
  return false;
}

String mqDoneTag() {

  if (!preferences.isKey("mqV2")) {
    return String("");
  }
  size_t total = preferences.getBytesLength("mqV2");
  if (total < 12 || total > 8 + 4 + MQ_MAX * (2 + MQ_ITEM_MAX)) {
    return String("");
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (buf == NULL) {
    return String("");
  }
  uint32_t crc = 0;
  bool ok = preferences.getBytes("mqV2", buf, total) == total;
  if (ok) {

    uint32_t magic = 0;
    memcpy(&magic, buf, 4);
    ok = magic == MQ_MAGIC && buf[4] <= MQ_MAX;
    size_t pos = 8;
    for (int i = 0; ok && i < buf[4]; i++) {
      uint16_t len;
      if (pos + 2 > total - 4) {
        ok = false;
        break;
      }
      memcpy(&len, buf + pos, 2);
      if (len > MQ_ITEM_MAX || pos + 2 + len > total - 4) {
        ok = false;
        break;
      }
      pos += 2 + len;
    }
    ok = ok && pos == total - 4;
  }
  if (ok) {
    memcpy(&crc, buf + total - 4, 4);
    ok = crc == statsCrc32(buf, total - 4);
  }
  free(buf);
  if (!ok) {
    return String("");
  }
  char t[12];
  snprintf(t, sizeof(t), "#%08lx", (unsigned long)crc);
  return String(t);
}

bool mqDoneCurrent(const String& done, const String& tag) {
  int e = done.indexOf(MQ_DONE_SEP);
  return tag.length() > 0 && e > 0 && done.substring(0, e) == tag;
}

int mqDoneCount() {
  if (!preferences.isKey(MQ_DONE_KEY)) return 0;
  String d = preferences.getString(MQ_DONE_KEY, "");
  if (!mqDoneCurrent(d, mqDoneTag())) return 0;
  int n = 0;
  for (unsigned i = 0; i < d.length(); i++) {
    if (d[i] == MQ_DONE_SEP) n++;
  }
  return n;
}

bool mqDoneAdd(const String& key) {

  String tag = mqDoneTag();
  if (tag.length() == 0) {
    return false;
  }
  String d = mqDoneClearFailed ? String("") : preferences.getString(MQ_DONE_KEY, "");
  if (!mqDoneCurrent(d, tag)) {
    d = "";
  } else {
    d = d.substring(d.indexOf(MQ_DONE_SEP) + 1);
  }

  int cnt = d.length() > 0 ? 1 : 0;
  for (unsigned i = 0; i < d.length(); i++) {
    if (d[i] == MQ_DONE_SEP) cnt++;
  }
  if (cnt >= MQ_MAX) {
    int e = d.indexOf(MQ_DONE_SEP);
    d = (e < 0) ? String("") : d.substring(e + 1);
  }
  String nv = tag + MQ_DONE_SEP + (d.length() > 0 ? d + MQ_DONE_SEP + key : key);
  bool ok = prefPutStrVerified(MQ_DONE_KEY, nv);
  if (ok) {
    mqDoneClearFailed = false;
  }
  return ok;
}

bool mqDoneClear() {
  if (!preferences.isKey(MQ_DONE_KEY)) {
    mqDoneClearFailed = false;
    return true;
  }
  preferences.remove(MQ_DONE_KEY);
  bool ok = !preferences.isKey(MQ_DONE_KEY);
  if (!ok) {
    preferences.putString(MQ_DONE_KEY, "");
    ok = preferences.isKey(MQ_DONE_KEY) && preferences.getString(MQ_DONE_KEY, "x").length() == 0;
  }
  if (!ok && !mqDoneClearFailed) {
    logEvent(LC_STORE, LS_ERR, "Черга: список доставлених не очищено (черга записана) - повтор раз на хвилину");
  }
  mqDoneClearFailed = !ok;
  return ok;
}

void mqDoneSettle(const String& key) {
  if (!mqDirty || mqDoneAdd(key)) {
    pendReleaseEvent(key);
    return;
  }
  if (mqDoneWaitN < MQ_MAX) {
    mqDoneWait[mqDoneWaitN++] = key;
  }
  logEvent(LC_STORE, LS_ERR, "Черга: доставлене не позначено у флеш-пам'яті - копію збережено, після перезапуску можливий повтор");
}
bool mqPersist() {

  if (mqLoadBlocked && !mqUnblock()) {
    mqDirty = true;
    mqDirtyTryMs = millis();
    return false;
  }
  bool ok = mqSave() && mqStoredOk();
  mqDirty = !ok;
  mqDirtyTryMs = millis();
  if (ok) {
    mqUnsaved = 0;

    mqDoneClear();

    for (int i = 0; i < mqDoneWaitN; i++) {
      pendReleaseEvent(mqDoneWait[i]);
    }
    mqDoneWaitN = 0;
    pendReleaseQueued();
  }
  return ok;
}

int pendWriteFree(const String& item) {
  uint32_t seq = 1;
  for (int i = 0; i < PEND_SLOTS; i++) {
    if (preferences.isKey(PEND_KEYS[i])) {
      uint32_t q = preferences.getUInt(PEND_SEQ_KEYS[i], 0);
      if (q >= seq) seq = q + 1;
    }
  }
  for (int i = 0; i < PEND_SLOTS; i++) {
    if (!preferences.isKey(PEND_KEYS[i])) {
      if (prefPutStrVerified(PEND_KEYS[i], item)) {

        bool seqOk = false;
        for (int t = 0; t < 2 && !seqOk; t++) {
          seqOk = preferences.putUInt(PEND_SEQ_KEYS[i], seq) > 0 && preferences.getUInt(PEND_SEQ_KEYS[i], 0) == seq;
        }
        if (seqOk) {
          return i;
        }

        logEvent(LC_STORE, LS_ERR, String("Резервна копія ") + PEND_KEYS[i] + ": порядковий номер не записано - копію не зараховано");
        pendRelease(i);
        return -1;
      }
      pendRelease(i);
    }
  }
  return -1;
}

bool pendStore(const String& item) {
  if (item.length() > MQ_ITEM_MAX) {
    logEvent(LC_STORE, LS_ERR, "Резервна копія довша MQ_ITEM_MAX - не записано");
    pendSlot = -1;
    return false;
  }
  bool busy = false;
  for (int i = 0; i < PEND_SLOTS; i++) {
    if (preferences.isKey(PEND_KEYS[i]) && !(pendingOnMessage && i == pendSlot)) {
      busy = true;
    }
  }
  if (busy || mqDirty) {
    mqPersist();
  }
  pendSlot = pendWriteFree(item);
  return pendSlot >= 0;
}

bool mqLoadV2Into(String* items, int& outN) {
  mqLoadFail = 0;
  outN = 0;
  size_t total = preferences.getBytesLength("mqV2");
  if (total < 12 || total > 8 + 4 + MQ_MAX * (2 + MQ_ITEM_MAX)) {
    return false;
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (buf == NULL) {
    mqLoadFail = 2;
    return false;
  }

  if (preferences.getBytes("mqV2", buf, total) != total) {
    free(buf);
    mqLoadFail = 1;
    return false;
  }
  uint32_t magic, crc;
  memcpy(&magic, buf, 4);
  memcpy(&crc, buf + total - 4, 4);
  bool ok = (magic == MQ_MAGIC) && buf[4] <= MQ_MAX && crc == statsCrc32(buf, total - 4);
  int n = ok ? buf[4] : 0;
  size_t pos = 8;
  for (int i = 0; ok && i < n; i++) {
    uint16_t len;
    if (pos + 2 > total - 4) { ok = false; break; }
    memcpy(&len, buf + pos, 2);
    if (len > MQ_ITEM_MAX || pos + 2 + len > total - 4) { ok = false; break; }
    char* tmp = (char*)malloc(len + 1);
    if (tmp == NULL) { ok = false; mqLoadFail = 2; break; }
    memcpy(tmp, buf + pos + 2, len);
    tmp[len] = 0;
    items[i] = String(tmp);
    free(tmp);
    if (items[i].length() != len) { ok = false; mqLoadFail = 2; break; }
    pos += 2 + len;
  }
  ok = ok && pos == total - 4;
  free(buf);
  outN = ok ? n : 0;
  return ok;
}

bool mqLoadV2() {
  int n = 0;
  bool ok = mqLoadV2Into(mqItems, n);
  mqCount = ok ? n : 0;
  return ok;
}

bool mqUnblock() {
  String fl[MQ_MAX];
  int fn = 0;
  if (!mqLoadV2Into(fl, fn)) {
    if (mqLoadFail == 0) {
      logEvent(LC_STORE, LS_ERR, "Черга: записаний блок пошкоджений - замінюється поточною чергою");
      mqLoadBlocked = false;
      return true;
    }
    static unsigned long lastLog = 0;
    if (lastLog == 0 || millis() - lastLog >= 3600000UL) {
      lastLog = millis();
      logEvent(LC_STORE, LS_ERR, "Черга: записаний блок досі не читається - перезапис заблоковано (" + String((millis() - mqBlockedSince) / 60000UL) + " хв)");
    }
    return false;
  }

  String done = preferences.isKey(MQ_DONE_KEY) ? preferences.getString(MQ_DONE_KEY, "") : String("");
  if (!mqDoneCurrent(done, mqDoneTag())) {
    done = "";
  }
  String big[2 * MQ_MAX];
  int bn = 0;
  for (int i = 0; i < fn; i++) {
    if (!mqDoneHas(done, mqEventKey(fl[i]))) {
      big[bn++] = fl[i];
    }
  }
  for (int i = 0; i < mqCount; i++) {
    String key = mqEventKey(mqItems[i]);
    bool dup = false;
    for (int j = 0; j < bn && !dup; j++) dup = mqEventKey(big[j]) == key;
    if (!dup) {
      big[bn++] = mqItems[i];
    }
  }
  while (bn > MQ_MAX) {
    int d = -1, firstFree = -1;
    for (int i = 0; i < bn && d < 0; i++) {
      if (mqGuarded(mqEventKey(big[i]))) continue;
      if (big[i].length() > 0 && big[i][0] != 'N') d = i;
      else if (firstFree < 0) firstFree = i;
    }
    if (d < 0) d = firstFree;
    if (d < 0) {
      return false;
    }
    logEvent(LC_STORE, LS_WARN, "Черга переповнена - відкинуто: " + mqEventKey(big[d]).substring(1, 60));
    for (int i = d + 1; i < bn; i++) big[i - 1] = big[i];
    bn--;
  }
  for (int i = 0; i < bn; i++) mqItems[i] = big[i];
  mqCount = bn;
  mqLoadBlocked = false;
  mqLoadLost = false;
  logEvent(LC_STORE, LS_INFO, "Черга: записаний блок прочитано повторно - повідомлення відновлено (" + String(fn) + ")");
  return true;
}

void mqLoad() {
  mqCount = 0;
  bool loaded = false;
  if (preferences.isKey("mqV2")) {
    loaded = mqLoadV2();

    for (int a = 0; !loaded && mqLoadFail != 0 && a < 2; a++) {
      delay(50);
      loaded = mqLoadV2();
    }
    if (!loaded) {
      mqCount = 0;
      if (mqLoadFail != 0) {

        logEvent(LC_STORE, LS_ERR, mqLoadFail == 2 ? "Черга: не вистачило пам'яті для читання записаного блоку - перезапис заблоковано"
                                                   : "Черга: записаний блок не вдалося прочитати - перезапис заблоковано");
        mqLoadLost = true;
        mqLoadBlocked = true;
        mqBlockedSince = millis();
      } else {
        logEvent(LC_STORE, LS_ERR, "Черга недоставлених повідомлень пошкоджена - очищено");
        mqSave();
      }
    }
  }
  bool pendAdded = false, pendFound = false;

  int order[PEND_SLOTS] = { 0, 1 };
  pendOrderNote = "";
  if (preferences.isKey(PEND_KEYS[0]) && preferences.isKey(PEND_KEYS[1])) {
    uint32_t q0 = preferences.getUInt(PEND_SEQ_KEYS[0], 0), q1 = preferences.getUInt(PEND_SEQ_KEYS[1], 0);
    if (q0 != 0 && q1 != 0 && q0 != q1) {
      if (q0 > q1) {
        order[0] = 1;
        order[1] = 0;
      }
    } else if (q0 == 0 && q1 == 0) {
      pendOrderNote = "обидві копії без порядкового номера - за номером слота";
    } else if (q0 == q1) {
      pendOrderNote = "однакові порядкові номери копій - за номером слота";
    } else {
      if (q0 == 0) {
        order[0] = 1;
        order[1] = 0;
      }
      pendOrderNote = "одна копія без порядкового номера - відновлена останньою";
    }
    if (pendOrderNote.length() > 0) {
      logEvent(LC_STORE, LS_WARN, "Порядок резервних копій неточний: " + pendOrderNote);
    }
  }
  for (int oi = 0; oi < PEND_SLOTS; oi++) {
    int k = order[oi];
    if (!preferences.isKey(PEND_KEYS[k])) {
      continue;
    }
    pendFound = true;
    String pend = preferences.getString(PEND_KEYS[k], "");

    int ps = pend.indexOf(MQ_SEP);
    String pendText = (ps >= 0) ? pend.substring(0, ps) : pend;
    bool already = false;
    for (int i = 0; i < mqCount && !already; i++) {
      int qs = mqItems[i].indexOf(MQ_SEP);
      already = ((qs >= 0) ? mqItems[i].substring(0, qs) : mqItems[i]) == pendText;
    }
    if (pend.length() > MQ_ITEM_MAX) {

      logEvent(LC_STORE, LS_ERR, "Резервна копія довша MQ_ITEM_MAX - не додано до черги, копію збережено");
      continue;
    }
    if (pend.length() > 0 && !already) {
      if (mqCount >= MQ_MAX && mqDropIndex() < 0) {

        logEvent(LC_STORE, LS_ERR, "Черга переповнена захищеними повідомленнями - резервна копія лишається у слоті");
        continue;
      }
      if (mqCount >= MQ_MAX) {
        int d = mqDropIndex();
        logEvent(LC_STORE, LS_WARN, "Черга переповнена - відкинуто: " + mqEventKey(mqItems[d]).substring(1, 60));
        for (int i = d + 1; i < mqCount; i++) mqItems[i - 1] = mqItems[i];
        mqCount--;
      }
      mqItems[mqCount++] = pend;
      pendAdded = true;
    }
  }

  bool doneRemoved = false;
  if (preferences.isKey(MQ_DONE_KEY)) {
    String done = preferences.getString(MQ_DONE_KEY, "");

    if (!mqDoneCurrent(done, mqDoneTag())) {
      if (done.length() > 0) {
        logEvent(LC_STORE, LS_WARN, "Черга: список доставлених не підтверджено (застарілий) - не застосовано, можливий повтор");
      }
      done = "";
    }
    for (int i = 0; i < mqCount;) {
      if (mqDoneHas(done, mqEventKey(mqItems[i]))) {
        for (int j = i + 1; j < mqCount; j++) mqItems[j - 1] = mqItems[j];
        mqCount--;
        doneRemoved = true;
      } else {
        i++;
      }
    }
    for (int k = 0; k < PEND_SLOTS; k++) {
      if (preferences.isKey(PEND_KEYS[k]) && mqDoneHas(done, mqEventKey(preferences.getString(PEND_KEYS[k], "")))) {
        pendRelease(k);
      }
    }
    if (doneRemoved) {
      logEvent(LC_STORE, LS_INFO, "Черга: доставлені до перезапуску повідомлення не надсилатимуться вдруге");
    }
  }
  if (pendFound || doneRemoved || preferences.isKey(MQ_DONE_KEY)) {

    if (!mqPersist()) {
      logEvent(LC_STORE, LS_ERR, "Черга: повідомлення з резервної копії не записано, копія збережена");
    } else if (pendAdded) {
      logEvent(LC_STORE, LS_INFO, "Черга: відновлено повідомлення з резервної копії");
    }
  }
  if (mqCount > 0) {
    Serial.println("Черга: " + String(mqCount) + " недоставлених повідомлень");
  }
}

bool mqPush(const String& item) {

  if (item.length() > MQ_ITEM_MAX) {
    mqRejected++;
    logEvent(LC_STORE, LS_ERR, "Черга: повідомлення довше " + String(MQ_ITEM_MAX) + " байт не прийнято (" + String(item.length()) + " байт)");
    return false;
  }
  bool dropped = false;
  if (mqCount >= MQ_MAX) {
    int d = mqDropIndex();
    if (d < 0) {

      bool kept = mqGuarded(mqEventKey(item)) || pendWriteFree(item) >= 0;
      if (!kept) {
        mqUnsaved++;
      }
      logEvent(LC_STORE, LS_ERR, String("Черга переповнена захищеними повідомленнями - нове не додано") + (kept ? " (є резервна копія)" : " (втрачено)"));
      return false;
    }
    logEvent(LC_STORE, LS_WARN, "Черга переповнена - відкинуто: " + mqEventKey(mqItems[d]).substring(1, 60));
    for (int i = d + 1; i < mqCount; i++) mqItems[i - 1] = mqItems[i];
    mqCount--;
    dropped = true;
  }
  mqItems[mqCount++] = item;
  bool ok = mqPersist();
  if (!ok && dropped) {

    logEvent(LC_STORE, LS_WARN, "Черга: відкидання ще не записано - відкинуте повідомлення залишається у флеш-пам'яті");
  }

  int freeSlots = 0;
  for (int i = 0; i < PEND_SLOTS; i++) {
    if (!preferences.isKey(PEND_KEYS[i])) freeSlots++;
  }
  bool mayUse = item.length() > 0 && (item[0] == 'N' ? freeSlots >= 1 : freeSlots >= 2);
  if (!ok && !mqGuarded(mqEventKey(item))) {
    if (mayUse && pendWriteFree(item) >= 0) {
      logEvent(LC_STORE, LS_WARN, "Черга не записалася - повідомлення збережено в резервну копію");
    } else {
      int u = mqUnsavedCount();
      mqUnsaved = (u >= 0) ? u : mqUnsaved + 1;
      logEvent(LC_STORE, LS_ERR, String(mayUse ? "Черга не записалася, резервну копію не записано" : "Черга не записалася, обидва резервні слоти зайняті")
                                 + " - повідомлення лише в оперативній пам'яті");
    }
  }
  Serial.println("Повідомлення поставлено в чергу (" + String(mqCount) + ")" + (ok ? "" : " - запис не підтверджено"));
  return ok;
}

void mqPop() {
  for (int i = 1; i < mqCount; i++) mqItems[i - 1] = mqItems[i];
  if (mqCount > 0) mqCount--;
  mqPersist();
}

String withVoltageLine(const String& text, float v) {
  const String line = "⚡ Напруга: " + voltageText(v);
  const int sched = text.indexOf("\n📅 ");
  if (sched >= 0) return text.substring(0, sched + 1) + line + text.substring(sched);
  String msg = text;
  if (msg.length() > 0 && !msg.endsWith("\n")) msg += "\n";
  return msg + line;
}

String mqBuild(const String& itemIn, bool delayed) {
  String item = resolveTimeTokens(itemIn);
  if (item.length() == 0) return "";
  if (item[0] == 'F') {
    String t = item.substring(1);
    if (delayed) t += "\n\n📵 Надіслано із затримкою: не було зв'язку";
    return t;
  }
  int sep = item.indexOf(MQ_SEP);
  String text = (sep >= 0) ? item.substring(1, sep) : item.substring(1);
  String note = (sep >= 0) ? item.substring(sep + 1) : String("");

  String msg = withVoltageLine(text, messageFlag ? NAN : getShellyVoltage());
  if (note.length() > 0) {
    msg += "\n\n" + note;
  }
  return msg;
}

void mqProcess() {

  if (mqDirty && millis() - mqDirtyTryMs >= 60000UL) {
    if (mqPersist()) {
      logEvent(LC_STORE, LS_INFO, "Черга: запис у флеш-пам'ять відновлено");
    }
  }

  static unsigned long pendCheckMs = 0;
  if (millis() - pendCheckMs >= 60000UL) {
    pendCheckMs = millis();
    for (int i = 0; i < PEND_SLOTS; i++) {
      if ((pendReleaseWanted & (1 << i)) && !(pendingOnMessage && i == pendSlot)) {
        pendRelease(i);
      }
    }
    pendReleaseQueued();

    for (int i = 0; i < mqDoneWaitN;) {
      if (mqDirty && mqDoneAdd(mqDoneWait[i])) {
        pendReleaseEvent(mqDoneWait[i]);
        for (int j = i + 1; j < mqDoneWaitN; j++) mqDoneWait[j - 1] = mqDoneWait[j];
        mqDoneWaitN--;
      } else {
        i++;
      }
    }
    if (mqDoneClearFailed && !mqDirty) {
      mqDoneClear();
    }
  }
  if (mqCount == 0 || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (tgSendAttemptedThisPass) {
    return;
  }
  const bool continuing = mqHeadRoundActive && mqHeadKey == mqEventKey(mqItems[0]);
  if (!continuing && millis() - mqLastTry < (telegramOnline ? 5000UL : 30000UL)) {
    return;
  }
  if (powerChangePending()) {
    return;
  }

  bool onlineLong = wifiUpSinceMs != 0 && millis() - wifiUpSinceMs >= 120000UL && telegramOnline;
  if (hasWaitingTokens(mqItems[0]) && !onlineLong) {
    return;
  }
  if (!tgNetAllowed()) {
    return;
  }
  const String key = mqEventKey(mqItems[0]);
  if (key != mqHeadKey) {
    mqHeadKey = key;
    mqHeadPending = RCPT_ALL;
    mqHeadFails = 0;
    mqHeadRoundActive = false;
  }
  if (!mqHeadRoundActive) {
    mqLastTry = millis();
    mqHeadMsg = mqBuild(mqItems[0], true);
    mqHeadRoundFailed = 0;
    mqHeadRoundDelivered = false;
    mqHeadRoundActive = true;
  }
  const uint16_t attempt = mqHeadPending & (uint16_t)~mqHeadRoundFailed;
  uint16_t tried = 0;
  const uint16_t left = mqHeadMsg.length() == 0 ? 0 : broadcastNotifyTo(mqHeadMsg, attempt, 1, &tried);
  mqHeadPending = (uint16_t)((mqHeadPending & ~attempt) | left);
  mqHeadRoundFailed |= (uint16_t)(tried & left);
  if (tried & ~left) mqHeadRoundDelivered = true;
  if (mqHeadPending == 0) {
    mqPop();
    mqDoneSettle(key);
    mqHeadKey = "";
    mqHeadRoundActive = false;
    mqHeadMsg = "";
    Serial.println("Відкладене повідомлення надіслано");
    return;
  }
  if ((mqHeadPending & (uint16_t)~mqHeadRoundFailed) != 0) {
    return;
  }
  mqHeadRoundActive = false;
  mqLastTry = millis();
  if (mqHeadRoundDelivered) mqHeadFails = 0;
  const bool telegramReachable = tgLastOkMs != 0 && millis() - tgLastOkMs < 120000UL && !tlsCertFailing;
  if (telegramReachable) mqHeadFails++;
  if (mqHeadFails >= 10) {
    Serial.println("Відкладене повідомлення відхилено Telegram 10 разів - видалено з черги, щоб не блокувати інші");
    logEvent(LC_TG, LS_WARN, "Telegram 10 разів відхилив повідомлення - видалено з черги: " + key.substring(1, 60));
    mqPop();
    mqDoneSettle(key);
    mqHeadKey = "";
    mqHeadMsg = "";
  } else {
    Serial.println("Відкладене повідомлення не надіслано, повтор пізніше");
  }
}

void mqSetHeadPending(const String& item, uint16_t left, uint16_t tried, const String& message) {
  if (mqCount > 0 && mqEventKey(mqItems[0]) == mqEventKey(item)) {
    mqHeadKey = mqEventKey(item);
    mqHeadPending = left;
    mqHeadFails = 0;
    mqHeadMsg = message;
    mqHeadRoundFailed = (uint16_t)(tried & left);
    mqHeadRoundDelivered = (tried & ~left) != 0;
    mqHeadRoundActive = true;
    mqLastTry = millis();
  }
}

void sendOrQueue(const String& item) {
  uint16_t left = RCPT_ALL, tried = 0;
  String msg;
  if (mqCount == 0 && !tgSendAttemptedThisPass && WiFi.status() == WL_CONNECTED && telegramOnline && !powerChangePending() && tgNetAllowed()) {
    msg = mqBuild(item, false);
    left = broadcastNotifyTo(msg, RCPT_ALL, 1, &tried);
    if (left == 0) {
      return;
    }
  }
  mqPush(item);
  if (left != RCPT_ALL) mqSetHeadPending(item, left, tried, msg);
}
