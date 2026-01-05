#include "esp_wifi.h"
#include "mongoose.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi.h"

#define NVS_WIFI "wifi"
#define NVS_WIFI_PROVISIONED "provisioned"
#define WIFI_SSID "ESP32"
#define WIFI_PASS "12345678"
#define WIFI_CHANNEL 1
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_SCAN_DONE_BIT BIT2
#define WIFI_SCAN_LIST_SIZE     10
#define WIFI_MAX_RETRY       5

ESP_EVENT_DEFINE_BASE(WIFI_USER_EVENT);
enum {
    WIFI_USER_EVENT_SCAN,
    WIFI_USER_EVENT_PROVISION,

};
enum wifi_state {
  WIFI_STATE_IDLE,
  WIFI_STATE_SCANNING,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_REPROVISIONING,
};

static nvs_handle_t s_nvs_wifi_handle = 0;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static struct wifi_context {
  SemaphoreHandle_t mutex;
  uint8_t state;
  wifi_config_t cfg;
  wifi_ap_record_t aps[WIFI_SCAN_LIST_SIZE];
  char ssid[MAX_SSID_LEN+1];
  uint16_t ap_count;
  uint16_t retry_count;
} s_wifi_ctx = {
    .state = WIFI_STATE_IDLE,
    .cfg = {},
    .aps = {},
    .ssid = {},
    .ap_count = 0,
    .retry_count = 0,
};

static void wifi_start_sta(wifi_config_t *cfg) {
  if (s_sta_netif != NULL) {
    esp_netif_destroy_default_wifi(s_sta_netif);
  }
  s_sta_netif = esp_netif_create_default_wifi_sta();
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
}

static void wifi_start_provisioning()
{
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  if (s_ap_netif != NULL) {
    esp_netif_destroy_default_wifi(s_ap_netif);
  }
  if (s_sta_netif != NULL) {
    esp_netif_destroy_default_wifi(s_sta_netif);
  }
  s_ap_netif = esp_netif_create_default_wifi_ap();
  s_sta_netif = esp_netif_create_default_wifi_sta();
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
  ESP_ERROR_CHECK(esp_wifi_start());
  MG_INFO(("wifi_init_apsta finished. SSID:%s password:%s channel:%d",
           WIFI_SSID, WIFI_PASS, WIFI_CHANNEL));
}

static bool wifi_get_provisioned() {
  uint8_t provisioned = 0;
  esp_err_t err = nvs_get_u8(s_nvs_wifi_handle, NVS_WIFI_PROVISIONED, &provisioned);
  return (err == ESP_OK && provisioned);
}

static void wifi_set_provisioned(bool provisioned) {
  nvs_set_u8(s_nvs_wifi_handle, NVS_WIFI_PROVISIONED, provisioned ? 1 : 0);
  nvs_commit(s_nvs_wifi_handle);
}

static void wifi_save_scan_results() {
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  s_wifi_ctx.ap_count = WIFI_SCAN_LIST_SIZE;
  memset(s_wifi_ctx.aps, 0, sizeof(s_wifi_ctx.aps));
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&s_wifi_ctx.ap_count, s_wifi_ctx.aps));
  MG_INFO(("Total APs scanned = %d", s_wifi_ctx.ap_count));
  xSemaphoreGive(s_wifi_ctx.mutex);
}

static void wifi_save_config(wifi_config_t *cfg) {
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  memcpy(&s_wifi_ctx.cfg, cfg, sizeof(wifi_config_t));
  xSemaphoreGive(s_wifi_ctx.mutex);
}

static void wifi_commit_config() {
  wifi_config_t *cfg = &s_wifi_ctx.cfg;
  MG_INFO(("Committing WiFi config SSID=%s PASS=%s", cfg->sta.ssid, cfg->sta.password));
  xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, cfg));
  xSemaphoreGive(s_wifi_ctx.mutex);
}

static bool wifi_is_busy() {
  return s_wifi_ctx.state == WIFI_STATE_CONNECTING ||
         s_wifi_ctx.state == WIFI_STATE_SCANNING ||
         s_wifi_ctx.state == WIFI_STATE_REPROVISIONING;
}

static void event_user_handler(void *arg, int32_t event_id, void *event_data)
{
  if (event_id == WIFI_USER_EVENT_SCAN) {
    if (!wifi_is_busy()) {
      s_wifi_ctx.state = WIFI_STATE_SCANNING;
      MG_INFO(("User event: start scanning"));
      ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
    } else {
      MG_INFO(("WiFi is busy, cannot start scanning"));
    }
  } else if (event_id == WIFI_USER_EVENT_PROVISION) {
    MG_INFO(("User event: provisioning"));
    s_wifi_ctx.retry_count = 0;
    if (s_wifi_ctx.state == WIFI_STATE_SCANNING) {
      MG_INFO(("Stopping ongoing scan before provisioning"));
      esp_wifi_scan_stop();
    }
    if (wifi_get_provisioned()) {
      MG_INFO(("WiFi is already provisioned, disconnecting first"));
      wifi_set_provisioned(false);
      s_wifi_ctx.state = WIFI_STATE_REPROVISIONING;
      ESP_ERROR_CHECK(esp_wifi_disconnect());
    } else {
      wifi_commit_config();
      s_wifi_ctx.state = WIFI_STATE_CONNECTING;
      ESP_ERROR_CHECK(esp_wifi_connect());
    }
    free(event_data);
  }
}

