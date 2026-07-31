#include "station_window.h"

// Main window
static Window *s_station_window;

// Status bar layers
static StatusBarLayer *s_status_bar;

// Drawing layers
static Layer *s_canvas_layer;
static Layer *s_canvas_layer2;

// Data text layers
static TextLayer *s_first_stop_dest_layer;
static TextLayer *s_second_stop_dest_layer;
static TextLayer *s_third_stop_dest_layer;
static TextLayer *s_first_stop_time_layer;
static TextLayer *s_second_stop_time_layer;
static TextLayer *s_third_stop_time_layer;
static TextLayer *s_station_name_layer;
static TextLayer *s_next_stop_time_layer;

// Passed by main to determine stop data to poll
static int stopNumber;
static char *stopName;
static int stopType;

static int s_content_inset;
static int s_header_height;
static int s_first_divider_y;
static int s_next_departures_divider_y;
static int s_next_departure_box_y;
static int s_next_departure_box_x;
static int s_next_departure_box_width;
static int s_next_departure_box_height;
static int s_next_stop_time_height;
static int s_third_departures_divider_y;
static int s_first_departure_row_y;
static int s_second_departure_row_y;
static int s_third_departure_row_y;
static int s_departure_row_height;
static int s_time_column_width;

// Sync for JS communication
static AppSync s_sync;
static uint8_t s_sync_buffer[200];

// Refresh timer (ms)
static uint32_t refresh_timeout_ms = 10*1000;
static AppTimer *refresh_timer_handle;

// Post strings to log
static void logger(char *message){
  static char s_buff[32];
  snprintf(s_buff, sizeof(s_buff), message);
  APP_LOG(APP_LOG_LEVEL_INFO, "%s", s_buff);
}

static GColor get_header_background_color() {
#if defined(PBL_COLOR)
  switch (stopType) {
    case 0:
      return GColorFromHEX(0x0055FF);
    case 1:
      return GColorFromHEX(0x00AA55);
    case 2:
    case 4:
      return GColorFromHEX(0xFF6600);
    case 3:
      return GColorFromHEX(0xAA00AA);
    default:
      return GColorBlack;
  }
#else
  return GColorBlack;
#endif
}

static void update_layout_metrics(GRect bounds) {
  const bool is_round = PBL_IF_ROUND_ELSE(true, false);
  const int base_height = is_round ? 180 : 168;
  const int round_offset = is_round ? 6 : 0;
  int extra_height = bounds.size.h - base_height;

  if (extra_height < 0) {
    extra_height = 0;
  }

  s_content_inset = is_round ? 10 : 0;
  if (bounds.size.w >= 200) {
    s_content_inset += 2;
  }

  s_header_height = bounds.size.h >= 200 ? 38 : 32;
  s_departure_row_height = bounds.size.h >= 200 ? 24 : 20;
  s_time_column_width = PBL_IF_ROUND_ELSE(44, 42) + (bounds.size.w >= 200 ? 6 : 0);
  s_next_departure_box_width = bounds.size.w >= 200 ? PBL_IF_ROUND_ELSE(88, 78) : PBL_IF_ROUND_ELSE(80, 62);
  s_next_departure_box_height = bounds.size.h >= 200 ? 42 : 35;
  s_next_departure_box_x = (bounds.size.w - s_next_departure_box_width) / 2;
  s_next_stop_time_height = bounds.size.h >= 200 ? 28 : 24;

  const int vertical_step = extra_height / 6;
  s_first_departure_row_y = STATUS_BAR_LAYER_HEIGHT + 34 + round_offset + (vertical_step * 2);
  s_first_divider_y = s_first_departure_row_y + s_departure_row_height - 2;
  s_next_departure_box_y = s_first_divider_y + 10 + vertical_step;
  s_next_departures_divider_y = s_next_departure_box_y + s_next_departure_box_height + 7 + vertical_step;
  s_second_departure_row_y = s_next_departures_divider_y + 4;
  s_third_departures_divider_y = s_second_departure_row_y + s_departure_row_height;
  s_third_departure_row_y = s_third_departures_divider_y + 2;
}

