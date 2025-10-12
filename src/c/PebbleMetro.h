#pragma once
#include <pebble.h>

// MessageKeys
#define STOP_COUNT 3
#define SETTINGS_STOP_1_NAME 31
#define SETTINGS_STOP_2_NAME 32
#define SETTINGS_STOP_3_NAME 33

// Persist Keys
#define PERSIST_SETTINGS_KEY 1
typedef struct ClaySettings {
    char STOP_1_NAME[20];
    char STOP_2_NAME[20];
    char STOP_3_NAME[20];
} ClaySettings;
