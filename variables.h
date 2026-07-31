/*
 * ============================================================================
 * variables.h — GLOBÁLNÍ PROMĚNNÉ
 * ============================================================================
 *
 * ÚČEL:
 *   Jediné místo pro deklaraci všech globálních proměnných.
 *   Každá proměnná má podrobný popis: co reprezentuje, jaké má vazby,
 *   kdo ji čte a kdo zapisuje. Díky tomu se v kódu snadno orientuje.
 *
 * PRAVIDLA:
 *   - struct pro související skupiny (stav, nastavení, senzory, MQTT data)
 *   - prefix: g_ = globální, s_ = statická v rámci modulu (v .cpp)
 *   - komentář vždy: [KDO ZAPISUJE] [KDO ČTE] [JEDNOTKA] [VÝZNAM]
 *
 * VAZBY:
 *   - includován všemi moduly (každý .cpp #include "variables.h")
 *   - config.h: definice pinů a konstant
 *   - Preferences: ukládání/načítání nastavení
 * ============================================================================
 */

#ifndef VARIABLES_H
#define VARIABLES_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "config.h"

// ============================================================================
// STAVOVÝ AUTOMAT OHŘEVU
// ============================================================================

/*
 * Stavy řízení ohřevu vířivky.
 *
 * VS_OFF             — obě relé OFF, čeká na enabled=true z OPI
 * VS_STARTING        — relé1 ON, čeká RELAY_STEP_ON_MS, pak zapne relé2
 * VS_ACTIVE          — obě relé ON, hlídá override podmínky
 * VS_STOPPING        — relé1 OFF, čeká RELAY_STEP_OFF_MS, pak vypne relé2
 * VS_OVERRIDE_CHECK  — relé1 OFF (kvůli override), čeká OVERRIDE_RECHECK_MS,
 *                       pak zkontroluje podmínky: OK → ACTIVE, stále špatné → OFF
 *
 * Přechody:
 *   OFF → STARTING:       enabled=true && !override && !safety
 *   STARTING → ACTIVE:    timer RELAY_STEP_ON_MS vypršel
 *   STARTING → STOPPING:  enabled=false || override
 *   ACTIVE → STOPPING:    enabled=false
 *   ACTIVE → OVERRIDE_CHECK: override (baterie nebo výkon)
 *   STOPPING → OFF:       timer RELAY_STEP_OFF_MS vypršel
 *   OVERRIDE_CHECK → ACTIVE: timer vypršel && !override && enabled=true
 *   OVERRIDE_CHECK → OFF: timer vypršel && (override || enabled=false)
 *   JAKÝKOLI STAV → OFF:  MQTT timeout (safety) — okamžité vypnutí obou relé
 */
enum VirivkaState {
    VS_OFF,
    VS_STARTING,
    VS_ACTIVE,
    VS_STOPPING,
    VS_OVERRIDE_CHECK
};

// ============================================================================
// STRUKTURY
// ============================================================================

/*
 * Stav relé — kdo co požaduje a skutečný stav.
 * priority: SAFETY > OVERRIDE > MQTT_COMMAND
 */
struct RelayState {
    // [ZAPISUJE: relay_control] [ČTE: mqtt_handler, web_setup]
    // Skutečný fyzický stav relé (po vyhodnocení všech override)
    bool actual[2];

    // [ZAPISUJE: relay_control] [ČTE: web_setup]
    // Důvod poslední změny — pro diagnostiku
    enum Reason { NONE, MQTT_ON, MQTT_OFF, OVERRIDE_POWER, OVERRIDE_BAT, SAFETY_OFF, MENIC_STALE } reason;
};

/*
 * Senzory — hodnoty z čidel.
 */
struct SensorData {
    // [ZAPISUJE: sensors] [ČTE: mqtt_handler, web_setup] [A]
    // Proud změřený SCT013 na vstupu vířivky (RMS, AC)
    float proud;

    // [ZAPISUJE: sensors] [ČTE: mqtt_handler, web_setup] [°C]
    // Teplota z DS18B20 — pouze informativní
    float teplota;

    // [ZAPISUJE: sensors] [ČTE: sensors, web_setup]
    // Chybový stav senzorů (true = chyba)
    bool proud_error;
    bool teplota_error;
};

