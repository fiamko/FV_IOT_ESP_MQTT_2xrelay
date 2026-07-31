/*
 * ============================================================================
 * TESTOVACÍ PROGRAM — ESP32 Dual Relay Board (ESP32-32E N4)
 * ============================================================================
 * 
 * ÚČEL:
 *   Exaktně ověřit GPIO piny pro relé, LED a tlačítko na desce
 *   ESP32_Relay_AC_X2 303E32AC210. Různé zdroje uvádějí různá zapojení,
 *   proto je nutné fyzické ověření.
 *
 *   Zároveň umožňuje první OTA nahrání — po ověření GPIO stačí přes WiFi
 *   nahrát finální firmware bez dalšího připojování USB-UART převodníku.
 *
 * POUŽITÍ:
 *   1. Uprav WiFi údaje níže (SSID, heslo)
 *   2. Nahraj přes USB-UART (IO0 na GND při startu)
 *   3. Otevři Serial Monitor (115200 baud)
 *   4. Postupuj podle menu — program tě provede testem všech pinů
 *   5. Po ověření GPIO můžeš nahrát finální firmware přes OTA
 *
 * POZNÁMKY K HARDWARU:
 *   - Deska NEMÁ USB-UART převodník — programuje se přes 6pin header
 *   - Napájení AC 230V přes palubní spínaný zdroj
 *   - Relé: 2× 10A (COM/NO/NC)
 *   - Podle ESPHome dokumentace: GPIO16=Relé1, GPIO17=Relé2, GPIO23=LED, GPIO0=Tlačítko
 *   - ALE: tato přiřazení MUSÍ BÝT OVĚŘENA tímto testem!
 *
 * VAZBY:
 *   - WiFi: připojení k domácí síti pro OTA
 *   - Serial: interaktivní menu pro test GPIO
 *   - OTA: ArduinoOTA na portu 3232 (standard)
 * ============================================================================
 */

#include <WiFi.h>
#include <ArduinoOTA.h>

// ============================================================================
// WiFi — uprav před prvním nahráním!
// ============================================================================
const char* WIFI_SSID     = "FIAMCHAL";
const char* WIFI_PASSWORD = "7770665468";

// ============================================================================
// OTA nastavení
// ============================================================================
const char* OTA_HOSTNAME = "ESP32-virivka-test";
const char* OTA_PASSWORD = "";  // nech prázdné pro bez hesla, nebo nastav

// ============================================================================
// KANDIDÁTNÍ GPIO PINY K TESTOVÁNÍ
// ============================================================================
// Testujeme všechny piny, které by mohly ovládat relé, LED a tlačítko.
// Piny 34,35,36,39 = ADC vstupní-only, nelze jako výstup → vynecháváme.
// Piny 6-11 = připojeny k interní SPI flash → vynecháváme.

const int CANDIDATE_PINS[] = {
  0,   // často tlačítko (BOOT), ale může být i relé
  2,   // často LED na jiných deskách
  4,   // volný
  5,   // volný
  12,  // volný (pozor: boot fail při stažení HIGH)
  13,  // volný
  14,  // volný
  15,  // volný
  16,  // podezřelý pro Relé 1
  17,  // podezřelý pro Relé 2
  18,  // volný
  19,  // volný
  21,  // volný (často I2C SDA)
  22,  // volný (často I2C SCL)
  23,  // podezřelý pro LED
  25,  // volný
  26,  // volný
  27,  // volný
  32,  // volný
  33,  // volný
};

const int NUM_CANDIDATE_PINS = sizeof(CANDIDATE_PINS) / sizeof(CANDIDATE_PINS[0]);

// Piny, které z bezpečnostních důvodů netestujeme jako výstup
// (ADC only, flash, atd.)
bool is_safe_to_test(int pin) {
  // ADC-only piny (input only)
  if (pin == 34 || pin == 35 || pin == 36 || pin == 39) return false;
  // Flash piny
  if (pin >= 6 && pin <= 11) return false;
  return true;
}

// ============================================================================
// Pomocné funkce
// ============================================================================

