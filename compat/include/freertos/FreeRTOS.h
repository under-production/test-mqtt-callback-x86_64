/* FreeRTOS core compatibility shim for x86_64 Linux (pthreadベース実装)。
 * ESP-IDF/FreeRTOSのAPIをそのまま呼べるように、pthreadミューテックス/条件変数/
 * スレッドで意味的に等価な動作を提供する。main/*.c 側のロジックは無変更で使える。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

/* portMUX: 単純なpthreadミューテックスで代替するクリティカルセクション */
typedef struct {
    void *mutex; /* pthread_mutex_t* (opaque, freertos_shim.cで確保) */
} portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED { NULL }

void portENTER_CRITICAL(portMUX_TYPE *mux);
void portEXIT_CRITICAL(portMUX_TYPE *mux);
