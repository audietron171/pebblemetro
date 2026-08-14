#include "station_window.h"

// Main window
static Window *s_station_window;

// Status bar layers
static StatusBarLayer *s_status_bar;

// Drawing layers
static Layer *s_canvas_layer;
static Layer *s_canvas_layer2;
static Layer *s_canvas_layer3;

// Icons
static GBitmap *s_transport_type_icon;

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

// Sync for JS communication
static AppSync s_sync;
static uint8_t s_sync_buffer[200];

// Refresh timer (ms)
static uint32_t refresh_timeout_ms = 10*1000;
static AppTimer *refresh_timer_handle;

// Use longer destination names when resolution permits
#ifdef PBL_PLATFORM_EMERY
  #define DESTINATION_MAX_LENGTH 25
#else
  #define DESTINATION_MAX_LENGTH 15
#endif
static char s_first_destination[DESTINATION_MAX_LENGTH + 1];
static char s_second_destination[DESTINATION_MAX_LENGTH + 1];
static char s_third_destination[DESTINATION_MAX_LENGTH + 1];

// Post strings to log
static void logger(char *message){
  static char s_buff[32];
  snprintf(s_buff, sizeof(s_buff), message);
  APP_LOG(APP_LOG_LEVEL_INFO, "%s", s_buff);
}

static int scale_dimension(int value, int base, int target) {
  return (value * target + (base / 2)) / base;
}

static int scale_x(int value, int width) {
  return scale_dimension(value, 144, width);
}

static int scale_y(int value, int height) {
  return scale_dimension(value, 168 - STATUS_BAR_LAYER_HEIGHT, height);
}

