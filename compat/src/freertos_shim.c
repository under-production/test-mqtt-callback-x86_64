/* FreeRTOS互換シムの実体実装 (pthreadベース, x86_64 Linux向け)。 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------- portMUX (critical section) ---------------- */
void portENTER_CRITICAL(portMUX_TYPE *mux)
{
    if (!mux->mutex) {
        pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(m, NULL);
        mux->mutex = m;
    }
    pthread_mutex_lock((pthread_mutex_t *)mux->mutex);
}

void portEXIT_CRITICAL(portMUX_TYPE *mux)
{
    if (mux->mutex) pthread_mutex_unlock((pthread_mutex_t *)mux->mutex);
}

/* ---------------- task ---------------- */
struct task_ctx {
    TaskFunction_t fn;
    void *arg;
};

static void *task_trampoline(void *raw)
{
    struct task_ctx *ctx = raw;
    TaskFunction_t fn = ctx->fn;
    void *arg = ctx->arg;
    free(ctx);
    fn(arg);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t task_function, const char *name,
                        uint32_t stack_depth, void *parameters,
                        UBaseType_t priority, TaskHandle_t *created_task)
{
    (void)name; (void)stack_depth; (void)priority;
    struct task_ctx *ctx = malloc(sizeof(*ctx));
    if (!ctx) return pdFAIL;
    ctx->fn = task_function;
    ctx->arg = parameters;
    pthread_t thread;
    if (pthread_create(&thread, NULL, task_trampoline, ctx) != 0) {
        free(ctx);
        return pdFAIL;
    }
    pthread_detach(thread);
    if (created_task) *created_task = (TaskHandle_t)(uintptr_t)thread;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks_ms)
{
    struct timespec ts = {.tv_sec = ticks_ms / 1000,
                          .tv_nsec = (long)(ticks_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* ---------------- queue ---------------- */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    unsigned char *storage;
    UBaseType_t item_size;
    UBaseType_t capacity;
    UBaseType_t count;
    UBaseType_t head;
} queue_t;

static void deadline_from_ticks(struct timespec *deadline, TickType_t ticks_ms)
{
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += ticks_ms / 1000;
    deadline->tv_nsec += (long)(ticks_ms % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_nsec -= 1000000000L;
        deadline->tv_sec += 1;
    }
}

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size)
{
    queue_t *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->storage = calloc(queue_length, item_size);
    if (!q->storage) { free(q); return NULL; }
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    q->item_size = item_size;
    q->capacity = queue_length;
    return (QueueHandle_t)q;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item,
                       TickType_t ticks_to_wait)
{
    queue_t *q = queue;
    if (!q) return pdFALSE;
    struct timespec deadline;
    bool has_deadline = ticks_to_wait > 0;
    if (has_deadline) deadline_from_ticks(&deadline, ticks_to_wait);
    pthread_mutex_lock(&q->lock);
    while (q->count == q->capacity) {
        if (!has_deadline) { pthread_mutex_unlock(&q->lock); return pdFALSE; }
        int rc = pthread_cond_timedwait(&q->not_full, &q->lock, &deadline);
        if (rc != 0) { pthread_mutex_unlock(&q->lock); return pdFALSE; }
    }
    UBaseType_t tail = (q->head + q->count) % q->capacity;
    memcpy(&q->storage[tail * q->item_size], item, q->item_size);
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *out_item,
                          TickType_t ticks_to_wait)
{
    queue_t *q = queue;
    if (!q) return pdFALSE;
    struct timespec deadline;
    bool has_deadline = ticks_to_wait > 0;
    if (has_deadline) deadline_from_ticks(&deadline, ticks_to_wait);
    pthread_mutex_lock(&q->lock);
    while (q->count == 0) {
        if (!has_deadline) { pthread_mutex_unlock(&q->lock); return pdFALSE; }
        int rc = pthread_cond_timedwait(&q->not_empty, &q->lock, &deadline);
        if (rc != 0) { pthread_mutex_unlock(&q->lock); return pdFALSE; }
    }
    memcpy(out_item, &q->storage[q->head * q->item_size], q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}

void xQueueReset(QueueHandle_t queue)
{
    queue_t *q = queue;
    if (!q) return;
    pthread_mutex_lock(&q->lock);
    q->count = 0;
    q->head = 0;
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

/* ---------------- event group ---------------- */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    EventBits_t bits;
} event_group_t;

EventGroupHandle_t xEventGroupCreate(void)
{
    event_group_t *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    pthread_mutex_init(&g->lock, NULL);
    pthread_cond_init(&g->cond, NULL);
    return (EventGroupHandle_t)g;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits)
{
    event_group_t *g = group;
    pthread_mutex_lock(&g->lock);
    g->bits |= bits;
    EventBits_t result = g->bits;
    pthread_cond_broadcast(&g->cond);
    pthread_mutex_unlock(&g->lock);
    return result;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits)
{
    event_group_t *g = group;
    pthread_mutex_lock(&g->lock);
    EventBits_t before = g->bits;
    g->bits &= ~bits;
    pthread_mutex_unlock(&g->lock);
    return before;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t group)
{
    event_group_t *g = group;
    pthread_mutex_lock(&g->lock);
    EventBits_t bits = g->bits;
    pthread_mutex_unlock(&g->lock);
    return bits;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t group,
                                 EventBits_t bits_to_wait_for,
                                 BaseType_t clear_on_exit,
                                 BaseType_t wait_for_all_bits,
                                 TickType_t ticks_to_wait)
{
    event_group_t *g = group;
    struct timespec deadline;
    bool has_deadline = ticks_to_wait > 0 && ticks_to_wait != (TickType_t)-1;
    if (has_deadline) deadline_from_ticks(&deadline, ticks_to_wait);
    pthread_mutex_lock(&g->lock);
    for (;;) {
        bool satisfied = wait_for_all_bits
                              ? (g->bits & bits_to_wait_for) == bits_to_wait_for
                              : (g->bits & bits_to_wait_for) != 0;
        if (satisfied) {
            EventBits_t result = g->bits;
            if (clear_on_exit) g->bits &= ~bits_to_wait_for;
            pthread_mutex_unlock(&g->lock);
            return result;
        }
        if (!has_deadline) {
            EventBits_t result = g->bits;
            pthread_mutex_unlock(&g->lock);
            return result;
        }
        int rc = pthread_cond_timedwait(&g->cond, &g->lock, &deadline);
        if (rc != 0) {
            EventBits_t result = g->bits;
            pthread_mutex_unlock(&g->lock);
            return result;
        }
    }
}

/* ---------------- binary semaphore ---------------- */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool available;
} binary_sem_t;

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    binary_sem_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->cond, NULL);
    return (SemaphoreHandle_t)s;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait)
{
    binary_sem_t *s = semaphore;
    if (!s) return pdFALSE;
    struct timespec deadline;
    bool has_deadline = ticks_to_wait > 0;
    if (has_deadline) deadline_from_ticks(&deadline, ticks_to_wait);
    pthread_mutex_lock(&s->lock);
    while (!s->available) {
        if (!has_deadline) { pthread_mutex_unlock(&s->lock); return pdFALSE; }
        int rc = pthread_cond_timedwait(&s->cond, &s->lock, &deadline);
        if (rc != 0) { pthread_mutex_unlock(&s->lock); return pdFALSE; }
    }
    s->available = false;
    pthread_mutex_unlock(&s->lock);
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    binary_sem_t *s = semaphore;
    if (!s) return pdFALSE;
    pthread_mutex_lock(&s->lock);
    s->available = true;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
    return pdTRUE;
}
