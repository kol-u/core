#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <stdint.h>

// Nordic UART Service UUIDs (NUS)
static const char *NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // Write / WriteNR
static const char *NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // Notify

static NimBLEAdvertising *adv;
static NimBLEAdvertisementData advAdv;        // constant ADV
static NimBLEAdvertisementData advScan;       // rotating Scan Response
static NimBLECharacteristic *txChr = nullptr; // for notifications

// --- rotation period (change for testing) ---
static const uint32_t SLOT_MS = 60 * 1000; // 60000 ms
static uint32_t last_tick = 0;

// ----- 12 generated codes (one 32-bit word per slot) -----
static const uint32_t CODES[12] = {
    0xf952b74b, 0xd7627cff, 0x1c76ed95, 0xff3b5f16,
    0x1ba623ca, 0xc3f960d3, 0x3bb3dc02, 0x43ec57bd,
    0x165c3a72, 0xd611370d, 0xbc51cc84, 0xf4a4c80b};
static size_t g_idx = 0;

// ---------- tiny CRC8 (Dallas/Maxim poly 0x31) ----------
static uint8_t crc8_dm(const uint8_t *p, size_t n)
{
  uint8_t crc = 0x00;
  for (size_t i = 0; i < n; ++i)
  {
    uint8_t inbyte = p[i];
    for (uint8_t j = 0; j < 8; ++j)
    {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix)
        crc ^= 0x8C; // 0x31 reversed
      inbyte >>= 1;
    }
  }
  return crc;
}

// Build SR: [CompanyID 0xFFFF][ver=1][idx][word_be(4)][crc8]
static void setScanRespChunk(uint8_t idx, uint32_t word_be)
{
  uint8_t payload[1 + 1 + 4];          // ver + idx + word
  payload[0] = 1;                      // protocol/version
  payload[1] = idx & 0xFF;             // index 0..11
  payload[2] = (word_be >> 24) & 0xFF; // MSB first (big-endian)
  payload[3] = (word_be >> 16) & 0xFF;
  payload[4] = (word_be >> 8) & 0xFF;
  payload[5] = (word_be >> 0) & 0xFF;

  uint8_t crc = crc8_dm(payload, sizeof(payload));

  std::string m;
  m.push_back((char)0xFF); // Company ID LSB (0xFFFF)
  m.push_back((char)0xFF); // Company ID MSB
  m.append((const char *)payload, sizeof(payload));
  m.push_back((char)crc);

  advScan = NimBLEAdvertisementData();
  advScan.setManufacturerData(m);
  adv->setScanResponseData(advScan);

  // pretty log
  Serial.printf("[ADV] SR idx=%u word=0x%08X crc8=0x%02X\n", idx, (unsigned)word_be, crc);
}

// --- in RxWriteCB: keep it (optional), but now treat incoming bytes as new codebook (multiple of 4)
class RxWriteCB : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
  {
    const std::string &v = c->getValue();
    if (v.empty())
      return;

    // interpret as raw bytes; take up to 12 words (48 bytes)
    size_t words = v.size() / 4;
    if (words > 12)
      words = 12;

    if (words > 0)
    {
      for (size_t i = 0; i < words; ++i)
      {
        uint32_t w = ((uint8_t)v[4 * i + 0] << 0) |
                     ((uint8_t)v[4 * i + 1] << 8) |
                     ((uint8_t)v[4 * i + 2] << 16) |
                     ((uint8_t)v[4 * i + 3] << 24);
        // assume incoming is little-endian; store as-is then send big-endian
        ((uint32_t *)CODES)[i] = w;
      }
      // zero any remaining
      for (size_t i = words; i < 12; ++i)
        ((uint32_t *)CODES)[i] = 0;
      g_idx = 0;
      last_tick = millis();
      setScanRespChunk((uint8_t)g_idx, CODES[g_idx]);

      if (txChr)
      {
        char msg[64];
        int n = snprintf(msg, sizeof(msg), "OK %u bytes (%u words)", (unsigned)v.size(), (unsigned)words);
        txChr->setValue((uint8_t *)msg, (size_t)n);
        txChr->notify();
      }
      Serial.printf("[NUS] RX wrote %u bytes; loaded %u words\n",
                    (unsigned)v.size(), (unsigned)words);
    }
  }
};

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("ESP32C6 + NUS (write RX, notify TX), rotating Scan Response (1-word per slot)");

  NimBLEDevice::init("ESP32C6-Beacon");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // GATT server with NUS
  auto *server = NimBLEDevice::createServer();
  auto *svc = server->createService(NUS_SVC);

  // RX: phone writes here (optional update of the 12 words)
  auto *rx = svc->createCharacteristic(
      NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ);
  rx->setCallbacks(new RxWriteCB());

  // TX: notify confirmations
  txChr = svc->createCharacteristic(
      NUS_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

  svc->start(); // start service before advertising

  // Advertising (keep constant); rotate chunk via Scan Response only
  adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SVC);

  advAdv = NimBLEAdvertisementData();
  advAdv.setFlags(0x06);
  advAdv.setName("ESP32C6-Beacon"); // remove if you want more ADV space
  adv->setAdvertisementData(advAdv);

  // initial chunk
  setScanRespChunk((uint8_t)g_idx, CODES[g_idx]);
  adv->start();

  last_tick = millis();
  Serial.println("[ADV] started");
}

void loop()
{
  const uint32_t now = millis();

  // advance exactly once per SLOT_MS
  if ((uint32_t)(now - last_tick) >= SLOT_MS)
  {
    last_tick = now;
    g_idx = (g_idx + 1) % 12;
    setScanRespChunk((uint8_t)g_idx, CODES[g_idx]);
  }

  delay(5);
}
