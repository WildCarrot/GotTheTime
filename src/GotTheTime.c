/*
Watch face with date and time like I like them.
General layout is below (not to scale).

+------------+
|   Sunday   |
| 2013-10-06 |
|            |
|   09:23    | <<< big font
|     AM     | <<< only in 12-hour mode
| w:--  p:-- | <<< battery indicator for watch and phone (? can do phone w/out phone app?)
+------------+

Vibrate on the hour, when enabled.  (Enabled by default.)

Used "Simplicity" watchface as a guide, but I want the day of the week.

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

#define BATTERY_X DRAW_INSET
#define BATTERY_Y 145 // LOWER_TIME_Y + LOWER_TIME_HEIGHT + FONT_PAD_Y
#define BATTERY_WIDTH 68 // DRAW_WIDTH / 2
#define BATTERY_HEIGHT 31 // SMALL_FONT_HEIGHT (can get away with less b/c no descending characters)

#define TEMPERATURE_X BATTERY_WIDTH
#define TEMPERATURE_Y BATTERY_Y
#define TEMPERATURE_WIDTH BATTERY_WIDTH
#define TEMPERATURE_HEIGHT BATTERY_HEIGHT

#define VIBRATE_HOURLY 1 // Change to 0 to disable

#define ALL_TIME_CHANGED (SECOND_UNIT | MINUTE_UNIT | HOUR_UNIT | DAY_UNIT | MONTH_UNIT | YEAR_UNIT)

typedef enum {
	CHARGE_PERCENT_FIELD = 0x1,
	IS_CHARGING_FIELD    = 0x2,
	IS_PLUGGED_FIELD     = 0x4,
} BatteryFields;

#define ALL_BATTERY_CHANGED (CHARGE_PERCENT_FIELD | IS_CHARGING_FIELD | IS_PLUGGED_FIELD)

// Message values
#define INBOUND_MESSAGE_SIZE 64
#define OUTBOUND_MESSAGE_SIZE 16

typedef struct {
	uint8_t icon_index;
	const char* temperature_str;
} Weather;

typedef enum {
	WEATHER_ICON_FIELD = 0x1,
	WEATHER_TEMPERATURE_FIELD= 0x2,
} WeatherFields;

#define ALL_WEATHER_CHANGED (WEATHER_ICON_FIELD | WEATHER_TEMPERATURE_FIELD)

typedef enum {
	WEATHER_MESSAGE_ICON = 0x0, // TUPLE_UINT
	WEATHER_MESSAGE_TEMPERATURE = 0x1, // TUPLE_CSTRING
} WeatherMessageIndex;


Window* window;

TextLayer* text_date_layer;
TextLayer* text_time_layer;
TextLayer* text_lower_date_layer;
TextLayer* text_lower_time_layer;
TextLayer* text_battery_layer;
TextLayer* text_temperature_layer;

// For getting information from the companion app on the phone.
AppSync sync;
uint8_t sync_buffer[32];

GFont font_21;
GFont font_49_numbers;

const VibePattern HOUR_VIBE_PATTERN = {
  .durations = (uint32_t []) {50, 200, 50, 200, 50, 200},
  .num_segments = 6
};

void draw_screen(struct tm* ptime, TimeUnits units_changed,
		 BatteryChargeState charge_state, BatteryFields battery_changed,
		 Weather weather, WeatherFields weather_changed) {
	// Buffers we can format text into.
	// Need to be static because they're used by the text layers
	// even after this function completes.
	static char time_text[]  = "00:00";
	static char date_text[]  = "Xxxxxxxxxx"; // Wednesday
	static char lower_date[] = "0000-00-00"; // 2013-10-02
	static char lower_time[] = "XX"; // AM
	static char battery_text[] = "000% c"; // 0% - 100% c for "charging"
	static char temperature_text[] = "0000pC"; // temperature

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

	// Update the battery text, if it changed.
	// XXX Change from text to a drawing!
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "battery_changed=%x percent=%d %s charging?=%d\n", battery_changed, charge_state.charge_percent, battery_text, charge_state.is_charging);
	if ((battery_changed & CHARGE_PERCENT_FIELD) ||
	    (battery_changed & IS_CHARGING_FIELD)) {
		snprintf(battery_text, sizeof(battery_text), "%3d%% %c", charge_state.charge_percent,
			 ((battery_changed & IS_CHARGING_FIELD) && charge_state.is_charging)? 'c': ' ');
	}
	// XXX If drawing the other fields (charging/plugged), test and draw those.
	if (battery_changed) {
		text_layer_set_text(text_battery_layer, battery_text);
	}

	// Update the temperature text, if it changed.
	// XXX update to include graphics!
	if (weather_changed & WEATHER_TEMPERATURE_FIELD) {
		snprintf(temperature_text, sizeof(temperature_text), "%s", weather.temperature_str);
	}
	if (weather_changed) {
		text_layer_set_text(text_temperature_layer, temperature_text);
	}
}

static void sync_error_callback(DictionaryResult dict_err, AppMessageResult app_msg_err, void* context) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "App message sync error: %d", app_msg_err);

#define case_log_enum(e) case e: APP_LOG(APP_LOG_LEVEL_DEBUG, #e); break

	switch (app_msg_err) {
		case_log_enum(APP_MSG_OK);
		case_log_enum(APP_MSG_SEND_TIMEOUT);
		case_log_enum(APP_MSG_SEND_REJECTED);
		case_log_enum(APP_MSG_NOT_CONNECTED);
		case_log_enum(APP_MSG_APP_NOT_RUNNING);
		case_log_enum(APP_MSG_INVALID_ARGS);
		case_log_enum(APP_MSG_BUSY);
		case_log_enum(APP_MSG_BUFFER_OVERFLOW);
		case_log_enum(APP_MSG_ALREADY_RELEASED);
		case_log_enum(APP_MSG_CALLBACK_ALREADY_REGISTERED);
		case_log_enum(APP_MSG_CALLBACK_NOT_REGISTERED);
		case_log_enum(APP_MSG_OUT_OF_MEMORY);
		case_log_enum(APP_MSG_CLOSED);
		case_log_enum(APP_MSG_INTERNAL_ERROR);
	default:
		break;
	};
}

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_values, const Tuple* old_values, void* context) {
	Weather w;
	WeatherFields wfields = 0;

	switch (key) {
	case WEATHER_MESSAGE_ICON:
		if (new_values && new_values->value) {
			wfields |= WEATHER_ICON_FIELD;
			w.icon_index = new_values->value->uint8;
		}

		if (old_values && new_values && old_values->value && new_values->value) {
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Icon changed: old=%d new=%d",
				old_values->value->uint8,
				new_values->value->uint8);
		}
		break;
	case WEATHER_MESSAGE_TEMPERATURE:
		// new_values are stored in the global sync_buffer, so we can use it
		// directly.  draw_screen will make a local copy for the text layer anyway.
		// directly in the text layer
		if (new_values && new_values->value) {
			wfields |= WEATHER_TEMPERATURE_FIELD;
			w.temperature_str = new_values->value->cstring;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Temperature changed: ");
		}
		break;
	};

	time_t now = time(NULL);

	draw_screen(localtime(&now), 0, battery_state_service_peek(), 0, w, wfields);
}

static void window_load(Window* win) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	// XXX TODO Base width/height on actual frame bounds.
	// Eg bounds.size.w for width of frame

	// Date layer
	text_date_layer = text_layer_create((GRect) { .origin = { SCREEN_DATE_X, SCREEN_DATE_Y },
				.size = { SCREEN_DATE_WIDTH, SCREEN_DATE_HEIGHT } });
	text_layer_set_text_alignment(text_date_layer, GTextAlignmentCenter);
	text_layer_set_text_color(text_date_layer, GColorWhite);
	text_layer_set_background_color(text_date_layer, GColorClear);
	//text_layer_set_font(text_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
	text_layer_set_font(text_date_layer, font_21);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_date_layer));

	// Time layer
	text_time_layer = text_layer_create((GRect) { .origin = { SCREEN_TIME_X, SCREEN_TIME_Y },
				.size = { SCREEN_TIME_WIDTH, SCREEN_TIME_HEIGHT } });
	text_layer_set_text_alignment(text_time_layer, GTextAlignmentCenter);
	text_layer_set_text_color(text_time_layer, GColorWhite);
	text_layer_set_background_color(text_time_layer, GColorClear);
//	text_layer_set_font(text_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
	text_layer_set_font(text_time_layer, font_49_numbers);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_time_layer));

	// Lower date layer
	text_lower_date_layer = text_layer_create((GRect) { .origin = { LOWER_DATE_X, LOWER_DATE_Y },
				.size = { LOWER_DATE_WIDTH, LOWER_DATE_HEIGHT } });
	text_layer_set_text_color(text_lower_date_layer, GColorWhite);
	text_layer_set_text_alignment(text_lower_date_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_lower_date_layer, GColorClear);
//	text_layer_set_font(text_lower_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
	text_layer_set_font(text_lower_date_layer, font_21);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_lower_date_layer));

	// Lower text layer
	text_lower_time_layer = text_layer_create((GRect) { .origin = { LOWER_TIME_X, LOWER_TIME_Y },
				.size = { LOWER_TIME_WIDTH, LOWER_TIME_HEIGHT } });
	text_layer_set_text_color(text_lower_time_layer, GColorWhite);
	text_layer_set_text_alignment(text_lower_time_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_lower_time_layer, GColorClear);
//	text_layer_set_font(text_lower_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
	text_layer_set_font(text_lower_time_layer, font_21);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_lower_time_layer));

	// Battery layer
	text_battery_layer = text_layer_create((GRect) { .origin = { BATTERY_X, BATTERY_Y },
				.size = { BATTERY_WIDTH, BATTERY_HEIGHT } });
	text_layer_set_text_color(text_battery_layer, GColorWhite);
	text_layer_set_text_alignment(text_battery_layer, GTextAlignmentLeft);
	text_layer_set_background_color(text_battery_layer, GColorClear);
	text_layer_set_font(text_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_battery_layer));

	// Temperature layer
	text_temperature_layer = text_layer_create((GRect) { .origin = { TEMPERATURE_X, TEMPERATURE_Y },
				.size = { TEMPERATURE_WIDTH, TEMPERATURE_HEIGHT } });
	text_layer_set_text_color(text_temperature_layer, GColorWhite);
	text_layer_set_text_alignment(text_temperature_layer, GTextAlignmentRight);
	text_layer_set_background_color(text_temperature_layer, GColorClear);
	text_layer_set_font(text_temperature_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));

	Tuplet initial_temperature_values[] = {
		TupletInteger(WEATHER_MESSAGE_ICON, (uint8_t) 1),
		TupletCString(WEATHER_MESSAGE_TEMPERATURE, "0000\u00B0C"),
	};
	app_sync_init(&sync, sync_buffer, sizeof(sync_buffer),
		      initial_temperature_values, ARRAY_LENGTH(initial_temperature_values),
		      sync_tuple_changed_callback, sync_error_callback, NULL);

	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_temperature_layer));

}

static void window_appear(Window* win) {
	// Update here to avoid blank display on launch
	// and to update when the window is redrawn.
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	time_t now = time(NULL);
	Weather w;
	draw_screen(localtime(&now), ALL_TIME_CHANGED,
		    battery_state_service_peek(), ALL_BATTERY_CHANGED,
		    w, 0);
}

static void window_unload(Window *win) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	app_sync_deinit(&sync);

	text_layer_destroy(text_temperature_layer);
	text_layer_destroy(text_battery_layer);
	text_layer_destroy(text_lower_time_layer);
	text_layer_destroy(text_lower_date_layer);
	text_layer_destroy(text_time_layer);
	text_layer_destroy(text_date_layer);
}

void handle_minute_tick(struct tm* tick_time, TimeUnits units_changed) {
	// Assume the battery didn't change.  If it did, the battery update
	// function will redraw the screen.
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	Weather w;
	draw_screen(tick_time, units_changed,
		    battery_state_service_peek(), 0,
		    w, 0);
}

void handle_battery_update(BatteryChargeState charge_state) {
	// Get the current time so we have something to pass,
	// then say that nothing changed: let the tick handler
	// draw with any updates.
	// Ugh, can't take address of the temporary returned by time(NULL)
	// to pass to localtime. :P
	// XXX Could be smarter and save the battery state and compare, only
	// setting the field flags as they change.  When I actually use the other
	// fields, do that.
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	time_t now = time(NULL);
	Weather w;
	draw_screen(localtime(&now), (TimeUnits) 0, charge_state, ALL_BATTERY_CHANGED, w, 0);
}

void do_init() {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	// Load the fonts before anything in the window functions
	// tries to use them.
	font_21 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_21));
	font_49_numbers = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UBUNTU_B_SUBSET_49));

	window = window_create();
	// XXX This seems to be more for apps that load and unload windows a lot,
	// not really for watchfaces, so consider changing this to just do the
	// text layer creation here.
	window_set_window_handlers(window, (WindowHandlers) {
				.load = window_load,
				.appear = window_appear,
			  	.unload = window_unload,
			  });
	window_stack_push(window, true /* Animated */);
	window_set_background_color(window, GColorBlack);

	tick_timer_service_subscribe(MINUTE_UNIT, &handle_minute_tick);
	battery_state_service_subscribe(&handle_battery_update);

	app_message_open(INBOUND_MESSAGE_SIZE, OUTBOUND_MESSAGE_SIZE);
}

void do_deinit(void) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	tick_timer_service_unsubscribe();
	battery_state_service_unsubscribe();

	// XXX TODO Release other layers?
	//window_unload(window) should have been called to destroy layers?
	window_destroy(window);

	fonts_unload_custom_font(font_49_numbers);
	fonts_unload_custom_font(font_21);
}

int main(void) {
	do_init();
	app_event_loop();
	do_deinit();

	return 0;
}
