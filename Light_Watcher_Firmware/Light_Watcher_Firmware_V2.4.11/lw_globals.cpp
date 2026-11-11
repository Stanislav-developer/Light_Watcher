#include "lw.h"

String ssid = "";
String password = "";
String botToken = "";
String chatId = "";
String groupId = "";
String shellyIP = "";

const char* ntp1 = "pool.ntp.org";
const char* ntp2 = "time.google.com";
const char* ntp3 = "time.cloudflare.com";

Preferences preferences;
uint8_t outageMarkersDirty = 0;
LWTelegramClient client;
UniversalTelegramBot bot(botToken, client);
WebServer server(80);

bool powerStatus = true;
bool messageFlag = false;
bool lastOutageDetect = false;
int readValue = 0;
int powerOutageCount = 0;

unsigned long powerOffTime = 0;
unsigned long powerOnTime = 0;
time_t powerOffTimestamp = 0;

String currentTZ = "";
bool autoDst = true;
uint16_t shellyTimeoutMs = SHELLY_TIMEOUT_MS;
String powerOffShortTime = "";
String lastOnDurationText = "";
String lastOffDurationText = "";
String powerOnShortTime = "";
unsigned long powerOnSinceMs = 0;
bool powerOnSinceBoot = true;
bool pendingOnMessage = false;

String yasnoCity = "";
String yasnoStreet = "";
String yasnoHouse = "";
String yasnoGroup = "";
String yasnoPendingCity = "";
String yasnoPendingStreet = "";
int32_t yasnoPendingStreetId = -1;

volatile bool yasnoTaskRunning = false;
volatile bool yasnoTaskDone = false;
volatile bool yasnoTaskCancelled = false;

String yasnoTodaySlotsJson = "[]";
String yasnoTomorrowSlotsJson = "[]";
String yasnoTodayDate = "";
String yasnoTomorrowDate = "";
String yasnoTodayStatus = "";
String yasnoTomorrowStatus = "";
String yasnoScheduleUpdatedOn = "";
uint32_t yasnoScheduleFetchedAt = 0;
bool yasnoScheduleValid = false;

uint8_t yasnoPublishCursor = 0;
uint32_t yasnoPublishLastMs = 0;

bool ownerNotifyOn = true;
bool ownerVoltAlertOn = false;
bool ownerVoltInLightOn = false;
bool ownerYasnoPublishOn = false;
bool ownerYasnoEmergencyOn = false;
bool ownerYasnoQueryOn = false;
bool ownerYasnoEventOn = false;
String yasnoTaskErrorCopy = "";
bool telegramOnline = true;
int telegramFailCount = 0;
unsigned long wifiUpSinceMs = 0;
uint32_t wifiDiscBoot = 0;
uint32_t wifiConnBoot = 0;
uint32_t wifiDiscTotal = 0;
uint32_t wifiConnTotal = 0;
String bootRstWhy = "";
uint32_t bootCount = 0;
uint32_t prevAliveEpoch = 0;
unsigned long tgLastOkMs = 0;
float vmLastV = NAN;
unsigned long vmLastVMs = 0;
bool safeMode = false;
uint32_t crashTotal = 0;
bool wdtOn = false;
bool tlsCertFailing = false;
bool ecoActive = false;
bool tgStaleSkip = false;
bool tgInitDone = false;
unsigned long pendingOnSince = 0;
unsigned long powerCandidateSince = 0;
unsigned long loopMaxMs = 0;
bool logDirty = false;
unsigned long logLastSave = 0;
uint32_t logIssues24 = 0;

bool evSaveOk = true;
bool spkSaveOk = true;
bool chatsSaveOk = true;
bool vDaySaveOk = true;
bool logSaveFailed = false;
bool voltSaveFailed = false;
uint32_t nvsFailCount = 0;
String nvsFailKey = "";
unsigned long nvsFailMs = 0;

String nvsLoadNote = "";

bool logUnread = false, evUnread = false, voltUnread = false, spkUnread = false;
bool statsUnread = false, histUnread = false, chatsUnread = false;
AdcWindow adcLast = { 0, 0, 0, 0, 0, 0 };
unsigned long adcWinStart = 0;
bool adcHaveLast = false;
uint8_t adcFlags = 0;
unsigned long adcFlagLogMs[4] = { 0, 0, 0, 0 };

uint32_t bootId = 0;
bool offShortFix = false;
unsigned long offShortFixMs = 0;
bool onShortFix = false;
unsigned long onShortFixMs = 0;

unsigned long shellyLastFailMs = 0;
int mqCount = 0;
unsigned long mqLastTry = 0;

