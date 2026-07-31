# ESP32 — ovládání vířivky (2× relé)

Řízení ohřevu vířivky (2300 W) jako vytěžování přebytků FVE.
Dvoureléový modul spíná dvě topné spirály podle povelů z nadřazeného systému přes MQTT
s vlastní ochranou měniče a baterie.

## Hardware
| Součást | Popis |
|---------|-------|
| **Modul** | ESP32-32E N4 |
| **Deska** | ESP32_Relay_AC_X2 303E32AC210 |
| **Relé** | 2× 10 A (GPIO16, GPIO17, active HIGH) |
| **Čidla** | DS18B20 (teplota vody, GPIO33), SCT013 20A/1V (proud, GPIO34/ADC) |
| **LED** | GPIO23 (active HIGH) |
| **Tlačítko** | GPIO0 (dlouhý stisk 5s = WiFi konfigurace) |

## Funkce
- **WiFi** s duální sítí (primární + záložní), captive portal
- **MQTT** — povely ON/OFF, stav relé, data ze senzorů
- **Ochrany** — měnič (výkonový limit), baterie (vybíjecí proud)
- **Senzory** — měření proudu (SCT013, RMS), teplota vody (DS18B20)
- **Stavový automat** — postupné spínání/vypínání dvou relé
- **Webové rozhraní** — AJAX, nastavení parametrů, diagnostika
- **OTA** — bezdrátové nahrávání firmwaru

## MQTT topicy
| Topic | Směr | Obsah |
|-------|------|-------|
| `fve/spotrebice/virivka/set` | OPI → ESP | `{"enabled":true/false}` |
| `fve/spotrebice/virivka/stav` | ESP → OPI | `{"status":"ZAP"/"OFF","vystup1":0/1,"vystup2":0/1,"proud0":12.5,"teplota":28.3}` |
| `fve/spotrebice/virivka/status` | ESP → MQTT | `{"status":"online"/"offline"}` (Last Will) |
| `menic/1/data` | OPI → MQTT | data z měniče pro ochrany |

## Priorita řízení relé
1. **SAFETY** — výpadek MQTT → okamžité OFF
2. **OVERRIDE výkon** — překročen limit měniče → postupné OFF
3. **OVERRIDE baterie** — překročen vybíjecí proud → postupné OFF
4. **MQTT příkaz** — normální režim podle `enabled`

## Soubory
| Soubor | Účel |
|--------|------|
| `FVE_ovladani_virivka_ESP32.ino` | Hlavní soubor, setup/loop, OTA |
| `config.h` | Piny, MQTT topicy, konstanty |
| `variables.h` | Globální proměnné a struktury |
| `wifi_manager.cpp/h` | WiFi, duální síť, captive portal |
| `mqtt_handler.cpp/h` | MQTT, JSON, publish/subscribe |
| `relay_control.cpp/h` | Stavový automat, vyhodnocení ochran |
| `sensors.cpp/h` | DS18B20 + SCT013 ADC |
| `web_setup.cpp/h` | Webové rozhraní s AJAX pollingem |

## Knihovny (Arduino IDE)
`PubSubClient` `ArduinoJson` `DallasTemperature` `OneWire` `ArduinoOTA` `DNSServer` `WebServer` `WiFi` `Preferences`

## Použití
Volné dílo — dělej si s tím co chceš.
