#ifndef diag_ui_h
#define diag_ui_h

void diagUiBegin();
void diagUiLink(bool connected);
void diagUiRates(const char *midiBpm, const char *arrivalBpm, const char *clockRate, const char *mtcRate);
void diagUiTest(const char *testName, const char *hint, int percent);

#endif
