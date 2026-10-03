#include "unity.h"

#include "bbt_core.h"
#include "bbt_packet.h"

#include "mock_bbt_receiver.h"
#include "mock_bbt_sender.h"

#include <stdint.h>
#include <string.h>

static uint8_t packet_buf[512];

extern void fake_bbt_port_reset(void);
extern uint32_t fake_queue_create_fail_count;
extern uint32_t fake_mutex_create_fail_count;

void setUp(void)
{
    memset(packet_buf, 0, sizeof(packet_buf));
    fake_bbt_port_reset();

    bbt_receiver_deinit_IgnoreAndReturn(BBT_OK);
    bbt_sender_deinit_IgnoreAndReturn(BBT_OK);

    (void)bbt_core_deinit();
}

void tearDown(void)
{
    (void)bbt_core_deinit();
}

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static void expect_core_init_success(void)
{
    bbt_sender_init_ExpectAndReturn(BBT_OK);
    bbt_receiver_init_ExpectAndReturn(BBT_OK);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_init());
}

static uint16_t build_start_packet(uint8_t transfer_id)
{
    bbt_start_meta_t meta = {
        .transfer_id = transfer_id,
        .total_size = 1024u,
        .chunk_size = 128u,
        .total_chunks = 8u,
    };

    return bbt_packet_build_start(packet_buf, sizeof(packet_buf), &meta);
}

static uint16_t build_pull_req_packet(uint8_t transfer_id)
{
    bbt_pull_req_t req = {
        .transfer_id = transfer_id,
        .max_chunk_size = 128u,
    };

    return bbt_packet_build_pull_req(packet_buf, sizeof(packet_buf), &req);
}

static uint16_t build_chunk_packet(uint8_t transfer_id,
                                   uint16_t seq,
                                   uint16_t payload_len)
{
    uint8_t payload[BBT_MAX_CHUNK_SIZE];

    for (uint16_t i = 0u; i < payload_len; ++i)
    {
        payload[i] = (uint8_t)(seq + i);
    }

    bbt_chunk_header_t header = {
        .transfer_id = transfer_id,
        .seq = seq,
        .payload_len = payload_len,
        .payload_crc16 = 0u,
    };

    return bbt_packet_build_chunk(packet_buf,
                                  sizeof(packet_buf),
                                  &header,
                                  payload);
}

static uint16_t build_nack_packet(uint8_t transfer_id,
                                  uint8_t flags,
                                  const uint8_t *bitmap,
                                  uint16_t bitmap_len)
{
    bbt_nack_header_t header = {
        .transfer_id = transfer_id,
        .flags = flags,
        .bitmap_len = bitmap_len,
    };

    return bbt_packet_build_nack(packet_buf,
                                 sizeof(packet_buf),
                                 &header,
                                 bitmap);
}

/* -------------------------------------------------------------------------- */
/* Init / Deinit                                                              */
/* -------------------------------------------------------------------------- */

void test_core_init_should_initialize_context(void)
{
    bbt_sender_init_ExpectAndReturn(BBT_OK);
    bbt_receiver_init_ExpectAndReturn(BBT_OK);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_init());

    bbt_core_ctx_t *ctx = bbt_core_get_context();

    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(BBT_ROLE_IDLE, ctx->role);
    TEST_ASSERT_EQUAL_UINT16(BBT_MAX_CHUNK_SIZE, ctx->max_chunk_size);
    TEST_ASSERT_TRUE(ctx->worker.running);
    TEST_ASSERT_NOT_NULL(ctx->worker.queue);
    TEST_ASSERT_NOT_NULL(ctx->worker.mutex);
}

void test_core_init_should_be_idempotent(void)
{
    expect_core_init_success();

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_init());
}

void test_core_init_should_fail_when_sender_init_fails(void)
{
    bbt_sender_init_ExpectAndReturn(BBT_ERR_INTERNAL);

    TEST_ASSERT_EQUAL(BBT_ERR_INTERNAL, bbt_core_init());
}