// Send acknowledgement for a JS message and request more data
static void send_data_ack(){
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
      text_layer_set_text(s_first_stop_dest_layer, new_tuple->value->cstring);
      break;
    case PTV_1_TIME_KEY:
      text_layer_set_text(s_first_stop_time_layer, new_tuple->value->cstring);
      break;
    case PTV_2_DEST_KEY:
      text_layer_set_text(s_second_stop_dest_layer, new_tuple->value->cstring);
      break; 
    case PTV_2_TIME_KEY:
      text_layer_set_text(s_second_stop_time_layer, new_tuple->value->cstring);
      break;
    case PTV_3_DEST_KEY:
      text_layer_set_text(s_third_stop_dest_layer, new_tuple->value->cstring);
      break; 
    case PTV_3_TIME_KEY:
      text_layer_set_text(s_third_stop_time_layer, new_tuple->value->cstring);
      break;
    case PTV_NEXT_TIME_KEY:
      text_layer_set_text(s_next_stop_time_layer, new_tuple->value->cstring);
    case DATA_ACK:
      // Value of "1" means phone sent data and has more, request it
      if (new_tuple->value->int32 == 1){
          // Wait 100ms to increase reliability (TO-DO: Add timer function)
          psleep(100);
          send_data_ack();
        }
    }      
}

static void sync_error_callback(DictionaryResult dict_error,
				AppMessageResult app_message_error,
				void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %d", app_message_error);
}

// Request stop data for stop
static void request_stop_info(){
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
  text_layer_destroy(s_station_name_layer);
  text_layer_destroy(s_next_stop_time_layer);
  text_layer_destroy(s_first_stop_time_layer);
  text_layer_destroy(s_first_stop_dest_layer);
  text_layer_destroy(s_second_stop_time_layer);
  text_layer_destroy(s_third_stop_time_layer);
  text_layer_destroy(s_second_stop_dest_layer);
  text_layer_destroy(s_third_stop_dest_layer);
  
  status_bar_layer_destroy(s_status_bar);

  layer_destroy(s_canvas_layer);
  layer_destroy(s_canvas_layer2);

  window_destroy(window);
  s_station_window = NULL;

  if (refresh_timer_handle) {
    app_timer_cancel(refresh_timer_handle);
    refresh_timer_handle = NULL;
  }
}

// Drawing (Bolded lines)
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);

  // Next departures seperation
  GPoint start = GPoint(s_content_inset, s_next_departures_divider_y);
  GPoint end = GPoint(bounds.size.w - s_content_inset, s_next_departures_divider_y);
  graphics_draw_line(ctx, start, end);

  // Time to next departure rectangle background
  GRect rect_bounds = GRect(s_next_departure_box_x, s_next_departure_box_y, s_next_departure_box_width, s_next_departure_box_height);
  graphics_fill_rect(ctx, rect_bounds, 8, GCornersAll);
}
// Drawing (Thin lines)
static void canvas_update_proc2(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);

  // First departure seperation line
  GPoint start = GPoint(s_content_inset, s_first_divider_y);
  GPoint end = GPoint(bounds.size.w - s_content_inset, s_first_divider_y);
  graphics_draw_line(ctx, start, end);

  // Next depatures list seperation
  GPoint start2 = GPoint(s_content_inset, s_third_departures_divider_y);
  GPoint end2 = GPoint(bounds.size.w - s_content_inset, s_third_departures_divider_y);
  graphics_draw_line(ctx, start2, end2);
  
}

