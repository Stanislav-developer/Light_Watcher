#include "lw.h"

static unsigned long otaWaitMs = 0;
static unsigned long otaRestartMs = 0;
static bool otaReportPending = false;
static String otaReportText = "";
static unsigned long otaReportLastTry = 0;
static uint8_t otaReportTries = 0;
static esp_partition_t otaOldNvsPart;

String otaCheckHeader(const uint8_t* b, size_t n) {
  if (n < OTA_HDR) return "файл закороткий";
  if (b[0] != 0xE9) return "це не файл прошивки ESP (.bin)";
  if (b[12] != 0x05 || b[13] != 0x00) return "прошивка зібрана не для ESP32-C3";
  if (!(b[32] == 0x32 && b[33] == 0x54 && b[34] == 0xCD && b[35] == 0xAB)) {
    return "це не файл НАЗВА.ino.bin (схоже на bootloader.bin або merged.bin)";
  }
  return "";
}

size_t otaMarkStep(size_t matched, const uint8_t* data, size_t len) {
  static const char mark[] = OTA_MARK;
  const size_t markLen = sizeof(mark) - 1;
  for (size_t i = 0; i < len && matched < markLen; i++) {
    if (data[i] == (uint8_t)mark[matched]) {
      matched++;
    } else {
      matched = (data[i] == (uint8_t)mark[0]) ? 1 : 0;
    }
  }
  return matched;
}

size_t otaMarkLen() {
  return sizeof(OTA_MARK) - 1;
}

