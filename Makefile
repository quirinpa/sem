.PHONY: run clean

UNAME != uname
LDFLAGS-Linux := -lbsd

LIBDB_PATH ?= /usr/lib
LIBFILES-Linux := ${LIBDB_PATH}/libdb.a
CFLAGS-Alpine := -DALPINE

CFLAGS += ${CFLAGS-${DISTRO}} -I/usr/local/include
LDFLAGS += ${LDFLAGS-${UNAME}} -L/usr/local/lib -ldb

sem: sem.c
	${LINK.c} -g -o $@ sem.c ${LIBFILES-${UNAME}}

run: sem
	cat data.txt | ./sem

clean:
	rm sem