void test_core_init_should_fail_when_receiver_init_fails(void)
{
    bbt_sender_init_ExpectAndReturn(BBT_OK);
    bbt_receiver_init_ExpectAndReturn(BBT_ERR_INTERNAL);

    TEST_ASSERT_EQUAL(BBT_ERR_INTERNAL, bbt_core_init());
}

void test_core_init_should_fail_when_queue_create_fails(void)
{
    fake_queue_create_fail_count = 1u;

    bbt_sender_init_ExpectAndReturn(BBT_OK);
    bbt_receiver_init_ExpectAndReturn(BBT_OK);

    TEST_ASSERT_EQUAL(BBT_ERR_NO_MEMORY, bbt_core_init());
}

void test_core_init_should_fail_when_worker_mutex_create_fails(void)
{
    fake_mutex_create_fail_count = 2u;

    bbt_sender_init_ExpectAndReturn(BBT_OK);
    bbt_receiver_init_ExpectAndReturn(BBT_OK);

    TEST_ASSERT_EQUAL(BBT_ERR_NO_MEMORY, bbt_core_init());
}

void test_core_deinit_should_cleanup_context(void)
{
    expect_core_init_success();

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_deinit());

    bbt_core_ctx_t *ctx = bbt_core_get_context();

    TEST_ASSERT_EQUAL(BBT_ROLE_IDLE, ctx->role);
    TEST_ASSERT_FALSE(ctx->worker.running);
    TEST_ASSERT_NULL(ctx->worker.queue);
    TEST_ASSERT_NULL(ctx->worker.mutex);
}

/* -------------------------------------------------------------------------- */
/* Enqueue validation                                                         */
/* -------------------------------------------------------------------------- */

void test_enqueue_should_reject_null_data(void)
{
    expect_core_init_success();

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG,
                      bbt_core_enqueue_packet(NULL, 1u));
}

void test_enqueue_should_reject_zero_length(void)
{
    expect_core_init_success();

    uint8_t byte = BBT_OPCODE_START;

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG,
                      bbt_core_enqueue_packet(&byte, 0u));
}

void test_enqueue_should_reject_when_not_initialized(void)
{
    uint8_t byte = BBT_OPCODE_START;

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_STATE,
                      bbt_core_enqueue_packet(&byte, 1u));
}

void test_enqueue_should_reject_unknown_opcode(void)
{
    expect_core_init_success();

    uint8_t invalid_opcode = 0xFFu;

    TEST_ASSERT_EQUAL(BBT_ERR_PACKET_INVALID,
                      bbt_core_enqueue_packet(&invalid_opcode, 1u));
}

/* -------------------------------------------------------------------------- */
/* RX parser                                                                  */
/* -------------------------------------------------------------------------- */

void test_enqueue_should_accept_complete_start_packet(void)
{
    expect_core_init_success();

    uint16_t len = build_start_packet(0x01u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_accept_start_packet_split_in_two_parts(void)
{
    expect_core_init_success();

    uint16_t len = build_start_packet(0x01u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, 3u));

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(&packet_buf[3], len - 3u));
}

void test_enqueue_should_accept_multiple_packets_in_one_buffer(void)
{
    expect_core_init_success();

    uint8_t combined[64];

    uint16_t start_len = build_start_packet(0x01u);
    memcpy(combined, packet_buf, start_len);

    bbt_packet_id_t end_id = {
        .transfer_id = 0x01u,
    };

    uint16_t end_len = bbt_packet_build_end(&combined[start_len],
                                            sizeof(combined) - start_len,
                                            &end_id);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(combined, start_len + end_len));
}

void test_reset_rx_parser_should_discard_partial_packet(void)
{
    expect_core_init_success();

    uint16_t len = build_start_packet(0x01u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, 3u));

    bbt_core_reset_rx_parser();

    TEST_ASSERT_EQUAL(BBT_ERR_PACKET_INVALID,
                      bbt_core_enqueue_packet(&packet_buf[3], len - 3u));
}