bool nvsPageHeaderOk(const uint8_t* h) {
  const uint32_t state = (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
  if (state != 0xFFFFFFFEUL && state != 0xFFFFFFFCUL) return false;
  if (h[8] != 0xFE && h[8] != 0xFF) return false;
  const uint32_t crc = (uint32_t)h[28] | ((uint32_t)h[29] << 8) | ((uint32_t)h[30] << 16) | ((uint32_t)h[31] << 24);
  return esp_rom_crc32_le(0xFFFFFFFFUL, h + 4, 24) == crc;
}

static bool otaCopyEntry(nvs_handle_t from, nvs_handle_t to, const char* key, nvs_type_t type) {
  switch (type) {
    case NVS_TYPE_U8:  { uint8_t v;  return nvs_get_u8(from, key, &v) == ESP_OK && nvs_set_u8(to, key, v) == ESP_OK; }
    case NVS_TYPE_I8:  { int8_t v;   return nvs_get_i8(from, key, &v) == ESP_OK && nvs_set_i8(to, key, v) == ESP_OK; }
    case NVS_TYPE_U16: { uint16_t v; return nvs_get_u16(from, key, &v) == ESP_OK && nvs_set_u16(to, key, v) == ESP_OK; }
    case NVS_TYPE_I16: { int16_t v;  return nvs_get_i16(from, key, &v) == ESP_OK && nvs_set_i16(to, key, v) == ESP_OK; }
    case NVS_TYPE_U32: { uint32_t v; return nvs_get_u32(from, key, &v) == ESP_OK && nvs_set_u32(to, key, v) == ESP_OK; }
    case NVS_TYPE_I32: { int32_t v;  return nvs_get_i32(from, key, &v) == ESP_OK && nvs_set_i32(to, key, v) == ESP_OK; }
    case NVS_TYPE_U64: { uint64_t v; return nvs_get_u64(from, key, &v) == ESP_OK && nvs_set_u64(to, key, v) == ESP_OK; }
    case NVS_TYPE_I64: { int64_t v;  return nvs_get_i64(from, key, &v) == ESP_OK && nvs_set_i64(to, key, v) == ESP_OK; }
    case NVS_TYPE_STR: {
      size_t len = 0;
      if (nvs_get_str(from, key, NULL, &len) != ESP_OK || len == 0) return false;
      char* buf = (char*)malloc(len);
      bool ok = buf != NULL && nvs_get_str(from, key, buf, &len) == ESP_OK && nvs_set_str(to, key, buf) == ESP_OK;
      free(buf);
      return ok;
    }
    case NVS_TYPE_BLOB: {
      size_t len = 0;
      if (nvs_get_blob(from, key, NULL, &len) != ESP_OK) return false;
      if (len == 0) return nvs_set_blob(to, key, "", 0) == ESP_OK;
      void* buf = malloc(len);
      bool ok = buf != NULL && nvs_get_blob(from, key, buf, &len) == ESP_OK && nvs_set_blob(to, key, buf, len) == ESP_OK;
      free(buf);
      return ok;
    }
    default:
      return false;
  }
}

int otaMigrateNvs() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);
  const esp_partition_t* nvsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
  if (run == NULL || other == NULL || nvsPart == NULL || run->address != 0x10000UL || nvsPart->address == OLD_NVS_ADDR ||
      OLD_NVS_ADDR < other->address || OLD_NVS_ADDR + OLD_NVS_SIZE > other->address + other->size) {
    return 0;
  }

  uint8_t attempt = 0;
  nvs_handle_t h;
  esp_err_t e = nvs_open("light-watcher", NVS_READONLY, &h);
  if (e == ESP_OK) {
    const bool interrupted = nvs_get_u8(h, "migRun", &attempt) == ESP_OK;
    nvs_close(h);
    if (!interrupted) return 0;
    if (attempt >= NVS_MIGRATE_TRIES) {
      if (nvs_open("light-watcher", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "migRun");
        nvs_commit(h);
        nvs_close(h);
      }
      Serial.println("NVS: перенесення не вдалося " + String(attempt) + " рази - припинено");
      return -1;
    }
  } else if (e != ESP_ERR_NVS_NOT_FOUND) {
    return 0;
  }

  bool found = false;
  uint8_t hdr[32];
  for (uint32_t off = 0; off < OLD_NVS_SIZE && !found; off += NVS_PAGE_SIZE) {
    found = esp_flash_read(NULL, hdr, OLD_NVS_ADDR + off, sizeof(hdr)) == ESP_OK && nvsPageHeaderOk(hdr);
  }
  if (!found) return 0;

  memset(&otaOldNvsPart, 0, sizeof(otaOldNvsPart));
  otaOldNvsPart.flash_chip = esp_flash_default_chip;
  otaOldNvsPart.type = ESP_PARTITION_TYPE_DATA;
  otaOldNvsPart.subtype = ESP_PARTITION_SUBTYPE_DATA_NVS;
  otaOldNvsPart.address = OLD_NVS_ADDR;
  otaOldNvsPart.size = OLD_NVS_SIZE;
  otaOldNvsPart.erase_size = NVS_PAGE_SIZE;
  strlcpy(otaOldNvsPart.label, "lwold", sizeof(otaOldNvsPart.label));
  if (nvs_flash_init_partition_ptr(&otaOldNvsPart) != ESP_OK) {
    Serial.println("NVS: старий розділ не читається - перенесення пропущено");
    return 0;
  }
  nvs_handle_t oldH;
  if (nvs_open_from_partition("lwold", "light-watcher", NVS_READONLY, &oldH) != ESP_OK) {
    nvs_flash_deinit_partition("lwold");
    return 0;
  }

  nvs_flash_deinit();
  if (nvs_flash_erase() != ESP_OK || nvs_flash_init() != ESP_OK) {
    nvs_close(oldH);
    nvs_flash_deinit_partition("lwold");
    Serial.println("NVS: новий розділ не вдалося підготувати");
    return -1;
  }
  nvs_handle_t newH;
  if (nvs_open("light-watcher", NVS_READWRITE, &newH) != ESP_OK) {
    nvs_close(oldH);
    nvs_flash_deinit_partition("lwold");
    return -1;
  }
  nvs_set_u8(newH, "migRun", (uint8_t)(attempt + 1));
  nvs_commit(newH);

  int copied = 0;
  int failed = 0;
  nvs_iterator_t it = NULL;
  esp_err_t r = nvs_entry_find("lwold", "light-watcher", NVS_TYPE_ANY, &it);
  while (r == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (strcmp(info.key, "migRun") == 0) {
    } else if (otaCopyEntry(oldH, newH, info.key, info.type)) {
      copied++;
    } else {
      failed++;
      Serial.println(String("NVS: не перенесено ключ ") + info.key);
    }
    r = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  if (failed == 0) nvs_erase_key(newH, "migRun");
  const bool committed = nvs_commit(newH) == ESP_OK;
  nvs_close(newH);
  nvs_close(oldH);
  nvs_flash_deinit_partition("lwold");
  Serial.println("NVS: перенесено записів " + String(copied) + (failed ? ", з помилкою " + String(failed) : String("")));
  return (failed == 0 && committed) ? copied : -1;
}

const esp_partition_t* otaTargetPartition() {
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (next == NULL || run == NULL || next->address == run->address) {
    return nullptr;
  }
  return next;
}

