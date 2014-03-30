/*
Watch face with date and time like I like them.
General layout is below (not to scale).
Used "Simplicity" watchface as a guide, but I want the day of the week.

+------------+
|   Sunday   |
| 2013-10-06 |
|            |
|   09:23    | <<< big font
|     AM     | <<< only in 12-hour mode
| w:--  p:-- | <<< battery indicator for watch and phone, temperature (from phone app)
+------------+

- Vibrate on the hour, when enabled.  (Enabled by default.)
- Vibrate and display graphic when Bluetooth disconnected.

*/

#include <pebble.h>
#include <time.h>

// ---------- Screen Locations ------------------------------

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
#define SCREEN_DATE_DOW_X DRAW_INSET
#define SCREEN_DATE_DOW_Y 3
#define SCREEN_DATE_DOW_WIDTH  DRAW_WIDTH
#define SCREEN_DATE_DOW_HEIGHT 26 // SMALL_FONT_HEIGHT + FONT_PAD_Y + 4 to make room for descent of "y" in "Thursday"

#define SCREEN_TIME_X DRAW_INSET
#define SCREEN_TIME_Y 59 // SCREEN_MIDDLE_Y - LARGE_FONT_HEIGHT/2
#define SCREEN_TIME_WIDTH  DRAW_WIDTH
#define SCREEN_TIME_HEIGHT 51 // LARGE_FONT_HEIGHT + FONT_PAD_Y

#define SCREEN_DATE_X DRAW_INSET
#define SCREEN_DATE_Y 28 // SCREEN_DATE_Y + SCREEN_DATE_HEIGHT
#define SCREEN_DATE_WIDTH  SCREEN_DATE_DOW_WIDTH
#define SCREEN_DATE_HEIGHT 31 // SMALL_FONT_HEIGHT + FONT_PAD_Y

#define SCREEN_TIME_AMPM_X 32 // DRAW_INSET + 32
#define SCREEN_TIME_AMPM_Y 112 // SCREEN_TIME_Y + SCREEN_TIME_HEIGHT + FONT_PAD_Y
#define SCREEN_TIME_AMPM_WIDTH  121 // SCREEN_TIME_WIDTH - 15 (for inset)
#define SCREEN_TIME_AMPM_HEIGHT 31 // SMALL_FONT_HEIGHT

#define BLUETOOTH_WARN_X DRAW_INSET
#define BLUETOOTH_WARN_Y 114 // SCREEN_TIME_AMPM_Y + FONT_PAD_Y
#define BLUETOOTH_WARN_WIDTH  32
#define BLUETOOTH_WARN_HEIGHT 32

#define WATCH_BATTERY_X DRAW_INSET
#define WATCH_BATTERY_Y 145 // SCREEN_TIME_AMPM_Y + SCREEN_TIME_AMPM_HEIGHT + FONT_PAD_Y
#define WATCH_BATTERY_WIDTH 48 // DRAW_WIDTH / 3
#define WATCH_BATTERY_HEIGHT 31 // SMALL_FONT_HEIGHT (can get away with less b/c no descending characters)

#define PHONE_BATTERY_X WATCH_BATTERY_WIDTH
#define PHONE_BATTERY_Y WATCH_BATTERY_Y
#define PHONE_BATTERY_WIDTH WATCH_BATTERY_WIDTH
#define PHONE_BATTERY_HEIGHT WATCH_BATTERY_HEIGHT

#define TEMPERATURE_X 96 // WATCH_BATTERY_WIDTH + PHONE_BATTERY_WIDTH
#define TEMPERATURE_Y PHONE_BATTERY_Y
#define TEMPERATURE_WIDTH PHONE_BATTERY_WIDTH
#define TEMPERATURE_HEIGHT PHONE_BATTERY_HEIGHT

// ---------- Options and vibes ------------------------------

#define VIBRATE_HOURLY 1 // Change to 0 to disable

// After losing bluetooth, the wait before the vibe/display is sent.
// This should stop it from alerting on very short drops in bluetooth.
#define BLUETOOTH_TIMEOUT_MS 5000

