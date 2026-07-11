/**
 * FART MACHINE for Pebble Time 2  (debug build v1.1)
 * UP/DOWN: choose  SELECT: fire  LONG SELECT: random
 */

#include <pebble.h>

#define NUM_FARTS 5
#define PUMP_INTERVAL_MS 25
#define VOLUME 100
#define CHUNK_BYTES 2048     // max bytes handed to the stream per pump
#define MAX_STALLS 200       // ~5s of "buffer full" before giving up

static const uint32_t FART_RESOURCES[NUM_FARTS] = {
  RESOURCE_ID_FART_1, RESOURCE_ID_FART_2, RESOURCE_ID_FART_3,
  RESOURCE_ID_FART_4, RESOURCE_ID_FART_5,
};

static const char *FART_NAMES[NUM_FARTS] = {
  "El Clasico", "El Pedito", "El Terremoto",
  "La Metralleta", "La Venganza",
};

static Window *s_window;
static TextLayer *s_title_layer, *s_name_layer, *s_hint_layer;

static int s_current = 0;
static uint8_t *s_buf = NULL;
static uint32_t s_buf_size = 0;
static uint32_t s_buf_offset = 0;
static AppTimer *s_pump_timer = NULL;
static bool s_playing = false;
static int s_stalls = 0;

static void prv_update_name(const char *override) {
  text_layer_set_text(s_name_layer, override ? override : FART_NAMES[s_current]);
}

static void prv_cleanup_buffer(void) {
  if (s_pump_timer) { app_timer_cancel(s_pump_timer); s_pump_timer = NULL; }
  if (s_buf) { free(s_buf); s_buf = NULL; }
}

static void prv_pump(void *context) {
  s_pump_timer = NULL;
  if (!s_buf) return;

  uint32_t remaining = s_buf_size - s_buf_offset;
  uint32_t to_write = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;

  uint32_t written = speaker_stream_write(s_buf + s_buf_offset, to_write);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "pump: wrote %lu/%lu (offset %lu/%lu)",
          (unsigned long)written, (unsigned long)to_write,
          (unsigned long)s_buf_offset, (unsigned long)s_buf_size);

  if (written > to_write) {
    // Error value from the API (e.g. -1) - abort safely
    APP_LOG(APP_LOG_LEVEL_ERROR, "stream_write error (%lu), aborting",
            (unsigned long)written);
    speaker_stream_close();
    prv_cleanup_buffer();
    prv_update_name("write err :(");
    return;
  }

  s_buf_offset += written;
  s_stalls = (written == 0) ? s_stalls + 1 : 0;

  if (s_stalls > MAX_STALLS) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "stream stalled, aborting");
    speaker_stream_close();
    prv_cleanup_buffer();
    prv_update_name("stalled :(");
    return;
  }

  if (s_buf_offset < s_buf_size) {
    s_pump_timer = app_timer_register(PUMP_INTERVAL_MS, prv_pump, NULL);
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "pump: done, closing stream");
    speaker_stream_close();
    prv_cleanup_buffer();
  }
}

static void prv_stop_playback(void) {
  prv_cleanup_buffer();
  if (s_playing) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "stopping active playback");
    speaker_stop();
    s_playing = false;
  }
}

static void prv_on_finish(SpeakerFinishReason reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "finish callback, reason=%d", (int)reason);
  s_playing = false;
  prv_update_name(NULL);
}

static void prv_play_fart(int index) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "play_fart(%d), heap free=%u",
          index, (unsigned)heap_bytes_free());

  prv_stop_playback();

  if (speaker_is_muted()) {
    prv_update_name("Muted!");
    return;
  }

  ResHandle handle = resource_get_handle(FART_RESOURCES[index]);
  s_buf_size = resource_size(handle);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "resource size=%lu", (unsigned long)s_buf_size);

  if (s_buf_size == 0) {
    prv_update_name("empty res :(");
    return;
  }

  s_buf = malloc(s_buf_size);
  if (!s_buf) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "malloc(%lu) failed, heap=%u",
            (unsigned long)s_buf_size, (unsigned)heap_bytes_free());
    prv_update_name("no memory :(");
    return;
  }

  size_t loaded = resource_load(handle, s_buf, s_buf_size);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "loaded %u bytes", (unsigned)loaded);
  s_buf_size = loaded;
  s_buf_offset = 0;
  s_stalls = 0;

  if (!speaker_stream_open(SpeakerPcmFormat_16kHz_8bit, VOLUME)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "stream_open failed");
    prv_cleanup_buffer();
    prv_update_name("speaker busy :(");
    return;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "stream open OK");

  s_playing = true;
  prv_update_name("* PFFFFT *");
  prv_pump(NULL);
}

static void prv_select_click(ClickRecognizerRef rec, void *ctx) {
  prv_play_fart(s_current);
}
static void prv_select_long_click(ClickRecognizerRef rec, void *ctx) {
  s_current = rand() % NUM_FARTS;
  prv_play_fart(s_current);
}
static void prv_up_click(ClickRecognizerRef rec, void *ctx) {
  s_current = (s_current + NUM_FARTS - 1) % NUM_FARTS;
  prv_update_name(NULL);
}
static void prv_down_click(ClickRecognizerRef rec, void *ctx) {
  s_current = (s_current + 1) % NUM_FARTS;
  prv_update_name(NULL);
}
static void prv_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, prv_select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  window_set_background_color(window,
      PBL_IF_COLOR_ELSE(GColorWindsorTan, GColorWhite));

  s_title_layer = text_layer_create(GRect(0, 8, bounds.size.w, 32));
  text_layer_set_text(s_title_layer, "FARTINI MAXIMO");
  text_layer_set_font(s_title_layer,
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer,
      PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorBlack));
  layer_add_child(root, text_layer_get_layer(s_title_layer));

  s_name_layer = text_layer_create(
      GRect(0, bounds.size.h / 2 - 34, bounds.size.w, 68));
  text_layer_set_font(s_name_layer,
      fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_name_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_name_layer, GColorClear);
  text_layer_set_text_color(s_name_layer,
      PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  layer_add_child(root, text_layer_get_layer(s_name_layer));
  prv_update_name(NULL);

  s_hint_layer = text_layer_create(
      GRect(0, bounds.size.h - 24, bounds.size.w, 24));
  text_layer_set_text(s_hint_layer, "up/down: pick   select: fire");
  text_layer_set_font(s_hint_layer,
      fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_hint_layer, GColorClear);
  text_layer_set_text_color(s_hint_layer,
      PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorBlack));
  layer_add_child(root, text_layer_get_layer(s_hint_layer));
}

static void prv_window_unload(Window *window) {
  prv_stop_playback();
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_name_layer);
  text_layer_destroy(s_hint_layer);
}

static void prv_init(void) {
  srand(time(NULL));
  speaker_set_finish_callback(prv_on_finish, NULL);
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  speaker_set_finish_callback(NULL, NULL);
  prv_stop_playback();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}