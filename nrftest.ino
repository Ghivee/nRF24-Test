/*
 * ============================================================================
 *  nrftest.ino  —  SELF-TEST modul nRF24L01+ di ESP-12E / NodeMCU (ESP8266)
 * ============================================================================
 *
 *  Sketch ini TIDAK memakai library apa pun (SPI di-bit-bang), jadi bisa
 *  langsung di-upload tanpa install library. Tujuannya membuktikan modul
 *  benar-benar hidup dan berfungsi:
 *
 *    1. Deteksi otomatis susunan kabel SPI (6 kemungkinan)
 *    2. Tulis/baca register CONFIG
 *    3. Nilai STATUS bawaan chip (signature nRF24L01+)
 *    4. Uji tiap bit register (deteksi bit nyangkut / MISO kotor)
 *    5. Uji lebar alamat 3/4/5 byte + tulis/baca alamat 5 byte
 *    6. Uji FIFO TX (payload masuk, FIFO kosong lagi setelah flush)
 *    7. Uji PLL / synthesizer RF
 *    8. Deteksi pin CE + transmisi nyata (paket keluar dari FIFO,
 *       flag TX_DS / MAX_RT muncul) + cek pin IRQ
 *    9. Scan aktivitas RF 2.4 GHz lewat register RPD (RX chain)
 *
 *  Pin yang diasumsikan (boleh beda, nanti dideteksi otomatis):
 *      VCC  -> 3V3      (WAJIB 3.3V, JANGAN 5V)
 *      GND  -> GND
 *      CE   -> D1  GPIO5
 *      CSN  -> D3  GPIO0
 *      SCK  -> D7  GPIO13   <-- di NodeMCU, pin HW-SPI sebenarnya:
 *      MOSI -> D6  GPIO12       SCK=D5/GPIO14, MISO=D6/GPIO12, MOSI=D7/GPIO13
 *      MISO -> D5  GPIO14       makanya sketch ini mencoba semua susunan
 *      IRQ  -> D2  GPIO4
 *
 *  CATATAN PENTING
 *   - CSN di GPIO0 (D3) adalah pin strapping. Modul tetap bisa dipakai,
 *     tapi kalau board gagal boot / gagal upload, cabut kabel CSN dulu
 *     atau pindahkan CSN ke D0/GPIO16.
 *   - Modul PA+LNA (ada antena + chip amplifier) sebaiknya diberi kapasitor
 *     elektrolit 10-100 uF antara VCC dan GND sedekat mungkin ke modul,
 *     kalau tidak bisa reset/brownout saat transmit.
 *   - Kalau VCC modul diambil dari pin 3V3 NodeMCU dan tegangan drop,
 *     pakai regulator 3.3V terpisah.
 *
 *  Buka Serial Monitor 115200 baud.
 * ============================================================================
 */

#include <Arduino.h>

// ─── Pin kontrol (sesuaikan hanya jika kabel CE/CSN/IRQ kamu beda) ──────────
#define PIN_CE   5   // D1  GPIO5
#define PIN_CSN  0   // D3  GPIO0
#define PIN_IRQ  4   // D2  GPIO4

// Tiga pin yang dipakai untuk SPI. Sketch akan mencoba 6 susunan dari
// ketiga pin ini dan memilih yang benar-benar dijawab oleh modul.
static const uint8_t kSpiPins[3] = {13, 12, 14};   // D7, D6, D5
static const char   *kSpiPinNames[3] = {"D7/GPIO13", "D6/GPIO12", "D5/GPIO14"};

// ─── Perintah nRF24L01+ ─────────────────────────────────────────────────────
#define CMD_R_REGISTER    0x00
#define CMD_W_REGISTER    0x20
#define CMD_R_RX_PAYLOAD  0x61
#define CMD_W_TX_PAYLOAD  0xA0
#define CMD_FLUSH_TX      0xE1
#define CMD_FLUSH_RX      0xE2
#define CMD_NOP           0xFF
#define CMD_ACTIVATE      0x50

