# ESP32 — ovládání ohřevu vířivky (FV vytěžování)

## Popis zařízení

### Účel

Zařízení slouží k **řízení ohřevu vířivky** jako způsobu vytěžování přebytků z fotovoltaické
elektrárny. Dvě topné spirály (každá ~2–3 kW) jsou spínány relé podle povelů z nadřazeného
systému (OrangePi PC2) přes MQTT. Zařízení zároveň chrání měnič před přetížením a baterii
před nadměrným vybíjením — při překročení nastavených limitů samo odpojí relé bez ohledu
na povely z MQTT.

### Hardware

| Součást | Popis |
|---------|-------|
| **Řídicí modul** | ESP32-32E N4, FCCID: 2BB787-ESP32-32X |
| **Deska** | `ESP32_Relay_AC_X2 303E32AC210` — dvoureléový modul s palubním spínaným zdrojem |
| **Napájení** | AC 230 V (palubní spínaný zdroj) |
| **Relé** | 2× 10 A (COM/NO/NC), spínaná při HIGH |
| **Relé 1** | GPIO16 — vnější relé na desce |
| **Relé 2** | GPIO17 — vnitřní relé na desce |
| **LED** | GPIO23 — svítí při HIGH |
| **Tlačítko** | GPIO0 — stisk = LOW (INPUT_PULLUP), dlouhý stisk (5 s) vstup do WiFi konfigurace |
| **Proudový senzor** | SCT013 20A/1V, interní ADC ESP32 (GPIO34) s externím bias děličem |
| **Teplotní senzor** | DS18B20, OneWire (GPIO33), vodotěsné provedení, pull-up 4,7 kΩ na 3,3 V |
| **Programování** | 6pin header (GND/TX/RX/3V3/IO0/GND), USB-UART převodník (CP2102N doporučen) |
| **OTA** | ArduinoOTA, hostname `ESP32-virivka`, port 3232 |

### Blokové schéma

```
┌──────────────────────────────────────────────────────┐
│                  ESP32_Relay_AC_X2                    │
│                                                      │
│  AC 230V ──► Spínaný zdroj ──► 3.3V ──► ESP32-32E   │
│                                                      │
│  SCT013 ──► bias dělič ──► GPIO34 (ADC)             │
│  DS18B20 ──────────────► GPIO33 (OneWire)            │
│                                                      │
│  GPIO16 ──► Relé 1 ──► Topná spirála 1              │
│  GPIO17 ──► Relé 2 ──► Topná spirála 2              │
│  GPIO23 ──► LED                                      │
│  GPIO0  ◄── Tlačítko                                 │
│                                                      │
│  WiFi ◄──► MQTT broker (192.168.0.191:1883)         │
└──────────────────────────────────────────────────────┘
```

### Komunikace

| Směr | Topic MQTT | Obsah |
|------|-----------|-------|
| OPI → ESP | `fve/spotrebice/virivka/set` | `enabled` (true = zapnout, false = vypnout) |
| OPI → ESP | `menic/1/data` | `output_apparent_power`, `battery_discharge_current` (baterie) |
| OPI → ESP | `menic/2/data` | `output_apparent_power` (výkon — budoucí) |
| ESP → MQTT | `spinac/VIRIVKA_OHREV/stav` | `status`, `vystup1`, `vystup2`, `proud0`, `teplota` |
| ESP → MQTT | `spinac/VIRIVKA_OHREV/status` | `status` (online/offline — Last Will) |

### Princip činnosti

ESP32 se připojí k domácí WiFi síti a k MQTT brokeru na OrangePi PC2
(192.168.0.191:1883, bez autentizace). Naslouchá na dvou topicích:

1. **`fve/spotrebice/virivka/set`** — řídicí povely z OPI. JSON s klíčem `enabled`
   (true = zapnout ohřev, false = vypnout). Na základě tohoto povelu spouští ESP32
   stavový automat zapínání/vypínání relé.

2. **`menic/1/data`** — data z měniče 1 (baterie). ESP čte `battery_discharge_current`
   (vybíjecí proud baterie) pro ochranu proti nadměrnému vybíjení.

3. **`menic/2/data`** — data z měniče 2 (budoucí). ESP čte `output_apparent_power`
   (výkon měniče 2) pro ochranu proti přetížení. Zatím nejsou data k dispozici.

#### Stavový automat řízení relé

ESP32 používá stavový automat s pěti stavy:

