#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#define MY_UUID { 0xAE, 0xC3, 0x6F, 0x0D, 0x05, 0x17, 0x43, 0x9B, 0x81, 0x81, 0x05, 0x4E, 0xF5, 0x25, 0xC8, 0x65 }

#define SHOW_MOON 1
#define SHOW_DATE 1
#define SHOW_SECONDS 1

#define STOUGH_LAYOUT 1

PBL_APP_INFO(MY_UUID,
             "BN0046", "ryck.me",
             0, 1, /* App version */
             RESOURCE_ID_IMAGE_MENU_ICON,
             APP_INFO_WATCH_FACE);

Window window;

Layer parent;           // Parent Layer
BmpContainer cursor_layer;    // Colon Layer

GFont custom_font21;
GFont custom_font45;
GFont moon_font30;

TextLayer month; // Month Layer
TextLayer date; // Date Layer
TextLayer ampm; // AM/PM Layer
TextLayer seconds; // Seconds Layer
TextLayer moon; // Moon Layer

GFont custom_font21;
GFont custom_font45;
GFont moon_font30;

AppTimerHandle timer_handle;

#define TOTAL_IMAGE_SLOTS 4
#define NUMBER_OF_IMAGES 10
#define EMPTY_SLOT -1

const int IMAGE_RESOURCE_IDS[NUMBER_OF_IMAGES] = {
  RESOURCE_ID_IMAGE_NUM_0, RESOURCE_ID_IMAGE_NUM_1, RESOURCE_ID_IMAGE_NUM_2,
  RESOURCE_ID_IMAGE_NUM_3, RESOURCE_ID_IMAGE_NUM_4, RESOURCE_ID_IMAGE_NUM_5,
  RESOURCE_ID_IMAGE_NUM_6, RESOURCE_ID_IMAGE_NUM_7, RESOURCE_ID_IMAGE_NUM_8,
  RESOURCE_ID_IMAGE_NUM_9
};

BmpContainer image_containers[TOTAL_IMAGE_SLOTS];

int image_slot_state[TOTAL_IMAGE_SLOTS] = {EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT};

char *itoa(int num)
{
  static char buff[20] = {};
  int i = 0, temp_num = num, length = 0;
  char *string = buff;

  if(num >= 0) {
    // count how many characters in the number
    while(temp_num) {
      temp_num /= 10;
      length++;
    }

    // assign the number to the buffer starting at the end of the
    // number and going to the begining since we are doing the
    // integer to character conversion on the last number in the
    // sequence
    for(i = 0; i < length; i++) {
      buff[(length-1)-i] = '0' + (num % 10);
      num /= 10;
    }
    buff[i] = '\0'; // can't forget the null byte to properly end our string
  }
  else
    return "Unsupported Number";

  return string;
}

// Define Reference (recent) Full Moon
// Reference Full Moon delta from T_0 = y-m-d 0000-00-00 (or 2 BCE - Dec - 31)
// ref: http://aa.usno.navy.mil/data/docs/JulianDate.php
//     T_0 -> JD_0 = 1721056.5
// Full Moon Date in Desired Timezone: T_1 = March 17, 1900 02:09:36
//     T_1 -> JD_1 = 2415095.59
// Original Value...
// #define JD_MOON_EPOCH_DELTA 694039.09
// Full Moon Date in UTC: Jan 27  04:38, 2013
// ref: http://eclipse.gsfc.nasa.gov/phase/phase2001gmt.html
//   Convert to Los Angeles Time: T_1 = Jan 26  20:35, 2013 PST
//     T_1 -> JD_1 = 2456319.357639
#define JD_MOON_EPOCH_DELTA 735262.8576

int get_moon_phase(int y, int m, int d) {
  /*
    calculates the moon phase (0-7), accurate to 1 segment.
    0 = > new moon.
    4 => full moon.
  */
  int c,e;
  double jd;
  int b;

  if (m < 3) {
      y--;
      m += 12;
  }
  ++m;
  c = 365.25*y;
  e = 30.438*m;  // corrected to (365.25/12) orig = 30.6
  jd = c+e+d-JD_MOON_EPOCH_DELTA;  /* jd is total days elapsed */
  jd /= 29.53059; /* divide by the moon cycle (29.53 days) */
  b = (int) jd;   /* int(jd) -> b, take integer part of jd */
  jd -= b; /* subtract integer part to leave fractional part of original jd */
  b = jd*8 + 0.5;    /* scale fraction from 0-8 and round by adding 0.5 */
  b = b & 7;       /* 0 and 8 are the same so turn 8 into 0 */
  return b;
}

unsigned short get_display_hour(unsigned short hour) {

  if (clock_is_24h_style()) {
    return hour;
  }

  unsigned short display_hour = hour % 12;

  // Converts "0" to "12"
  return display_hour ? display_hour : 12;

}

