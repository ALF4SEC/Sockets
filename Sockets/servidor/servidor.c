/*
 * servidor.c
 * Servidor del protocolo de la práctica. Escucha a la vez en TCP y UDP en el
 * mismo puerto usando select(). Cada conexión TCP la atiende un proceso hijo;
 * los datagramas UDP los atiende directamente el proceso principal.
 * Todas las peticiones se registran en peticiones.log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../protocolo.h"

#define FICHERO_LOG "peticiones.log"
#define TAM_CLIENTE (NI_MAXHOST + INET_ADDRSTRLEN + 16)

static volatile sig_atomic_t terminar = 0;

static void manejadorFin(int senal)
{
    (void)senal;
    terminar = 1;
}

/* Añade una línea con fecha al fichero de log. Se escribe con un único write()
 * en modo O_APPEND para que no se mezclen las líneas de los procesos hijos. */
static void registrar(const char *formato, ...)
{
    char linea[TAM_BUFFER * 3];
    char fecha[32];
    time_t ahora = time(NULL);
    struct tm tmAhora;
    va_list args;
    int n, m, fd;

    localtime_r(&ahora, &tmAhora);
    strftime(fecha, sizeof(fecha), "%d/%m/%Y %H:%M:%S", &tmAhora);
    n = snprintf(linea, sizeof(linea), "[%s] ", fecha);

    va_start(args, formato);
    m = vsnprintf(linea + n, sizeof(linea) - n, formato, args);
    va_end(args);
    if (m > 0)
        n += m;
    if (n > (int)sizeof(linea) - 2)
        n = sizeof(linea) - 2;
    linea[n++] = '\n';

    if ((fd = open(FICHERO_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644)) >= 0) {
        if (write(fd, linea, n) < 0)
            perror("write log");
        close(fd);
    }
}

/* Escribe "nombre (ip:puerto)" del cliente en texto. */
static void describirCliente(const struct sockaddr_in *dir, char *texto, size_t tam)
{
    char host[NI_MAXHOST];
    char ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &dir->sin_addr, ip, sizeof(ip));
    if (getnameinfo((const struct sockaddr *)dir, sizeof(*dir), host, sizeof(host),
                    NULL, 0, NI_NAMEREQD) != 0)
        strcpy(host, "desconocido");
    snprintf(texto, tam, "%s (%s:%u)", host, ip, ntohs(dir->sin_port));
}

/* Quita los espacios, \r y \n del final de la cadena. */
static void recortarFinal(char *cadena)
{
    size_t lon = strlen(cadena);

    while (lon > 0 && isspace((unsigned char)cadena[lon - 1]))
        cadena[--lon] = '\0';
}

/* Suma los enteros de args. Devuelve 0 si alguno no es válido o hay desbordamiento. */
static int sumar(const char *args, long long *resultado)
{
    const char *p = args;
    char *fin;
    long long total = 0;
    long valor;
    int numeros = 0;

    while (*p != '\0') {
        errno = 0;
        valor = strtol(p, &fin, 10);
        if (fin == p || errno == ERANGE)
            return 0;
        if ((valor > 0 && total > LLONG_MAX - valor) || (valor < 0 && total < LLONG_MIN - valor))
            return 0;
        total += valor;
        numeros++;
        p = fin;
        while (isspace((unsigned char)*p))
            p++;
    }
    *resultado = total;
    return numeros > 0;
}

/* Interpreta una orden y deja la respuesta en respuesta.
 * Devuelve 1 si la orden es ADIOS (el cliente quiere cerrar), 0 en otro caso. */
