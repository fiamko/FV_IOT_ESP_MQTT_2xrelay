/*
 * ============================================================================
 * config.h — DEFINICE PINŮ, KONSTANT A VÝCHOZÍCH HODNOT
 * ============================================================================
 *
 * ÚČEL:
 *   Centrální soubor všech hardwarových definic a konstant.
 *   Cokoliv, co se používá na více místech nebo se může v budoucnu měnit,
 *   patří sem. NEPATŘÍ sem proměnné — ty jsou v variables.h.
 *
 * VAZBY:
 *   - includován všemi .cpp moduly (přes variables.h nebo přímo)
 *   - GPIO piny: fyzické zapojení ESP32 modulu
 *   - MQTT: adresa brokeru a struktura topiců (musí odpovídat OPI)
 *   - Výchozí hodnoty: použity při prvním startu, pak přepsány z Preferences
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// VERZE FIRMWARU
// ============================================================================
#define FIRMWARE_VERSION "2.0.1"

// ============================================================================
// GPIO PINY — OVĚŘENO TESTOVACÍM PROGRAMEM 17.7.2026
// ============================================================================

#define RELAY1_PIN      16    // Relé 1 (vnější) — OVĚŘENO: active HIGH
#define RELAY2_PIN      17    // Relé 2 (vnitřní) — OVĚŘENO: active HIGH

// Stavová LED na desce
#define LED_PIN         23    // LED — OVĚŘENO: active HIGH

// Tlačítko (BOOT na ESP32)
#define BUTTON_PIN      0     // Tlačítko — OVĚŘENO: stisk = LOW (INPUT_PULLUP)

// DS18B20 teplotní čidlo (OneWire)
#define ONEWIRE_PIN     33    // Datový pin pro DS18B20 (+ pull-up 4.7kΩ na 3.3V)

// SCT013 20A/1V proudový senzor (ADC)
#define ADC_CURRENT_PIN 34    // ADC1_CH6 — pouze vstupní (nemá pull-up)

// ============================================================================
// AKTIVNÍ ÚROVNĚ VÝSTUPŮ — OVĚŘENO
// ============================================================================
#define RELAY_ACTIVE_HIGH  true    // OVĚŘENO: relé spíná při HIGH
#define LED_ACTIVE_HIGH    true    // OVĚŘENO: LED svítí při HIGH

// ============================================================================
// MQTT
// ============================================================================
#define MQTT_SERVER           "192.168.0.191"
#define MQTT_PORT             1883
#define MQTT_CLIENT_ID        "ESP32_virivka"

// Topicy — musí odpovídat tomu, co běží na OrangePi!
#define MQTT_TOPIC_VIRIVKA_CMD  "fve/spotrebice/virivka/set"    // OPI → ESP: povel ON/OFF
#define MQTT_TOPIC_STATUS       "fve/spotrebice/virivka/stav"   // ESP → MQTT: stav relé + senzory
#define MQTT_TOPIC_STAV         "fve/spotrebice/virivka/status" // ESP → MQTT: online/offline (Last Will)
#define MQTT_TOPIC_MENIC        "menic/1/data"                  // OPI → MQTT: data z měniče 1
#define MQTT_TOPIC_MENIC2       "menic/2/data"                  // OPI → MQTT: data z měniče 2 (budoucí)

// JSON klíče — příchozí (z OPI)
#define JSON_ENABLED        "enabled"

// JSON klíče — příchozí (z měničů)
#define JSON_VYKON          "output_apparent_power"
#define JSON_BAT_VYBIJENI   "battery_discharge_current"

// JSON klíče — odchozí (status)
#define JSON_STATUS         "status"
#define JSON_VYSTUP1        "vystup1"
#define JSON_VYSTUP2        "vystup2"
#define JSON_PROUD          "proud0"
#define JSON_TEPLOTA        "teplota"

// ============================================================================
// VÝCHOZÍ HODNOTY NASTAVENÍ (lze změnit přes webovou stránku)
// ============================================================================
#define DEFAULT_MQTT_TIMEOUT     15      // [s]  — výpadek MQTT → vypnout relé
#define MENIC_TIMEOUT_S          30      // [s]  — výpadek dat z měniče → vypnout relé
#define DEFAULT_MAX_VYKON        3000    // [W]  — maximální povolený výkon měniče
#define DEFAULT_VYBIJENI_BAT     20      // [A]  — maximální vybíjecí proud baterie
#define DEFAULT_OVERRIDE_DELAY   5       // [s]  — prodleva mezi vypínáním relé při override (pro web)

// ============================================================================
// WEBOVÉ NASTAVENÍ
// ============================================================================
#define WEB_PORT          80
#define WEB_USERNAME      "admin"
#define DEFAULT_WEB_PASS   "zmenit"   // výchozí heslo — změň na webu!

// ============================================================================
// ČASOVÁNÍ (ms)
// ============================================================================
#define MQTT_RECONNECT_MS       5000    // interval pokusů o reconnect MQTT
#define SENSOR_READ_MS          2000    // interval čtení senzorů
#define STATUS_PUBLISH_MS       5000    // interval publikování stavu do MQTT (4-5s)
#define BUTTON_LONG_PRESS_MS    5000    // doba pro spuštění WiFi portálu tlačítkem

// Časování relé — stavový automat
#define RELAY_STEP_ON_MS        2500    // prodleva mezi zapnutím relé1 a relé2
#define RELAY_STEP_OFF_MS       2500    // prodleva mezi vypnutím relé1 a relé2
#define OVERRIDE_RECHECK_MS     4000    // čekání po vypnutí relé1 při override, než se zkontroluje stav

// ============================================================================
// ADC — SCT013 20A/1V kalibrace
// ============================================================================
// SCT013 dává 1V AC při 20A. ESP32 ADC: 12bit (0-4095), rozsah ~0-3.3V
// Pro AC měření: používáme vzorkování a RMS výpočet.
#define ADC_SAMPLES          200     // počet vzorků pro RMS (musí pokrýt ~2 periody 50Hz)
#define ADC_VREF             3.3     // [V] referenční napětí ESP32
#define ADC_RESOLUTION       4095.0  // 12-bit ADC
#define SCT013_RATIO         20.0    // 20A → 1V (poměr transformátoru)
#define ADC_MIDPOINT         2048    // teoretický střed ADC (1.65V) — kalibrováno za běhu

// ============================================================================
// SAFETY
// ============================================================================
// Při ztrátě MQTT spojení se relé vypnou. Tento timeout určuje, za jak dlouho.
// Hodnota se dá změnit za běhu přes web. Zde je výchozí.
#define MQTT_WATCHDOG_MS     (DEFAULT_MQTT_TIMEOUT * 1000UL)

#endif // CONFIG_H
