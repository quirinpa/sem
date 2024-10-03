.PHONY: all run clean install

DESTDIR ?= ../../../../
INSTALL_DEP ?= ${DESTDIR}make_dep.sh
PREFIX ?= usr

UNAME != uname
LDFLAGS-Linux := -lbsd
LDFLAGS += -lit -lqhash -ldb ${LDFLAGS-${UNAME}}

CFLAGS-Alpine := -DALPINE
CFLAGS += -g ${CFLAGS-${DISTRO}}

all: sem sem-echo

sem: sem.c
	${CC} -o $@ sem.c ${CFLAGS} ${LDFLAGS}

sem-echo: sem-echo.c
	${LINK.c} -o $@ sem-echo.c

run: sem
	cat data.txt | ./sem

clean:
	rm sem || true

$(DESTDIR)$(PREFIX)/bin/sem: sem
	install -m 755 sem $@
	${INSTALL_DEP} ${@:${DESTDIR}%=%}

install: ${DESTDIR}${PREFIX}/bin/sem
