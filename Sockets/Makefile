CC = gcc
CFLAGS = -Wall -Wextra -g

all: servidor cliente

servidor: servidor/servidor.c protocolo.h
	$(CC) $(CFLAGS) servidor/servidor.c -o servidor

cliente: cliente/cliente.c protocolo.h
	$(CC) $(CFLAGS) cliente/cliente.c -o cliente

limpiar:
	rm -f servidor cliente cliente_*.txt peticiones.log

.PHONY: all limpiar
