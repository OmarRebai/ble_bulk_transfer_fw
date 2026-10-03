#include "unity.h"
#include "bbt_packet.h"

#include <stdint.h>
#include <string.h>

static uint8_t buf[512];

void setUp(void)
{
    memset(buf, 0, sizeof(buf));
}

void tearDown(void)
{
}

/* CRC */

void test_crc16_ccitt_known_vector(void)
{
    const uint8_t data[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, bbt_crc16_ccitt(data, 9u));
}

void test_crc16_null_returns_initial_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, bbt_crc16_ccitt(NULL, 10u));
}

void test_crc32_known_vector(void)
{
    const uint8_t data[] = "123456789";

    uint32_t crc = bbt_crc32_init();
    crc = bbt_crc32_update(crc, data, 9u);
    crc = bbt_crc32_final(crc);

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc);
}

void test_crc32_null_keeps_current_crc(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x12345678u,
                            bbt_crc32_update(0x12345678u, NULL, 10u));
}

/* START */

void test_build_and_parse_start_packet(void)
{
    bbt_start_meta_t meta = {
        .transfer_id = 0x11u,
        .total_size = 1024u,
        .chunk_size = 128u,
        .total_chunks = 8u,
    };

    uint16_t len = bbt_packet_build_start(buf, sizeof(buf), &meta);

    TEST_ASSERT_EQUAL_UINT16(BBT_PACKET_START_FIXED_SIZE, len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_START, buf[0]);

    bbt_start_meta_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_start(buf, len, &parsed));

    TEST_ASSERT_EQUAL_UINT8(meta.transfer_id, parsed.transfer_id);
    TEST_ASSERT_EQUAL_UINT32(meta.total_size, parsed.total_size);
    TEST_ASSERT_EQUAL_UINT16(meta.chunk_size, parsed.chunk_size);
    TEST_ASSERT_EQUAL_UINT16(meta.total_chunks, parsed.total_chunks);
}

void test_build_start_rejects_invalid_args(void)
{
    bbt_start_meta_t meta = {0};

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_start(NULL, sizeof(buf), &meta));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_start(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_start(buf, BBT_PACKET_START_FIXED_SIZE - 1u, &meta));
}

void test_parse_start_rejects_invalid_packets(void)
{
    bbt_start_meta_t out;

    TEST_ASSERT_FALSE(bbt_packet_parse_start(NULL, BBT_PACKET_START_FIXED_SIZE, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_start(buf, BBT_PACKET_START_FIXED_SIZE, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_start(buf, BBT_PACKET_START_FIXED_SIZE - 1u, &out));

    buf[0] = BBT_OPCODE_CHUNK;
    TEST_ASSERT_FALSE(bbt_packet_parse_start(buf, BBT_PACKET_START_FIXED_SIZE, &out));
}

/* CHUNK */

void test_build_and_parse_chunk_packet(void)
{
    const uint8_t payload[] = {0x10u, 0x20u, 0x30u, 0x40u};

    bbt_chunk_header_t header = {
        .transfer_id = 0x22u,
        .seq = 3u,
        .payload_len = sizeof(payload),
        .payload_crc16 = 0u,
    };

    uint16_t len = bbt_packet_build_chunk(buf, sizeof(buf), &header, payload);

    TEST_ASSERT_EQUAL_UINT16(BBT_PACKET_CHUNK_HEADER_SIZE + sizeof(payload), len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_CHUNK, buf[0]);

    bbt_chunk_packet_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_chunk(buf, len, &parsed));

    TEST_ASSERT_EQUAL_UINT8(header.transfer_id, parsed.header.transfer_id);
    TEST_ASSERT_EQUAL_UINT16(header.seq, parsed.header.seq);
    TEST_ASSERT_EQUAL_UINT16(header.payload_len, parsed.header.payload_len);
    TEST_ASSERT_EQUAL_HEX16(bbt_crc16_ccitt(payload, sizeof(payload)),
                            parsed.header.payload_crc16);
    TEST_ASSERT_EQUAL_MEMORY(payload, parsed.payload, sizeof(payload));
}

void test_build_chunk_supports_payload_already_in_output_buffer(void)
{
    uint8_t *payload = &buf[BBT_PACKET_CHUNK_HEADER_SIZE];

    payload[0] = 0xAAu;
    payload[1] = 0xBBu;
    payload[2] = 0xCCu;

    bbt_chunk_header_t header = {
        .transfer_id = 0x33u,
        .seq = 9u,
        .payload_len = 3u,
        .payload_crc16 = 0u,
    };

    uint16_t len = bbt_packet_build_chunk(buf, sizeof(buf), &header, payload);

    TEST_ASSERT_EQUAL_UINT16(BBT_PACKET_CHUNK_HEADER_SIZE + 3u, len);

    bbt_chunk_packet_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_chunk(buf, len, &parsed));

    TEST_ASSERT_EQUAL_UINT8(0xAAu, parsed.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBBu, parsed.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCCu, parsed.payload[2]);
}