void print_header() {
  Serial.println();
  Serial.println(F("╔══════════════════════════════════════════════════════╗"));
  Serial.println(F("║   ESP32 GPIO TEST — Dual Relay Board               ║"));
  Serial.println(F("║   Ověření relé, LED, tlačítka + OTA                ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════╝"));
}

void print_wifi_info() {
  Serial.println();
  Serial.print(F("WiFi: "));
  Serial.println(WiFi.SSID());
  Serial.print(F("IP:   "));
  Serial.println(WiFi.localIP());
  Serial.print(F("OTA:  "));
  Serial.print(OTA_HOSTNAME);
  Serial.println(F(".local (port 3232)"));
  Serial.println();
}

// ============================================================================
// TEST 1: Hledání LED
// ============================================================================
void test_led() {
  Serial.println();
  Serial.println(F("═══════════ TEST 1: HLEDÁNÍ LED ═══════════"));
  Serial.println(F("Budu postupně blikat všemi kandidátními piny."));
  Serial.println(F("Sleduj LED na desce a řekni mi, na kterém pinu se rozsvítila."));
  Serial.println(F("(LED může být aktivní v LOW nebo HIGH — vyzkouším obojí)"));
  Serial.println();

  // Nejprve resetujeme všechny piny
  for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
    int pin = CANDIDATE_PINS[i];
    if (!is_safe_to_test(pin)) continue;
    pinMode(pin, INPUT);
  }

  for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
    int pin = CANDIDATE_PINS[i];
    if (!is_safe_to_test(pin)) continue;

    Serial.print(F("Testuji GPIO "));
    Serial.print(pin);
    Serial.print(F(" → "));

    pinMode(pin, OUTPUT);

    // Test HIGH
    Serial.print(F("HIGH... "));
    digitalWrite(pin, HIGH);
    delay(800);
    digitalWrite(pin, LOW);
    delay(400);

    // Test LOW (pro aktivně-nízké LED)
    // Necháme chvilku LOW ať je vidět rozdíl
    digitalWrite(pin, LOW);
    delay(800);

    pinMode(pin, INPUT);
    Serial.println(F("hotovo."));
    delay(300);
  }

  Serial.println();
  Serial.println(F("KONEC TESTU LED."));
  Serial.println(F("Na kterém GPIO pinu svítila LED?"));
  Serial.println(F("A svítila při HIGH nebo LOW?"));
  Serial.println(F("───────────────────────────────────"));
}

// ============================================================================
// TEST 2: Hledání relé
// ============================================================================
void test_relays() {
  Serial.println();
  Serial.println(F("═══════════ TEST 2: HLEDÁNÍ RELÉ ═══════════"));
  Serial.println(F("POZOR: Deska musí být pod napětím 230V AC!"));
  Serial.println(F("Budu postupně spínat každý pin po dobu 2 sekund."));
  Serial.println(F("Poslouchej cvaknutí relé a sleduj které sepnulo."));
  Serial.println();
  Serial.println(F("Relé mohou být aktivní v HIGH nebo LOW."));
  Serial.println(F("Nejprve zkusím HIGH (sepnu), pak LOW (sepnu)."));
  Serial.println();

  // Reset
  for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
    int pin = CANDIDATE_PINS[i];
    if (!is_safe_to_test(pin)) continue;
    pinMode(pin, INPUT);
  }
  delay(500);

  // TEST HIGH (aktivní v HIGH)
  Serial.println(F("--- Fáze A: spínání HIGH (relé sepne při HIGH) ---"));
  for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
    int pin = CANDIDATE_PINS[i];
    if (!is_safe_to_test(pin)) continue;

    Serial.print(F("GPIO "));
    Serial.print(pin);
    Serial.print(F(" → HIGH... "));

    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delay(2000);  // 2s na poslech
    digitalWrite(pin, LOW);
    pinMode(pin, INPUT);

    Serial.println(F("vypnuto."));
    delay(500);
  }

  // Pauza mezi fázemi
  Serial.println();
  Serial.println(F("5 sekund pauza..."));
  delay(5000);

  // TEST LOW (aktivní v LOW — relé sepne při LOW)
  Serial.println(F("--- Fáze B: spínání LOW (relé sepne při LOW) ---"));
  for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
    int pin = CANDIDATE_PINS[i];
    if (!is_safe_to_test(pin)) continue;

    // Nejdřív nastavíme HIGH (relé rozepnuté, pokud je aktivní v LOW)
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delay(100);

    Serial.print(F("GPIO "));
    Serial.print(pin);
    Serial.print(F(" → LOW... "));

    digitalWrite(pin, LOW);
    delay(2000);  // 2s na poslech
    digitalWrite(pin, HIGH);
    pinMode(pin, INPUT);

    Serial.println(F("vypnuto."));
    delay(500);
  }

  Serial.println();
  Serial.println(F("KONEC TESTU RELÉ."));
  Serial.println(F("Které GPIO piny spínaly Relé 1 a Relé 2?"));
  Serial.println(F("A při HIGH nebo LOW?"));
  Serial.println(F("───────────────────────────────────"));
}

