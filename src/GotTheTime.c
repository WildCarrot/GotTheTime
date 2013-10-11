/*
Watch face with date and time like I like them.
General layout is below (not to scale).

+------------+
|   Sunday   |
| 2013-10-06 |
|            |
|   09:23    | <<< big font
|            |
+------------+

Vibrate on the hour, when enabled.  (Enabled by default.)

Used "Simplicity" watchface as a guide, but I want the day of the week.
Also, I eventually want to add number of meetings or timezone info.

MAYBE: add fuzzy text below the time?
 */

#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#define MY_UUID { 0xC5, 0xCE, 0xC5, 0x1C, 0x27, 0x6B, 0x44, 0xBA, 0xAE, 0x22, 0x58, 0x0E, 0x74, 0xA5, 0xAD, 0x21 }
PBL_APP_INFO(MY_UUID,
             "GotTheTime Watchface", "Build Something Awesome",
             1, 0, /* App version */
             RESOURCE_ID_IMAGE_MENU_ICON,
             APP_INFO_WATCH_FACE);

/* Seems like these should be defined somewhere. */
#define SCREEN_WIDTH  144
#define SCREEN_HEIGHT 168

#define SCREEN_MIDDLE_X 72 /* SCREEN_WIDTH / 2 */
#define SCREEN_MIDDLE_Y 84 /* SCREEN_HEIGHT / 2 */

#define DRAW_INSET 0
#define DRAW_WIDTH 136 // SCREEN_WIDTH - DRAW_INSET

#define SMALL_FONT_HEIGHT 21
#define LARGE_FONT_HEIGHT 49
#define FONT_PAD_Y 2

// I think the drawing of text is smart enough to use the upper-left
// corner of where you want to draw text and not the baseline.
#define SCREEN_DATE_X DRAW_INSET
#define SCREEN_DATE_Y 3
#define SCREEN_DATE_WIDTH  DRAW_WIDTH
#define SCREEN_DATE_HEIGHT 26 // SMALL_FONT_HEIGHT + FONT_PAD_Y + 4 to make room for descent of "y" in "Thursday"

#define SCREEN_TIME_X DRAW_INSET
#define SCREEN_TIME_Y 59 // SCREEN_MIDDLE_Y - LARGE_FONT_HEIGHT/2
#define SCREEN_TIME_WIDTH  DRAW_WIDTH
#define SCREEN_TIME_HEIGHT 51 // LARGE_FONT_HEIGHT + FONT_PAD_Y

#define LOWER_DATE_X DRAW_INSET
#define LOWER_DATE_Y 28 // SCREEN_DATE_Y + SCREEN_DATE_HEIGHT
#define LOWER_DATE_WIDTH  SCREEN_DATE_WIDTH
#define LOWER_DATE_HEIGHT 31 // SMALL_FONT_HEIGHT + FONT_PAD_Y

#define LOWER_TIME_X DRAW_INSET
#define LOWER_TIME_Y 112 // SCREEN_TIME_Y + SCREEN_TIME_HEIGHT + FONT_PAD_Y
#define LOWER_TIME_WIDTH  SCREEN_TIME_WIDTH
#define LOWER_TIME_HEIGHT 31 // SMALL_FONT_HEIGHT

#define VIBRATE_HOURLY 1 // Change to 0 to disable

Window window;

TextLayer text_date_layer;
TextLayer text_time_layer;
TextLayer text_lower_date_layer;
TextLayer text_lower_time_layer;

Layer line_layer;
Layer lower_line_layer;

const VibePattern HOUR_VIBE_PATTERN = {
  .durations = (uint32_t []) {50, 200, 50, 200, 50, 200},
  .num_segments = 4
};

