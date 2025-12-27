#include "esp_wifi.h"
#include "mongoose.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

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
  }
}

void wifi_init(const char *ssid, const char *pass) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

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
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    MG_INFO(("connected to ap SSID:%s pass:%s", ssid, pass));
  } else if (bits & WIFI_FAIL_BIT) {
    MG_ERROR(("Failed to connect to SSID:%s, pass:%s", ssid, pass));
  } else {
    MG_ERROR(("UNEXPECTED EVENT"));
  }
}

void wifi_scan(bool block) {
  esp_wifi_scan_start(NULL, block);
}

void wifi_scan_result(struct mg_str *out) {
  wifi_ap_record_t ap_info[10];
  uint16_t ap_count = 10;
  memset(ap_info, 0, sizeof(ap_info));
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_info));
  MG_INFO(("Total APs scanned = %d", ap_count));
  size_t ofs = 0;
  ofs += mg_snprintf(out->buf + ofs, out->len - ofs, "[");
  for (uint16_t i = 0; i < ap_count; ++i) {
    ofs += mg_snprintf(out->buf + ofs, out->len - ofs,
                            "{\"ssid\":\"%s\", \"rssi\":%d, \"isopened\": %d}",
                            ap_info[i].ssid, ap_info[i].rssi,
                            (ap_info[i].authmode == WIFI_AUTH_OPEN) ? 1 : 0);
    if (i < ap_count - 1) {
      ofs += mg_snprintf(out->buf + ofs, out->len - ofs, ",");
    }
  }
  ofs += mg_snprintf(out->buf + ofs, out->len - ofs, "]");
  out->len = ofs;
}