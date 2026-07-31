#include "nearby_window.h"
#include "../PebbleMetro.h"

#define MAX_NEARBY_ENTRIES 8
#define ENTRY_NAME_LEN 22
#define ENTRY_DEST_LEN 22
#define ENTRY_TIME_LEN 12
#define CONTENT_BUFFER_LEN 512

// UI elements
static Window *s_nearby_window;
static StatusBarLayer *s_status_bar;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_text_layer;

// Nearby stop data
static char s_entry_names[MAX_NEARBY_ENTRIES][ENTRY_NAME_LEN];
static char s_entry_dests[MAX_NEARBY_ENTRIES][ENTRY_DEST_LEN];
static char s_entry_times[MAX_NEARBY_ENTRIES][ENTRY_TIME_LEN];
static int s_entry_count = 0;
static bool s_loading = true;

// Text buffer (persists as long as text layer uses it)
static char s_content_buffer[CONTENT_BUFFER_LEN];

// Auto-refresh timer
static AppTimer *s_refresh_timer;
static const uint32_t REFRESH_TIMEOUT_MS = 30 * 1000;

static void logger(char *message) {
  APP_LOG(APP_LOG_LEVEL_INFO, "%s", message);
}

// Build formatted text and resize scroll content to fit
static void update_text_layer() {
  if (!s_text_layer || !s_scroll_layer || !s_nearby_window) {
    return;
  }

  if (s_loading) {
    text_layer_set_text(s_text_layer, "Loading nearby\nstops...");
  } else if (s_entry_count == 0) {
    text_layer_set_text(s_text_layer, "No nearby stops\nfound.\n\nCheck GPS signal.");
  } else {
    s_content_buffer[0] = '\0';
    for (int i = 0; i < s_entry_count; i++) {
      if (i > 0) {
        strncat(s_content_buffer, "\n\n", CONTENT_BUFFER_LEN - strlen(s_content_buffer) - 1);
      }
      char entry[80];
      snprintf(entry, sizeof(entry), "%s\n%s > %s",
               s_entry_names[i], s_entry_times[i], s_entry_dests[i]);
      strncat(s_content_buffer, entry, CONTENT_BUFFER_LEN - strlen(s_content_buffer) - 1);
    }
    text_layer_set_text(s_text_layer, s_content_buffer);
  }

  // Measure content and resize layers accordingly
  Layer *window_layer = window_get_root_layer(s_nearby_window);
  GRect bounds = layer_get_bounds(window_layer);
  int16_t text_width = bounds.size.w - 8;
  int16_t available_height = bounds.size.h - STATUS_BAR_LAYER_HEIGHT;

  GSize text_size = text_layer_get_content_size(s_text_layer);
  int16_t content_height = text_size.h + 16;
  if (content_height < available_height) {
    content_height = available_height;
  }

  layer_set_frame(text_layer_get_layer(s_text_layer),
                  GRect(4, 4, text_width, content_height));
  scroll_layer_set_content_size(s_scroll_layer,
                                GSize(bounds.size.w, content_height + 8));
}

// Send DATA_ACK=0 to request the next queued message from JS
static void send_nearby_ack() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter) {
    int value = 0;
    dict_write_int(iter, DATA_ACK, &value, sizeof(int), true);
    dict_write_end(iter);
    app_message_outbox_send();
  }
}

