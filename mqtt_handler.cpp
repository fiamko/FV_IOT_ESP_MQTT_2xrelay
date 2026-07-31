/*
 * ============================================================================
 * mqtt_handler.cpp — IMPLEMENTACE MQTT KOMUNIKACE
 * ============================================================================
 *
 * VAZBY:
 *   - Subscribe: fve/spotrebice/virivka/set (povel ON/OFF z OPI)
 *                menic/1/data (data z měniče 1 — baterie)
 *                menic/2/data (data z měniče 2 — výkon, budoucí)
 *   - Publish:   spinac/VIRIVKA_OHREV/stav (status relé + senzory, periodicky)
 *                spinac/VIRIVKA_OHREV/status (online/offline, Last Will)
 *   - NESubscribe na status topic — aby nevznikla zpětná vazba!
 * ============================================================================
 */

#include "variables.h"
#include "mqtt_handler.h"
#include <ArduinoJson.h>

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

// Časovače
static unsigned long s_last_reconnect_attempt = 0;
static unsigned long s_last_status_publish = 0;

// Buffer pro JSON — 1024 B (původní 256 B nestačilo pro menic/1/data s 20+ klíči)
static StaticJsonDocument<1024> s_json_doc;

// ============================================================================
// INTERNÍ: MQTT CALLBACK — zpracování příchozích zpráv
// ============================================================================

/*
 * Callback volaný při příjmu MQTT zprávy.
 * Zpracovává tři topicy:
 *   1. fve/spotrebice/virivka/set → čte enabled (true/false)
 *   2. menic/1/data → čte output_apparent_power, battery_discharge_current
 *   3. menic/2/data → čte output_apparent_power (budoucí měnič)
 *
 * Každá přijatá zpráva resetuje MQTT watchdog.
 */
static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    // Null-terminate payload
    char msg[1024];
    unsigned int len = length < 1023 ? length : 1023;
    memcpy(msg, payload, len);
    msg[len] = '\0';

    Serial.print(F("MQTT ← ["));
    Serial.print(topic);
    Serial.print(F("]: "));
    Serial.println(msg);

    // Reset watchdog — přišla zpráva, spojení žije
    g_last_mqtt_msg_ms = millis();

    // Parsování JSON
    DeserializationError err = deserializeJson(s_json_doc, msg);
    if (err) {
        Serial.print(F("MQTT: JSON chyba: "));
        Serial.println(err.c_str());
        return;
    }

    // Topic: fve/spotrebice/virivka/set — povel ON/OFF z OPI
    if (strcmp(topic, MQTT_TOPIC_VIRIVKA_CMD) == 0) {
        if (s_json_doc.containsKey(JSON_ENABLED)) {
            g_virivka_enabled = s_json_doc[JSON_ENABLED].as<bool>();
            Serial.print(F("MQTT: virivka enabled = "));
            Serial.println(g_virivka_enabled ? F("true (ZAP)") : F("false (OFF)"));
        } else {
            Serial.println(F("MQTT: varování — klíč 'enabled' nenalezen!"));
        }
    }

    // Topic: menic/1/data — data z měniče 1 (baterie, výkon)
    if (strcmp(topic, MQTT_TOPIC_MENIC) == 0) {
        if (s_json_doc.containsKey(JSON_VYKON)) {
            g_menic.output_apparent_power = s_json_doc[JSON_VYKON].as<float>();
        }
        if (s_json_doc.containsKey(JSON_BAT_VYBIJENI)) {
            g_menic.battery_discharge_current = s_json_doc[JSON_BAT_VYBIJENI].as<float>();
        }
        g_menic.last_update_ms = millis();

        Serial.print(F("MQTT: menic1 vykon="));
        Serial.print(g_menic.output_apparent_power);
        Serial.print(F("W, bat="));
        Serial.print(g_menic.battery_discharge_current);
        Serial.println(F("A"));
    }

    // Topic: menic/2/data — data z měniče 2 (budoucí — výkon pro ochranu)
    if (strcmp(topic, MQTT_TOPIC_MENIC2) == 0) {
        if (s_json_doc.containsKey(JSON_VYKON)) {
            g_menic2.output_apparent_power = s_json_doc[JSON_VYKON].as<float>();
        }
        g_menic2.last_update_ms = millis();

        Serial.print(F("MQTT: menic2 vykon="));
        Serial.println(g_menic2.output_apparent_power);
    }
}