void draw_screen(AppContextRef ctx, PblTm* ptime) {
  // Need to be static because they're used by the system later.
  static char time_text[]  = "00:00";
  static char date_text[]  = "Xxxxxxxxxx"; // Sunday
  static char lower_date[] = "0000-00-00"; // 2013-10-02
  static char lower_time[] = "XX"; // AM

  static bool  last_time_saved = false;
  static PblTm last_time;

  char *time_format;

  // Date string: set the text if we've never set it before or if it changed.
  if (!last_time_saved || (last_time_saved && (ptime->tm_mday != last_time.tm_mday))) {
	  string_format_time(date_text, sizeof(date_text), "%A", ptime);
	  text_layer_set_text(&text_date_layer, date_text);

	  string_format_time(lower_date, sizeof(lower_date), "%Y-%m-%d", ptime);
	  text_layer_set_text(&text_lower_date_layer, lower_date);
  }

  // Time string: always set since we're called at init time or when the minute changes.
  if (clock_is_24h_style()) {
    time_format = "%R";
  } else {
    time_format = "%I:%M";
  }
  string_format_time(time_text, sizeof(time_text), time_format, ptime);

  if (VIBRATE_HOURLY && (ptime->tm_min == 0)) {
    vibes_enqueue_custom_pattern(HOUR_VIBE_PATTERN);
  }

  // Remove the leading zero for 12-hour clocks.
  // Only needed because there's no non-padded hour format string.
  if (!clock_is_24h_style()) {
    if (time_text[0] == '0') {
      memmove(time_text, &time_text[1], sizeof(time_text) - 1);
    }
    string_format_time(lower_time, sizeof(lower_time), "%p", ptime);
    text_layer_set_text(&text_lower_time_layer, lower_time);
  }

  text_layer_set_text(&text_time_layer, time_text);

  // Save the latest date/time.
  last_time_saved = true;
  last_time = *ptime;
}

void handle_init(AppContextRef ctx) {
  window_init(&window, "GotTheTime");
  window_stack_push(&window, true /* Animated */);
  window_set_background_color(&window, GColorBlack);

  // Load the custom fonts.
  resource_init_current_app(&GOT_THE_TIME_RESOURCES);

  /* Date layer */
  text_layer_init(&text_date_layer, window.layer.frame);
  text_layer_set_text_alignment(&text_date_layer, GTextAlignmentCenter);
  text_layer_set_text_color(&text_date_layer, GColorWhite);
  text_layer_set_background_color(&text_date_layer, GColorClear);
  layer_set_frame(&text_date_layer.layer,
		  GRect(SCREEN_DATE_X, SCREEN_DATE_Y, SCREEN_DATE_WIDTH, SCREEN_DATE_HEIGHT));
  text_layer_set_font(&text_date_layer,
		      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21)));
  layer_add_child(&window.layer, &text_date_layer.layer);

  /* Time layer */
  text_layer_init(&text_time_layer, window.layer.frame);
  text_layer_set_text_alignment(&text_time_layer, GTextAlignmentCenter);
  text_layer_set_text_color(&text_time_layer, GColorWhite);
  text_layer_set_background_color(&text_time_layer, GColorClear);
  layer_set_frame(&text_time_layer.layer,
		  GRect(SCREEN_TIME_X, SCREEN_TIME_Y, SCREEN_TIME_WIDTH, SCREEN_TIME_HEIGHT));
  text_layer_set_font(&text_time_layer,
		      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_B_SUBSET_49)));
  layer_add_child(&window.layer, &text_time_layer.layer);

  // Lower date layer
  text_layer_init(&text_lower_date_layer, window.layer.frame);
  text_layer_set_text_color(&text_lower_date_layer, GColorWhite);
  text_layer_set_text_alignment(&text_lower_date_layer, GTextAlignmentCenter);
  text_layer_set_background_color(&text_lower_date_layer, GColorClear);
  layer_set_frame(&text_lower_date_layer.layer,
		  GRect(LOWER_DATE_X, LOWER_DATE_Y, LOWER_DATE_WIDTH, LOWER_DATE_HEIGHT));
  text_layer_set_font(&text_lower_date_layer,
		      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21)));
  layer_add_child(&window.layer, &text_lower_date_layer.layer);

  // Lower text layer
  text_layer_init(&text_lower_time_layer, window.layer.frame);
  text_layer_set_text_color(&text_lower_time_layer, GColorWhite);
  text_layer_set_text_alignment(&text_lower_time_layer, GTextAlignmentCenter);
  text_layer_set_background_color(&text_lower_time_layer, GColorClear);
  layer_set_frame(&text_lower_time_layer.layer,
		  GRect(LOWER_TIME_X, LOWER_TIME_Y, LOWER_TIME_WIDTH, LOWER_TIME_HEIGHT));
  text_layer_set_font(&text_lower_time_layer,
		      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21)));
  layer_add_child(&window.layer, &text_lower_time_layer.layer);

  // Update here to avoid blank display on launch
  PblTm curr_time;
  get_time(&curr_time);
  draw_screen(ctx, &curr_time);
}


void handle_minute_tick(AppContextRef ctx, PebbleTickEvent* t) {
  draw_screen(ctx, t->tick_time);
}


void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,

    .tick_info = {
      .tick_handler = &handle_minute_tick,
      .tick_units   = MINUTE_UNIT
    }
  };

  app_event_loop(params, &handlers);
}
