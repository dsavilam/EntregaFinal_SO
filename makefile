CC = gcc
CFLAGS = -Wall -pthread

all: receptor solicitante

receptor: receptor.o db.o buffer.o
	$(CC) $(CFLAGS) -o receptor receptor.o db.o buffer.o

solicitante: solicitante.o
	$(CC) $(CFLAGS) -o solicitante solicitante.o

receptor.o: receptor.c common.h db.h buffer.h
	$(CC) $(CFLAGS) -c receptor.c

solicitante.o: solicitante.c common.h
	$(CC) $(CFLAGS) -c solicitante.c

db.o: db.c common.h db.h
	$(CC) $(CFLAGS) -c db.c

buffer.o: buffer.c common.h buffer.h
	$(CC) $(CFLAGS) -c buffer.c

clean:
	rm -f *.o receptor solicitante