void test_enqueue_should_reject_chunk_with_payload_bigger_than_max(void)
{
    expect_core_init_success();

    bbt_chunk_header_t header = {
        .transfer_id = 0x01u,
        .seq = 0u,
        .payload_len = BBT_MAX_CHUNK_SIZE + 1u,
        .payload_crc16 = 0u,
    };

    packet_buf[0] = BBT_OPCODE_CHUNK;
    memcpy(&packet_buf[1], &header, sizeof(header));

    TEST_ASSERT_EQUAL(BBT_ERR_PACKET_TOO_LARGE,
                      bbt_core_enqueue_packet(packet_buf,
                                              BBT_PACKET_CHUNK_HEADER_SIZE));
}

void test_enqueue_should_accept_chunk_with_payload(void)
{
    expect_core_init_success();

    uint16_t len = build_chunk_packet(0x01u, 0u, 4u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_accept_zero_length_chunk(void)
{
    expect_core_init_success();

    uint16_t len = build_chunk_packet(0x01u, 0u, 0u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_accept_start_ack_complete_error_abort_and_nack_packets(void)
{
    expect_core_init_success();

    bbt_start_ack_t ack = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
        .status = BBT_PROTO_STATUS_OK,
        .accepted_chunk_size = 128u,
    };
    uint16_t len = bbt_packet_build_start_ack(packet_buf, sizeof(packet_buf), &ack);
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, len));

    bbt_packet_id_t complete = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
    };
    len = bbt_packet_build_complete(packet_buf, sizeof(packet_buf), &complete);
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, len));

    bbt_error_packet_t error = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
        .error = BBT_PROTO_ERR_TIMEOUT,
    };
    len = bbt_packet_build_error(packet_buf, sizeof(packet_buf), &error);
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, len));

    bbt_packet_id_t abort_id = {
        .transfer_id = 0x02u,
    };
    packet_buf[0] = BBT_OPCODE_ABORT;
    memcpy(&packet_buf[1], &abort_id, sizeof(abort_id));
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, 2u));

    const uint8_t bitmap[] = {0x01u, 0x02u};
    len = build_nack_packet(BBT_TRANSFER_ID_DIR_BIT | 0x02u,
                            0u,
                            bitmap,
                            sizeof(bitmap));
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_accept_nack_with_zero_bitmap(void)
{
    expect_core_init_success();

    const uint8_t bitmap[] = {0u};
    uint16_t len = build_nack_packet(BBT_TRANSFER_ID_DIR_BIT | 0x02u,
                                     0u,
                                     bitmap,
                                     0u);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_reject_nack_with_bitmap_bigger_than_max(void)
{
    expect_core_init_success();

    bbt_nack_header_t header = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
        .flags = 0u,
        .bitmap_len = BBT_BITMAP_SIZE_BYTES + 1u,
    };

    packet_buf[0] = BBT_OPCODE_NACK;
    memcpy(&packet_buf[1], &header, sizeof(header));

    TEST_ASSERT_EQUAL(BBT_ERR_PACKET_TOO_LARGE,
                      bbt_core_enqueue_packet(packet_buf,
                                              (uint16_t)(1u + sizeof(header))));
}

void test_enqueue_should_return_queue_full_and_increment_drop_counter(void)
{
    expect_core_init_success();

    uint16_t len = build_start_packet(0x01u);

    for (uint8_t i = 0u; i < BBT_RX_QUEUE_DEPTH; ++i)
    {
        TEST_ASSERT_EQUAL(BBT_OK,
                          bbt_core_enqueue_packet(packet_buf, len));
    }

    TEST_ASSERT_EQUAL(BBT_ERR_QUEUE_FULL,
                      bbt_core_enqueue_packet(packet_buf, len));

    TEST_ASSERT_EQUAL_UINT32(1u,
                             bbt_core_get_context()->receiver.queue_drops);
}

