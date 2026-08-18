var Clay = require('@rebble/clay');
var clayConfig = require('./config');

var clay = new Clay(clayConfig);

var LEGACY_WEATHER_CACHE_KEY = 'textWatchWeather';
var WEATHER_CACHE_KEY = 'textWatchWeatherV2';
var WEATHER_CACHE_MAX_AGE_MS = 6 * 60 * 60 * 1000;
var WEATHER_CACHE_RADIUS_KM = 150;
var RETRY_DELAYS_MS = [60 * 1000, 5 * 60 * 1000, 15 * 60 * 1000];

var highAccuracyLocationOptions = {
  enableHighAccuracy: true,
  timeout: 12000,
  maximumAge: 0
};

var lowAccuracyLocationOptions = {
  enableHighAccuracy: false,
  timeout: 15000,
  maximumAge: 5 * 60 * 1000
};

var fetchInProgress = false;
var cachedWeather = null;
var lastPosition = null;
var retryAttempt = 0;
var retryTimer = null;

function conditionForCode(code) {
  if (code === 0) {
    return 'clear';
  }
  if (code >= 1 && code <= 3) {
    return 'cloudy';
  }
  if (code === 45 || code === 48) {
    return 'fog';
  }
  if (code >= 51 && code <= 57) {
    return 'drizzle';
  }
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) {
    return 'rain';
  }
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    return 'snow';
  }
  if (code >= 95) {
    return 'storm';
  }
  return 'mixed';
}

function isNumber(value) {
  return typeof value === 'number' && isFinite(value);
}

function isValidWeather(weather) {
  return Boolean(weather &&
    isNumber(weather.temperature) &&
    isNumber(weather.low) &&
    isNumber(weather.high) &&
    typeof weather.conditions === 'string' &&
    weather.conditions.length > 0 &&
    isNumber(weather.fetchedAt) &&
    isNumber(weather.latitude) &&
    isNumber(weather.longitude));
}

function isDisplayableWeather(weather) {
  if (!isValidWeather(weather)) {
    return false;
  }
  var age = Date.now() - weather.fetchedAt;
  return age >= 0 && age <= WEATHER_CACHE_MAX_AGE_MS;
}

function clearWeatherCache() {
  cachedWeather = null;
  localStorage.removeItem(WEATHER_CACHE_KEY);
}

function restoreWeather() {
  // Version 1 did not record coordinates and could show weather cached before
  // the user travelled. It is intentionally discarded during migration.
  localStorage.removeItem(LEGACY_WEATHER_CACHE_KEY);

  var saved = localStorage.getItem(WEATHER_CACHE_KEY);
  if (!saved) {
    return null;
  }

  try {
    var weather = JSON.parse(saved);
    if (isDisplayableWeather(weather)) {
      return weather;
    }
  } catch (error) {
    console.log('Unable to restore cached weather: ' + error);
  }

  clearWeatherCache();
  return null;
}

function saveWeather(weather) {
  cachedWeather = weather;
  localStorage.setItem(WEATHER_CACHE_KEY, JSON.stringify(weather));
}

function sendWeather(weather) {
  Pebble.sendAppMessage({
    KEY_TEMPERATURE: weather.temperature,
    KEY_LOW: weather.low,
    KEY_HIGH: weather.high,
    KEY_CONDITIONS: weather.conditions
  });
}

function sendWeatherUnavailable() {
  Pebble.sendAppMessage({
    KEY_CONDITIONS: 'X'
  });
}

function degreesToRadians(degrees) {
  return degrees * Math.PI / 180;
}

function distanceBetweenKm(firstLatitude, firstLongitude,
                           secondLatitude, secondLongitude) {
  var earthRadiusKm = 6371;
  var latitudeDelta = degreesToRadians(secondLatitude - firstLatitude);
  var longitudeDelta = degreesToRadians(secondLongitude - firstLongitude);
  var firstLatitudeRadians = degreesToRadians(firstLatitude);
  var secondLatitudeRadians = degreesToRadians(secondLatitude);
  var haversine = Math.sin(latitudeDelta / 2) * Math.sin(latitudeDelta / 2) +
    Math.cos(firstLatitudeRadians) * Math.cos(secondLatitudeRadians) *
    Math.sin(longitudeDelta / 2) * Math.sin(longitudeDelta / 2);
  haversine = Math.max(0, Math.min(1, haversine));
  return earthRadiusKm * 2 * Math.atan2(Math.sqrt(haversine),
    Math.sqrt(1 - haversine));
}

function cacheMatchesPosition(weather, position) {
  if (!position) {
    return true;
  }
  return distanceBetweenKm(weather.latitude, weather.longitude,
    position.coords.latitude, position.coords.longitude) <=
    WEATHER_CACHE_RADIUS_KM;
}

