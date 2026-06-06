#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "ota-metadata.h"
#include "firmware-packet.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "sys/node-id.h"
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define OTA_TARGET_VERSION 2u

static struct simple_udp_connection udp_conn;

static uint16_t expected_block = 0;
static uint16_t known_total    = 0;
static uint32_t received_bytes = 0;
static uint32_t progressive_crc = 0;
static uint8_t  finished = 0;

static ota_boot_metadata_t boot_metadata = {
  .magic = OTA_IMAGE_MAGIC,
  .active_slot    = OTA_SLOT_A,
  .candidate_slot = OTA_SLOT_NONE,
  .state_a        = OTA_IMAGE_STATE_CONFIRMED,
  .state_b        = OTA_IMAGE_STATE_EMPTY,
};

PROCESS(udp_server_process, "OTA receiver (root)");
AUTOSTART_PROCESSES(&udp_server_process);

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static void
send_response(uint8_t type, uint16_t block_no, const uip_ipaddr_t *to)
{
  ota_packet_t resp;
  memset(&resp, 0, sizeof(resp));
  resp.type         = type;
  resp.block_no     = block_no;
  resp.total_blocks = known_total;
  resp.payload_len  = 0;
  resp.crc16 = ota_packet_crc16(&resp);
  simple_udp_sendto(&udp_conn, &resp, sizeof(resp), to);
}
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
                const uip_ipaddr_t *sender_addr,
                uint16_t sender_port,
                const uip_ipaddr_t *receiver_addr,
                uint16_t receiver_port,
                const uint8_t *data,
                uint16_t datalen)
{
  ota_packet_t pkt;
  uint16_t expected_crc;

  (void)c; (void)sender_port; (void)receiver_addr; (void)receiver_port;

  if(datalen != sizeof(ota_packet_t)) {
    LOG_WARN("Beklenmedik boyutta paket (len=%u)\n", (unsigned)datalen);
    return;
  }
  memcpy(&pkt, data, sizeof(pkt));
  expected_crc = pkt.crc16;
  pkt.crc16 = 0;
  if(ota_packet_crc16(&pkt) != expected_crc) {
    LOG_WARN("CRC16 hatasi, NACK gonderiliyor\n");
    send_response(PKT_NACK, expected_block, sender_addr);
    return;
  }

  if(finished && pkt.type == PKT_DONE) {
    send_response(PKT_COMPLETE, 0, sender_addr);
    return;
  }

  switch(pkt.type) {
  case PKT_DATA: handle_data(&pkt, sender_addr); break;
  case PKT_DONE: handle_done(&pkt, sender_addr); break;
  default:
    LOG_WARN("Bilinmeyen paket tipi: 0x%02x\n", (unsigned)pkt.type);
    break;
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();

  NETSTACK_ROUTING.root_start();
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_CLIENT_PORT, udp_rx_callback);

  LOG_INFO("OTA alici basladi (node %u, DAG root)\n", (unsigned)node_id);
  LOG_INFO("Baslangic metadata: slot A=CONFIRMED, slot B=EMPTY\n");

  while(1) {
    PROCESS_YIELD();
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