void test_enqueue_should_return_queue_full_for_zero_length_chunk_finish(void)
{
    expect_core_init_success();

    uint16_t fill_len = build_start_packet(0x01u);
    for (uint8_t i = 0u; i < BBT_RX_QUEUE_DEPTH; ++i)
    {
        TEST_ASSERT_EQUAL(BBT_OK,
                          bbt_core_enqueue_packet(packet_buf, fill_len));
    }

    uint16_t len = build_chunk_packet(0x01u, 0u, 0u);

    TEST_ASSERT_EQUAL(BBT_ERR_QUEUE_FULL,
                      bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_return_queue_full_for_start_ack_tail_finish(void)
{
    expect_core_init_success();

    uint16_t fill_len = build_start_packet(0x01u);
    for (uint8_t i = 0u; i < BBT_RX_QUEUE_DEPTH; ++i)
    {
        TEST_ASSERT_EQUAL(BBT_OK,
                          bbt_core_enqueue_packet(packet_buf, fill_len));
    }

    bbt_start_ack_t ack = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
        .status = BBT_PROTO_STATUS_OK,
        .accepted_chunk_size = 128u,
    };
    uint16_t len = bbt_packet_build_start_ack(packet_buf, sizeof(packet_buf), &ack);

    TEST_ASSERT_EQUAL(BBT_ERR_QUEUE_FULL,
                      bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_return_queue_full_for_nack_zero_bitmap_finish(void)
{
    expect_core_init_success();

    uint16_t fill_len = build_start_packet(0x01u);
    for (uint8_t i = 0u; i < BBT_RX_QUEUE_DEPTH; ++i)
    {
        TEST_ASSERT_EQUAL(BBT_OK,
                          bbt_core_enqueue_packet(packet_buf, fill_len));
    }

    const uint8_t bitmap[] = {0u};
    uint16_t len = build_nack_packet(BBT_TRANSFER_ID_DIR_BIT | 0x02u,
                                     0u,
                                     bitmap,
                                     0u);

    TEST_ASSERT_EQUAL(BBT_ERR_QUEUE_FULL,
                      bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_return_queue_full_for_nack_bitmap_finish(void)
{
    expect_core_init_success();

    uint16_t fill_len = build_start_packet(0x01u);
    for (uint8_t i = 0u; i < BBT_RX_QUEUE_DEPTH; ++i)
    {
        TEST_ASSERT_EQUAL(BBT_OK,
                          bbt_core_enqueue_packet(packet_buf, fill_len));
    }

    const uint8_t bitmap[] = {0xAAu};
    uint16_t len = build_nack_packet(BBT_TRANSFER_ID_DIR_BIT | 0x02u,
                                     0u,
                                     bitmap,
                                     sizeof(bitmap));

    TEST_ASSERT_EQUAL(BBT_ERR_QUEUE_FULL,
                      bbt_core_enqueue_packet(packet_buf, len));
}

void test_enqueue_should_return_invalid_state_if_queue_removed_mid_packet(void)
{
    expect_core_init_success();

    uint16_t len = build_start_packet(0x01u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, 3u));

    bbt_core_get_context()->worker.queue = NULL;

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_STATE,
                      bbt_core_enqueue_packet(&packet_buf[3], len - 3u));
}

/* -------------------------------------------------------------------------- */
/* Worker dispatch                                                            */
/* -------------------------------------------------------------------------- */

void test_worker_should_dispatch_start_packet_to_receiver(void)
{
    expect_core_init_success();

    uint16_t len = build_start_packet(0x01u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, len));

    bbt_receiver_process_item_Ignore();

    bbt_core_worker_service(false);
}

void test_worker_should_dispatch_pull_req_packet_to_sender(void)
{
    expect_core_init_success();

    uint16_t len = build_pull_req_packet(BBT_TRANSFER_ID_DIR_BIT | 0x02u);

    TEST_ASSERT_EQUAL(BBT_OK,
                      bbt_core_enqueue_packet(packet_buf, len));

    bbt_sender_handle_pull_req_Expect(packet_buf, len);

    bbt_sender_is_active_ExpectAndReturn(false);

    bbt_core_worker_service(false);
}

void test_worker_should_dispatch_sender_direction_packet_to_sender(void)
{
    expect_core_init_success();

    bbt_packet_id_t id = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
    };
    uint16_t len = bbt_packet_build_complete(packet_buf,
                                             sizeof(packet_buf),
                                             &id);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, len));

    bbt_sender_handle_complete_response_Expect(packet_buf, len);
    bbt_sender_is_active_ExpectAndReturn(false);

    bbt_core_worker_service(false);
}