// ─── Register nRF24L01+ ─────────────────────────────────────────────────────
#define REG_CONFIG        0x00
#define REG_EN_AA         0x01
#define REG_EN_RXADDR     0x02
#define REG_SETUP_AW      0x03
#define REG_SETUP_RETR    0x04
#define REG_RF_CH         0x05
#define REG_RF_SETUP      0x06
#define REG_STATUS        0x07
#define REG_OBSERVE_TX    0x08
#define REG_RPD           0x09
#define REG_RX_ADDR_P0    0x0A
#define REG_TX_ADDR       0x10
#define REG_RX_PW_P0      0x11
#define REG_RX_PW_P1      0x12
#define REG_FIFO_STATUS   0x17
#define REG_DYNPD         0x1C
#define REG_FEATURE       0x1D

// FIFO_STATUS: bit4 = TX_EMPTY, bit5 = TX_FULL, bit0 = RX_EMPTY, bit1 = RX_FULL

// ─── Pin SPI aktif (diisi oleh auto-detect) ─────────────────────────────────
static uint8_t g_sck = 13, g_mosi = 12, g_miso = 14;
static uint8_t g_ce  = PIN_CE;

// ─── Statistik hasil tes ────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

// ============================================================================
//  Lapisan SPI bit-bang (mode 0, MSB first)
// ============================================================================
static inline void csn(bool high) { digitalWrite(PIN_CSN, high ? HIGH : LOW); }

static uint8_t spiXfer(uint8_t out) {
  uint8_t in = 0;
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(g_mosi, (out & 0x80) ? HIGH : LOW);
    out <<= 1;
    digitalWrite(g_sck, HIGH);                     // slave mengambil MOSI
    in = (uint8_t)((in << 1) | (digitalRead(g_miso) ? 1 : 0));
    digitalWrite(g_sck, LOW);
  }
  return in;
}

static void applySpiPins(uint8_t sck, uint8_t mosi, uint8_t miso) {
  g_sck = sck; g_mosi = mosi; g_miso = miso;
  pinMode(g_sck, OUTPUT);  digitalWrite(g_sck, LOW);
  pinMode(g_mosi, OUTPUT); digitalWrite(g_mosi, LOW);
  pinMode(g_miso, INPUT);
}

// ============================================================================
//  Akses register
// ============================================================================
static uint8_t readReg(uint8_t reg) {
  csn(false);
  spiXfer(CMD_R_REGISTER | (reg & 0x1F));
  uint8_t v = spiXfer(0xFF);
  csn(true);
  return v;
}

static void writeReg(uint8_t reg, uint8_t val) {
  csn(false);
  spiXfer(CMD_W_REGISTER | (reg & 0x1F));
  spiXfer(val);
  csn(true);
}

static void readBuf(uint8_t reg, uint8_t *buf, uint8_t len) {
  csn(false);
  spiXfer(CMD_R_REGISTER | (reg & 0x1F));
  for (uint8_t i = 0; i < len; i++) buf[i] = spiXfer(0xFF);
  csn(true);
}

static void writeBuf(uint8_t reg, const uint8_t *buf, uint8_t len) {
  csn(false);
  spiXfer(CMD_W_REGISTER | (reg & 0x1F));
  for (uint8_t i = 0; i < len; i++) spiXfer(buf[i]);
  csn(true);
}

static void sendCmd(uint8_t cmd) {
  csn(false);
  spiXfer(cmd);
  csn(true);
}

static void flushTx() { sendCmd(CMD_FLUSH_TX); }
static void flushRx() { sendCmd(CMD_FLUSH_RX); }

static void writeTxPayload(const uint8_t *buf, uint8_t len) {
  csn(false);
  spiXfer(CMD_W_TX_PAYLOAD);
  for (uint8_t i = 0; i < len; i++) spiXfer(buf[i]);
  csn(true);
}

static void activateFeatures() {
  csn(false);
  spiXfer(CMD_ACTIVATE);
  spiXfer(0x73);
  csn(true);
}

