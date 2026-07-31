/*
 * ============================================================================
 * wifi_manager.cpp — IMPLEMENTACE WIFI PŘIPOJENÍ S DUÁLNÍ SÍTÍ
 * ============================================================================
 */

#include "variables.h"
#include "wifi_manager.h"
#include <DNSServer.h>
#include <WebServer.h>

// ============================================================================
// STATICKÉ PROMĚNNÉ MODULU
// ============================================================================

// [souborová] DNS server — přesměrovává všechny dotazy na ESP IP (captive portal)
DNSServer g_dns_server;

// [souborová] Web server pro konfigurační stránku (jen při config režimu)
static WebServer* s_config_server = nullptr;

// [souborová] Stav WiFi
static enum { WIFI_DISCONNECTED, WIFI_CONNECTING, WIFI_CONNECTED } s_wifi_state = WIFI_DISCONNECTED;

// [souborová] Která síť je právě aktivní (0=primární, 1=záložní, -1=žádná)
static int8_t s_active_network = -1;

// [souborová] Časovač pro pokus o reconnect
static unsigned long s_reconnect_timer = 0;

// [souborová] Stav tlačítka pro detekci dlouhého stisku
static unsigned long s_button_press_start = 0;
static bool s_button_was_pressed = false;

// [souborová] LED blikání — kratky zablesk
#define LED_FLASH_MS 20
static unsigned long s_led_timer = 0;
static enum { LED_WAIT, LED_FLASH } s_led_phase = LED_WAIT;

// [souborová] Příznak: konfigurační web byl již odeslán
static bool s_portal_active = false;

// ============================================================================
// INTERNÍ FUNKCE
// ============================================================================

/*
 * Nastaví LED na desce.
 * @param on true = svítí (dle LED_ACTIVE_HIGH), false = zhasne
 */
static void set_led(bool on) {
    bool level = LED_ACTIVE_HIGH ? on : !on;
    digitalWrite(LED_PIN, level);
}

/*
 * Blikání LED podle stavu WiFi.
 * Volá se v každé smyčce loop().
 */
static void led_blink() {
    unsigned long now = millis();
    int interval;

    switch (s_wifi_state) {
        case WIFI_CONNECTING: interval = 250;  break;
        case WIFI_CONNECTED:  interval = 2500; break;
        default:              interval = 3000; break;
    }

    if (g_wifi_config_mode) interval = 150;

    switch (s_led_phase) {
        case LED_WAIT:
            if (now - s_led_timer > interval) {
                s_led_timer = now;
                s_led_phase = LED_FLASH;
                set_led(true);
            }
            break;
        case LED_FLASH:
            if (now - s_led_timer > LED_FLASH_MS) {
                set_led(false);
                s_led_timer = now;
                s_led_phase = LED_WAIT;
            }
            break;
    }
}

/*
 * Připojí se k zadané WiFi síti.
 * @return true = připojeno, false = selhalo
 */
static bool connect_wifi(const char* ssid, const char* password) {
    if (strlen(ssid) == 0) return false;

    Serial.print(F("WiFi: připojuji k "));
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(F("."));
        led_blink();
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print(F("WiFi: připojeno! IP: "));
        Serial.println(WiFi.localIP());
        s_wifi_state = WIFI_CONNECTED;
        return true;
    }

    Serial.println(F(" FAIL"));
    return false;
}

/*
 * HTML stránka pro konfiguraci WiFi (captive portal).
 */