static void event_wifi_handler(void *arg, int32_t event_id, void *event_data)
{
  if (event_id == WIFI_EVENT_STA_START) {
    if (wifi_get_provisioned()) {
      s_wifi_ctx.state = WIFI_STATE_CONNECTING;
      ESP_ERROR_CHECK(esp_wifi_connect());
    }
  } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    s_wifi_ctx.retry_count = 0;
    s_wifi_ctx.state = WIFI_STATE_IDLE;
  } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_wifi_ctx.state == WIFI_STATE_REPROVISIONING) {
      wifi_commit_config();
    }
    if (s_wifi_ctx.retry_count < WIFI_MAX_RETRY) {
      s_wifi_ctx.retry_count++;
      s_wifi_ctx.state = WIFI_STATE_CONNECTING;
      MG_INFO(("Retrying to connect to the AP (%d/%d)",
               s_wifi_ctx.retry_count, WIFI_MAX_RETRY));
      ESP_ERROR_CHECK(esp_wifi_connect());
    } else {
      MG_INFO(("Failed to connect ap"));
      s_wifi_ctx.state = WIFI_STATE_IDLE;
    }
  } else if (event_id == WIFI_EVENT_SCAN_DONE) {
    MG_INFO(("WiFi scan done"));
    wifi_save_scan_results();
    if (s_wifi_ctx.state == WIFI_STATE_SCANNING) {
      s_wifi_ctx.state = WIFI_STATE_IDLE;
    }
  }
}

static void ip_event_handler(void *arg, int32_t event_id, void *event_data)
{
  if (event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    MG_INFO(("Got IP ADDRESS: " IPSTR, IP2STR(&event->ip_info.ip)));
    if (!wifi_get_provisioned()) {
      wifi_set_provisioned(true);
      MG_INFO(("wifi provisioned successfully!!!"));
      //esp_restart();
    }
  }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if(event_base == WIFI_USER_EVENT) {
    event_user_handler(arg, event_id, event_data);
  } else if (event_base == WIFI_EVENT) {
    event_wifi_handler(arg, event_id, event_data);
  } else if (event_base == IP_EVENT) {
    ip_event_handler(arg, event_id, event_data);
  }
}

void wifi_init() {
  esp_err_t err = ESP_OK;
  if (!s_nvs_wifi_handle) {
    err = nvs_open(NVS_WIFI, NVS_READWRITE, &s_nvs_wifi_handle);
    if (err != ESP_OK) {
      MG_ERROR(("Error opening NVS wifi namespace"));
    }
  }
  s_wifi_ctx.mutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_USER_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
  /*Initialize WiFi */
  wifi_init_config_t initcfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&initcfg));
  s_wifi_ctx.state = WIFI_STATE_IDLE;
  if (wifi_get_provisioned()) {
    MG_INFO(("WiFi is provisioned, starting STA mode"));
    wifi_config_t cfg = {};
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    wifi_start_sta(&cfg);
  } else {
    MG_INFO(("WiFi is not provisioned, starting AP+STA mode"));
    wifi_start_provisioning();
  }
}

void wifi_uninit() {
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_deinit());
  if (s_ap_netif != NULL) {
    esp_netif_destroy_default_wifi(s_ap_netif);
    s_ap_netif = NULL;
  }
  if (s_sta_netif != NULL) {
    esp_netif_destroy_default_wifi(s_sta_netif);
    s_sta_netif = NULL;
  }
  if (s_nvs_wifi_handle) {
    nvs_close(s_nvs_wifi_handle);
    s_nvs_wifi_handle = 0;
  }
  if (s_wifi_ctx.mutex) {
    vSemaphoreDelete(s_wifi_ctx.mutex);
    s_wifi_ctx.mutex = NULL;
  }
}

void wifi_scan_start() {
  esp_event_post(WIFI_USER_EVENT, WIFI_USER_EVENT_SCAN, NULL, 0, portMAX_DELAY);
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
  wifi_save_config(&cfg);
  esp_event_post(WIFI_USER_EVENT, WIFI_USER_EVENT_PROVISION, NULL, 0, portMAX_DELAY);
}

bool wifi_is_provisioned(char* ssid)
{
  bool provisioned = wifi_get_provisioned();
  if (provisioned) {
    wifi_config_t cfg = {};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &cfg));
    strncpy(ssid, (char*)cfg.sta.ssid, MAX_SSID_LEN);
    return true;
  }
  return false;
}