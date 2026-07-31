/*
 * ============================================================================
 * relay_control.h — ŘÍZENÍ RELÉ (STAVOVÝ AUTOMAT)
 * ============================================================================
 *
 * ÚČEL:
 *   - Řízení fyzického stavu relé podle stavového automatu:
 *     VS_OFF → VS_STARTING → VS_ACTIVE (zapínání)
 *     VS_ACTIVE → VS_STOPPING → VS_OFF (vypínání)
 *     VS_ACTIVE → VS_OVERRIDE_CHECK → ... (ochrana)
 *   - Sekvenční zapínání: relé1 → 2.5s → relé2
 *   - Sekvenční vypínání: relé1 → 2.5s → relé2
 *   - Override: při překročení limitů relé1 OFF → 4s → kontrola → ...
 *   - MQTT watchdog: při výpadku okamžité vypnutí
 *   - Bez enabled=true NIKDY nezapínat relé
 *
 * VAZBY:
 *   - Čte: g_virivka_enabled, g_menic, g_menic2, g_settings, g_last_mqtt_msg_ms
 *   - Zapisuje: g_relay.actual[], g_relay.reason, g_virivka_state
 *   - Ovládá: GPIO piny RELAY1_PIN, RELAY2_PIN
 *   - Voláno z: loop() v hlavním .ino
 * ============================================================================
 */

#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

/*
 * Inicializace relé modulu.
 * - Nastaví GPIO piny jako výstupy
 * - Vypne obě relé (bezpečný výchozí stav)
 */
void relay_control_init();

/*
 * Hlavní smyčka řízení relé — volá se v loop().
 * - Kontroluje MQTT watchdog
 * - Vyhodnocuje stavový automat (VS_OFF → STARTING → ACTIVE → ...)
 * - Aplikuje změny s prodlevami
 */
void relay_control_loop();

/*
 * Okamžitě vypne obě relé (bezpečnostní funkce).
 * Používá se při MQTT timeoutu.
 */
void relay_emergency_off();

/*
 * Vrátí textový popis důvodu poslední změny.
 */
const char* relay_reason_str();

#endif // RELAY_CONTROL_H
