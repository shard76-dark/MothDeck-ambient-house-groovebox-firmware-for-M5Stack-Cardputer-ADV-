#ifndef Ui_h
#define Ui_h

class BleMidi;

void uiBegin();
void uiPoll(BleMidi &ble);
void uiDraw(BleMidi &ble);
const char *uiBleName();

#endif
