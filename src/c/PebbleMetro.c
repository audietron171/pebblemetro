#include <pebble.h>
#include "./PebbleMetro.h"
#include "windows/station_window.h"

Window *window;
SimpleMenuLayer *simplemenulayer;

static GRect available_bounds;

static bool loaded = false;
static Window *s_main_window;
static StatusBarLayer *s_status_bar;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_first_menu_items[STOP_COUNT];

// Post strings to log
static void logger(char *message){
  static char s_buff[32];
  snprintf(s_buff, sizeof(s_buff), message);
  APP_LOG(APP_LOG_LEVEL_INFO, "%s", s_buff);
}

// A struct for our specific settings (see main.h)
static ClaySettings settings;

static void load_window_for_stop(int stop_id, char *stop_name) {
  app_message_deregister_callbacks();
  station_window_push(stop_id, stop_name);
}

static void load_window_stop_1() {
  load_window_for_stop(1, settings.STOP_1_NAME);
}
static void load_window_stop_2() {
  load_window_for_stop(2, settings.STOP_2_NAME);
}
static void load_window_stop_3() {
  load_window_for_stop(3, settings.STOP_3_NAME);
}

const char* get_stop_type(int type) {
    switch (type) {
      case 0:
        return "Train";
      case 1:
        return "Tram";
      case 2:
        return "Bus";
      case 3:
        return "V/Line";
      case 4:
        return "Night Bus";
      default:
        return "PTV Stop";
    }
}

static void main_window_load(Window *window) {
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Loading main...");

  char *stop_names[STOP_COUNT] = {
    settings.STOP_1_NAME,
    settings.STOP_2_NAME,
    settings.STOP_3_NAME
  };
  char *stop_types[STOP_COUNT] = {
    settings.STOP_1_TYPE,
    settings.STOP_2_TYPE,
    settings.STOP_3_TYPE
  };
  void (*stop_callbacks[STOP_COUNT])(void) = {
    load_window_stop_1,
    load_window_stop_2,
    load_window_stop_3
  };

  for (int i = 0; i < STOP_COUNT; i++) {
    s_first_menu_items[i] = (SimpleMenuItem) {
      .title = stop_names[i],
      .subtitle = stop_types[i],
      .callback = stop_callbacks[i]
    };
  }

  s_menu_sections[0] = (SimpleMenuSection) {
    .num_items = STOP_COUNT,
    .items = s_first_menu_items,
  };

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  s_status_bar = status_bar_layer_create();
  GRect statusbar_frame = GRect(0, 0, bounds.size.w, STATUS_BAR_LAYER_HEIGHT);
  status_bar_layer_set_colors(s_status_bar, GColorClear, GColorBlack);
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeDotted);
  layer_set_frame(status_bar_layer_get_layer(s_status_bar), statusbar_frame);
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));

  available_bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w, bounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  s_simple_menu_layer = simple_menu_layer_create(available_bounds, window, s_menu_sections, 1, NULL);
  layer_add_child(window_layer, simple_menu_layer_get_layer(s_simple_menu_layer));

  loaded = true;
};

void main_window_unload() {
  status_bar_layer_destroy(s_status_bar);
  simple_menu_layer_destroy(s_simple_menu_layer);
  loaded = false;
};

void draw_home_screen() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
      .load = main_window_load,
      .unload = main_window_unload,
	});
  
  window_stack_push(s_main_window, true);
}

// Initialize the default settings
static void default_settings() {
  char *stop_names[STOP_COUNT] = {
    settings.STOP_1_NAME,
    settings.STOP_2_NAME,
    settings.STOP_3_NAME
  };
  char *stop_types[STOP_COUNT] = {
    settings.STOP_1_TYPE,
    settings.STOP_2_TYPE,
    settings.STOP_3_TYPE
  };
  size_t stop_size = sizeof(settings.STOP_1_NAME);

  for (int i = 0; i < STOP_COUNT; i++) {
    strncpy(stop_names[i], "...", stop_size - 1);
    stop_names[i][stop_size - 1] = '\0';
    strncpy(stop_types[i], get_stop_type(9999), stop_size - 1);
    stop_types[i][stop_size - 1] = '\0';
  }
}

static void load_settings() {
  // Load the default settings
  default_settings();
  // Read settings from persistent storage, if they exist
  persist_read_data(PERSIST_SETTINGS_KEY, &settings, sizeof(settings));
  // Load menu
  draw_home_screen();
}

// Handle settings updates
static void config_load_handler(DictionaryIterator *iter, void *context){
  logger("Got message");

  // Define pointers to the destination arrays based on index
  char *dest_ptrs[] = {
      settings.STOP_1_NAME,
      settings.STOP_2_NAME,
      settings.STOP_3_NAME,
  };
  size_t dest_sizes[] = {
      sizeof(settings.STOP_1_NAME),
      sizeof(settings.STOP_2_NAME),
      sizeof(settings.STOP_3_NAME)
  };
  char *type_ptrs[] = {
      settings.STOP_1_TYPE,
      settings.STOP_2_TYPE,
      settings.STOP_3_TYPE,
  };

  // Setting keys
  int stationNameKeys[STOP_COUNT];
  int stationTypeKeys[STOP_COUNT];
  for (int i = 0; i < STOP_COUNT; i++) {
    stationNameKeys[i] = SETTINGS_STOP_1_NAME + i;
    stationTypeKeys[i] = SETTINGS_STOP_1_TYPE + i;
  }

  // Checking if setting has been updated
  bool changed = false;
  for (int i = 0; i < STOP_COUNT; i++) {
    // Check for updates to station names
    Tuple *station_name_tuple = dict_find(iter, stationNameKeys[i]);
    if (station_name_tuple){
      logger("Found station name setting");

      // Update setting
      char* name = station_name_tuple->value->cstring;
      char* dest_ptr = dest_ptrs[i];
      size_t dest_size = dest_sizes[i];
      strncpy(dest_ptr, name, dest_size - 1);
      dest_ptr[dest_size - 1] = '\0';
      APP_LOG(APP_LOG_LEVEL_INFO, "%s", dest_ptr);

      changed = true;
    }

    // Check for updates to station types
    Tuple *station_type_tuple = dict_find(iter, stationTypeKeys[i]);
    if (station_type_tuple){
      logger("Found station type setting");

      // Update setting
      int type = (int)station_type_tuple->value->int32;
      char* type_ptr = type_ptrs[i];
      size_t type_size = dest_sizes[i];
      strncpy(type_ptr, get_stop_type(type), type_size - 1);
      type_ptr[type_size - 1] = '\0';

      changed = true;
    }
  }

  if (changed){
    persist_write_data(PERSIST_SETTINGS_KEY, &settings, sizeof(settings));
    logger("Updating screen...");
    layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  }
}

static void init(){
  // Open AppMessage connection
  app_message_register_inbox_received(config_load_handler);
  app_message_open(128, 128);

  load_settings();
};

static void deinit(){
  if (s_main_window) {
    window_destroy(s_main_window);
    s_main_window = NULL;
  }
};

int main(void) {
  init();
  app_event_loop();
  deinit();
};