// dit-dit-dit-dit = "H" in morse code. :)
const VibePattern HOUR_VIBE_PATTERN = {
  .durations = (uint32_t []) {50, 200, 50, 200, 50, 200, 50, 200},
  .num_segments = 8
};

// dah-dit-dit-dit = "B" in morse code. :)
const VibePattern BLUETOOTH_WARN_VIBE_PATTERN = {
	.durations = (uint32_t []) {200, 200, 50, 200, 50, 200, 50, 200},
	.num_segments = 8
};

// ---------- Messages ------------------------------

typedef enum {
	PHONE_BATTERY_PERCENT = 0, // TUPLE_UINT
	PHONE_BATTERY_CHARGING = 1, // TUPLE_UINT
	PHONE_BATTERY_PLUGGED = 2, // TUPLE_UINT
	WEATHER_MESSAGE_ICON = 3, // TUPLE_UINT
	WEATHER_MESSAGE_TEMPERATURE = 4, // TUPLE_UINT
} GTTMessageIndex; // GotTheTime App Message indexes

#define INBOUND_MESSAGE_SIZE 64
#define OUTBOUND_MESSAGE_SIZE 64

// For getting information from the companion app on the phone.
AppSync sync;
uint8_t sync_buffer[64];

typedef struct {
	uint8_t icon;
	int32_t temp;
} WeatherInfo;

// Last known state
BatteryChargeState phone_battery_state;
WeatherInfo weather_info;

// ---------- Graphics layers and fonts ------------------------------

Window* window;

TextLayer* text_date_dow_layer;
TextLayer* text_date_layer;
TextLayer* text_time_layer;
TextLayer* text_time_ampm_layer;
TextLayer* text_battery_watch_layer;
TextLayer* text_battery_phone_layer;
TextLayer* text_temperature_layer;

BitmapLayer* bitmap_bluetooth_layer;
GBitmap* bitmap_bluetooth_warn;

GFont font_21;
GFont font_49_numbers;

// ---------- Drawing functions ------------------------------

void draw_dayofweek(struct tm* ptime) {
	static char day_text[]  = "Xxxxxxxxxx"; // Wednesday

	// Day of the week, full name
	strftime(day_text, sizeof(day_text), "%A", ptime);
	text_layer_set_text(text_date_dow_layer, day_text);
}

void draw_date(struct tm* ptime) {
	static char date_text[] = "0000-00-00"; // 2013-10-02

	// Date
	strftime(date_text, sizeof(date_text), "%Y-%m-%d", ptime);
	text_layer_set_text(text_date_layer, date_text);
}

void draw_time(struct tm* ptime) {
	// Although this makes it different from the other "per line"
	// drawing functions, the time is all related, so update all
	// the time lines in the same function.
	static char time_text[]  = "00:00";
	static char ampm[] = "XX"; // AM
	char* time_format = NULL;

	// Time, not including seconds
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

	if (VIBRATE_HOURLY && (ptime->tm_min == 0)) {
		vibes_enqueue_custom_pattern(HOUR_VIBE_PATTERN);
	}

	// Set the AM/PM, if needed.
	if (!clock_is_24h_style()) {
		strftime(ampm, sizeof(ampm), "%p", ptime);
		text_layer_set_text(text_time_ampm_layer, ampm);
	}
}

void draw_bluetooth_warning(bool connected) {
	// If connected, clear the area (or, hide the layer).
	// If disconnected, show the graphic/layer.
	layer_set_hidden((Layer*) bitmap_bluetooth_layer, connected);

	if (!connected) {
		vibes_enqueue_custom_pattern(BLUETOOTH_WARN_VIBE_PATTERN);
	}
}

void battery_text_common(BatteryChargeState batt, char* text, uint16_t text_len) {
	char charging = (batt.is_charging)? 'c': ' ';
	if (batt.is_plugged) {
		charging = 'p';
	}
	snprintf(text, text_len, "%3d%%%c", batt.charge_percent, charging);
}