void load_digit_image_into_slot(int slot_number, int digit_value) {

  if ((slot_number < 0) || (slot_number >= TOTAL_IMAGE_SLOTS)) {
    return;
  }

  if ((digit_value < 0) || (digit_value > 9)) {
    return;
  }

  if (image_slot_state[slot_number] != EMPTY_SLOT) {
    return;
  }

  image_slot_state[slot_number] = digit_value;
  bmp_init_container(IMAGE_RESOURCE_IDS[digit_value], &image_containers[slot_number]);

  int x;
  int y;
  if(slot_number == 0) {
    x = 2;   // 4
    y = 54;
  }
  if(slot_number == 1) {
    x = 34;  // 34
    y = 54;
  }
  if(slot_number == 2) {
    x = 72;  // 74
    y = 54;
  }
  if(slot_number == 3) {
    x = 103; // 105
    y = 54;
  }

  image_containers[slot_number].layer.layer.frame.origin.x = x;
  image_containers[slot_number].layer.layer.frame.origin.y = y;

  layer_add_child(&window.layer, &image_containers[slot_number].layer.layer);

}

void unload_digit_image_from_slot(int slot_number) {
  /*

     Removes the digit from the display and unloads the image resource
     to free up RAM.

     Can handle being called on an already empty slot.

  */

  if (image_slot_state[slot_number] != EMPTY_SLOT) {
    layer_remove_from_parent(&image_containers[slot_number].layer.layer);
    bmp_deinit_container(&image_containers[slot_number]);
    image_slot_state[slot_number] = EMPTY_SLOT;
  }
}

void display_value(unsigned short value, unsigned short row_number, bool show_first_leading_zero) {
  /*

     Displays a numeric value between 0 and 99 on screen.

     Rows are ordered on screen as:

       Row 0
       Row 1

     Includes optional blanking of first leading zero,
     i.e. displays ' 0' rather than '00'.

  */
  value = value % 100; // Maximum of two digits per row.

  // Column order is: | Column 0 | Column 1 |
  // (We process the columns in reverse order because that makes
  // extracting the digits from the value easier.)
  for (int column_number = 1; column_number >= 0; column_number--) {
    int slot_number = (row_number * 2) + column_number;
    unload_digit_image_from_slot(slot_number);
    if (!((value == 0) && (column_number == 0) && !show_first_leading_zero)) {
      load_digit_image_into_slot(slot_number, value % 10);
    }
    value = value / 10;
  }
}

void update_display_seconds(PblTm *tick_time) {
    static char seconds_text[] = "00";
    string_format_time(seconds_text, sizeof(seconds_text), "%S", tick_time);
    text_layer_set_text(&seconds, seconds_text);
}

void update_display_minutes(PblTm *tick_time) {
    display_value(tick_time->tm_min, 1, true);
}

void update_display_hours(PblTm *tick_time) {
    static char am_pm_text[] = "PM";

    display_value(get_display_hour(tick_time->tm_hour), 0, false);
    // AM/PM
    string_format_time(am_pm_text, sizeof(am_pm_text), "%p", tick_time);

    if (!clock_is_24h_style()) {
      text_layer_set_text(&ampm, am_pm_text);
    }
}

void update_display_day(PblTm *tick_time) {
    // Day
    static char date_text[] = "00.00";
    string_format_time(date_text, sizeof(date_text), "%m-%d", tick_time);
    text_layer_set_text(&date, date_text);
}

void update_display_month(PblTm *tick_time) {
    // Month
    static char month_text[] = "AAA";
    string_format_time(month_text, sizeof(month_text), "%a", tick_time);
    text_layer_set_text(&month, month_text);
}

void update_display_moon(PblTm *tick_time) {
  // Moon
    int year_number = 2012;
    int month_number = 01;
    int day_number = 01;
    year_number = tick_time->tm_year + 1900;
    month_number = tick_time->tm_mon;
    day_number = tick_time->tm_mday;

    char *temp = itoa(get_moon_phase(year_number, month_number, day_number));

    text_layer_set_text(&moon, temp);
}

void update_display(PblTm *tick_time) {

  update_display_seconds(tick_time);
  update_display_minutes(tick_time);
  update_display_hours(tick_time);
  update_display_day(tick_time);
  update_display_month(tick_time);
  update_display_moon(tick_time);
}