static char *format_destination(char *formatted, size_t formatted_size, const char *dest) {
  if (!dest || formatted_size == 0) {
    if (formatted_size > 0) {
      formatted[0] = '\0';
    }
    return formatted;
  }

  size_t length = strlen(dest);
  if (length > DESTINATION_MAX_LENGTH) {
    size_t text_length = DESTINATION_MAX_LENGTH - 3;

    memcpy(formatted, dest, text_length);
    memcpy(formatted + text_length, "...", 3);
    formatted[DESTINATION_MAX_LENGTH] = '\0';
  } else {
    snprintf(formatted, formatted_size, "%s", dest);
  }

  return formatted;
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
      text_layer_set_text(s_first_stop_dest_layer, format_destination(
        s_first_destination,
        sizeof(s_first_destination),
        new_tuple->value->cstring)
      );
      break;
    case PTV_1_TIME_KEY:
      text_layer_set_text(s_first_stop_time_layer, new_tuple->value->cstring);
      break;
    case PTV_2_DEST_KEY:
      text_layer_set_text(s_second_stop_dest_layer, format_destination(
        s_second_destination,
        sizeof(s_second_destination),
        new_tuple->value->cstring)
      );
      break; 
    case PTV_2_TIME_KEY:
      text_layer_set_text(s_second_stop_time_layer, new_tuple->value->cstring);
      break;
    case PTV_3_DEST_KEY:
      text_layer_set_text(s_third_stop_dest_layer, format_destination(
        s_third_destination,
        sizeof(s_third_destination),
        new_tuple->value->cstring)
      );
      break; 
    case PTV_3_TIME_KEY:
      text_layer_set_text(s_third_stop_time_layer, new_tuple->value->cstring);
      break;
    case PTV_NEXT_TIME_KEY:
      text_layer_set_text(s_next_stop_time_layer, new_tuple->value->cstring);
      break;
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


static uint32_t get_stop_icon_resource_id(int type) {
  #ifdef PBL_PLATFORM_EMERY
    switch (type) {
      case 1:
        return RESOURCE_ID_TRAM_BIG_PNG_ICON;
      case 2:
      case 4:
        return RESOURCE_ID_BUS_BIG_PNG_ICON;
      case 3:
        return RESOURCE_ID_VLINE_BIG_PNG_ICON;
      default:
        return RESOURCE_ID_TRAIN_BIG_PNG_ICON;
    }
  #else
    switch (type) {
      case 1:
        return RESOURCE_ID_TRAM_PNG_ICON;
      case 2:
      case 4:
        return RESOURCE_ID_BUS_PNG_ICON;
      case 3:
        return RESOURCE_ID_VLINE_PNG_ICON;
      default:
        return RESOURCE_ID_TRAIN_PNG_ICON;
    }
  #endif
}

static GColor get_station_banner_background_color(int type) {
  #ifdef PBL_COLOR
    switch (type) {
      case 1:
        // Tram green
        return GColorIslamicGreen;
      case 2:
      case 4:
        // Bus orange
        return GColorChromeYellow;
      case 3:
        // V/Line purple
        return GColorVividViolet;
      default:
        // Metro blue
        return GColorBlueMoon;
    }
  #else
    // Monochrome platforms have no color support.
    return GColorBlack;
  #endif
}

static GColor get_station_banner_text_color() {
  #ifdef PBL_COLOR
    return GColorBlack;
  #else
    // Monochrome platforms have no color support.
    return GColorWhite;
  #endif
}

// Unload window and return to stop selection
void station_window_unload(Window *window) {
  app_sync_deinit(&s_sync);
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

  app_timer_cancel(refresh_timer_handle);
}

// Drawing (Bolded lines)
static void canvas_update_bold_lines(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int content_height = bounds.size.h - STATUS_BAR_LAYER_HEIGHT;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, scale_x(3, bounds.size.w));

  // Next departures seperation
  GPoint start = GPoint(0, STATUS_BAR_LAYER_HEIGHT + scale_y(104, content_height));
  GPoint end = GPoint(bounds.size.w, STATUS_BAR_LAYER_HEIGHT + scale_y(104, content_height));
  graphics_draw_line(ctx, start, end);

  // Time to next departure rectangle background
  GRect rect_bounds = GRect(scale_x(42, bounds.size.w), STATUS_BAR_LAYER_HEIGHT + scale_y(62, content_height), scale_x(62, bounds.size.w), scale_y(35, content_height));
  graphics_fill_rect(ctx, rect_bounds, scale_x(8, bounds.size.w), GCornersAll);
}
// Drawing (Thin lines)
static void canvas_update_thin_lines(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int content_height = bounds.size.h - STATUS_BAR_LAYER_HEIGHT;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, scale_x(1, bounds.size.w));

  // Route type icon bottom divider
  GPoint start = GPoint(0, STATUS_BAR_LAYER_HEIGHT + scale_y(32, content_height));
  GPoint end = GPoint(bounds.size.w, STATUS_BAR_LAYER_HEIGHT + scale_y(32, content_height));
  graphics_draw_line(ctx, start, end);

  // First departure seperation line
  GPoint start2 = GPoint(0, STATUS_BAR_LAYER_HEIGHT + scale_y(52, content_height));
  GPoint end2 = GPoint(bounds.size.w, STATUS_BAR_LAYER_HEIGHT + scale_y(52, content_height));
  graphics_draw_line(ctx, start2, end2);

  // Next depatures list seperation
  GPoint start3 = GPoint(0, STATUS_BAR_LAYER_HEIGHT + scale_y(128, content_height));
  GPoint end3 = GPoint(bounds.size.w, STATUS_BAR_LAYER_HEIGHT + scale_y(128, content_height));
  graphics_draw_line(ctx, start3, end3);
  
}
// Drawing (route icon)
static void canvas_update_icon(Layer *layer, GContext *ctx) {
  // Top left hand corner
  GRect bounds = layer_get_bounds(layer);
  GRect image_bounds = gbitmap_get_bounds(s_transport_type_icon);
  image_bounds.origin = GPoint(scale_x(3, bounds.size.w), STATUS_BAR_LAYER_HEIGHT);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_transport_type_icon, image_bounds);
}

