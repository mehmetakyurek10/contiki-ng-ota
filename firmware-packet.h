#ifndef FIRMWARE_PACKET_H_
#define FIRMWARE_PACKET_H_

#include <stdint.h>

#define OTA_BLOCK_SIZE 64

typedef enum {
  PKT_DATA     = 0x01,
  PKT_ACK      = 0x02,
  PKT_NACK     = 0x03,
  PKT_DONE     = 0x05,
  PKT_COMPLETE = 0x06
} pkt_type_t;

typedef struct __attribute__((packed)) {
  uint8_t  type;
  uint16_t block_no;
  uint16_t total_blocks;
  uint8_t  payload_len;
  uint8_t  data[OTA_BLOCK_SIZE];
  uint16_t crc16;
} ota_packet_t;

static inline uint16_t
ota_crc16_update(uint16_t crc, uint8_t byte)
{
  uint8_t i;
  crc ^= byte;
  for(i = 0; i < 8; i++) {
    if(crc & 0x0001u) {
      crc = (crc >> 1) ^ 0xA001u;
    } else {
      crc >>= 1;
    }
  }
  return crc;
}

static inline uint16_t
ota_packet_crc16(const ota_packet_t *p)
{
  uint16_t crc = 0xFFFFu;
  uint8_t i;
  const uint8_t *bytes = (const uint8_t *)p;
  unsigned header_len = sizeof(ota_packet_t) - sizeof(p->data) - sizeof(p->crc16);

  for(i = 0; i < header_len; i++) {
    crc = ota_crc16_update(crc, bytes[i]);
  }
  for(i = 0; i < p->payload_len; i++) {
    crc = ota_crc16_update(crc, p->data[i]);
  }
  return crc;
}

#endif /* FIRMWARE_PACKET_H_ */
