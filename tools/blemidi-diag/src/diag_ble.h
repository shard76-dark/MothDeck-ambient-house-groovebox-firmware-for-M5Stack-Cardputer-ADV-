#ifndef diag_ble_h
#define diag_ble_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// BLE MIDI peripheral with the same service, characteristic, advertising
// payload, and Just Works bonding as MothDeck. The device name is MothDiag.

static const char *const kMothDiagName = "MothDiag";
static const char *const kMothDiagVersion = "0.1.0";

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

bool diagBleBegin();
void diagBleMaintain(uint32_t nowMs);
bool diagBlePopLink(DiagLinkEvent *out);
int diagBlePopPacket(uint8_t *buf, int cap, uint64_t *us);
uint32_t diagBleQueueDrops();
void diagBleRefreshRssi();
const DiagLinkInfo *diagBleInfo();
const char *diagBleFailReason();

#endif
