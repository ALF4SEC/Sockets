/*
 * protocolo.h
 * Constantes compartidas por el cliente y el servidor.
 */
#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#define PUERTO          17278   /* puerto TCP y UDP del servidor */
#define TAM_BUFFER      512     /* longitud máxima de una línea (orden o respuesta) */
#define TIMEOUT_TCP     60      /* segundos sin recibir nada antes de cerrar una conexión TCP */
#define TIMEOUT_UDP     3       /* segundos que espera el cliente UDP cada respuesta */
#define REINTENTOS_UDP  5       /* intentos del cliente UDP antes de dar la orden por perdida */

#endif
