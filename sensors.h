/*
 * ============================================================================
 * sensors.h — ČTENÍ SENZORŮ (DS18B20 + SCT013)
 * ============================================================================
 *
 * ÚČEL:
 *   - Čtení teploty z DS18B20 (OneWire, dedikovaný pin)
 *   - DS18B20 adresa se ukládá do Preferences → nemusí se hledat při každém startu
 *   - Čtení proudu ze SCT013 20A/1V (interní ADC ESP32, RMS výpočet)
 *   - Detekce chyb senzorů (odpojeno, mimo rozsah)
 *
 * VAZBY:
 *   - Používá: DallasTemperature, OneWire knihovny
 *   - Čte: ADC pin (SCT013), OneWire pin (DS18B20)
 *   - Zapisuje: g_sensors (proud, teplota, proud_error, teplota_error)
 *   - Ukládá/načítá adresu DS18B20 z Preferences ("ds18_addr", 8 bajtů)
 *   - Voláno z: loop() v hlavním .ino (každých SENSOR_READ_MS)
 * ============================================================================
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <OneWire.h>
#include <DallasTemperature.h>

// OneWire a DallasTemperature instance (potřebují být globální kvůli knihovně)
extern OneWire g_onewire;
extern DallasTemperature g_dallas;

/*
 * Inicializace senzorů.
 * - Nastaví ADC pin a změří kalibrační midpoint
 * - DS18B20: načte uloženou adresu z Preferences, ověří připojení.
 *   Není-li adresa, pokusí se o automatické vyhledání.
 */
void sensors_init();

/*
 * Skenování OneWire sběrnice — najde všechna DS18B20 čidla,
 * vypíše jejich adresy a první nalezené uloží do Preferences.
 * Lze volat ručně pro diagnostiku (např. přes sériovou konzoli).
 */
void sensors_scan();

/*
 * Přečte všechny senzory a uloží hodnoty do g_sensors.
 * Volá se periodicky (SENSOR_READ_MS).
 */
void sensors_read();

/*
 * Vypočítá RMS proud z ADC vzorků.
 * SCT013 dává střídavé napětí → používáme vzorkování přes několik period 50Hz.
 * @return RMS proud v ampérech
 */
float read_current_rms();

#endif // SENSORS_H
