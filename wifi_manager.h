/*
 * ============================================================================
 * wifi_manager.h — WIFI PŘIPOJENÍ S DUÁLNÍ SÍTÍ A CAPTIVE PORTÁLEM
 * ============================================================================
 *
 * ÚČEL:
 *   - Připojení k WiFi (primární SSID, při výpadku záložní SSID)
 *   - Captive portal pro prvotní konfiguraci (vlastní AP: ESP32-virivka)
 *   - Detekce dlouhého stisku tlačítka (5s) → vstup do konfigurace
 *   - Ukládání WiFi údajů do Preferences
 *   - Signalizace stavu přes LED:
 *       - rychlé blikání: připojování
 *       - 1 blik / 2s:   připojeno
 *       - 3 blik / 1s:   konfigurační režim
 *
 * VAZBY:
 *   - Používá: config.h (piny), variables.h (nastavení), Preferences
 *   - Nastavuje: WiFi (ESP32), DNSServer (captive portal), ESPAsyncWebServer
 *   - Modifikuje: g_wifi_config_mode, g_settings (wifi1/wifi2)
 * ============================================================================
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <DNSServer.h>

// DNS server pro captive portal
extern DNSServer g_dns_server;

/*
 * Inicializace WiFi modulu.
 * - Nastaví LED pin
 * - Načte uložené WiFi údaje z Preferences (pokud existují)
 * - Pokud nejsou uložené → spustí captive portal
 * - Pokud jsou → pokusí se připojit k primární, pak záložní síti
 */
void wifi_manager_init();

/*
 * Hlavní smyčka WiFi modulu — volá se v loop().
 * - Obsluhuje DNS server (captive portal)
 * - Kontroluje stav připojení, případně přepíná na záložní síť
 * - Detekuje dlouhý stisk tlačítka → vstup do konfigurace
 * - Bliká LED podle stavu
 */
void wifi_manager_loop();

/*
 * Spustí WiFi konfigurační režim.
 * - Vytvoří vlastní AP: ESP32-virivka
 * - Spustí DNS server pro captive portal
 * - Zobrazí konfigurační webovou stránku
 */
void wifi_start_config_portal();

#endif // WIFI_MANAGER_H
