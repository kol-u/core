/*
 * ESP32-C6 SuperMini — NimBLE scanner (FILTER 0xFFFF, hex-only payload)
 */

#include <Arduino.h>
#include <NimBLEDevice.h>

static constexpr uint32_t SCAN_TIME_MS = 30 * 1000; // 30s
static constexpr uint16_t OUR_COMPANY_ID = 0xFFFF;  // filter target

static void printHex(const std::string &s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        uint8_t b = static_cast<uint8_t>(s[i]);
        Serial.printf("%02X", b);
        if (i + 1 < s.size())
            Serial.print(' ');
    }
}

static bool parseSlotValuePayload(const std::string &payload,
                                  uint8_t &ver, uint8_t &slot, uint32_t &value)
{
    if (payload.size() < 6)
        return false;
    ver = static_cast<uint8_t>(payload[0]);
    slot = static_cast<uint8_t>(payload[1]);
    value = (uint32_t)(uint8_t)payload[2] | ((uint32_t)(uint8_t)payload[3] << 8) | ((uint32_t)(uint8_t)payload[4] << 16) | ((uint32_t)(uint8_t)payload[5] << 24);
    return true;
}

class ScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *d) override
    {
        if (!d->haveManufacturerData())
            return;

        const std::string &m = d->getManufacturerData();
        if (m.size() < 2)
            return;

        uint16_t comp = (uint8_t)m[0] | ((uint16_t)(uint8_t)m[1] << 8);
        if (comp != OUR_COMPANY_ID)
            return;

        Serial.println(">>> OUR BEACON <<<");
        Serial.printf("Addr:   %s  RSSI: %d dBm\n",
                      d->getAddress().toString().c_str(), d->getRSSI());

        Serial.print("MFG:    ");
        printHex(m);
        Serial.println();

        // Payload (after company ID) — print hex only
        std::string payload = (m.size() > 2) ? std::string(m.begin() + 2, m.end()) : std::string();
        Serial.print("Data:   ");
        printHex(payload);
        Serial.println();

        // Optional: also decode [ver,slot,value] if present (kept for convenience)
        uint8_t ver = 0, slot = 0;
        uint32_t val = 0;
        if (parseSlotValuePayload(payload, ver, slot, val))
        {
            Serial.printf("Parsed: ver=%u slot=%u value=%u\n", ver, slot, val);
        }

        Serial.println("------------------");
    }

    void onScanEnd(const NimBLEScanResults & /*results*/, int reason) override
    {
        Serial.printf("Scan ended (reason=%d). Restarting...\n", reason);
        NimBLEDevice::getScan()->start(SCAN_TIME_MS, false, true);
    }
} scanCB;

void setup()
{
    Serial.begin(115200);
    delay(400);

    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCB, false);
    scan->setActiveScan(true);
    scan->setMaxResults(0);
    scan->start(SCAN_TIME_MS, false, true);

    Serial.println("Scanning (filter: CompanyID 0xFFFF, hex-only payload)...");
}

void loop() {}