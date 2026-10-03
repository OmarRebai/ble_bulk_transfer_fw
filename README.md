# BBT: BLE Bulk Transfer

BBT is a small C protocol module for moving larger binary payloads over a BLE
GATT stream. It supports app-to-device uploads and device-to-app exports with
chunk sequencing, CRC checks, retry/repair signaling, and a small platform
abstraction layer.

## Runtime Model

Incoming BLE bytes are passed to `bbt_core_enqueue_packet()`. The core stream
parser assembles complete protocol packets and pushes them into an internal
worker queue. Packet handling then runs from a lower-priority worker context:

- `BBT_OS_CMSIS`: a CMSIS-RTOS2 worker thread is created.
- `BBT_OS_SEQUENCER`: a STM32 Utility Sequencer task is registered and scheduled
  when packets arrive.
- `BBT_OS_BAREMETAL`: no worker is created; the application calls
  `bbt_core_worker_service(false)` periodically.

## Configuration

The application must provide `bbt_config.h` in the include path and select
exactly one OS adapter.

```c
#ifndef BBT_CONFIG_H
#define BBT_CONFIG_H

/* Choose exactly one. */
#define BBT_OS_CMSIS
/* #define BBT_OS_SEQUENCER */
/* #define BBT_OS_BAREMETAL */

#define BBT_RX_QUEUE_DEPTH         16u
#define BBT_RX_THREAD_STACK_SIZE   2048u
#define BBT_RX_THREAD_PRIORITY     24

#define BBT_MAX_CHUNK_SIZE         244u
#define BBT_MAX_TOTAL_CHUNKS       4096u
#define BBT_MAX_NOTIFY_SIZE        260u

#define BBT_WORKER_POLL_MS         20u
#define BBT_IDLE_TIMEOUT_MS        5000u
#define BBT_NACK_RETRY_MAX         3u

#endif /* BBT_CONFIG_H */
```

### CMSIS-RTOS2 Port

Select:

```c
#define BBT_OS_CMSIS
```

Compile `Src/port/bbt_port_cmsis.c` and make sure `cmsis_os.h` is available.
The core creates a worker thread with `BBT_RX_THREAD_STACK_SIZE` and
`BBT_RX_THREAD_PRIORITY`.

### STM32 Utility Sequencer Port

Select:

```c
#define BBT_OS_SEQUENCER
#define BBT_SEQUENCER_TASK_ID_BIT    (1u << CFG_TASK_BBT_WORKER)
#define BBT_SEQUENCER_TASK_PRIORITY  CFG_SCH_PRIO_0
#define BBT_SEQUENCER_GET_TIME_MS()  HAL_GetTick()
#define BBT_SEQUENCER_DELAY_MS(ms)   HAL_Delay(ms)
```

Compile `Src/port/bbt_port_sequencer.c` and include STM32's `stm32_seq.h`.
The adapter registers the BBT worker with `UTIL_SEQ_RegTask()` and schedules it
with `UTIL_SEQ_SetTask()` whenever an RX packet is queued. The application must
continue running the sequencer scheduler, for example with `UTIL_SEQ_Run()`.

If BLE callbacks and the sequencer task can run concurrently, provide critical
section hooks:

```c
#define BBT_SEQUENCER_ENTER_CRITICAL()  __disable_irq()
#define BBT_SEQUENCER_EXIT_CRITICAL()   __enable_irq()
```

### Bare-Metal Port

Select:

```c
#define BBT_OS_BAREMETAL
#define BBT_BAREMETAL_GET_TIME_MS() systick_ms_get()
```

Compile `Src/port/bbt_port_baremetal.c` and call the worker service from the
main loop:

```c
void main_loop_tick(void)
{
    bbt_core_worker_service(false);
}
```

If the BLE write callback can interrupt the main loop, provide:

```c
#define BBT_BAREMETAL_ENTER_CRITICAL()  __disable_irq()
#define BBT_BAREMETAL_EXIT_CRITICAL()   __enable_irq()
```

## Application Hooks

The final application must implement the BLE stack send primitive declared in
`bbt_port.h`. The BBT module implements `bbt_port_ble_notify()` and
`bbt_port_ble_indicate()` internally and retries this hook when the BLE stack is
busy.

```c
int app_bbt_send(const uint8_t *data, uint16_t len, bool is_reliable)
{
    if (data == NULL || len == 0u) {
        return -1;
    }

    if (is_reliable) {
        return ble_gatts_indicate_custom_characteristic(data, len) == BLE_OK ? 0 : -1;
    }

    return ble_gatts_notify_custom_characteristic(data, len) == BLE_OK ? 0 : -1;
}

void bbt_port_log(int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
```

## Basic Integration

```c
#include "bbt_core.h"

static void bbt_event_cb(bbt_event_t event, const void *event_data)
{
    (void)event_data;

    switch (event)
    {
    case BBT_EVENT_STARTED:
        break;
    case BBT_EVENT_PROGRESS:
        break;
    case BBT_EVENT_COMPLETE:
        break;
    case BBT_EVENT_ERROR:
        break;
    default:
        break;
    }
}

void app_bbt_init(void)
{
    if (bbt_core_init() == BBT_OK)
    {
        bbt_core_register_event_callback(bbt_event_cb);
    }
}

void ble_write_callback(const uint8_t *data, uint16_t len)
{
    (void)bbt_core_enqueue_packet(data, len);
}
```

## Mode Table

Register mode handlers before starting transfers. Mode indexes are encoded in
`transfer_id & BBT_TRANSFER_ID_INDEX_MASK`; the direction bit selects transfer
direction.

```c
static const bbt_mode_entry_t ota_mode = {
    .app_to_device = &ota_upload_vtable,
    .device_to_app = NULL,
    .ctx = &ota_ctx,
};

void app_register_bbt_modes(void)
{
    (void)bbt_mode_register(BBT_MODE_INDEX_OTA, &ota_mode);
}
```

## Notes

- Do not block inside BLE write callbacks. Queue the bytes and return.
- Size `BBT_RX_QUEUE_DEPTH` for the worst BLE burst your storage or export path
  can tolerate.
- `BBT_MAX_NOTIFY_SIZE` must be large enough for the largest outgoing packet
  built by the sender path.
- Sequencer and bare-metal ports use static queue storage and support one BBT
  worker queue instance.
