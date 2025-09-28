#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>

// Nordic UART Service UUIDs (NUS)
static const char *NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // Write / WriteNR
static const char *NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // Notify

static NimBLEAdvertising *adv;
static NimBLEAdvertisementData advAdv;        // constant ADV
static NimBLEAdvertisementData advScan;       // rotating Scan Response
static NimBLECharacteristic *txChr = nullptr; // for notifications

// --- rotation period: 1 minute ---
static const uint32_t SLOT_MS = 60 * 1000; // 60000 ms
static uint32_t last_tick = 0;

// Rotating bytes (phone will replace by writing to RX)
static std::vector<uint8_t> g_payloads = {0x0A, 0x0B, 0x0C, 0x0D, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04};
static size_t g_idx = 0;

static void setScanRespMfgByte(uint8_t b)
{
  std::string m;
  m.push_back((char)0xFF);
  m.push_back((char)0xFF);
  m.push_back((char)b);
  advScan = NimBLEAdvertisementData();
  advScan.setManufacturerData(m);
  adv->setScanResponseData(advScan);
}

// --- in RxWriteCB ---
class RxWriteCB : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
  {
    const std::string &v = c->getValue();
    if (v.empty())
      return;

    const size_t MAX = 64;
    g_payloads.assign(v.begin(), v.begin() + (v.size() > MAX ? MAX : v.size()));
    g_idx = 0;
    setScanRespMfgByte(g_payloads[g_idx]);
    Serial.printf("[ADV] SR byte: 0x%02X\n", g_payloads[g_idx]);

    // reset the 1-minute timer so the next change happens one minute from now
    last_tick = millis();

    if (txChr)
    {
      char msg[64];
      int n = snprintf(msg, sizeof(msg), "OK %u bytes", (unsigned)g_payloads.size());
      txChr->setValue((uint8_t *)msg, (size_t)n);
      txChr->notify();
    }

    Serial.printf("[NUS] RX wrote %u bytes; new list size=%u\n",
                  (unsigned)v.size(), (unsigned)g_payloads.size());
  }
};

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("ESP32C6 + NUS (write RX, notify TX), rotating Scan Response (1-minute slots)");

  NimBLEDevice::init("ESP32C6-Beacon");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // GATT server with NUS
  auto *server = NimBLEDevice::createServer();
  auto *svc = server->createService(NUS_SVC);

  // RX: phone writes here (hex payload)
  auto *rx = svc->createCharacteristic(
      NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ);
  rx->setCallbacks(new RxWriteCB());
  rx->setValue(std::string((const char *)g_payloads.data(), g_payloads.size()));

  // TX: notify confirmations
  txChr = svc->createCharacteristic(
      NUS_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

  svc->start(); // start service before advertising

  // Advertising (keep constant); rotate byte via Scan Response only
  adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SVC);

  advAdv = NimBLEAdvertisementData();
  advAdv.setFlags(0x06);
  advAdv.setName("ESP32C6-Beacon");
  adv->setAdvertisementData(advAdv);

  // initial byte
  setScanRespMfgByte(g_payloads[g_idx]);
  adv->start();

  last_tick = millis();
  Serial.println("[ADV] started");
  Serial.printf("[ADV] SR byte: 0x%02X\n", g_payloads[g_idx]);
}

void loop()
{
  const uint32_t now = millis();

  // advance exactly once per SLOT_MS
  if ((uint32_t)(now - last_tick) >= SLOT_MS)
  {
    last_tick = now;
    g_idx = (g_idx + 1) % g_payloads.size();
    setScanRespMfgByte(g_payloads[g_idx]);
    Serial.printf("[ADV] SR byte (1m slot): 0x%02X\n", g_payloads[g_idx]);
  }

  // keep loop light so BLE stays responsive
  delay(5);
}
