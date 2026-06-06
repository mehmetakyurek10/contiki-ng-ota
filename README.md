# BİL 304 İşletim Sistemleri — OTA Firmware Aktarımı

**Ondokuz Mayıs Üniversitesi · Bilgisayar Mühendisliği · Bahar 2025/2026**
**Dersi veren:** Doç. Dr. Sercan Demirci · **Asistan:** Arş. Gör. İsmail Hakkı Turan

> Bu klasör, Contiki-NG'nin `examples/rpl-udp` örneği üzerine kurulmuş bir **OTA (Over-The-Air) firmware aktarımı** projesidir. Cooja simülatöründe, bir gönderici düğüm 8 KB'lık yeni firmware imajını UDP üzerinden komşusu üzerinden uzaktaki alıcı düğüme parçalı olarak iletir; alıcı imajı bütünleyip CRC32 ile doğrular ve `ota-metadata` API'ı üzerinden boot adayı olarak işaretler.

---

## 🎥 Demo Videosu

📺 **Video Linki:** _<!-- BURAYA VİDEO LİNKİNİZİ KOYUN -->_

Videoda projenin amacı, sistem mimarisi, paket akışı ve Cooja üzerinde uçtan uca başarılı aktarım demo'su anlatılmaktadır.

---

## 📌 Proje Özeti

Telsiz duyarga ağındaki (WSN) cihazlara fiziksel erişim olmadan yazılım güncelleme problemi — gerçek dünyada Tesla araç güncellemeleri, Apple AirTag, akıllı sayaçlar ve IoT cihazlarının her gün çözdüğü problem. Bu projede aynı senaryo Contiki-NG / Cooja üzerinde simüle edilmiştir.

**Hedef:** Bir kablosuz duyarga ağı üzerinden, ağ kesintilerine ve paket kayıplarına karşı dayanıklı şekilde, doğrulanmış bir firmware imajını uzaktaki bir düğüme güvenle teslim etmek.

---

## 👥 Ekip ve İş Bölümü

| Ekip Üyesi | 
|---|---|
| Mehmet Akyürek | 
| Aslıhan Erturhan | 
| Ali Ellikci | 

---

## 🧩 Sistem Mimarisi

```
   ┌──────────────┐         ┌──────────────┐         ┌──────────────┐
   │  Node 2      │  UDP    │  Node 3      │  UDP    │  Node 1      │
   │  Gönderici   ├────────►│  Yönlendirici├────────►│  Alıcı       │
   │  (Client)    │ (RPL)   │  (Forwarder) │ (RPL)   │  (Server/DAG │
   │              │         │              │         │   root)      │
   └──────────────┘         └──────────────┘         └──────────────┘
        ID:2                     ID:3                     ID:1
```

- **3 düğüm**, Z1 (MSP430) hedef platformu
- **RPL** üzerinde UDP haberleşme, **Stop-and-Wait** ARQ
- Radyo modeli: **UDGM** (%100 başarım, ideal kanal)
- Toplam aktarım: **8192 bayt** firmware imajı, **128 blok × 64 bayt**
- **CRC16** her pakette, **CRC32** tüm imaj üzerinden son doğrulama

---

## 📦 Paket Formatı (`firmware-packet.h`)

```c
typedef enum {
  PKT_DATA      = 0x01,  // Firmware bloğu
  PKT_ACK       = 0x02,  // Blok alındı
  PKT_NACK      = 0x03,  // Blok bozuk / CRC hatası
  PKT_REQ_BLOCK = 0x04,  // Eksik blok talebi
  PKT_DONE      = 0x05,  // Gönderici tüm imajı bitirdi
  PKT_COMPLETE  = 0x06,  // Alıcı imajı doğruladı (final)
} pkt_type_t;

typedef struct __attribute__((packed)) {
  uint8_t  type;          // pkt_type_t
  uint16_t block_no;      // 0..N-1
  uint16_t total_blocks;  // N
  uint8_t  payload_len;   // 0..64
  uint8_t  data[64];      // Firmware verisi
  uint16_t crc16;         // Başlık + veri üzerinden CRC16
} ota_packet_t;
```

---

## 🔄 `ota-metadata` State Machine

Hocanın hazır verdiği `ota-metadata` API'ı üzerine inşa edildi:

```
   EMPTY ──[veri akışı başla]──► DOWNLOADING ──[CRC OK]──► VERIFIED
                                                              │
                                                  [stage çağrısı]
                                                              ▼
   INVALID ◄──[boot başarısız]── CONFIRMED ◄──[reboot OK]── PENDING
```

Cooja simülasyonunda gerçek reboot yapılmadığı için akış `PENDING` durumuna kadar gider; gerçek cihazda `CONFIRMED` geçişini bootloader yapar.

---

## 🚀 Build & Çalıştırma

### Gereksinimler
- WSL2 + Ubuntu 24.04 (Windows için) veya yerel Linux
- Docker
- Contiki-NG'nin `contiker/contiki-ng:latest` imajı

