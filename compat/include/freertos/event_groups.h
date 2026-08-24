#pragma once

#include "freertos/FreeRTOS.h"

typedef void *EventGroupHandle_t;
typedef uint32_t EventBits_t;

#define BIT0  (1U << 0)
#define BIT1  (1U << 1)
#define BIT2  (1U << 2)
#define BIT3  (1U << 3)
#define BIT4  (1U << 4)
#define BIT5  (1U << 5)
#define BIT6  (1U << 6)
#define BIT7  (1U << 7)

EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group,
                                 EventBits_t bits_to_wait_for,
                                 BaseType_t clear_on_exit,
                                 BaseType_t wait_for_all_bits,
                                 TickType_t ticks_to_wait);
