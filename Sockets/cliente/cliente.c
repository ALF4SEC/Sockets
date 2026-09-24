/*
 * cliente.c
 * Cliente del protocolo de la práctica. Lee las órdenes de un fichero, una por
 * línea, las envía al servidor por TCP o UDP y guarda cada orden con su
 * respuesta en cliente_<puerto>.txt, donde <puerto> es el puerto local que
 * le ha asignado el sistema.
 *
 * Uso: cliente <servidor> <TCP|UDP> <fichero_ordenes>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../protocolo.h"

/* Quita los espacios, \r y \n del final de la cadena. */
static void recortarFinal(char *cadena)
{
    size_t lon = strlen(cadena);

    while (lon > 0 && isspace((unsigned char)cadena[lon - 1]))
        cadena[--lon] = '\0';
}

/* Devuelve 1 si la orden es ADIOS (sin distinguir mayúsculas). */
static int esAdios(const char *orden)
{
    while (isspace((unsigned char)*orden))
        orden++;
    return strncasecmp(orden, "ADIOS", 5) == 0 &&
           (orden[5] == '\0' || isspace((unsigned char)orden[5]));
}

static void mostrar(FILE *log, const char *prefijo, const char *texto)
{
    printf("%s%s\n", prefijo, texto);
    fprintf(log, "%s%s\n", prefijo, texto);
    fflush(log);
}

/* Abre el fichero de resultados usando el puerto local del socket. */
static FILE *abrirLog(int s, const char *protocolo, const char *servidor)
{
    struct sockaddr_in local;
    socklen_t lon = sizeof(local);
    char nombre[64];
    FILE *log;

    if (getsockname(s, (struct sockaddr *)&local, &lon) < 0) {
        perror("getsockname");
        exit(1);
    }
    snprintf(nombre, sizeof(nombre), "cliente_%u.txt", ntohs(local.sin_port));
    if ((log = fopen(nombre, "w")) == NULL) {
        perror(nombre);
        exit(1);
    }
    fprintf(log, "Cliente %s conectado a %s:%d desde el puerto %u\n",
            protocolo, servidor, PUERTO, ntohs(local.sin_port));
    return log;
}

/* Lee una línea (hasta \n) de un socket TCP, usando buffer para guardar lo que
 * sobre entre llamadas. Devuelve 1 si ha leído una línea y 0 si el servidor
 * ha cerrado la conexión o ha habido un error. */
static int leerLineaTCP(int s, char *buffer, size_t *usados, char *linea, size_t tam)
{
    char *nl;
    size_t lon;
    ssize_t n;

    while ((nl = memchr(buffer, '\n', *usados)) == NULL) {
        if (*usados == TAM_BUFFER)
            return 0;
        n = recv(s, buffer + *usados, TAM_BUFFER - *usados, 0);
        if (n <= 0)
            return 0;
        *usados += n;
    }
    lon = (size_t)(nl - buffer);
    if (lon >= tam)
        lon = tam - 1;
    memcpy(linea, buffer, lon);
    linea[lon] = '\0';
    recortarFinal(linea);

    lon = (size_t)(nl - buffer) + 1;
    memmove(buffer, buffer + lon, *usados - lon);
    *usados -= lon;
    return 1;
}

static int enviarTodo(int s, const char *datos, size_t lon)
{
    size_t enviados = 0;
    ssize_t n;

    while (enviados < lon) {
        n = send(s, datos + enviados, lon - enviados, 0);
        if (n <= 0)
            return -1;
        enviados += n;
    }
    return 0;
}

static void clienteTCP(int s, FILE *ordenes, FILE *log)
{
    char buffer[TAM_BUFFER];
    char orden[TAM_BUFFER];
    char linea[TAM_BUFFER + 2];
    char respuesta[TAM_BUFFER];
    size_t usados = 0;
    int despedido = 0, lon;

    while (!despedido && fgets(orden, sizeof(orden), ordenes) != NULL) {
        recortarFinal(orden);
        if (orden[0] == '\0')
            continue;

        lon = snprintf(linea, sizeof(linea), "%s\r\n", orden);
        mostrar(log, "C: ", orden);
        if (enviarTodo(s, linea, lon) < 0) {
            mostrar(log, "", "ERROR: no se pudo enviar la orden");
            return;
        }
        if (!leerLineaTCP(s, buffer, &usados, respuesta, sizeof(respuesta))) {
            mostrar(log, "", "ERROR: el servidor ha cerrado la conexion");
            return;
        }
        mostrar(log, "S: ", respuesta);
        despedido = esAdios(orden);
    }

    /* Si el fichero no terminaba con ADIOS, se despide igualmente */
    if (!despedido) {
        mostrar(log, "C: ", "ADIOS");
        if (enviarTodo(s, "ADIOS\r\n", 7) == 0 &&
            leerLineaTCP(s, buffer, &usados, respuesta, sizeof(respuesta)))
            mostrar(log, "S: ", respuesta);
    }
}