void draw_battery_watch(BatteryChargeState batt) {
	static char watch_battery_text[] = "000%c"; // 0% - 100% c for "charging"

	battery_text_common(batt, watch_battery_text, sizeof(watch_battery_text));
	text_layer_set_text(text_battery_watch_layer, watch_battery_text);
}

void draw_battery_phone(BatteryChargeState batt) {
	static char phone_battery_text[] = "000%c"; // 0% - 100% c for "charging"

	battery_text_common(batt, phone_battery_text, sizeof(phone_battery_text));
	text_layer_set_text(text_battery_phone_layer, phone_battery_text);
}

void draw_weather(WeatherInfo winfo) {
	static char temperature_text[] = "000%C"; // temperature

	snprintf(temperature_text, sizeof(temperature_text), "%3d\u00B0", (int) winfo.temp);
	text_layer_set_text(text_temperature_layer, temperature_text);
}


// ---------- Message functions ------------------------------

static void sync_error_callback(DictionaryResult dict_err, AppMessageResult app_msg_err, void* context) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s %d", __FUNCTION__, app_msg_err);

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

static void send_message(void) {
	// Empty message to send, just to trigger the JS/phone to do something.
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	Tuplet value = TupletInteger(1, 1);

	DictionaryIterator *iter;
	AppMessageResult res = app_message_outbox_begin(&iter);
	DictionaryResult dres = DICT_OK;
	sync_error_callback(dres, res, NULL);

	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "NO OUTBOX ITER!");
		return;
	}

	dict_write_tuplet(iter, &value);
	dict_write_end(iter);

	app_message_outbox_send();
}

static void send_message_callback(void* ignored) {
	send_message();
}

static void sync_tuple_changed_callback(const uint32_t key,
					const Tuple* new_values,
					const Tuple* old_values,
					void* context)
{
	bool update_battery = false;
	bool update_weather = false;

	switch (key) {

	case PHONE_BATTERY_PERCENT:
		if (new_values && new_values->value) {
			phone_battery_state.charge_percent = new_values->value->uint8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "%s PHONE_BATTERY_PERCENT: %d",
				__FUNCTION__, phone_battery_state.charge_percent);
			update_battery = true;
		}
		break;
	case PHONE_BATTERY_CHARGING:
		if (new_values && new_values->value) {
			phone_battery_state.is_charging = new_values->value->uint8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "%s PHONE_BATTERY_CHARGING: %d",
				__FUNCTION__, phone_battery_state.is_charging);
			update_battery = true;
		}
		break;
	case PHONE_BATTERY_PLUGGED:
		if (new_values && new_values->value) {
			phone_battery_state.is_plugged = new_values->value->uint8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "%s PHONE_BATTERY_PLUGGED: %d",
				__FUNCTION__, phone_battery_state.is_plugged);
			update_battery = true;
		}
		break;

	case WEATHER_MESSAGE_ICON:
		// XXX Not actually using this yet
		if (new_values && new_values->value) {
			weather_info.icon = new_values->value->uint8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "%s WEATHER_MESSAGE_ICON: %d",
				__FUNCTION__, weather_info.icon);
			update_weather = true;
		}
		break;
	case WEATHER_MESSAGE_TEMPERATURE:
		if (new_values && new_values->value) {
			weather_info.temp = new_values->value->uint8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "%s WEATHER_MESSAGE_TEMPERATURE: %d",
				__FUNCTION__, (int) weather_info.temp);
			update_weather = true;
		}
		break;

	};

	if (update_battery) {
		draw_battery_phone(phone_battery_state);
	}

	if (update_weather) {
		draw_weather(weather_info);
	}
}


// ---------- Timer and watch update functions ------------------------------

void handle_minute_tick(struct tm* tick_time, TimeUnits units_changed) {

	// If the month or year changes, the day will change, too.
	if (units_changed & DAY_UNIT) {
		draw_dayofweek(tick_time);
		draw_date(tick_time);
	}
	draw_time(tick_time);
}

void handle_battery_update(BatteryChargeState charge_state) {
	draw_battery_watch(charge_state);
}