static int procesarOrden(char *orden, char *respuesta, size_t tam)
{
    char comando[16];
    char *args;
    size_t i = 0;
    long long suma;
    time_t ahora;
    struct tm tmAhora;

    recortarFinal(orden);
    while (isspace((unsigned char)*orden))
        orden++;

    while (orden[i] != '\0' && !isspace((unsigned char)orden[i]) && i < sizeof(comando) - 1) {
        comando[i] = toupper((unsigned char)orden[i]);
        i++;
    }
    comando[i] = '\0';
    if (orden[i] != '\0' && !isspace((unsigned char)orden[i])) {
        snprintf(respuesta, tam, "500 Orden no reconocida");
        return 0;
    }
    args = orden + i;
    while (isspace((unsigned char)*args))
        args++;

    if (comando[0] == '\0') {
        snprintf(respuesta, tam, "500 Orden vacia");
    } else if (strcmp(comando, "HOLA") == 0) {
        if (*args == '\0')
            snprintf(respuesta, tam, "501 Falta el nombre");
        else
            snprintf(respuesta, tam, "200 Hola %.*s, bienvenido al servidor", TAM_BUFFER - 40, args);
    } else if (strcmp(comando, "HORA") == 0) {
        ahora = time(NULL);
        localtime_r(&ahora, &tmAhora);
        i = (size_t)snprintf(respuesta, tam, "200 ");
        strftime(respuesta + i, tam - i, "%d/%m/%Y %H:%M:%S", &tmAhora);
    } else if (strcmp(comando, "ECO") == 0) {
        snprintf(respuesta, tam, "200 %.*s", TAM_BUFFER - 8, args);
    } else if (strcmp(comando, "MAYUS") == 0) {
        snprintf(respuesta, tam, "200 %.*s", TAM_BUFFER - 8, args);
        for (i = 4; respuesta[i] != '\0'; i++)
            respuesta[i] = toupper((unsigned char)respuesta[i]);
    } else if (strcmp(comando, "SUMA") == 0) {
        if (sumar(args, &suma))
            snprintf(respuesta, tam, "200 %lld", suma);
        else
            snprintf(respuesta, tam, "501 SUMA necesita uno o mas numeros enteros");
    } else if (strcmp(comando, "AYUDA") == 0) {
        snprintf(respuesta, tam, "214 Ordenes: HOLA <nombre>, HORA, ECO <texto>, "
                                 "MAYUS <texto>, SUMA <n1> <n2> ..., AYUDA, ADIOS");
    } else if (strcmp(comando, "ADIOS") == 0) {
        snprintf(respuesta, tam, "221 Adios");
        return 1;
    } else {
        snprintf(respuesta, tam, "500 Orden no reconocida");
    }
    return 0;
}

/* Envía una línea terminada en CRLF por un socket TCP. */
static int enviarLineaTCP(int s, const char *texto)
{
    char linea[TAM_BUFFER + 2];
    int lon = snprintf(linea, sizeof(linea), "%s\r\n", texto);
    int enviados = 0;
    ssize_t n;

    if (lon > (int)sizeof(linea) - 1)
        lon = sizeof(linea) - 1;
    while (enviados < lon) {
        n = send(s, linea + enviados, lon - enviados, 0);
        if (n <= 0)
            return -1;
        enviados += n;
    }
    return 0;
}

/* Atiende una conexión TCP completa. Se ejecuta en un proceso hijo. */
static void atenderTCP(int s, const struct sockaddr_in *dir)
{
    char cliente[TAM_CLIENTE];
    char buffer[TAM_BUFFER];
    char orden[TAM_BUFFER];
    char respuesta[TAM_BUFFER];
    size_t usados = 0, consumidos;
    struct timeval espera = { TIMEOUT_TCP, 0 };
    char *nl;
    ssize_t n;
    int fin = 0;

    describirCliente(dir, cliente, sizeof(cliente));
    registrar("TCP %s: conexion establecida", cliente);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof(espera));

    while (!fin) {
        /* Leer hasta tener una línea completa en el buffer */
        while ((nl = memchr(buffer, '\n', usados)) == NULL) {
            if (usados == sizeof(buffer)) {
                enviarLineaTCP(s, "500 Linea demasiado larga");
                registrar("TCP %s: linea demasiado larga, se cierra la conexion", cliente);
                fin = 1;
                break;
            }
            n = recv(s, buffer + usados, sizeof(buffer) - usados, 0);
            if (n == 0) {
                fin = 1;
                break;
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    registrar("TCP %s: tiempo de espera agotado", cliente);
                fin = 1;
                break;
            }
            usados += n;
        }
        if (nl == NULL)
            break;

        *nl = '\0';
        consumidos = (size_t)(nl - buffer) + 1;
        recortarFinal(buffer);
        strcpy(orden, buffer);

        fin = procesarOrden(orden, respuesta, sizeof(respuesta));
        registrar("TCP %s: \"%s\" -> \"%s\"", cliente, buffer, respuesta);
        if (enviarLineaTCP(s, respuesta) < 0)
            fin = 1;

        memmove(buffer, buffer + consumidos, usados - consumidos);
        usados -= consumidos;
    }

    registrar("TCP %s: conexion cerrada", cliente);
    close(s);
}

