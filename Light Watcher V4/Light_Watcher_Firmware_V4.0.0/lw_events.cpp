#include "lw.h"

static int evFixIdx[EV_FIX_MAX];
static int evFixCount = 0;
static bool evAliveWritten = false;

void evSave() {
  if (evUnread && !evReread()) {
    evSaveOk = false;
    return;
  }
  evLog.magic = EV_MAGIC;
  evLog.crc = statsCrc32((const uint8_t*)&evLog, sizeof(EvLog) - sizeof(uint32_t));
  evSaveOk = nvsPutVerified("evLogV1", &evLog, sizeof(EvLog));
}
void evLoad() {
  memset(&evLog, 0, sizeof(EvLog));
  uint8_t st = nvsReadState("evLogV1", &evLog, sizeof(EvLog));
  if (st == 0 && !evLogValid(evLog)) {
    st = 4;
  }
  if (st != 0) {
    memset(&evLog, 0, sizeof(EvLog));
    if (st != 1) {
      nvsNoteLoad("Журнал станів", st);
    }
    evUnread = (st == 3);
  }
}

bool evLogValid(const EvLog& l) {
  return l.magic == EV_MAGIC && l.head < EV_SIZE && l.count <= EV_SIZE && l.crc == statsCrc32((const uint8_t*)&l, sizeof(EvLog) - sizeof(uint32_t));
}

bool evReread() {
  EvLog* l = (EvLog*)malloc(sizeof(EvLog));
  if (l == NULL) {
    return false;
  }
  uint8_t st = nvsReadState("evLogV1", l, sizeof(EvLog));
  if (st == 3) {
    free(l);
    return false;
  }
  if (st == 0 && evLogValid(*l)) {
    int start = (evLog.head + EV_SIZE - evLog.count) % EV_SIZE;
    for (int i = 0; i < evLog.count; i++) {
      int from = (start + i) % EV_SIZE;
      int to = l->head;
      l->t[to] = evLog.t[from];
      l->k[to] = evLog.k[from];
      l->head = (l->head + 1) % EV_SIZE;
      if (l->count < EV_SIZE) l->count++;
      for (int f = 0; f < evFixCount; f++) {
        if (evFixIdx[f] == from) evFixIdx[f] = to + EV_SIZE;
      }
    }
    for (int f = 0; f < evFixCount; f++) {
      if (evFixIdx[f] >= EV_SIZE) evFixIdx[f] -= EV_SIZE;
    }
    evLog = *l;
  }
  free(l);
  evUnread = false;
  return true;
}

void evPut(uint8_t kind, uint32_t t, unsigned long fixMs) {
  int idx = evLog.head;
  for (int i = 0; i < evFixCount; i++) {
    if (evFixIdx[i] == idx) {
      for (int j = i; j < evFixCount - 1; j++) {
        evFixIdx[j] = evFixIdx[j + 1];
        evFixMs[j] = evFixMs[j + 1];
      }
      evFixCount--;
      break;
    }
  }
  evLog.t[idx] = t;
  evLog.k[idx] = kind;
  evLog.head = (evLog.head + 1) % EV_SIZE;
  if (evLog.count < EV_SIZE) {
    evLog.count++;
  }
  if (t == 0 && evFixCount < EV_FIX_MAX) {
    evFixIdx[evFixCount] = idx;
    evFixMs[evFixCount] = fixMs;
    evFixCount++;
  }
}

void evAdd(uint8_t kind, unsigned long ms) {
  uint32_t now = statsNowEpoch();
  evPut(kind, statsEdgeEpoch(ms), ms);
  evSave();
  if (now != 0) {
    preferences.putUInt("evAlive", now);
    evAliveLastMs = millis();
    evAliveWritten = true;
  }
}

void evBegin(bool powerNow) {
  evLoad();
  if (evLog.count > 0) {
    uint32_t lastT = evLog.t[(evLog.head + EV_SIZE - 1) % EV_SIZE];
    uint32_t alive = preferences.getUInt("evAlive", 0);
    uint32_t stopT = (alive > lastT) ? alive : lastT;
    uint8_t lastKind = evLog.k[(evLog.head + EV_SIZE - 1) % EV_SIZE];
    if (stopT != 0 && lastKind != EV_STOP) {
      evPut(EV_STOP, stopT, millis());
    }
  }
  evAdd(powerNow ? EV_BOOT_ON : EV_BOOT_OFF, millis());
}

bool evReset(bool powerNow) {
  const bool removed = resetRemoveKeyVerified("evLogV1");
  memset(&evLog, 0, sizeof(EvLog));
  evFixCount = 0;
  evUnread = false;
  evSaveOk = false;
  if (!removed) return false;

  const uint32_t now = statsNowEpoch();
  if (!powerNow && stats.offOpen && stats.curOffStartEpoch >= EPOCH_MIN_VALID) {
    evPut(EV_OFF, stats.curOffStartEpoch, millis());
  }
  evPut(powerNow ? EV_BOOT_ON : EV_BOOT_OFF, now, millis());
  evSave();
  if (now != 0) {
    preferences.putUInt("evAlive", now);
    evAliveLastMs = millis();
    evAliveWritten = true;
  }

  EvLog check;
  const bool readback = nvsReadState("evLogV1", &check, sizeof(check)) == 0 &&
    evLogValid(check) && check.count == evLog.count && check.head == evLog.head &&
    memcmp(&check, &evLog, sizeof(EvLog)) == 0;
  if (!readback) Serial.println("[RESET] evLogV1: single-write read-back failed");
  return evSaveOk && readback;
}

void evTick() {
  static unsigned long last = 0;
  if (millis() - last < 10000UL) {
    return;
  }
  last = millis();
  uint32_t now = statsNowEpoch();
  if (now == 0) {
    return;
  }
  if (evFixCount > 0) {
    for (int i = 0; i < evFixCount; i++) {
      evLog.t[evFixIdx[i]] = now - (millis() - evFixMs[i]) / 1000;
    }
    evFixCount = 0;
    evSave();
  }
  if (!evAliveWritten || millis() - evAliveLastMs >= EV_ALIVE_MS) {
    preferences.putUInt("evAlive", now);
    evAliveLastMs = millis();
    evAliveWritten = true;
  }
}
