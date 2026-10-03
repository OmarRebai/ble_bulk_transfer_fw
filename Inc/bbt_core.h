/**
 * @file            bbt_core.h
 * @brief           Core context and public APIs for the BLE Bulk Transfer (BBT) module.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#ifndef BBT_CORE_H
#define BBT_CORE_H

#include "bbt_port.h"
#include "bbt_receiver.h"
#include "bbt_sender.h"
#include "bbt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  bbt_role_t role;
  uint16_t   max_chunk_size;
  struct
  {
    bbt_os_queue_t  queue;
    bbt_os_thread_t thread;
    bbt_os_mutex_t  mutex;
    volatile bool   running;
  } worker;
  bbt_receiver_ctx_t receiver;
  bbt_sender_ctx_t   sender;
  /**
   * @struct bbt_core_ctx_t
   * @brief Global core context for the BBT module containing role, worker and subcontexts.
   */
} bbt_core_ctx_t;

/**
 * @brief Initialize the BBT core subsystem.
 *
 * Sets up the worker queue and initializes sender and receiver submodules.
 *
 * @return BBT_OK on success, otherwise a bbt_result_t error code.
 */
bbt_result_t bbt_core_init(void);
/**
 * @brief Deinitialize the BBT core and release resources.
 *
 * Stops worker activity and resets internal contexts.
 *
 * @return BBT_OK on success, otherwise a bbt_result_t error code.
 */
bbt_result_t bbt_core_deinit(void);
/**
 * @brief Enqueue incoming raw bytes for processing.
 *
 * This function accepts a byte stream that may contain partial packets or
 * multiple concatenated packets. The internal parser assembles full packets
 * before pushing them into the RX queue.
 *
 * @param data Pointer to the incoming bytes.
 * @param len Length of the byte buffer.
 * @return BBT_OK on success, or an error code (e.g. BBT_ERR_QUEUE_FULL).
 */
bbt_result_t bbt_core_enqueue_packet(const uint8_t *data, uint16_t len);

/**
 * @brief Reset the internal RX stream parser state.
 *
 * Call this if the transport detects a discontinuity (e.g. UART framing error)
 * so that partial packets are discarded.
 */
void bbt_core_reset_rx_parser(void);
/**
 * @brief Abort the active transfer (sender or receiver).
 *
 * Requests immediate termination of the current transfer and notifies the peer.
 *
 * @return BBT_OK on success, or a bbt_result_t error code.
 */
bbt_result_t bbt_core_abort(void);
/**
 * @brief Snapshot the current protocol status into the provided structure.
 *
 * @param status Pointer to a caller-provided bbt_status_t to populate.
 * @return BBT_OK on success, or an error code on invalid state/args.
 */
bbt_result_t bbt_core_get_status(bbt_status_t *status);
/**
 * @brief Register an asynchronous event callback.
 *
 * The callback will be invoked for high-level events such as START/PROGRESS/COMPLETE.
 *
 * @param cb Function pointer to receive events (may be NULL to unregister).
 */
void bbt_core_register_event_callback(bbt_event_cb_t cb);
/**
 * @brief Set an upper bound for negotiated chunk size.
 *
 * @param max_chunk_size Maximum chunk size accepted by the core.
 */
void bbt_core_set_max_chunk_size(uint16_t max_chunk_size);
/**
 * @brief Service the worker queue and perform periodic tasks.
 *
 * Must be called periodically by the application when running in bare-metal mode
 * or used internally when an OS worker thread is enabled.
 *
 * @param wait_for_item If true, may block waiting for queue items.
 */
void bbt_core_worker_service(bool wait_for_item);
/**
 * @brief Check whether it is currently permitted to start the sender role.
 *
 * @return true if the sender can be started, false otherwise.
 */
bool bbt_core_can_start_sender(void);

/**
 * @brief Select role based on current role and incoming opcode.
 *
 * @param current_role Current role to consider.
 * @param opcode First byte of the incoming packet used to infer role.
 * @return New role to use for processing the packet.
 */
bbt_role_t bbt_core_select_role(bbt_role_t current_role, uint8_t opcode);
/*
 * @brief Update the active role after processing a packet.
 *
 * This helper adjusts the core role according to sender activity and
 * receiver state transitions.
 *
 * @param role Pointer to the core role value to update.
 * @param receiver_state Current receiver state used to decide idleness.
 */
void bbt_core_update_role_after_processing(bbt_role_t *role, bbt_state_t receiver_state);
/**
 * @brief Drive timeout checks for the provided role.
 *
 * @param role Role to check timeouts for (BBT_ROLE_SENDER or BBT_ROLE_RECEIVER).
 */
void bbt_core_check_role_timeouts(bbt_role_t role);
/**
 * @brief Process a packet that belongs to the sender role.
 *
 * @param data Pointer to the packet buffer.
 * @param len Packet length in bytes.
 */
void bbt_core_process_sender_packet(const uint8_t *data, uint16_t len);
/**
 * @brief Obtain a pointer to the global core context.
 *
 * @return Pointer to the persistent bbt_core_ctx_t.
 */
bbt_core_ctx_t *bbt_core_get_context(void);
/**
 * @brief Return a pointer to a temporary notify buffer for building outgoing frames.
 *
 * The returned buffer has size BBT_MAX_NOTIFY_SIZE and is owned by the core.
 *
 * @return Pointer to the notify buffer.
 */
uint8_t *bbt_core_get_notify_buffer(void);
/**
 * @brief Emit an asynchronous event to the registered callback.
 *
 * @param event Event identifier.
 * @param event_data Optional event-specific data pointer (may be NULL).
 */
void bbt_core_emit_event(bbt_event_t event, const void *event_data);
/**
 * @brief Retrieve a registered mode table entry.
 *
 * @param index Mode table index (0..BBT_MODE_TABLE_SIZE-1).
 * @param entry Out parameter populated with the registered entry.
 * @return BBT_OK on success or an error code.
 */
bbt_result_t bbt_mode_get_entry(uint8_t index, bbt_mode_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* BBT_CORE_H */