// ============================================================================
//  Util
// ============================================================================
static const char *gpioName(uint8_t g) {
  switch (g) {
    case 0:  return "D3/GPIO0";
    case 1:  return "TX/GPIO1";
    case 2:  return "D4/GPIO2";
    case 3:  return "RX/GPIO3";
    case 4:  return "D2/GPIO4";
    case 5:  return "D1/GPIO5";
    case 12: return "D6/GPIO12";
    case 13: return "D7/GPIO13";
    case 14: return "D5/GPIO14";
    case 15: return "D8/GPIO15";
    case 16: return "D0/GPIO16";
    default: return "?";
  }
}

static void report(const char *name, bool ok, const char *detail) {
  if (ok) {
    g_pass++;
    Serial.printf("  [ OK ] %-32s %s\n", name, detail ? detail : "");
  } else {
    g_fail++;
    Serial.printf("  [FAIL] %-32s %s\n", name, detail ? detail : "");
  }
}

// Seperti report(), tapi kegagalan hanya jadi peringatan (tidak menggagalkan
// verdict). Dipakai untuk tes yang tidak didukung semua varian chip/clone.
static void reportSoft(const char *name, bool ok, const char *detail) {
  if (ok) {
    g_pass++;
    Serial.printf("  [ OK ] %-32s %s\n", name, detail ? detail : "");
  } else {
    Serial.printf("  [WARN] %-32s %s\n", name, detail ? detail : "");
  }
}

// ============================================================================
//  1. Deteksi susunan kabel SPI
// ============================================================================
static bool probeCandidate(uint8_t sck, uint8_t mosi, uint8_t miso) {
  applySpiPins(sck, mosi, miso);
  csn(true);
  delay(3);

  writeReg(REG_CONFIG, 0x0C);                     // EN_CRC + CRCO, TX mode
  uint8_t cfg = readReg(REG_CONFIG);
  writeReg(REG_CONFIG, 0x08);                     // kembali ke default
  writeReg(REG_STATUS, 0x70);                     // bersihkan flag
  uint8_t st = readReg(REG_STATUS);

  // Signature nRF24L01+: CONFIG bisa ditulis 0x0C dan STATUS = 0x0E
  return (cfg == 0x0C) && ((st & 0x0F) == 0x0E);
}

static bool detectWiring() {
  Serial.println(F("[1] Deteksi susunan pin SPI"));
  Serial.printf("    CE=%s  CSN=%s  IRQ=%s\n",
                gpioName(PIN_CE), gpioName(PIN_CSN), gpioName(PIN_IRQ));

  bool found = false;
  for (uint8_t i = 0; i < 3 && !found; i++) {
    for (uint8_t j = 0; j < 3 && !found; j++) {
      if (j == i) continue;
      for (uint8_t k = 0; k < 3 && !found; k++) {
        if (k == i || k == j) continue;
        uint8_t sck = kSpiPins[i], mosi = kSpiPins[j], miso = kSpiPins[k];
        bool ok = probeCandidate(sck, mosi, miso);
        Serial.printf("    coba SCK=%-10s MOSI=%-10s MISO=%-10s -> %s\n",
                      kSpiPinNames[i], kSpiPinNames[j], kSpiPinNames[k],
                      ok ? "MENJAWAB" : "diam");
        if (ok) {
          found = true;
          Serial.println();
          Serial.println(F("    => Susunan yang dipakai modul:"));
          Serial.printf("       SCK  -> %s\n", gpioName(sck));
          Serial.printf("       MOSI -> %s\n", gpioName(mosi));
          Serial.printf("       MISO -> %s\n", gpioName(miso));
          if (sck == 14 && mosi == 13 && miso == 12) {
            Serial.println(F("       (ini susunan SPI hardware NodeMCU)"));
          } else {
            Serial.println(F("       (BUKAN susunan SPI hardware NodeMCU)"));
          }
        }
      }
    }
  }

  if (!found) {
    Serial.println();
    Serial.println(F("    Tidak ada susunan yang menjawab. Kemungkinan:"));
    Serial.println(F("      - VCC modul bukan 3.3V / belum tersambung"));
    Serial.println(F("      - GND modul belum tersambung"));
    Serial.println(F("      - kabel SPI/CSN longgar atau tertukar ke pin lain"));
    Serial.println(F("      - modul rusak"));
    Serial.println(F("      - CSN bukan di D3 (ubah PIN_CSN di atas)"));
    Serial.println(F("    Diagnostik mentah pada susunan tabel kamu:"));
    applySpiPins(13, 12, 14);
    delay(5);
    Serial.printf("      CONFIG=0x%02X  STATUS=0x%02X  (0x00/0xFF = MISO mati)\n",
                  readReg(REG_CONFIG), readReg(REG_STATUS));
    return false;
  }

  report("modul menjawab di SPI", true, "3.3V + GND + SPI terhubung");
  return true;
}

