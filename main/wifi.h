#ifndef ESP32_WIFI_H
#define ESP32_WIFI_H

#include "mongoose.h"

#define MAX_SSID_LEN 32

void wifi_init();
void wifi_scan_start();
void wifi_scan_result(struct mg_str *out);
void wifi_connect(const char* ssid, const char* pass);
bool wifi_is_provisioned(char* ssid);

#endif