static bool otaFileUrl(const String& fileId, String& url, long& size, String& error) {
  wdtFeed();
  String resp = bot.sendGetToTelegram(bot.buildCommand("getFile?file_id=" + fileId));
  wdtFeed();
  JsonDocument doc;
  if (resp.length() == 0 || deserializeJson(doc, resp)) {
    error = "Telegram не відповів на запит файлу";
    return false;
  }
  if (!doc["ok"].as<bool>()) {
    error = "Telegram не видав файл: " + jsonStr(doc["description"]);
    return false;
  }
  String path = jsonStr(doc["result"]["file_path"]);
  if (path.length() == 0) {
    error = "Telegram не видав шлях до файлу";
    return false;
  }
  if (doc["result"]["file_size"].is<long>()) size = doc["result"]["file_size"].as<long>();
  url = "https://api.telegram.org/file/" + bot.buildCommand(path);
  return true;
}

String otaDownload(const String& url, long expected, const esp_partition_t* part) {
  static uint8_t buf[1024];
  client.stop();
  wdtFeed();
  if (!client.connect(TG_HOST, 443)) {
    return "немає з'єднання з сервером Telegram";
  }
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    client.stop();
    return "не вдалося підготувати запит файлу";
  }
  wdtFeed();
  const int code = http.GET();
  wdtFeed();
  if (code != 200) {
    http.end();
    client.stop();
    return "Telegram не віддав файл (код " + String(code) + ")";
  }
  long len = http.getSize();
  if (len <= 0) len = expected;
  if (len < (long)OTA_MIN_SIZE || (size_t)len > part->size) {
    http.end();
    client.stop();
    return "некоректний розмір файлу (" + String(len) + " Б)";
  }
  NetworkClient* stream = http.getStreamPtr();
  const unsigned long startMs = millis();

  size_t got = 0;
  unsigned long lastData = millis();
  while (got < OTA_HDR && millis() - lastData < OTA_DATA_TIMEOUT_MS) {
    wdtFeed();
    const int a = stream->available();
    if (a > 0) {
      size_t want = OTA_HDR - got;
      if ((size_t)a < want) want = a;
      got += stream->readBytes(buf + got, want);
      lastData = millis();
    } else if (!stream->connected()) {
      break;
    } else {
      delay(2);
    }
  }
  String err = got < OTA_HDR ? String("файл не завантажився (обрив зв'язку)") : otaCheckHeader(buf, got);
  if (err.length() > 0) {
    http.end();
    client.stop();
    return err;
  }

  if (!Update.begin((size_t)len)) {
    err = String("не вдалося підготувати пам'ять: ") + Update.errorString();
    http.end();
    client.stop();
    return err;
  }
  size_t mark = otaMarkStep(0, buf, got);
  size_t total = 0;
  if (Update.write(buf, got) != got) {
    err = String("помилка запису: ") + Update.errorString();
  } else {
    total = got;
  }
  lastData = millis();
  while (err.length() == 0 && total < (size_t)len) {
    wdtFeed();
    if (millis() - startMs > OTA_TOTAL_TIMEOUT_MS) {
      err = "завантаження триває понад 5 хв";
      break;
    }
    const int a = stream->available();
    if (a > 0) {
      size_t want = sizeof(buf);
      if ((size_t)a < want) want = a;
      if (want > (size_t)len - total) want = (size_t)len - total;
      const size_t n = stream->readBytes(buf, want);
      if (n > 0) {
        mark = otaMarkStep(mark, buf, n);
        if (Update.write(buf, n) != n) {
          err = String("помилка запису: ") + Update.errorString();
          break;
        }
        total += n;
        lastData = millis();
      }
    } else {
      if (!stream->connected()) break;
      if (millis() - lastData > OTA_DATA_TIMEOUT_MS) break;
      delay(2);
    }
  }
  http.end();
  client.stop();
  wdtFeed();
  if (err.length() == 0 && total != (size_t)len) {
    err = "файл завантажено не повністю (" + String((unsigned long)(total / 1024)) + " з " + String((unsigned long)(len / 1024)) + " КБ)";
  }
  if (err.length() == 0 && mark < otaMarkLen()) {
    err = "це не прошивка Light Watcher з оновленням через Telegram (V2.4.9 або новіша)";
  }
  if (err.length() > 0) {
    Update.abort();
    return err;
  }
  if (!Update.end(true)) {
    return String("прошивка не пройшла перевірку: ") + Update.errorString();
  }
  return "";
}

static String otaHowToText() {
  return "Arduino IDE: Скетч → Експортувати скомпільований бінарний файл. Потрібен файл "
         "<code>Light_Watcher_Firmware_V….ino.bin</code> з теки <code>build/…</code> "
         "(не bootloader, не partitions і не merged).";
}

