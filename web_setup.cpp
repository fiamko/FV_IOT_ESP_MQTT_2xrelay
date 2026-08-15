/*
 * ============================================================================
 * web_setup.cpp — WEBOVÁ STRÁNKA (AJAX polling + POST formulář)
 *            Vířivka — FW 2.0.1
 * ============================================================================
 */

#include "variables.h"
#include "web_setup.h"
#include "relay_control.h"
#include <WebServer.h>

static WebServer* s_web_server = nullptr;

static String generate_html(const char* message = nullptr) {
    String h = F(
        "<!DOCTYPE html><html lang='cs'><head>"
        "<meta charset='UTF-8' name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Virivka</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:'Segoe UI',Arial;background:#0f0f1a;color:#e0e0e0;padding:15px}"
        "h1{color:#e94560;text-align:center;font-size:22px;margin-bottom:15px}"
        "h2{color:#aaa;font-size:14px;margin:15px 0 8px;border-bottom:1px solid #333;padding-bottom:5px}"
        ".card{background:#1a1a2e;border-radius:8px;padding:12px;margin-bottom:12px;border:1px solid #2a2a4a}"
        ".row{display:flex;justify-content:space-between;padding:4px 0;font-size:14px}"
        ".label{color:#888}.value{font-weight:bold}"
        ".on{color:#4ecca3}.off{color:#e94560}.warn{color:#f0a500}"
        "form{margin-top:10px}"
        "input{width:100%;padding:8px;margin:4px 0;border:1px solid #333;border-radius:5px;"
        "background:#16213e;color:#eee;font-size:14px}"
        "input[type=submit]{background:#e94560;color:#fff;border:none;padding:10px;margin-top:12px;"
        "font-size:15px;cursor:pointer;font-weight:bold}"
        ".msg{padding:8px;border-radius:5px;margin:8px 0;font-size:14px;text-align:center}"
        ".msg-ok{background:#1a3a1a;color:#4ecca3}"
        ".msg-err{background:#3a1a1a;color:#e94560}"
        ".version{text-align:center;color:#555;font-size:11px;margin-top:15px}"
        "</style></head><body>"
        "<h1>🔧 ESP32 Virivka</h1>"
    );

    if (message) {
        bool err = (strstr(message, "patn") || strstr(message, "Chyba") || strstr(message, "Zadej"));
        h += F("<div class='msg ");
        h += err ? F("msg-err") : F("msg-ok");
        h += F("'>");
        h += message;
        h += F("</div>");
    }

    // STATUS
    h += F("<div class='card'><h2>📡 WiFi / MQTT</h2>"
           "<div class='row'><span class='label'>WiFi:</span><span id='wifiSsid' class='value ");
    h += WiFi.isConnected() ? F("on") : F("off");
    h += F("'>");
    h += WiFi.isConnected() ? WiFi.SSID() : String("odpojeno");
    h += F("</span></div>"
           "<div class='row'><span class='label'>IP:</span><span id='wifiIp' class='value'>");
    h += WiFi.isConnected() ? WiFi.localIP().toString() : String("—");
    h += F("</span></div>"
           "<div class='row'><span class='label'>MQTT:</span><span id='mqttState' class='value ");
    h += g_mqtt_connected ? F("on") : F("off");
    h += F("'>");
    h += g_mqtt_connected ? F("připojeno") : F("odpojeno");
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>⚡ Relé</h2>"
           "<div class='row'><span class='label'>Relé 1:</span><span id='relay1State' class='value ");
    h += g_relay.actual[0] ? F("on") : F("off");
    h += F("'>");
    h += g_relay.actual[0] ? F("ZAPNUTO") : F("VYPNUTO");
    h += F("</span></div>"
           "<div class='row'><span class='label'>Relé 2:</span><span id='relay2State' class='value ");
    h += g_relay.actual[1] ? F("on") : F("off");
    h += F("'>");
    h += g_relay.actual[1] ? F("ZAPNUTO") : F("VYPNUTO");
    h += F("</span></div>"
           "<div class='row'><span class='label'>Důvod:</span><span id='relayReason' class='value warn'>");
    h += relay_reason_str();
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>🌡️ Senzory</h2>"
           "<div class='row'><span class='label'>Teplota:</span><span id='sensorTemp' class='value'>");
    if (g_sensors.teplota_error) h += F("chyba");
    else { h += String(g_sensors.teplota, 1); h += " °C"; }
    h += F("</span></div>"
           "<div class='row'><span class='label'>Proud:</span><span id='sensorProud' class='value'>");
    h += String(g_sensors.proud, 1) + " A";
    h += F("</span></div></div>");

    h += F("<div class='card'><h2>🔋 Měnič</h2>"
           "<div class='row'><span class='label'>Výkon:</span><span id='menicPower' class='value'>");
    h += String(g_menic.output_apparent_power, 0) + " W";
    h += F("</span></div>"
           "<div class='row'><span class='label'>Vybíjení bat:</span><span id='menicBat' class='value'>");
    h += String(g_menic.battery_discharge_current, 1) + " A";
    h += F("</span></div></div>");

    // FORMULAR
    h += F("<div class='card'><h2>⚙️ Nastavení</h2>"
           "<form method='POST' action='/save'>"

           "<label>Max. výkon [W]:</label>"
           "<input name='max_vykon' type='number' min='100' max='20000' value='");
    h += String(g_settings.max_vykon);
    h += F("'>"

           "<label>Max. vybíjení bat [A]:</label>"
           "<input name='vybijeni_bat' type='number' min='1' max='200' value='");
    h += String(g_settings.vybijeni_bat);
    h += F("'>"

           "<label>MQTT timeout [s]:</label>"
           "<input name='mqtt_timeout' type='number' min='5' max='300' value='");
    h += String(g_settings.mqtt_timeout);
    h += F("'>"

           "<label>Prodleva přepsání [s]:</label>"
           "<input name='override_delay' type='number' min='0' max='300' value='");
    h += String(g_settings.override_delay);
    h += F("'>"

           "<label>Web heslo:</label>"
           "<input name='web_password' type='password' maxlength='31' placeholder='Zadej heslo...'>"

           "<input type='submit' value='💾 Uložit'></form></div>");

    h += F("<div class='version'>FW: 2.0.1 | ESP32-virivka</div>");
    h += F("<script>"
           "function cof(e,c){e.className='value '+(c?'on':'off')}"
           "function poll(){var x=new XMLHttpRequest();x.open('GET','/api/status',true);"
           "x.onload=function(){if(x.status!=200)return;var d=JSON.parse(x.responseText);"
           "document.getElementById('wifiSsid').textContent=d.wifi_ssid||'odpojeno';"
           "cof(document.getElementById('wifiSsid'),d.wifi);"
           "document.getElementById('wifiIp').textContent=d.wifi_ip||'—';"
           "document.getElementById('mqttState').textContent=d.mqtt?'pripojeno':'odpojeno';"
           "cof(document.getElementById('mqttState'),d.mqtt);"
           "document.getElementById('relay1State').textContent=d.r1?'ZAPNUTO':'VYPNUTO';"
           "cof(document.getElementById('relay1State'),d.r1);"
           "document.getElementById('relay2State').textContent=d.r2?'ZAPNUTO':'VYPNUTO';"
           "cof(document.getElementById('relay2State'),d.r2);"
           "document.getElementById('relayReason').textContent=d.reason||'—';"
           "document.getElementById('sensorTemp').textContent=d.te?'chyba':(d.temp||0).toFixed(1)+' °C';"
           "document.getElementById('sensorProud').textContent=(d.proud||0).toFixed(1)+' A';"
           "document.getElementById('menicPower').textContent=(d.menic_p||0)+' W';"
           "document.getElementById('menicBat').textContent=(d.menic_b||0).toFixed(1)+' A';};x.send();}"
           "poll();setInterval(poll,3000);"
           "</script></body></html>");

    return h;
}

