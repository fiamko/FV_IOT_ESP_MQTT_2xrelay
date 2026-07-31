/*
 * ============================================================================
 * sensors.cpp — IMPLEMENTACE ČTENÍ SENZORŮ (DS18B20 + SCT013)
 * ============================================================================
 */

#include "variables.h"
#include "sensors.h"

// ============================================================================
// GLOBÁLNÍ OBJEKTY SENZORŮ
// ============================================================================

OneWire g_onewire(ONEWIRE_PIN);
DallasTemperature g_dallas(&g_onewire);

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

// Adresa nalezeného DS18B20 čidla (8 bajtů)
static DeviceAddress s_ds18b20_addr;
static bool s_ds18b20_valid = false;  // true = máme platnou adresu

// Časovač čtení
static unsigned long s_last_sensor_read = 0;

// Kalibrace ADC — průměrování střední hodnoty (midpoint)
static float s_adc_midpoint = ADC_MIDPOINT;

// ============================================================================
// INTERNÍ: Uložení / načtení adresy DS18B20 z Preferences
// ============================================================================

/*
 * Uloží adresu DS18B20 do Preferences.
 */
static void ds18_save_address(const DeviceAddress& addr) {
    g_prefs.putBytes("ds18_addr", addr, 8);
    Serial.print(F("DS18B20: adresa uložena — "));
    for (int i = 0; i < 8; i++) {
        Serial.print(addr[i], HEX);
        if (i < 7) Serial.print(":");
    }
    Serial.println();
}

/*
 * Načte adresu DS18B20 z Preferences.
 * @return true pokud adresa existuje
 */
