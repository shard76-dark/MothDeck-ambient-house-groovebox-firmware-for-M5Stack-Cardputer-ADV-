#ifndef WifiFiles_h
#define WifiFiles_h
#include <stdint.h>

// Host-safe path and request checks for the card file page. The radio
// itself lives in WifiPortal.cpp and is not linked into the host tests.

// Join a relative path onto /moth. Rejects absolute paths, "..", empty
// segments, and characters outside the card name set. An empty rel is /moth.
bool wifiMothPath(const char *rel, char *out, int outLen);

struct WifiHead {
  char method[8];
  char target[96];
  int contentLength;  // -1 when the header omits it
};

// First request line plus headers, not the body. False when the request
// line is missing or does not fit.
bool wifiParseHead(const char *head, WifiHead *out);

enum WifiAct : uint8_t { WIFI_ACT_BAD = 0, WIFI_ACT_INDEX = 1, WIFI_ACT_FILE = 2 };

// INDEX is GET /. FILE is GET, PUT, or DELETE under /moth. path is the
// card path for FILE and is left empty for INDEX.
WifiAct wifiClassify(const WifiHead &head, char *path, int pathLen);

#endif
