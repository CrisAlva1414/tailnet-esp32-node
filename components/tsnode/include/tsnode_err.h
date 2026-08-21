/*
 * Códigos de error públicos de tsnode.
 *
 * Todos los retornos del componente usan este tipo. Las funciones de
 * criptografía, red y storage NUNCA devuelven códigos crudos de las
 * bibliotecas subyacentes: se normalizan en la frontera (ver
 * docs/format/c-style.md). El detalle del error original queda para el log,
 * nunca con material sensible (AGENTS.md §2.1).
 */

#ifndef TSNODE_ERR_H
#define TSNODE_ERR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TSNODE_OK = 0,
    TSNODE_ERR_INVALID_ARG,
    TSNODE_ERR_NOT_INITIALIZED,
    TSNODE_ERR_INVALID_STATE,
    TSNODE_ERR_NO_MEMORY,
    TSNODE_ERR_TIMEOUT,
    TSNODE_ERR_CRYPTO,
    TSNODE_ERR_STORAGE,
    TSNODE_ERR_NETWORK,
    TSNODE_ERR_PROVISIONING,
    TSNODE_ERR_NOT_IMPLEMENTED,
} tsnode_err_t;

/*
 * Nombre legible del código de error, para logs. Nunca retorna NULL:
 * códigos desconocidos retornan "TSNODE_ERR_UNKNOWN".
 */
const char *tsnode_err_name(tsnode_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_ERR_H */
