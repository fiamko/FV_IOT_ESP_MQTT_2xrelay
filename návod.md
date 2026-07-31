# Návod k použití — ESP32 ovládání ohřevu vířivky

## První spuštění

### 1. Nahrání firmwaru

1. Připoj USB-UART převodník k 6pin programovacímu headeru:
   - GND → GND (pin 1 nebo 5)
   - TX (převodník) → RX (deska, pin 3)
   - RX (převodník) → TX (deska, pin 2)
   - 5V (převodník) → 3V3 (deska, pin 4) — **dej pozor, ať je převodník v režimu 3,3 V!**
2. Připoj **IO0** (pin 4 na headeru) na **GND**
3. Zapni napájení nebo stiskni EN tlačítko
4. V Arduino IDE: vyber desku **ESP32 Dev Module**, port převodníku
5. Otevři `FVE_ovladani_virivka_ESP32.ino` a nahraj
6. Po nahrání **odpoj IO0 od GND** a resetuj (tlačítko EN)

> 💡 **První nahrání testovacím programem:** Pro ověření GPIO doporučujeme nejprve
> nahrát `test_gpio_ota/test_gpio_ota.ino`. Ověříš tím funkčnost relé, LED a tlačítka
> a zároveň získáš OTA přístup pro další nahrávání bez drátů.

### 2. Nastavení WiFi

**První spuštění (tovární nastavení):**
- ESP32 nemá uložené WiFi údaje → automaticky spustí **vlastní WiFi síť**
- Na mobilu/notebooku najdi síť **`ESP32-virivka`** (bez hesla)
- Otevři libovolnou webovou stránku — objeví se konfigurační formulář
- Zadej:
  - **Primární síť:** SSID a heslo (povinné)
  - **Záložní síť:** SSID a heslo (volitelné — např. hotspot z mobilu)
- Klikni na **Uložit a restartovat**
- ESP32 se restartuje a připojí k zadané síti

**Pozdější změna WiFi:**
- Drž tlačítko na desce **5 sekund** — LED začne rychle blikat
- ESP32 spustí konfigurační WiFi `ESP32-virivka`
- Postupuj stejně jako při prvním spuštění

**Duální síť:**
- ESP32 se vždy pokusí připojit k **primární** síti
- Pokud primární není dostupná, automaticky přepne na **záložní**
- Při obnovení primární sítě se k ní vrátí

### 3. Webové rozhraní

Po připojení k domácí síti zjisti IP adresu ESP32:
- V Arduino IDE: `Nástroje → Port` — ESP32 se zobrazí jako `ESP32-virivka at x.x.x.x`
- Nebo v routeru podle DHCP tabulky

Do prohlížeče zadej `http://[IP-adresa]` — zobrazí se stránka se stavem a nastavením.

Stránka se automaticky obnovuje každých 10 sekund.

---

## Webová stránka — přehled

### Stavové informace (čtení — vždy viditelné)

| Sekce | Zobrazuje |
|-------|-----------|
| **WiFi / MQTT** | Název připojené sítě, IP adresa, síla signálu (dBm), stav MQTT |
| **Relé** | Stav Relé 1 a 2 (ZAPNUTO/VYPNUTO), důvod poslední změny |
| **Senzory** | Teplota vody (°C), proud vířivkou (A) |
| **Měnič** | Aktuální výkon měniče (W), vybíjecí proud baterie (A) |

### Nastavení (zápis — vyžaduje heslo)

| Parametr | Výchozí | Rozsah | Význam |
|----------|---------|--------|--------|
| **Max. výkon** | 3000 W | 100–20000 | Při překročení začne override — postupné vypínání relé |
| **Max. vybíjení bat** | 20 A | 1–200 | Při překročení začne override — vypne relé1, za 4s kontrola |
| **MQTT timeout** | 15 s | 5–300 | Doba bez MQTT zprávy, po které se relé bezpečnostně vypnou |
| **Prodleva override** | 5 s | 2–8 | Prodleva mezi vypínáním relé při override (pro budoucí použití) |
| **Web heslo** | `virivka` | max 31 znaků | Heslo pro ukládání změn (změň při první příležitosti!) |

> ⚠️ **Heslo se ověřuje na serveru** — není vidět v kódu stránky (není v JavaScriptu).

---

## OTA aktualizace (bezdrátové nahrávání)

Po prvním úspěšném nahrání přes USB-UART můžeš další verze nahrávat bezdrátově:

