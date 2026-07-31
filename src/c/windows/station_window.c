#include "station_window.h"

#define PAGE_COUNT 4
#define TITLE_BUFFER_SIZE 32
#define SUBTITLE_BUFFER_SIZE 48
#define DETAIL_BUFFER_SIZE 64
#define ICON_FRAME_SIZE 56
#define ICON_ANIMATION_FRAMES 8

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
static Layer *s_icon_layer;

// Passed by main to determine stop data to poll
static int stopNumber;
static char *stopName;
static char *stopType;

// Page content
static int s_current_page = 0;
static char s_next_departure_time[TITLE_BUFFER_SIZE];
static char s_departure_times[3][TITLE_BUFFER_SIZE];
static char s_departure_destinations[3][TITLE_BUFFER_SIZE];
static char s_page_indicator_text[8];
static char s_page_title[TITLE_BUFFER_SIZE];
static char s_page_subtitle[SUBTITLE_BUFFER_SIZE];
static char s_page_body[DETAIL_BUFFER_SIZE];
static char s_page_footer[DETAIL_BUFFER_SIZE];

// Sync for JS communication
static AppSync s_sync;
static uint8_t s_sync_buffer[200];

// Refresh timer (ms)
static uint32_t refresh_timeout_ms = 10 * 1000;
static AppTimer *refresh_timer_handle;
static AppTimer *icon_timer_handle;
static int s_icon_animation_frame = 0;

static const GColor s_page_background_colors[PAGE_COUNT] = {
  GColorBlueMoon,
  GColorDarkCandyAppleRed,
  GColorIslamicGreen,
  GColorPictonBlue
};

typedef enum {
  StationIconTypeDefault,
  StationIconTypeTrain,
  StationIconTypeTram,
  StationIconTypeBus,
  StationIconTypeVLine,
  StationIconTypeNightBus,
} StationIconType;

static StationIconType s_icon_type = StationIconTypeDefault;

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

static StationIconType get_station_icon_type() {
  if (!stopType) {
    return StationIconTypeDefault;
  }

  if (strcmp(stopType, "Train") == 0) {
    return StationIconTypeTrain;
  } else if (strcmp(stopType, "Tram") == 0) {
    return StationIconTypeTram;
  } else if (strcmp(stopType, "Bus") == 0) {
    return StationIconTypeBus;
  } else if (strcmp(stopType, "V/Line") == 0) {
    return StationIconTypeVLine;
  } else if (strcmp(stopType, "Night Bus") == 0) {
    return StationIconTypeNightBus;
  }

  return StationIconTypeDefault;
}

static void draw_train_icon(GContext *ctx, GRect bounds, GColor foreground) {
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 6, bounds.origin.y + 6, 44, 32), 7, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorClear);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 12, bounds.origin.y + 12, 10, 10), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 26, bounds.origin.y + 12, 10, 10), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 23, bounds.origin.y + 25, 10, 7), 2, GCornersAll);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 18, bounds.origin.y + 38, 4, 8), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 34, bounds.origin.y + 38, 4, 8), 0, GCornerNone);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 16, bounds.origin.y + 46), 4);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 40, bounds.origin.y + 46), 4);
}

static void draw_tram_icon(GContext *ctx, GRect bounds, GColor foreground) {
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 7, bounds.origin.y + 10, 42, 28), 6, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorClear);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 12, bounds.origin.y + 14, 8, 10), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 23, bounds.origin.y + 14, 8, 10), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 34, bounds.origin.y + 14, 8, 10), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 18, bounds.origin.y + 42), 4);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 38, bounds.origin.y + 42), 4);
  graphics_context_set_stroke_color(ctx, foreground);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(bounds.origin.x + 28, bounds.origin.y + 4), GPoint(bounds.origin.x + 20, bounds.origin.y + 10));
  graphics_draw_line(ctx, GPoint(bounds.origin.x + 28, bounds.origin.y + 4), GPoint(bounds.origin.x + 36, bounds.origin.y + 10));
}

static void draw_bus_icon(GContext *ctx, GRect bounds, GColor foreground) {
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 4, bounds.origin.y + 10, 48, 26), 6, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorClear);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 9, bounds.origin.y + 14, 30, 9), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 41, bounds.origin.y + 14, 7, 16), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 16, bounds.origin.y + 40), 5);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 40, bounds.origin.y + 40), 5);
}

static void draw_vline_icon(GContext *ctx, GRect bounds, GColor foreground, GColor accent) {
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 8, bounds.origin.y + 10, 40, 26), 6, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorClear);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 17, bounds.origin.y + 14, 22, 8), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, accent);
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(bounds.origin.x + 15, bounds.origin.y + 27), GPoint(bounds.origin.x + 23, bounds.origin.y + 18));
  graphics_draw_line(ctx, GPoint(bounds.origin.x + 23, bounds.origin.y + 18), GPoint(bounds.origin.x + 31, bounds.origin.y + 27));
  graphics_draw_line(ctx, GPoint(bounds.origin.x + 31, bounds.origin.y + 27), GPoint(bounds.origin.x + 39, bounds.origin.y + 18));
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 18, bounds.origin.y + 41), 4);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 38, bounds.origin.y + 41), 4);
}

static void draw_night_bus_icon(GContext *ctx, GRect bounds, GColor foreground, GColor background) {
  draw_bus_icon(ctx, bounds, foreground);
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 42, bounds.origin.y + 10), 7);
  graphics_context_set_fill_color(ctx, background);
  graphics_fill_circle(ctx, GPoint(bounds.origin.x + 45, bounds.origin.y + 10), 6);
}