void test_build_chunk_rejects_invalid_args(void)
{
    const uint8_t payload[] = {1u, 2u, 3u};

    bbt_chunk_header_t header = {
        .transfer_id = 1u,
        .seq = 0u,
        .payload_len = sizeof(payload),
        .payload_crc16 = 0u,
    };

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_chunk(NULL, sizeof(buf), &header, payload));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_chunk(buf, sizeof(buf), NULL, payload));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_chunk(buf, sizeof(buf), &header, NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_chunk(buf, BBT_PACKET_CHUNK_HEADER_SIZE + sizeof(payload) - 1u, &header, payload));
}

void test_parse_chunk_rejects_invalid_packets(void)
{
    bbt_chunk_packet_t out;

    TEST_ASSERT_FALSE(bbt_packet_parse_chunk(NULL, BBT_PACKET_CHUNK_HEADER_SIZE, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_chunk(buf, BBT_PACKET_CHUNK_HEADER_SIZE, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_chunk(buf, BBT_PACKET_CHUNK_HEADER_SIZE - 1u, &out));

    buf[0] = BBT_OPCODE_START;
    TEST_ASSERT_FALSE(bbt_packet_parse_chunk(buf, BBT_PACKET_CHUNK_HEADER_SIZE, &out));
}

void test_parse_chunk_rejects_short_payload(void)
{
    const uint8_t payload[] = {1u, 2u, 3u, 4u};

    bbt_chunk_header_t header = {
        .transfer_id = 1u,
        .seq = 1u,
        .payload_len = sizeof(payload),
        .payload_crc16 = 0u,
    };

    uint16_t len = bbt_packet_build_chunk(buf, sizeof(buf), &header, payload);

    bbt_chunk_packet_t out;
    TEST_ASSERT_FALSE(bbt_packet_parse_chunk(buf, len - 1u, &out));
}

void test_parse_chunk_rejects_payload_len_bigger_than_max(void)
{
    bbt_chunk_header_t header = {
        .transfer_id = 1u,
        .seq = 1u,
        .payload_len = BBT_MAX_CHUNK_SIZE + 1u,
        .payload_crc16 = 0u,
    };

    buf[0] = BBT_OPCODE_CHUNK;
    memcpy(&buf[1], &header, sizeof(header));

    bbt_chunk_packet_t out;
    TEST_ASSERT_FALSE(bbt_packet_parse_chunk(buf, BBT_PACKET_CHUNK_HEADER_SIZE, &out));
}

/* END / ABORT / COMPLETE */

void test_build_and_parse_end_packet(void)
{
    bbt_packet_id_t id = {.transfer_id = 0x44u};

    uint16_t len = bbt_packet_build_end(buf, sizeof(buf), &id);

    TEST_ASSERT_EQUAL_UINT16(2u, len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_END, buf[0]);

    bbt_packet_id_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_end(buf, len, &parsed));
    TEST_ASSERT_EQUAL_UINT8(id.transfer_id, parsed.transfer_id);
}

void test_parse_abort_packet(void)
{
    buf[0] = BBT_OPCODE_ABORT;
    buf[1] = 0x66u;

    bbt_packet_id_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_abort(buf, 2u, &parsed));
    TEST_ASSERT_EQUAL_UINT8(0x66u, parsed.transfer_id);
}

void test_build_and_parse_complete_packet(void)
{
    bbt_packet_id_t id = {.transfer_id = 0x55u};

    uint16_t len = bbt_packet_build_complete(buf, sizeof(buf), &id);

    TEST_ASSERT_EQUAL_UINT16(2u, len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_COMPLETE, buf[0]);

    bbt_packet_id_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_complete(buf, len, &parsed));
    TEST_ASSERT_EQUAL_UINT8(id.transfer_id, parsed.transfer_id);
}