static String get_config_html() {
    String html = F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8' name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Vířivka — WiFi nastavení</title>"
        "<style>"
        "body{font-family:Arial;max-width:500px;margin:20px auto;padding:15px;background:#1a1a2e;color:#eee}"
        "h2{color:#e94560;text-align:center}"
        "label{display:block;margin-top:12px;color:#ccc;font-size:14px}"
        "input{width:100%;padding:10px;margin-top:4px;border:1px solid #333;border-radius:5px;"
        "background:#16213e;color:#eee;font-size:16px;box-sizing:border-box}"
        "input[type=submit]{background:#e94560;color:#fff;border:none;padding:12px;margin-top:20px;"
        "font-size:16px;cursor:pointer}"
        "fieldset{border:1px solid #333;border-radius:8px;padding:15px;margin-top:15px}"
        "legend{color:#e94560;font-weight:bold}"
        ".note{font-size:12px;color:#888;margin-top:4px}"
        "</style></head><body>"
        "<h2>⚙️ ESP32 Vířivka</h2>"
        "<p>Nastavení WiFi připojení. Zadej až dvě sítě — primární a záložní.</p>"
        "<form method='POST' action='/save'>"
        "<fieldset><legend>🔵 Primární síť</legend>"
        "<label>SSID:</label><input name='w1s' maxlength='31' required>"
        "<label>Heslo:</label><input name='w1p' type='password' maxlength='63'>"
        "</fieldset>"
        "<fieldset><legend>🟠 Záložní síť (volitelná)</legend>"
        "<label>SSID:</label><input name='w2s' maxlength='31'>"
        "<label>Heslo:</label><input name='w2p' type='password' maxlength='63'>"
        "<p class='note'>Nech prázdné, pokud nechceš záložní síť.</p>"
        "</fieldset>"
        "<input type='submit' value='💾 Uložit a restartovat'>"
        "</form></body></html>"
    );
    return html;
}

/*
 * Zpracování POST požadavku — uložení WiFi údajů.
 */
static void handle_config_save() {
    if (!s_config_server) return;

    String w1s = s_config_server->arg("w1s");
    String w1p = s_config_server->arg("w1p");
    String w2s = s_config_server->arg("w2s");
    String w2p = s_config_server->arg("w2p");

    // Uložení do globálního nastavení a Preferences
    strncpy(g_settings.wifi1_ssid, w1s.c_str(), 31);
    strncpy(g_settings.wifi1_pass, w1p.c_str(), 63);
    strncpy(g_settings.wifi2_ssid, w2s.c_str(), 31);
    strncpy(g_settings.wifi2_pass, w2p.c_str(), 63);

    g_prefs.putString("w1_ssid", w1s);
    g_prefs.putString("w1_pass", w1p);
    g_prefs.putString("w2_ssid", w2s);
    g_prefs.putString("w2_pass", w2p);

    String html = F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8' name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Uloženo</title>"
        "<style>"
        "body{font-family:Arial;max-width:400px;margin:50px auto;text-align:center;"
        "background:#1a1a2e;color:#eee}"
        "h2{color:#4ecca3}"
        "</style></head><body>"
        "<h2>✅ Uloženo!</h2>"
        "<p>ESP32 se restartuje a připojí k nové síti.</p>"
        "<p>Tato stránka se za 5 sekund zavře.</p>"
        "</body></html>"
    );

    s_config_server->send(200, "text/html; charset=utf-8", html);

    // Restart po 2 sekundách
    delay(2000);
    ESP.restart();
}

/*
 * Zachytí všechny ostatní požadavky → přesměruje na config stránku.
 */
static void handle_captive_portal() {
    if (!s_config_server) return;
    s_config_server->send(200, "text/html; charset=utf-8", get_config_html());
}

// ============================================================================
// VEŘEJNÉ FUNKCE
// ============================================================================

void wifi_start_config_portal() {
    Serial.println(F("WiFi: spouštím konfigurační režim..."));
    g_wifi_config_mode = true;

    // Vypneme STA, zapneme AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-virivka", nullptr);  // otevřená síť

    Serial.print(F("WiFi AP: ESP32-virivka, IP: "));
    Serial.println(WiFi.softAPIP());

    // DNS server — všechny dotazy na ESP IP
    g_dns_server.start(53, "*", WiFi.softAPIP());

    // Web server pro config stránku
    if (s_config_server) {
        delete s_config_server;
    }
    s_config_server = new WebServer(80);

    s_config_server->onNotFound(handle_captive_portal);
    s_config_server->on("/save", HTTP_POST, handle_config_save);
    s_config_server->on("/", HTTP_GET, handle_captive_portal);

    s_config_server->begin();
    s_portal_active = true;

    Serial.println(F("WiFi: konfigurační portál spuštěn."));
    Serial.println(F("       Připoj se k WiFi 'ESP32-virivka' a otevři libovolnou stránku."));
}

