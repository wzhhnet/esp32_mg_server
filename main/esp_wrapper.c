#ifndef MODULE_TAG
#define MODULE_TAG "wrapper_sdk"
#endif

#include "esp_wrapper.h"

#ifndef RETURN_IF_FAIL
#define RETURN_IF_FAIL(EC, RC)                                     \
  if ((EC) != ESP_OK) {                                            \
    ESP_LOGE(MODULE_TAG, "Failed cause: %s", esp_err_to_name(EC)); \
    return RC;                                                     \
  }
#endif

static char g_sysinfo[64];
const char* chip_info();
const struct chip_info_t {
  uint16_t model;
  const char* info;
} c_chip_map[] = {
    {CHIP_ESP32, "ESP32"},
    {CHIP_ESP32S2, "ESP32-S2"},
    {CHIP_ESP32S3, "ESP32-S3"},
    {CHIP_ESP32C3, "ESP32-C3"},
    {CHIP_ESP32C2, "ESP32-C2"},
    {CHIP_ESP32C6, "ESP32-C6"},
    {CHIP_ESP32H2, "ESP32-H2"},
    {CHIP_ESP32P4, "ESP32-P4"},
    {CHIP_ESP32C61, "ESP32-C61"},
    {CHIP_ESP32C5, "ESP32-C5"},
    {CHIP_ESP32H21, "ESP32-H21"},
    {CHIP_ESP32H4, " ESP32-H4"},
    {CHIP_POSIX_LINUX, "POSIX_LINUX"},
};

bool wrap_gpio_config(struct mg_str in, struct mg_str* out) {
  gpio_config_t cfg = {};
  struct mg_str pins = mg_json_get_tok(in, "$.pins");
  if (!pins.buf)
    goto ERR;
  size_t ofs = 0;
  struct mg_str val;
  while ((ofs = mg_json_next(pins, ofs, NULL, &val)) > 0) {
    uint8_t pin;
    if (mg_str_to_num(val, 10, &pin, sizeof(pin))) {
      cfg.pin_bit_mask |= (1ULL << pin);
    } else
      goto ERR;
  }

  cfg.mode = mg_json_get_long(in, "$.mode", GPIO_MODE_DISABLE);
  cfg.pull_up_en = mg_json_get_long(in, "$.pull_up_en", GPIO_PULLUP_DISABLE);
  cfg.pull_down_en =
      mg_json_get_long(in, "$.pull_down_en", GPIO_PULLDOWN_DISABLE);
  cfg.intr_type = mg_json_get_long(in, "$.intr_type", GPIO_INTR_DISABLE);
  esp_err_t r = gpio_config(&cfg);
  if (ESP_OK != r)
    goto ERR;

  out->len = mg_snprintf(out->buf, out->len, JSON_SUCCESS);
  return true;
ERR:
  out->len = mg_snprintf(out->buf, out->len, JSON_INVALID_PARAMS);
  return false;
}

bool wrap_gpio_info(struct mg_str in, struct mg_str* out) {
  const char* msg = JSON_SUCCESS;
  gpio_io_config_t cfg = {};
  long pin = mg_json_get_long(in, "$.pin", -1);
  if (pin == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }
  esp_err_t r = gpio_get_io_config(pin, &cfg);
  if (ESP_OK != r) {
    msg = JSON_ESP32_ERROR;
    goto ERR;
  }
  out->len = mg_snprintf(out->buf, out->len,
                         "{\"cause\":\"success\", \
    \"info\": { \
    \"pu\":%d,\
    \"pd\":%d,\
      \"ie\":%d,\
      \"oe\":%d,\
      \"oe_ctrl_by_periph\":%d,\
      \"oe_inv\":%d,\
      \"od\":%d,\
      \"slp_sel\":%d}}",
                         cfg.pu, cfg.pd, cfg.ie, cfg.oe, cfg.oe_ctrl_by_periph,
                         cfg.oe_inv, cfg.od, cfg.slp_sel);
  return true;

ERR:
  out->len = mg_snprintf(out->buf, out->len, msg);
  return false;
}

bool wrap_gpio_mode(struct mg_str in, struct mg_str* out) {
  const char* msg = JSON_SUCCESS;
  long pin = mg_json_get_long(in, "$.pin", -1);
  if (pin == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }
  long mode = mg_json_get_long(in, "$.mode", -1);
  if (mode == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }
  esp_err_t r = gpio_set_direction(pin, mode);
  if (ESP_OK != r) {
    msg = JSON_ESP32_ERROR;
    goto ERR;
  }
  out->len = mg_snprintf(out->buf, out->len, JSON_SUCCESS);
  return true;

ERR:
  out->len = mg_snprintf(out->buf, out->len, msg);
  return false;
}

bool wrap_gpio_level(struct mg_str in, struct mg_str* out) {
  const char* msg = JSON_SUCCESS;
  long pin = mg_json_get_long(in, "$.pin", -1);
  if (pin == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }
  long level = mg_json_get_long(in, "$.level", -1);
  if (level == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }
  esp_err_t r = gpio_set_level(pin, level);
  if (ESP_OK != r) {
    msg = JSON_ESP32_ERROR;
    goto ERR;
  }
  out->len = mg_snprintf(out->buf, out->len, JSON_SUCCESS);
  return true;

ERR:
  out->len = mg_snprintf(out->buf, out->len, msg);
  return false;
}