bool mqDirty = false;
unsigned long mqDirtyTryMs = 0;

static_assert(MQ_MAX > PEND_SLOTS, "MQ_MAX must exceed PEND_SLOTS");
int pendSlot = -1;
bool pendSaveFailed = false;
unsigned long pendSaveLastMs = 0;

int mqUnsaved = 0;

bool mqDoneClearFailed = false;
int mqDoneWaitN = 0;

String pendOrderNote;

bool mqLoadLost = false;

bool mqLoadBlocked = false;
unsigned long mqBlockedSince = 0;

uint32_t mqRejected = 0;

bool batteryRecPending = false;
unsigned long batteryRecMs = 0;
bool statsPairDirty = false;
bool statsTxnBlocked = false;

PowerStats stats;
bool statOffInSession = false;
unsigned long statOffStartMs = 0;
bool statOnObserving = false;
unsigned long statOnSegMs = 0;
unsigned long statOnPeriodStartMs = 0;
unsigned long statLastCheckpointMs = 0;
unsigned long statLastTickMs = 0;
bool statLastOffKnown = false;
unsigned long statResetMs = 0;
bool resetPending = false;
String resetPendingChat = "";
String resetPendingUser = "";
String resetPendingCode = "";
unsigned long resetPendingMs = 0;
uint32_t statLastOffDurSec = 0;

bool statOnFixPending = false;
unsigned long statOnFixMs = 0;
uint32_t statOnFixStart = 0;
unsigned long statHistFixMs[HIST_FIX_MAX];
int statHistFixCount = 0;
unsigned long statsBackupMs = 0;

bool statsSaveOk = true;
bool statsBackupOk = true;
bool histSaveOk = true;
String statsLoadNote = "";

bool onRestorePending = false;

ChatRegistry chats;
unsigned long chatLimitNoticeMs = 0;
String botIdStr = "";
bool botCanReadAllGroup = false;

uint32_t grpFailTotal = 0;
String grpFailLast = "";
unsigned long grpFailLastMs = 0;
WebServer pubServer(WEB_PUBLIC_PORT);
bool webStarted = false;

EvLog evLog;
unsigned long evFixMs[EV_FIX_MAX];
unsigned long evAliveLastMs = 0;

int voltLow = VOLT_LOW_DEFAULT;
int voltHigh = VOLT_HIGH_DEFAULT;
int voltStableLow = 220;
int voltStableHigh = 230;
VoltLog voltLog;
SpkLog spkLog;

bool voltDirty = false;
unsigned long voltLastSaveMs = 0;
unsigned long voltLastAttemptMs = 0;
unsigned long voltDirtySinceMs = 0;
unsigned long voltLastPollMs = 0;

unsigned long webVoltRefreshMs = 0;

bool vAlertOn = true;
bool tgSendAttemptedThisPass = false;
int vIntSec = 30;
int vConfirm = 2;
int vPauseMin = 0;
unsigned long vmLastCheckMs = 0;
unsigned long vmLastAlertMs = 0;
bool vDayUnread = false;

bool webDomOn = false;
String webDomain = "";
bool webCfOnly = false;
bool webIpManual = false;
String webIpVal = "";
String pubIpAuto = "";
uint32_t pubIpAt = 0;
unsigned long pubIpLastTry = 0;

uint8_t webCfState = 0;
uint8_t webHttpsState = 0;
uint32_t webCheckAt = 0;
String admUser = "";
bool admHasPass = false;
unsigned long webSessMs[WEB_SESS_MAX];
unsigned long webPubCacheMs = 0;

String menuState = "main";
unsigned long menuRestartMs = 0;
String menuInput = "";
String menuInputBack = "";
String menuPendingLogin = "";
String menuPendingPass = "";
int menuCurrentUserMsg = 0;
unsigned long yasnoPubNextMs[MAX_CHATS + 1];
uint8_t webProbeTls = WEB_TLS_NONE;

unsigned long webTlsRetryMs = 0;
unsigned long webTlsLastMs = 0;
unsigned long tlsCertFailSince = 0;
unsigned long tlsLastFailMs = 0;
unsigned long tlsBackoffMs = 0;
unsigned long tlsLastRemindMs = 0;
uint32_t tlsFailTotal = 0;

bool startMsgPending = false;
String startMsgText = "";
unsigned long ecoNoWifiSince = 0;
unsigned long ecoLastAttempt = 0;
unsigned long ecoWindowStart = 0;
const char* const PEND_KEYS[PEND_SLOTS] = { "mqPend", "mqPend2" };