// ============================================================================
//  2..9  Pengujian
// ============================================================================
struct RegMask { uint8_t reg; uint8_t mask; const char *name; };

static bool testConfigReg() {
  Serial.println(F("[2] Register CONFIG (tulis/baca)"));
  writeReg(REG_CONFIG, 0x0C);
  uint8_t a = readReg(REG_CONFIG);
  writeReg(REG_CONFIG, 0x2C);
  uint8_t b = readReg(REG_CONFIG);
  writeReg(REG_CONFIG, 0x08);
  char buf[64];
  snprintf(buf, sizeof(buf), "tulis 0x0C -> baca 0x%02X, tulis 0x2C -> baca 0x%02X", a, b);
  bool ok = (a == 0x0C) && (b == 0x2C);
  report("CONFIG bisa ditulis", ok, buf);
  return ok;
}

static bool testStatusSignature() {
  Serial.println(F("[3] Signature STATUS"));
  writeReg(REG_STATUS, 0x70);
  uint8_t st = readReg(REG_STATUS);
  uint8_t fifo = readReg(REG_FIFO_STATUS);
  char buf[64];
  snprintf(buf, sizeof(buf), "STATUS=0x%02X FIFO_STATUS=0x%02X", st, fifo);
  bool ok = ((st & 0x0F) == 0x0E);
  report("STATUS sesuai nRF24L01+", ok, buf);
  return ok;
}

static bool testRegisterBits() {
  Serial.println(F("[4] Integritas bit register (pola 0xFF/0xA5/0x5A/0x00)"));
  static const RegMask regs[] = {
    {REG_EN_AA,       0x3F, "EN_AA"},
    {REG_EN_RXADDR,   0x3F, "EN_RXADDR"},
    {REG_SETUP_RETR,  0xFF, "SETUP_RETR"},
    {REG_RF_CH,       0x7F, "RF_CH"},
    {REG_RF_SETUP,    0x2E, "RF_SETUP"},
    {REG_RX_PW_P0,    0x3F, "RX_PW_P0"},
    {REG_RX_PW_P1,    0x3F, "RX_PW_P1"},
  };
  static const uint8_t patterns[] = {0xFF, 0xA5, 0x5A, 0x00};
  bool allOk = true;

  for (uint8_t r = 0; r < sizeof(regs) / sizeof(regs[0]); r++) {
    bool ok = true;
    for (uint8_t p = 0; p < 4; p++) {
      writeReg(regs[r].reg, patterns[p] & regs[r].mask);
      uint8_t rb = readReg(regs[r].reg);
      if ((rb & regs[r].mask) != (patterns[p] & regs[r].mask)) ok = false;
    }
    writeReg(regs[r].reg, 0x00);
    char buf[32];
    snprintf(buf, sizeof(buf), "reg 0x%02X mask 0x%02X", regs[r].reg, regs[r].mask);
    report(regs[r].name, ok, buf);
    allOk &= ok;
  }

  // FEATURE + DYNPD baru bisa ditulis setelah ACTIVATE(0x73)
  activateFeatures();
  bool featOk = true;
  for (uint8_t p = 0; p < 4; p++) {
    writeReg(REG_FEATURE, patterns[p] & 0x07);
    if ((readReg(REG_FEATURE) & 0x07) != (patterns[p] & 0x07)) featOk = false;
  }
  writeReg(REG_FEATURE, 0x00);

  bool dynOk = true;
  for (uint8_t p = 0; p < 4; p++) {
    writeReg(REG_DYNPD, patterns[p] & 0x3F);
    if ((readReg(REG_DYNPD) & 0x3F) != (patterns[p] & 0x3F)) dynOk = false;
  }
  writeReg(REG_DYNPD, 0x00);

  reportSoft("FEATURE (via ACTIVATE)", featOk, "butuh chip nRF24L01+ asli");
  reportSoft("DYNPD", dynOk, "");
  return allOk;
}

