#include <pebble.h>
#include <ctype.h>
#include <string.h>

#include "num2words-en.h"

#define BUFFER_SIZE 48
#define WEATHER_BUFFER_SIZE 64
#define PERSIST_KEY_THEME 1
#define PERSIST_KEY_DISABLE_ANIMATION 2

typedef enum {
  THEME_DARK = 0,
  THEME_LIGHT = 1
} Theme;

typedef struct {
  TextLayer *layers[2];
  PropertyAnimation *animations[2];
  char text[2][BUFFER_SIZE];
  uint8_t active;
  int16_t visible_x;
  int16_t hidden_x;
  int16_t offscreen_x;
} AnimatedLine;

static Window *s_window;
static AnimatedLine s_lines[3];
static TextLayer *s_weather_layer;
static TextLayer *s_day_layer;
static TextLayer *s_date_layer;
static TextLayer *s_battery_layer;

static Theme s_theme = THEME_DARK;
static bool s_animation_disabled;
static GColor s_foreground;
static int16_t s_screen_width;
static bool s_large_layout;

static int s_temperature;
static int s_low;
static int s_high;
static char s_conditions[32];
static char s_weather_text[WEATHER_BUFFER_SIZE];
static char s_battery_text[8];
static char s_day_text[24];
static char s_date_text[32];

static void destroy_animation(PropertyAnimation **animation) {
  if (!*animation) {
    return;
  }

  if (animation_is_scheduled((Animation *)*animation)) {
    animation_unschedule((Animation *)*animation);
  }
  property_animation_destroy(*animation);
  *animation = NULL;
}

static void set_layer_colors(TextLayer *layer) {
  if (!layer) {
    return;
  }
  text_layer_set_text_color(layer, s_foreground);
  text_layer_set_background_color(layer, GColorClear);
}

static void apply_theme(void) {
  GColor background = s_theme == THEME_LIGHT ? GColorWhite : GColorBlack;
  s_foreground = s_theme == THEME_LIGHT ? GColorBlack : GColorWhite;

  if (s_window) {
    window_set_background_color(s_window, background);
  }

  for (size_t i = 0; i < ARRAY_LENGTH(s_lines); ++i) {
    set_layer_colors(s_lines[i].layers[0]);
    set_layer_colors(s_lines[i].layers[1]);
  }
  set_layer_colors(s_weather_layer);
  set_layer_colors(s_day_layer);
  set_layer_colors(s_date_layer);
  set_layer_colors(s_battery_layer);
}

static void load_settings(void) {
  if (!persist_exists(PERSIST_KEY_THEME)) {
    s_theme = THEME_DARK;
  } else {
    int stored_theme = persist_read_int(PERSIST_KEY_THEME);
    s_theme = stored_theme == THEME_LIGHT ? THEME_LIGHT : THEME_DARK;
  }

  s_animation_disabled = persist_exists(PERSIST_KEY_DISABLE_ANIMATION) &&
      persist_read_int(PERSIST_KEY_DISABLE_ANIMATION);
}

static void configure_text_layer(TextLayer *layer, GFont font,
                                 GTextAlignment alignment) {
  text_layer_set_font(layer, font);
  text_layer_set_text_alignment(layer, alignment);
  text_layer_set_overflow_mode(layer, GTextOverflowModeFill);
  set_layer_colors(layer);
}

static void configure_animated_line(AnimatedLine *line, GRect frame,
                                    GFont font) {
  memset(line, 0, sizeof(*line));
  line->visible_x = frame.origin.x;
  line->hidden_x = s_screen_width + frame.origin.x;
  line->offscreen_x = frame.origin.x - s_screen_width;

  line->layers[0] = text_layer_create(frame);
  frame.origin.x = line->hidden_x;
  line->layers[1] = text_layer_create(frame);

  configure_text_layer(line->layers[0], font, GTextAlignmentLeft);
  configure_text_layer(line->layers[1], font, GTextAlignmentLeft);
}

static void copy_line_text(char destination[BUFFER_SIZE], const char *source) {
  size_t length = 0;
  while (length < BUFFER_SIZE - 1 && source[length] != '\0') {
    ++length;
  }
  memcpy(destination, source, length);
  destination[length] = '\0';
}

