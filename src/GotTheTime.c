/*
Watch face with date and time like I like them.
General layout is below (not to scale).

+------------+
|   Sunday   |
| 2013-10-06 |
|            |
|   09:23    | <<< big font
|     AM     | <<< only in 12-hour mode
| w:--  p:-- | <<< battery indicator for watch and phone, temperature (from phone app)
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

#define LOWER_TIME_X 32 // DRAW_INSET + 32
#define LOWER_TIME_Y 112 // SCREEN_TIME_Y + SCREEN_TIME_HEIGHT + FONT_PAD_Y
#define LOWER_TIME_WIDTH  121 // SCREEN_TIME_WIDTH - 15 (for inset)
#define LOWER_TIME_HEIGHT 31 // SMALL_FONT_HEIGHT

#define BLUETOOTH_WARN_X DRAW_INSET
#define BLUETOOTH_WARN_Y 114 // LOWER_TIME_Y + FONT_PAD_Y
#define BLUETOOTH_WARN_WIDTH  32
#define BLUETOOTH_WARN_HEIGHT 32

#define WATCH_BATTERY_X DRAW_INSET
#define WATCH_BATTERY_Y 145 // LOWER_TIME_Y + LOWER_TIME_HEIGHT + FONT_PAD_Y
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

#define VIBRATE_HOURLY 1 // Change to 0 to disable

#define ALL_TIME_CHANGED (SECOND_UNIT | MINUTE_UNIT | HOUR_UNIT | DAY_UNIT | MONTH_UNIT | YEAR_UNIT)

typedef enum {
	CHARGE_PERCENT_FIELD = 0x1,
	IS_CHARGING_FIELD    = 0x2,
	IS_PLUGGED_FIELD     = 0x4,
} BatteryFields;

#define ALL_BATTERY_CHANGED (CHARGE_PERCENT_FIELD | IS_CHARGING_FIELD | IS_PLUGGED_FIELD)

typedef struct {
	uint8_t icon_index;
	const char* temperature_str;
} Weather;

typedef enum {
	WEATHER_ICON_FIELD = 0x1,
	WEATHER_TEMPERATURE_FIELD= 0x2,
} WeatherFields;

#define ALL_WEATHER_CHANGED (WEATHER_ICON_FIELD | WEATHER_TEMPERATURE_FIELD)

// App Message values

typedef enum {
	WEATHER_MESSAGE_ICON = 0, // TUPLE_UINT
	WEATHER_MESSAGE_TEMPERATURE = 1, // TUPLE_CSTRING
	PHONE_BATTERY_PERCENT = 2, // TUPLE_UINT
	PHONE_BATTERY_CHARGING = 3, // TUPLE_UINT
	PHONE_BATTERY_PLUGGED = 4, // TUPLE_UINT
} GTTMessageIndex; // GotTheTime App Message indexes

#define INBOUND_MESSAGE_SIZE 64
#define OUTBOUND_MESSAGE_SIZE 64

// For getting information from the companion app on the phone.
AppSync sync;
uint8_t sync_buffer[64];


Window* window;

TextLayer* text_date_layer;
TextLayer* text_time_layer;
TextLayer* text_lower_date_layer;
TextLayer* text_lower_time_layer;
TextLayer* text_watch_battery_layer;
TextLayer* text_phone_battery_layer;
TextLayer* text_temperature_layer;

BitmapLayer* bitmap_bluetooth_layer;
GBitmap* bitmap_bluetooth_warn;

GFont font_21;
GFont font_49_numbers;

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

void draw_screen(struct tm* ptime, TimeUnits units_changed,
		 BatteryChargeState watch_battery, BatteryFields watch_battery_changed,
		 BatteryChargeState phone_battery, BatteryFields phone_battery_changed,
		 Weather weather, WeatherFields weather_changed) {
	// Buffers we can format text into.
	// Need to be static because they're used by the text layers
	// even after this function completes.
	static char time_text[]  = "00:00";
	static char date_text[]  = "Xxxxxxxxxx"; // Wednesday
	static char lower_date[] = "0000-00-00"; // 2013-10-02
	static char lower_time[] = "XX"; // AM
	static char watch_battery_text[] = "w000% c"; // 0% - 100% c for "charging"
	static char phone_battery_text[] = "p000% c"; // 0% - 100% c for "charging"
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
	// Since this is using the same text layer, update for either one changing.
	if ((watch_battery_changed & CHARGE_PERCENT_FIELD) ||
	    (watch_battery_changed & IS_CHARGING_FIELD)) {
		char watch_charging = ((watch_battery_changed & IS_CHARGING_FIELD) && watch_battery.is_charging)? 'c': ' ';
		snprintf(watch_battery_text, sizeof(watch_battery_text), "%3d%%%c",
			 watch_battery.charge_percent, watch_charging);
	}
	// XXX If drawing the other fields (charging/plugged), test and draw those.
	if (watch_battery_changed) {
		text_layer_set_text(text_watch_battery_layer, watch_battery_text);
	}

	APP_LOG(APP_LOG_LEVEL_DEBUG, "phone_battery: %d %d %d  changed: %x",
		phone_battery.charge_percent,
		phone_battery.is_charging,
		phone_battery.is_plugged, phone_battery_changed);
	if ((phone_battery_changed & CHARGE_PERCENT_FIELD) ||
	    (phone_battery_changed & IS_CHARGING_FIELD)) {
		char phone_charging = ((phone_battery_changed & IS_CHARGING_FIELD) && phone_battery.is_charging)? 'c': ' ';
		snprintf(phone_battery_text, sizeof(phone_battery_text), "%3d%%%c",
			 phone_battery.charge_percent, phone_charging);
	}
	// XXX If drawing the other fields (charging/plugged), test and draw those.
	if (phone_battery_changed) {
		text_layer_set_text(text_phone_battery_layer, phone_battery_text);
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


static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_values, const Tuple* old_values, void* context) {
	Weather w;
	WeatherFields wfields = 0;
	BatteryChargeState b;
	BatteryFields bfields = 0;

	memset(&w, 0, sizeof(w));
	memset(&b, 0, sizeof(b));

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
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Temperature changed: %x %x %x",
				w.temperature_str[0], w.temperature_str[1],
				w.temperature_str[2]);
		}
		break;

// XXX When we get values from the App, we'll get them one at a time in the
// sync_tuple_changed_callback.  We need/want all the battery info at the same
// time to correctly draw the phone's battery state.
// Rather than solve that now, I'm going to ignore the charging/plugged info.
// Another way would be to save the old value and only update the fields that have
// new info.  Icky.

	case PHONE_BATTERY_PERCENT:
		if (new_values && new_values->value) {
			b.charge_percent = new_values->value->uint8;
			bfields |= CHARGE_PERCENT_FIELD;
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Battery percent: %d", b.charge_percent);
		}
		break;
	case PHONE_BATTERY_CHARGING:
		/* if (new_values && new_values->value) { */
		/* 	b.is_charging = new_values->value->uint8; */
		/* 	bfields |= IS_CHARGING_FIELD; */
		/* 	APP_LOG(APP_LOG_LEVEL_DEBUG, "Battery charging: %d", b.is_charging); */
		/* } */
		break;
	case PHONE_BATTERY_PLUGGED:
		/* if (new_values && new_values->value) { */
		/* 	b.is_plugged = new_values->value->uint8; */
		/* 	bfields |= IS_PLUGGED_FIELD; */
		/* 	APP_LOG(APP_LOG_LEVEL_DEBUG, "Battery plugged: %d", b.is_plugged); */
		/* } */
		break;
	};

	time_t now = time(NULL);

	draw_screen(localtime(&now), 0,
		    battery_state_service_peek(), 0,
		    b, bfields,
		    w, wfields);
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

	// Watch Battery layer
	text_watch_battery_layer = text_layer_create((GRect) { .origin = { WATCH_BATTERY_X, WATCH_BATTERY_Y },
				.size = { WATCH_BATTERY_WIDTH, WATCH_BATTERY_HEIGHT } });
	text_layer_set_text_color(text_watch_battery_layer, GColorWhite);
	text_layer_set_text_alignment(text_watch_battery_layer, GTextAlignmentLeft);
	text_layer_set_background_color(text_watch_battery_layer, GColorClear);
	text_layer_set_font(text_watch_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_watch_battery_layer));

	// Phone Battery layer
	text_phone_battery_layer = text_layer_create((GRect) { .origin = { PHONE_BATTERY_X, PHONE_BATTERY_Y },
				.size = { PHONE_BATTERY_WIDTH, PHONE_BATTERY_HEIGHT } });
	text_layer_set_text_color(text_phone_battery_layer, GColorWhite);
	text_layer_set_text_alignment(text_phone_battery_layer, GTextAlignmentCenter);
	text_layer_set_background_color(text_phone_battery_layer, GColorClear);
	text_layer_set_font(text_phone_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_phone_battery_layer));

	// Temperature layer
	text_temperature_layer = text_layer_create((GRect) { .origin = { TEMPERATURE_X, TEMPERATURE_Y },
				.size = { TEMPERATURE_WIDTH, TEMPERATURE_HEIGHT } });
	text_layer_set_text_color(text_temperature_layer, GColorWhite);
	text_layer_set_text_alignment(text_temperature_layer, GTextAlignmentRight);
	text_layer_set_background_color(text_temperature_layer, GColorClear);
	text_layer_set_font(text_temperature_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_get_root_layer(win),
			text_layer_get_layer(text_temperature_layer));

	// No bluetooth layer
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
	Weather w;
	BatteryChargeState b;

	memset(&w, 0, sizeof(w));
	memset(&b, 0, sizeof(b));

	draw_screen(localtime(&now), ALL_TIME_CHANGED,
		    battery_state_service_peek(), ALL_BATTERY_CHANGED,
		    b, ALL_BATTERY_CHANGED,
		    w, 0);

	// If bluetooth is connected (the normal state of things),
	// don't show the layer yet.
	layer_set_hidden((Layer*) bitmap_bluetooth_layer, bluetooth_connection_service_peek());

	Tuplet initial_message_values[] = {
		TupletInteger(WEATHER_MESSAGE_ICON, (uint8_t) 1),
		TupletCString(WEATHER_MESSAGE_TEMPERATURE, "0000\u00B0C"),
		TupletInteger(PHONE_BATTERY_PERCENT, (uint8_t) 0),
		TupletInteger(PHONE_BATTERY_CHARGING, (uint8_t) 0),
		TupletInteger(PHONE_BATTERY_PLUGGED, (uint8_t) 0),
	};
	app_sync_init(&sync, sync_buffer, sizeof(sync_buffer),
		      initial_message_values, ARRAY_LENGTH(initial_message_values),
		      sync_tuple_changed_callback, sync_error_callback, NULL);
	send_message();
}