void test_worker_should_dispatch_receiver_direction_packet_to_receiver(void)
{
    expect_core_init_success();

    bbt_packet_id_t id = {
        .transfer_id = 0x02u,
    };
    uint16_t len = bbt_packet_build_complete(packet_buf,
                                             sizeof(packet_buf),
                                             &id);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_enqueue_packet(packet_buf, len));

    bbt_receiver_process_item_Ignore();

    bbt_core_worker_service(false);
}

void test_worker_should_poll_sender_when_sender_role_is_active(void)
{
    expect_core_init_success();

    bbt_core_get_context()->role = BBT_ROLE_SENDER;

    bbt_sender_check_timeouts_Expect();
    bbt_sender_poll_Expect();

    bbt_core_worker_service(true);
}

void test_worker_should_do_nothing_when_not_initialized(void)
{
    bbt_core_worker_service(false);
}

/* -------------------------------------------------------------------------- */
/* Role helpers                                                               */
/* -------------------------------------------------------------------------- */

void test_select_role_should_keep_existing_receiver_role(void)
{
    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER,
                      bbt_core_select_role(BBT_ROLE_RECEIVER,
                                           BBT_OPCODE_PULL_REQ));
}

void test_select_role_should_keep_existing_sender_role(void)
{
    TEST_ASSERT_EQUAL(BBT_ROLE_SENDER,
                      bbt_core_select_role(BBT_ROLE_SENDER,
                                           BBT_OPCODE_START));
}

void test_select_role_should_select_sender_for_sender_opcodes(void)
{
    TEST_ASSERT_EQUAL(BBT_ROLE_SENDER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_PULL_REQ));

    TEST_ASSERT_EQUAL(BBT_ROLE_SENDER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_START_ACK));

    TEST_ASSERT_EQUAL(BBT_ROLE_SENDER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_NACK));

    TEST_ASSERT_EQUAL(BBT_ROLE_SENDER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_COMPLETE));

    TEST_ASSERT_EQUAL(BBT_ROLE_SENDER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_ERROR));
}

void test_select_role_should_select_receiver_for_receiver_opcodes(void)
{
    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_START));

    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_CHUNK));

    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_END));

    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER,
                      bbt_core_select_role(BBT_ROLE_IDLE,
                                           BBT_OPCODE_ABORT));
}

void test_update_role_should_set_sender_to_idle_when_sender_inactive(void)
{
    bbt_role_t role = BBT_ROLE_SENDER;

    bbt_sender_is_active_ExpectAndReturn(false);

    bbt_core_update_role_after_processing(&role, BBT_STATE_IDLE);

    TEST_ASSERT_EQUAL(BBT_ROLE_IDLE, role);
}

void test_update_role_should_keep_sender_when_sender_active(void)
{
    bbt_role_t role = BBT_ROLE_SENDER;

    bbt_sender_is_active_ExpectAndReturn(true);

    bbt_core_update_role_after_processing(&role, BBT_STATE_IDLE);

    TEST_ASSERT_EQUAL(BBT_ROLE_SENDER, role);
}

void test_update_role_should_keep_receiver_when_receiving(void)
{
    bbt_role_t role = BBT_ROLE_RECEIVER;

    bbt_core_update_role_after_processing(&role, BBT_STATE_RECEIVING);

    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER, role);
}