1. V Arduino IDE: `Nástroje → Port` → vyber **`ESP32-virivka at x.x.x.x`** (síťový port)
2. Otevři soubor a klikni na **Nahrát**
3. Po nahrání se ESP32 automaticky restartuje

> 💡 Pokud se OTA port nezobrazuje: zkontroluj, že ESP32 i počítač jsou ve stejné síti.
> Někdy pomůže restart Arduino IDE nebo vypnutí/zapnutí WiFi na počítači.

---

## Signalizace LED

LED na desce signalizuje stav:

| Stav LED | Význam |
|----------|--------|
| Rychlé blikání (~5×/s) | Připojování k WiFi |
| Pomalé blikání (~1×/2s) | Připojeno, vše OK |
| Velmi pomalé blikání | WiFi odpojeno, pokus o reconnect |
| Velmi rychlé (~7×/s) | Konfigurační režim (vlastní AP aktivní) |

---

## Co dělat když...

### ... se ESP32 nepřipojí k WiFi?

1. Zkontroluj, že SSID a heslo byly zadány správně (pozor na diakritiku, velká písmena)
2. Drž tlačítko 5 s → spustí se konfigurační WiFi `ESP32-virivka`
3. Zadej údaje znovu

### ... se webová stránka nenačítá?

1. Ověř IP adresu ESP32 (v Arduino IDE nebo v routeru)
2. Zkontroluj, že jsi ve stejné síti jako ESP32
3. Zkus `http://ESP32-virivka.local` (funguje na Windows/macOS/iOS, na Androidu ne)

### ... relé nereagují na MQTT povely?

1. Na webové stránce zkontroluj stav MQTT — musí být **připojeno**
2. Ověř, že MQTT broker (192.168.0.191:1883) běží
3. Zkontroluj důvod na webu v sekci Relé — pokud je **Překročení výkonu** nebo
   **Vybíjení baterie**, ESP samo blokuje sepnutí (ochrana)
4. Pokud je **Bezpečnostní vypnutí**, MQTT spojení vypadlo na déle než timeout

### ... potřebuji resetovat do továrního nastavení?

Pro kompletní reset nastavení:
1. Připoj USB-UART a otevři Serial Monitor (115200 baud)
2. V Arduino IDE nahraj prázdný sketch, který vymaže Preferences:
   ```cpp
   #include <Preferences.h>
   void setup() {
       Preferences p;
       p.begin("virivka", false);
       p.clear();
       p.end();
   }
   void loop() {}
   ```
3. Pak nahraj znovu firmware — bude se chovat jako při prvním spuštění

---

## Prvotní kalibrace proudu

SCT013 měří s přesností ~5 %, což pro orientační měření stačí. Pro zpřesnění:

1. Zapni známou zátěž (např. topnou spirálu 2000 W ≈ ~8,7 A při 230 V)
2. Na webové stránce odečti hodnotu proudu
3. Porovnej s ampérmetrem (klešťovým měřákem)
4. Pokud je odchylka větší než ~10 %, uprav konstantu `SCT013_RATIO` v `config.h`
   - Vyšší hodnota = nižší zobrazovaný proud
   - Nižší hodnota = vyšší zobrazovaný proud

Příklad: Při skutečných 8,7 A ESP ukazuje 9,5 A → `SCT013_RATIO` změň z `20.0` na
`20.0 * (9.5 / 8.7) ≈ 21.8`

---

## Montáž do vířivky

### Umístění

- **Deska ESP32:** uvnitř ovládací skříně vířivky (místo musí být suché!)
- **Napájení desky:** AC 230 V — připoj před hlavní vypínač vířivky, ať ESP běží
  i při vypnuté vířivce (pro MQTT komunikaci)
- **Relé:** přerušují přívod k topným spirálám — **sériově** s původním ovládáním.
  Původní automatika vířivky zůstává plně funkční, ESP jen přidává možnost vypnout
  ohřev nezávisle.

### SCT013 (proudový senzor)

- Nasaď na **přívodní kabel** celé vířivky (fázi, ne nulák!)
- Směr šipky na transformátoru: k vířivce (k zátěži)

### DS18B20 (teplotní senzor)

- Umísti na vhodné místo ve vodě — např. do filtračního otvoru, trubky, nebo
  do vyhrazeného jímacího otvoru
- Kabel čidla veď mimo topné spirály a čerpadlo (rušení)

---

*Firmware verze 2.0.0 — červenec 2026*