// Load stop window
void station_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  GRect available_bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w, bounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  int time_x = scale_x(3, bounds.size.w);
  int detail_x = scale_x(47, bounds.size.w);
  int header_height = scale_y(32, available_bounds.size.h);
  int time_box_y = STATUS_BAR_LAYER_HEIGHT + scale_y(70, available_bounds.size.h);
  int row_height = scale_y(32, available_bounds.size.h);
  int destination_height = scale_y(16, available_bounds.size.h);

  // Create status bar
  s_status_bar = status_bar_layer_create();
  GRect statusbar_frame = GRect(0, 0, bounds.size.w, STATUS_BAR_LAYER_HEIGHT);
  status_bar_layer_set_colors(s_status_bar, GColorClear, GColorBlack);
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeDotted);
  layer_set_frame(status_bar_layer_get_layer(s_status_bar), statusbar_frame);
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));
  
  // Create drawing/borders
  s_transport_type_icon = gbitmap_create_with_resource(get_stop_icon_resource_id(stopType));
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_bold_lines);
  layer_add_child(window_layer, s_canvas_layer);
  s_canvas_layer2 = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer2, canvas_update_thin_lines);
  layer_add_child(window_layer, s_canvas_layer2);
  s_canvas_layer3 = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer3, canvas_update_icon);
  layer_add_child(window_layer, s_canvas_layer3);

  // Banner station name
  s_station_name_layer = text_layer_create(GRect(0.25*available_bounds.size.w, STATUS_BAR_LAYER_HEIGHT, 0.75*available_bounds.size.w, header_height));
  text_layer_set_text_color(s_station_name_layer, get_station_banner_text_color());
  text_layer_set_background_color(s_station_name_layer, get_station_banner_background_color(stopType));
  text_layer_set_overflow_mode(s_station_name_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_station_name_layer, GTextAlignmentCenter);
  text_layer_set_text(s_station_name_layer, stopName);
  layer_add_child(window_layer, text_layer_get_layer(s_station_name_layer));

  // Time till next departure
  s_next_stop_time_layer = text_layer_create(GRect(0, time_box_y, available_bounds.size.w, row_height));
  text_layer_set_text_color(s_next_stop_time_layer, GColorWhite);
  text_layer_set_background_color(s_next_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_next_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_next_stop_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_next_stop_time_layer));

  // First depature time
  s_first_stop_time_layer = text_layer_create(GRect(time_x, STATUS_BAR_LAYER_HEIGHT + scale_y(34, available_bounds.size.h), available_bounds.size.w, row_height));
  text_layer_set_text_color(s_first_stop_time_layer, GColorBlack);
  text_layer_set_background_color(s_first_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_first_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_first_stop_time_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_first_stop_time_layer));

  // First departure destination
  s_first_stop_dest_layer = text_layer_create(GRect(detail_x, STATUS_BAR_LAYER_HEIGHT + scale_y(34, available_bounds.size.h), available_bounds.size.w - detail_x, destination_height));
  text_layer_set_text_color(s_first_stop_dest_layer, GColorBlack);
  text_layer_set_background_color(s_first_stop_dest_layer, GColorClear);
  text_layer_set_overflow_mode(s_first_stop_dest_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_first_stop_dest_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_first_stop_dest_layer));

  // Second depature time
  s_second_stop_time_layer = text_layer_create(GRect(time_x, STATUS_BAR_LAYER_HEIGHT + scale_y(108, available_bounds.size.h), available_bounds.size.w, row_height));
  text_layer_set_text_color(s_second_stop_time_layer, GColorBlack);
  text_layer_set_background_color(s_second_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_second_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_second_stop_time_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_second_stop_time_layer));

  // Second departure destination
  s_second_stop_dest_layer = text_layer_create(GRect(detail_x, STATUS_BAR_LAYER_HEIGHT + scale_y(108, available_bounds.size.h), available_bounds.size.w - detail_x, destination_height));
  text_layer_set_text_color(s_second_stop_dest_layer, GColorBlack);
  text_layer_set_background_color(s_second_stop_dest_layer, GColorClear);
  text_layer_set_overflow_mode(s_second_stop_dest_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_second_stop_dest_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_second_stop_dest_layer));

  // Third depature time
  s_third_stop_time_layer = text_layer_create(GRect(time_x, STATUS_BAR_LAYER_HEIGHT + scale_y(130, available_bounds.size.h), available_bounds.size.w, row_height));
  text_layer_set_text_color(s_third_stop_time_layer, GColorBlack);
  text_layer_set_background_color(s_third_stop_time_layer, GColorClear);
  text_layer_set_overflow_mode(s_third_stop_time_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_alignment(s_third_stop_time_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_third_stop_time_layer));

  // Third departure destination
  s_third_stop_dest_layer = text_layer_create(GRect(detail_x, STATUS_BAR_LAYER_HEIGHT + scale_y(130, available_bounds.size.h), available_bounds.size.w - detail_x, destination_height));
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
void station_window_push(int stop, char *name, int type) {
  logger("Starting station window");
  stopNumber = stop;
  stopName = name;
  stopType = type;

  if(!s_station_window) {
    s_station_window = window_create();
    window_set_window_handlers(s_station_window, (WindowHandlers) {
        .load = station_window_load,
        .unload = station_window_unload,
    });
  }
  window_stack_push(s_station_window, true);
}