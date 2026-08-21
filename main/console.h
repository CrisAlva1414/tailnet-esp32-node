/*
 * Consola serie de provisioning y diagnóstico (ADR-0007).
 *
 * Canal único de provisioning v1: UART0 a 115200. Los secretos se leen
 * SIN echo y jamás se loguean. Comandos: help, wifi set, status,
 * provision status, provision wipe, tskey set, reboot.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Crea la tarea de consola. Llamar una sola vez desde app_main. */
void console_start(void);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