// Handle incoming nearby-stop data messages from JS
static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  Tuple *index_tuple = dict_find(iter, NEARBY_ENTRY_INDEX_KEY);
  if (index_tuple) {
    int idx = (int)index_tuple->value->int32;
    if (idx >= 0 && idx < MAX_NEARBY_ENTRIES) {
      s_loading = false;

      Tuple *name_tuple = dict_find(iter, NEARBY_STOP_NAME_KEY);
      if (name_tuple) {
        strncpy(s_entry_names[idx], name_tuple->value->cstring, ENTRY_NAME_LEN - 1);
        s_entry_names[idx][ENTRY_NAME_LEN - 1] = '\0';
      }

      Tuple *dest_tuple = dict_find(iter, NEARBY_DEST_KEY);
      if (dest_tuple) {
        strncpy(s_entry_dests[idx], dest_tuple->value->cstring, ENTRY_DEST_LEN - 1);
        s_entry_dests[idx][ENTRY_DEST_LEN - 1] = '\0';
      }

      Tuple *time_tuple = dict_find(iter, NEARBY_TIME_KEY);
      if (time_tuple) {
        strncpy(s_entry_times[idx], time_tuple->value->cstring, ENTRY_TIME_LEN - 1);
        s_entry_times[idx][ENTRY_TIME_LEN - 1] = '\0';
      }

      if (idx >= s_entry_count) {
        s_entry_count = idx + 1;
      }

      update_text_layer();
    }

    // If JS has more data queued, acknowledge to trigger next send
    Tuple *ack_tuple = dict_find(iter, DATA_ACK);
    if (ack_tuple && ack_tuple->value->int32 == 1) {
      psleep(100);
      send_nearby_ack();
    }
  }
}

static void request_nearby();

// Callback used by the refresh timer
static void refresh_timer_callback(void *context) {
  request_nearby();
}

// Send request for nearby departures to JS and schedule next refresh
static void request_nearby() {
  logger("Requesting nearby stops");
  s_loading = true;
  s_entry_count = 0;

  for (int i = 0; i < MAX_NEARBY_ENTRIES; i++) {
    s_entry_names[i][0] = '\0';
    s_entry_dests[i][0] = '\0';
    s_entry_times[i][0] = '\0';
  }

  update_text_layer();

  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter) {
    int value = 1;
    dict_write_int(iter, PTV_REQ_NEARBY, &value, sizeof(int), true);
    dict_write_end(iter);
    app_message_outbox_send();
  }

  s_refresh_timer = app_timer_register(REFRESH_TIMEOUT_MS, refresh_timer_callback, NULL);
}

static void nearby_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Status bar
  s_status_bar = status_bar_layer_create();
  GRect statusbar_frame = GRect(0, 0, bounds.size.w, STATUS_BAR_LAYER_HEIGHT);
  status_bar_layer_set_colors(s_status_bar, GColorClear, GColorBlack);
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeDotted);
  layer_set_frame(status_bar_layer_get_layer(s_status_bar), statusbar_frame);
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));

  // Scroll layer fills the area beneath the status bar
  GRect available = GRect(0, STATUS_BAR_LAYER_HEIGHT,
                          bounds.size.w, bounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  s_scroll_layer = scroll_layer_create(available);
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);
  layer_add_child(window_layer, scroll_layer_get_layer(s_scroll_layer));

  // Text layer inside the scroll layer (tall initial height, resized by update_text_layer)
  s_text_layer = text_layer_create(GRect(4, 4, available.size.w - 8, 2000));
  text_layer_set_text_color(s_text_layer, GColorBlack);
  text_layer_set_background_color(s_text_layer, GColorClear);
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_text_layer, GTextAlignmentLeft);
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_text_layer));

  app_message_register_inbox_received(inbox_received_callback);

  update_text_layer();
  request_nearby();
}

static void nearby_window_unload(Window *window) {
  app_timer_cancel(s_refresh_timer);
  app_message_deregister_callbacks();

  text_layer_destroy(s_text_layer);
  s_text_layer = NULL;
  scroll_layer_destroy(s_scroll_layer);
  s_scroll_layer = NULL;
  status_bar_layer_destroy(s_status_bar);

  window_destroy(window);
  s_nearby_window = NULL;
}

void nearby_window_push() {
  logger("Opening nearby window");
  s_loading = true;
  s_entry_count = 0;

  if (!s_nearby_window) {
    s_nearby_window = window_create();
    window_set_window_handlers(s_nearby_window, (WindowHandlers) {
      .load = nearby_window_load,
      .unload = nearby_window_unload,
    });
  }
  window_stack_push(s_nearby_window, true);
}
