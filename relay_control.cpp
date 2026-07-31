/*
 * ============================================================================
 * relay_control.cpp — IMPLEMENTACE ŘÍZENÍ RELÉ (STAVOVÝ AUTOMAT)
 * ============================================================================
 *
 * ÚČEL:
 *   Řízení dvou relé ohřevu vířivky podle stavového automatu.
 *
 *   STAVY:
 *     VS_OFF             — obě OFF, čeká na enabled=true
 *     VS_STARTING        — relé1 ON, za 2.5s → relé2 ON
 *     VS_ACTIVE          — obě ON, hlídá override
 *     VS_STOPPING        — relé1 OFF, za 2.5s → relé2 OFF
 *     VS_OVERRIDE_CHECK  — relé1 OFF (baterie/výkon), za 4s kontrola
 *
 *   PŘECHODY (priorita: SAFETY > OVERRIDE > MQTT_COMMAND):
 *     - OFF + enabled=true + !override → STARTING
 *     - STARTING + timer → ACTIVE
 *     - STARTING + (!enabled || override) → STOPPING
 *     - ACTIVE + !enabled → STOPPING
 *     - ACTIVE + override → OVERRIDE_CHECK
 *     - STOPPING + timer → OFF
 *     - OVERRIDE_CHECK + timer + !override + enabled → ACTIVE (obnovení)
 *     - OVERRIDE_CHECK + timer + (override || !enabled) → OFF
 *     - JAKÝKOLI STAV + MQTT timeout → OFF (okamžitě!)
 *
 *   BEZPEČNOST:
 *     - Bez enabled=true NIKDY nezapínat relé
 *     - Vypnout relé lze vždy (ochrana baterie/měniče)
 *     - MQTT watchdog: při výpadku delším než mqtt_timeout → okamžité OFF
 *
 * VAZBY:
 *   - Čte:  g_virivka_enabled, g_virivka_state, g_menic, g_menic2,
 *           g_settings, g_last_mqtt_msg_ms
 *   - Píše: g_relay.actual[], g_relay.reason, g_virivka_state
 *   - Ovládá: GPIO RELAY1_PIN, RELAY2_PIN
 * ============================================================================
 */

#include "variables.h"
#include "relay_control.h"

// ============================================================================
// STATICKÉ PROMĚNNÉ
// ============================================================================

// Časovač pro přechody stavového automatu
static unsigned long s_state_timer = 0;

// Příznak: probíhá safety-off (okamžité, bez prodlevy)
static bool s_safety_active = false;
static bool s_menic_stale_active = false;      // menic data timeout

// ============================================================================
// INTERNÍ FUNKCE
// ============================================================================

/*
 * Fyzicky nastaví stav jednoho relé (pouze při změně).
 * @param index 0=Relé1, 1=Relé2
 * @param on true=zapnout, false=vypnout
 */
static void set_relay(int index, bool on) {
    if (g_relay.actual[index] == on) return;  // beze změny

    bool level = RELAY_ACTIVE_HIGH ? on : !on;
    int pin = (index == 0) ? RELAY1_PIN : RELAY2_PIN;
    digitalWrite(pin, level);
    g_relay.actual[index] = on;

    Serial.print(F("Relé "));
    Serial.print(index + 1);
    Serial.print(F(" → "));
    Serial.println(on ? F("ON") : F("OFF"));
}

/*
 * Zjistí, zda je aktivní některá override podmínka.
 * - Překročení vybíjecího proudu baterie (z měniče 1)
 * - Překročení výkonu měniče 2 (pokud jsou data)
 * @return true = override aktivní, relé se nesmí zapínat / musí se vypínat
 */
static RelayState::Reason check_override_reason() {
    // Pouze pokud máme data (last_update_ms > 0)
    if (g_menic.last_update_ms > 0) {
        if (g_menic.battery_discharge_current > g_settings.vybijeni_bat) {
            return RelayState::OVERRIDE_BAT;
        }
    }
    if (g_menic2.last_update_ms > 0) {
        if (g_menic2.output_apparent_power > g_settings.max_vykon) {
            return RelayState::OVERRIDE_POWER;
        }
    }
    return RelayState::NONE;
}