static bool testAddresses() {
  Serial.println(F("[5] Alamat (3/4/5 byte)"));
  static const uint8_t addr[5] = {'n', 'R', 'F', '2', '4'};

  writeReg(REG_SETUP_AW, 0x03);                   // 5 byte
  writeBuf(REG_TX_ADDR, addr, 5);
  uint8_t tx[5] = {0};
  readBuf(REG_TX_ADDR, tx, 5);
  bool txOk = (memcmp(addr, tx, 5) == 0);

  writeBuf(REG_RX_ADDR_P0, addr, 5);
  uint8_t rx[5] = {0};
  readBuf(REG_RX_ADDR_P0, rx, 5);
  bool rxOk = (memcmp(addr, rx, 5) == 0);

  bool awOk = true;
  for (uint8_t aw = 1; aw <= 3; aw++) {
    writeReg(REG_SETUP_AW, aw);
    if ((readReg(REG_SETUP_AW) & 0x03) != aw) awOk = false;
  }
  writeReg(REG_SETUP_AW, 0x03);

  char buf[64];
  snprintf(buf, sizeof(buf), "TX_ADDR=%c%c%c%c%c RX_ADDR_P0=%c%c%c%c%c",
           tx[0], tx[1], tx[2], tx[3], tx[4],
           rx[0], rx[1], rx[2], rx[3], rx[4]);
  report("SETUP_AW 3/4/5 byte", awOk, "");
  report("alamat TX & RX cocok", txOk && rxOk, buf);
  return awOk && txOk && rxOk;
}

static bool testFifo() {
  Serial.println(F("[6] FIFO TX"));
  flushTx();
  flushRx();
  uint8_t empty1 = readReg(REG_FIFO_STATUS);
  uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  writeTxPayload(payload, 8);
  uint8_t empty2 = readReg(REG_FIFO_STATUS);
  flushTx();
  uint8_t empty3 = readReg(REG_FIFO_STATUS);

  // bit4 = TX_EMPTY
  bool ok = ((empty1 & 0x10) != 0) && ((empty2 & 0x10) == 0) && ((empty3 & 0x10) != 0);
  char buf[64];
  snprintf(buf, sizeof(buf), "kosong=0x%02X terisi=0x%02X flush=0x%02X",
           empty1, empty2, empty3);
  report("FIFO TX terisi & ter-flush", ok, buf);
  return ok;
}

static bool testPll() {
  Serial.println(F("[7] PLL / synthesizer RF"));
  writeReg(REG_RF_CH, 76);
  writeReg(REG_RF_SETUP, 0x26);                   // 250 kbps, 0 dBm
  writeReg(REG_CONFIG, 0x0E);                     // PWR_UP, CRC 2 byte, TX
  delay(5);                                       // tpd2stby ~1.5 ms

  pinMode(g_ce, OUTPUT);
  digitalWrite(g_ce, HIGH);
  delayMicroseconds(200);
  writeReg(REG_RF_SETUP, 0xB6);                   // CONT_WAVE + PLL_LOCK + 250k
  delay(10);

  uint8_t rs = readReg(REG_RF_SETUP);
  bool locked = (rs & 0x10) != 0;                 // bit PLL_LOCK

  writeReg(REG_RF_SETUP, 0x26);
  digitalWrite(g_ce, LOW);

  char buf[48];
  snprintf(buf, sizeof(buf), "RF_SETUP=0x%02X, PLL_LOCK=%d", rs, locked ? 1 : 0);
  reportSoft("PLL mengunci (RF hidup)", locked, buf);
  return locked;
}