void test_update_role_should_keep_receiver_when_waiting_repair(void)
{
    bbt_role_t role = BBT_ROLE_RECEIVER;

    bbt_core_update_role_after_processing(&role, BBT_STATE_WAITING_REPAIR);

    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER, role);
}

void test_update_role_should_keep_receiver_when_verifying(void)
{
    bbt_role_t role = BBT_ROLE_RECEIVER;

    bbt_core_update_role_after_processing(&role, BBT_STATE_VERIFYING);

    TEST_ASSERT_EQUAL(BBT_ROLE_RECEIVER, role);
}

void test_update_role_should_set_receiver_to_idle_when_complete(void)
{
    bbt_role_t role = BBT_ROLE_RECEIVER;

    bbt_core_update_role_after_processing(&role, BBT_STATE_COMPLETE);

    TEST_ASSERT_EQUAL(BBT_ROLE_IDLE, role);
}

void test_update_role_should_ignore_null_pointer(void)
{
    bbt_core_update_role_after_processing(NULL, BBT_STATE_IDLE);
}

/* -------------------------------------------------------------------------- */
/* Timeout dispatch                                                           */
/* -------------------------------------------------------------------------- */

void test_check_role_timeouts_should_call_sender_timeout_for_sender_role(void)
{
    bbt_sender_check_timeouts_Expect();

    bbt_core_check_role_timeouts(BBT_ROLE_SENDER);
}

void test_check_role_timeouts_should_call_receiver_timeout_for_receiver_role(void)
{
    bbt_receiver_check_timeouts_Expect();

    bbt_core_check_role_timeouts(BBT_ROLE_RECEIVER);
}

void test_check_role_timeouts_should_do_nothing_for_idle_role(void)
{
    bbt_core_check_role_timeouts(BBT_ROLE_IDLE);
}

/* -------------------------------------------------------------------------- */
/* Sender packet dispatch                                                     */
/* -------------------------------------------------------------------------- */

void test_process_sender_packet_should_dispatch_pull_req(void)
{
    uint16_t len = build_pull_req_packet(BBT_TRANSFER_ID_DIR_BIT | 0x02u);

    bbt_sender_handle_pull_req_Expect(packet_buf, len);

    bbt_core_process_sender_packet(packet_buf, len);
}

void test_process_sender_packet_should_dispatch_start_ack(void)
{
    bbt_start_ack_t ack = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
        .status = BBT_PROTO_STATUS_OK,
        .accepted_chunk_size = 128u,
    };

    uint16_t len = bbt_packet_build_start_ack(packet_buf,
                                              sizeof(packet_buf),
                                              &ack);

    bbt_sender_handle_start_ack_Expect(packet_buf, len);

    bbt_core_process_sender_packet(packet_buf, len);
}

void test_process_sender_packet_should_dispatch_nack(void)
{
    const uint8_t bitmap[] = {0x01u};

    bbt_nack_header_t header = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
        .flags = 0u,
        .bitmap_len = sizeof(bitmap),
    };

    uint16_t len = bbt_packet_build_nack(packet_buf,
                                         sizeof(packet_buf),
                                         &header,
                                         bitmap);

    bbt_sender_handle_nack_Expect(packet_buf, len);

    bbt_core_process_sender_packet(packet_buf, len);
}

void test_process_sender_packet_should_dispatch_complete(void)
{
    bbt_packet_id_t id = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
    };

    uint16_t len = bbt_packet_build_complete(packet_buf,
                                             sizeof(packet_buf),
                                             &id);

    bbt_sender_handle_complete_response_Expect(packet_buf, len);

    bbt_core_process_sender_packet(packet_buf, len);
}

void test_process_sender_packet_should_dispatch_error(void)
{
    bbt_error_packet_t error = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 0x02u,
        .error = BBT_PROTO_ERR_INVALID_PACKET,
    };

    uint16_t len = bbt_packet_build_error(packet_buf,
                                          sizeof(packet_buf),
                                          &error);

    bbt_sender_handle_error_response_Expect(packet_buf, len);

    bbt_core_process_sender_packet(packet_buf, len);
}

