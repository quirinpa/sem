.PHONY: all run clean install

DESTDIR ?= ../../../../
INSTALL_DEP ?= ${DESTDIR}make_dep.sh
PREFIX ?= usr

UNAME != uname
LDFLAGS-Linux := -lbsd
LD=${CC}

LIBDB_PATH ?= /usr/lib
CFLAGS-Alpine := -DALPINE

CFLAGS += ${CFLAGS-${DISTRO}} -I/usr/local/include -I/usr/include
LDFLAGS += ${LDFLAGS-${UNAME}} -L/usr/local/lib -L/usr/lib -ldb

all: sem sem-echo

sem: sem.c
	${LD} ${CFLAGS} -o $@ sem.c ${LDFLAGS}
	# ${LINK.c} -g -o $@ sem.c ${LIBFILES-${UNAME}}

sem-echo: sem-echo.c
	${LD} ${CFLAGS} -o $@ sem-echo ${LDFLAGS}
	# ${LINK.c} -g -o $@ sem-echo.c ${LIBFILES-${UNAME}}

run: sem
	cat data.txt | ./sem

clean:
	rm sem || true

$(DESTDIR)$(PREFIX)/bin/sem: sem
	install -m 755 sem $@
	${INSTALL_DEP} ${@:${DESTDIR}%=%}

install: ${DESTDIR}${PREFIX}/bin/sem
