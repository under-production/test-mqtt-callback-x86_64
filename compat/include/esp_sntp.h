/* ESP-IDF esp_sntp.h compatibility shim.
 * mqtt_task.c 側は esp_netif_sntp.h のAPIしか使わないため、
 * ここでは互換性のためだけに存在するプレースホルダ。
 */
#pragma once

#include "esp_netif_sntp.h"