/* Atiende un datagrama UDP: cada datagrama es una orden y recibe una respuesta. */
static void atenderUDP(int s)
{
    struct sockaddr_in dir;
    socklen_t lonDir = sizeof(dir);
    char cliente[TAM_CLIENTE];
    char buffer[TAM_BUFFER];
    char orden[TAM_BUFFER];
    char respuesta[TAM_BUFFER];
    char linea[TAM_BUFFER + 2];
    ssize_t n;
    int lon;

    n = recvfrom(s, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&dir, &lonDir);
    if (n < 0) {
        if (errno != EINTR)
            perror("recvfrom");
        return;
    }
    buffer[n] = '\0';
    recortarFinal(buffer);
    strcpy(orden, buffer);

    procesarOrden(orden, respuesta, sizeof(respuesta));
    describirCliente(&dir, cliente, sizeof(cliente));
    registrar("UDP %s: \"%s\" -> \"%s\"", cliente, buffer, respuesta);

    lon = snprintf(linea, sizeof(linea), "%s\r\n", respuesta);
    if (lon > (int)sizeof(linea) - 1)
        lon = sizeof(linea) - 1;
    if (sendto(s, linea, lon, 0, (struct sockaddr *)&dir, lonDir) < 0)
        perror("sendto");
}

static int crearSocket(int tipo)
{
    struct sockaddr_in dir;
    int s, opcion = 1;

    if ((s = socket(AF_INET, tipo, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opcion, sizeof(opcion));

    memset(&dir, 0, sizeof(dir));
    dir.sin_family = AF_INET;
    dir.sin_addr.s_addr = htonl(INADDR_ANY);
    dir.sin_port = htons(PUERTO);
    if (bind(s, (struct sockaddr *)&dir, sizeof(dir)) < 0) {
        perror("bind");
        exit(1);
    }
    return s;
}

int main(void)
{
    struct sigaction accion;
    struct sockaddr_in dirCliente;
    socklen_t lonDir;
    fd_set lectura;
    int sTCP, sUDP, sConexion, maximo;
    pid_t pid;

    /* SIGINT y SIGTERM terminan el servidor de forma ordenada. Sin SA_RESTART
     * para que select() y recv() se interrumpan al recibirlas. */
    memset(&accion, 0, sizeof(accion));
    accion.sa_handler = manejadorFin;
    sigemptyset(&accion.sa_mask);
    sigaction(SIGINT, &accion, NULL);
    sigaction(SIGTERM, &accion, NULL);

    /* Ignorar SIGCHLD hace que el sistema recoja a los hijos (sin zombis).
     * Ignorar SIGPIPE evita morir si un cliente cierra mientras respondemos. */
    accion.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &accion, NULL);
    sigaction(SIGPIPE, &accion, NULL);

    sTCP = crearSocket(SOCK_STREAM);
    if (listen(sTCP, 5) < 0) {
        perror("listen");
        exit(1);
    }
    sUDP = crearSocket(SOCK_DGRAM);

    printf("Servidor escuchando en el puerto %d (TCP y UDP)\n", PUERTO);
    registrar("Servidor iniciado en el puerto %d", PUERTO);
    maximo = (sTCP > sUDP ? sTCP : sUDP) + 1;

    while (!terminar) {
        FD_ZERO(&lectura);
        FD_SET(sTCP, &lectura);
        FD_SET(sUDP, &lectura);

        if (select(maximo, &lectura, NULL, NULL, NULL) < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        if (FD_ISSET(sTCP, &lectura)) {
            lonDir = sizeof(dirCliente);
            sConexion = accept(sTCP, (struct sockaddr *)&dirCliente, &lonDir);
            if (sConexion < 0) {
                if (errno != EINTR)
                    perror("accept");
            } else {
                switch (pid = fork()) {
                case -1:
                    perror("fork");
                    close(sConexion);
                    break;
                case 0:
                    close(sTCP);
                    close(sUDP);
                    atenderTCP(sConexion, &dirCliente);
                    exit(0);
                default:
                    close(sConexion);
                }
            }
        }

        if (FD_ISSET(sUDP, &lectura))
            atenderUDP(sUDP);
    }

    close(sTCP);
    close(sUDP);
    registrar("Servidor detenido");
    printf("Servidor detenido\n");
    return 0;
}
