#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#define MY_UUID { 0xC5, 0xCE, 0xC5, 0x1C, 0x27, 0x6B, 0x44, 0xBA, 0xAE, 0x22, 0x58, 0x0E, 0x74, 0xA5, 0xAD, 0x21 }
PBL_APP_INFO(MY_UUID,
             "GotTheTime Watchface", "Build Something Awesome",
             1, 0, /* App version */
             DEFAULT_MENU_ICON,
             APP_INFO_WATCH_FACE);

/*
Watch face with date and time like I like them.
Window is 144x98 (?) (not to scale below)
+-----------+
| Tue Oct 2 |
| --------- |
|   09:23   |
+-----------+

Used "Simplicity" watchface as a guide, but I want the day of the week.
Also, I eventually want to add number of meetings or timezone info.
 */

Window window;
/* Seems like these should be defined somewhere. */
#define SCREEN_WIDTH  144
#define SCREEN_HEIGHT 168

#define SCREEN_MIDDLE_X 72 /* SCREEN_WIDTH / 2 */
#define SCREEN_MIDDLE_Y 84 /* SCREEN_HEIGHT / 2 */

#define DRAW_INSET 8

#define SCREEN_DATE_X DRAW_INSET
#define SCREEN_DATE_Y 68
#define SCREEN_DATE_WIDTH  136 /* 144-8 */
#define SCREEN_DATE_HEIGHT 100 /* 168-68 */

#define SCREEN_LINE_START_X DRAW_INSET
#define SCREEN_LINE_END_X   131
#define SCREEN_LINE_Y1 97
#define SCREEN_LINE_Y2 98

#define SCREEN_TIME_X DRAW_INSET
#define SCREEN_TIME_Y 92
#define SCREEN_TIME_WIDTH  137 /* 144 - 7 */
#define SCREEN_TIME_HEIGHT 76 /* 168 - 92 */

TextLayer text_date_layer;
TextLayer text_time_layer;

Layer line_layer;


void line_layer_update_callback(Layer* me, GContext* ctx) {
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_line(ctx,
		     GPoint(SCREEN_LINE_START_X, SCREEN_LINE_Y1),
		     GPoint(SCREEN_LINE_END_X,   SCREEN_LINE_Y1));
  graphics_draw_line(ctx,
		     GPoint(SCREEN_LINE_START_X, SCREEN_LINE_Y2),
		     GPoint(SCREEN_LINE_END_X,   SCREEN_LINE_Y2));
}

void draw_screen(AppContextRef ctx, PblTm* ptime) {
  // Need to be static because they're used by the system later.
  static char time_text[] = "00:00";
  static char date_text[] = "Xxx Xxx 00"; // Tue Oct 02

  static bool  last_time_saved = false;
  static PblTm last_time;

  char *time_format;

  // Date string: set the text if we've never set it before or if it changed.
  if (!last_time_saved || (last_time_saved && (ptime->tm_mday != last_time.tm_mday))) {
	  string_format_time(date_text, sizeof(date_text), "%a %b %d", ptime);
	  text_layer_set_text(&text_date_layer, date_text);
  }

  // Time string: always set since we're called at init time or when the minute changes.
  if (clock_is_24h_style()) {
    time_format = "%R";
  } else {
    time_format = "%I:%M";
  }
  string_format_time(time_text, sizeof(time_text), time_format, ptime);

  // Remove the leading zero for 12-hour clocks.
  // Only needed because there's no non-padded hour format string.
  if (!clock_is_24h_style() && (time_text[0] == '0')) {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
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
  text_layer_set_text_color(&text_date_layer, GColorWhite);
  text_layer_set_background_color(&text_date_layer, GColorClear);
  layer_set_frame(&text_date_layer.layer,
		  GRect(SCREEN_DATE_X, SCREEN_DATE_Y, SCREEN_DATE_WIDTH, SCREEN_DATE_HEIGHT));
  text_layer_set_font(&text_date_layer,
//		      fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
		      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21)));
  layer_add_child(&window.layer, &text_date_layer.layer);

  /* Time layer */
  text_layer_init(&text_time_layer, window.layer.frame);
  text_layer_set_text_color(&text_time_layer, GColorWhite);
  text_layer_set_background_color(&text_time_layer, GColorClear);
  layer_set_frame(&text_time_layer.layer,
		  GRect(SCREEN_TIME_X, SCREEN_TIME_Y, SCREEN_TIME_WIDTH, SCREEN_TIME_HEIGHT));
  text_layer_set_font(&text_time_layer,
//		      fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
		      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_B_SUBSET_49)));
  layer_add_child(&window.layer, &text_time_layer.layer);

  /* Line layer */
  layer_init(&line_layer, window.layer.frame);
  line_layer.update_proc = &line_layer_update_callback;
  layer_add_child(&window.layer, &line_layer);

/*
	text_layer_init(&hello_layer, GRect(0, 65, 144, 30));
	text_layer_set_text_alignment(&hello_layer, GTextAlignmentCenter);
	text_layer_set_text(&hello_layer, "Hello, World!");
	text_layer_set_font(&hello_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
	layer_add_child(&window.layer, &hello_layer.layer);
*/
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