static void window_unload(Window *win) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);

	app_sync_deinit(&sync);

	gbitmap_destroy(bitmap_bluetooth_warn);
	bitmap_layer_destroy(bitmap_bluetooth_layer);

	text_layer_destroy(text_temperature_layer);
	text_layer_destroy(text_phone_battery_layer);
	text_layer_destroy(text_watch_battery_layer);
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
	BatteryChargeState b;
	draw_screen(tick_time, units_changed,
		    battery_state_service_peek(), 0,
		    b, 0,
		    w, 0);

	// Update the weather frequently, but not every minute.
	// The "or hour" is there to catch when we first load the watchface.
	// On "appear", we may not have the message conduit setup yet.
	if ((tick_time->tm_min % 15 == 0) || (units_changed & HOUR_UNIT)) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, ">>>> POKING APP TO GIVE ME WEATHER");
		send_message();
	}
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
	BatteryChargeState b;
	draw_screen(localtime(&now), (TimeUnits) 0,
		    charge_state, ALL_BATTERY_CHANGED,
		    b, 0,
		    w, 0);
}

void handle_bluetooth_update(bool connected)
{
	// If connected, clear the area (or, hide the layer).
	// If disconnected, show the graphic/layer.
	layer_set_hidden((Layer*) bitmap_bluetooth_layer, connected);

	if (!connected) {
		vibes_enqueue_custom_pattern(BLUETOOTH_WARN_VIBE_PATTERN);
	}
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

	app_message_open(INBOUND_MESSAGE_SIZE, OUTBOUND_MESSAGE_SIZE);

	/* Tuplet initial_message_values[] = { */
	/* 	TupletInteger(WEATHER_MESSAGE_ICON, (uint8_t) 1), */
	/* 	TupletCString(WEATHER_MESSAGE_TEMPERATURE, "0000\u00B0C"), */
	/* 	TupletInteger(PHONE_BATTERY_PERCENT, (uint8_t) 0), */
	/* 	TupletInteger(PHONE_BATTERY_CHARGING, (uint8_t) 0), */
	/* 	TupletInteger(PHONE_BATTERY_PLUGGED, (uint8_t) 0), */
	/* }; */
	/* app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), */
	/* 	      initial_message_values, ARRAY_LENGTH(initial_message_values), */
	/* 	      sync_tuple_changed_callback, sync_error_callback, NULL); */
//	send_message();
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
