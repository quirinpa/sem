.PHONY: run clean

CFLAGS += -I/usr/local/include
LDFLAGS += -L/usr/local/lib -ldb

sem: sem.c
	${LINK.c} -g -o $@ sem.c

run: sem
	./sem

clean:
	rm sem
