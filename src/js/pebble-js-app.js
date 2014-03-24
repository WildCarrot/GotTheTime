
Pebble.addEventListener("ready",
			function(e) {
			    console.log(">>>>JS ready!");
			});

// To return messages to the pebble:
//Pebble.sendAppMessage(data, ackHandler, nackHandler);
//data the message dictionary
//ackHandler and nackHandler as expected.
// returns a transaction ID

var locationOptions = { "timeout": 15000, "maximumAge": 60000 };

Pebble.addEventListener("appmessage",
			function(e) {
			    console.log(">>>>>Got message ");
			    //getBattery();
			    window.navigator.geolocation.getCurrentPosition(getWeather, locationError,
									    locationOptions);
			    console.log(e.type);
			});
// Key strings are in the appKeys dictionary in appinfo.json.

function getBattery() {
    console.log(">>>> GETTING BATTERY");
    var batt = window.navigator.battery.level;
    console.log(">>>> BATTERY: " + batt);
    Pebble.sendAppMessage({"PHONE_BATTERY_PERCENT": (batt * 100)});
}

// Copied this from the example.
function iconFromWeatherId(weatherId) {
  if (weatherId < 600) {
    return 2;
  } else if (weatherId < 700) {
    return 3;
  } else if (weatherId > 800) {
    return 1;
  } else {
    return 0;
  }
}


function getWeather(pos) {
    var coords = pos.coords;

    var req = new XMLHttpRequest();
    // XXX Get location from the phone/JS interface.
    req.open('GET', "http://api.openweathermap.org/data/2.5/weather?lat=" + coords.latitude +
	     "&lon=" + coords.longitude, true);
    req.onload = function(e) {
	if (req.readyState ==4 && req.status == 200) {
	    console.log(req.responseText);

	    var resp = JSON.parse(req.responseText);
	    var temp = parseInt(resp.main.temp - 273.15);
	    var icon = iconFromWeatherId(resp.weather[0].id);

	    console.log("TEMP>>>>" + temp);
	    console.log("ICON>>>>" + icon);
	    console.log("CITY>>>>" + resp.name); // city

	    Pebble.sendAppMessage({"WEATHER_MESSAGE_ICON": icon,
				   "WEATHER_MESSAGE_TEMPERATURE": temp.toString() + "\u00B0C"});
	}
	locationError(e);
    }
    req.send(null);
}

var error_weather_response = {"WEATHER_MESSAGE_ICON": 0, "WEATHER_MESSAGE_TEMPERATURE": "n/a"};

function locationError(err) {
    console.warn("Error getting location: " + err.message + " (" + err.code + ")");
    //Pebble.sendAppMessage(error_weather_response);
    // don't update anything if there was an error
    Pebble.sendAppMessage({});
}

// Configuration window
// Pebble.addEventListener("showConfiguration",
// 			function(e) {
// 			    Pebble.OpenURL(..);
// 			    // Web page has config controls,
// 			    // returns by calling pebblejs://close#some_return_value
// 			    // (Or "pebblejs://close#" + encodeURIComponent(JSON.stringify(configuration));")
// 			    // this code handles the return with the function below.
// 			});
// Pebble.addEventListener("webviewclosed",
// 			function(e) {
// 			    // All done with config web page,
// 			    //e.response has the some_return_value from above.
// 			    // (Or "var config = JSON.parse(decodeURIComponent(e.response));"
// 			});
Pebble.addEventListener("webviewclosed",
                                     function(e) {
                                     console.log("webview closed");
                                     console.log(e.type);
                                     console.log(e.response);
                                     });
