/*
Watch face with date and time like I like them.
General layout is below (not to scale).

+------------+
|   Sunday   |
| 2013-10-06 |
|            |
|   09:23    | <<< big font
|     59     | <<< TODO: not yet added
|     AM     | <<< only in 12-hour mode
+------------+

Vibrate on the hour, when enabled.  (Enabled by default.)

Used "Simplicity" watchface as a guide, but I want the day of the week.
Also, I eventually want to add number of meetings or timezone info.

*/

#include <pebble.h>
#include <time.h>

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

#define ALL_TIME_CHANGED (SECOND_UNIT | MINUTE_UNIT | HOUR_UNIT | DAY_UNIT | MONTH_UNIT | YEAR_UNIT)

Window* window;

TextLayer* text_date_layer;
TextLayer* text_time_layer;
TextLayer* text_lower_date_layer;
TextLayer* text_lower_time_layer;

const VibePattern HOUR_VIBE_PATTERN = {
  .durations = (uint32_t []) {50, 200, 50, 200, 50, 200},
  .num_segments = 6
};

void draw_screen(struct tm* ptime, TimeUnits units_changed) {
	// Buffers we can format text into.
	// Need to be static because they're used by the text layers
	// even after this function completes.
	static char time_text[]  = "00:00";
	static char date_text[]  = "Xxxxxxxxxx"; // Wednesday
	static char lower_date[] = "0000-00-00"; // 2013-10-02
	static char lower_time[] = "XX"; // AM

	char *time_format;

	// If the month or year changes, the day will change, too.
	if (units_changed & DAY_UNIT) {
		// Day of the week, full name
		strftime(date_text, sizeof(date_text), "%A", ptime);
		text_layer_set_text(text_date_layer, date_text);

		// Date
		strftime(lower_date, sizeof(lower_date), "%Y-%m-%d", ptime);
		text_layer_set_text(text_lower_date_layer, lower_date);
	}

	// Time string: always set since we're called at init time or when the minute changes.
	if (clock_is_24h_style()) {
		time_format = "%R";
	} else {
		// This includes a leading zero.
		// The format without a leading zero ("%l")
		// includes a leading space.  We want neither,
		// so remove the zero after formatting the text.
		time_format = "%I:%M";
	}
	strftime(time_text, sizeof(time_text), time_format, ptime);

	// Hack to remove a leading zero or space for 12-hour times.
	if (!clock_is_24h_style()) {
		memmove(time_text, &time_text[1], sizeof(time_text) - 1);
	}

	text_layer_set_text(text_time_layer, time_text);

	if (VIBRATE_HOURLY && (units_changed & HOUR_UNIT)) {
		// The very first time we draw the screen,
		// the hours will have "changed".
		// Don't vibe in that case, only when the minutes are zero.
		if (ptime->tm_min == 0) {
			vibes_enqueue_custom_pattern(HOUR_VIBE_PATTERN);
		}
	}

	// Set the AM/PM, if needed.
	if (!clock_is_24h_style()) {
		strftime(lower_time, sizeof(lower_time), "%p", ptime);
		text_layer_set_text(text_lower_time_layer, lower_time);
	}
}

static void window_load(Window* win) {
	// XXX TODO Base width/height on actual frame bounds.
	// Eg bounds.size.w for width of frame

	// Date layer
	text_date_layer = text_layer_create((GRect) { .origin = { SCREEN_DATE_X, SCREEN_DATE_Y },
				.size = { SCREEN_DATE_WIDTH, SCREEN_DATE_HEIGHT } });
	text_layer_set_text_alignment(text_date_layer, GTextAlignmentCenter);
	text_layer_set_text_color(text_date_layer, GColorWhite);
	text_layer_set_background_color(text_date_layer, GColorClear);
	text_layer_set_font(text_date_layer,
			    fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21)));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_date_layer));

	// Time layer
	text_time_layer = text_layer_create((GRect) { .origin = { SCREEN_TIME_X, SCREEN_TIME_Y },
				.size = { SCREEN_TIME_WIDTH, SCREEN_TIME_HEIGHT } });
	text_layer_set_text_alignment(text_time_layer, GTextAlignmentCenter);
	text_layer_set_text_color(text_time_layer, GColorWhite);
	text_layer_set_background_color(text_time_layer, GColorClear);
	text_layer_set_font(text_time_layer,
			    fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_B_SUBSET_49)));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_time_layer));

	// Lower date layer
	text_lower_date_layer = text_layer_create((GRect) { .origin = { LOWER_DATE_X, LOWER_DATE_Y },
				.size = { LOWER_DATE_WIDTH, LOWER_DATE_HEIGHT } });
	text_layer_set_text_color(text_lower_date_layer, GColorWhite);
	text_layer_set_text_alignment(text_lower_date_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_lower_date_layer, GColorClear);
	text_layer_set_font(text_lower_date_layer,
			    fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21)));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_lower_date_layer));

	// Lower text layer
	text_lower_time_layer = text_layer_create((GRect) { .origin = { LOWER_TIME_X, LOWER_TIME_Y },
				.size = { LOWER_TIME_WIDTH, LOWER_TIME_HEIGHT } });
	text_layer_set_text_color(text_lower_time_layer, GColorWhite);
	text_layer_set_text_alignment(text_lower_time_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_lower_time_layer, GColorClear);
	text_layer_set_font(text_lower_time_layer,
			    fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21)));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_lower_time_layer));
}

static void window_appear(Window* win) {
	// Update here to avoid blank display on launch
	// and to update when the window is redrawn.
	time_t now = time(NULL);
	struct tm* curr_time = localtime(&now);
	draw_screen(curr_time, ALL_TIME_CHANGED);
	free(curr_time);
}

static void window_unload(Window *win) {
	text_layer_destroy(text_lower_time_layer);
	text_layer_destroy(text_lower_date_layer);
	text_layer_destroy(text_time_layer);
	text_layer_destroy(text_date_layer);
}

void handle_minute_tick(struct tm* tick_time, TimeUnits units_changed) {
	draw_screen(tick_time, units_changed);
}

void handle_init() {
	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) {
				.load = window_load,
				.appear = window_appear,
			  	.unload = window_unload,
			  });
	window_stack_push(window, true /* Animated */);
	window_set_background_color(window, GColorBlack);

	tick_timer_service_subscribe(MINUTE_UNIT, &handle_minute_tick);
}

void handle_deinit(void) {
	tick_timer_service_unsubscribe();

	// XXX TODO Release other layers?
	//window_unload(window) should have been called to destroy layers?
	window_destroy(window);
}

int main(void) {
	handle_init();

	app_event_loop();

	handle_deinit();

	return 0;
}
