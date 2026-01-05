#ifndef ESP32_WIFI_H
#define ESP32_WIFI_H

#include "mongoose.h"

#define MAX_SSID_LEN 32
#define MG_EV_WIFI                  (MG_EV_USER + 100)
#define MG_EV_WIFI_STA_START        (MG_EV_WIFI + 1)
#define MG_EV_WIFI_STA_CONNECTED    (MG_EV_WIFI + 2)
#define MG_EV_WIFI_STA_DISCONNECTED (MG_EV_WIFI + 3)
#define MG_EV_WIFI_STA_GOT_IP       (MG_EV_WIFI + 4)
#define MG_EV_WIFI_SCAN_DONE        (MG_EV_WIFI + 5)

void wifi_init();
void wifi_scan_start();
void wifi_scan_result(struct mg_str *out);
void wifi_connect(const char* ssid, const char* pass);
bool wifi_is_provisioned(char* ssid);

#endif