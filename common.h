#ifndef COMMON_H
#define COMMON_H
#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __OpenBSD__
#define TS_FMT "%lld"
#define TS_MIN LLONG_MIN
#define TS_MAX LLONG_MAX
#else
#define TS_FMT "%ld"
#define TS_MIN LONG_MIN
#define TS_MAX LONG_MAX
#endif

#define CBUG(c) if (c) { fprintf(stderr, "CBUG! " #c " %s:%s:%d\n", \
		__FILE__, __FUNCTION__, __LINE__); raise(SIGINT); }

#define DATE_MAX_LEN 20

const time_t mtinf = (time_t) TS_MIN; // minus infinite
const time_t tinf = (time_t) TS_MAX; // infinite

/* get timestamp from ISO-8601 date string */
static time_t
sscantime(char *buf)
{
	char *aux;
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	aux = strptime(buf, "%Y-%m-%dT%H:%M:%S", &tm);
	if (!aux && !strptime(buf, "%Y-%m-%d", &tm))
		err(EXIT_FAILURE, "Invalid date");

	tm.tm_isdst = -1;
	return mktime(&tm);
}

/* get ISO-8601 date string from timestamp
 *
 * only use this for debug (memory leak), or free pointer
 */
static char *
printtime(time_t ts)
{
	char *buf;
	struct tm tm;

	if (ts == mtinf)
		return "-inf";

	if (ts == tinf)
		return "inf";

	buf = (char *) malloc(DATE_MAX_LEN);
	tm = *localtime(&ts);

	if (tm.tm_sec || tm.tm_min || tm.tm_hour)
		strftime(buf, DATE_MAX_LEN, "%FT%T", &tm);
	else
		strftime(buf, DATE_MAX_LEN, "%F", &tm);

	return buf;
}

/* read a word */
static size_t
read_word(char *buf, char *input, size_t max_len)
{
	size_t ret = 0;

	for (; *input && isspace(*input); input++, ret++);

	for (; *input && !isspace(*input) && ret < max_len;
		input++, buf++, ret++)

		*buf = *input;

	CBUG(ret > max_len); // FIXME?
	*buf = '\0';

	return ret;
}

/* read date in iso 8601 and convert it to a unix timestamp */
static size_t
read_ts(time_t *target, char *line)
{
	char date_str[DATE_MAX_LEN];
	size_t ret;

	ret = read_word(date_str, line, sizeof(date_str));
	*target = sscantime(date_str);

	return ret;
}

#endif