void handle_second_tick(AppContextRef ctx, PebbleTickEvent *t) {
  (void)t;
  (void)ctx;


  if ((t->units_changed & SECOND_UNIT) != 0 && SHOW_SECONDS) {
    update_display_seconds(t->tick_time);
  }

  if ((t->units_changed & MINUTE_UNIT) != 0) {
    update_display_minutes(t->tick_time);
  }

  if ((t->units_changed & HOUR_UNIT) != 0) {
    update_display_hours(t->tick_time);
  }

  if ((t->units_changed & DAY_UNIT) != 0) {
    #if SHOW_DATE
      update_display_day(t->tick_time);
    #endif
    #if SHOW_MOON
      update_display_moon(t->tick_time);
    #endif
  }

  if ((t->units_changed & MONTH_UNIT) != 0 && SHOW_DATE) {
    update_display_month(t->tick_time);
  }

}

void LayerSetup(PblTm *tick_time) {

  //21pt
  custom_font21 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITAL21));
  //40pt
  custom_font45 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITAL45));
  //Moon
  moon_font30 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_CLIMAICONS30));

  //Init the parent layer at (0,0) and size (144 X 168)
  layer_init(&parent, GRect(0, 0, 144, 168));

  text_layer_init(&month, GRect(-25, 25, 60, 30));   // Month
#if STOUGH_LAYOUT
  text_layer_init(&date, GRect(45, 25, 60, 30));  // Date
  text_layer_init(&ampm, GRect(112, 48, 30, 30));  // AM/PM
  text_layer_init(&seconds, GRect(92, 95, 60, 60));  // Date
  text_layer_init(&moon, GRect(108, 5, 40, 40));  // Date
#else
  text_layer_init(&date, GRect(48, 25, 30, 30));  // Date
  text_layer_init(&ampm, GRect(5, 100, 30, 30));  // AM/PM
  text_layer_init(&seconds, GRect(92, 94, 60, 60));  // Date
  text_layer_init(&moon, GRect(105, 5, 60, 60));  // Date
#endif 


  text_layer_set_font(&month, custom_font21);
  text_layer_set_font(&date, custom_font21);
  text_layer_set_font(&ampm, custom_font21);
  text_layer_set_font(&seconds, custom_font45);
  text_layer_set_font(&moon, moon_font30);

  text_layer_set_background_color(&month, GColorBlack);
  text_layer_set_background_color(&date, GColorBlack);
  text_layer_set_background_color(&ampm, GColorBlack);
  text_layer_set_background_color(&seconds, GColorBlack);
  text_layer_set_background_color(&moon, GColorBlack);

  text_layer_set_text_color(&month, GColorWhite);
  text_layer_set_text_color(&date, GColorWhite);
  text_layer_set_text_color(&ampm, GColorWhite);
  text_layer_set_text_color(&seconds, GColorWhite);
  text_layer_set_text_color(&moon, GColorWhite);

  text_layer_set_text_alignment(&month, GTextAlignmentRight);
  text_layer_set_text_alignment(&date, GTextAlignmentLeft);
  text_layer_set_text_alignment(&ampm, GTextAlignmentLeft);
  text_layer_set_text_alignment(&seconds, GTextAlignmentLeft);
  text_layer_set_text_alignment(&moon, GTextAlignmentLeft);


  layer_add_child(&parent, &date.layer);
  layer_add_child(&parent, &month.layer);
  layer_add_child(&parent, &ampm.layer);
  layer_add_child(&parent, &seconds.layer);
  layer_add_child(&parent, &moon.layer);

  layer_add_child(&window.layer, &parent);

  bmp_init_container(RESOURCE_ID_IMAGE_COLON, &cursor_layer);
  cursor_layer.layer.layer.frame.origin.x = 65;  // 64
  cursor_layer.layer.layer.frame.origin.y = 60;
  layer_add_child(&parent, &cursor_layer.layer.layer);


  update_display(tick_time);
}

void handle_deinit(AppContextRef ctx) {
  (void)ctx;

  // Bitmaps
  bmp_deinit_container(&cursor_layer); // Colon
  for (int i = 0; i < TOTAL_IMAGE_SLOTS; i++)
    bmp_deinit_container(&image_containers[i]);

  // Fonts
  fonts_unload_custom_font(custom_font21);
  fonts_unload_custom_font(custom_font45);
  fonts_unload_custom_font(moon_font30);
}

void handle_init(AppContextRef ctx) {
  (void)ctx;

  window_init(&window, "BN0046 Window");
  window_stack_push(&window, true /* Animated */);
  window_set_background_color(&window, GColorBlack);

  // If you neglect to call this, all `resource_get_handle()` requests
  // will return NULL.
  resource_init_current_app(&BN0046RESOURCES);

  // Avoids a blank screen on watch start.
  PblTm tick_time;

  get_time(&tick_time);

  LayerSetup(&tick_time);
}


void pbl_main(void *params) {

  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .deinit_handler = &handle_deinit,
    
    .tick_info = {
      .tick_handler = &handle_second_tick,
      .tick_units = SECOND_UNIT
    }
  };
  app_event_loop(params, &handlers);
}