static void set_initial_line(AnimatedLine *line, const char *text) {
  copy_line_text(line->text[0], text);
  text_layer_set_text(line->layers[0], line->text[0]);
}

static void set_line_immediately(AnimatedLine *line, const char *next_text) {
  if (strcmp(line->text[line->active], next_text) == 0) {
    return;
  }

  copy_line_text(line->text[line->active], next_text);
  text_layer_set_text(line->layers[line->active], line->text[line->active]);
}

static void update_line(AnimatedLine *line, const char *next_text) {
  if (strcmp(line->text[line->active], next_text) == 0) {
    return;
  }

  uint8_t current = line->active;
  uint8_t next = current == 0 ? 1 : 0;

  destroy_animation(&line->animations[current]);
  destroy_animation(&line->animations[next]);

  copy_line_text(line->text[next], next_text);
  text_layer_set_text(line->layers[next], line->text[next]);

  GRect current_frame = layer_get_frame(text_layer_get_layer(line->layers[current]));
  GRect next_frame = layer_get_frame(text_layer_get_layer(line->layers[next]));
  current_frame.origin.x = line->visible_x;
  next_frame.origin.x = line->hidden_x;
  layer_set_frame(text_layer_get_layer(line->layers[current]), current_frame);
  layer_set_frame(text_layer_get_layer(line->layers[next]), next_frame);

  GRect current_target = current_frame;
  GRect next_target = next_frame;
  current_target.origin.x = line->offscreen_x;
  next_target.origin.x = line->visible_x;

  line->animations[current] = property_animation_create_layer_frame(
      text_layer_get_layer(line->layers[current]), &current_frame, &current_target);
  line->animations[next] = property_animation_create_layer_frame(
      text_layer_get_layer(line->layers[next]), &next_frame, &next_target);

  if (!line->animations[current] || !line->animations[next]) {
    destroy_animation(&line->animations[current]);
    destroy_animation(&line->animations[next]);
    layer_set_frame(text_layer_get_layer(line->layers[current]), current_target);
    layer_set_frame(text_layer_get_layer(line->layers[next]), next_target);
    line->active = next;
    return;
  }

  animation_set_duration((Animation *)line->animations[current], 400);
  animation_set_duration((Animation *)line->animations[next], 400);
  animation_set_curve((Animation *)line->animations[current], AnimationCurveEaseOut);
  animation_set_curve((Animation *)line->animations[next], AnimationCurveEaseOut);
  animation_schedule((Animation *)line->animations[current]);
  animation_schedule((Animation *)line->animations[next]);
  line->active = next;
}

static void set_date(const struct tm *time_parts) {
  char month[8];
  strftime(month, sizeof(month), "%b", time_parts);
  strftime(s_day_text, sizeof(s_day_text), "%A", time_parts);

  month[0] = (char)tolower((unsigned char)month[0]);
  s_day_text[0] = (char)tolower((unsigned char)s_day_text[0]);

  snprintf(s_date_text, sizeof(s_date_text), "%s %d, %d", month,
           time_parts->tm_mday, time_parts->tm_year + 1900);

  text_layer_set_text(s_day_layer, s_day_text);
  text_layer_set_text(s_date_layer, s_date_text);

  // The weekday and date use different weights, so they are separate layers
  // positioned to read as one right-aligned phrase.
  GRect date_frame = layer_get_frame(text_layer_get_layer(s_date_layer));
  GSize date_size = text_layer_get_content_size(s_date_layer);
  int16_t gap = s_large_layout ? 4 : 3;
  int16_t day_width = date_frame.size.w - date_size.w - gap;
  if (day_width < 0) {
    day_width = 0;
  }

  GRect day_frame = layer_get_frame(text_layer_get_layer(s_day_layer));
  day_frame.origin.x = date_frame.origin.x;
  day_frame.size.w = day_width;
  layer_set_frame(text_layer_get_layer(s_day_layer), day_frame);
}

