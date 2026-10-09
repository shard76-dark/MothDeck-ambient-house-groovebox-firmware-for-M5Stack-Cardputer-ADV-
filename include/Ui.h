#ifndef Ui_h
#define Ui_h

class BleMidi;

// NVS only. Safe before the display and before BLE init.
void uiLoadPrefs();
void uiBegin();
// Card, kit, and loop scans. Called after the first Play frame is on screen.
// withLoops false skips the stream preload. BLE on cannot spare the windows.
void uiMountStorage(bool withLoops = true);
void uiPoll(BleMidi &ble);
void uiDraw(BleMidi &ble);
const char *uiBleName();
// NVS key bleEn. Missing means off, including a 1.1.1 bleOn value.
bool uiBleEnabled();
bool uiBleWantLoad();

#endif
