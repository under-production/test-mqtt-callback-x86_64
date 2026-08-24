#pragma once

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t task_function, const char *name,
                        uint32_t stack_depth, void *parameters,
                        UBaseType_t priority, TaskHandle_t *created_task);
void vTaskDelay(TickType_t ticks_ms);
