include env.mk
.PHONY: all run clean install

PREFIX ?= usr

UNAME != uname
LDFLAGS-Linux := -lbsd
LDFLAGS-OpenBSD := -L/usr/local/lib
LDFLAGS += -lit -lqdb -ldb ${LDFLAGS-${UNAME}}

CFLAGS-Alpine := -DALPINE
CFLAGS-OpenBSD := -I/usr/local/include
CFLAGS += -g ${CFLAGS-${DISTRO}} ${CFLAGS-${UNAME}}

npm-lib := @tty-pt/qdb
node_modules != npm root
prefix := ${srcdir} \
	  ${npm-lib:%=${node_modules}/%} \
	  -I/usr/local/include

CFLAGS += ${prefix:%=-I${node_modules}/%/include}
LDFLAGS += ${prefix:%=-L${node_modules}/%/lib} ${prefix:%=-Wl,-rpath,%/lib}

all: ${exe}

sem: sem.c
	${CC} -o $@ sem.c ${CFLAGS} ${LDFLAGS}

sem-echo: sem-echo.c
	${LINK.c} -o $@ sem-echo.c

run: sem
	cat data.txt | ./sem

clean:
	rm sem sem-echo || true

$(DESTDIR)$(PREFIX)/bin/sem: sem
	install -m 755 sem $@

$(DESTDIR)$(PREFIX)/bin/sem-echo: sem-echo
	install -m 755 sem-echo $@

install: ${exe:%=${DESTDIR}${PREFIX}/bin/%}