// ============================================================================
// API
// ============================================================================

static void handle_api_status() {
    String json = "{";
    json += "\"wifi\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
    json += "\"wifi_ssid\":\"" + String(WiFi.isConnected() ? WiFi.SSID().c_str() : "odpojeno") + "\",";
    json += "\"wifi_ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : "—") + "\",";
    json += "\"mqtt\":" + String(g_mqtt_connected ? "true" : "false") + ",";
    json += "\"r1\":" + String(g_relay.actual[0] ? "true" : "false") + ",";
    json += "\"r2\":" + String(g_relay.actual[1] ? "true" : "false") + ",";
    json += "\"reason\":\"" + String(relay_reason_str()) + "\",";
    json += "\"te\":" + String(g_sensors.teplota_error ? "true" : "false") + ",";
    json += "\"temp\":" + String(g_sensors.teplota, 1) + ",";
    json += "\"proud\":" + String(g_sensors.proud, 1) + ",";
    json += "\"menic_p\":" + String(g_menic.output_apparent_power, 0) + ",";
    json += "\"menic_b\":" + String(g_menic.battery_discharge_current, 1);
    json += "}";
    s_web_server->send(200, "application/json", json);
}

// ============================================================================
// HANDLERY
// ============================================================================

static void handle_root() {
    if (!s_web_server) return;
    s_web_server->send(200, "text/html; charset=utf-8", generate_html());
}

