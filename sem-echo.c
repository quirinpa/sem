#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#include "common.h"

char *insert;
time_t insert_ts;
int finished = 0;

static inline void
process_line(char *line)
{
	char op_type_str[9];
	time_t ts;

	if (finished || line[0] == '#' || line[0] == '\n') {
		printf("%s", line);
		return;
	}

	line += read_word(op_type_str, line, sizeof(op_type_str));
	line += read_ts(&ts, line);

	char *tss = printtime(ts);

	if (ts >= insert_ts) {
		printf("%s\n", insert);
		finished = 1;
	}

	printf("%s %s%s", op_type_str, tss, line);
	free(tss);
}

int
main(int argc, char *argv[])
{
	char *insert_tss;
	char *line = NULL;
	ssize_t linelen;
	size_t linesize;

	insert = argv[1];

	insert_tss = strchr(insert, ' ');
	CBUG(!insert_tss);
	read_ts(&insert_ts, insert_tss);

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		process_line(line);

	if (!finished)
		printf("%s\n", insert);

	free(line);

	return EXIT_SUCCESS;
}
