#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "3461as.h"

#define _PIN_MASK(_PIN, _DISP)  (( 1U << (_PIN) ) & (_DISP))
#define _PIN_LEVEL(_PIN, _DISP) (_PIN_MASK(_PIN, _DISP) > 0 ? 1 : 0)

static struct leds_ctx_t {
  uint8_t index; // digit index
  four_digit r_ctx;
  four_digit w_ctx;
  volatile bool w_flag;
  esp_timer_handle_t timer;
} s_leds_ctx = {
  .index = LED_INDIX_1,
  .r_ctx = {
    .leds = {
      {LED_DISPLAY_NONE, false},
      {LED_DISPLAY_NONE, false},
      {LED_DISPLAY_NONE, false},
      {LED_DISPLAY_NONE, false}
    } 
  },
  .w_ctx = {
    .leds = {
      {LED_DISPLAY_NONE, false},
      {LED_DISPLAY_NONE, false},
      {LED_DISPLAY_NONE, false},
      {LED_DISPLAY_NONE, false}
    }
  },
  .w_flag = false,
  .timer = NULL
};

static uint8_t s_gpio[_3461_AS_PIN_NUM] = {
  8,    // PIN_A
  6,    // PIN_B
  4,    // PIN_C
  9,    // PIN_D
  10,   // PIN_E
  7,    // PIN_F
  5,    // PIN_G
  3,    // PIN_DP
  11,   // PIN_DIG1
  12,   // PIN_DIG2
  13,   // PIN_DIG3
  14,   // PIN_DIG4
};

static void _3461_as_clear() {
  gpio_set_level(s_gpio[LED_INDIX_1], 1);
  gpio_set_level(s_gpio[LED_INDIX_2], 1);
  gpio_set_level(s_gpio[LED_INDIX_3], 1);
  gpio_set_level(s_gpio[LED_INDIX_4], 1);
}

static void _3461_as_init(void) {
  gpio_config_t io_conf = {
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask =
      (1ULL << s_gpio[_3461_AS_PIN_DIG1]) |
      (1ULL << s_gpio[_3461_AS_PIN_DIG2]) |
      (1ULL << s_gpio[_3461_AS_PIN_DIG3]) |
      (1ULL << s_gpio[_3461_AS_PIN_DIG4]) |
      (1ULL << s_gpio[_3461_AS_PIN_A]) |
      (1ULL << s_gpio[_3461_AS_PIN_B]) |
      (1ULL << s_gpio[_3461_AS_PIN_C]) |
      (1ULL << s_gpio[_3461_AS_PIN_D]) |
      (1ULL << s_gpio[_3461_AS_PIN_E]) |
      (1ULL << s_gpio[_3461_AS_PIN_F]) |
      (1ULL << s_gpio[_3461_AS_PIN_G]) |
      (1ULL << s_gpio[_3461_AS_PIN_DP])
  };
  gpio_config(&io_conf);
  _3461_as_clear();
}

static void _3461_as_display(uint8_t dig_index, one_digit* led) {
  gpio_set_level(s_gpio[_3461_AS_PIN_DIG1], dig_index == LED_INDIX_1 ? 0 : 1);
  gpio_set_level(s_gpio[_3461_AS_PIN_DIG2], dig_index == LED_INDIX_2 ? 0 : 1);
  gpio_set_level(s_gpio[_3461_AS_PIN_DIG3], dig_index == LED_INDIX_3 ? 0 : 1);
  gpio_set_level(s_gpio[_3461_AS_PIN_DIG4], dig_index == LED_INDIX_4 ? 0 : 1);
  gpio_set_level(s_gpio[_3461_AS_PIN_A], _PIN_LEVEL(_3461_AS_PIN_A, led->segs));
  gpio_set_level(s_gpio[_3461_AS_PIN_B], _PIN_LEVEL(_3461_AS_PIN_B, led->segs));
  gpio_set_level(s_gpio[_3461_AS_PIN_C], _PIN_LEVEL(_3461_AS_PIN_C, led->segs));
  gpio_set_level(s_gpio[_3461_AS_PIN_D], _PIN_LEVEL(_3461_AS_PIN_D, led->segs));
  gpio_set_level(s_gpio[_3461_AS_PIN_E], _PIN_LEVEL(_3461_AS_PIN_E, led->segs));
  gpio_set_level(s_gpio[_3461_AS_PIN_F], _PIN_LEVEL(_3461_AS_PIN_F, led->segs));
  gpio_set_level(s_gpio[_3461_AS_PIN_G], _PIN_LEVEL(_3461_AS_PIN_G, led->segs));
  gpio_set_level(s_gpio[_3461_AS_PIN_DP], led->dp ? 1 : 0);
}

