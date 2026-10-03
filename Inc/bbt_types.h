/**
 * @file            bbt_types.h
 * @brief           Core protocol types, packet structures and public enums for BBT.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#ifndef BBT_TYPES_H
#define BBT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * The application MUST provide this file.
 * It must be available in the include path before building the module.
 */
#include "bbt_config.h"

/* ============================================================================
 * Configuration validation
 * ==========================================================================*/

#if defined(BBT_OS_CMSIS) && defined(BBT_OS_BAREMETAL)
#error "Define only one BBT OS adapter"
#endif

#if defined(BBT_OS_CMSIS) && defined(BBT_OS_SEQUENCER)
#error "Define only one BBT OS adapter"
#endif

#if defined(BBT_OS_BAREMETAL) && defined(BBT_OS_SEQUENCER)
#error "Define only one BBT OS adapter"
#endif

#if !defined(BBT_OS_CMSIS) && !defined(BBT_OS_BAREMETAL) && !defined(BBT_OS_SEQUENCER)
#error "You must define one of BBT_OS_CMSIS, BBT_OS_BAREMETAL, or BBT_OS_SEQUENCER in bbt_config.h"
#endif

#ifndef BBT_MAX_CHUNK_SIZE
#error "BBT_MAX_CHUNK_SIZE must be defined in bbt_config.h"
#endif

#ifndef BBT_MAX_TOTAL_CHUNKS
#error "BBT_MAX_TOTAL_CHUNKS must be defined in bbt_config.h"
#endif

#ifndef BBT_RX_QUEUE_DEPTH
#error "BBT_RX_QUEUE_DEPTH must be defined in bbt_config.h"
#endif

#ifndef BBT_RX_THREAD_STACK_SIZE
#error "BBT_RX_THREAD_STACK_SIZE must be defined in bbt_config.h"
#endif

#ifndef BBT_RX_THREAD_PRIORITY
#error "BBT_RX_THREAD_PRIORITY must be defined in bbt_config.h"
#endif

#ifndef BBT_WORKER_POLL_MS
#error "BBT_WORKER_POLL_MS must be defined in bbt_config.h"
#endif

#ifndef BBT_IDLE_TIMEOUT_MS
#error "BBT_IDLE_TIMEOUT_MS must be defined in bbt_config.h"
#endif

#ifndef BBT_NACK_RETRY_MAX
#error "BBT_NACK_RETRY_MAX must be defined in bbt_config.h"
#endif

#ifndef BBT_MAX_NOTIFY_SIZE
#error "BBT_MAX_NOTIFY_SIZE must be defined in bbt_config.h"
#endif

#ifndef BBT_SENDER_CHUNK_DELAY_MS
#define BBT_SENDER_CHUNK_DELAY_MS 20u
#endif


#if BBT_MAX_TOTAL_CHUNKS > 65535u
#error "This implementation uses uint16_t sequence numbers. BBT_MAX_TOTAL_CHUNKS must be <= 65535"
#endif

#if BBT_MAX_NOTIFY_SIZE < 10u
#error "BBT_MAX_NOTIFY_SIZE must be at least 10 bytes"
#endif

#if BBT_MAX_CHUNK_SIZE == 0u
#error "BBT_MAX_CHUNK_SIZE must be > 0"
#endif

#if BBT_RX_QUEUE_DEPTH == 0u
#error "BBT_RX_QUEUE_DEPTH must be > 0"
#endif

/* ============================================================================
 * Packing helper (STM32 target)
 * ==========================================================================*/

#ifndef BBT_PACKED
#define BBT_PACKED __attribute__((packed))
#endif

/* ============================================================================
 * Packet constants
 * ==========================================================================*/

#define BBT_PROTOCOL_VERSION              1u
#define BBT_TRANSFER_ID_DIR_BIT           0x80u
#define BBT_TRANSFER_ID_INDEX_MASK        0x7Fu

/*
 * START:
 *   opcode                  1
 *   transfer_id             1
 *   total_size              4
 *   chunk_size              2
 *   total_chunks            2
 */
#define BBT_PACKET_START_FIXED_SIZE       10u

/*
 * CHUNK:
 *   opcode                  1
 *   transfer_id             1
 *   seq                     2
 *   payload_len             2
 *   payload_crc16           2
 *   payload                 N
 */
#define BBT_PACKET_CHUNK_HEADER_SIZE      8u

#define BBT_MAX_START_PACKET_SIZE         BBT_PACKET_START_FIXED_SIZE
#define BBT_MAX_CHUNK_PACKET_SIZE         (BBT_PACKET_CHUNK_HEADER_SIZE + BBT_MAX_CHUNK_SIZE)

