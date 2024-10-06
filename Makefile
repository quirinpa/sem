include env.mk
.PHONY: all run clean install

PREFIX ?= usr

UNAME != uname
LDFLAGS-Linux := -lbsd
LDFLAGS-OpenBSD := -L/usr/local/lib
LDFLAGS += -lit -lqhash -ldb ${LDFLAGS-${UNAME}}

CFLAGS-Alpine := -DALPINE
CFLAGS-OpenBSD := -I/usr/local/include
CFLAGS += -g ${CFLAGS-${DISTRO}} ${CFLAGS-${UNAME}}

npm-lib := @tty-pt/qhash
node_modules != npm root
CFLAGS += ${npm-lib:%=-I${node_modules}/%/include}
LDFLAGS	+= ${npm-lib:%=-L${node_modules}/%} ${npm-lib:%=-Wl,-rpath,%}

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
