#include "esp_wifi.h"
#include "mongoose.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi.h"

#define WIFI_SSID "ESP32"
#define WIFI_PASS "12345678"
#define WIFI_CHANNEL 1
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_SCAN_DONE_BIT BIT2
#define WIFI_SCAN_LIST_SIZE     10
#define WIFI_MAX_RETRY       5

static struct wifi_context {
  SemaphoreHandle_t mutex;
  wifi_ap_record_t aps[WIFI_SCAN_LIST_SIZE];
  uint16_t ap_count;
  bool busy;
  bool provisioned;
} s_wifi_ctx = {
    .aps = {},
    .ap_count = 0,
    .busy = false,
    .provisioned = false
};

static void wifi_set_busy(bool busy) {
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  s_wifi_ctx.busy = busy;
  xSemaphoreGive(s_wifi_ctx.mutex);
}

static bool wifi_get_busy() {
  bool busy;
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  busy = s_wifi_ctx.busy;
  xSemaphoreGive(s_wifi_ctx.mutex);
  return busy;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  static int retry_count = 0;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    MG_INFO(("WiFi started"));
    if (s_wifi_ctx.provisioned) {
      esp_wifi_connect();
    }
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (retry_count < WIFI_MAX_RETRY) {
      wifi_set_busy(true);
      esp_wifi_connect();
      retry_count++;
    } else {
      MG_INFO(("Failed to connect to SSID, transfer to NOT_PROVISIONED"));
      wifi_set_busy(false);
      s_wifi_ctx.provisioned = false;
    }
    MG_INFO(("Connecting to the AP fail, attempt #%d", retry_count));
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    MG_INFO(("Got IP ADDRESS: " IPSTR, IP2STR(&event->ip_info.ip)));
    wifi_set_busy(false);
    retry_count = 0;
  } else if (event_id == WIFI_EVENT_SCAN_DONE) {
    MG_INFO(("WiFi scan done"));
    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    s_wifi_ctx.ap_count = WIFI_SCAN_LIST_SIZE;
    memset(s_wifi_ctx.aps, 0, sizeof(s_wifi_ctx.aps));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&s_wifi_ctx.ap_count, s_wifi_ctx.aps));
    s_wifi_ctx.busy = false;
    xSemaphoreGive(s_wifi_ctx.mutex);
  }
}

static void wifi_start_sta(wifi_config_t *cfg) {

  esp_netif_create_default_wifi_sta();
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
}

static void wifi_start_provisioning()
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    esp_netif_create_default_wifi_ap();
    wifi_config_t wifi_config = {
      .ap = {
        .ssid = WIFI_SSID,
        .ssid_len = strlen(WIFI_SSID),
        .channel = WIFI_CHANNEL,
        .password = WIFI_PASS,
        .max_connection = 6,
        .authmode = WIFI_AUTH_WPA2_PSK,
        .pmf_cfg = {
            .required = true,
        },
        .gtk_rekey_interval = 600,
       },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_start());
    MG_INFO(("wifi_init_apsta finished. SSID:%s password:%s channel:%d",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL));
}

void wifi_init() {
  s_wifi_ctx.mutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
  /*Initialize WiFi */
  wifi_init_config_t initcfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&initcfg));
  wifi_config_t cfg = {};
  esp_wifi_get_config(WIFI_IF_STA, &cfg);
  if (cfg.sta.ssid[0] != '\0') {
    MG_INFO(("WiFi configured, connecting to SSID=%s", cfg.sta.ssid));
    s_wifi_ctx.provisioned = true;
    wifi_start_sta(&cfg);
  } else {
    MG_INFO(("WiFi not configured, starting AP+STA mode"));
    wifi_start_provisioning();
  }
}

void wifi_scan_start() {
  if (!wifi_get_busy()) {
    wifi_set_busy(true);
    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
  } else {
    MG_ERROR(("WiFi is busy, cannot start scan"));
  }
}

void wifi_scan_result(struct mg_str *out) {
  size_t ofs = 0;
  ofs += mg_snprintf(out->buf + ofs, out->len - ofs, "[");
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  for (uint16_t i = 0; i < s_wifi_ctx.ap_count; ++i) {
    ofs += mg_snprintf(out->buf + ofs, out->len - ofs,
                            "{\"ssid\":\"%s\", \"rssi\":%d, \"isopened\": %d}",
                            s_wifi_ctx.aps[i].ssid, s_wifi_ctx.aps[i].rssi,
                            (s_wifi_ctx.aps[i].authmode == WIFI_AUTH_OPEN) ? 1 : 0);
    if (i < s_wifi_ctx.ap_count - 1) {
      ofs += mg_snprintf(out->buf + ofs, out->len - ofs, ",");
    }
  }
  xSemaphoreGive(s_wifi_ctx.mutex);
  ofs += mg_snprintf(out->buf + ofs, out->len - ofs, "]");
  out->len = ofs;
}

void wifi_connect(const char* ssid, const char* pass) {
  wifi_config_t cfg = {};
  strncpy((char*)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
  strncpy((char*)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_connect());
  wifi_set_busy(true);
}