### Adımlar

```bash
# 1) Contiki-NG container'ını başlat
docker run --rm -it -v $(pwd):/home/user/contiki-ng \
  contiker/contiki-ng:latest bash

# 2) rpl-udp dizinine geç ve Z1 için derle
cd /home/user/contiki-ng/examples/rpl-udp
make TARGET=z1                  # ~7 saniye

# Üretilen ELF'ler:
#   udp-server.z1   (alıcı)
#   udp-client.z1   (gönderici)
```

### Cooja Simülasyonu

```bash
# Container içinde Cooja'yı başlat
cd /home/user/contiki-ng/tools/cooja
./gradlew run

# Cooja açılınca:
#   File → Open Simulation
#   → BIL304-OS-Project-1.csc dosyasını seç
#   → Start
```

### Beklenen Log Çıktısı

```
[INFO: App] OTA gonderici basladi (node 2)
[INFO: App] Blok 0/128 gonderiliyor (64 bayt)
[INFO: App] ACK alindi: blok 0
[INFO: App] Blok 1/128 gonderiliyor (64 bayt)
...
[INFO: App] Tum bloklar gonderildi, DONE gonderiliyor
[INFO: App] OTA alici: tum bloklar alindi
[INFO: App] CRC32 dogrulamasi: 0x66B06BEA == 0x66B06BEA OK
[INFO: App] Slot B PENDING olarak isaretlendi
[INFO: App] Yuklenmeye hazir yeni firmware alimi tamamlandi.
```

Tam başarı log'u: [`cooja-mote-output.txt`](cooja-mote-output.txt)

---

## 📂 Dizin Yapısı

```
examples/rpl-udp/
├── README.md                       ← Bu dosya
├── BIL304-OS-Project-1.csc         ← Cooja senaryosu (3 düğüm)
├── Makefile                        ← PROJECT_SOURCEFILES += ota-metadata.c
│
├── udp-client.c                    ← Gönderici (Node 2, ID:2)
├── udp-server.c                    ← Alıcı (Node 1, ID:1, DAG root)
├── firmware-packet.h               ← OTA paket formatı tanımı
├── firmware_data.h                 ← 8 KB firmware verisi (hex array)
│
├── ota-metadata.h                  ← (Hocanın verdiği) State machine API
├── ota-metadata.c                  ← (Hocanın verdiği) Implementasyon
│
├── new-firmware-8k.bin             ← 8 KB'a kırpılmış aktarım imajı
├── udp-client.z1                   ← Derlenmiş gönderici ELF
├── udp-server.z1                   ← Derlenmiş alıcı ELF
│
├── cooja-mote-output.txt           ← Başarılı aktarım log'u (496 satır)
└── slot-a.ld, slot-b.ld            ← (CC1352R için, MSP430'da kullanılmaz)
```

---

## 🛡 Güvenilirlik Mekanizmaları

| Mekanizma | Nerede | Amaç |
|---|---|---|
| **Stop-and-Wait ARQ** | Gönderici | Her blok için ACK beklenir, kayıp blok yeniden gönderilir |
| **CRC16 (paket bazlı)** | Hem gönderici hem alıcı | Tek paket bozulmasını yakalamak için |
| **CRC32 (imaj bazlı)** | Alıcı (son aşama) | Tüm imajın bütünlüğünü onaylamak için |
| **Block bitmap** | Alıcı | Hangi blokların geldiğini takip etmek için |
| **Timeout + Retry** | Gönderici | ACK gelmezse aynı bloğu en fazla 5 kez tekrar gönderir |
| **Slot A/B ayrımı** | `ota-metadata` | Yeni imaj eskisini bozmadan ayrı slot'a yazılır |

---

## 📸 Ekran Görüntüleri

| Görüntü | Açıklama |
|---|---|
| Cooja network topolojisi | 3 düğüm + RPL DAG bağlantıları |
| Akış sırasında log | Blok N gönderim/ACK akışı |
| Başarı mesajı | "Yüklenmeye hazır yeni firmware alımı tamamlandı" + CRC32 |

> Ekran görüntüleri proje deposunun ana kökündeki `screenshots/` klasöründedir.

---

## 📚 Kaynaklar

- [Contiki-NG Wiki](https://github.com/contiki-ng/contiki-ng/wiki)
- [Z1 (MSP430) platform referansı](https://github.com/contiki-ng/contiki-ng/wiki/Platform-zoul)
- [RFC 4944 — IPv6 over IEEE 802.15.4](https://datatracker.ietf.org/doc/html/rfc4944)
- [RFC 6550 — RPL](https://datatracker.ietf.org/doc/html/rfc6550)

---

## 📝 Lisans

Bu proje, Contiki-NG'nin lisansı (3-Clause BSD) altında yayınlanmıştır. Akademik kullanım içindir.
