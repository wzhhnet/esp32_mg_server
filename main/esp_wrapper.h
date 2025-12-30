#ifndef ESP_WRAPPER_H
#define ESP_WRAPPER_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "mongoose.h"
#include "wifi.h"

#define JSON_SUCCESS "{\"cause\":\"success\"}"
#define JSON_INVALID_PARAMS "{\"cause\":\"invalid parameters\"}"
#define JSON_INVALID_API "{\"cause\":\"invalid rest api\"}"
#define JSON_ESP32_ERROR "{\"cause\":\"esp32 internal error\"}"

typedef bool(*wrap_func)(struct mg_str, struct mg_str*);

bool wrap_gpio_config(struct mg_str in, struct mg_str* out);
bool wrap_gpio_info(struct mg_str in, struct mg_str* out);
bool wrap_gpio_mode(struct mg_str in, struct mg_str* out);
bool wrap_gpio_level(struct mg_str in, struct mg_str* out);
bool wrap_pwm_config(struct mg_str in, struct mg_str* out);
bool wrap_pwm_set_duty(struct mg_str in, struct mg_str* out);
bool wrap_pwm_stop(struct mg_str in, struct mg_str* out);
bool wrap_sys_info(struct mg_str in, struct mg_str* out);
bool wrap_wifi_scan(struct mg_str in, struct mg_str* out);

#endif