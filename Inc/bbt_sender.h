/**
 * @file            bbt_sender.h
 * @brief           Sender/public-export API for the BBT module.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#ifndef BBT_SENDER_H
#define BBT_SENDER_H

#include "bbt_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the sender (export) subsystem.
 *
 * @return BBT_OK on success.
 */
bbt_result_t bbt_sender_init(void);

/**
 * @brief Deinitialize sender and reset export state.
 *
 * @return BBT_OK on success.
 */
bbt_result_t bbt_sender_deinit(void);

/**
 * @brief Abort any active sender export operation and notify the app.
 *
 * @return BBT_OK on success, or an error code.
 */
bbt_result_t bbt_sender_abort(void);

/**
 * @brief Retrieve a snapshot of sender status.
 *
 * @param status Out parameter populated with current sender status.
 * @return BBT_OK on success.
 */
bbt_result_t bbt_sender_get_status(bbt_status_t *status);

/**
 * @brief Periodic timeout checker for sender state machine.
 */
void bbt_sender_check_timeouts(void);

/**
 * @brief Query whether the sender is currently active.
 *
 * @return true if an export is active.
 */
bool bbt_sender_is_active(void);

/**
 * @brief Set a maximum chunk size the sender should use.
 *
 * @param max_chunk_size Upper bound for chunk payloads.
 */
void bbt_sender_set_max_chunk_size(uint16_t max_chunk_size);

/**
 * @brief Handle an incoming PULL_REQ packet (start export request).
 *
 * @param data Raw packet buffer.
 * @param len Packet length in bytes.
 */
void bbt_sender_handle_pull_req(const uint8_t *data, uint16_t len);

/**
 * @brief Handle START_ACK responses from the receiver.
 *
 * @param data Raw packet buffer.
 * @param len Packet length in bytes.
 */
void bbt_sender_handle_start_ack(const uint8_t *data, uint16_t len);

/**
 * @brief Handle incoming NACK packets (bitmap segments) from receiver.
 *
 * @param data Raw packet buffer.
 * @param len Packet length in bytes.
 */
void bbt_sender_handle_nack(const uint8_t *data, uint16_t len);

/**
 * @brief Handle COMPLETE responses from the receiver.
 *
 * @param data Raw packet buffer.
 * @param len Packet length in bytes.
 */
void bbt_sender_handle_complete_response(const uint8_t *data, uint16_t len);

/**
 * @brief Handle ERROR packets from the receiver.
 *
 * @param data Raw packet buffer.
 * @param len Packet length in bytes.
 */
void bbt_sender_handle_error_response(const uint8_t *data, uint16_t len);

/**
 * @brief Poll-driven sender worker to stream chunks when exporting.
 */
void bbt_sender_poll(void);

typedef struct
{
    bool                              active;
    bbt_state_t                       state;
    uint8_t                           transfer_id;
    const bbt_device_to_app_vtable_t *vt;
    void                             *vt_ctx;
    bbt_export_desc_t                 desc;
    uint16_t                          chunk_size;
    uint16_t                          total_chunks;
    uint16_t                          next_seq;
    uint32_t                          last_rx_time_ms;
    uint32_t                          last_notify_time_ms;
    uint8_t                           nack_retry_count;

    /* Accumulator for segmented bitmap NACKs */
    uint8_t                           nack_bitmap[BBT_BITMAP_SIZE_BYTES];
    uint16_t                          nack_bitmap_pos;
/**
* @struct bbt_sender_ctx_t
* @brief Sender runtime context (export path) storing state, counters and NACK accumulator.
*
* The nack_bitmap[] and nack_bitmap_pos fields are used to assemble segmented
* bitmap NACK payloads received from the receiver before processing.
*/
} bbt_sender_ctx_t;

#ifdef __cplusplus
}
#endif

#include "bbt_core.h"

#endif /* BBT_SENDER_H */