| Stav | Popis |
|------|-------|
| **VS_OFF** | Obě relé OFF, čeká na `enabled=true` z OPI |
| **VS_STARTING** | Relé1 ON, po 2,5 s → relé2 ON (VS_ACTIVE) |
| **VS_ACTIVE** | Obě relé ON, hlídá override podmínky |
| **VS_STOPPING** | Relé1 OFF, po 2,5 s → relé2 OFF (VS_OFF) |
| **VS_OVERRIDE_CHECK** | Relé1 OFF (kvůli override), po 4 s kontrola. OK → VS_ACTIVE, stále špatné → VS_OFF |

**Priority** (od nejvyšší):
1. **Safety — MQTT timeout:** výpadek delší než `mqtt_timeout` → obě relé okamžitě OFF
2. **Override:** `battery_discharge_current > vybijeni_bat` nebo `output_apparent_power > max_vykon` → relé1 OFF, za 4 s kontrola. Pokud stále překročeno → relé2 OFF. Pokud ne → relé1 zpět ON.
3. **MQTT povel:** `enabled=true` → sekvenční zapnutí (relé1 → 2,5 s → relé2). `enabled=false` → sekvenční vypnutí (relé1 → 2,5 s → relé2).

**Bezpečnost:** Bez `enabled=true` z OPI ESP nikdy nezapne relé. Vypnout relé (kvůli ochraně) může vždy.

### Bezpečnostní prvky

- **MQTT Watchdog**: při výpadku MQTT spojení delším než nastavený timeout (výchozí 15 s)
  se obě relé vypnou. Tím se zabrání nekontrolovanému odběru při ztrátě komunikace.
- **Last Will Testament**: při ztrátě MQTT spojení broker automaticky publikuje
  `{"status":"offline"}` na stavový topic.
- **Výchozí stav po startu**: obě relé jsou vypnutá, dokud nepřijde první MQTT zpráva.
- **Webové heslo**: ukládání nastavení přes web vyžaduje heslo — ověřuje se na serveru,
  nikoli v JavaScriptu.

### Měření proudu

SCT013 20A/1V je proudový transformátor s interním zatěžovacím odporem. Výstup je
napěťový: 1 V AC RMS při 20 A. ESP32 ADC (GPIO34, 12bit, 0–3,3 V) vzorkuje signál
a software počítá RMS hodnotu. Pro správnou funkci je nutný externí bias dělič, který
posune střídavý signál na polovinu rozsahu ADC (~1,65 V).

### Měření teploty

DS18B20 (vodotěsné provedení) na OneWire sběrnici (GPIO33). Měří teplotu vody ve vířivce.
Hodnota je pouze **informativní** — nepoužívá se pro žádné řízení. Slouží pro zobrazení
na webu FV elektrárny a kontrolu uživatelem.

### Struktura firmware

| Soubor | Účel |
|--------|------|
| `FVE_ovladani_virivka_ESP32.ino` | Hlavní soubor — `setup()` a `loop()` |
| `config.h` | Definice pinů, konstant, výchozích hodnot |
| `variables.h` | Globální proměnné s podrobnými komentáři |
| `wifi_manager.h/.cpp` | WiFi, duální síť, captive portal |
| `mqtt_handler.h/.cpp` | MQTT spojení, JSON, publish/subscribe |
| `relay_control.h/.cpp` | Logika relé + priority override |
| `sensors.h/.cpp` | DS18B20 + SCT013 ADC |
| `web_setup.h/.cpp` | Webová stránka nastavení |

### Použité knihovny

- `PubSubClient` (MQTT)
- `ArduinoJson` (JSON parsing)
- `DallasTemperature` + `OneWire` (DS18B20)
- `ArduinoOTA` (OTA aktualizace)
- `DNSServer` (captive portal)
- `WebServer` (webové rozhraní)
- `WiFi` (ESP32 built-in)
- `Preferences` (trvalé úložiště nastavení)

### Technické parametry

| Parametr | Hodnota |
|----------|---------|
| Napájení | AC 230 V, palubní spínaný zdroj |
| Max. spínaný proud na relé | 10 A (AC 250 V) |
| Odběr desky (bez zátěže relé) | ~1 W |
| WiFi | 802.11 b/g/n, 2,4 GHz |
| ADC rozlišení | 12 bit |
| Rozsah měření proudu | 0–20 A AC |
| Přesnost proudu | ~5 % (interní ADC, bez kalibrace) |
| Rozsah teploty DS18B20 | −55 až +125 °C, přesnost ±0,5 °C |

---

*Firmware verze 2.0.0 — červenec 2026*
