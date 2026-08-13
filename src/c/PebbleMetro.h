#pragma once
#include <pebble.h>

// MessageKeys
#define STOP_COUNT 3
#define SETTINGS_STOP_1_NAME 31
#define SETTINGS_STOP_2_NAME 32
#define SETTINGS_STOP_3_NAME 33
#define SETTINGS_STOP_1_TYPE 34
#define SETTINGS_STOP_2_TYPE 35
#define SETTINGS_STOP_3_TYPE 36

// Storage keys
// NOTE:
// - Bump settings key to reset settings
// - Char length needs some padding to avoid 
#define PERSIST_SETTINGS_KEY 3
typedef struct ClaySettings {
    char STOP_1_NAME[25+5];
    int32_t STOP_1_TYPE;
    char STOP_2_NAME[25+5];
    int32_t STOP_2_TYPE;
    char STOP_3_NAME[25+5];
    int32_t STOP_3_TYPE;
} ClaySettings;
