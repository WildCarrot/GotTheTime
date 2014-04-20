/*
Watch face with date and time like I like them.
General layout is below (not to scale).
Used "Simplicity" watchface as a guide, but I want the day of the week.

+------------+
|w%    B   p%| <<< status
|   Sunday   |
| 2013-10-06 | <<< date area
|            |
|    09:23   | <<< time in big font
|            | <<< time zones, .beats in tiny font
| sun    10d | <<< weather
+------------+

- Vibrate on the hour, when enabled.  (Enabled by default.)
- Vibrate and display graphic when Bluetooth disconnected.

*/

#include <pebble.h>
#include <time.h>

// ---------- Screen Locations ------------------------------
// These are all relative to the base window.
// Individual layers are relative to their parent layer.

/* Seems like these should be defined somewhere. */
#define SCREEN_WIDTH  144
#define SCREEN_HEIGHT 168

#define SCREEN_MIDDLE_X 72 /* SCREEN_WIDTH / 2 */
#define SCREEN_MIDDLE_Y 84 /* SCREEN_HEIGHT / 2 */

#define DRAW_INSET 0
#define DRAW_WIDTH 144 // SCREEN_WIDTH - DRAW_INSET

#define SMALL_FONT_HEIGHT 21
#define LARGE_FONT_HEIGHT 49
#define FONT_PAD_Y 2


/* Status area */
#define STATUS_X DRAW_INSET
#define STATUS_Y DRAW_INSET
#define STATUS_WIDTH  DRAW_WIDTH
#define STATUS_HEIGHT 16 // Just some small graphics here

/* Date area */
#define DATE_X DRAW_INSET
#define DATE_Y 16 // STATUS_Y + STATUS_HEIGHT
#define DATE_WIDTH  DRAW_WIDTH
#define DATE_HEIGHT 50 // 2 * SMALL_FONT_HEIGHT + 2 * FONT_PAD_Y + 4 for the descent of "y"

/* Time area */
#define TIME_X DRAW_INSET
#define TIME_Y 68 // DATE_Y + DATE_HEIGHT + FONT_PAD_Y
#define TIME_WIDTH  DRAW_WIDTH
#define TIME_HEIGHT 74 // LARGE_FONT_HEIGHT + SMALL_FONT_HEIGHT + 2 * FONT_PAD_Y

/* Weather area */
#define WEATHER_X DRAW_INSET
#define WEATHER_Y 142 // TIME_Y + TIME_HEIGHT
#define WEATHER_WIDTH  DRAW_WIDTH
#define WEATHER_HEIGHT 26 // SCREEN_HEIGHT - DRAW_INSET - WEATHER_Y

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

// Weather Icon codes are here:
// http://bugs.openweathermap.org/projects/api/wiki/Weather_Condition_Codes
typedef enum {
        WEATHER_ICON_NONE = 0,
	WEATHER_ICON_RAIN = 1,  // 200 - 500
	WEATHER_ICON_SNOW = 2,  // 600
	WEATHER_ICON_SUN = 3,   // 800, 801
	WEATHER_ICON_CLOUD = 4, // 802-804
} WeatherIconCode;

static uint32_t WEATHER_ICONS[] = {
        RESOURCE_ID_IMAGE_WEATHER_NONE,
	RESOURCE_ID_IMAGE_WEATHER_RAIN,
	RESOURCE_ID_IMAGE_WEATHER_SNOW,
	RESOURCE_ID_IMAGE_WEATHER_SUN,
	RESOURCE_ID_IMAGE_WEATHER_CLOUD,
};

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

Layer* status_layer;  // Battery and bluetooth status
Layer* status_watch_battery_layer;
Layer* status_phone_battery_layer;
TextLayer* status_bluetooth_warn_layer;

Layer* date_layer;    // Date and day of week
TextLayer* date_dow_layer;
TextLayer* date_text_layer;

Layer* time_layer;    // Time
TextLayer* time_text_layer;
TextLayer* time_tz1_text_layer;
TextLayer* time_tz2_text_layer;
TextLayer* time_beats_text_layer;

Layer* weather_layer; // Weather info
BitmapLayer* weather_cond_layer;
GBitmap* weather_cond_bitmap;
TextLayer* weather_temp_layer;

GFont font_21;
GFont font_49_numbers;

// ---------- Drawing functions ------------------------------

void draw_dayofweek(struct tm* ptime) {
	static char day_text[]  = "Xxxxxxxxxx"; // Wednesday

	// Day of the week, full name
	strftime(day_text, sizeof(day_text), "%A", ptime);
	text_layer_set_text(date_dow_layer, day_text);
}

