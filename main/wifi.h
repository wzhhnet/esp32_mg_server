#ifndef ESP32_WIFI_H
#define ESP32_WIFI_H

#include "mongoose.h"


void wifi_init_sta(const char* ssid, const char* pass);
void wifi_init_apsta(void* arg);
void wifi_scan_start();
void wifi_scan_result(struct mg_str *out);
bool wifi_is_scanning();
#endif