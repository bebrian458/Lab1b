CC = gcc
CFLAGS = -g
DIST = README lab1a.c Makefile

default:
	$(CC) $(CFLAGS) -o lab1a lab1a.c
clean:
	rm -f lab1a *.tar.gz

dist:
	tar -czf lab1a-204612203.tar.gz $(DIST)

.PHONY:
	default clean dist