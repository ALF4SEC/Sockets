#!/bin/sh
# Arranca el servidor y lanza a la vez tres clientes TCP y tres UDP, cada uno
# con su fichero de órdenes. Cuando terminan todos los clientes, para el
# servidor. Se ejecuta desde la carpeta Sockets después de compilar con make.
SERVIDOR=${1:-localhost}

./servidor &
PID_SERVIDOR=$!
sleep 1

PIDS=""
for ordenes in ordenes/ordenes1.txt ordenes/ordenes2.txt ordenes/ordenes3.txt; do
    ./cliente "$SERVIDOR" TCP "$ordenes" > /dev/null &
    PIDS="$PIDS $!"
    ./cliente "$SERVIDOR" UDP "$ordenes" > /dev/null &
    PIDS="$PIDS $!"
done

for pid in $PIDS; do
    wait "$pid"
done

kill "$PID_SERVIDOR"
wait "$PID_SERVIDOR" 2>/dev/null

echo "Resultados de los clientes en cliente_*.txt y del servidor en peticiones.log"
