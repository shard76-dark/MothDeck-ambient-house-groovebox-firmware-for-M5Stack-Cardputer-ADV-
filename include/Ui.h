#ifndef Ui_h
#define Ui_h

class BleMidi;

// NVS only. Safe before the display and before BLE init.
void uiLoadPrefs();
void uiBegin();
// Card, kit, and loop scans. Called after the first Play frame is on screen.
void uiMountStorage();
void uiPoll(BleMidi &ble);
void uiDraw(BleMidi &ble);
const char *uiBleName();

#endif
