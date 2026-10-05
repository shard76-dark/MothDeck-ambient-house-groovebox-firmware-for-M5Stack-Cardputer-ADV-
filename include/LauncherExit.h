#ifndef LauncherExit_h
#define LauncherExit_h
#include <stdbool.h>

// True only when an APP_TEST slot holds a valid ESP32-S3 app image
// (magic 0xE9) that is not the image currently running.
bool launcherInstalled();

// Erases otadata and restarts toward Launcher. Does nothing to otadata
// when launcherInstalled() is false.
bool exitToLauncher();

#endif
