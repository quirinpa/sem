.PHONY: run clean

sem: sem.c
	${CC} -g -o $@ -ldb sem.c

run: sem
	./sem

clean:
	rm sem
