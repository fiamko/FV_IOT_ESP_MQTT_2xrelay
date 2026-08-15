/*
 * ============================================================================
 * FVE_ovladani_virivka_ESP32.ino — HLAVNÍ SOUBOR
 * ============================================================================
 *
 * ÚČEL:
 *   Řízení ohřevu vířivky jako vytěžování FV elektrárny.
 *   ESP32 dvoureléový modul (ESP32-32E N4) ovládá dvě topné spirály
 *   podle povelů z MQTT, s ochranou proti přetížení měniče a vybíjení baterie.
 *
 * FUNKCE:
 *   - WiFi s duální sítí (primární + záložní)
 *   - Captive portal pro prvotní nastavení WiFi (tlačítko 5s)
 *   - MQTT komunikace (subscribe + publish)
 *   - Řízení relé (normální režim + override ochrana)
 *   - Měření proudu (SCT013 20A/1V, interní ADC)
 *   - Měření teploty (DS18B20, OneWire)
 *   - Webová stránka s nastavením (heslo server-side)
 *   - OTA aktualizace (ArduinoOTA)
 *   - Trvalé uložení nastavení (Preferences)
 *
 * STRUKTURA SOUBORŮ:
 *   FVE_ovladani_virivka_ESP32.ino — hlavní soubor (tento)
 *   config.h          — definice pinů, konstant, výchozí hodnoty
 *   variables.h       — globální proměnné s podrobnými komentáři
 *   wifi_manager.h/cpp   — WiFi, duální síť, captive portal
 *   mqtt_handler.h/cpp   — MQTT spojení, JSON, publish/subscribe
 *   relay_control.h/cpp  — logika relé + override
 *   sensors.h/cpp        — DS18B20 + SCT013 ADC
 *   web_setup.h/cpp      — webová stránka nastavení
 *
 * HW:
 *   - ESP32-32E N4, deska ESP32_Relay_AC_X2 303E32AC210
 *   - 2× relé 10A (GPIO16, GPIO17 — OVĚŘENO: active HIGH)
 *   - LED GPIO23 (OVĚŘENO: active HIGH), tlačítko GPIO0 (OVĚŘENO: stisk=LOW)
 *   - DS18B20 na GPIO33 (OneWire)
 *   - SCT013 20A/1V na GPIO34 (ADC)
 *
 * KNIHOVNY (Arduino IDE):
 *   - PubSubClient (MQTT)
 *   - ArduinoJson (JSON)
 *   - DallasTemperature + OneWire (DS18B20)
 *   - ArduinoOTA (OTA updates)
 *   - DNSServer (captive portal)
 *   - Preferences (vestavěné)
 *   - WebServer (vestavěné)
 * ============================================================================
 */

#include "variables.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "relay_control.h"
#include "sensors.h"
#include "web_setup.h"
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

// ============================================================================
// GLOBÁLNÍ PROMĚNNÉ — DEFINICE (deklarace jsou v variables.h)
// ============================================================================

Preferences g_prefs;              // úložiště nastavení (Preferences)
WiFiClient   g_wifi_client;       // TCP klient pro MQTT
PubSubClient g_mqtt(g_wifi_client); // MQTT klient

RelayState  g_relay;              // stav relé
SensorData  g_sensors;            // data ze senzorů
MenicData   g_menic;              // data z měniče 1 (baterie)
MenicData   g_menic2;             // data z měniče 2 (výkon pro vířivku)
Settings    g_settings;           // uživatelské nastavení

bool         g_opi_relay1 = false;       // OPI povel: relé 1
bool         g_opi_relay2 = false;       // OPI povel: relé 2

bool         g_mqtt_connected = false;
unsigned long g_last_mqtt_msg_ms = 0;
unsigned long g_last_wifi_change_ms = 0;
bool         g_wifi_config_mode = false;
unsigned long g_loop_ms = 0;

// ============================================================================
// INTERNÍ: Načtení / uložení nastavení
// ============================================================================

/*
 * Načte nastavení z Preferences. Pokud neexistují, použije výchozí hodnoty.
 */