// ============================================================================
// INTERNÍ: Připojení k MQTT brokeru
// ============================================================================

/*
 * Pokusí se připojit k MQTT brokeru.
 * - Nastaví Last Will: při odpojení publikuje {"status":"offline"}
 * - Subscribe na řídicí topicy a data z měničů
 * @return true = připojeno
 */
static bool mqtt_connect() {
    if (!WiFi.isConnected()) return false;

    Serial.print(F("MQTT: připojuji k "));
    Serial.print(MQTT_SERVER);
    Serial.print(F("... "));

    // Last Will: při ztrátě spojení oznámí výpadek
    String will_msg = F("{\"status\":\"offline\"}");

    if (g_mqtt.connect(MQTT_CLIENT_ID,
                       nullptr, nullptr,          // user, pass (nepoužíváme)
                       MQTT_TOPIC_STAV, 0, true,  // Last Will topic, QoS=0, retain
                       will_msg.c_str())) {

        Serial.println(F("OK"));

        // Subscribe na řídicí povely z OPI
        g_mqtt.subscribe(MQTT_TOPIC_VIRIVKA_CMD);
        // Subscribe na data z měničů
        g_mqtt.subscribe(MQTT_TOPIC_MENIC);
        g_mqtt.subscribe(MQTT_TOPIC_MENIC2);

        Serial.print(F("MQTT: subscribed "));
        Serial.print(MQTT_TOPIC_VIRIVKA_CMD);
        Serial.print(F(", "));
        Serial.print(MQTT_TOPIC_MENIC);
        Serial.print(F(", "));
        Serial.println(MQTT_TOPIC_MENIC2);

        // Oznámení že jsme online
        String online_msg = F("{\"status\":\"online\"}");
        g_mqtt.publish(MQTT_TOPIC_STAV, online_msg.c_str(), true);

        g_mqtt_connected = true;
        return true;
    }

    Serial.print(F("SELHALO ("));
    Serial.print(g_mqtt.state());
    Serial.println(F(")"));
    g_mqtt_connected = false;
    return false;
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void mqtt_handler_init() {
    g_mqtt.setClient(g_wifi_client);
    g_mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    g_mqtt.setCallback(mqtt_callback);
    g_mqtt.setBufferSize(1024);

    Serial.println(F("MQTT: handler inicializován."));
}

void mqtt_handler_loop() {
    unsigned long now = millis();

    // Kontrola WiFi — bez WiFi nemá smysl
    if (!WiFi.isConnected()) {
        g_mqtt_connected = false;
        return;
    }

    // Reconnect pokud není spojení
    if (!g_mqtt.connected()) {
        g_mqtt_connected = false;
        if (now - s_last_reconnect_attempt > MQTT_RECONNECT_MS) {
            s_last_reconnect_attempt = now;
            mqtt_connect();
        }
        return;
    }

    // Zpracování příchozích zpráv
    g_mqtt.loop();

    // Periodické publikování stavu (každých STATUS_PUBLISH_MS)
    if (now - s_last_status_publish > STATUS_PUBLISH_MS) {
        s_last_status_publish = now;
        mqtt_publish_status();
    }
}

void mqtt_publish_status() {
    if (!g_mqtt.connected()) return;

    // Sestavení JSON zprávy se stavem
    s_json_doc.clear();
    bool any_on = g_relay.actual[0] || g_relay.actual[1];
    s_json_doc[JSON_STATUS]  = any_on ? "ZAP" : "OFF";
    s_json_doc[JSON_VYSTUP1] = g_relay.actual[0] ? 1 : 0;
    s_json_doc[JSON_VYSTUP2] = g_relay.actual[1] ? 1 : 0;
    s_json_doc[JSON_PROUD]   = g_sensors.proud;
    s_json_doc[JSON_TEPLOTA] = g_sensors.teplota;

    String msg;
    serializeJson(s_json_doc, msg);

    mqtt_publish(MQTT_TOPIC_STATUS, msg.c_str());

    Serial.print(F("MQTT → ["));
    Serial.print(MQTT_TOPIC_STATUS);
    Serial.print(F("]: "));
    Serial.println(msg);
}

void mqtt_publish(const char* topic, const char* message) {
    if (!g_mqtt.connected()) return;
    g_mqtt.publish(topic, message);
}
