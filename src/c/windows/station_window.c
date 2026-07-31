#include "station_window.h"

#define PAGE_COUNT 4
#define TITLE_BUFFER_SIZE 32
#define DETAIL_BUFFER_SIZE 64

// Main window
static Window *s_station_window;

// Status bar layers
static StatusBarLayer *s_status_bar;

// Card layers
static TextLayer *s_page_indicator_layer;
static TextLayer *s_title_layer;
static TextLayer *s_subtitle_layer;
static TextLayer *s_body_layer;
static TextLayer *s_footer_layer;

// Passed by main to determine stop data to poll
static int stopNumber;
static char *stopName;

// Page content
static int s_current_page = 0;
static char s_next_departure_time[TITLE_BUFFER_SIZE];
static char s_departure_times[3][TITLE_BUFFER_SIZE];
static char s_departure_destinations[3][TITLE_BUFFER_SIZE];
static char s_page_indicator_text[8];
static char s_page_title[TITLE_BUFFER_SIZE];
static char s_page_subtitle[TITLE_BUFFER_SIZE];
static char s_page_body[DETAIL_BUFFER_SIZE];
static char s_page_footer[DETAIL_BUFFER_SIZE];

// Sync for JS communication
static AppSync s_sync;
static uint8_t s_sync_buffer[200];

// Refresh timer (ms)
static uint32_t refresh_timeout_ms = 10 * 1000;
static AppTimer *refresh_timer_handle;

static const GColor s_page_background_colors[PAGE_COUNT] = {
  GColorBlueMoon,
  GColorDarkCandyAppleRed,
  GColorIslamicGreen,
  GColorPictonBlue
};

// Post strings to log
static void logger(char *message) {
  static char s_buff[32];
  snprintf(s_buff, sizeof(s_buff), "%s", message);
  APP_LOG(APP_LOG_LEVEL_INFO, "%s", s_buff);
}

static void copy_value(char *destination, size_t size, const char *value) {
  if (!value || !strlen(value)) {
    snprintf(destination, size, "...");
    return;
  }

  strncpy(destination, value, size - 1);
  destination[size - 1] = '\0';
}

static void reset_departure_data() {
  copy_value(s_next_departure_time, sizeof(s_next_departure_time), "...");
  for (int i = 0; i < 3; i++) {
    copy_value(s_departure_times[i], sizeof(s_departure_times[i]), "...");
    copy_value(s_departure_destinations[i], sizeof(s_departure_destinations[i]), "...");
  }
}

static void apply_page_colors() {
  GColor background = PBL_IF_COLOR_ELSE(s_page_background_colors[s_current_page], GColorWhite);
  GColor foreground = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
  GColor secondary = PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack);

  window_set_background_color(s_station_window, background);
  status_bar_layer_set_colors(s_status_bar, background, foreground);

  text_layer_set_text_color(s_page_indicator_layer, secondary);
  text_layer_set_background_color(s_page_indicator_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, foreground);
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_subtitle_layer, secondary);
  text_layer_set_background_color(s_subtitle_layer, GColorClear);
  text_layer_set_text_color(s_body_layer, foreground);
  text_layer_set_background_color(s_body_layer, GColorClear);
  text_layer_set_text_color(s_footer_layer, secondary);
  text_layer_set_background_color(s_footer_layer, GColorClear);
}

static void update_card_page() {
  snprintf(s_page_indicator_text, sizeof(s_page_indicator_text), "%d/%d", s_current_page + 1, PAGE_COUNT);

  switch (s_current_page) {
    case 0:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      copy_value(s_page_subtitle, sizeof(s_page_subtitle), "Next departure");
      copy_value(s_page_body, sizeof(s_page_body), s_next_departure_time);
      snprintf(s_page_footer, sizeof(s_page_footer), "%s  %s\nDown for more",
               s_departure_times[0], s_departure_destinations[0]);
      break;
    case 1:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      copy_value(s_page_subtitle, sizeof(s_page_subtitle), "Departure 1");
      copy_value(s_page_body, sizeof(s_page_body), s_departure_times[0]);
      copy_value(s_page_footer, sizeof(s_page_footer), s_departure_destinations[0]);
      break;
    case 2:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      copy_value(s_page_subtitle, sizeof(s_page_subtitle), "Departure 2");
      copy_value(s_page_body, sizeof(s_page_body), s_departure_times[1]);
      copy_value(s_page_footer, sizeof(s_page_footer), s_departure_destinations[1]);
      break;
    case 3:
    default:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      copy_value(s_page_subtitle, sizeof(s_page_subtitle), "Departure 3");
      copy_value(s_page_body, sizeof(s_page_body), s_departure_times[2]);
      copy_value(s_page_footer, sizeof(s_page_footer), s_departure_destinations[2]);
      break;
  }

  apply_page_colors();
  text_layer_set_text(s_page_indicator_layer, s_page_indicator_text);
  text_layer_set_text(s_title_layer, s_page_title);
  text_layer_set_text(s_subtitle_layer, s_page_subtitle);
  text_layer_set_text(s_body_layer, s_page_body);
  text_layer_set_text(s_footer_layer, s_page_footer);
}

static void change_page(int delta) {
  int next_page = s_current_page + delta;
  if (next_page < 0 || next_page >= PAGE_COUNT) {
    return;
  }

  s_current_page = next_page;
  update_card_page();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  change_page(-1);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  change_page(1);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

// Send acknowledgement for a JS message and request more data
static void send_data_ack() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  if (!iter) {
    logger("Outbox failed");
    return;
  }

  // "0" is for acks sent from watch, "1" is from phone
  int value = 0;
  dict_write_int(iter, DATA_ACK, &value, sizeof(int), true);
  dict_write_end(iter);
  app_message_outbox_send();
}