void test_process_sender_packet_should_ignore_null_data(void)
{
    bbt_core_process_sender_packet(NULL, 10u);
}

void test_process_sender_packet_should_ignore_zero_len(void)
{
    bbt_core_process_sender_packet(packet_buf, 0u);
}

void test_process_sender_packet_should_log_receiver_and_unknown_opcodes(void)
{
    packet_buf[0] = BBT_OPCODE_START;
    bbt_core_process_sender_packet(packet_buf, 1u);

    packet_buf[0] = 0xFEu;
    bbt_core_process_sender_packet(packet_buf, 1u);
}

/* -------------------------------------------------------------------------- */
/* Status / configuration                                                     */
/* -------------------------------------------------------------------------- */

void test_can_start_sender_should_return_false_when_not_initialized(void)
{
    TEST_ASSERT_FALSE(bbt_core_can_start_sender());
}

void test_can_start_sender_should_return_true_when_idle(void)
{
    expect_core_init_success();

    TEST_ASSERT_TRUE(bbt_core_can_start_sender());
}

void test_can_start_sender_should_return_false_when_receiver_active(void)
{
    expect_core_init_success();

    bbt_core_ctx_t *ctx = bbt_core_get_context();

    ctx->role = BBT_ROLE_RECEIVER;
    ctx->receiver.state = BBT_STATE_RECEIVING;

    TEST_ASSERT_FALSE(bbt_core_can_start_sender());
}

void test_set_max_chunk_size_should_clamp_to_config_max(void)
{
    expect_core_init_success();

    bbt_receiver_set_max_chunk_size_Expect(BBT_MAX_CHUNK_SIZE);
    bbt_sender_set_max_chunk_size_Expect(BBT_MAX_CHUNK_SIZE);

    bbt_core_set_max_chunk_size(BBT_MAX_CHUNK_SIZE + 100u);

    TEST_ASSERT_EQUAL_UINT16(BBT_MAX_CHUNK_SIZE,
                             bbt_core_get_context()->max_chunk_size);
}

void test_set_max_chunk_size_should_accept_smaller_value(void)
{
    expect_core_init_success();

    bbt_receiver_set_max_chunk_size_Expect(64u);
    bbt_sender_set_max_chunk_size_Expect(64u);

    bbt_core_set_max_chunk_size(64u);

    TEST_ASSERT_EQUAL_UINT16(64u,
                             bbt_core_get_context()->max_chunk_size);
}

void test_set_max_chunk_size_should_do_nothing_when_not_initialized(void)
{
    bbt_core_set_max_chunk_size(64u);

    TEST_ASSERT_EQUAL_UINT16(0u,
                             bbt_core_get_context()->max_chunk_size);
}

void test_abort_should_reject_uninitialized_and_dispatch_by_role(void)
{
    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_STATE, bbt_core_abort());

    expect_core_init_success();
    bbt_core_get_context()->role = BBT_ROLE_SENDER;

    bbt_sender_abort_ExpectAndReturn(BBT_OK);
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_abort());

    bbt_core_get_context()->role = BBT_ROLE_RECEIVER;

    bbt_receiver_abort_ExpectAndReturn(BBT_OK);
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_abort());
}

void test_abort_should_dispatch_to_sender_when_sender_context_active(void)
{
    expect_core_init_success();

    bbt_core_get_context()->role = BBT_ROLE_IDLE;
    bbt_core_get_context()->sender.active = true;

    bbt_sender_abort_ExpectAndReturn(BBT_OK);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_abort());
}

void test_get_status_should_validate_state_and_dispatch_by_role(void)
{
    bbt_status_t status;

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_STATE,
                      bbt_core_get_status(&status));

    expect_core_init_success();

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG,
                      bbt_core_get_status(NULL));

    bbt_core_get_context()->role = BBT_ROLE_SENDER;
    bbt_sender_get_status_ExpectAndReturn(&status, BBT_OK);
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_get_status(&status));

    bbt_core_get_context()->role = BBT_ROLE_RECEIVER;
    bbt_receiver_get_status_ExpectAndReturn(&status, BBT_OK);
    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_get_status(&status));
}

