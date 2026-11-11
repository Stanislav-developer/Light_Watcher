#pragma once
#include "lw_config.h"

class LWTelegramClient : public WiFiClientSecure {
 public:
  using WiFiClientSecure::connect;
  int connect(const char* host, uint16_t port) override;
};
enum { LC_WIFI = 0, LC_TG, LC_SHELLY, LC_ADC, LC_NTP, LC_WEB, LC_MEM, LC_WDT, LC_STORE, LC_SYS };
enum { LS_INFO = 0, LS_WARN, LS_ERR };

struct LogEntry {
  uint32_t t;
  uint32_t up;
  uint8_t cat;
  uint8_t sev;
  uint8_t prev;
  uint8_t pad;
  char msg[80];
};

struct LogStore {
  uint32_t magic;
  uint16_t head;
  uint16_t count;
  LogEntry e[LOG_NVS];
  uint32_t crc;
};

struct AdcWindow {
  int minV;
  int maxV;
  uint32_t n;
  uint32_t grey;
  double sum;
  double sumSq;
};

struct PowerStats {
  uint32_t magic;
  uint32_t offCount;
  uint32_t exactCount;
  uint32_t totalOffSec;
  uint32_t totalOnSec;
  uint32_t maxOffSec;
  uint32_t minOffSec;
  uint32_t lastOffEpoch;
  uint32_t lastOnEpoch;
  uint32_t curOffStartEpoch;
  uint32_t curOffId;
  uint32_t offOpen;
  uint32_t crc;
};

struct HistRecord {
  uint32_t id;
  uint32_t offEpoch;
  uint32_t onEpoch;
  uint32_t durSec;
};

struct HistBuffer {
  uint32_t magic;
  uint32_t head;
  uint32_t count;
  HistRecord rec[HIST_SIZE];
  uint32_t crc;
};
struct StatsTxn {
  uint32_t magic;
  PowerStats statsData;
  HistBuffer histData;
  uint32_t crc;
};

struct ChatEntry {
  char id[24];
  char title[64];
  uint8_t used;
  uint8_t type;
  uint8_t status;
  uint8_t flags;
  uint8_t canPost;
  uint8_t pad[3];
};

struct ChatRegistry {
  uint32_t magic;
  ChatEntry e[MAX_CHATS];
  uint32_t crc;
};

struct EvLog {
  uint32_t magic;
  uint16_t head;
  uint16_t count;
  uint32_t t[EV_SIZE];
  uint8_t k[EV_SIZE];
  uint32_t crc;
};

struct VoltLog {
  uint32_t magic;
  uint16_t head;
  uint16_t count;
  uint32_t t[VOLT_SIZE];
  uint16_t dv[VOLT_SIZE];
  uint32_t crc;
};

struct SpkLog {
  uint32_t magic;
  uint16_t head;
  uint16_t count;
  uint32_t t[SPK_SIZE];
  uint16_t dv[SPK_SIZE];
  uint16_t lim[SPK_SIZE];
  uint32_t crc;
};
struct VoltDay {
  uint32_t magic;
  uint32_t dayKey;
  uint16_t minDv;
  uint16_t maxDv;
  uint32_t minT;
  uint32_t maxT;
  uint32_t crc;
};
struct TgCleanState {
  uint32_t magic;
  int32_t from;
  int32_t startId;
  uint8_t n;
  uint8_t pad[3];
  int32_t keep[TG_KEEP_MAX];
  uint32_t crc;
};
struct WebFailEntry { uint32_t ip; uint8_t fails; unsigned long lockStart; unsigned long lockDur; unsigned long lastMs; };

class YasnoHttpBodyStream : public Stream {
 public:
  YasnoHttpBodyStream(Stream& source, int32_t contentLength, bool chunked, uint32_t timeoutMs)
      : _source(source), _expected(contentLength), _chunked(chunked),
        _timeoutMs(timeoutMs), _remaining(contentLength > 0 ? (uint32_t)contentLength : 0),
        _chunkRemaining(0), _decoded(0), _finished(false), _failed(false),
        _needChunkCrlf(false), _hasPeek(false), _peekValue(-1), _trailingGarbage(false),
        _startMs(millis()) {}

  int available() override {
    if (_hasPeek) return 1;
    if (_finished || _failed) return 0;
    return 1;
  }

  int read() override {
    if (_hasPeek) {
      _hasPeek = false;
      return _peekValue;
    }
    return readDecodedByte();
  }

  int peek() override {
    if (!_hasPeek) {
      _peekValue = readDecodedByte();
      _hasPeek = _peekValue >= 0;
    }
    return _hasPeek ? _peekValue : -1;
  }