/*
 * Zjistí, zda vypršel MQTT watchdog.
 * @param now aktuální čas (millis)
 * @return true = timeout, má se spustit safety-off
 */
static bool is_mqtt_timeout(unsigned long now) {
    unsigned long elapsed = now - g_last_mqtt_msg_ms;
    return elapsed > (g_settings.mqtt_timeout * 1000UL);
}

static bool is_menic_timeout(unsigned long now) {
    if (g_menic.last_update_ms == 0) return false;
    unsigned long elapsed = now - g_menic.last_update_ms;
    return elapsed > (MENIC_TIMEOUT_S * 1000UL);
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void relay_control_init() {
    pinMode(RELAY1_PIN, OUTPUT);
    pinMode(RELAY2_PIN, OUTPUT);

    // Výchozí stav: obě relé vypnutá (bezpečnost)
    digitalWrite(RELAY1_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    digitalWrite(RELAY2_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    g_relay.actual[0] = false;
    g_relay.actual[1] = false;
    g_relay.reason = RelayState::NONE;

    g_virivka_state = VS_OFF;
    g_virivka_enabled = false;
    s_safety_active = false;

    Serial.println(F("Relé: inicializováno (obě OFF)."));
}

void relay_control_loop() {
    unsigned long now = millis();
    RelayState::Reason override_reason = check_override_reason();
    bool override = (override_reason != RelayState::NONE);

    // =====================================================================
    // PRIORITA 1: SAFETY — MQTT timeout → okamžité vypnutí všeho
    // =====================================================================
    if (is_mqtt_timeout(now)) {
        if (!s_safety_active) {
            Serial.println(F("Relé: SAFETY OFF — MQTT timeout!"));
            relay_emergency_off();
            s_safety_active = true;
        }
        return; // Dokud trvá safety, nic jiného neděláme
    }
    if (is_menic_timeout(now)) {
        if (!s_menic_stale_active) {
            Serial.println(F("Relé: SAFETY OFF — Vypadek dat menice!"));
            g_relay.reason = RelayState::MENIC_STALE;
            relay_emergency_off();
            s_menic_stale_active = true;
        }
        return;
    }

    // Reset safety příznaků
    if (s_safety_active) {
        s_safety_active = false;
        // Důvod se nastaví v příštím průchodu stavovým automatem
        Serial.println(F("Relé: SAFETY zrušeno — MQTT obnoveno."));
    }
    if (s_menic_stale_active) {
        s_menic_stale_active = false;
        g_relay.reason = RelayState::NONE;
        Serial.println(F("Relé: SAFETY zrušeno — data menice obnovena."));
    }

    // =====================================================================
    // STAVOVÝ AUTOMAT
    // =====================================================================

    switch (g_virivka_state) {

        // -----------------------------------------------------------------
        case VS_OFF:
            // Obě relé OFF. Přechod do STARTING pouze pokud:
            // - OPI dalo povel enabled=true
            // - Není aktivní override
            // - Není safety timeout
            if (g_virivka_enabled && !override) {
                set_relay(0, true);          // zapni relé1
                g_virivka_state = VS_STARTING;
                s_state_timer = now;
                g_relay.reason = RelayState::MQTT_ON;
                Serial.println(F("Relé: STARTING — relé1 ON, čekám 2.5s na relé2..."));
            } else if (g_virivka_enabled && override) {
                // OPI chce zapnout, ale override blokuje
                g_relay.reason = override_reason;
            } else {
                // OPI dalo enabled=false — normální stav v klidu
                g_relay.reason = RelayState::MQTT_OFF;
            }
            break;

        // -----------------------------------------------------------------
        case VS_STARTING:
            // Relé1 ON, relé2 OFF. Čekáme RELAY_STEP_ON_MS.
            // Může být přerušeno: enabled=false nebo override → STOPPING
            if (!g_virivka_enabled || override) {
                set_relay(0, false);         // vypni relé1
                set_relay(1, false);         // vypni i relé2 (pro jistotu)
                g_virivka_state = VS_STOPPING;
                s_state_timer = now;
                g_relay.reason = override ? override_reason : RelayState::MQTT_OFF;
                Serial.println(F("Relé: STARTING přerušeno → STOPPING."));
            } else if (now - s_state_timer >= RELAY_STEP_ON_MS) {
                set_relay(1, true);          // zapni relé2
                g_virivka_state = VS_ACTIVE;
                Serial.println(F("Relé: ACTIVE — obě relé ON, hlídám override."));
            }
            break;

        // -----------------------------------------------------------------
        case VS_ACTIVE:
            // Obě relé ON. Hlídáme:
            // - enabled=false → STOPPING (normální vypnutí)
            // - override → OVERRIDE_CHECK (ochrana)
            if (!g_virivka_enabled) {
                set_relay(0, false);         // vypni relé1
                g_virivka_state = VS_STOPPING;
                s_state_timer = now;
                g_relay.reason = RelayState::MQTT_OFF;
                Serial.println(F("Relé: STOPPING — vypínám ohřev."));
            } else if (override) {
                set_relay(0, false);         // vypni relé1 (relé2 zatím nech)
                g_virivka_state = VS_OVERRIDE_CHECK;
                s_state_timer = now;
                g_relay.reason = override_reason;
                Serial.println(F("Relé: OVERRIDE — relé1 OFF, za 4s kontrola..."));
            }
            break;

        // -----------------------------------------------------------------
        case VS_STOPPING:
            // Relé1 OFF, relé2 ještě ON. Čekáme RELAY_STEP_OFF_MS.
            if (now - s_state_timer >= RELAY_STEP_OFF_MS) {
                set_relay(1, false);         // vypni relé2
                g_virivka_state = VS_OFF;
                g_relay.reason = RelayState::MQTT_OFF;
                Serial.println(F("Relé: OFF — obě relé vypnuta."));
            }
            // Během STOPPING může přijít nový enabled=true — ale necháme
            // vypnutí dokončit. Nový STARTING přijde až v dalším cyklu z VS_OFF.
            break;

        // -----------------------------------------------------------------
        case VS_OVERRIDE_CHECK:
            // Relé1 OFF (kvůli override), relé2 ještě ON.
            // Čekáme OVERRIDE_RECHECK_MS, pak zkontrolujeme podmínky.
            if (!g_virivka_enabled) {
                // Uživatel vypnul — dokončíme vypnutí
                set_relay(1, false);         // vypni relé2
                g_virivka_state = VS_OFF;
                g_relay.reason = RelayState::MQTT_OFF;
                Serial.println(F("Relé: OVERRIDE_CHECK → OFF (enabled=false)."));
            } else if (now - s_state_timer >= OVERRIDE_RECHECK_MS) {
                if (override) {
                    // Stále překročeno → vypnout i relé2
                    set_relay(1, false);
                    g_virivka_state = VS_OFF;
                    Serial.println(F("Relé: OVERRIDE — stále překročeno, obě OFF."));
                } else {
                    // Překročení pominulo → obnovit relé1 (relé2 běží)
                    set_relay(0, true);
                    g_virivka_state = VS_ACTIVE;
                    g_relay.reason = RelayState::MQTT_ON;
                    Serial.println(F("Relé: OVERRIDE zrušeno → ACTIVE."));
                }
            }
            break;
    }
}

void relay_emergency_off() {
    // Okamžité vypnutí obou relé — bezpečnostní funkce
    digitalWrite(RELAY1_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    digitalWrite(RELAY2_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    g_relay.actual[0] = false;
    g_relay.actual[1] = false;
    g_virivka_state = VS_OFF;
    g_relay.reason = RelayState::SAFETY_OFF;
    s_safety_active = true;
}

const char* relay_reason_str() {
    switch (g_relay.reason) {
        case RelayState::MQTT_ON:        return "Zapnuto rizenim";
        case RelayState::MQTT_OFF:       return "Vypnuto rizenim";
        case RelayState::OVERRIDE_POWER: return "Pretizeny menic";
        case RelayState::OVERRIDE_BAT:   return "Zatez baterie";
        case RelayState::SAFETY_OFF:     return "Ztrata spojeni";
        case RelayState::MENIC_STALE:    return "Vypadek dat menice";
        default:                         return "Inicializace...";
    }
}