void bluetooth_timer_callback(void* ignored) {
	draw_bluetooth_warning(bluetooth_connection_service_peek());
}

void handle_bluetooth_update(bool connected)
{
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s %s", __FUNCTION__, (connected? "true": "false"));

	if (connected) {
		draw_bluetooth_warning(bluetooth_connection_service_peek());
	}
	else {
		// Don't show/buzz right away, wait for a few seconds.
		// I'm not being smart here and cancelling any in-flight
		// timers in the case we're flapping the connect/disconnect.
		app_timer_register(BLUETOOTH_TIMEOUT_MS,
				   bluetooth_timer_callback,
				   NULL);
	}
}


// ---------- Window functions ------------------------------

static void window_load(Window* win) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	// XXX TODO Base width/height on actual frame bounds.
	// Eg bounds.size.w for width of frame

	// Day of week layer
	text_date_dow_layer = text_layer_create((GRect) { .origin = { SCREEN_DATE_DOW_X, SCREEN_DATE_DOW_Y },
				.size = { SCREEN_DATE_DOW_WIDTH, SCREEN_DATE_DOW_HEIGHT } });
	text_layer_set_text_color(text_date_dow_layer, GColorWhite);
	text_layer_set_text_alignment(text_date_dow_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_date_dow_layer, GColorClear);
	text_layer_set_font(text_date_dow_layer, font_21);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_date_dow_layer));

	// Date layer
	text_date_layer = text_layer_create((GRect) { .origin = { SCREEN_DATE_X, SCREEN_DATE_Y },
				.size = { SCREEN_DATE_WIDTH, SCREEN_DATE_HEIGHT } });
	text_layer_set_text_alignment(text_date_layer, GTextAlignmentCenter);
	text_layer_set_text_color(text_date_layer, GColorWhite);
	text_layer_set_background_color(text_date_layer, GColorClear);
	text_layer_set_font(text_date_layer, font_21);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_date_layer));

	// Time layer
	text_time_layer = text_layer_create((GRect) { .origin = { SCREEN_TIME_X, SCREEN_TIME_Y },
				.size = { SCREEN_TIME_WIDTH, SCREEN_TIME_HEIGHT } });
	text_layer_set_text_alignment(text_time_layer, GTextAlignmentCenter);
	text_layer_set_text_color(text_time_layer, GColorWhite);
	text_layer_set_background_color(text_time_layer, GColorClear);
	text_layer_set_font(text_time_layer, font_49_numbers);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_time_layer));


	// AM/PM layer
	text_time_ampm_layer = text_layer_create((GRect) { .origin = { SCREEN_TIME_AMPM_X, SCREEN_TIME_AMPM_Y },
				.size = { SCREEN_TIME_AMPM_WIDTH, SCREEN_TIME_AMPM_HEIGHT } });
	text_layer_set_text_color(text_time_layer, GColorWhite);
	text_layer_set_text_alignment(text_time_ampm_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_time_ampm_layer, GColorClear);
	text_layer_set_font(text_time_ampm_layer, font_21);
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_time_ampm_layer));

	// Watch Battery layer
	text_battery_watch_layer = text_layer_create((GRect) { .origin = { WATCH_BATTERY_X, WATCH_BATTERY_Y },
				.size = { WATCH_BATTERY_WIDTH, WATCH_BATTERY_HEIGHT } });
	text_layer_set_text_color(text_battery_watch_layer, GColorWhite);
	text_layer_set_text_alignment(text_battery_watch_layer, GTextAlignmentLeft);
	text_layer_set_background_color(text_battery_watch_layer, GColorClear);
	text_layer_set_font(text_battery_watch_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_battery_watch_layer));

	// Phone Battery layer
	text_battery_phone_layer = text_layer_create((GRect) { .origin = { PHONE_BATTERY_X, PHONE_BATTERY_Y },
				.size = { PHONE_BATTERY_WIDTH, PHONE_BATTERY_HEIGHT } });
	text_layer_set_text_color(text_battery_phone_layer, GColorWhite);
	text_layer_set_text_alignment(text_battery_phone_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_battery_phone_layer, GColorClear);
	text_layer_set_font(text_battery_phone_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_battery_phone_layer));

	// Temperature layer
	text_temperature_layer = text_layer_create((GRect) { .origin = { TEMPERATURE_X, TEMPERATURE_Y },
				.size = { TEMPERATURE_WIDTH, TEMPERATURE_HEIGHT } });
	text_layer_set_text_color(text_temperature_layer, GColorWhite);
	text_layer_set_text_alignment(text_temperature_layer, GTextAlignmentRight);
	text_layer_set_background_color(text_temperature_layer, GColorClear);
	text_layer_set_font(text_temperature_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_temperature_layer));

	// Bluetooth warning layer
	bitmap_bluetooth_warn = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_NO_BLUETOOTH);
	bitmap_bluetooth_layer = bitmap_layer_create((GRect) { .origin = { BLUETOOTH_WARN_X, BLUETOOTH_WARN_Y },
				.size = { BLUETOOTH_WARN_WIDTH, BLUETOOTH_WARN_HEIGHT } });
	bitmap_layer_set_bitmap(bitmap_bluetooth_layer, bitmap_bluetooth_warn);
	bitmap_layer_set_alignment(bitmap_bluetooth_layer, GAlignCenter);
	layer_add_child(window_get_root_layer(win),
			bitmap_layer_get_layer(bitmap_bluetooth_layer));
}

