#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "ota-metadata.h"
#include "firmware-packet.h"
#include "firmware_data.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "sys/node-id.h"
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define ACK_TIMEOUT      (2 * CLOCK_SECOND)
#define MAX_RETRIES      8
#define POST_DONE_RETRY  (3 * CLOCK_SECOND)

static struct simple_udp_connection udp_conn;

enum sender_state {
  ST_WAIT_DAG,
  ST_SEND_BLOCK,
  ST_SEND_DONE,
  ST_FINISHED,
  ST_ABORTED
};

static enum sender_state state = ST_WAIT_DAG;
static uint16_t current_block;
static uint16_t total_blocks;
static uint8_t retries;
static volatile int32_t ack_for_block = -1;
static volatile int32_t nack_for_block = -1;
static volatile uint8_t complete_received = 0;

PROCESS(udp_client_process, "OTA sender (node 2)");
AUTOSTART_PROCESSES(&udp_client_process);

/*---------------------------------------------------------------------------*/
static void
build_data_packet(uint16_t blk, ota_packet_t *p)
{
  uint32_t offset;
  uint16_t remaining;
  uint8_t  payload;

  offset    = (uint32_t)blk * OTA_BLOCK_SIZE;
  remaining = firmware_image_len - offset;
  payload   = (remaining >= OTA_BLOCK_SIZE) ? OTA_BLOCK_SIZE : (uint8_t)remaining;

  memset(p, 0, sizeof(*p));
  p->type         = PKT_DATA;
  p->block_no     = blk;
  p->total_blocks = total_blocks;
  p->payload_len  = payload;
  memcpy(p->data, &firmware_image[offset], payload);
  p->crc16 = ota_packet_crc16(p);
}
/*---------------------------------------------------------------------------*/
static void
build_done_packet(ota_packet_t *p)
{
  uint32_t image_crc = ota_crc32_buffer(firmware_image, firmware_image_len);

  memset(p, 0, sizeof(*p));
  p->type         = PKT_DONE;
  p->block_no     = 0;
  p->total_blocks = total_blocks;
  p->payload_len  = 4;
  p->data[0] = (uint8_t)(image_crc      );
  p->data[1] = (uint8_t)(image_crc >>  8);
  p->data[2] = (uint8_t)(image_crc >> 16);
  p->data[3] = (uint8_t)(image_crc >> 24);
  p->crc16   = ota_packet_crc16(p);
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
  uint16_t expected;

  (void)c; (void)sender_port; (void)receiver_addr; (void)receiver_port; (void)sender_addr;

  if(datalen != sizeof(ota_packet_t)) {
    return;
  }
  memcpy(&pkt, data, sizeof(pkt));
  expected = pkt.crc16;
  pkt.crc16 = 0;
  if(ota_packet_crc16(&pkt) != expected) {
    LOG_WARN("Bozuk CRC16 ile cevap geldi, atildi\n");
    return;
  }

  switch(pkt.type) {
  case PKT_ACK:
    ack_for_block = pkt.block_no;
    LOG_INFO("ACK alindi: blok %u\n", (unsigned)pkt.block_no);
    break;
  case PKT_NACK:
    nack_for_block = pkt.block_no;
    LOG_WARN("NACK alindi: blok %u\n", (unsigned)pkt.block_no);
    break;
  case PKT_COMPLETE:
    complete_received = 1;
    LOG_INFO("Alici COMPLETE bildirdi: yeni firmware alimi onaylandi\n");
    break;
  default:
    break;
  }

  process_poll(&udp_client_process);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer wait_timer;
  static uip_ipaddr_t dest_ipaddr;
  static ota_packet_t out_pkt;

  PROCESS_BEGIN();

  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  if(node_id != 2) {
    LOG_INFO("Bu cihaz (node %u) sender degil; sadece RPL aktarimina yardim eder\n",
             (unsigned)node_id);
    while(1) {
      PROCESS_YIELD();
    }
  }

  total_blocks = (firmware_image_len + OTA_BLOCK_SIZE - 1) / OTA_BLOCK_SIZE;
  LOG_INFO("OTA gonderici hazirlaniyor: imaj=%lu bayt, blok=%u, toplam=%u paket\n",
           (unsigned long)firmware_image_len,
           (unsigned)OTA_BLOCK_SIZE, (unsigned)total_blocks);

  /* DAG'in hazirlanmasini ve root adresini bulmayi bekle */
  etimer_set(&wait_timer, CLOCK_SECOND);
  while(!NETSTACK_ROUTING.node_is_reachable() ||
        !NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
    LOG_INFO("RPL agi hazir degil, bekleniyor...\n");
    etimer_reset(&wait_timer);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
  }

  LOG_INFO("Alici (root) bulundu: ");
  LOG_INFO_6ADDR(&dest_ipaddr);
  LOG_INFO_("\n");

  state          = ST_SEND_BLOCK;
  current_block  = 0;
  retries        = 0;
  ack_for_block  = -1;
  nack_for_block = -1;

  while(state != ST_FINISHED && state != ST_ABORTED) {

    if(state == ST_SEND_BLOCK) {
      build_data_packet(current_block, &out_pkt);
      LOG_INFO("DATA gonderiliyor: blok %u/%u (deneme %u)\n",
               (unsigned)current_block, (unsigned)total_blocks,
               (unsigned)(retries + 1));
      simple_udp_sendto(&udp_conn, &out_pkt, sizeof(out_pkt), &dest_ipaddr);

    } else if(state == ST_SEND_DONE) {
      build_done_packet(&out_pkt);
      LOG_INFO("DONE gonderiliyor: total_crc32 paketin icinde, COMPLETE bekleniyor\n");
      simple_udp_sendto(&udp_conn, &out_pkt, sizeof(out_pkt), &dest_ipaddr);
    }

    etimer_set(&wait_timer, (state == ST_SEND_DONE) ? POST_DONE_RETRY : ACK_TIMEOUT);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer) || ev == PROCESS_EVENT_POLL);

    if(state == ST_SEND_BLOCK) {
      if(ack_for_block == (int32_t)current_block) {
        ack_for_block = -1;
        retries = 0;
        current_block++;
        if(current_block >= total_blocks) {
          state = ST_SEND_DONE;
          LOG_INFO("Tum bloklar ACK'lendi (%u adet), DONE'a geciyor\n",
                   (unsigned)total_blocks);
        }
      } else if(nack_for_block == (int32_t)current_block) {
        nack_for_block = -1;
        retries++;
        LOG_WARN("Blok %u NACK; yeniden gonderilecek (deneme %u)\n",
                 (unsigned)current_block, (unsigned)(retries + 1));
      } else {
        retries++;
        LOG_WARN("Blok %u ACK gelmedi (timeout), tekrar deneniyor\n",
                 (unsigned)current_block);
      }

      if(retries >= MAX_RETRIES) {
        LOG_ERR("Blok %u icin %u deneme basarisiz, aktarim iptal\n",
                (unsigned)current_block, (unsigned)MAX_RETRIES);
        state = ST_ABORTED;
      }

    } else if(state == ST_SEND_DONE) {
      if(complete_received) {
        state = ST_FINISHED;
      } else {
        retries++;
        if(retries >= MAX_RETRIES) {
          LOG_ERR("DONE icin COMPLETE alinamadi, aktarim iptal\n");
          state = ST_ABORTED;
        } else {
          LOG_WARN("COMPLETE gelmedi, DONE tekrar gonderiliyor (deneme %u)\n",
                   (unsigned)(retries + 1));
        }
      }
    }
  }

  if(state == ST_FINISHED) {
    LOG_INFO("=== OTA AKTARIMI BASARILI: %lu bayt, %u blok ===\n",
             (unsigned long)firmware_image_len, (unsigned)total_blocks);
  } else {
    LOG_ERR("=== OTA AKTARIMI BASARISIZ ===\n");
  }

  while(1) {
    PROCESS_YIELD();
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
