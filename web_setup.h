/*
 * ============================================================================
 * web_setup.h — WEBOVÁ STRÁNKA NASTAVENÍ
 * ============================================================================
 *
 * ÚČEL:
 *   - Webová stránka pro zobrazení a změnu provozních parametrů
 *   - Dostupná na IP adrese ESP32 po připojení k domácí síti
 *   - Stránka je veřejně viditelná (bez hesla)
 *   - HESLO je vyžadováno POUZE při ukládání změn (POST /save)
 *   - Heslo se ověřuje na serveru (ne v JavaScriptu!)
 *
 * NASTAVITELNÉ PARAMETRY:
 *   - max_vykon:     max. výkon měniče před override [W]
 *   - vybijeni_bat:  max. vybíjecí proud baterie [A]
 *   - mqtt_timeout:  timeout výpadku MQTT [s]
 *   - override_delay: prodleva mezi vypínáním relé [s]
 *   - web_password:  heslo pro ukládání nastavení
 *
 * ZOBRAZOVANÉ INFORMACE:
 *   - Stav WiFi (SSID, IP, síla signálu)
 *   - Stav MQTT (připojeno/odpojeno)
 *   - Stav relé (ON/OFF, důvod)
 *   - Hodnoty senzorů (teplota, proud)
 *   - Data z měniče (výkon, vybíjení)
 *   - Verze firmwaru
 *
 * VAZBY:
 *   - Používá: ESPAsyncWebServer (nebo WebServer), Preferences
 *   - Čte: všechny globální proměnné (stav)
 *   - Zapisuje: g_settings (přes POST /save)
 *   - Voláno z: loop() v hlavním .ino
 * ============================================================================
 */

#ifndef WEB_SETUP_H
#define WEB_SETUP_H

/*
 * Inicializace webového serveru pro nastavení.
 * - Vytvoří endpointy (GET /, POST /save)
 * - Spustí server na portu WEB_PORT
 */
void web_setup_init();

/*
 * Obsluha webového serveru — volá se v loop().
 * (Při použití asynchronního serveru může být prázdná)
 */
void web_setup_loop();

#endif // WEB_SETUP_H