#if BBT_MAX_START_PACKET_SIZE > BBT_MAX_CHUNK_PACKET_SIZE
#define BBT_MAX_RX_PACKET_SIZE            BBT_MAX_START_PACKET_SIZE
#else
#define BBT_MAX_RX_PACKET_SIZE            BBT_MAX_CHUNK_PACKET_SIZE
#endif

#define BBT_BITMAP_SIZE_BYTES             ((BBT_MAX_TOTAL_CHUNKS + 7u) / 8u)

/* ============================================================================
 * Opcodes
 * ==========================================================================*/

typedef enum
{
    BBT_OPCODE_START       = 0x01u,
    BBT_OPCODE_CHUNK       = 0x02u,
    BBT_OPCODE_END         = 0x03u,
    BBT_OPCODE_ABORT       = 0x04u,
    BBT_OPCODE_PULL_REQ    = 0x05u,

    BBT_OPCODE_START_ACK   = 0x81u,
    BBT_OPCODE_NACK        = 0x82u,
    BBT_OPCODE_COMPLETE    = 0x83u,
    BBT_OPCODE_ERROR       = 0x84u
} bbt_opcode_t;

/* ============================================================================
 * Results
 * ==========================================================================*/

typedef enum
{
    BBT_OK = 0,

    BBT_ERR_INVALID_ARG,
    BBT_ERR_INVALID_STATE,
    BBT_ERR_NO_MEMORY,
    BBT_ERR_QUEUE_FULL,
    BBT_ERR_PACKET_TOO_LARGE,
    BBT_ERR_PACKET_INVALID,
    BBT_ERR_STORAGE,
    BBT_ERR_BLE_NOTIFY,
    BBT_ERR_TIMEOUT,
    BBT_ERR_INTERNAL
} bbt_result_t;

/* ============================================================================
 * Protocol status/error codes sent over BLE
 * ==========================================================================*/

typedef enum
{
    BBT_PROTO_STATUS_OK = 0x00u,

    BBT_PROTO_ERR_UNKNOWN = 0x01u,
    BBT_PROTO_ERR_INVALID_PACKET = 0x02u,
    BBT_PROTO_ERR_INVALID_STATE = 0x03u,
    BBT_PROTO_ERR_UNSUPPORTED_VERSION = 0x04u,
    BBT_PROTO_ERR_CHUNK_TOO_LARGE = 0x05u,
    BBT_PROTO_ERR_TOO_MANY_CHUNKS = 0x06u,
    BBT_PROTO_ERR_SIZE_MISMATCH = 0x07u,
    BBT_PROTO_ERR_STORAGE = 0x08u,
    BBT_PROTO_ERR_FILE_CRC = 0x09u,
    BBT_PROTO_ERR_TIMEOUT = 0x0Au,
    BBT_PROTO_ERR_ABORTED = 0x0Bu
} bbt_proto_status_t;

/* ============================================================================
 * Receiver state
 * ==========================================================================*/

typedef enum
{
    BBT_STATE_IDLE = 0,
    BBT_STATE_WAITING_START_ACK,
    BBT_STATE_RECEIVING,
    BBT_STATE_WAITING_REPAIR,
    BBT_STATE_EXPORTING,
    BBT_STATE_WAITING_EXPORT_REPAIR,
    BBT_STATE_VERIFYING,
    BBT_STATE_COMPLETE,
    BBT_STATE_ERROR
} bbt_state_t;

/* ============================================================================
 * Transfer role
 * ==========================================================================*/

typedef enum
{
    BBT_ROLE_IDLE = 0,
    BBT_ROLE_RECEIVER,
    BBT_ROLE_SENDER
} bbt_role_t;

/* ============================================================================
 * Events
 * ==========================================================================*/

typedef enum
{
    BBT_EVENT_STARTED = 0,
    BBT_EVENT_PROGRESS,
    BBT_EVENT_NACK_SENT,
    BBT_EVENT_COMPLETE,
    BBT_EVENT_ABORTED,
    BBT_EVENT_ERROR
} bbt_event_t;

/* ============================================================================
 * Data structures
 * ==========================================================================*/

typedef struct
{
    uint8_t  transfer_id;
    uint32_t total_size;
    uint16_t chunk_size;
    uint16_t total_chunks;
} BBT_PACKED bbt_start_meta_t;

typedef struct
{
    uint8_t  transfer_id;
    uint16_t seq;
    uint16_t payload_len;
    uint16_t payload_crc16;
} BBT_PACKED bbt_chunk_header_t;

