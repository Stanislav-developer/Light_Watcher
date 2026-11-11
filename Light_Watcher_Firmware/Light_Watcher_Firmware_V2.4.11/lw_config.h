#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <Preferences.h>
#include <UniversalTelegramBot.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <nvs.h>
#include <esp_err.h>
#include "mbedtls/md.h"
#include <esp_task_wdt.h>
#include <esp_arduino_version.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_flash.h>
#include <esp_rom_crc.h>
#include <nvs_flash.h>

#define readPin 3

#define OUTAGE_MARKER_COUNT_DIRTY 0x01U
#define OUTAGE_MARKER_TIME_DIRTY  0x02U
#define OUTAGE_MARKER_FLAG_DIRTY  0x04U

#define TZ_EUROPE_KYIV "EET-2EEST,M3.5.0/3,M10.5.0/4"
#define EPOCH_MIN_VALID 1700000000UL

#define SHELLY_TIMEOUT_MS 2000
#define SHELLY_BG_TIMEOUT_MS 800
#define TG_HOST "api.telegram.org"
#define TG_OFFLINE_GAP_MS 15000UL
#define TG_ONLINE_GAP_MS 1000UL
#define NET_DNS_WAIT_MS 1500UL
#define SHELLY_PEND_TIMEOUT_MS 1000
#define SHELLY_DNS_WAIT_MS 1000UL
#define SHELLY_RETRY_MS 2000
#define SHELLY_WAIT_MAX_MS 25000
#define RCPT_OWNER 0x8000U
#define RCPT_ALL 0xFFFFU
#define TG_STALE_SEC 60

#define POWER_ON_THRESHOLD 3000
#define POWER_GREY_LOW 500
#define POWER_STABLE_MS 1500
#define POWER_SAMPLE_MS 50
#define POWER_FAULT_WINDOW_MS 10000UL
#define POWER_FAULT_ON_PCT 80
#define POWER_FAULT_OFF_PCT 20
#define POWER_EV_MAX 4

#define LOG_RAM 60
#define LOG_NVS 8
#define LOG_MAGIC 0x4C574C31UL
#define LOG_SAVE_MS 300000UL

#define MQ_MAX 6
#define MQ_SEP '\x1F'

#define MQ_MAGIC 0x4C574D32UL
#define MQ_ITEM_MAX 1500

#define PEND_SLOTS 2

#define MQ_DONE_KEY "mqDone"
#define MQ_DONE_SEP '\x1E'

#define STATS_MAGIC 0x4C575331UL
#define HIST_MAGIC  0x4C574831UL
#define HIST_SIZE 20
#define STATS_CHECKPOINT_MS 600000UL

#define STATS_TXN_MAGIC 0x4C575458UL
#define HIST_FIX_MAX 4

#define STATS_BACKUP_MS 21600000UL

#define MAX_CHATS 10
#define CHATS_MAGIC 0x4C574331UL
#define CHAT_F_LIGHT 0x01
#define CHAT_F_VOLT 0x02
#define CHAT_F_VOLT_IN_LIGHT 0x04
#define CHAT_F_NOTIFY 0x08
#define CHAT_F_VALERT 0x10
#define CHAT_F_YASNO_PUBLISH 0x20
#define CHAT_F_YASNO_EMERGENCY 0x40
#define CHAT_F_YASNO_QUERY 0x80
#define CHAT_P_PENDING 0x01
#define CHAT_LIMIT_NOTICE_MS 600000UL

#define TEXT_BOT_ADDED "👋 <b>Light Watcher підключено</b>\n\nУ цьому чаті надходитимуть сповіщення про стан електромережі."
#define TEXT_BOT_LEFT "👋 <b>Light Watcher відключено</b>\n\nСповіщення про стан електромережі в цьому чаті більше не надходитимуть."

#define WEB_PUBLIC_PORT 8080

#define EV_SIZE 128
#define EV_MAGIC 0x4C574531UL
#define EV_BOOT_ON 1
#define EV_BOOT_OFF 2
#define EV_OFF 3
#define EV_ON 4
#define EV_STOP 5
#define EV_ALIVE_MS 600000UL

#define EV_FIX_MAX 8

#define VOLT_LOW_DEFAULT 207
#define VOLT_HIGH_DEFAULT 253
#define VOLT_SIZE 288
#define VOLT_MAGIC 0x4C575631UL
#define VOLT_POLL_MS 300000UL
#define VOLT_SAVE_MS 600000UL
#define VOLT_RETRY_MS 60000UL
#define VALERT_MAX 8
#define VALERT_EXPIRE_SEC 21600UL
#define SPK_SIZE 40
#define SPK_MAGIC 0x4C575331UL

#define VDAY_MAGIC 0x4C574432UL
#define PUBIP_REFRESH_MS 21600000UL
#define PUBIP_RETRY_MS 600000UL

#define ADM_ITER 10000

#define WEB_SESS_MAX 4
#define WEB_SESS_IDLE_MS 43200000UL

#define WEB_FAIL_SLOTS 8

#define TG_CLEAN_MAGIC 0x4C57434CUL
#define TG_KEEP_MAX 40
#define TG_CLEAN_SPAN 1000
#define TG_CLEAN_BATCH 100
#define TG_CLEAN_SAVE_MS 60000UL

#define YASNO_REFRESH_INTERVAL_MS 300000UL

#define GUIDE_STEPS 6

#define WDT_TIMEOUT_S 60

#define SAFE_MODE_MS 3600000UL
#define CRASH_SEQ_RESET_MS 900000UL

#define TLS_CERT_ERR (-0x2700)
#define TLS_RETRY_MIN_MS 60000UL
#define TLS_RETRY_MAX_MS 900000UL
#define TLS_REMIND_MS 21600000UL

#define ECO_ENTER_MS 60000UL
#define ECO_RETRY_MS 60000UL
#define ECO_WINDOW_MS 15000UL

#define AP_IDLE_RESTART_MS 900000UL

#define NVS_RETRY_MS 300000UL

#define LW_VERSION "2.4.11"
#define OTA_WAIT_MS 600000UL
#define OTA_MIN_SIZE 100000UL
#define OTA_HDR 64
#define OTA_DATA_TIMEOUT_MS 30000UL
#define OTA_TOTAL_TIMEOUT_MS 300000UL
#define OTA_MARK "#LWFW:LightWatcher:ESP32C3:OTA1"
#define OTA_REPORT_RETRY_MS 30000UL
#define OTA_REPORT_TRIES 20
#define OTA_CONFIRM_MS 900000UL
#define OLD_NVS_ADDR 0x310000UL
#define OLD_NVS_SIZE 0x17000UL
#define NVS_PAGE_SIZE 0x1000UL
#define NVS_MIGRATE_TRIES 3
