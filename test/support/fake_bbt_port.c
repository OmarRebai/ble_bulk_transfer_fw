#include "bbt_port.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t item_size;
    uint32_t depth;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
    uint8_t *storage;
} fake_queue_t;

uint32_t fake_time_ms = 0u;
int fake_ble_notify_result = 0;
uint8_t fake_ble_notify_data[BBT_MAX_NOTIFY_SIZE];
uint16_t fake_ble_notify_len = 0u;
uint32_t fake_ble_notify_count = 0u;
int fake_ble_indicate_result = 0;
uint8_t fake_ble_indicate_data[BBT_MAX_NOTIFY_SIZE];
uint16_t fake_ble_indicate_len = 0u;
uint32_t fake_ble_indicate_count = 0u;
uint32_t fake_queue_create_fail_count = 0u;
uint32_t fake_mutex_create_fail_count = 0u;

void fake_bbt_port_reset(void)
{
    fake_time_ms = 0u;
    fake_ble_notify_result = 0;
    fake_ble_notify_len = 0u;
    fake_ble_notify_count = 0u;
    fake_ble_indicate_result = 0;
    fake_ble_indicate_len = 0u;
    fake_ble_indicate_count = 0u;
    fake_queue_create_fail_count = 0u;
    fake_mutex_create_fail_count = 0u;
    memset(fake_ble_notify_data, 0, sizeof(fake_ble_notify_data));
    memset(fake_ble_indicate_data, 0, sizeof(fake_ble_indicate_data));
}

bbt_os_thread_t bbt_os_thread_create(void (*entry)(void *),
                                     void *arg,
                                     const char *name,
                                     uint32_t stack_size,
                                     int priority)
{
    (void)entry;
    (void)arg;
    (void)name;
    (void)stack_size;
    (void)priority;

    return (bbt_os_thread_t)1;
}

void bbt_os_thread_delete(bbt_os_thread_t thread)
{
    (void)thread;
}

bbt_os_queue_t bbt_os_queue_create(uint32_t item_size, uint32_t depth)
{
    if (fake_queue_create_fail_count > 0u)
    {
        fake_queue_create_fail_count--;
        return NULL;
    }

    fake_queue_t *q = malloc(sizeof(fake_queue_t));

    if (q == NULL)
    {
        return NULL;
    }

    q->storage = calloc(depth, item_size);

    if (q->storage == NULL)
    {
        free(q);
        return NULL;
    }

    q->item_size = item_size;
    q->depth = depth;
    q->count = 0u;
    q->head = 0u;
    q->tail = 0u;

    return q;
}

void bbt_os_queue_delete(bbt_os_queue_t queue)
{
    fake_queue_t *q = (fake_queue_t *)queue;

    if (q != NULL)
    {
        free(q->storage);
        free(q);
    }
}

bool bbt_os_queue_send(bbt_os_queue_t queue,
                       const void *item,
                       uint32_t timeout_ms)
{
    (void)timeout_ms;

    fake_queue_t *q = (fake_queue_t *)queue;

    if ((q == NULL) || (item == NULL) || (q->count >= q->depth))
    {
        return false;
    }

    memcpy(&q->storage[q->tail * q->item_size], item, q->item_size);

    q->tail = (q->tail + 1u) % q->depth;
    q->count++;

    return true;
}

bool bbt_os_queue_receive(bbt_os_queue_t queue,
                          void *item,
                          uint32_t timeout_ms)
{
    (void)timeout_ms;

    fake_queue_t *q = (fake_queue_t *)queue;

    if ((q == NULL) || (item == NULL) || (q->count == 0u))
    {
        return false;
    }

    memcpy(item, &q->storage[q->head * q->item_size], q->item_size);

    q->head = (q->head + 1u) % q->depth;
    q->count--;

    return true;
}

bbt_os_mutex_t bbt_os_mutex_create(void)
{
    if (fake_mutex_create_fail_count > 0u)
    {
        fake_mutex_create_fail_count--;
        if (fake_mutex_create_fail_count == 0u)
        {
            return NULL;
        }
    }

    return (bbt_os_mutex_t)1;
}

void bbt_os_mutex_delete(bbt_os_mutex_t mutex)
{
    (void)mutex;
}

void bbt_os_mutex_lock(bbt_os_mutex_t mutex)
{
    (void)mutex;
}

void bbt_os_mutex_unlock(bbt_os_mutex_t mutex)
{
    (void)mutex;
}

uint32_t bbt_os_get_time_ms(void)
{
    return fake_time_ms;
}

void bbt_os_delay_ms(uint32_t delay_ms)
{
    fake_time_ms += delay_ms;
}

int bbt_port_ble_notify(const uint8_t *data, uint16_t len)
{
    fake_ble_notify_count++;
    fake_ble_notify_len = len;

    if ((data != NULL) && (len <= sizeof(fake_ble_notify_data)))
    {
        memcpy(fake_ble_notify_data, data, len);
    }

    return fake_ble_notify_result;
}

int bbt_port_ble_indicate(const uint8_t *data, uint16_t len)
{
    fake_ble_indicate_count++;
    fake_ble_indicate_len = len;

    if ((data != NULL) && (len <= sizeof(fake_ble_indicate_data)))
    {
        memcpy(fake_ble_indicate_data, data, len);
    }

    return fake_ble_indicate_result;
}

void bbt_port_log(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}