// Called on all dictionary changes (and will update all layers)
static void sync_tuple_changed_callback(const uint32_t key,
                                        const Tuple *new_tuple,
                                        const Tuple *old_tuple,
                                        void *context) {
  logger("Dictionary update");
  switch (key) {
    case PTV_1_DEST_KEY:
      copy_value(s_departure_destinations[0], sizeof(s_departure_destinations[0]), new_tuple->value->cstring);
      break;
    case PTV_1_TIME_KEY:
      copy_value(s_departure_times[0], sizeof(s_departure_times[0]), new_tuple->value->cstring);
      break;
    case PTV_2_DEST_KEY:
      copy_value(s_departure_destinations[1], sizeof(s_departure_destinations[1]), new_tuple->value->cstring);
      break;
    case PTV_2_TIME_KEY:
      copy_value(s_departure_times[1], sizeof(s_departure_times[1]), new_tuple->value->cstring);
      break;
    case PTV_3_DEST_KEY:
      copy_value(s_departure_destinations[2], sizeof(s_departure_destinations[2]), new_tuple->value->cstring);
      break;
    case PTV_3_TIME_KEY:
      copy_value(s_departure_times[2], sizeof(s_departure_times[2]), new_tuple->value->cstring);
      break;
    case PTV_NEXT_TIME_KEY:
      copy_value(s_next_departure_time, sizeof(s_next_departure_time), new_tuple->value->cstring);
      break;
    case DATA_ACK:
      // Value of "1" means phone sent data and has more, request it
      if (new_tuple->value->int32 == 1) {
        // Wait 100ms to increase reliability (TO-DO: Add timer function)
        psleep(100);
        send_data_ack();
      }
      break;
  }

  update_card_page();
}

static void sync_error_callback(DictionaryResult dict_error,
                                AppMessageResult app_message_error,
                                void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %d", app_message_error);
}

// Request stop data for stop
static void request_stop_info() {
  logger("Requesting data");
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  // Request data for saved slot when ready
  if (iter) {
    dict_write_int(iter, PTV_REQ_STOP_NUMBER, &stopNumber, sizeof(int32_t), false);
    dict_write_end(iter);
    app_message_outbox_send();
  }

  // Refresh data automatically
  refresh_timer_handle = app_timer_register(refresh_timeout_ms, request_stop_info, NULL);
}

// Unload window and return to stop selection
void station_window_unload(Window *window) {
  app_sync_deinit(&s_sync);

  text_layer_destroy(s_page_indicator_layer);
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_subtitle_layer);
  text_layer_destroy(s_body_layer);
  text_layer_destroy(s_footer_layer);

  status_bar_layer_destroy(s_status_bar);

  if (refresh_timer_handle) {
    app_timer_cancel(refresh_timer_handle);
    refresh_timer_handle = NULL;
  }

  window_destroy(window);
  s_station_window = NULL;
}

// Load stop window
void station_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  GRect available_bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w, bounds.size.h - STATUS_BAR_LAYER_HEIGHT);

  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeDotted);
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));

  s_page_indicator_layer = text_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, available_bounds.size.w - 8, 18));
  text_layer_set_font(s_page_indicator_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_page_indicator_layer, GTextAlignmentRight);
  layer_add_child(window_layer, text_layer_get_layer(s_page_indicator_layer));

  s_title_layer = text_layer_create(GRect(8, STATUS_BAR_LAYER_HEIGHT + 18, available_bounds.size.w - 16, 30));
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_title_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  s_subtitle_layer = text_layer_create(GRect(8, STATUS_BAR_LAYER_HEIGHT + 48, available_bounds.size.w - 16, 24));
  text_layer_set_font(s_subtitle_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_overflow_mode(s_subtitle_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_subtitle_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_subtitle_layer));

  s_body_layer = text_layer_create(GRect(8, STATUS_BAR_LAYER_HEIGHT + 72, available_bounds.size.w - 16, 46));
  text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_body_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeTrailingEllipsis);
  layer_add_child(window_layer, text_layer_get_layer(s_body_layer));

  s_footer_layer = text_layer_create(GRect(8, STATUS_BAR_LAYER_HEIGHT + 122, available_bounds.size.w - 16, 30));
  text_layer_set_font(s_footer_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_footer_layer, GTextAlignmentLeft);
  text_layer_set_overflow_mode(s_footer_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_footer_layer));

  reset_departure_data();
  s_current_page = 0;
  update_card_page();

  Tuplet initial_values[] = {
    TupletCString(PTV_1_DEST_KEY, "..."),
    TupletCString(PTV_2_DEST_KEY, "..."),
    TupletCString(PTV_3_DEST_KEY, "..."),
    TupletCString(PTV_1_TIME_KEY, "..."),
    TupletCString(PTV_2_TIME_KEY, "..."),
    TupletCString(PTV_3_TIME_KEY, "..."),
    TupletCString(PTV_NEXT_TIME_KEY, "..."),
    TupletInteger(DATA_ACK, 0)
  };

  app_sync_init(&s_sync, s_sync_buffer, sizeof(s_sync_buffer),
                initial_values, ARRAY_LENGTH(initial_values),
                sync_tuple_changed_callback, sync_error_callback, NULL);

  // Start requesting data
  refresh_timer_handle = app_timer_register(10, request_stop_info, NULL);
}

// Store stop number and load station window
void station_window_push(int stop, char *name) {
  logger("Starting station window");
  stopNumber = stop;
  stopName = name;

  if (!s_station_window) {
    s_station_window = window_create();
    window_set_click_config_provider(s_station_window, click_config_provider);
    window_set_window_handlers(s_station_window, (WindowHandlers) {
      .load = station_window_load,
      .unload = station_window_unload,
    });
  }

  window_stack_push(s_station_window, true);
}
