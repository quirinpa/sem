#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <err.h>

/* void process_start(char *line) { */
	/* g_insert(username); */
/* } */

time_t sscantime(char *buf) {
	char *aux;
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	aux = strptime(buf, "%FT%T", &tm);
	if (!aux && !strptime(buf, "%F", &tm))
		err(EXIT_FAILURE, "Invalid date");

	tm.tm_isdst = -1;
	return mktime(&tm);
}

void process_line(char *line) {
	char op_type_str[64], date_str[64];
	time_t ts;

	fprintf(stderr, "%s", line);
	if (sscanf(line, "%s %s", op_type_str, date_str) != 2)
		goto error;

	ts = sscantime(date_str);

	switch (*op_type_str) {
	case 'S':
		if (op_type_str[1] != 'T')
			goto error;

		switch(op_type_str[2]) {
		case 'A':
			return;
			/* return process_start(line); */
		case 'O':
			return;
			/* return process_stop(line); */
		default:
			goto error;
		}
	case 'T':
		return;
		/* return process_transfer(line); */
	case 'P':
		return;
		/* return process_pay(line); */
	case 'R':
		return;
		/* return process_resume(line); */
	}

error:
	err(EXIT_FAILURE, "Invalid format");
}

int main() {
	FILE *fp = fopen("data.txt", "r");
	char *line = NULL;
	ssize_t linelen;
	size_t linesize;
	int ret;

	if (fp == NULL)
		err(EXIT_FAILURE, "Unable to open file");

	while ((linelen = getline(&line, &linesize, fp)))
		process_line(line);

	free(line);

	if (ferror(fp))
		err(EXIT_FAILURE, "getline");

	return EXIT_SUCCESS;
}