static void display_time(const struct tm *time_parts, bool animated) {
  char words[3][BUFFER_SIZE];
  time_to_3words(time_parts->tm_hour, time_parts->tm_min,
                 words[0], words[1], words[2], BUFFER_SIZE);

  // The wider Emery display can render long teen words intact. The compact
  // layout retains the original split across two lines for 144-pixel screens.
  if (s_large_layout && time_parts->tm_min >= 14 && time_parts->tm_min <= 19 &&
      strcmp(words[2], "teen") == 0) {
    size_t prefix_length = strlen(words[1]);
    if (prefix_length + sizeof("teen") <= BUFFER_SIZE) {
      memcpy(words[1] + prefix_length, "teen", sizeof("teen"));
    }
    words[2][0] = '\0';
  }

  for (size_t i = 0; i < ARRAY_LENGTH(s_lines); ++i) {
    if (animated && !s_animation_disabled) {
      update_line(&s_lines[i], words[i]);
    } else if (animated) {
      set_line_immediately(&s_lines[i], words[i]);
    } else {
      set_initial_line(&s_lines[i], words[i]);
    }
  }
}

static void update_battery(BatteryChargeState state) {
  snprintf(s_battery_text, sizeof(s_battery_text), "%d%%", state.charge_percent);
  text_layer_set_text(s_battery_layer, s_battery_text);
}

static void update_weather_text(void) {
  if (s_conditions[0] == '\0') {
    snprintf(s_weather_text, sizeof(s_weather_text), "weather loading");
  } else if (s_conditions[0] == 'X') {
    s_weather_text[0] = '\0';
  } else {
    snprintf(s_weather_text, sizeof(s_weather_text), "%s  %dc  %d/%d",
             s_conditions, s_temperature, s_low, s_high);
  }
  text_layer_set_text(s_weather_layer, s_weather_text);
}

static void send_phone_update(bool request_weather) {
  DictionaryIterator *iterator = NULL;
  AppMessageResult result = app_message_outbox_begin(&iterator);
  if (result != APP_MSG_OK || !iterator) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Unable to start phone update: %d", result);
    return;
  }

  if (request_weather) {
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_REQUEST_WEATHER, 1);
  }
  dict_write_uint8(iterator, MESSAGE_KEY_KEY_THEME, s_theme == THEME_LIGHT);
  dict_write_uint8(iterator, MESSAGE_KEY_KEY_DISABLE_ANIMATION,
                   s_animation_disabled);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Unable to send phone update: %d", result);
  }
}

static void request_weather(void) {
  send_phone_update(true);
}

