#ifndef diag_ui_h
#define diag_ui_h

void diagUiBegin();
void diagUiLink(bool connected);
void diagUiBpm(const char *bpm);
void diagUiTest(const char *testName, const char *hint, int percent);

#endif