bool wrap_pwm_config(struct mg_str in, struct mg_str* out) {
  const char* msg = JSON_SUCCESS;
  ledc_timer_config_t timer_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_channel_config_t ch_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .intr_type = LEDC_INTR_DISABLE,
  };

  long pin = mg_json_get_long(in, "$.pin", -1);
  if (pin == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  } else {
    ch_cfg.gpio_num = pin;
  }

  long ch = mg_json_get_long(in, "$.channel", -1);
  if (ch == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  } else {
    ch_cfg.channel = ch;
  }

  long freq = mg_json_get_long(in, "$.freq", -1);
  if (freq == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  } else {
    timer_cfg.freq_hz = freq;
  }

  long timer = mg_json_get_long(in, "$.timer", -1);
  if (timer == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  } else {
    ch_cfg.timer_sel = timer;
    timer_cfg.timer_num = timer;
  }

  long resolution = mg_json_get_long(in, "$.resolution", -1);
  if (resolution == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  } else {
    timer_cfg.duty_resolution = resolution;
  }

  esp_err_t r = ledc_timer_config(&timer_cfg);
  if (ESP_OK != r) {
    ESP_LOGE(MODULE_TAG, "ledc_timer_config failed(%d)", r);
    msg = JSON_ESP32_ERROR;
    goto ERR;
  }
  r = ledc_channel_config(&ch_cfg);
  if (ESP_OK != r) {
    msg = JSON_ESP32_ERROR;
    ESP_LOGE(MODULE_TAG, "ledc_channel_config failed(%d)", r);
    goto ERR;
  }
  out->len = mg_snprintf(out->buf, out->len, JSON_SUCCESS);
  return true;

ERR:
  out->len = mg_snprintf(out->buf, out->len, msg);
  return false;
}

bool wrap_pwm_set_duty(struct mg_str in, struct mg_str* out) {
  const char* msg = JSON_SUCCESS;
  long ch = mg_json_get_long(in, "$.channel", -1);
  if (ch == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }
  long duty = mg_json_get_long(in, "$.duty", -1);
  if (duty == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }
  esp_err_t r = ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
  if (ESP_OK != r) {
    msg = JSON_ESP32_ERROR;
    goto ERR;
  }
  r = ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
  if (ESP_OK != r) {
    msg = JSON_ESP32_ERROR;
    goto ERR;
  }
  out->len = mg_snprintf(out->buf, out->len, JSON_SUCCESS);
  return true;

ERR:
  out->len = mg_snprintf(out->buf, out->len, msg);
  return false;
}

bool wrap_pwm_stop(struct mg_str in, struct mg_str* out) {
  const char* msg = JSON_SUCCESS;
  long ch = mg_json_get_long(in, "$.channel", -1);
  if (ch == -1) {
    msg = JSON_INVALID_PARAMS;
    goto ERR;
  }

  esp_err_t r = ledc_stop(LEDC_LOW_SPEED_MODE, ch, 0);
  if (ESP_OK != r) {
    msg = JSON_ESP32_ERROR;
    goto ERR;
  }
  out->len = mg_snprintf(out->buf, out->len, JSON_SUCCESS);
  return true;

ERR:
  out->len = mg_snprintf(out->buf, out->len, msg);
  return false;
}

bool wrap_sys_info(struct mg_str in, struct mg_str* out) {
  out->len =
      mg_snprintf(out->buf, out->len,
                  "{\"cause\":\"success\", \"info\":\"%s\"}", chip_info());
  return true;
}

static int fill_features(int size, uint32_t feats) {
  int offset;
  if (size > 0 && (feats | CHIP_FEATURE_EMB_FLASH)) {
    offset = sizeof(g_sysinfo) - size;
    size -= snprintf(g_sysinfo + offset, size, "|Flash");
  }
  if (size > 0 && (feats | CHIP_FEATURE_WIFI_BGN)) {
    offset = sizeof(g_sysinfo) - size;
    size -= snprintf(g_sysinfo + offset, size, "|WiFi");
  }
  if (size > 0 && (feats | CHIP_FEATURE_BLE)) {
    offset = sizeof(g_sysinfo) - size;
    size -= snprintf(g_sysinfo + offset, size, "|BLE");
  }

  if (size > 0 && (feats | CHIP_FEATURE_BT)) {
    offset = sizeof(g_sysinfo) - size;
    size -= snprintf(g_sysinfo + offset, size, "|BT");
  }

  if (size > 0 && (feats | CHIP_FEATURE_IEEE802154)) {
    offset = sizeof(g_sysinfo) - size;
    size -= snprintf(g_sysinfo + offset, size, "|IEEE802.15.4");
  }

  if (size > 0 && (feats | CHIP_FEATURE_EMB_PSRAM)) {
    offset = sizeof(g_sysinfo) - size;
    size -= snprintf(g_sysinfo + offset, size, "|PSRAM");
  }
  return size;
}

const char* chip_info() {
  int size = sizeof(g_sysinfo);
  memset(&g_sysinfo, 0, size);
  esp_chip_info_t info;
  esp_chip_info(&info);

  for (int i = 0; i < sizeof(c_chip_map) / sizeof(struct chip_info_t); ++i) {
    if (c_chip_map[i].model == info.model) {
      size -= snprintf(g_sysinfo, size, "%s", c_chip_map[i].info);
      break;
    }
  }

  if (size) {
    size = fill_features(size, info.features);
  }
  if (size) {
    size -= snprintf(g_sysinfo + (sizeof(g_sysinfo) - size), size, " ver.%u",
                     info.revision);
  }
  if (size) {
    size -= snprintf(g_sysinfo + (sizeof(g_sysinfo) - size), size, " core.%u",
                     info.cores);
  }

  g_sysinfo[sizeof(g_sysinfo) - 1] = 0;
  return g_sysinfo;
}

bool wrap_wifi_scan(struct mg_str in, struct mg_str* out) {
  if (!wifi_is_scanning()) {
    wifi_scan_start();
  }
  wifi_scan_result(out);
  return true;
}