static void draw_default_icon(GContext *ctx, GRect bounds, GColor foreground, GColor background) {
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 14, bounds.origin.y + 10, 28, 26), 4, GCornersAll);
  graphics_context_set_fill_color(ctx, background);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 19, bounds.origin.y + 15, 18, 8), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, foreground);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 26, bounds.origin.y + 36, 4, 12), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bounds.origin.x + 18, bounds.origin.y + 48, 20, 3), 0, GCornerNone);
}

static void icon_layer_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  const int8_t bob_offsets[ICON_ANIMATION_FRAMES] = { 0, -2, -3, -2, 0, 2, 3, 2 };
  const int8_t sway_offsets[ICON_ANIMATION_FRAMES] = { 0, 1, 2, 1, 0, -1, -2, -1 };
  GColor background = PBL_IF_COLOR_ELSE(s_page_background_colors[s_current_page], GColorWhite);
  GColor foreground = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
  GRect shifted_bounds = bounds;

  shifted_bounds.origin.y += bob_offsets[s_icon_animation_frame];
  shifted_bounds.origin.x += sway_offsets[s_icon_animation_frame];
  graphics_context_set_stroke_color(ctx, foreground);
  graphics_context_set_fill_color(ctx, foreground);
  graphics_context_set_stroke_width(ctx, 2);

  switch (s_icon_type) {
    case StationIconTypeTrain:
      draw_train_icon(ctx, shifted_bounds, foreground);
      break;
    case StationIconTypeTram:
      draw_tram_icon(ctx, shifted_bounds, foreground);
      break;
    case StationIconTypeBus:
      draw_bus_icon(ctx, shifted_bounds, foreground);
      break;
    case StationIconTypeVLine:
      draw_vline_icon(ctx, shifted_bounds, foreground, background);
      break;
    case StationIconTypeNightBus:
      draw_night_bus_icon(ctx, shifted_bounds, foreground, background);
      break;
    case StationIconTypeDefault:
    default:
      draw_default_icon(ctx, shifted_bounds, foreground, background);
      break;
  }
}

static void icon_animation_timer_callback(void *context) {
  s_icon_animation_frame = (s_icon_animation_frame + 1) % ICON_ANIMATION_FRAMES;

  if (s_icon_layer) {
    layer_mark_dirty(s_icon_layer);
  }

  icon_timer_handle = app_timer_register(120, icon_animation_timer_callback, NULL);
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
  const char *type_label = stopType ? stopType : "PTV Stop";
  snprintf(s_page_indicator_text, sizeof(s_page_indicator_text), "%d/%d", s_current_page + 1, PAGE_COUNT);

  switch (s_current_page) {
    case 0:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      snprintf(s_page_subtitle, sizeof(s_page_subtitle), "%s - Next departure", type_label);
      copy_value(s_page_body, sizeof(s_page_body), s_next_departure_time);
      snprintf(s_page_footer, sizeof(s_page_footer), "%s  %s\nDown for more",
               s_departure_times[0], s_departure_destinations[0]);
      break;
    case 1:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      snprintf(s_page_subtitle, sizeof(s_page_subtitle), "%s - Departure 1", type_label);
      copy_value(s_page_body, sizeof(s_page_body), s_departure_times[0]);
      copy_value(s_page_footer, sizeof(s_page_footer), s_departure_destinations[0]);
      break;
    case 2:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      snprintf(s_page_subtitle, sizeof(s_page_subtitle), "%s - Departure 2", type_label);
      copy_value(s_page_body, sizeof(s_page_body), s_departure_times[1]);
      copy_value(s_page_footer, sizeof(s_page_footer), s_departure_destinations[1]);
      break;
    case 3:
    default:
      copy_value(s_page_title, sizeof(s_page_title), stopName);
      snprintf(s_page_subtitle, sizeof(s_page_subtitle), "%s - Departure 3", type_label);
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
  if (s_icon_layer) {
    layer_mark_dirty(s_icon_layer);
  }
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
  layer_destroy(s_icon_layer);

  status_bar_layer_destroy(s_status_bar);

  if (refresh_timer_handle) {
    app_timer_cancel(refresh_timer_handle);
    refresh_timer_handle = NULL;
  }

  if (icon_timer_handle) {
    app_timer_cancel(icon_timer_handle);
    icon_timer_handle = NULL;
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

  s_icon_layer = layer_create(GRect(available_bounds.size.w - ICON_FRAME_SIZE - 8,
                                    STATUS_BAR_LAYER_HEIGHT + 58,
                                    ICON_FRAME_SIZE,
                                    ICON_FRAME_SIZE));
  layer_set_update_proc(s_icon_layer, icon_layer_update_proc);
  layer_add_child(window_layer, s_icon_layer);

  s_body_layer = text_layer_create(GRect(8, STATUS_BAR_LAYER_HEIGHT + 74, available_bounds.size.w - ICON_FRAME_SIZE - 20, 46));
  text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_body_layer, GTextAlignmentLeft);
  text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeTrailingEllipsis);
  layer_add_child(window_layer, text_layer_get_layer(s_body_layer));

  s_footer_layer = text_layer_create(GRect(8, STATUS_BAR_LAYER_HEIGHT + 122, available_bounds.size.w - 16, 30));
  text_layer_set_font(s_footer_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_footer_layer, GTextAlignmentLeft);
  text_layer_set_overflow_mode(s_footer_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_footer_layer));

  reset_departure_data();
  s_current_page = 0;
  s_icon_type = get_station_icon_type();
  s_icon_animation_frame = 0;
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
  icon_timer_handle = app_timer_register(120, icon_animation_timer_callback, NULL);
}

// Store stop number and load station window
void station_window_push(int stop, char *name, char *type) {
  logger("Starting station window");
  stopNumber = stop;
  stopName = name;
  stopType = type;

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
