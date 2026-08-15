/*
 * ============================================================================
 * relay_control.cpp — CHYTRÁ REGULACE S VÝPOČTEM REZERVY (v3.1.0)
 * ============================================================================
 *
 * NC ZAPOJENÍ:
 *   - Relé OFF (de-energized) = NC sepnuto = TOPÍ
 *   - Relé ON  (energized)    = NC rozepnuto = NETOPÍ
 *
 * PRINCIP:
 *   ESP zná aktuální odběr z baterie i výkon měniče (MQTT).
 *   Místo "testování" dopočítá, jestli je dostatečná rezerva pro
 *   připojení spirály — s 20% bezpečnostní marží.
 *
 *   spirála = power_virivka W (~1150W)
 *   odběr spirály z bat = power_virivka / battery_voltage [A]
 *   efektivní max = vybijeni_bat * 0.8 (20% rezerva)
 *   volná kapacita = efektivní max - battery_discharge_current
 *
 *   Pokud volná kapacita >= odběr spirály → lze topit
 *   Jinak → override (netopit, chránit baterii)
 *
 * PRIORITY:
 *   SAFETY (MQTT timeout) > HEADROOM > OPI_COMMAND
 *
 * WARMUP: 3 minuty po prvním zapnutí — override blokován
 * ============================================================================
 */

#include "variables.h"
#include "relay_control.h"

static unsigned long s_warmup_start = 0;
static bool s_warmup_active = false;
static bool s_safety_active = false;

// ============================================================================
// INTERNÍ
// ============================================================================

static void set_relay(int index, bool topit) {
    if (g_relay.actual[index] == topit) return;
    bool physical = !topit;  // NC: topit = relé OFF
    bool level = RELAY_ACTIVE_HIGH ? physical : !physical;
    int pin = (index == 0) ? RELAY1_PIN : RELAY2_PIN;
    digitalWrite(pin, level);
    g_relay.actual[index] = topit;
    Serial.print(F("Relé ")); Serial.print(index + 1);
    Serial.print(F(" → "));
    Serial.print(topit ? F("TOPÍ") : F("NETOPÍ"));
    Serial.println();
}

static bool mqtt_timeout(unsigned long now) {
    return (now - g_last_mqtt_msg_ms) > (g_settings.mqtt_timeout * 1000UL);
}

/*
 * Spočítá volnou kapacitu na baterii [A] a na měniči [W].
 * Vrací true pokud je dost místa pro spirálu o výkonu spiral_w.
 */
static bool has_headroom(float spiral_w) {
    float bat_v = (g_menic.last_update_ms > 0) ? g_menic.battery_voltage : 25.0f;
    if (bat_v < 1.0f) bat_v = 25.0f;

    // Baterie: 20% rezerva
    if (g_menic.last_update_ms > 0 && g_settings.vybijeni_bat > 0) {
        float efektivni_max = g_settings.vybijeni_bat * 0.8f;
        float spiral_bat_a = spiral_w / bat_v;
        float volna = efektivni_max - g_menic.battery_discharge_current;
        if (volna < spiral_bat_a) return false;
    }

    // Měnič 2: přesně dle nastaveného maxima (měnič stavěn na plný výkon)
    if (g_menic2.last_update_ms > 0 && g_settings.max_vykon > 0) {
        float volna = g_settings.max_vykon - g_menic2.output_apparent_power;
        if (volna < spiral_w) return false;
    }

    return true;
}

/*
 * Vypočítá kolik spirál může běžet (0, 1, nebo 2).
 * Bere v úvahu už běžící spirály.
 */
static int max_spirals_allowed() {
    float spiral_w = SPIRALA_W;  // výkon jedné spirály

    // Kolik spirál už běží?
    int running = (g_relay.actual[0] ? 1 : 0) + (g_relay.actual[1] ? 1 : 0);

    // Zkus přidat další spirálu
    float total_w = (running + 1) * spiral_w;
    if (has_headroom(total_w)) return running + 1;

    // Zkus současný počet
    total_w = running * spiral_w;
    if (running > 0 && has_headroom(total_w)) return running;

    // Ani jedna?
    if (has_headroom(0)) return 0;

    return 0;  // nic nemůže běžet
}

// ============================================================================
// VEŘEJNÉ
// ============================================================================

void relay_control_init() {
    pinMode(RELAY1_PIN, OUTPUT);
    pinMode(RELAY2_PIN, OUTPUT);
    digitalWrite(RELAY1_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    digitalWrite(RELAY2_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    g_relay.actual[0] = true;   // NC: TOPÍ
    g_relay.actual[1] = true;
    g_relay.reason = RelayState::NONE;
    s_safety_active = false;
    s_warmup_active = false;
    Serial.println(F("Relé: chytrá regulace (NC, 20% rezerva)."));
}

void relay_control_loop() {
    unsigned long now = millis();

    // ===== SAFETY: MQTT timeout → relé OFF = TOPÍ (bezpečné) =====
    if (mqtt_timeout(now)) {
        if (!s_safety_active) {
            relay_emergency_off();
            s_safety_active = true;
            Serial.println(F("Relé: SAFETY — MQTT timeout."));
        }
        return;
    }
    s_safety_active = false;

    // ===== WARMUP =====
    bool any_heating = g_opi_relay1 || g_opi_relay2;
    if (any_heating && !s_warmup_active) {
        bool was_off = !g_relay.actual[0] && !g_relay.actual[1];
        if (was_off) {
            s_warmup_start = now;
            s_warmup_active = true;
            Serial.println(F("Relé: WARMUP 3min."));
        }
    }
    if (!any_heating) s_warmup_active = false;
    if (s_warmup_active && (now - s_warmup_start >= (WARMUP_S * 1000UL))) {
        s_warmup_active = false;
    }

    // ===== VÝPOČET KOLIK SPIRÁL MŮŽE BĚŽET =====
    int allowed = max_spirals_allowed();

    // OPI chce topit, ALE omezeno dostupnou kapacitou
    bool target1 = g_opi_relay1 && (allowed >= 1);
    bool target2 = g_opi_relay2 && (allowed >= 2);

    // Během warmupu: ignorovat limit (topit vždy když OPI chce)
    if (s_warmup_active) {
        target1 = g_opi_relay1;
        target2 = g_opi_relay2;
        g_relay.reason = RelayState::WARMUP;
    } else if (g_opi_relay1 && !target1) {
        g_relay.reason = RelayState::OVERRIDE_BAT;  // nedostatek kapacity
    } else if (g_opi_relay2 && !target2 && target1) {
        g_relay.reason = RelayState::OVERRIDE_BAT;
    } else if (target1 || target2) {
        g_relay.reason = RelayState::MQTT_ON;
    } else {
        g_relay.reason = RelayState::MQTT_OFF;
    }

    // Aplikuj
    set_relay(0, target1);
    set_relay(1, target2);
}

void relay_emergency_off() {
    digitalWrite(RELAY1_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    digitalWrite(RELAY2_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    g_relay.actual[0] = true;
    g_relay.actual[1] = true;
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
        case RelayState::WARMUP:         return "Nabeh virivky";
        default:                         return "Inicializace...";
    }
}
