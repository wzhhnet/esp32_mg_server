#ifndef _3461_AS_H
#define _3461_AS_H

#include <stdint.h>

enum {
  _3461_AS_OK,
  _3461_AS_ERR,
};

// PIN index
enum {
  _3461_AS_PIN_A,
  _3461_AS_PIN_B,
  _3461_AS_PIN_C,
  _3461_AS_PIN_D,
  _3461_AS_PIN_E,
  _3461_AS_PIN_F,
  _3461_AS_PIN_G,
  _3461_AS_PIN_DP,
  _3461_AS_PIN_DIG1,
  _3461_AS_PIN_DIG2,
  _3461_AS_PIN_DIG3,
  _3461_AS_PIN_DIG4,
  _3461_AS_PIN_NUM
}; 

// Digit index
enum {
  LED_INDIX_1,
  LED_INDIX_2,
  LED_INDIX_3,
  LED_INDIX_4,
  LED_INDEX_NUM
};

typedef enum {
  LED_DISPLAY_NONE = 0b00000000, // clear
  LED_DISPLAY_0 = 0b00111111, // ABCDEF
  LED_DISPLAY_1 = 0b00000110, // BC
  LED_DISPLAY_2 = 0b01011011, // ABDEG
  LED_DISPLAY_3 = 0b01001111, // ABCDG
  LED_DISPLAY_4 = 0b01100110, // BCFG
  LED_DISPLAY_5 = 0b01101101, // ACDFG
  LED_DISPLAY_6 = 0b01111101, // ACDEFG
  LED_DISPLAY_7 = 0b00000111, // ABC
  LED_DISPLAY_8 = 0b01111111, // ABCDEFG
  LED_DISPLAY_9 = 0b01101111, // ABCDFG
} led_segments;

typedef struct {
  led_segments segs;
  bool dp; 
} one_digit;

typedef struct {
  one_digit leds[LED_INDEX_NUM];
} four_digit;

void _3461_as_start();
void _3461_as_stop();
int _3461_as_config(uint8_t *pin, uint8_t size);
int _3461_as_update(four_digit* fd);

#endif //_3461_AS_H