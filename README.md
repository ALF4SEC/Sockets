# Sockets

Práctica de Redes de Computadores I: un cliente y un servidor en C para Linux que se comunican con sockets, tanto por TCP como por UDP, siguiendo un protocolo de texto sencillo.

## Protocolo

El cliente envía órdenes de una línea terminadas en `\r\n` y el servidor contesta a cada una con una línea que empieza por un código de tres cifras, al estilo de SMTP o FTP. Las órdenes no distinguen mayúsculas de minúsculas.

| Orden | Respuesta |
|---|---|
| `HOLA <nombre>` | `200 Hola <nombre>, bienvenido al servidor` |
| `HORA` | `200 dd/mm/aaaa hh:mm:ss` con la hora del servidor |
| `ECO <texto>` | `200 <texto>` |
| `MAYUS <texto>` | `200 <TEXTO>` |
| `SUMA <n1> <n2> ...` | `200 <suma>` |
| `AYUDA` | `214` y la lista de órdenes |
| `ADIOS` | `221 Adios`. En TCP, el servidor cierra la conexión. |

Códigos de error:

| Código | Significado |
|---|---|
| `500` | Orden desconocida, línea vacía o línea de más de 512 caracteres. |
| `501` | Faltan argumentos o no son válidos, por ejemplo `HOLA` sin nombre o `SUMA` con algo que no es un número entero. |

El servidor escucha en el puerto 17278 en TCP y UDP. Este puerto y el resto de constantes (tamaño de línea, tiempos de espera y reintentos) están en `protocolo.h`.

## Servidor

`servidor` atiende a la vez los dos protocolos en un único proceso, esperando en los dos sockets con `select()`:

- TCP: por cada conexión crea un proceso hijo con `fork()`, que atiende órdenes hasta recibir `ADIOS`, hasta que el cliente cierra o hasta que pasan 60 segundos sin recibir nada. El servidor ignora `SIGCHLD` para que el sistema recoja a los hijos y no queden procesos zombi.
- UDP: cada datagrama es una orden y recibe una respuesta. Los atiende el proceso principal.
- `SIGINT` y `SIGTERM` paran el servidor de forma ordenada.

Todas las conexiones y peticiones quedan en `peticiones.log`, con la fecha, el protocolo, el nombre y la dirección del cliente, la orden y la respuesta:

```
[24/09/2026 12:30:05] TCP localhost (127.0.0.1:51234): conexion establecida
[24/09/2026 12:30:05] TCP localhost (127.0.0.1:51234): "SUMA 10 20 30" -> "200 60"
[24/09/2026 12:30:05] UDP localhost (127.0.0.1:40211): "HORA" -> "200 24/09/2026 12:30:05"
```

Cada línea del log se escribe con una sola llamada a `write()` en modo `O_APPEND`, así que las líneas de los distintos procesos hijos no se mezclan.

## Cliente

```bash
./cliente <servidor> <TCP|UDP> <fichero_ordenes>
```

Lee el fichero de órdenes línea a línea, envía cada orden al servidor y escribe la orden (`C:`) y la respuesta (`S:`) por pantalla y en `cliente_<puerto>.txt`, donde `<puerto>` es el puerto local que le asigna el sistema. Así, varios clientes lanzados a la vez no se pisan el fichero.

- En TCP, si el fichero no termina con `ADIOS`, el cliente la envía al final para cerrar la conexión de forma ordenada.
- En UDP no hay garantía de entrega: si una respuesta no llega en 3 segundos, el cliente reenvía la orden, hasta 5 veces. Si ninguna llega, lo apunta como error y pasa a la siguiente orden.

## Compilación y prueba

```bash
cd Sockets
make
./lanzaServidor.sh
```

`lanzaServidor.sh` arranca el servidor, lanza a la vez tres clientes TCP y tres UDP con los ficheros de `ordenes/`, espera a que terminen y para el servidor. Se le puede pasar el nombre de otra máquina como argumento para que los clientes se conecten a ella (por defecto, `localhost`).

Para probarlo a mano, en dos terminales:

```bash
./servidor
./cliente localhost TCP ordenes/ordenes1.txt
```

`make limpiar` borra los ejecutables, los ficheros de resultados de los clientes y el log.

## Archivos

| Archivo | Contenido |
|---|---|
| `Sockets/protocolo.h` | Puerto y constantes compartidas. |
| `Sockets/servidor/servidor.c` | Servidor TCP y UDP. |
| `Sockets/cliente/cliente.c` | Cliente TCP y UDP. |
| `Sockets/Makefile` | Compila `servidor` y `cliente`. |
| `Sockets/lanzaServidor.sh` | Prueba con el servidor y varios clientes a la vez. |
| `Sockets/ordenes/` | Ficheros de órdenes de ejemplo, con órdenes correctas y con errores. |

## Limitaciones

- Solo IPv4.
- En UDP, si una respuesta llega después de que el cliente haya reenviado la orden, el cliente puede tomar esa respuesta tardía como la de la orden siguiente. Para evitarlo habría que numerar las peticiones.