/*
 * Data z měniče (přijatá z MQTT topicu menic/1/data nebo menic/2/data).
 */
struct MenicData {
    // [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup] [W]
    // Zdánlivý výkon měniče — pro ochranu proti přetížení
    float output_apparent_power;

    // [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup] [A]
    // Vybíjecí proud baterie — pro ochranu proti nadměrnému vybíjení
    float battery_discharge_current;

    // [ZAPISUJE: mqtt_handler] [ČTE: relay_control]
    // Čas posledního příjmu dat z měniče (millis) — pro watchdog
    unsigned long last_update_ms;
};

/*
 * Uživatelské nastavení — ukládáno do Preferences (přežije restart).
 */
struct Settings {
    // [ZAPISUJE: web_setup] [ČTE: mqtt_handler] [s]
    // Timeout výpadku MQTT — po této době bez zprávy se relé vypnou
    int mqtt_timeout;

    // [ZAPISUJE: web_setup] [ČTE: relay_control] [W]
    // Maximální povolený výkon — při překročení se začnou vypínat relé
    int max_vykon;

    // [ZAPISUJE: web_setup] [ČTE: relay_control] [A]
    // Maximální vybíjecí proud baterie — při překročení se vypínají relé
    int vybijeni_bat;

    // [ZAPISUJE: web_setup] [ČTE: relay_control] [s]
    // Prodleva mezi vypínáním jednotlivých relé při override (2-8s)
    int override_delay;

    // [ZAPISUJE: web_setup] [ČTE: web_setup]
    // Heslo pro uložení nastavení přes web (server-side, ne JS!)
    char web_password[32];

    // WiFi — primární
    char wifi1_ssid[32];
    char wifi1_pass[64];

    // WiFi — záložní
    char wifi2_ssid[32];
    char wifi2_pass[64];
};

// ============================================================================
// GLOBÁLNÍ OBJEKTY — [VLASTNÍK] objektu
// ============================================================================

// [VLASTNÍK: FVE_ovladani_virivka_ESP32.ino]
extern Preferences g_prefs;            // úložiště nastavení

// [VLASTNÍK: WiFi (ESP32)]
extern WiFiClient g_wifi_client;       // TCP klient pro MQTT

// [VLASTNÍK: mqtt_handler]
extern PubSubClient g_mqtt;            // MQTT klient

// [VLASTNÍK: hlavní .ino — inicializace; ČTE: všechny moduly]
extern RelayState  g_relay;            // stav relé
extern SensorData  g_sensors;          // data ze senzorů
extern MenicData   g_menic;            // data z měniče 1 (baterie)
extern MenicData   g_menic2;           // data z měniče 2 (výkon pro vířivku)
extern Settings    g_settings;         // uživatelské nastavení

// ============================================================================
// GLOBÁLNÍ STAVOVÉ PROMĚNNÉ
// ============================================================================

// [ZAPISUJE: relay_control] [ČTE: mqtt_handler, web_setup, relay_control]
// Aktuální stav automatu ohřevu vířivky
extern VirivkaState g_virivka_state;

// [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup]
// Povel z OPI: true = zapnout ohřev, false = vypnout
extern bool g_virivka_enabled;

// [ZAPISUJE: mqtt_handler] [ČTE: relay_control, web_setup]
// true = MQTT je připojeno, false = výpadek
extern bool g_mqtt_connected;

// [ZAPISUJE: mqtt_handler] [ČTE: relay_control]
// Čas poslední MQTT zprávy (millis()) — watchdog pro safety-off
extern unsigned long g_last_mqtt_msg_ms;

// [ZAPISUJE: wifi_manager] [ČTE: web_setup]
// Čas poslední změny WiFi stavu
extern unsigned long g_last_wifi_change_ms;

// [ZAPISUJE: wifi_manager] [ČTE: hlavní .ino, web_setup]
// Příznak: ESP je v režimu WiFi konfigurace (captive portal)
extern bool g_wifi_config_mode;

// [ZAPISUJE: hlavní .ino] [ČTE: všechny moduly]
// Čas poslední smyčky loop() — pro časování
extern unsigned long g_loop_ms;

#endif // VARIABLES_H
