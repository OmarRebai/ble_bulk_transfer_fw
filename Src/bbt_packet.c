/**
 * @file            bbt_packet.c
 * @brief           Packet parsing and building utilities used by the BBT protocol.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_packet.h"

#include <string.h>

/* ============================================================================
 * Native-endian packet casts (little-endian target)
 * ==========================================================================*/

#define PROTOCOL_CAST_PAYLOAD(type, buffer, offset) (*((type *)(&(buffer)[offset])))
#define PROTOCOL_CAST_PAYLOAD_CONST(type, buffer, offset) (*((const type *)(&(buffer)[offset])))

/* ============================================================================
 * Packet parsing
 * ==========================================================================*/

bool bbt_packet_parse_start(const uint8_t *data, uint16_t len, bbt_start_meta_t *out)
{
  if ((data == NULL) || (out == NULL))
  {
    return false;
  }

  if (len < BBT_PACKET_START_FIXED_SIZE)
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_START)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_start_meta_t, data, 1u);

  return true;
}

bool bbt_packet_parse_chunk(const uint8_t *data, uint16_t len, bbt_chunk_packet_t *out)
{
  if ((data == NULL) || (out == NULL))
  {
    return false;
  }

  if (len < BBT_PACKET_CHUNK_HEADER_SIZE)
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_CHUNK)
  {
    return false;
  }

  const bbt_chunk_header_t *hdr = &PROTOCOL_CAST_PAYLOAD_CONST(bbt_chunk_header_t, data, 1u);
  uint16_t payload_len          = hdr->payload_len;

  if (payload_len > BBT_MAX_CHUNK_SIZE)
  {
    return false;
  }

  uint16_t needed = (uint16_t)(BBT_PACKET_CHUNK_HEADER_SIZE + payload_len);
  if (len < needed)
  {
    return false;
  }

  out->header  = *hdr;
  out->payload = &data[BBT_PACKET_CHUNK_HEADER_SIZE];

  return true;
}

bool bbt_packet_parse_end(const uint8_t *data, uint16_t len, bbt_packet_id_t *out)
{
  if ((data == NULL) || (out == NULL))
  {
    return false;
  }

  if (len < 2u)
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_END)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_packet_id_t, data, 1u);
  return true;
}

bool bbt_packet_parse_abort(const uint8_t *data, uint16_t len, bbt_packet_id_t *out)
{
  if ((data == NULL) || (out == NULL))
  {
    return false;
  }

  if (len < 2u)
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_ABORT)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_packet_id_t, data, 1u);
  return true;
}

bool bbt_packet_parse_pull_req(const uint8_t *data, uint16_t len, bbt_pull_req_t *out)
{
  if ((data == NULL) || (out == NULL))
  {
    return false;
  }

  if (len < 4u)
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_PULL_REQ)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_pull_req_t, data, 1u);
  return true;
}

bool bbt_packet_parse_nack(const uint8_t *data, uint16_t len, bbt_nack_header_t *out, const uint8_t **bitmap,
                           uint16_t *bitmap_len)
{
  if ((data == NULL) || (out == NULL))
  {
    return false;
  }

  uint16_t header_len = (uint16_t)(1u + sizeof(bbt_nack_header_t));
  if (len < header_len)
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_NACK)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_nack_header_t, data, 1u);

  uint16_t payload_len = out->bitmap_len;
  uint32_t needed = (uint32_t)header_len + (uint32_t)payload_len;
  if (len < needed)
  {
    return false;
  }

  if (bitmap != NULL)
  {
    *bitmap = &data[header_len];
  }

  if (bitmap_len != NULL)
  {
    *bitmap_len = payload_len;
  }

  return true;
}

bool bbt_packet_parse_complete(const uint8_t *data, uint16_t len, bbt_packet_id_t *out)
{
  if ((data == NULL) || (out == NULL) || (len < 2u))
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_COMPLETE)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_packet_id_t, data, 1u);
  return true;
}

bool bbt_packet_parse_error(const uint8_t *data, uint16_t len, bbt_error_packet_t *out)
{
  if ((data == NULL) || (out == NULL) || (len < 3u))
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_ERROR)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_error_packet_t, data, 1u);
  return true;
}

/* ============================================================================
 * Packet builders
 * ==========================================================================*/

uint16_t bbt_packet_build_start_ack(uint8_t *buf, uint16_t max_len, const bbt_start_ack_t *ack)
{
  const uint16_t needed = (uint16_t)(1u + sizeof(bbt_start_ack_t));
  if ((buf == NULL) || (ack == NULL) || (max_len < needed))
  {
    return 0u;
  }

  buf[0] = BBT_OPCODE_START_ACK;
  PROTOCOL_CAST_PAYLOAD(bbt_start_ack_t, buf, 1u) = *ack;

  return needed;
}

uint16_t bbt_packet_build_complete(uint8_t *buf, uint16_t max_len, const bbt_packet_id_t *id)
{
  const uint16_t needed = (uint16_t)(1u + sizeof(bbt_packet_id_t));
  if ((buf == NULL) || (id == NULL) || (max_len < needed))
  {
    return 0u;
  }

  buf[0] = BBT_OPCODE_COMPLETE;
  PROTOCOL_CAST_PAYLOAD(bbt_packet_id_t, buf, 1u) = *id;

  return needed;
}

uint16_t bbt_packet_build_error(uint8_t *buf, uint16_t max_len, const bbt_error_packet_t *error)
{
  const uint16_t needed = (uint16_t)(1u + sizeof(bbt_error_packet_t));
  if ((buf == NULL) || (error == NULL) || (max_len < needed))
  {
    return 0u;
  }

  buf[0] = BBT_OPCODE_ERROR;
  PROTOCOL_CAST_PAYLOAD(bbt_error_packet_t, buf, 1u) = *error;

  return needed;
}

