.PHONY: run clean

sem: sem.c
	${CC} -o $@ -ldb sem.c

run: sem
	./sem

clean:
	rm sem