void otaStart() {
  if (otaRestartMs != 0) {
    menuSend("⏳ Нову прошивку вже записано, пристрій зараз перезапуститься.", "");
    return;
  }
  const esp_partition_t* part = otaTargetPartition();
  if (part == nullptr) {
    menuSend("⚠️ <b>Оновлення через Telegram недоступне</b>\n\nУ пам'яті пристрою немає другого слота під прошивку "
             "(стара таблиця розділів). Один раз прошийте V" LW_VERSION " по USB без «Erase All Flash» — далі оновлення працюватиме тут.", "HTML");
    return;
  }
  otaWaitMs = millis();
  if (otaWaitMs == 0) otaWaitMs = 1;
  String m = "⬆️ <b>Оновлення прошивки</b>\n\nПоточна версія: V" LW_VERSION "\n\n";
  m += "Надішліть сюди файл прошивки <b>.bin</b> як документ (📎 → Файл).\n";
  m += otaHowToText() + "\n";
  m += "Максимум " + String((unsigned long)(part->size / 1024)) + " КБ.\n\n";
  m += "Чекаю файл 10 хв. Налаштування, статистика й історія збережуться. «❌ Скасувати» — вийти.";
  if (!checkPowerStatus()) {
    m += "\n\n⚠️ Зараз немає світла: пристрій працює від акумулятора. Якщо живлення зникне посеред запису, "
         "лишиться поточна прошивка, але краще оновлювати, коли світло є.";
  }
  menuInputStart("fw", "sys", m);
}

static void otaFinishWait(const String& note) {
  otaWaitMs = 0;
  menuInputReturn(note);
}

void otaInputText(const String& text) {
  if (text == "/update" || text == "⬆️ Оновити прошивку") {
    otaStart();
    return;
  }
  if (otaWaitMs == 0 || millis() - otaWaitMs > OTA_WAIT_MS) {
    otaFinishWait("⌛ Час очікування файлу минув. Натисніть «⬆️ Оновити прошивку» ще раз.");
    return;
  }
  menuSend("📎 Чекаю файл прошивки <b>.bin</b> документом. Щоб вийти, натисніть «❌ Скасувати».", "HTML");
}

void otaHandleDocument(const String& fileId, const String& fileName, long fileSize) {
  if (menuInput != "fw" || otaWaitMs == 0 || millis() - otaWaitMs > OTA_WAIT_MS) {
    if (menuInput == "fw") otaFinishWait("⌛ Час очікування файлу минув.");
    otaWaitMs = 0;
    bot.sendMessage(chatId, "ℹ️ Щоб оновити прошивку, спочатку натисніть «💡 Система → ⬆️ Оновити прошивку» (або /update), а потім надішліть файл.", "HTML");
    return;
  }
  String lower = fileName;
  lower.toLowerCase();
  if (!lower.endsWith(".bin")) {
    menuSend("❌ Потрібен файл <b>.bin</b>. " + otaHowToText() + "\nНадішліть правильний файл або натисніть «❌ Скасувати».", "HTML");
    return;
  }
  if (lower.indexOf("bootloader") >= 0 || lower.indexOf("merged") >= 0 || lower.indexOf("partitions") >= 0) {
    menuSend("❌ Це не той файл: потрібен <code>….ino.bin</code>, а не bootloader, partitions чи merged. Надішліть правильний файл.", "HTML");
    return;
  }
  const esp_partition_t* part = otaTargetPartition();
  if (part == nullptr) {
    otaFinishWait("");
    otaStart();
    return;
  }
  if (fileSize > 0 && ((unsigned long)fileSize < OTA_MIN_SIZE || (unsigned long)fileSize > part->size)) {
    menuSend("❌ Розмір файлу " + String((unsigned long)(fileSize / 1024)) + " КБ не підходить (потрібно від " +
             String((unsigned long)(OTA_MIN_SIZE / 1024)) + " до " + String((unsigned long)(part->size / 1024)) + " КБ).", "");
    return;
  }
  if (yasnoTaskRunning) {
    menuSend("⏳ Зараз виконується запит YASNO. Надішліть файл ще раз за хвилину.", "");
    return;
  }

  otaWaitMs = 0;
  menuInput = "";
  menuInputBack = "";
  bot.sendMessage(chatId, "⬇️ Завантажую <b>" + htmlEsc(fileName) + "</b> (" + String((unsigned long)(fileSize / 1024)) +
                  " КБ). Не вимикайте живлення, зазвичай до 2 хв. Сповіщення про світло на цей час призупинено.", "HTML");
  logEvent(LC_SYS, LS_INFO, "Оновлення прошивки: завантаження " + fileName + " (" + String(fileSize) + " Б)");

  String url;
  long size = fileSize;
  String err;
  if (otaFileUrl(fileId, url, size, err)) {
    err = otaDownload(url, size, part);
  }
  if (err.length() > 0) {
    logEvent(LC_SYS, LS_WARN, "Оновлення прошивки не вдалося: " + err);
    menuOpen("sys", "❌ <b>Оновлення не вдалося</b>: " + htmlEsc(err) +
             "\n\nПоточна прошивка не змінена, пристрій працює як раніше. Спробуйте ще раз: «⬆️ Оновити прошивку».");
    return;
  }

  prefPutStrVerified("otaFrom", LW_VERSION);
  preferences.putUInt("otaDst", part->address);
  logEvent(LC_SYS, LS_INFO, "Оновлення прошивки: файл перевірено, перезапуск у нову прошивку");
  bot.sendMessage(chatId, "✅ Прошивку завантажено й перевірено. Перезапуск…", "HTML");
  otaRestartMs = millis();
  if (otaRestartMs == 0) otaRestartMs = 1;
}

