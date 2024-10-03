#ifndef COMMON_H
#define COMMON_H
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <it.h>

#define CBUG(c) if (c) { fprintf(stderr, "CBUG! " #c " %s:%s:%d\n", \
		__FILE__, __FUNCTION__, __LINE__); raise(SIGINT); }

/* read a word */
static void
read_word(char *buf, char **input, size_t max_len)
{
	size_t ret = 0;
	char *inp = *input;

	for (; *inp && isspace(*inp); inp++, ret++);

	for (; *inp && !isspace(*inp) && ret < max_len; inp++, buf++, ret++)
		*buf = *inp;

	*buf = '\0';

	*input += ret;
}

/* read date in iso 8601 and convert it to a unix timestamp */
static inline time_t
read_ts(char **line)
{
	char buf[DATE_MAX_LEN];
	read_word(buf, line, sizeof(buf));
	return sscantime(buf);
}

#endif
