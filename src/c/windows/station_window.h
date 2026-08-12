#pragma once

#include <pebble.h>

#define PTV_HEALTH_KEY 0
#define PTV_1_DEST_KEY 2
#define PTV_1_TIME_KEY 3
#define PTV_2_TIME_KEY 4
#define PTV_3_TIME_KEY 5
#define PTV_2_DEST_KEY 6
#define PTV_3_DEST_KEY 7
#define PTV_NEXT_TIME_KEY 8
#define DATA_ACK 10

#define PTV_REQ_STOP_NUMBER 20

void station_window_push(int stop, char *name, int type);