#pragma once

#include <stdbool.h>

// Existing, supported JetBrains profiles in the OS's standard directories.
bool tide_jetbrains_have_tide(void);
int tide_jetbrains(const char *root, bool only_updates, int *found);