// Load stop window
void station_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  update_layout_metrics(bounds);
  const int content_width = bounds.size.w - (s_content_inset * 2);

  // Create status bar
  s_status_bar = status_bar_layer_create();
  GRect statusbar_frame = GRect(0, 0, bounds.size.w, STATUS_BAR_LAYER_HEIGHT);
  status_bar_layer_set_colors(s_status_bar, GColorClear, GColorBlack);
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeDotted);
  layer_set_frame(status_bar_layer_get_layer(s_status_bar), statusbar_frame);
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));
  
  // Create drawing/borders
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
  s_canvas_layer2 = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer2, canvas_update_proc2);
  layer_add_child(window_layer, s_canvas_layer2);

  // Banner station name
  s_station_name_layer = text_layer_create(GRect(s_content_inset, STATUS_BAR_LAYER_HEIGHT, content_width, 32));
  text_layer_set_text_color(s_station_name_layer, GColorWhite);
  text_layer_set_background_color(s_station_name_layer, get_header_background_color());
  text_layer_set_overflow_mode(s_station_name_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_station_name_layer, GTextAlignmentCenter);
  text_layer_set_font(s_station_name_layer, fonts_get_system_font((bounds.size.h >= 200 || PBL_IF_ROUND_ELSE(true, false)) ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD));
  layer_set_frame(text_layer_get_layer(s_station_name_layer), GRect(s_content_inset, STATUS_BAR_LAYER_HEIGHT, content_width, s_header_height));
  text_layer_set_text(s_station_name_layer, stopName);
  layer_add_child(window_layer, text_layer_get_layer(s_station_name_layer));

  // Time till next departure
  s_next_stop_time_layer = text_layer_create(GRect(s_content_inset, s_next_departure_box_y + ((s_next_departure_box_height - s_next_stop_time_height) / 2), content_width, s_next_stop_time_height));
  text_layer_set_text_color(s_next_stop_time_layer, GColorWhite);
  text_layer_set_background_color(s_next_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_next_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_next_stop_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_next_stop_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_next_stop_time_layer));

  // First depature time
  s_first_stop_time_layer = text_layer_create(GRect(s_content_inset + 3, s_first_departure_row_y, s_time_column_width, s_departure_row_height));
  text_layer_set_text_color(s_first_stop_time_layer, GColorBlack);
  text_layer_set_background_color(s_first_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_first_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_first_stop_time_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_first_stop_time_layer));

  // First departure destination
  s_first_stop_dest_layer = text_layer_create(GRect(s_content_inset + s_time_column_width, s_first_departure_row_y, content_width - s_time_column_width, s_departure_row_height));
  text_layer_set_text_color(s_first_stop_dest_layer, GColorBlack);
  text_layer_set_background_color(s_first_stop_dest_layer, GColorClear);
  text_layer_set_overflow_mode(s_first_stop_dest_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_first_stop_dest_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_first_stop_dest_layer));

  // Second depature time
  s_second_stop_time_layer = text_layer_create(GRect(s_content_inset + 3, s_second_departure_row_y, s_time_column_width, s_departure_row_height));
  text_layer_set_text_color(s_second_stop_time_layer, GColorBlack);
  text_layer_set_background_color(s_second_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_second_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_second_stop_time_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_second_stop_time_layer));

  // Second departure destination
  s_second_stop_dest_layer = text_layer_create(GRect(s_content_inset + s_time_column_width, s_second_departure_row_y, content_width - s_time_column_width, s_departure_row_height));
  text_layer_set_text_color(s_second_stop_dest_layer, GColorBlack);
  text_layer_set_background_color(s_second_stop_dest_layer, GColorClear);
  text_layer_set_overflow_mode(s_second_stop_dest_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_second_stop_dest_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_second_stop_dest_layer));

  // Third depature time
  s_third_stop_time_layer = text_layer_create(GRect(s_content_inset + 3, s_third_departure_row_y, s_time_column_width, s_departure_row_height));
  text_layer_set_text_color(s_third_stop_time_layer, GColorBlack);
  text_layer_set_background_color(s_third_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_third_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_third_stop_time_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_third_stop_time_layer));

  // Third departure destination
  s_third_stop_dest_layer = text_layer_create(GRect(s_content_inset + s_time_column_width, s_third_departure_row_y, content_width - s_time_column_width, s_departure_row_height));
  text_layer_set_text_color(s_third_stop_dest_layer, GColorBlack);
  text_layer_set_background_color(s_third_stop_dest_layer, GColorClear);
  text_layer_set_overflow_mode(s_third_stop_dest_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_third_stop_dest_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_third_stop_dest_layer));

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
void station_window_push(int stop, char *name, int stop_type) {
  logger("Starting station window");
  stopNumber = stop;
  stopName = name;
  stopType = stop_type;

  if(!s_station_window) {
    s_station_window = window_create();
    window_set_window_handlers(s_station_window, (WindowHandlers) {
        .load = station_window_load,
        .unload = station_window_unload,
    });
  }
  window_stack_push(s_station_window, true);
}