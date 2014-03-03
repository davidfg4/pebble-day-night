#include <pebble.h>
#include "config.h"

#define STR_SIZE 20
#define TIME_OFFSET_PERSIST 1
#define REDRAW_INTERVAL 15

static Window *window;
static TextLayer *time_text_layer;
static TextLayer *date_text_layer;
static GBitmap *world_bitmap;
static Layer *canvas;
// this is a manually created bitmap, of the same size as world_bitmap
static GBitmap image;
static int redraw_counter;
// s is set to memory of size STR_SIZE, and temporarily stores strings
char *s;
// Local time is wall time, not UTC, so an offset is used to get UTC
int time_offset;

static void draw_earth() {
  // ##### calculate the time
#ifdef UTC_OFFSET
  int now = (int)time(NULL) + -3600 * UTC_OFFSET;
#else
  int now = (int)time(NULL) + time_offset;
#endif
  float day_of_year; // value from 0 to 1 of progress through a year
  float time_of_day; // value from 0 to 1 of progress through a day
  // approx number of leap years since epoch
  // = now / SECONDS_IN_YEAR * .24; (.24 = average rate of leap years)
  int leap_years = (int)((float)now / 131487192.0);
  // day_of_year is an estimate, but should be correct to within one day
  day_of_year = now - (((int)((float)now / 31556926.0) * 365 + leap_years) * 86400);
  day_of_year = day_of_year / 86400.0;
  time_of_day = day_of_year - (int)day_of_year;
  day_of_year = day_of_year / 365.0;
  // ##### calculate the position of the sun
  // left to right of world goes from 0 to 65536
  int sun_x = (int)((float)TRIG_MAX_ANGLE * (1.0 - time_of_day));
  // bottom to top of world goes from -32768 to 32768
  // 0.2164 is march 20, the 79th day of the year, the march equinox
  // Earth's inclination is 23.4 degrees, so sun should vary 23.4/90=.26 up and down
  int sun_y = -sin_lookup((day_of_year - 0.2164) * TRIG_MAX_ANGLE) * .26 * .25;
  // ##### draw the bitmap
  int x, y;
  for(x = 0; x < image.bounds.size.w; x++) {
    int x_angle = (int)((float)TRIG_MAX_ANGLE * (float)x / (float)(image.bounds.size.w));
    for(y = 0; y < image.bounds.size.h; y++) {
      int byte = y * image.row_size_bytes + (int)(x / 8);
      int y_angle = (int)((float)TRIG_MAX_ANGLE * (float)y / (float)(image.bounds.size.h * 2)) - TRIG_MAX_ANGLE/4;
      // spherical law of cosines
      float angle = ((float)sin_lookup(sun_y)/(float)TRIG_MAX_RATIO) * ((float)sin_lookup(y_angle)/(float)TRIG_MAX_RATIO);
      angle = angle + ((float)cos_lookup(sun_y)/(float)TRIG_MAX_RATIO) * ((float)cos_lookup(y_angle)/(float)TRIG_MAX_RATIO) * ((float)cos_lookup(sun_x - x_angle)/(float)TRIG_MAX_RATIO);
      if ((angle < 0) ^ (0x1 & (((char *)world_bitmap->addr)[byte] >> (x % 8)))) {
        // white pixel
        ((char *)image.addr)[byte] = ((char *)image.addr)[byte] | (0x1 << (x % 8));
      } else {
        // black pixel
        ((char *)image.addr)[byte] = ((char *)image.addr)[byte] & ~(0x1 << (x % 8));
      }
    }
  }
  layer_mark_dirty(canvas);
}

static void draw_watch(struct Layer *layer, GContext *ctx) {
  graphics_draw_bitmap_in_rect(ctx, &image, image.bounds);
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  static char time_text[] = "00:00";
  static char date_text[] = "Xxx, Xxx 00";

  strftime(date_text, sizeof(date_text), "%a, %b %e", tick_time);
  text_layer_set_text(date_text_layer, date_text);

  if (clock_is_24h_style()) {
    strftime(time_text, sizeof(time_text), "%R", tick_time);
  } else {
    strftime(time_text, sizeof(time_text), "%I:%M", tick_time);
  }
  if (!clock_is_24h_style() && (time_text[0] == '0')) {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  }
  text_layer_set_text(time_text_layer, time_text);
 
  redraw_counter++;
  if (redraw_counter >= REDRAW_INTERVAL) {
    draw_earth();
    redraw_counter = 0;
  }
}

// Get the time from the phone, which is probably UTC
// Calculate and store the offset when compared to the local clock
static void app_message_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *t = dict_find(iterator, 0);
  int unixtime = t->value->int32;
  int now = (int)time(NULL);
  time_offset = unixtime - now;
  status_t s = persist_write_int(TIME_OFFSET_PERSIST, time_offset); 
  if (s) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Saved time offset %d with status %d", time_offset, (int) s);
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Failed to save time offset with status %d", (int) s);
  }
  draw_earth();
}

static void window_load(Window *window) {
#ifdef BLACK_ON_WHITE
  window_set_background_color(window, GColorWhite);
#else
  window_set_background_color(window, GColorBlack);
#endif
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  time_text_layer = text_layer_create(GRect(0, 72, 144-0, 168-72));
#ifdef BLACK_ON_WHITE
  text_layer_set_background_color(time_text_layer, GColorWhite);
  text_layer_set_text_color(time_text_layer, GColorBlack);
#else
  text_layer_set_background_color(time_text_layer, GColorBlack);
  text_layer_set_text_color(time_text_layer, GColorWhite);
#endif
  text_layer_set_font(time_text_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text(time_text_layer, "");
  text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer));

  date_text_layer = text_layer_create(GRect(0, 130, 144-0, 168-130));
#ifdef BLACK_ON_WHITE
  text_layer_set_background_color(date_text_layer, GColorWhite);
  text_layer_set_text_color(date_text_layer, GColorBlack);
#else
  text_layer_set_background_color(date_text_layer, GColorBlack);
  text_layer_set_text_color(date_text_layer, GColorWhite);
#endif
  text_layer_set_font(date_text_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
  text_layer_set_text(date_text_layer, "");
  text_layer_set_text_alignment(date_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(date_text_layer));

  canvas = layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
  layer_set_update_proc(canvas, draw_watch);
  layer_add_child(window_layer, canvas);
  image = *world_bitmap;
  image.addr = malloc(image.row_size_bytes * image.bounds.size.h);
  draw_earth();
}

static void window_unload(Window *window) {
  text_layer_destroy(time_text_layer);
  text_layer_destroy(date_text_layer);
  layer_destroy(canvas);
  free(image.addr);
}

static void init(void) {
  redraw_counter = 0;

  // Load the UTC offset, if it exists
  time_offset = 0;
  if (persist_exists(TIME_OFFSET_PERSIST)) {
    time_offset = persist_read_int(TIME_OFFSET_PERSIST);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "loaded offset %d", time_offset);
  }

  world_bitmap = gbitmap_create_with_resource(RESOURCE_ID_WORLD);

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  
  const bool animated = true;
  window_stack_push(window, animated);

  s = malloc(STR_SIZE);
  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);

  app_message_register_inbox_received(app_message_inbox_received);
  app_message_open(30, 0);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  free(s);
  window_destroy(window);
  gbitmap_destroy(world_bitmap);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}