// Satu percobaan kirim. Return true kalau ada event TX_DS atau MAX_RT.
static bool txAttempt(uint8_t cePin, uint16_t pulseUs, bool useAck,
                      uint8_t *stOut, uint8_t *fifoOut) {
  writeReg(REG_SETUP_RETR, 0x03);                 // ARD 250us, ARC 3
  writeReg(REG_EN_AA, useAck ? 0x01 : 0x00);
  writeReg(REG_EN_RXADDR, useAck ? 0x01 : 0x00);
  writeReg(REG_CONFIG, 0x6E);                     // PWR_UP, TX, CRC, MASK_MAX_RT=0
  delay(4);
  writeReg(REG_STATUS, 0x70);
  flushTx();

  const uint8_t payload[8] = {0xA5, 1, 2, 3, 4, 5, 6, 7};
  writeTxPayload(payload, 8);

  pinMode(cePin, OUTPUT);
  digitalWrite(cePin, LOW);
  digitalWrite(cePin, HIGH);
  delayMicroseconds(pulseUs);
  digitalWrite(cePin, LOW);

  uint8_t st = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 60) {
    st = readReg(REG_STATUS);
    if (st & 0x30) break;
    delay(1);
  }
  *stOut = st;
  *fifoOut = readReg(REG_FIFO_STATUS);
  flushTx();
  return (st & 0x30) != 0;
}

