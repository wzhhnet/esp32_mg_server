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

static EventGroupHandle_t s_wifi_event_group;
static struct wifi_context {
  SemaphoreHandle_t mutex;
  wifi_ap_record_t aps[WIFI_SCAN_LIST_SIZE];
  uint16_t ap_count;
  bool scanning;
} s_wifi_ctx = {
    .aps = {},
    .ap_count = 0,
    .scanning = false
};

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  static int retry_count = 0;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    retry_count++;
    MG_INFO(("Connecting to the AP fail, attempt #%d", retry_count));
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    MG_INFO(("Got IP ADDRESS: " IPSTR, IP2STR(&event->ip_info.ip)));
    retry_count = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  } else if (event_id == WIFI_EVENT_SCAN_DONE) {
    MG_INFO(("WiFi scan done"));
    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    s_wifi_ctx.ap_count = WIFI_SCAN_LIST_SIZE;
    memset(s_wifi_ctx.aps, 0, sizeof(s_wifi_ctx.aps));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&s_wifi_ctx.ap_count, s_wifi_ctx.aps));
    s_wifi_ctx.scanning = false;
    xSemaphoreGive(s_wifi_ctx.mutex);
    xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
  }
}

void wifi_init_sta(const char *ssid, const char *pass) {

  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
  wifi_config_t wc = {};
  strncpy((char *) wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
  strncpy((char *) wc.sta.password, pass, sizeof(wc.sta.password));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());

  MG_INFO(("Trying to connect to SSID:%s pass:%s", ssid, pass));
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_SCAN_DONE_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    MG_INFO(("connected to ap SSID:%s pass:%s", ssid, pass));
  } else if (bits & WIFI_FAIL_BIT) {
    MG_ERROR(("Failed to connect to SSID:%s, pass:%s", ssid, pass));
  } else {
    MG_ERROR(("UNEXPECTED EVENT"));
  }
}

void wifi_init_apsta(void* arg)
{
    s_wifi_event_group = xEventGroupCreate();
    s_wifi_ctx.mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        arg,
                                                        NULL));
    /*Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
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

void wifi_scan_start() {
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  s_wifi_ctx.scanning = true;
  xSemaphoreGive(s_wifi_ctx.mutex);
  ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
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

bool wifi_is_scanning() {
  bool scanning;
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  scanning = s_wifi_ctx.scanning;
  xSemaphoreGive(s_wifi_ctx.mutex);
  return scanning;
}