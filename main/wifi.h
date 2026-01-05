#ifndef ESP32_WIFI_H
#define ESP32_WIFI_H

#include "mongoose.h"

#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define MAX_IPV4_LEN 16

#define MG_EV_WIFI                  (MG_EV_USER + 100)
#define MG_EV_WIFI_STA_START        (MG_EV_WIFI + 1)
#define MG_EV_WIFI_STA_CONNECTED    (MG_EV_WIFI + 2)
#define MG_EV_WIFI_STA_DISCONNECTED (MG_EV_WIFI + 3)
#define MG_EV_WIFI_STA_GOT_IP       (MG_EV_WIFI + 4)
#define MG_EV_WIFI_SCAN_DONE        (MG_EV_WIFI + 5)

struct wifi_prov_cfg {
  char ssid[MAX_SSID_LEN];
  char pass[MAX_PASS_LEN];
};

struct wifi_prov_info {
  char ssid[MAX_SSID_LEN];
  char ipv4[MAX_IPV4_LEN];
};

void wifi_init();
void wifi_scan_start();
void wifi_scan_result(struct mg_str *out);
void wifi_provision(struct wifi_prov_cfg *cfg);
bool wifi_provisioned(struct wifi_prov_info *info);

#endif