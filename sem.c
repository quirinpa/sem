#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <err.h>
#include <signal.h>
/* #include <sys/rbtree.h> */
#ifdef __OpenBSD__
#include <db4/db.h>
#else
#include <db.h>
#endif

#ifdef NDEBUG
#define CBUG(c) if (c) abort()
#else
#define CBUG(c) if (c) { fprintf(stderr, "CBUG! " #c " %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__); raise(SIGINT); } 
#endif

const time_t mtinf = (time_t) LONG_MIN;
const time_t tinf = (time_t) LONG_MAX;

DB *gdb = NULL;
DB *gedb = NULL;
static DB_ENV *dbe = NULL;

unsigned g_len = 0;
unsigned *g_edges = NULL;
unsigned g_notfound = (unsigned) -1;

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

void g_init() {
	CBUG(db_create(&gdb, dbe, 0)
			|| gdb->open(gdb, NULL, NULL, "g", DB_HASH, DB_CREATE, 0664));
	CBUG(db_create(&gedb, dbe, 0)
			|| gedb->open(gedb, NULL, NULL, "ge", DB_HASH, DB_CREATE, 0664));
}

unsigned g_insert(char *name) {
	DBT key;
	DBT data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = name;
	key.size = strlen(name);
	data.data = &g_len;
	data.size = sizeof(g_len);

	CBUG(gdb->put(gdb, NULL, &key, &data, 0));
	fprintf(stderr, "g_insert %s id %u\n", name, g_len);
	return g_len++;
}

unsigned g_find(char *name) {
	DBT key;
	DBT data;
	int ret;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = name;
	key.size = strlen(name);

	ret = gdb->get(gdb, NULL, &key, &data, 0);

	if (ret == DB_NOTFOUND)
		return g_notfound;
 
	CBUG(ret);
	return * (unsigned *) data.data;
}

int ge_get(unsigned id0, unsigned id1) {
	unsigned ids[2];
	DBT key;
	DBT data;
	int ret;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	if (id0 > id1) {
		ids[0] = id1;
		ids[1] = id0;
	} else {
		ids[0] = id0;
		ids[1] = id1;
	}

	key.data = ids;
	key.size = sizeof(ids);
	ret = gedb->get(gedb, NULL, &key, &data, 0);

	if (ret == DB_NOTFOUND)
		return 0;

	CBUG(ret);
	ret = * (int *) data.data;
	return id0 > id1 ? -ret : ret;
}

void ge_insert(unsigned id_from, unsigned id_to, int value) {
	unsigned ids[2];
	DBT key;
	DBT data;
	int cvalue;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	if (id_from > id_to) {
		ids[0] = id_to;
		ids[1] = id_from;
	} else {
		ids[0] = id_from;
		ids[1] = id_to;
	}

	key.data = ids;
	key.size = sizeof(ids);

	cvalue = ge_get(id_from, id_to);

	if (id_from > id_to)
		value = - cvalue - value;
	else
		value = cvalue + value;

	data.data = &value;
	data.size = sizeof(value);

	CBUG(gedb->put(gedb, NULL, &key, &data, 0));
	fprintf(stderr, "ge_insert %u -> %u : %u\n", ids[0], ids[1], value);
}

void ti_insert(unsigned id, time_t start, time_t end) {
	// TODO
	fprintf(stderr, "ti_insert %u [%ld, %ld]\n", id, start, end);
}

void ti_finish_last(unsigned id, time_t end) {
	// TODO
	fprintf(stderr, "ti_finish_last %u %ld\n", id, end);
}

void process_start(time_t ts, char *line) {
	char username[32];
	unsigned id;
	sscanf(line, "%s", username);
	id = g_insert(username);
	ti_insert(id, ts, tinf);
}

void process_stop(time_t ts, char *line) {
	char username[32];
	unsigned id;

	sscanf(line, "%s", username);
	id = g_find(username);
	if (id != g_notfound)
		ti_finish_last(id, ts);
	else {
		id = g_insert(username);
		ti_insert(id, mtinf, ts);
	}
}

void process_transfer(time_t ts, char *line) {
	char username_from[32], username_to[32];
	unsigned id_from, id_to;
	float fvalue;
	int value;

	sscanf(line, "%s %s %f", username_from, username_to, &fvalue);
	value = (int) (fvalue * 100.0f);
	id_from = g_find(username_from);
	id_to = g_find(username_to);
	CBUG(id_from == g_notfound || id_to == g_notfound);
	ge_insert(id_from, id_to, value);
}

void process_pay(time_t ts, char *line) {
  char username[32], start_date_str[64], end_date_str[64];
	unsigned id;
  float fvalue;
  int value;
  time_t start_ts, end_ts;

	sscanf(line, "%s %f %s %s", username, &fvalue, start_date_str, end_date_str);
	id = g_find(username);
  CBUG(id == g_notfound);
	value = (int) (fvalue * 100.0f);
  start_ts = sscantime(start_date_str);
  end_ts = sscantime(end_date_str);
  fprintf(stderr, "process_pay %ld %u %d [%ld, %ld]\n", ts, id, value, start_ts, end_ts);
}

void process_pause(time_t ts, char *line) {
	char username[32];
	unsigned id;

	sscanf(line, "%s", username);
	id = g_find(username);
	CBUG(id == g_notfound);
	// TODO assert interval for id at this ts
	ti_finish_last(id, ts);
}

void process_resume(time_t ts, char *line) {
	char username[32];
	unsigned id;

	sscanf(line, "%s", username);
	id = g_find(username);
	CBUG(id == g_notfound);
	// TODO assert no interval for id at this ts
	ti_insert(id, ts, tinf);
}

void process_line(char *line) {
	char op_type_str[64], date_str[64];
	time_t ts;
	int end;

	if (line[0] == '#')
		return;

	fprintf(stderr, "> %s", line);
	if (sscanf(line, "%s %s %n", op_type_str, date_str, &end) != 2)
		goto error;

	line += end;
	ts = sscantime(date_str);

	switch (*op_type_str) {
	case 'S':
		if (op_type_str[1] != 'T')
			goto error;

		switch(op_type_str[2]) {
		case 'A':
			return process_start(ts, line);
		case 'O':
			return process_stop(ts, line);
		default:
			goto error;
		}
	case 'T':
		return process_transfer(ts, line);
	case 'P':
		if (op_type_str[1] != 'A')
			goto error;

		switch (op_type_str[2]) {
		case 'Y':
			return process_pay(ts, line);
		case 'U':
			return process_pause(ts, line);
		default:
			goto error;
		}
	case 'R':
		return process_resume(ts, line);
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

	g_init();

	while ((linelen = getline(&line, &linesize, fp)) >= 0)
		process_line(line);

	free(line);

	if (ferror(fp))
		err(EXIT_FAILURE, "getline");

	return EXIT_SUCCESS;
}