void otaRestartTick() {
  if (otaRestartMs == 0) return;
  if (powerChangePending() && millis() - otaRestartMs < 60000UL) return;
  prepareForRestart("оновлення прошивки через Telegram");
  delay(500);
  ESP.restart();
}

extern "C" bool verifyRollbackLater() {
  return true;
}

void otaConfirmTick() {
  static bool settled = false;
  if (settled) return;
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (run == NULL || esp_ota_get_state_partition(run, &st) != ESP_OK || st != ESP_OTA_IMG_PENDING_VERIFY) {
    settled = true;
    return;
  }
  if (tgLastOkMs != 0) {
    settled = true;
    const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    logEvent(LC_SYS, e == ESP_OK ? LS_INFO : LS_ERR, e == ESP_OK ? String("Нову прошивку підтверджено: зв'язок з Telegram є")
                                                                 : "Нову прошивку не вдалося позначити робочою: " + String(esp_err_to_name(e)));
    return;
  }
  if (WiFi.status() == WL_CONNECTED && wifiUpSinceMs != 0 && millis() - wifiUpSinceMs >= OTA_CONFIRM_MS && timeTrusted()) {
    settled = true;
    logEvent(LC_SYS, LS_ERR, "Нова прошивка 15 хв не має зв'язку з Telegram - повернення до попередньої");
    prepareForRestart("нова прошивка без зв'язку з Telegram - повернення до попередньої");
    delay(500);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

void otaReportBegin() {
  if (!preferences.isKey("otaFrom")) return;
  const String from = preferences.getString("otaFrom", "?");
  const uint32_t dst = preferences.getUInt("otaDst", 0);
  const esp_partition_t* run = esp_ota_get_running_partition();
  const bool ok = run != NULL && run->address == dst;
  if (ok) {
    otaReportText = "✅ <b>Прошивку оновлено через Telegram</b>\nV" + htmlEsc(from) + " → V" LW_VERSION;
    logEvent(LC_SYS, LS_INFO, "Прошивку оновлено: V" + from + " → V" LW_VERSION);
  } else {
    otaReportText = "⚠️ <b>Нова прошивка не запустилася</b>\nПристрій повернувся до попередньої: V" LW_VERSION ".";
    logEvent(LC_SYS, LS_ERR, "Нова прошивка не запустилася - працює попередня V" LW_VERSION);
  }
  otaReportPending = true;
}

void otaReportTick() {
  if (!otaReportPending || tgSendAttemptedThisPass || WiFi.status() != WL_CONNECTED || powerChangePending()) return;
  if (otaReportLastTry != 0 && millis() - otaReportLastTry < OTA_REPORT_RETRY_MS) return;
  if (!tgNetAllowed()) return;
  otaReportLastTry = millis();
  if (otaReportLastTry == 0) otaReportLastTry = 1;
  tgSendAttemptedThisPass = true;
  const bool sent = tgSendOwnerNotice(otaReportText);
  otaReportTries++;
  if (sent || otaReportTries >= OTA_REPORT_TRIES) {
    otaReportPending = false;
    otaReportText = "";
    preferences.remove("otaFrom");
    preferences.remove("otaDst");
  }
}