typedef struct
{
    bbt_chunk_header_t header;
    const uint8_t     *payload;
} bbt_chunk_packet_t;

typedef struct
{
    uint8_t  transfer_id;
    uint16_t max_chunk_size;
} BBT_PACKED bbt_pull_req_t;

typedef struct
{
    uint8_t  transfer_id;
    uint8_t  status;
    uint16_t accepted_chunk_size;
} BBT_PACKED bbt_start_ack_t;

typedef struct
{
    uint8_t  transfer_id;
    uint8_t  flags;
    uint16_t bitmap_len;
/**
* @struct bbt_nack_header_t
* @brief Header for NACK (bitmap) packets sent from receiver to sender.
*
* transfer_id: transfer identifier (direction bit may be set)
* flags: bit0 indicates that additional bitmap segments follow
* bitmap_len: number of bitmap bytes included in this packet
*/
} BBT_PACKED bbt_nack_header_t;

typedef struct
{
    uint8_t transfer_id;
} BBT_PACKED bbt_packet_id_t;

typedef struct
{
    uint8_t transfer_id;
    uint8_t error;
} BBT_PACKED bbt_error_packet_t;

typedef struct
{
    bbt_state_t state;

    uint8_t transfer_id;
    uint32_t total_size;
    uint16_t chunk_size;
    uint16_t total_chunks;

    uint32_t valid_chunks;
    uint32_t bytes_written;

    uint16_t high_watermark_next;

    uint32_t queue_drops;
    uint32_t crc_errors;
    uint32_t duplicate_chunks;
    uint32_t storage_errors;

    uint32_t last_rx_time_ms;
/**
* @struct bbt_status_t
* @brief Snapshot of current transfer status returned to application callers.
*/
} bbt_status_t;

typedef void (*bbt_event_cb_t)(bbt_event_t event, const void *event_data);

typedef bbt_result_t (*bbt_export_read_fn_t)(uint32_t offset,
                                             uint8_t *data,
                                             uint16_t len,
                                             uint16_t chunk_size,
                                             void *ctx);

typedef struct
{
    bbt_export_read_fn_t read_fn;
    uint32_t total_size;
    uint16_t chunk_size_hint;
    const char *label;
    void *ctx;
/**
* @struct bbt_export_desc_t
* @brief Descriptor returned by prepare_export() describing the export data source.
*
* Contains a platform-provided read callback and size/chunk hint for export.
*/
} bbt_export_desc_t;

/* ============================================================================
 * Mode table / application callback vtables
 * ==========================================================================*/

/* Number of mode table entries (indexed by transfer_id & 0x7F) */
#define BBT_MODE_TABLE_SIZE 128u

/* Default OTA mode index to reserve for firmware uploads */
#define BBT_MODE_INDEX_OTA 1u

/* Default log export mode index */
#define BBT_MODE_INDEX_LOG_EXPORT 2u

/* App->Device callback vtable (uploader incoming to device) */
typedef struct
{
    bbt_result_t (*on_start)(const bbt_start_meta_t *meta, void *ctx);
    bbt_result_t (*on_chunk)(const bbt_chunk_packet_t *chunk, void *ctx);
    bbt_result_t (*on_complete)(uint16_t transfer_id, void *ctx);
    bbt_result_t (*on_abort)(uint16_t transfer_id, void *ctx);
    bbt_result_t (*on_error)(uint16_t transfer_id, bbt_proto_status_t status, void *ctx);
} bbt_app_to_device_vtable_t;

/* Device->App callback vtable (export path) */
typedef struct
{
    bbt_result_t (*prepare_export)(const bbt_pull_req_t *req,
                                   bbt_export_desc_t *desc,
                                   void *ctx);
    bbt_result_t (*on_export_complete)(void *ctx);
    bbt_result_t (*on_error)(uint8_t transfer_id, bbt_proto_status_t status, void *ctx);
} bbt_device_to_app_vtable_t;

typedef struct
{
    const bbt_app_to_device_vtable_t *app_to_device;
    const bbt_device_to_app_vtable_t *device_to_app;
    void *ctx;
/**
* @struct bbt_mode_entry_t
* @brief Mode table entry connecting app/device vtables to a mode index.
*/
} bbt_mode_entry_t;

/* Register a mode table entry at index (0..BBT_MODE_TABLE_SIZE-1). */
bbt_result_t bbt_mode_register(uint8_t index, const bbt_mode_entry_t *entry);

#endif /* BBT_TYPES_H */