void test_packet_id_parsers_reject_invalid_packets(void)
{
    bbt_packet_id_t out;

    TEST_ASSERT_FALSE(bbt_packet_parse_end(NULL, 2u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_end(buf, 2u, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_end(buf, 1u, &out));

    buf[0] = BBT_OPCODE_START;
    buf[1] = 0x01u;

    TEST_ASSERT_FALSE(bbt_packet_parse_end(buf, 2u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_abort(buf, 2u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_complete(buf, 2u, &out));
}

void test_abort_and_complete_parsers_reject_null_and_short_packets(void)
{
    bbt_packet_id_t out;

    TEST_ASSERT_FALSE(bbt_packet_parse_abort(NULL, 2u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_abort(buf, 2u, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_abort(buf, 1u, &out));

    TEST_ASSERT_FALSE(bbt_packet_parse_complete(NULL, 2u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_complete(buf, 2u, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_complete(buf, 1u, &out));
}

void test_packet_id_builders_reject_invalid_args(void)
{
    bbt_packet_id_t id = {.transfer_id = 1u};

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_end(NULL, sizeof(buf), &id));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_end(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_end(buf, 1u, &id));

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_complete(NULL, sizeof(buf), &id));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_complete(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_complete(buf, 1u, &id));
}

/* START_ACK */

void test_build_and_parse_start_ack_packet(void)
{
    bbt_start_ack_t ack = {
        .transfer_id = 0x77u,
        .status = BBT_PROTO_STATUS_OK,
        .accepted_chunk_size = 128u,
    };

    uint16_t len = bbt_packet_build_start_ack(buf, sizeof(buf), &ack);

    TEST_ASSERT_EQUAL_UINT16(1u + sizeof(bbt_start_ack_t), len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_START_ACK, buf[0]);

    bbt_start_ack_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(buf, len, &parsed));

    TEST_ASSERT_EQUAL_UINT8(ack.transfer_id, parsed.transfer_id);
    TEST_ASSERT_EQUAL_UINT8(ack.status, parsed.status);
    TEST_ASSERT_EQUAL_UINT16(ack.accepted_chunk_size, parsed.accepted_chunk_size);
}

void test_start_ack_rejects_invalid_args(void)
{
    bbt_start_ack_t ack = {0};
    bbt_start_ack_t out;

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_start_ack(NULL, sizeof(buf), &ack));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_start_ack(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_start_ack(buf, 1u + sizeof(bbt_start_ack_t) - 1u, &ack));

    TEST_ASSERT_FALSE(bbt_packet_parse_start_ack(NULL, 5u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_start_ack(buf, 5u, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_start_ack(buf, 4u, &out));

    buf[0] = BBT_OPCODE_START;
    TEST_ASSERT_FALSE(bbt_packet_parse_start_ack(buf, 5u, &out));
}

/* PULL_REQ */

void test_build_and_parse_pull_req_packet(void)
{
    bbt_pull_req_t req = {
        .transfer_id = 0x88u,
        .max_chunk_size = 200u,
    };

    uint16_t len = bbt_packet_build_pull_req(buf, sizeof(buf), &req);

    TEST_ASSERT_EQUAL_UINT16(1u + sizeof(bbt_pull_req_t), len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_PULL_REQ, buf[0]);

    bbt_pull_req_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_pull_req(buf, len, &parsed));

    TEST_ASSERT_EQUAL_UINT8(req.transfer_id, parsed.transfer_id);
    TEST_ASSERT_EQUAL_UINT16(req.max_chunk_size, parsed.max_chunk_size);
}

void test_pull_req_rejects_invalid_args(void)
{
    bbt_pull_req_t req = {0};
    bbt_pull_req_t out;

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_pull_req(NULL, sizeof(buf), &req));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_pull_req(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_pull_req(buf, 1u + sizeof(bbt_pull_req_t) - 1u, &req));

    TEST_ASSERT_FALSE(bbt_packet_parse_pull_req(NULL, 4u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_pull_req(buf, 4u, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_pull_req(buf, 3u, &out));

    buf[0] = BBT_OPCODE_START;
    TEST_ASSERT_FALSE(bbt_packet_parse_pull_req(buf, 4u, &out));
}

/* NACK */

void test_build_and_parse_nack_packet(void)
{
    const uint8_t bitmap[] = {0x01u, 0x02u, 0x80u};

    bbt_nack_header_t header = {
        .transfer_id = 0x99u,
        .flags = 0x01u,
        .bitmap_len = sizeof(bitmap),
    };

    uint16_t len = bbt_packet_build_nack(buf, sizeof(buf), &header, bitmap);

    TEST_ASSERT_EQUAL_UINT16(1u + sizeof(bbt_nack_header_t) + sizeof(bitmap), len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_NACK, buf[0]);

    bbt_nack_header_t parsed;
    const uint8_t *parsed_bitmap = NULL;
    uint16_t parsed_bitmap_len = 0u;

    TEST_ASSERT_TRUE(bbt_packet_parse_nack(buf, len, &parsed, &parsed_bitmap, &parsed_bitmap_len));

    TEST_ASSERT_EQUAL_UINT8(header.transfer_id, parsed.transfer_id);
    TEST_ASSERT_EQUAL_UINT8(header.flags, parsed.flags);
    TEST_ASSERT_EQUAL_UINT16(header.bitmap_len, parsed.bitmap_len);
    TEST_ASSERT_EQUAL_UINT16(sizeof(bitmap), parsed_bitmap_len);
    TEST_ASSERT_EQUAL_MEMORY(bitmap, parsed_bitmap, sizeof(bitmap));
}

void test_parse_nack_supports_null_optional_outputs(void)
{
    const uint8_t bitmap[] = {0xAAu};

    bbt_nack_header_t header = {
        .transfer_id = 0x12u,
        .flags = 0u,
        .bitmap_len = sizeof(bitmap),
    };

    uint16_t len = bbt_packet_build_nack(buf, sizeof(buf), &header, bitmap);

    bbt_nack_header_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_nack(buf, len, &parsed, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT8(header.transfer_id, parsed.transfer_id);
}

void test_nack_rejects_invalid_args(void)
{
    const uint8_t bitmap[] = {0x01u};

    bbt_nack_header_t header = {
        .transfer_id = 1u,
        .flags = 0u,
        .bitmap_len = sizeof(bitmap),
    };

    bbt_nack_header_t out;

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_nack(NULL, sizeof(buf), &header, bitmap));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_nack(buf, sizeof(buf), NULL, bitmap));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_nack(buf, sizeof(buf), &header, NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_nack(buf, 1u + sizeof(bbt_nack_header_t), &header, bitmap));

    TEST_ASSERT_FALSE(bbt_packet_parse_nack(NULL, 1u + sizeof(bbt_nack_header_t), &out, NULL, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_nack(buf, 1u + sizeof(bbt_nack_header_t), NULL, NULL, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_nack(buf, sizeof(bbt_nack_header_t), &out, NULL, NULL));

    buf[0] = BBT_OPCODE_START;
    TEST_ASSERT_FALSE(bbt_packet_parse_nack(buf, 1u + sizeof(bbt_nack_header_t), &out, NULL, NULL));
}

void test_parse_nack_rejects_short_bitmap_payload(void)
{
    const uint8_t bitmap[] = {0x01u, 0x02u, 0x03u};

    bbt_nack_header_t header = {
        .transfer_id = 1u,
        .flags = 0u,
        .bitmap_len = sizeof(bitmap),
    };

    uint16_t len = bbt_packet_build_nack(buf, sizeof(buf), &header, bitmap);

    bbt_nack_header_t out;
    TEST_ASSERT_FALSE(bbt_packet_parse_nack(buf, len - 1u, &out, NULL, NULL));
}

/* ERROR */

void test_build_and_parse_error_packet(void)
{
    bbt_error_packet_t error = {
        .transfer_id = 0xABu,
        .error = BBT_PROTO_ERR_INVALID_PACKET,
    };

    uint16_t len = bbt_packet_build_error(buf, sizeof(buf), &error);

    TEST_ASSERT_EQUAL_UINT16(1u + sizeof(bbt_error_packet_t), len);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, buf[0]);

    bbt_error_packet_t parsed;
    TEST_ASSERT_TRUE(bbt_packet_parse_error(buf, len, &parsed));

    TEST_ASSERT_EQUAL_UINT8(error.transfer_id, parsed.transfer_id);
    TEST_ASSERT_EQUAL_UINT8(error.error, parsed.error);
}

void test_error_rejects_invalid_args(void)
{
    bbt_error_packet_t error = {0};
    bbt_error_packet_t out;

    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_error(NULL, sizeof(buf), &error));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_error(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, bbt_packet_build_error(buf, 1u + sizeof(bbt_error_packet_t) - 1u, &error));

    TEST_ASSERT_FALSE(bbt_packet_parse_error(NULL, 3u, &out));
    TEST_ASSERT_FALSE(bbt_packet_parse_error(buf, 3u, NULL));
    TEST_ASSERT_FALSE(bbt_packet_parse_error(buf, 2u, &out));

    buf[0] = BBT_OPCODE_START;
    TEST_ASSERT_FALSE(bbt_packet_parse_error(buf, 3u, &out));
}