static void inbox_received(DictionaryIterator *iterator, void *context) {
  bool weather_changed = false;
  bool settings_requested = false;
  Tuple *tuple = dict_read_first(iterator);

  while (tuple) {
    if (tuple->key == MESSAGE_KEY_KEY_TEMPERATURE) {
      s_temperature = (int)tuple->value->int32;
      weather_changed = true;
    } else if (tuple->key == MESSAGE_KEY_KEY_LOW) {
      s_low = (int)tuple->value->int32;
      weather_changed = true;
    } else if (tuple->key == MESSAGE_KEY_KEY_HIGH) {
      s_high = (int)tuple->value->int32;
      weather_changed = true;
    } else if (tuple->key == MESSAGE_KEY_KEY_CONDITIONS) {
      snprintf(s_conditions, sizeof(s_conditions), "%s", tuple->value->cstring);
      weather_changed = true;
    } else if (tuple->key == MESSAGE_KEY_KEY_THEME) {
      Theme new_theme = tuple->value->int32 ? THEME_LIGHT : THEME_DARK;
      if (new_theme != s_theme) {
        s_theme = new_theme;
        persist_write_int(PERSIST_KEY_THEME, s_theme);
        apply_theme();
      }
    } else if (tuple->key == MESSAGE_KEY_KEY_DISABLE_ANIMATION) {
      bool disabled = tuple->value->int32;
      if (disabled != s_animation_disabled) {
        s_animation_disabled = disabled;
        persist_write_int(PERSIST_KEY_DISABLE_ANIMATION,
                          s_animation_disabled);
      }
    } else if (tuple->key == MESSAGE_KEY_KEY_REQUEST_SETTINGS) {
      settings_requested = true;
    }
    tuple = dict_read_next(iterator);
  }

  if (weather_changed) {
    update_weather_text();
  }
  if (settings_requested) {
    send_phone_update(false);
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox message dropped: %d", reason);
}

static void outbox_failed(DictionaryIterator *iterator, AppMessageResult reason,
                          void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox message failed: %d", reason);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  display_time(tick_time, true);
  if (units_changed & DAY_UNIT) {
    set_date(tick_time);
  }
  if (units_changed & HOUR_UNIT) {
    request_weather();
  }
}

static void window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);
  s_screen_width = bounds.size.w;
  s_large_layout = bounds.size.h >= 200;

  int16_t margin = s_large_layout ? 2 : 0;
  // Bitham's visible glyphs occupy much less height than its nominal line
  // box. Keep the Emery rows close together, then center the resulting block
  // in the space above the footer.
  int16_t time_top = s_large_layout ? 22 : 13;
  int16_t line_spacing = s_large_layout ? 45 : 37;
  int16_t line_height = s_large_layout ? 52 : 50;
  int16_t footer_top = s_large_layout ? 185 : 135;
  int16_t footer_line_height = s_large_layout ? 22 : 18;
  int16_t footer_second_top = s_large_layout ? 206 : 150;

  GRect time_frame = GRect(margin, time_top,
                           bounds.size.w - (2 * margin), line_height);
  configure_animated_line(&s_lines[0], time_frame,
                          fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  time_frame.origin.y += line_spacing;
  configure_animated_line(&s_lines[1], time_frame,
                          fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT));
  time_frame.origin.y += line_spacing;
  configure_animated_line(&s_lines[2], time_frame,
                          fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT));

  GFont detail_font = fonts_get_system_font(
      s_large_layout ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14);
  GFont detail_bold_font = fonts_get_system_font(
      s_large_layout ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD);

  s_day_layer = text_layer_create(GRect(
      margin, footer_second_top,
      bounds.size.w - (2 * margin), footer_line_height));
  configure_text_layer(s_day_layer, detail_bold_font, GTextAlignmentRight);

  s_date_layer = text_layer_create(GRect(
      margin, footer_second_top,
      bounds.size.w - (2 * margin), footer_line_height));
  configure_text_layer(s_date_layer, detail_font, GTextAlignmentRight);

  s_weather_layer = text_layer_create(GRect(
      margin, footer_top,
      bounds.size.w - (2 * margin), footer_line_height));
  configure_text_layer(s_weather_layer, detail_font, GTextAlignmentRight);

  s_battery_layer = text_layer_create(GRect(
      margin, footer_second_top,
      s_large_layout ? 56 : 40, footer_line_height));
  configure_text_layer(s_battery_layer, detail_font, GTextAlignmentLeft);

  for (size_t i = 0; i < ARRAY_LENGTH(s_lines); ++i) {
    layer_add_child(root_layer, text_layer_get_layer(s_lines[i].layers[0]));
    layer_add_child(root_layer, text_layer_get_layer(s_lines[i].layers[1]));
  }
  layer_add_child(root_layer, text_layer_get_layer(s_weather_layer));
  layer_add_child(root_layer, text_layer_get_layer(s_day_layer));
  layer_add_child(root_layer, text_layer_get_layer(s_date_layer));
  layer_add_child(root_layer, text_layer_get_layer(s_battery_layer));

  apply_theme();

  time_t now = time(NULL);
  struct tm *time_parts = localtime(&now);
  display_time(time_parts, false);
  set_date(time_parts);
  update_weather_text();
  update_battery(battery_state_service_peek());
}

static void window_unload(Window *window) {
  for (size_t i = 0; i < ARRAY_LENGTH(s_lines); ++i) {
    destroy_animation(&s_lines[i].animations[0]);
    destroy_animation(&s_lines[i].animations[1]);
    text_layer_destroy(s_lines[i].layers[0]);
    text_layer_destroy(s_lines[i].layers[1]);
    s_lines[i].layers[0] = NULL;
    s_lines[i].layers[1] = NULL;
  }

  text_layer_destroy(s_weather_layer);
  text_layer_destroy(s_day_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_battery_layer);
  s_weather_layer = NULL;
  s_day_layer = NULL;
  s_date_layer = NULL;
  s_battery_layer = NULL;
}

static void init(void) {
  load_settings();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(update_battery);

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(256, 64);
  request_weather();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