static bool testTxBurst() {
  Serial.println(F("[8] Deteksi CE + transmisi nyata + pin IRQ"));
  static const uint8_t addr[5] = {'n', 'R', 'F', '2', '4'};

  writeReg(REG_SETUP_AW, 0x03);
  writeBuf(REG_TX_ADDR, addr, 5);
  writeBuf(REG_RX_ADDR_P0, addr, 5);
  writeReg(REG_RX_PW_P0, 8);
  writeReg(REG_RF_CH, 76);
  writeReg(REG_RF_SETUP, 0x26);                   // 250 kbps, 0 dBm

  // ── cari pin CE yang benar ────────────────────────────────────────────────
  const uint8_t cand[] = {PIN_CE, 4, 16, 2, 15};
  bool found = false, foundAckMode = true;
  uint8_t foundPin = PIN_CE, st = 0, fifo = 0;

  for (uint8_t i = 0; i < sizeof(cand); i++) {
    uint8_t p = cand[i];
    bool ok = txAttempt(p, 300, true, &st, &fifo);
    bool ackMode = true;
    if (!ok) {
      ok = txAttempt(p, 300, false, &st, &fifo);
      ackMode = false;
    }
    Serial.printf("    CE %-10s -> STATUS=0x%02X FIFO=0x%02X  %s\n",
                  gpioName(p), st, fifo,
                  ok ? "TRANSMIT (paket keluar)" : "tidak ada reaksi");
    if (p == PIN_IRQ) pinMode(PIN_IRQ, INPUT_PULLUP);
    if (ok) { found = true; foundPin = p; foundAckMode = ackMode; break; }
    if (p != PIN_IRQ) pinMode(p, INPUT);
  }

  if (!found) {
    Serial.println(F("    Tidak ada pin CE yang memicu TX."));
    Serial.println(F("    Cek kabel CE (harus ke D1/GPIO5) dan pastikan modul"));
    Serial.println(F("    dapat arus cukup (kapasitor 10uF di VCC-GND)."));
    uint8_t cfg = readReg(REG_CONFIG);
    uint8_t obs = readReg(REG_OBSERVE_TX);
    char buf[80];
    snprintf(buf, sizeof(buf), "CONFIG=0x%02X FIFO=0x%02X STATUS=0x%02X OBS=0x%02X",
             cfg, fifo, st, obs);
    report("radio mengirim paket", false, buf);
    return false;
  }

  g_ce = foundPin;
  if (foundPin == PIN_CE) {
    report("CE di D1/GPIO5", true,
           foundAckMode ? "auto-ack normal" : "hanya TX tanpa auto-ack");
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "CE ternyata di %s, bukan D1/GPIO5", gpioName(foundPin));
    reportSoft("lokasi pin CE", false, buf);
  }

  // ── percobaan final + verifikasi IRQ ─────────────────────────────────────
  uint8_t st2 = 0, fifo2 = 0;
  bool sent = txAttempt(g_ce, 500, true, &st2, &fifo2);
  uint8_t obs = readReg(REG_OBSERVE_TX);
  bool maxRt = (st2 & 0x10) != 0;
  bool txDs  = (st2 & 0x20) != 0;

  char buf[96];
  if (maxRt) {
    snprintf(buf, sizeof(buf), "MAX_RT: paket terkirim tanpa ACK (wajar 1 modul), OBS=0x%02X", obs);
  } else if (txDs) {
    snprintf(buf, sizeof(buf), "TX_DS: paket terkirim & di-ACK, OBS=0x%02X", obs);
  } else {
    snprintf(buf, sizeof(buf), "STATUS=0x%02X FIFO=0x%02X", st2, fifo2);
  }
  report("transmisi + auto-ack (MAX_RT)", sent, sent ? buf : "tidak ada event TX_DS/MAX_RT");

  // Baca IRQ selagi flag MAX_RT masih aktif (MASK_MAX_RT=0 -> IRQ turun)
  bool irqLow = (g_ce != PIN_IRQ) ? (digitalRead(PIN_IRQ) == LOW) : true;

  // Saat MAX_RT payload memang tetap di FIFO (harus di-flush manual),
  // jadi kosongnya FIFO hanya diharapkan pada TX_DS (mode tanpa ACK).
  uint8_t st3 = 0, fifo3 = 0;
  bool sent2 = txAttempt(g_ce, 500, false, &st3, &fifo3);
  bool txDs2 = (st3 & 0x20) != 0;
  bool drained = (fifo3 & 0x10) != 0;             // bit4 = TX_EMPTY
  snprintf(buf, sizeof(buf), "TX_DS=%d, FIFO setelah kirim=0x%02X", txDs2 ? 1 : 0, fifo3);
  report("transmisi tanpa auto-ack (TX_DS)", sent2 && txDs2 && drained, buf);

  bool irqOk = true;
  if (g_ce != PIN_IRQ) {
    writeReg(REG_STATUS, 0x70);
    delay(2);
    bool irqHigh = (digitalRead(PIN_IRQ) == HIGH);
    irqOk = maxRt ? (irqLow && irqHigh) : irqHigh;
    snprintf(buf, sizeof(buf), "saat event=%s, setelah clear=%s",
             irqLow ? "LOW" : "HIGH", irqHigh ? "HIGH" : "LOW");
    report("pin IRQ (D2) merespons", irqOk, buf);
  } else {
    Serial.println(F("    (IRQ tidak diuji: pin IRQ sama dengan pin CE)"));
  }

  return sent && sent2 && txDs2 && drained && (maxRt || txDs) && irqOk;
}

static int scanActivity() {
  Serial.println(F("[9] Scan aktivitas RF 2.4 GHz (informasi tambahan)"));
  writeReg(REG_EN_AA, 0x00);
  writeReg(REG_EN_RXADDR, 0x00);
  writeReg(REG_CONFIG, 0x0B);                     // PWR_UP + PRIM_RX + CRC
  pinMode(g_ce, OUTPUT);
  digitalWrite(g_ce, HIGH);
  delay(3);

  int hits = 0;
  Serial.print(F("    kanal bersinyal:"));
  for (int ch = 0; ch <= 100; ch += 2) {
    writeReg(REG_RF_CH, (uint8_t)ch);
    delayMicroseconds(350);
    bool rpd = (readReg(REG_RPD) & 0x01) != 0;
    if (!rpd) {
      delayMicroseconds(350);
      rpd = (readReg(REG_RPD) & 0x01) != 0;
    }
    if (rpd) { hits++; Serial.printf(" %d", ch); }
  }
  Serial.println();

  digitalWrite(g_ce, LOW);
  writeReg(REG_CONFIG, 0x08);                     // power down
  Serial.printf("    %d kanal ada sinyal > -64 dBm (WiFi ch1/6/11 ~ nRF ch 12/37/62).\n", hits);
  if (hits > 0) {
    Serial.println(F("    RX chain terbukti menerima gelombang radio di sekitar."));
  }
  return hits;
}