static void window_appear(Window* win) {
	// Update here to avoid blank display on launch
	// and to update when the window is redrawn.
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	time_t now = time(NULL);
	struct tm* ptime = localtime(&now);

	// Draw all the (local) things!
	draw_dayofweek(ptime);
	draw_date(ptime);
	draw_time(ptime);
	draw_battery_watch(battery_state_service_peek());

	// Draw the last known state of the phone information.
	draw_battery_phone(phone_battery_state);
	draw_weather(weather_info);

	// If bluetooth is connected (the normal state of things),
	// don't show the layer yet.
	layer_set_hidden((Layer*) bitmap_bluetooth_layer, bluetooth_connection_service_peek());

	Tuplet initial_message_values[] = {
		TupletInteger(PHONE_BATTERY_PERCENT, (uint8_t) 0),
		TupletInteger(PHONE_BATTERY_CHARGING, (uint8_t) 0),
		TupletInteger(PHONE_BATTERY_PLUGGED, (uint8_t) 0),
		TupletInteger(WEATHER_MESSAGE_ICON, (uint8_t) 0),
		TupletInteger(WEATHER_MESSAGE_TEMPERATURE, (int32_t) 0),
	};
	app_sync_init(&sync, sync_buffer, sizeof(sync_buffer),
		      initial_message_values, ARRAY_LENGTH(initial_message_values),
		      sync_tuple_changed_callback, sync_error_callback, NULL);

	// Let initialization happen, then send the message to get the
	// values from the phone.
	app_timer_register(1000 /* ms */,
			   send_message_callback,
			   NULL);
}

static void window_unload(Window *win) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	app_sync_deinit(&sync);

	gbitmap_destroy(bitmap_bluetooth_warn);
	bitmap_layer_destroy(bitmap_bluetooth_layer);

	text_layer_destroy(text_temperature_layer);
	text_layer_destroy(text_battery_phone_layer);
	text_layer_destroy(text_battery_watch_layer);
	text_layer_destroy(text_time_ampm_layer);
	text_layer_destroy(text_time_layer);
	text_layer_destroy(text_date_layer);
	text_layer_destroy(text_date_dow_layer);
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
	bluetooth_connection_service_subscribe(&handle_bluetooth_update);

	// Init the information from the phone until we have real info.
	memset(&phone_battery_state, 0, sizeof(phone_battery_state));
	memset(&weather_info, 0, sizeof(weather_info));

	app_message_open(INBOUND_MESSAGE_SIZE, OUTBOUND_MESSAGE_SIZE);
}

void do_deinit(void) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	tick_timer_service_unsubscribe();
	battery_state_service_unsubscribe();
	bluetooth_connection_service_unsubscribe();

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