// ============================================================================
// TEST 3: Detailní test jednoho pinu
// ============================================================================
void test_single_pin(int pin, bool active_high) {
  Serial.println();
  Serial.print(F("Detailní test GPIO "));
  Serial.print(pin);
  Serial.print(F(" (aktivní v "));
  Serial.print(active_high ? F("HIGH") : F("LOW"));
  Serial.println(F(")"));

  pinMode(pin, OUTPUT);

  for (int cycle = 0; cycle < 3; cycle++) {
    Serial.print(F("  Cyklus "));
    Serial.print(cycle + 1);
    Serial.print(F("/3: "));

    if (active_high) {
      Serial.print(F("HIGH "));
      digitalWrite(pin, HIGH);
    } else {
      Serial.print(F("LOW  "));
      digitalWrite(pin, LOW);
    }
    delay(1500);

    Serial.print(F("→ "));
    if (active_high) {
      Serial.print(F("LOW "));
      digitalWrite(pin, LOW);
    } else {
      Serial.print(F("HIGH"));
      digitalWrite(pin, HIGH);
    }
    delay(1000);
    Serial.println();
  }

  pinMode(pin, INPUT);
  Serial.println(F("Hotovo."));
}

// ============================================================================
// TEST 4: Hledání tlačítka
// ============================================================================
void test_button() {
  Serial.println();
  Serial.println(F("═══════════ TEST 3: HLEDÁNÍ TLAČÍTKA ═══════════"));
  Serial.println(F("Skenuji všechny kandidátní piny jako vstupy s pull-up."));
  Serial.println(F("Stiskni tlačítko na desce a já zjistím, který pin se změnil."));
  Serial.println(F("Máš 15 sekund — stiskni a drž tlačítko!"));
  Serial.println();

  // Nastavíme všechny kandidátní piny jako INPUT_PULLUP
  for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
    int pin = CANDIDATE_PINS[i];
    if (!is_safe_to_test(pin)) continue;
    pinMode(pin, INPUT_PULLUP);
  }

  // Čteme 15 sekund
  unsigned long start = millis();
  bool last_states[NUM_CANDIDATE_PINS];
  bool first_read = true;

  while (millis() - start < 15000) {
    bool any_change = false;

    for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
      int pin = CANDIDATE_PINS[i];
      if (!is_safe_to_test(pin)) continue;

      bool current = digitalRead(pin);

      if (!first_read && current != last_states[i]) {
        Serial.print(F("ZMĚNA! GPIO "));
        Serial.print(pin);
        Serial.print(F(": "));
        Serial.print(last_states[i] ? F("HIGH") : F("LOW"));
        Serial.print(F(" → "));
        Serial.print(current ? F("HIGH") : F("LOW"));
        Serial.print(F("  (tlačítko je na GPIO "));
        Serial.print(pin);
        Serial.println(F(", stisk = LOW)"));
        any_change = true;
      }

      last_states[i] = current;
    }

    if (first_read) first_read = false;
    if (any_change) break;

    // Progress bar
    int elapsed = (millis() - start) / 1000;
    if (elapsed % 3 == 0) {
      Serial.print(F("\rČekám na stisk tlačítka... "));
      Serial.print(15 - elapsed);
      Serial.print(F(" s zbývá"));
    }
    delay(50);
  }

  // Vrátíme piny do výchozího stavu
  for (int i = 0; i < NUM_CANDIDATE_PINS; i++) {
    int pin = CANDIDATE_PINS[i];
    if (!is_safe_to_test(pin)) continue;
    pinMode(pin, INPUT);
  }

  Serial.println();
  Serial.println(F("KONEC TESTU TLAČÍTKA."));
  Serial.println(F("───────────────────────────────────"));
}

