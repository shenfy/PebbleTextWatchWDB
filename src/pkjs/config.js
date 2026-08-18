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
        "defaultValue": "Weather uses your phone's location and Open-Meteo. No account or API key is required. The last successful forecast is kept for up to six hours, with staged retries after temporary failures."
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