void draw_date(struct tm* ptime) {
	static char date_text[] = "0000-00-00"; // 2013-10-02

	// Date
	strftime(date_text, sizeof(date_text), "%Y-%m-%d", ptime);
	text_layer_set_text(date_text_layer, date_text);
}

void draw_one_time(struct tm* ptime, char* s, const uint8_t slen, TextLayer* tlayer) {
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
	strftime(s, slen, time_format, ptime);

	// Hack to remove a leading zero or space for 12-hour times.
	if (!clock_is_24h_style()) {
		memmove(s, &s[1], slen - 1);
	}

	text_layer_set_text(tlayer, s);
}

int compute_beats(struct tm* utc_time) {
	// This is the floor, not rounded down.  Not yet sure if it matters.
	return ((utc_time->tm_sec + (utc_time->tm_min * 60) +
		 (utc_time->tm_hour * 3600)) / 86.4);
}

void draw_beats_time(struct tm* utc_time, char* s, const uint8_t slen, TextLayer* tlayer) {
	int beats = compute_beats(utc_time);

	snprintf(s, slen, "@%03d", beats);

	text_layer_set_text(tlayer, s);
}

void draw_time(struct tm* ptime) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	static char time_text[] = "00:00";
	static char tz1_text[]  = "00:00";
	static char tz2_text[]  = "00:00";
	static char beats_text[]= "@000";

	// Although this makes it different from the other "per line"
	// drawing functions, the time is all related, so update all
	// the time lines in the same function.

	// Local time
	draw_one_time(ptime, time_text, sizeof(time_text), time_text_layer);

	// XXX Since there's no good way to get UTC time from the watch
	//     we have to fake other timezones.  This will break if I
	//     ever travel or for daylight savings time. :(
	time_t tz_t;
	time(&tz_t);  // This is local to the current timezone.

	// Additional time zone 1
	time_t tz1_t = tz_t + (-3 * 60 * 60); // Pacific relative to my timezone
	struct tm* tz1_time = gmtime(&tz1_t);
	draw_one_time(tz1_time, tz1_text, sizeof(tz1_text), time_tz1_text_layer);

	// Additional time zone 2
	time_t tz2_t = tz_t + (+6 * 60 * 60);
	struct tm* tz2_time = gmtime(&tz2_t); // Central Europe relative to my timezone
	draw_one_time(tz2_time, tz2_text, sizeof(tz2_text), time_tz2_text_layer);

	// Beats time
	// No daylight savings time in .beats.  It's normally UTC+1 that's the
	// basis for .beats, but we're in DST now, so it should just be UTC.
	time_t utc_t = tz_t + (+5 * 60 * 60);
	struct tm* utc_time = gmtime(&utc_t);
	draw_beats_time(utc_time, beats_text, sizeof(beats_text), time_beats_text_layer);

	if (VIBRATE_HOURLY && (ptime->tm_min == 0)) {
		vibes_enqueue_custom_pattern(HOUR_VIBE_PATTERN);
	}
}

void draw_bluetooth_warning(bool connected) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	static char blue_text[] = "B!";

	snprintf(blue_text, sizeof(blue_text), "%s",
		 (connected? "": "B!"));
	text_layer_set_text(status_bluetooth_warn_layer, blue_text);

	if (!connected) {
		vibes_enqueue_custom_pattern(BLUETOOTH_WARN_VIBE_PATTERN);
	}
}

void draw_battery_common(Layer* layer, GContext* ctx, BatteryChargeState batt) {
	// Inset the battery a few pixels.
	GRect batt_rect = layer_get_bounds(layer);
	// XXX Trying to make this look good, but it's hard.
	batt_rect.origin.x += 12;
	batt_rect.origin.y += 2;
	batt_rect.size.w -= 12;
	batt_rect.size.h -= 2;

	// Draw the outline
	graphics_context_set_stroke_color(ctx, GColorWhite);
	graphics_context_set_fill_color(ctx, GColorBlack);
	graphics_draw_rect(ctx, batt_rect);

	// Fill to a percent of the box
	graphics_context_set_fill_color(ctx, GColorWhite);
	float percent = batt.charge_percent / 100.0;
	int fill_width = batt_rect.size.w * percent;
	GRect fill_rect = batt_rect;
	fill_rect.size.w = fill_width;

	graphics_fill_rect(ctx, fill_rect, 0, GCornerNone);
}

void draw_battery_watch_callback(Layer* layer, GContext* ctx) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
	draw_battery_common(layer, ctx, battery_state_service_peek());
}

void draw_battery_phone_callback(Layer* layer, GContext* ctx) {
	draw_battery_common(layer, ctx, phone_battery_state);
}

