#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>

// Nordic UART Service UUIDs (NUS)
static const char *NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // Write / WriteNR
static const char *NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // Notify

static NimBLEAdvertising *adv;
static NimBLEAdvertisementData advAdv;  // constant ADV
static NimBLEAdvertisementData advScan; // rotating Scan Response

static NimBLECharacteristic *txChr = nullptr; // for notifications

// Rotating bytes (phone will replace by writing to RX)
static std::vector<uint8_t> g_payloads = {0x0A, 0x0B, 0x0C, 0x0D, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04};
static size_t g_idx = 0;

static void setScanRespMfgByte(uint8_t b)
{
    // Manufacturer data: 0xFFFF + 1-byte payload
    std::string m;
    m.push_back((char)0xFF);
    m.push_back((char)0xFF);
    m.push_back((char)b);
    advScan = NimBLEAdvertisementData();
    advScan.setManufacturerData(m);
    // Update scan response on the fly (no stop/start)
    adv->setScanResponseData(advScan);
}

class RxWriteCB : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
    {
        const std::string &v = c->getValue();
        if (v.empty())
            return;

        // Clamp to a sane length
        const size_t MAX = 64;
        g_payloads.assign(v.begin(), v.begin() + (v.size() > MAX ? MAX : v.size()));
        g_idx = 0;
        setScanRespMfgByte(g_payloads[g_idx]);

        // Send a TX notification so the app sees a response
        if (txChr)
        {
            char msg[64];
            int n = snprintf(msg, sizeof(msg), "OK %u bytes", (unsigned)g_payloads.size());
            txChr->setValue((uint8_t *)msg, (size_t)n);
            txChr->notify(); // will work only if phone subscribed
        }

        Serial.printf("[NUS] RX wrote %u bytes; new list size=%u\n",
                      (unsigned)v.size(), (unsigned)g_payloads.size());
    }
};

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("ESP32C6 + NUS (write RX, notify TX), rotating Scan Response");

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

    // TX: we notify here so the app can subscribe and see confirmations
    txChr = svc->createCharacteristic(
        NUS_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

    svc->start(); // start service before advertising

    // Advertising: keep constant; rotate byte via Scan Response only
    adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SVC);

    advAdv = NimBLEAdvertisementData();
    advAdv.setFlags(0x06);
    advAdv.setName("ESP32C6-Beacon");
    adv->setAdvertisementData(advAdv);

    setScanRespMfgByte(g_payloads[g_idx]);
    adv->start();

    Serial.println("[ADV] started");
}

void loop()
{
    g_idx = (g_idx + 1) % g_payloads.size();
    setScanRespMfgByte(g_payloads[g_idx]);
    Serial.printf("[ADV] SR byte: 0x%02X\n", g_payloads[g_idx]);
    delay(1000);
}