void test_get_status_should_dispatch_to_sender_when_sender_context_active(void)
{
    bbt_status_t status;

    expect_core_init_success();

    bbt_core_get_context()->role = BBT_ROLE_IDLE;
    bbt_core_get_context()->sender.active = true;

    bbt_sender_get_status_ExpectAndReturn(&status, BBT_OK);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_core_get_status(&status));
}

void test_get_notify_buffer_should_return_valid_buffer(void)
{
    uint8_t *buf = bbt_core_get_notify_buffer();

    TEST_ASSERT_NOT_NULL(buf);

    buf[0] = 0xAAu;
    TEST_ASSERT_EQUAL_UINT8(0xAAu, bbt_core_get_notify_buffer()[0]);
}

/* -------------------------------------------------------------------------- */
/* Mode table                                                                 */
/* -------------------------------------------------------------------------- */

void test_mode_register_and_get_should_validate_args_and_store_entries(void)
{
    bbt_mode_entry_t entry = {
        .app_to_device = (const bbt_app_to_device_vtable_t *)0x11111111u,
        .device_to_app = (const bbt_device_to_app_vtable_t *)0x22222222u,
        .ctx = (void *)0x33333333u,
    };
    bbt_mode_entry_t out;

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG,
                      bbt_mode_register(BBT_MODE_TABLE_SIZE, &entry));
    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG,
                      bbt_mode_get_entry(BBT_MODE_TABLE_SIZE, &out));
    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG,
                      bbt_mode_get_entry(0u, NULL));

    expect_core_init_success();

    TEST_ASSERT_EQUAL(BBT_OK, bbt_mode_register(2u, &entry));
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BBT_OK, bbt_mode_get_entry(2u, &out));
    TEST_ASSERT_EQUAL_PTR(entry.app_to_device, out.app_to_device);
    TEST_ASSERT_EQUAL_PTR(entry.device_to_app, out.device_to_app);
    TEST_ASSERT_EQUAL_PTR(entry.ctx, out.ctx);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_mode_register(2u, NULL));
    memset(&out, 0xAA, sizeof(out));
    TEST_ASSERT_EQUAL(BBT_OK, bbt_mode_get_entry(2u, &out));
    TEST_ASSERT_NULL(out.app_to_device);
    TEST_ASSERT_NULL(out.device_to_app);
    TEST_ASSERT_NULL(out.ctx);
}

/* -------------------------------------------------------------------------- */
/* Events                                                                     */
/* -------------------------------------------------------------------------- */

static bbt_event_t last_event;
static const void *last_event_data;
static uint32_t event_count;

static void test_event_callback(bbt_event_t event, const void *event_data)
{
    last_event = event;
    last_event_data = event_data;
    event_count++;
}

void test_emit_event_should_call_registered_callback(void)
{
    uint32_t dummy_data = 0x12345678u;

    last_event = BBT_EVENT_ERROR;
    last_event_data = NULL;
    event_count = 0u;

    bbt_core_register_event_callback(test_event_callback);
    bbt_core_emit_event(BBT_EVENT_STARTED, &dummy_data);

    TEST_ASSERT_EQUAL_UINT32(1u, event_count);
    TEST_ASSERT_EQUAL(BBT_EVENT_STARTED, last_event);
    TEST_ASSERT_EQUAL_PTR(&dummy_data, last_event_data);

    bbt_core_register_event_callback(NULL);
}

void test_emit_event_should_do_nothing_when_callback_is_null(void)
{
    event_count = 0u;

    bbt_core_register_event_callback(NULL);
    bbt_core_emit_event(BBT_EVENT_STARTED, NULL);

    TEST_ASSERT_EQUAL_UINT32(0u, event_count);
}
