/*
 * ============================================================================
 * mqtt_handler.h — MQTT KOMUNIKACE
 * ============================================================================
 *
 * ÚČEL:
 *   - Připojení k MQTT brokeru (192.168.0.191:1883)
 *   - Subscribe: fve/spotrebice/virivka/set (povely), menic/1/data, menic/2/data
 *   - Publikování stavu na spinac/VIRIVKA_OHREV/stav (každých 5s)
 *   - Last Will testament (při odpojení oznámí výpadek)
 *   - Watchdog: sleduje čas poslední zprávy (pro safety-off)
 *
 * VAZBY:
 *   - Používá: g_mqtt (PubSubClient), g_wifi_client (WiFiClient)
 *   - Čte: g_sensors (data k publikování), g_relay (aktuální stav)
 *   - Zapisuje: g_opi_relay1, g_opi_relay2, g_menic, g_menic2, g_mqtt_connected, g_last_mqtt_msg_ms
 *   - Voláno z: loop() v hlavním .ino
 * ============================================================================
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

/*
 * Inicializace MQTT klienta.
 * - Nastaví server, port, callback pro příjem zpráv
 * - Nastaví Last Will testament
 */
void mqtt_handler_init();

/*
 * Hlavní smyčka MQTT — volá se v loop().
 * - Udržuje spojení s brokerem
 * - Zpracovává příchozí zprávy
 * - Periodicky publikuje data ze senzorů
 */
void mqtt_handler_loop();

/*
 * Publikuje stav do MQTT (senzory + relé).
 * Volá se periodicky (STATUS_PUBLISH_MS) a při změně stavu relé.
 * Formát: {"status":"ZAP"/"OFF","vystup1":0/1,"vystup2":0/1,"proud0":x.x,"teplota":x.x}
 */
void mqtt_publish_status();

/*
 * Publikuje samostatnou zprávu na zadaný topic.
 * @param topic MQTT topic
 * @param message JSON string
 */
void mqtt_publish(const char* topic, const char* message);

#endif // MQTT_HANDLER_H