static void _3461_as_flash(void *arg) {
  if (s_leds_ctx.index == LED_INDIX_1) {
    _3461_as_display(LED_INDIX_1, &s_leds_ctx.r_ctx.leds[LED_INDIX_1]);
    s_leds_ctx.index = LED_INDIX_2;
  } else if (s_leds_ctx.index == LED_INDIX_2) {
    _3461_as_display(LED_INDIX_2, &s_leds_ctx.r_ctx.leds[LED_INDIX_2]);
    s_leds_ctx.index = LED_INDIX_3;
  } else if (s_leds_ctx.index == LED_INDIX_3) {
    _3461_as_display(LED_INDIX_3, &s_leds_ctx.r_ctx.leds[LED_INDIX_3]);
    s_leds_ctx.index = LED_INDIX_4;
  } else if (s_leds_ctx.index == LED_INDIX_4) {
    _3461_as_display(LED_INDIX_4, &s_leds_ctx.r_ctx.leds[LED_INDIX_4]);
    s_leds_ctx.index = LED_INDIX_1;
    if (s_leds_ctx.w_flag) {
      memcpy(&s_leds_ctx.r_ctx, &s_leds_ctx.w_ctx, sizeof(s_leds_ctx.w_ctx));
      s_leds_ctx.w_flag = false;
    }
  }
}

void _3461_as_start() {
  _3461_as_init();
  esp_timer_create_args_t args = {
    .callback = _3461_as_flash,
    .name = "3461as"
  };

  if (s_leds_ctx.timer == NULL) {
    esp_timer_create(&args, &s_leds_ctx.timer);
  }
  esp_timer_start_periodic(s_leds_ctx.timer, 1500); // 1.5 ms
}

int _3461_as_config(uint8_t *pin, uint8_t size) {
  if (pin == NULL || size > _3461_AS_PIN_NUM) {
    return _3461_AS_ERR;
  }
  memcpy(s_gpio, pin, size);
  return _3461_AS_OK;
}

void _3461_as_stop() {
    esp_timer_stop(s_leds_ctx.timer);
    _3461_as_clear();
    gpio_set_level(s_gpio[_3461_AS_PIN_A], 0);
    gpio_set_level(s_gpio[_3461_AS_PIN_B], 0);
    gpio_set_level(s_gpio[_3461_AS_PIN_C], 0);
    gpio_set_level(s_gpio[_3461_AS_PIN_D], 0);
    gpio_set_level(s_gpio[_3461_AS_PIN_E], 0);
    gpio_set_level(s_gpio[_3461_AS_PIN_F], 0);
    gpio_set_level(s_gpio[_3461_AS_PIN_G], 0);
    gpio_set_level(s_gpio[_3461_AS_PIN_DP], 0);
}

int _3461_as_update(four_digit* fd) {
  if (!fd) return _3461_AS_ERR;
  memcpy(&s_leds_ctx.w_ctx, fd, sizeof(s_leds_ctx.w_ctx));
  s_leds_ctx.w_flag = true;
  return _3461_AS_OK;
}
