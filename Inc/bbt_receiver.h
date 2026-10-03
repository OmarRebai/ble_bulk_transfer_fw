/**
 * @file            bbt_receiver.h
 * @brief           Receiver public API and context definition for the BBT module.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#ifndef BBT_RECEIVER_H
#define BBT_RECEIVER_H

#include "bbt_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t len;
    uint8_t  data[BBT_MAX_RX_PACKET_SIZE];
} bbt_rx_queue_item_t;

typedef struct
{
    bbt_state_t state;

    uint8_t  transfer_id;
    uint32_t total_size;
    uint16_t chunk_size;
    uint16_t total_chunks;

    uint8_t bitmap[BBT_BITMAP_SIZE_BYTES];

    uint32_t valid_chunks;
    uint32_t bytes_written;
    uint16_t high_watermark_next;

    uint32_t queue_drops;
    uint32_t crc_errors;
    uint32_t duplicate_chunks;
    uint32_t storage_errors;

    uint32_t last_rx_time_ms;
    uint32_t last_notify_time_ms;
    uint8_t  nack_retry_count;
/**
* @struct bbt_receiver_ctx_t
* @brief Receiver runtime context storing transfer metadata and received-chunk bitmap.
*
* The bitmap field (bitmap[]) is sized by BBT_BITMAP_SIZE_BYTES and tracks which
* chunk indices have been successfully received.
*/
} bbt_receiver_ctx_t;



/**
 * @brief Initialize receiver subsystem and reset internal context.
 *
 * @return BBT_OK on success.
 */
bbt_result_t bbt_receiver_init(void);

/**
 * @brief Deinitialize receiver and clear state.
 *
 * @return BBT_OK on success.
 */
bbt_result_t bbt_receiver_deinit(void);

/**
 * @brief Abort the current inbound transfer and notify the peer.
 *
 * @return BBT_OK on success, or an error code.
 */
bbt_result_t bbt_receiver_abort(void);

/**
 * @brief Retrieve a snapshot of the receiver status.
 *
 * @param status Out parameter to receive the current status.
 * @return BBT_OK on success, BBT_ERR_INVALID_ARG if status==NULL.
 */
bbt_result_t bbt_receiver_get_status(bbt_status_t *status);

/**
 * @brief Configure the maximum chunk size the receiver will accept.
 *
 * @param max_chunk_size Maximum chunk length in bytes.
 */
void bbt_receiver_set_max_chunk_size(uint16_t max_chunk_size);

/**
 * @brief Trigger timeout checks for the receiver state machine.
 *
 * Used by the core worker or application poll loop.
 */
void bbt_receiver_check_timeouts(void);

/**
 * @brief Process a raw received queue item (called by the worker).
 *
 * @param item Pointer to the queued RX item.
 */
void bbt_receiver_process_item(const bbt_rx_queue_item_t *item);

/**
 * @brief Poll helper to integrate the receiver into a bare-metal main loop.
 */
void bbt_receiver_poll(void);

/**
 * @brief Query whether the receiver is currently idle.
 *
 * @return true when no upload is active.
 */
bool bbt_receiver_is_upload_idle(void);

#ifdef __cplusplus
}
#endif

#include "bbt_core.h"

#endif /* BBT_RECEIVER_H */
