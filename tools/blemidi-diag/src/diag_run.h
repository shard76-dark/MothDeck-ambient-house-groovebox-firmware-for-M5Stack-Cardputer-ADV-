#ifndef diag_run_h
#define diag_run_h

#include <stdint.h>
#include <stdbool.h>

void diagRunStart();
bool diagRunGo();
void diagRunStop();
void diagRunPoll(uint32_t nowMs);
bool diagRunActive();
bool diagRunWaiting();

#endif