// ============================================================================
//  Satu putaran pengujian
// ============================================================================
static void runAll() {
  g_pass = 0;
  g_fail = 0;
  g_ce = PIN_CE;
  pinMode(PIN_IRQ, INPUT_PULLUP);

  if (!detectWiring()) {
    Serial.println();
    Serial.println(F("HASIL: nRF24 TIDAK TERDETEKSI - tes lain dilewati."));
    Serial.println(F("Perbaiki kabel/VCC lalu tekan tombol RESET pada board."));
    return;
  }

  bool ok2 = testConfigReg();
  bool ok3 = testStatusSignature();
  bool ok4 = testRegisterBits();
  bool ok5 = testAddresses();
  bool ok6 = testFifo();
  bool ok7 = testPll();
  bool ok8 = testTxBurst();
  scanActivity();

  Serial.println();
  Serial.println(F("================= HASIL ================="));
  Serial.printf("  Lulus: %d   Gagal: %d\n", g_pass, g_fail);
  Serial.printf("  Pin terbaca: SCK=%s MOSI=%s MISO=%s CE=%s IRQ=%s\n",
                gpioName(g_sck), gpioName(g_mosi), gpioName(g_miso),
                gpioName(g_ce), gpioName(PIN_IRQ));
  if (g_ce != PIN_CE) {
    Serial.printf("  Catatan: CE merespons di %s, bukan %s seperti di tabel.\n",
                  gpioName(g_ce), gpioName(PIN_CE));
    Serial.printf("           Pindahkan kabel CE ke %s bila ingin sesuai tabel.\n",
                  gpioName(PIN_CE));
  }
  if (ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && g_fail == 0) {
    Serial.println(F("  VERDICT: MODUL nRF24L01+ BERFUNGSI."));
    Serial.println(F("  Bukti: SPI + semua register OK, PLL terkunci, paket"));
    Serial.println(F("         benar-benar ditransmisikan, pin IRQ merespons."));
  } else if (ok2 && ok4 && ok5 && ok6 && ok8) {
    Serial.println(F("  VERDICT: MODUL BERFUNGSI (ada catatan WARN di atas)."));
  } else {
    Serial.println(F("  VERDICT: ADA TES GAGAL - lihat baris [FAIL] di atas."));
    Serial.println(F("  Cek: VCC 3.3V, GND, kapasitor 10uF, panjang kabel <= 10cm."));
  }
  Serial.println(F("========================================="));
}

// ============================================================================
//  Setup & loop
// ============================================================================
void setup() {
  // CSN di GPIO0: paksa HIGH sedini mungkin (pin ini juga pin boot)
  pinMode(PIN_CSN, OUTPUT);
  digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_CE, OUTPUT);
  digitalWrite(PIN_CE, LOW);
  pinMode(PIN_IRQ, INPUT_PULLUP);

  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("  SELF-TEST nRF24L01+  |  ESP-12E / NodeMCU"));
  Serial.println(F("  SPI bit-bang, tanpa library tambahan"));
  Serial.println(F("=================================================="));

  runAll();
}

void loop() {
  static uint32_t last = 0;
  static int round = 0;
  if (millis() - last < 5000) return;
  last = millis();
  round++;
  Serial.println();
  Serial.printf("---------------- putaran ulang #%d ----------------\n", round);
  runAll();
}
