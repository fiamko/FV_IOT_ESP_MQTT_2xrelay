/*
 * relay_control.h — ŘÍZENÍ RELÉ (STAVOVÝ AUTOMAT, NC ZAPOJENÍ)
 *
 * NC: relé OFF = NC sepnuto = TOPÍ, relé ON = NC rozepnuto = NETOPÍ
 * MQTT timeout → relé OFF (bezpečný stav — topí)
 * Override → relé ON (ochrana baterie/měniče)
 *
 * STAVY: VS_IDLE → VS_HEATING → VS_OVERRIDE → VS_RECOVER_R1
 */
#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

void relay_control_init();
void relay_control_loop();
void relay_emergency_off();
const char* relay_reason_str();

#endif