static void load_settings() {
    g_prefs.begin("virivka", false);  // namespace "virivka", read-write

    // Pri zmene verze firmware vymazat stare nastaveni (cisty start)
    String storedVer = g_prefs.getString("fw_ver", "");
    if (storedVer != FIRMWARE_VERSION) {
        g_prefs.clear();
        g_prefs.putString("fw_ver", FIRMWARE_VERSION);
        Serial.println(F("Nastaveni: nova verze FW — vychozi hodnoty."));
    }

    g_settings.mqtt_timeout   = g_prefs.getInt("mqtt_timeout", DEFAULT_MQTT_TIMEOUT);
    g_settings.max_vykon      = g_prefs.getInt("max_vykon", DEFAULT_MAX_VYKON);
    g_settings.vybijeni_bat   = g_prefs.getInt("vybijeni_bat", DEFAULT_VYBIJENI_BAT);
    g_settings.override_delay = g_prefs.getInt("override_dly", DEFAULT_OVERRIDE_DELAY);

    String pass = g_prefs.getString("web_pass", DEFAULT_WEB_PASS);
    strncpy(g_settings.web_password, pass.c_str(), 31);

    Serial.println(F("Nastavení načteno:"));
    Serial.printf("  MQTT timeout:    %d s\n", g_settings.mqtt_timeout);
    Serial.printf("  Max. výkon:      %d W\n", g_settings.max_vykon);
    Serial.printf("  Max. vybíjení:   %d A\n", g_settings.vybijeni_bat);
    Serial.printf("  Override delay:  %d s\n", g_settings.override_delay);
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Vypnout defaultní task watchdog (5s) — setup může trvat déle
    esp_task_wdt_delete(NULL);

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════════╗"));
    Serial.println(F("║   FVE ovládání vířivky — ESP32                 ║"));
    Serial.println(F("║   FW: " FIRMWARE_VERSION "                                  ║"));
    Serial.println(F("╚══════════════════════════════════════════════════╝"));
    Serial.println();

    // 1. Načtení nastavení z Preferences
    load_settings();

    // 2. Inicializace relé (vypnutá — bezpečný stav)
    relay_control_init();

    // 3. Inicializace senzorů (DS18B20, ADC)
    sensors_init();

    // 4. WiFi — připojení nebo captive portal
    //    Toto může zablokovat na delší dobu (připojování)
    wifi_manager_init();

    // 5. Pokud jsme připojeni k WiFi, inicializujeme zbytek
    if (WiFi.isConnected() && !g_wifi_config_mode) {
        // 5a. OTA
        ArduinoOTA.setHostname("ESP32-virivka");
        ArduinoOTA.onStart([]() {
            // Během OTA deaktivujeme task watchdog — flash zápis blokuje loop >10s
            esp_task_wdt_delete(NULL);
            Serial.println(F("OTA: začínám update... (WDT off)"));
        });
        ArduinoOTA.onEnd([]() {
            // Po OTA obnovíme task watchdog
            esp_task_wdt_add(NULL);
            Serial.println(F("OTA: update dokončen. (WDT on)"));
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("OTA: %u%%\r", (progress * 100) / total);
        });
        ArduinoOTA.onError([](ota_error_t error) {
            // I při chybě obnovit watchdog!
            esp_task_wdt_add(NULL);
            Serial.printf("OTA error: %u (WDT restored)\n", error);
        });
        ArduinoOTA.begin();
        Serial.println(F("OTA: připraveno."));

        // 5b. MQTT handler
        mqtt_handler_init();

        // 5c. Webový server pro nastavení
        web_setup_init();
    }

    // 5. HW watchdog — restart ESP32 při zamrznutí (30s timeout, OTA-safe)
    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 30000, .trigger_panic = true };
    esp_task_wdt_init(&wdt_cfg);
    esp_task_wdt_add(NULL);

    Serial.println(F("Setup dokončen."));

    // Inicializace watchdog timeru (aby safety hned nevypnul relé)
    g_last_mqtt_msg_ms = millis();
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
    g_loop_ms = millis();

    // 0. HW watchdog reset (krmení psa)
    esp_task_wdt_reset();

    // 1. WiFi — správa pripojeni, detekce tlacitka, LED
    wifi_manager_loop();

    // 2. OTA — musí běžet vždy, když je WiFi
    if (WiFi.isConnected()) {
        esp_task_wdt_reset();  // krmení psa před OTA (může blokovat)
        ArduinoOTA.handle();
    }

    // 3. Pokud jsme v config režimu (captive portal), zbytek přeskakujeme
    if (g_wifi_config_mode) {
        delay(50);
        return;
    }

    // 4. Web server (nastavení)
    web_setup_loop();

    // 5. MQTT — spojení, příjem, publikování
    mqtt_handler_loop();

    // 6. Senzory — čtení teploty a proudu
    sensors_read();

    // 7. Relé — vyhodnocení a aplikace stavu
    relay_control_loop();

    // 8. Sériové příkazy (diagnostika)
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "scan") {
            sensors_scan();
        } else if (cmd == "status") {
            Serial.println(F("--- STATUS ---"));
            Serial.print(F("WiFi: ")); Serial.println(WiFi.isConnected() ? F("OK") : F("OFF"));
            Serial.print(F("MQTT: ")); Serial.println(g_mqtt_connected ? F("OK") : F("OFF"));
            Serial.print(F("Virivka: R1=")); Serial.print(g_opi_relay1);
            Serial.print(F(" R2=")); Serial.println(g_opi_relay2);
            Serial.print(F("R1=")); Serial.print(g_relay.actual[0]);
            Serial.print(F(" R2=")); Serial.println(g_relay.actual[1]);
            Serial.print(F("Proud: ")); Serial.print(g_sensors.proud, 2); Serial.println(F(" A"));
            Serial.print(F("Teplota: ")); Serial.print(g_sensors.teplota, 1); Serial.println(F(" °C"));
            Serial.print(F("Duvod: ")); Serial.println(relay_reason_str());
            Serial.println(F("--------------"));
        } else {
            Serial.println(F("Prikazy: scan, status"));
        }
    }

    delay(10);
}