  void flush() override {}
  size_t write(uint8_t) override { return 0; }

  size_t decodedBytes() const { return _decoded; }
  bool failed() const { return _failed; }
  bool complete() const { return _finished && !_failed; }
  bool trailingGarbage() const { return _trailingGarbage; }

  bool drainAndValidate() {
    while (!_finished && !_failed) {
      int ch = readDecodedByte();
      if (ch < 0) break;
      if (!isspace((unsigned char)ch)) _trailingGarbage = true;
    }
    if (_expected >= 0 && _decoded != (size_t)_expected) _failed = true;
    return _finished && !_failed;
  }

 private:
  Stream& _source;
  int32_t _expected;
  bool _chunked;
  uint32_t _timeoutMs;
  uint32_t _remaining;
  uint32_t _chunkRemaining;
  size_t _decoded;
  bool _finished;
  bool _failed;
  bool _needChunkCrlf;
  bool _hasPeek;
  int _peekValue;
  bool _trailingGarbage;
  uint32_t _startMs;
  static const uint32_t kTotalMs = 20000UL;

  int readRawByte() {
    uint32_t started = millis();
    while ((uint32_t)(millis() - started) < _timeoutMs && (uint32_t)(millis() - _startMs) < kTotalMs) {
      int ch = _source.read();
      if (ch >= 0) return ch;
      if (_source.available() > 0) continue;
      delay(1);
    }
    _failed = true;
    return -1;
  }

  bool readCrlf() {
    int a = readRawByte();
    int b = readRawByte();
    if (a != '\r' || b != '\n') {
      _failed = true;
      return false;
    }
    return true;
  }

  bool readChunkLine(char* out, size_t capacity) {
    if (!out || capacity < 2) {
      _failed = true;
      return false;
    }
    size_t n = 0;
    while (n + 1 < capacity) {
      int ch = readRawByte();
      if (ch < 0) return false;
      if (ch == '\r') {
        int lf = readRawByte();
        if (lf != '\n') {
          _failed = true;
          return false;
        }
        out[n] = '\0';
        return true;
      }
      if (ch == '\n') {
        _failed = true;
        return false;
      }
      out[n++] = (char)ch;
    }
    out[n] = '\0';
    _failed = true;
    return false;
  }

  bool beginNextChunk() {
    if (_needChunkCrlf) {
      if (!readCrlf()) return false;
      _needChunkCrlf = false;
    }
    char line[96];
    if (!readChunkLine(line, sizeof(line))) return false;
    char* semi = strchr(line, ';');
    if (semi) *semi = '\0';
    char* lineStart = line;
    while (*lineStart == ' ' || *lineStart == '\t') ++lineStart;
    if (!*lineStart) {
      _failed = true;
      return false;
    }
    char* end = nullptr;
    unsigned long chunkSize = strtoul(lineStart, &end, 16);
    if (end == lineStart || (*end && *end != ' ' && *end != '\t')) {
      _failed = true;
      return false;
    }
    if (chunkSize > 65536UL || _decoded + chunkSize > 65536UL) {
      _failed = true;
      return false;
    }
    if (chunkSize == 0) {
      size_t trailerBytes = 0;
      uint8_t trailerLines = 0;
      while (true) {
        if (!readChunkLine(line, sizeof(line))) return false;
        size_t lineBytes = strlen(line);
        trailerBytes += lineBytes + 2;
        if (trailerBytes > 1024 || trailerLines > 16) {
          _failed = true;
          return false;
        }
        if (line[0] == '\0') break;
        ++trailerLines;
      }
      _finished = true;
      return false;
    }
    _chunkRemaining = (uint32_t)chunkSize;
    return true;
  }

  int readDecodedByte() {
    if (_finished || _failed) return -1;
    if (_chunked) {
      if (_chunkRemaining == 0 && !beginNextChunk()) return -1;
      int ch = readRawByte();
      if (ch < 0) return -1;
      --_chunkRemaining;
      ++_decoded;
      if (_chunkRemaining == 0) _needChunkCrlf = true;
      return ch;
    }
    if (_expected < 0) {
      _failed = true;
      return -1;
    }
    if (_remaining == 0) {
      _finished = true;
      return -1;
    }
    int ch = readRawByte();
    if (ch < 0) return -1;
    --_remaining;
    ++_decoded;
    if (_remaining == 0) _finished = true;
    return ch;
  }
};

enum { WEB_TLS_NONE = 0, WEB_TLS_OK, WEB_TLS_BAD, WEB_TLS_EXPIRED, WEB_TLS_NOTIME, WEB_TLS_NOCONN };
