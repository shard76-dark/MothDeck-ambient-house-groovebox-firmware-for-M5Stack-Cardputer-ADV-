#ifndef diag_ble_h
#define diag_ble_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// BLE MIDI peripheral with the same service, characteristic, advertising
// payload, and Just Works bonding as MothDeck. The device name is MothDiag.

static const char *const kMothDiagName = "MothDiag";
static const char *const kMothDiagVersion = "0.1.6";
static const char kMothDiagImage[] = "MOTHIMG:0.1.6";

struct DiagLinkInfo {
  bool connected;
  bool advertising;
  bool radioOk;
  uint16_t interval;
  uint16_t latency;
  uint16_t timeout;
  uint16_t mtu;
  int rssi;
  bool hasInterval;
  bool hasMtu;
  bool hasRssi;
  uint16_t askedMin;
  uint16_t askedMax;
  uint16_t askedTimeout;
  bool hasAsk;
  uint8_t updateStatus;
  bool hasStatus;
  char source[12];
};

enum DiagLinkKind : uint8_t {
  DIAG_LINK_NONE = 0,
  DIAG_LINK_CONNECT = 1,
  DIAG_LINK_DISCONNECT = 2,
  DIAG_LINK_UPDATE = 3
};

struct DiagLinkEvent {
  DiagLinkKind kind;
  DiagLinkInfo info;
};

struct DiagClockPub {
  uint32_t raw;
  uint32_t dup;
  uint32_t burst;
  uint32_t playSteps;
  uint32_t tempoSteps;
  uint32_t spanMs;
  uint32_t arrivalSteps;
  uint64_t arrivalSpanUs;
  uint32_t packets;
};

bool diagBleBegin();
void diagBleMaintain(uint32_t nowMs);
bool diagBlePopLink(DiagLinkEvent *out);
int diagBlePopPacket(uint8_t *buf, int cap, uint64_t *us);
uint32_t diagBleQueueDrops();
void diagBleClockPub(DiagClockPub *out);
void diagBleRefreshRssi();
const DiagLinkInfo *diagBleInfo();
const char *diagBleFailReason();
void diagBleSetInterval(uint16_t minU, uint16_t maxU, uint32_t delayMs, bool sendNow);
void diagBleIntervalOff();
void diagBleRequestGap();
const char *diagBleProfileName();
bool diagBleStoreProfile(const char *name);

#endif