static void clienteUDP(int s, FILE *ordenes, FILE *log)
{
    char orden[TAM_BUFFER];
    char respuesta[TAM_BUFFER];
    struct timeval espera;
    fd_set lectura;
    ssize_t n;
    int intento, recibido, listo;

    while (fgets(orden, sizeof(orden), ordenes) != NULL) {
        recortarFinal(orden);
        if (orden[0] == '\0')
            continue;
        mostrar(log, "C: ", orden);

        /* UDP no garantiza la entrega: si no llega respuesta a tiempo se
         * reenvía la orden, hasta REINTENTOS_UDP veces. */
        recibido = 0;
        for (intento = 1; intento <= REINTENTOS_UDP && !recibido; intento++) {
            if (send(s, orden, strlen(orden), 0) < 0) {
                perror("send");
                break;
            }
            FD_ZERO(&lectura);
            FD_SET(s, &lectura);
            espera.tv_sec = TIMEOUT_UDP;
            espera.tv_usec = 0;
            listo = select(s + 1, &lectura, NULL, NULL, &espera);
            if (listo < 0 && errno != EINTR) {
                perror("select");
                break;
            }
            if (listo > 0) {
                n = recv(s, respuesta, sizeof(respuesta) - 1, 0);
                if (n >= 0) {
                    respuesta[n] = '\0';
                    recortarFinal(respuesta);
                    recibido = 1;
                }
            }
        }

        if (recibido)
            mostrar(log, "S: ", respuesta);
        else
            mostrar(log, "", "ERROR: sin respuesta del servidor tras varios intentos");
    }
}

int main(int argc, char *argv[])
{
    struct addrinfo pistas, *resultado;
    char puerto[8];
    FILE *ordenes, *log;
    int s, tcp, error;

    if (argc != 4) {
        fprintf(stderr, "Uso: %s <servidor> <TCP|UDP> <fichero_ordenes>\n", argv[0]);
        return 1;
    }
    if (strcasecmp(argv[2], "TCP") == 0) {
        tcp = 1;
    } else if (strcasecmp(argv[2], "UDP") == 0) {
        tcp = 0;
    } else {
        fprintf(stderr, "El protocolo tiene que ser TCP o UDP\n");
        return 1;
    }
    if ((ordenes = fopen(argv[3], "r")) == NULL) {
        perror(argv[3]);
        return 1;
    }

    memset(&pistas, 0, sizeof(pistas));
    pistas.ai_family = AF_INET;
    pistas.ai_socktype = tcp ? SOCK_STREAM : SOCK_DGRAM;
    snprintf(puerto, sizeof(puerto), "%d", PUERTO);
    if ((error = getaddrinfo(argv[1], puerto, &pistas, &resultado)) != 0) {
        fprintf(stderr, "No se encuentra el servidor %s: %s\n", argv[1], gai_strerror(error));
        return 1;
    }

    if ((s = socket(AF_INET, pistas.ai_socktype, 0)) < 0) {
        perror("socket");
        return 1;
    }
    /* En UDP, connect() solo fija el destino: permite usar send/recv y que el
     * sistema asigne el puerto local, que se usa para nombrar el fichero. */
    if (connect(s, resultado->ai_addr, resultado->ai_addrlen) < 0) {
        perror("connect");
        return 1;
    }
    freeaddrinfo(resultado);

    log = abrirLog(s, tcp ? "TCP" : "UDP", argv[1]);
    if (tcp)
        clienteTCP(s, ordenes, log);
    else
        clienteUDP(s, ordenes, log);

    close(s);
    fclose(log);
    fclose(ordenes);
    return 0;
}