static void handle_save() {
    if (!s_web_server) return;

    String password = s_web_server->arg("web_password");
    if (password.length() == 0) {
        s_web_server->send(200, "text/html; charset=utf-8", generate_html("Zadej heslo!"));
        return;
    }
    if (password != g_settings.web_password) {
        s_web_server->send(200, "text/html; charset=utf-8",
            generate_html("Spatne heslo! Nastaveni nebylo ulozeno."));
        return;
    }

    if (s_web_server->hasArg("max_vykon")) {
        g_settings.max_vykon = s_web_server->arg("max_vykon").toInt();
        g_prefs.putInt("max_vykon", g_settings.max_vykon);
    }
    if (s_web_server->hasArg("vybijeni_bat")) {
        g_settings.vybijeni_bat = s_web_server->arg("vybijeni_bat").toInt();
        g_prefs.putInt("vybijeni_bat", g_settings.vybijeni_bat);
    }
    if (s_web_server->hasArg("mqtt_timeout")) {
        g_settings.mqtt_timeout = s_web_server->arg("mqtt_timeout").toInt();
        g_prefs.putInt("mqtt_timeout", g_settings.mqtt_timeout);
    }
    if (s_web_server->hasArg("override_delay")) {
        g_settings.override_delay = s_web_server->arg("override_delay").toInt();
        g_prefs.putInt("override_dly", g_settings.override_delay);
    }
    if (s_web_server->hasArg("web_password") && password.length() > 0) {
        strncpy(g_settings.web_password, password.c_str(), 31);
        g_prefs.putString("web_pass", password);
    }

    g_prefs.putString("fw_ver", FIRMWARE_VERSION);
    Serial.println(F("Web: nastaveni ulozeno."));
    s_web_server->send(200, "text/html; charset=utf-8", generate_html("Nastaveni ulozeno!"));
}

void web_setup_init() {
    if (s_web_server) delete s_web_server;
    s_web_server = new WebServer(WEB_PORT);

    s_web_server->on("/", HTTP_GET, handle_root);
    s_web_server->on("/api/status", HTTP_GET, handle_api_status);
    s_web_server->on("/save", HTTP_POST, handle_save);
    s_web_server->on("/save", HTTP_GET, handle_root);

    s_web_server->onNotFound([]() {
        if (s_web_server) s_web_server->send(404, "text/plain", "404");
    });

    s_web_server->begin();
    Serial.print(F("Web: server na portu "));
    Serial.println(WEB_PORT);
}

void web_setup_loop() {
    if (s_web_server) s_web_server->handleClient();
}
