module.exports = [
  {
    "type": "heading",
    "defaultValue": "Text Watch WDB Modern"
  },
  {
    "type": "text",
    "defaultValue": "Choose the high-contrast appearance used on your watch."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Appearance"
      },
      {
        "type": "toggle",
        "messageKey": "KEY_THEME",
        "label": "Black text on white",
        "description": "Off uses white text on black.",
        "defaultValue": false
      },
      {
        "type": "toggle",
        "messageKey": "KEY_DISABLE_ANIMATION",
        "label": "Disable time animation",
        "description": "Makes minute changes immediate, avoiding animation work to modestly reduce battery use.",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Weather"
      },
      {
        "type": "text",
        "defaultValue": "Weather uses your phone's location and Open-Meteo. It prefers a fresh precise location and falls back to an approximate one when necessary. No account or API key is required. Cached forecasts are location-aware and kept for up to six hours, with staged retries after temporary failures."
      },
      {
        "type": "text",
        "defaultValue": "<a href=\"https://open-meteo.com/\">Weather data by Open-Meteo</a>"
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