uint16_t bbt_packet_build_start(uint8_t *buf, uint16_t max_len, const bbt_start_meta_t *meta)
{
  if ((buf == NULL) || (meta == NULL))
  {
    return 0u;
  }

  if (max_len < BBT_PACKET_START_FIXED_SIZE)
  {
    return 0u;
  }

  buf[0] = BBT_OPCODE_START;
  PROTOCOL_CAST_PAYLOAD(bbt_start_meta_t, buf, 1u) = *meta;

  return BBT_PACKET_START_FIXED_SIZE;
}

bool bbt_packet_parse_start_ack(const uint8_t *data, uint16_t len, bbt_start_ack_t *out)
{
  if ((data == NULL) || (out == NULL))
  {
    return false;
  }

  if (len < 5u)
  {
    return false;
  }

  if (data[0] != BBT_OPCODE_START_ACK)
  {
    return false;
  }

  *out = PROTOCOL_CAST_PAYLOAD_CONST(bbt_start_ack_t, data, 1u);

  return true;
}

uint16_t bbt_packet_build_pull_req(uint8_t *buf, uint16_t max_len, const bbt_pull_req_t *req)
{
  const uint16_t needed = (uint16_t)(1u + sizeof(bbt_pull_req_t));
  if ((buf == NULL) || (req == NULL) || (max_len < needed))
  {
    return 0u;
  }

  buf[0] = BBT_OPCODE_PULL_REQ;
  PROTOCOL_CAST_PAYLOAD(bbt_pull_req_t, buf, 1u) = *req;
  return needed;
}

uint16_t bbt_packet_build_chunk(uint8_t *buf, uint16_t max_len, const bbt_chunk_header_t *header,
                                const uint8_t *payload)
{
  if ((buf == NULL) || (header == NULL) || (payload == NULL))
  {
    return 0u;
  }

  uint16_t needed = (uint16_t)(BBT_PACKET_CHUNK_HEADER_SIZE + header->payload_len);
  if (max_len < needed)
  {
    return 0u;
  }

  bbt_chunk_header_t wire_header = *header;
  wire_header.payload_crc16      = bbt_crc16_ccitt(payload, header->payload_len);

  buf[0] = BBT_OPCODE_CHUNK;
  PROTOCOL_CAST_PAYLOAD(bbt_chunk_header_t, buf, 1u) = wire_header;
  if (payload != &buf[BBT_PACKET_CHUNK_HEADER_SIZE])
  {
    memcpy(&buf[BBT_PACKET_CHUNK_HEADER_SIZE], payload, header->payload_len);
  }

  return needed;
}

uint16_t bbt_packet_build_end(uint8_t *buf, uint16_t max_len, const bbt_packet_id_t *id)
{
  const uint16_t needed = (uint16_t)(1u + sizeof(bbt_packet_id_t));
  if ((buf == NULL) || (id == NULL) || (max_len < needed))
  {
    return 0u;
  }

  buf[0] = BBT_OPCODE_END;
  PROTOCOL_CAST_PAYLOAD(bbt_packet_id_t, buf, 1u) = *id;
  return needed;
}

/*
 * NACK format (bitmap segments):
 *
 *   opcode              1
 *   header               sizeof(bbt_nack_header_t)
 *     transfer_id       1
 *     flags             1  (bit0 = more segments follow)
 *     bitmap_len        2  (number of payload bytes in this packet)
 *   bitmap[]            bitmap_len
 */
uint16_t bbt_packet_build_nack(uint8_t *buf, uint16_t max_len, const bbt_nack_header_t *header,
                               const uint8_t *bitmap)
{
  if ((buf == NULL) || (header == NULL) || (bitmap == NULL))
  {
    return 0u;
  }

  const uint16_t header_len = (uint16_t)(1u + sizeof(bbt_nack_header_t));
  uint16_t       needed     = (uint16_t)(header_len + header->bitmap_len);

  if (max_len < needed)
  {
    return 0u;
  }

  buf[0] = BBT_OPCODE_NACK;
  PROTOCOL_CAST_PAYLOAD(bbt_nack_header_t, buf, 1u) = *header;

  uint16_t pos = header_len;
  memcpy(&buf[pos], bitmap, header->bitmap_len);
  pos += header->bitmap_len;

  return pos;
}

/* ============================================================================
 * CRC16-CCITT-FALSE
 *
 * Polynomial: 0x1021
 * Init:       0xFFFF
 * XOR out:    0x0000
 * ==========================================================================*/

uint16_t bbt_crc16_ccitt(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu;

  if (data == NULL)
  {
    return crc;
  }

  for (uint16_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)data[i] << 8;

    for (uint8_t bit = 0; bit < 8u; bit++)
    {
      if ((crc & 0x8000u) != 0u)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021u);
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return crc;
}

/* ============================================================================
 * CRC32 IEEE 802.3
 *
 * Polynomial: reflected 0xEDB88320
 * Init:       0xFFFFFFFF
 * XOR out:    0xFFFFFFFF
 * ==========================================================================*/

uint32_t bbt_crc32_init(void)
{
  return 0xFFFFFFFFu;
}

uint32_t bbt_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
  if (data == NULL)
  {
    return crc;
  }

  for (uint32_t i = 0; i < len; i++)
  {
    crc ^= data[i];

    for (uint8_t bit = 0; bit < 8u; bit++)
    {
      if ((crc & 1u) != 0u)
      {
        crc = (crc >> 1) ^ 0xEDB88320u;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  return crc;
}

uint32_t bbt_crc32_final(uint32_t crc)
{
  return crc ^ 0xFFFFFFFFu;
}
