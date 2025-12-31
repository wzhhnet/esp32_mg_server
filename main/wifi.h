#ifndef ESP32_WIFI_H
#define ESP32_WIFI_H

#include "mongoose.h"


void wifi_init();
void wifi_scan_start();
void wifi_scan_result(struct mg_str *out);
void wifi_connect(const char* ssid, const char* pass);
#endif