function sendCachedWeather(position) {
  if (isDisplayableWeather(cachedWeather) &&
      cacheMatchesPosition(cachedWeather, position)) {
    console.log('Using cached weather from ' + new Date(cachedWeather.fetchedAt));
    sendWeather(cachedWeather);
    return true;
  }

  if (cachedWeather && (!isDisplayableWeather(cachedWeather) || position)) {
    clearWeatherCache();
  }
  return false;
}

function resetRetries() {
  if (retryTimer) {
    clearTimeout(retryTimer);
    retryTimer = null;
  }
  retryAttempt = 0;
}

function scheduleRetry(reason) {
  if (retryTimer) {
    return;
  }
  if (retryAttempt >= RETRY_DELAYS_MS.length) {
    console.log('Weather retries exhausted (' + reason +
      '); waiting for the next watch request');
    return;
  }

  var delay = RETRY_DELAYS_MS[retryAttempt];
  retryAttempt += 1;
  console.log('Scheduling weather retry #' + retryAttempt + ' in ' +
    (delay / 1000) + ' seconds (' + reason + ')');
  retryTimer = setTimeout(function() {
    retryTimer = null;
    getWeather(true);
  }, delay);
}

function handleWeatherFailure(message) {
  fetchInProgress = false;
  console.log('Weather unavailable: ' + message);

  if (!sendCachedWeather(lastPosition)) {
    sendWeatherUnavailable();
  }
  scheduleRetry(message);
}

function fetchWeather(position) {
  lastPosition = position;
  sendCachedWeather(position);

  var rawLatitude = position.coords.latitude;
  var rawLongitude = position.coords.longitude;
  var latitude = encodeURIComponent(rawLatitude);
  var longitude = encodeURIComponent(rawLongitude);
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + latitude +
    '&longitude=' + longitude +
    '&current=temperature_2m,weather_code' +
    '&daily=temperature_2m_min,temperature_2m_max' +
    '&timezone=auto&forecast_days=1';

  var request = new XMLHttpRequest();
  request.onload = function() {
    if (request.status < 200 || request.status >= 300) {
      handleWeatherFailure('HTTP ' + request.status);
      return;
    }

    try {
      var data = JSON.parse(request.responseText);
      var current = data.current;
      var daily = data.daily;

      if (!current || !daily || !isNumber(current.temperature_2m) ||
          !isNumber(current.weather_code) ||
          !daily.temperature_2m_min || !daily.temperature_2m_max ||
          !isNumber(daily.temperature_2m_min[0]) ||
          !isNumber(daily.temperature_2m_max[0])) {
        throw new Error('incomplete response');
      }

      var weather = {
        temperature: Math.round(current.temperature_2m),
        low: Math.round(daily.temperature_2m_min[0]),
        high: Math.round(daily.temperature_2m_max[0]),
        conditions: conditionForCode(current.weather_code),
        fetchedAt: Date.now(),
        latitude: rawLatitude,
        longitude: rawLongitude,
        accuracy: isNumber(position.coords.accuracy) ?
          position.coords.accuracy : null
      };

      fetchInProgress = false;
      saveWeather(weather);
      resetRetries();
      sendWeather(weather);
    } catch (error) {
      handleWeatherFailure(error.message || 'invalid response');
    }
  };
  request.onerror = function() {
    handleWeatherFailure('network error');
  };
  request.ontimeout = function() {
    handleWeatherFailure('request timed out');
  };
  request.open('GET', url, true);
  request.timeout = 15000;
  request.send();
}

function requestLocation() {
  navigator.geolocation.getCurrentPosition(
    fetchWeather,
    function(highAccuracyError) {
      console.log('Precise location unavailable; trying approximate location: ' +
        (highAccuracyError.message || 'location error'));
      navigator.geolocation.getCurrentPosition(
        fetchWeather,
        function(lowAccuracyError) {
          handleWeatherFailure(lowAccuracyError.message || 'location error');
        },
        lowAccuracyLocationOptions
      );
    },
    highAccuracyLocationOptions
  );
}

function getWeather(isRetry) {
  if (fetchInProgress) {
    return;
  }

  if (!isRetry) {
    if (retryTimer) {
      clearTimeout(retryTimer);
      retryTimer = null;
    }
    retryAttempt = 0;
  }

  fetchInProgress = true;
  lastPosition = null;
  requestLocation();
}

Pebble.addEventListener('ready', function() {
  console.log('Text Watch WDB Modern PebbleKit JS ready');
  cachedWeather = restoreWeather();
  Pebble.sendAppMessage({
    KEY_REQUEST_SETTINGS: 1
  });
  getWeather(false);
});

Pebble.addEventListener('appmessage', function(event) {
  if (event.payload.KEY_THEME !== undefined) {
    clay.setSettings('KEY_THEME', Boolean(event.payload.KEY_THEME));
  }
  if (event.payload.KEY_DISABLE_ANIMATION !== undefined) {
    clay.setSettings('KEY_DISABLE_ANIMATION',
      Boolean(event.payload.KEY_DISABLE_ANIMATION));
  }
  if (event.payload.KEY_REQUEST_WEATHER !== undefined) {
    getWeather(false);
  }
});