void wifi_manager_init() {
    pinMode(LED_PIN, OUTPUT);
    set_led(false);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Načtení uložených WiFi údajů
    String w1s = g_prefs.getString("w1_ssid", "");
    String w1p = g_prefs.getString("w1_pass", "");
    String w2s = g_prefs.getString("w2_ssid", "");
    String w2p = g_prefs.getString("w2_pass", "");

    strncpy(g_settings.wifi1_ssid, w1s.c_str(), 31);
    strncpy(g_settings.wifi1_pass, w1p.c_str(), 63);
    strncpy(g_settings.wifi2_ssid, w2s.c_str(), 31);
    strncpy(g_settings.wifi2_pass, w2p.c_str(), 63);

    // Pokud nemáme uložené WiFi → jdeme do config režimu
    if (strlen(g_settings.wifi1_ssid) == 0) {
        Serial.println(F("WiFi: žádné uložené údaje — spouštím konfigurační portál."));
        wifi_start_config_portal();
        return;
    }

    // Pokus o připojení
    s_wifi_state = WIFI_CONNECTING;
    if (connect_wifi(g_settings.wifi1_ssid, g_settings.wifi1_pass)) {
        s_active_network = 0;
    } else if (strlen(g_settings.wifi2_ssid) > 0) {
        Serial.println(F("WiFi: primární selhalo, zkouším záložní..."));
        if (connect_wifi(g_settings.wifi2_ssid, g_settings.wifi2_pass)) {
            s_active_network = 1;
        }
    }

    if (s_wifi_state != WIFI_CONNECTED) {
        Serial.println(F("WiFi: obě sítě selhaly — spouštím konfigurační portál."));
        wifi_start_config_portal();
    }
}

void wifi_manager_loop() {
    unsigned long now = millis();

    // Obsluha konfiguračního portálu (pokud aktivní)
    if (s_portal_active && s_config_server) {
        g_dns_server.processNextRequest();
        s_config_server->handleClient();
    }

    // Detekce dlouhého stisku tlačítka (5s)
    bool button_now = (digitalRead(BUTTON_PIN) == LOW);  // LOW = stisk

    if (button_now && !s_button_was_pressed) {
        s_button_press_start = now;
    } else if (button_now && s_button_was_pressed) {
        if (now - s_button_press_start > BUTTON_LONG_PRESS_MS && !g_wifi_config_mode) {
            Serial.println(F("WiFi: dlouhý stisk tlačítka — spouštím konfigurační portál."));
            wifi_start_config_portal();
        }
    }
    s_button_was_pressed = button_now;

    // Kontrola WiFi připojení — pokud vypadlo, zkus reconnect
    if (!g_wifi_config_mode && s_wifi_state == WIFI_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi: spojení ztraceno!"));
            s_wifi_state = WIFI_DISCONNECTED;
            s_reconnect_timer = now;
        }
    }

    // Reconnect logika
    if (!g_wifi_config_mode && s_wifi_state == WIFI_DISCONNECTED) {
        if (now - s_reconnect_timer > 10000) { // každých 10s
            s_reconnect_timer = now;
            s_wifi_state = WIFI_CONNECTING;

            // Zkusíme aktuální nebo opačnou síť
            if (s_active_network == 0 && strlen(g_settings.wifi2_ssid) > 0) {
                Serial.println(F("WiFi: zkouším záložní síť..."));
                if (connect_wifi(g_settings.wifi2_ssid, g_settings.wifi2_pass)) {
                    s_active_network = 1;
                    return;
                }
            }

            Serial.println(F("WiFi: zkouším primární síť..."));
            if (connect_wifi(g_settings.wifi1_ssid, g_settings.wifi1_pass)) {
                s_active_network = 0;
                return;
            }

            s_wifi_state = WIFI_DISCONNECTED;
            Serial.println(F("WiFi: obě sítě nedostupné."));
        }
    }

    // LED signalizace
    led_blink();
}