void draw_weather(WeatherInfo winfo) {
	static char temperature_text[] = "000 %C"; // temperature, 2 spaces for unicode degree sign

	snprintf(temperature_text, sizeof(temperature_text), "%3d\u00B0C", (int) winfo.temp);
	text_layer_set_text(weather_temp_layer, temperature_text);

	// If we didn't get an icon, just leave it unchanged.
	if (winfo.icon > 0) {
		if (weather_cond_bitmap) {
			gbitmap_destroy(weather_cond_bitmap);
		}
		weather_cond_bitmap = gbitmap_create_with_resource(WEATHER_ICONS[winfo.icon]);
		bitmap_layer_set_bitmap(weather_cond_layer, weather_cond_bitmap);
	}
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
			weather_info.temp = new_values->value->int8;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "%s WEATHER_MESSAGE_TEMPERATURE: %d",
				__FUNCTION__, (int) weather_info.temp);
			update_weather = true;
		}
		break;

	};

	if (update_battery) {
		layer_mark_dirty(status_phone_battery_layer);
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
	layer_mark_dirty(status_phone_battery_layer);
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


// ---------- Window and layer functions ------------------------------

static void window_load(Window* win) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	// Status layers
	{
		// 3 beside each other:
		// watch batt      bluetooth     phone batt
		GRect status_rect = { .origin = { STATUS_X, STATUS_Y },
				      .size = { STATUS_WIDTH, STATUS_HEIGHT } };
		status_layer = layer_create(status_rect);

		int layer_w = STATUS_WIDTH / 3.0;
		status_watch_battery_layer = layer_create((GRect) { .origin = { 0, 0 },
					.size = { layer_w, STATUS_HEIGHT } });
		status_bluetooth_warn_layer = text_layer_create((GRect) { .origin = { layer_w, 0 },
					.size = { layer_w, STATUS_HEIGHT } });
		status_phone_battery_layer = layer_create((GRect) { .origin = { STATUS_WIDTH - layer_w, 0 },
					.size = { layer_w, STATUS_HEIGHT } });

		layer_set_update_proc(status_watch_battery_layer, draw_battery_watch_callback);
		layer_set_update_proc(status_phone_battery_layer, draw_battery_phone_callback);

		text_layer_set_text_color(status_bluetooth_warn_layer, GColorWhite);
		text_layer_set_text_alignment(status_bluetooth_warn_layer, GTextAlignmentCenter);
		text_layer_set_background_color(status_bluetooth_warn_layer, GColorClear);
		text_layer_set_font(status_bluetooth_warn_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

		layer_add_child(status_layer, status_watch_battery_layer);
		layer_add_child(status_layer, status_phone_battery_layer);
		layer_add_child(status_layer, text_layer_get_layer(status_bluetooth_warn_layer));

		layer_add_child(window_get_root_layer(win), status_layer);
	}

	// Date layers
	{
		// 2 on top of each other:
		// Sunday
		// 2014-03-30
		GRect date_rect = { .origin = { DATE_X, DATE_Y },
				    .size = { DATE_WIDTH, DATE_HEIGHT } };

		date_layer = layer_create(date_rect);

		int layer_h = DATE_HEIGHT / 2.0;
		date_dow_layer = text_layer_create((GRect) { .origin = { 0, 0 },
					.size = { DATE_WIDTH, layer_h } });
		date_text_layer = text_layer_create((GRect) { .origin = { 0, 0 + layer_h },
					.size = { DATE_WIDTH, layer_h } });

		text_layer_set_text_color(date_dow_layer, GColorWhite);
		text_layer_set_text_alignment(date_dow_layer, GTextAlignmentCenter);
		text_layer_set_background_color(date_dow_layer, GColorClear);
		text_layer_set_font(date_dow_layer, font_21);

		text_layer_set_text_color(date_text_layer, GColorWhite);
		text_layer_set_text_alignment(date_text_layer, GTextAlignmentCenter);
		text_layer_set_background_color(date_text_layer, GColorClear);
		text_layer_set_font(date_text_layer, font_21);

		layer_add_child(date_layer, text_layer_get_layer(date_dow_layer));
		layer_add_child(date_layer, text_layer_get_layer(date_text_layer));

		layer_add_child(window_get_root_layer(win), date_layer);
	}

	// Time layers
	{
		// Big time
		GRect time_rect = { .origin = { TIME_X, TIME_Y },
				    .size = { TIME_WIDTH, TIME_HEIGHT } };

		time_layer = layer_create(time_rect);

		int small_time_h = 16;
		int big_time_h = TIME_HEIGHT - small_time_h;

		time_text_layer = text_layer_create((GRect) { .origin = { 0, 0 },
					.size = { TIME_WIDTH, big_time_h } });

		text_layer_set_text_color(time_text_layer, GColorWhite);
		text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
		text_layer_set_background_color(time_text_layer, GColorClear);
		text_layer_set_font(time_text_layer, font_49_numbers);

		int small_time_w = TIME_WIDTH / 3.0;

		time_tz1_text_layer = text_layer_create((GRect) { .origin = { 0, big_time_h },
					.size = { small_time_w, small_time_h } });

		text_layer_set_text_color(time_tz1_text_layer, GColorWhite);
		text_layer_set_text_alignment(time_tz1_text_layer, GTextAlignmentCenter);
		text_layer_set_background_color(time_tz1_text_layer, GColorClear);
		text_layer_set_font(time_tz1_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

		time_beats_text_layer = text_layer_create((GRect) { .origin = { small_time_w, big_time_h },
					.size = { small_time_w, small_time_h } });

		text_layer_set_text_color(time_beats_text_layer, GColorWhite);
		text_layer_set_text_alignment(time_beats_text_layer, GTextAlignmentCenter);
		text_layer_set_background_color(time_beats_text_layer, GColorClear);
		text_layer_set_font(time_beats_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

		time_tz2_text_layer = text_layer_create((GRect) { .origin = { small_time_w*2, big_time_h },
					.size = { small_time_w, small_time_h } });

		text_layer_set_text_color(time_tz2_text_layer, GColorWhite);
		text_layer_set_text_alignment(time_tz2_text_layer, GTextAlignmentCenter);
		text_layer_set_background_color(time_tz2_text_layer, GColorClear);
		text_layer_set_font(time_tz2_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

		layer_add_child(time_layer, text_layer_get_layer(time_text_layer));
		layer_add_child(time_layer, text_layer_get_layer(time_tz1_text_layer));
		layer_add_child(time_layer, text_layer_get_layer(time_beats_text_layer));
		layer_add_child(time_layer, text_layer_get_layer(time_tz2_text_layer));

		layer_add_child(window_get_root_layer(win), time_layer);
	}

	// Weather layer
	{
		GRect weather_rect = { .origin = { WEATHER_X, WEATHER_Y },
				       .size = { WEATHER_WIDTH, WEATHER_HEIGHT } };

		weather_layer = layer_create(weather_rect);

		// Two layers, each half the width
		// conditions        temperature
		int half_w = WEATHER_WIDTH / 2.0;

		// XXX create weather icon bitmaps statically
		weather_cond_layer = bitmap_layer_create((GRect) { .origin = { 0, 0 },
					.size = { half_w, WEATHER_HEIGHT } });
		weather_temp_layer = text_layer_create((GRect) { .origin = { half_w, 5 },
					.size = { half_w, WEATHER_HEIGHT } });

		weather_cond_bitmap = gbitmap_create_with_resource(WEATHER_ICONS[WEATHER_ICON_NONE]);
		bitmap_layer_set_bitmap(weather_cond_layer, weather_cond_bitmap);

		text_layer_set_text_color(weather_temp_layer, GColorWhite);
		text_layer_set_text_alignment(weather_temp_layer, GTextAlignmentCenter);
		text_layer_set_background_color(weather_temp_layer, GColorClear);
		text_layer_set_font(weather_temp_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

		layer_add_child(weather_layer, bitmap_layer_get_layer(weather_cond_layer));
		layer_add_child(weather_layer, text_layer_get_layer(weather_temp_layer));

		layer_add_child(window_get_root_layer(win), weather_layer);
	}
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
	draw_bluetooth_warning(bluetooth_connection_service_peek());

	// Draw the last known state of the phone information.
	draw_weather(weather_info);

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

	gbitmap_destroy(weather_cond_bitmap);
	bitmap_layer_destroy(weather_cond_layer);
	text_layer_destroy(weather_temp_layer);
	layer_destroy(weather_layer);

	text_layer_destroy(time_tz2_text_layer);
	text_layer_destroy(time_beats_text_layer);
	text_layer_destroy(time_tz1_text_layer);
	text_layer_destroy(time_text_layer);
	layer_destroy(time_layer);

	text_layer_destroy(date_text_layer);
	text_layer_destroy(date_dow_layer);
	layer_destroy(date_layer);

	text_layer_destroy(status_bluetooth_warn_layer);
	layer_destroy(status_phone_battery_layer);
	layer_destroy(status_watch_battery_layer);
	layer_destroy(status_layer);
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
