.DEFAULT_GOAL := all
CC = gcc
CFLAGS = -Wall -pedantic --std=gnu99 -ggdb3 -pthread

all: client server

server: server.o sharedfunc.o
	$(CC) $^ $(CFLAGS) -o server

client: client.o sharedfunc.o
	$(CC) $^ $(CFLAGS) -o client

server.o: server.c sharedfunc.h

client.o: client.c sharedfunc.h
sharedfunc.o: sharedfunc.c sharedfunc.h