#ifndef WifiPortal_h
#define WifiPortal_h

// SoftAP file page for /moth. Starting it does not stop BLE, free loop
// windows, or unload a kit. If the radio cannot start, the call returns
// false and the Wi-Fi radio is left off.
bool wifiPortalToggle();
bool wifiPortalUp();
const char *wifiPortalStatus();
void wifiPortalPoll();

#endif
