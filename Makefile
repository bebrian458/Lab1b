CC = gcc
CFLAGS = -g
DIST = README lab1b-client.c lab1b-server.c Makefile

default: lab1b-client lab1b-server

lab1b-client: lab1b-client.c
	$(CC) $(CFLAGS) $< -o $@

lab1b-server: lab1b-server.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f lab1b-client lab1b-server *.tar.gz

dist:
	tar -czf lab1b-204612203.tar.gz $(DIST)

.PHONY:
	default clean dist lab1b-server lab1b-client