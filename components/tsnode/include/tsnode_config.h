/*
 * Constantes públicas de versión y configuración compile-time de tsnode.
 *
 * Deliberadamente NO define acá tamaños de buffers de protocolo: los techos
 * de frames/mensajes se fijan con el ADR de arquitectura de protocolo, con
 * valores verificados contra la spec (AGENTS.md §5.6: ningún valor de
 * protocolo asumido). Este header solo expone lo que un proyecto consumidor
 * necesita saber desde el día uno.
 */

#ifndef TSNODE_CONFIG_H
#define TSNODE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define TSNODE_VERSION_MAJOR 0
#define TSNODE_VERSION_MINOR 1
#define TSNODE_VERSION_PATCH 0

#define TSNODE_VERSION_STRING "0.1.0"

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_CONFIG_H */
