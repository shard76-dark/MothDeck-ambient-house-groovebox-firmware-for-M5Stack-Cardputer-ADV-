#ifndef BleAdvert_h
#define BleAdvert_h
#include <stdint.h>

// Legacy advertising and scan-response payloads are 31 bytes. Flags (3) plus
// the complete 128-bit MIDI service UUID (18) leave 10 bytes, which is a
// name of 8 characters. Longer stored names are shortened in the primary
// packet and sent in full in the scan response.
static const int kBleAdvMax = 31;
static const int kBleAdvNameMax = 8;
static const int kBleGapNameMax = 19;

struct BleAdvertPackets {
  uint8_t adv[kBleAdvMax];
  int advLen;
  uint8_t scan[kBleAdvMax];
  int scanLen;
  char advName[kBleAdvNameMax + 1];
  char gapName[kBleGapNameMax + 1];
  bool nameShortened;
};

// name is the stored name. fallback is used when name is null or empty
// (the firmware passes MOTHDECK_BLE_NAME_DEFAULT). A null fallback becomes
// "Mothdeck", so the primary packet always carries a name.
void buildBleMidiAdvert(const char *name, const char *fallback, BleAdvertPackets *out);

#endif
