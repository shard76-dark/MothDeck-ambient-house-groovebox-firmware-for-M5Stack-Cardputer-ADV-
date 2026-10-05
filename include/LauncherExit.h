#ifndef LauncherExit_h
#define LauncherExit_h
#include <stdbool.h>

// Points the next boot at bmorcelli Launcher and restarts.
// Returns false (and does not restart) when this image is the only
// application on the flash, so a boot-hold cannot reboot-loop.
bool exitToLauncher();

#endif