// ============================================================================
// Interaktivní menu
// ============================================================================
void print_menu() {
  Serial.println();
  Serial.println(F("═══════════════════ MENU ═══════════════════"));
  Serial.println(F("  1 = Test LED (hledání pinu LED)"));
  Serial.println(F("  2 = Test relé (hledání pinů relé 1 a 2)"));
  Serial.println(F("  3 = Test tlačítka"));
  Serial.println(F("  4 = Detailní test jednoho pinu (můžeš zadat)"));
  Serial.println(F("  5 = Zobrazit WiFi stav + OTA info"));
  Serial.println(F("  m = Zobrazit toto menu"));
  Serial.println(F("═════════════════════════════════════════════"));
  Serial.print(F("Volba: "));
}

void handle_detail_test() {
  Serial.println();
  Serial.print(F("Zadej GPIO pin (0-39): "));
  while (!Serial.available()) delay(10);
  int pin = Serial.parseInt();
  while (Serial.available()) Serial.read(); // vyčištění bufferu

  Serial.print(F("Aktivní v HIGH (1) nebo LOW (0)? "));
  while (!Serial.available()) delay(10);
  int mode = Serial.parseInt();
  while (Serial.available()) Serial.read();

  bool active_high = (mode == 1);
  test_single_pin(pin, active_high);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  print_header();
  Serial.println();

  // WiFi
  Serial.print(F("Připojuji WiFi: "));
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println(F("WiFi připojeno!"));
    print_wifi_info();
  } else {
    Serial.println();
    Serial.println(F("WiFi SELHALO! OTA nebude dostupné."));
    Serial.println(F("Pokračuji s offline testem GPIO..."));
  }

  // OTA setup
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    if (strlen(OTA_PASSWORD) > 0) {
      ArduinoOTA.setPassword(OTA_PASSWORD);
    }

    ArduinoOTA.onStart([]() {
      Serial.println(F("\nOTA: Začínám update..."));
    });
    ArduinoOTA.onEnd([]() {
      Serial.println(F("OTA: Update dokončen, restart..."));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA: %u%%\r", (progress * 100) / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("OTA: Chyba [%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
      else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
      else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
      else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
      else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
    });

    ArduinoOTA.begin();
    Serial.println(F("OTA: Připraveno."));
  }

  print_menu();
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  // OTA handler
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  // Serial menu
  if (Serial.available()) {
    char c = Serial.read();
    Serial.println(c);

    switch (c) {
      case '1':
        test_led();
        break;
      case '2':
        test_relays();
        break;
      case '3':
        test_button();
        break;
      case '4':
        handle_detail_test();
        break;
      case '5':
        if (WiFi.status() == WL_CONNECTED) {
          print_wifi_info();
        } else {
          Serial.println(F("WiFi není připojeno."));
        }
        break;
      case 'm':
      case 'M':
        print_menu();
        break;
      case '\n':
      case '\r':
        // ignorujeme
        break;
      default:
        Serial.print(F("Neznámá volba: '"));
        Serial.print(c);
        Serial.println(F("'. Zadej 1-5 nebo m."));
        break;
    }

    if (c >= '1' && c <= '5') {
      print_menu();
    }
  }

  delay(10);
}