static bool ds18_load_address(DeviceAddress& addr) {
    size_t len = g_prefs.getBytes("ds18_addr", addr, 8);
    if (len != 8) return false;

    // Ověříme, že to není samá nula (neinicializovaná hodnota)
    bool all_zero = true;
    for (int i = 0; i < 8; i++) {
        if (addr[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return false;

    Serial.print(F("DS18B20: adresa načtena — "));
    for (int i = 0; i < 8; i++) {
        Serial.print(addr[i], HEX);
        if (i < 7) Serial.print(":");
    }
    Serial.println();
    return true;
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void sensors_init() {
    // ADC pin pro SCT013
    pinMode(ADC_CURRENT_PIN, INPUT);
    analogSetAttenuation(ADC_11db);  // rozsah 0-3.3V (výchozí pro ESP32)

    // Kalibrace ADC — změříme několik vzorků pro zjištění midpointu
    long sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += analogRead(ADC_CURRENT_PIN);
        delayMicroseconds(200);
    }
    s_adc_midpoint = (float)sum / 100.0;
    Serial.print(F("Senzory: ADC midpoint = "));
    Serial.println(s_adc_midpoint);

    // DS18B20 — skenování přes raw OneWire (DallasTemperature::getDeviceCount()
    //           na ESP32 nespolehlivě vrací 0, i když čidlo existuje)
    g_onewire.reset_search();
    delay(50);

    // Nejdřív zkus načíst uloženou adresu z Preferences
    if (ds18_load_address(s_ds18b20_addr)) {
        // Ověříme raw OneWire resetem: je zařízení na sběrnici?
        g_onewire.reset();
        if (g_onewire.search(s_ds18b20_addr)) {
            // Adresa sedí — inicializujeme DallasTemperature pro čtení
            g_dallas.begin();
            s_ds18b20_valid = true;
            g_sensors.teplota_error = false;
            Serial.println(F("DS18B20: čidlo potvrzeno (uložená adresa)."));
        } else {
            // Adresa uložena, ale čidlo neodpovídá
            g_dallas.begin();
            Serial.println(F("DS18B20: uložená adresa — čidlo NEnalezeno!"));
            Serial.println(F("DS18B20: spusťte 'scan' v Serial Monitoru pro nové vyhledání."));
            g_sensors.teplota_error = true;
        }
    } else {
        // Žádná uložená adresa — raw OneWire sken sběrnice
        g_dallas.begin();
        Serial.println(F("DS18B20: žádná uložená adresa — raw OneWire scan..."));
        delay(100);

        uint8_t found_addr[8];
        g_onewire.reset_search();
        bool found = g_onewire.search(found_addr);

        if (found) {
            // Našli jsme čidlo — uložit adresu
            memcpy(s_ds18b20_addr, found_addr, 8);
            ds18_save_address(s_ds18b20_addr);
            s_ds18b20_valid = true;
            g_sensors.teplota_error = false;

            Serial.print(F("DS18B20: nalezeno 1 čidlo — "));
            for (int i = 0; i < 8; i++) {
                if (found_addr[i] < 0x10) Serial.print("0");
                Serial.print(found_addr[i], HEX);
                if (i < 7) Serial.print(":");
            }
            Serial.println();
        } else {
            Serial.println(F("DS18B20: VAROVÁNÍ — raw scan: žádné čidlo!"));
            Serial.println(F("DS18B20: zkontrolujte zapojení (VCC=3.3V, GND, DATA="));
            Serial.print(F("         pin "));
            Serial.print(ONEWIRE_PIN);
            Serial.println(F(" s pull-up 4.7kΩ na 3.3V)"));
            g_sensors.teplota_error = true;
        }
    }
}

void sensors_scan() {
    Serial.println(F("--- DS18B20 RAW ONEWIRE SKEN ---"));

    // Raw OneWire scan — nespoléhá na DallasTemperature (která na ESP32
    // občas vrací getDeviceCount()=0, i když čidlo na sběrnici je)
    uint8_t addr[8];
    int count = 0;

    g_onewire.reset_search();
    delay(10);

    while (g_onewire.search(addr)) {
        count++;
        Serial.print(F("  Čidlo #"));
        Serial.print(count);
        Serial.print(F(": "));
        for (int j = 0; j < 8; j++) {
            if (addr[j] < 0x10) Serial.print("0");
            Serial.print(addr[j], HEX);
            if (j < 7) Serial.print(":");
        }
        Serial.println();

        // První nalezené uložíme
        if (count == 1) {
            memcpy(s_ds18b20_addr, addr, 8);
        }
    }

    Serial.print(F("Celkem nalezeno: "));
    Serial.print(count);
    Serial.println(F(" čidel."));

    if (count == 0) {
        Serial.println(F("Žádné čidlo nenalezeno."));
        Serial.println(F("Zkontroluj:"));
        Serial.println(F("  1. Napájení — 3.3V mezi VCC a GND"));
        Serial.println(F("  2. Pull-up rezistor 4.7kΩ mezi DATA a 3.3V"));
        Serial.print(F("  3. DATA na GPIO"));
        Serial.print(ONEWIRE_PIN);
        Serial.println(F(" (ONEWIRE_PIN)"));
        Serial.println(F("  4. Nepřerušený kabel"));
    } else {
        ds18_save_address(s_ds18b20_addr);
        // Re-init DallasTemperature s novou adresou
        g_dallas.begin();
        s_ds18b20_valid = true;
        g_sensors.teplota_error = false;
        Serial.println(F("První čidlo uloženo do Preferences."));
    }

    Serial.println(F("--- KONEC SKENU ---"));
}

void sensors_read() {
    unsigned long now = millis();
    if (now - s_last_sensor_read < SENSOR_READ_MS) return;
    s_last_sensor_read = now;

    // === TEPLOTA (DS18B20) ===
    if (s_ds18b20_valid) {
        // Použijeme uloženou adresu — spolehlivější než index
        g_dallas.requestTemperaturesByAddress(s_ds18b20_addr);
        float temp = g_dallas.getTempC(s_ds18b20_addr);

        if (temp == DEVICE_DISCONNECTED_C || temp < -55.0 || temp > 125.0) {
            g_sensors.teplota_error = true;
        } else {
            g_sensors.teplota = temp;
            g_sensors.teplota_error = false;
        }
    }

    // === PROUD (SCT013 přes ADC) ===
    g_sensors.proud = read_current_rms();
    g_sensors.proud_error = false;
}

float read_current_rms() {
    long sum_sq = 0;
    int raw;

    for (int i = 0; i < ADC_SAMPLES; i++) {
        raw = analogRead(ADC_CURRENT_PIN);
        float centered = (float)raw - s_adc_midpoint;
        sum_sq += (long)(centered * centered);
        delayMicroseconds(200);
    }

    float rms_adc = sqrt((float)sum_sq / ADC_SAMPLES);
    float voltage_rms = rms_adc * (ADC_VREF / ADC_RESOLUTION);
    float current_rms = voltage_rms * SCT013_RATIO;

    return current_rms;
}
