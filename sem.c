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
#define debug(...) do {} while (0)
#define CBUG(c) if (c) abort()
#else
#define debug(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)
#define CBUG(c) if (c) { fprintf(stderr, "CBUG! " #c " %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__); raise(SIGINT); } 
#endif

struct ti {
	time_t min, max;
	unsigned who;
};

const time_t mtinf = (time_t) LONG_MIN;
const time_t tinf = (time_t) LONG_MAX;

DB *gdb = NULL;
DB *igdb = NULL;
DB *gedb = NULL;
DB *tidb = NULL;
DB *tiiddb = NULL;
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

char * printtime(time_t ts) {
	static char buf[64];
	struct tm tm;

	if (ts == mtinf)
		return "-inf";

	if (ts == tinf)
		return "inf";

	tm = *localtime(&ts);

	if (tm.tm_sec || tm.tm_min || tm.tm_hour)
		strftime(buf, sizeof(buf), "%FT%T", &tm);
	else
		strftime(buf, sizeof(buf), "%F", &tm);

	return buf;
}

static int
map_gdb_igdb(DB *sec, const DBT *key, const DBT *data, DBT *result) {
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(unsigned);
	result->data = data->data;
	return 0;
}

void g_init() {
	CBUG(db_create(&gdb, dbe, 0)
			|| gdb->open(gdb, NULL, NULL, "g", DB_HASH, DB_CREATE, 0664));

	CBUG(db_create(&igdb, dbe, 0)
			|| igdb->open(igdb, NULL, NULL, "ig", DB_HASH, DB_CREATE, 0664)
			|| gdb->associate(gdb, NULL, igdb, map_gdb_igdb, DB_CREATE));

	CBUG(db_create(&gedb, dbe, 0)
			|| gedb->open(gedb, NULL, NULL, "ge", DB_HASH, DB_CREATE, 0664));
}

unsigned g_insert(char *name) {
	DBT key;
	DBT data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = name;
	key.size = strlen(name) + 1;
	data.data = &g_len;
	data.size = sizeof(g_len);

	CBUG(gdb->put(gdb, NULL, &key, &data, 0));
	debug("g_insert %s id %u\n", name, g_len);
	return g_len++;
}

unsigned g_find(char *name) {
	DBT key;
	DBT data;
	int ret;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = name;
	key.size = strlen(name) + 1;

	ret = gdb->get(gdb, NULL, &key, &data, 0);

	if (ret == DB_NOTFOUND)
		return g_notfound;
 
	CBUG(ret);
	return * (unsigned *) data.data;
}

/* assumes strings of length 31 tops */
void gi_get(char * buffer, unsigned id) {
	DBT key;
	DBT pkey;
	DBT data;

	memset(&key, 0, sizeof(DBT));
	memset(&pkey, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &id;
	key.size = sizeof(id);;

	CBUG(igdb->pget(igdb, NULL, &key, &pkey, &data, 0));
	strlcpy(buffer, (char *) pkey.data, 31);
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
	debug("ge_insert %u -> %u : %u\n", ids[0], ids[1], value);
}

static int
ti_cmp(DB *sec, const DBT *a_r, const DBT *b_r, size_t *locp)
{
	struct ti a, b;
	memcpy(&a, a_r->data, sizeof(struct ti));
	memcpy(&b, b_r->data, sizeof(struct ti));

	if (b.min > a.min)
		return -1;

	if (a.min > b.min)
		return 1;

	if (b.who > a.who)
		return -1;

	if (a.who > b.who)
		return 1;

	return 0;
}

static int
map_tidb_tiiddb(DB *sec, const DBT *key, const DBT *data, DBT *result) {
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(unsigned);
	/* result->data = &((struct ti *) key->data)->who; */
	result->data = &((struct ti *) data->data)->who;
	/* debug("map_tidb_tiiddb %u\n", *(unsigned *) result->data); */
	return 0;
}

static int
tiid_cmp(DB *sec, const DBT *a_r, const DBT *b_r, size_t *locp)
{
	unsigned	a = * (unsigned *) a_r->data,
						b = * (unsigned *) b_r->data;
	return b > a ? -1 : (a > b ? 1 : 0);
}

void ti_init() {
	CBUG(db_create(&tidb, dbe, 0)
			|| tidb->set_bt_compare(tidb, ti_cmp)
			|| tidb->open(tidb, NULL, NULL, "ti", DB_BTREE, DB_CREATE, 0664));

	CBUG(db_create(&tiiddb, dbe, 0)
			|| tiiddb->set_bt_compare(tiiddb, tiid_cmp)
			|| tiiddb->set_flags(tiiddb, DB_DUP)
			|| tiiddb->open(tiiddb, NULL, NULL, "tiid", DB_BTREE, DB_CREATE, 0664)
			|| tidb->associate(tidb, NULL, tiiddb, map_tidb_tiiddb, DB_CREATE));
}

void ti_insert(unsigned id, time_t start, time_t end) {
	struct ti ti = { .min = start, .max = end, .who = id };
	DBT key;
	DBT data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &ti;
	key.size = sizeof(ti);
	data.data = &ti;
	data.size = sizeof(ti);

	CBUG(tidb->put(tidb, NULL, &key, &data, 0));

	char startstr[64];
	strcpy(startstr, printtime(start));
	debug("ti_insert %u [%s, %s]\n", id, startstr, printtime(end));
}

void tiid_show() {
	DBC *cur;
	DBT key;
	DBT data;
	int ret, dbflags = DB_NEXT;

	CBUG(tiiddb->cursor(tiiddb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	while (1) {
		unsigned value;

		ret = cur->get(cur, &key, &data, dbflags);

		if (ret == DB_NOTFOUND)
			return;

		CBUG(ret);
		value = * (unsigned *) key.data;
		debug("tiid_show %u\n", value);
	}
}

void ti_finish_last(unsigned id, time_t end) {
	struct ti ti;
	/* DBT pkey; */
	DBC *cur;
	DBT key;
	DBT data;
	int res, dbflags = DB_SET;

	CBUG(tiiddb->cursor(tiiddb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	/* memset(&pkey, 0, sizeof(DBT)); */
	memset(&data, 0, sizeof(DBT));

	key.data = &id;
	key.size = sizeof(id);

	while (1) {
		res = cur->c_get(cur, &key, &data, dbflags);

		if (res == DB_NOTFOUND)
			break;

		CBUG(res);

		if (* (unsigned *) key.data != id)
			break;

		memcpy(&ti, data.data, sizeof(ti));
		dbflags = DB_NEXT;
	}

	CBUG(cur->c_get(cur, &key, &data, DB_PREV));

	ti.max = end;
	data.data = &ti;

	CBUG(tidb->put(tidb, NULL, &key, &data, 0));
	char startstr[64];
	strcpy(startstr, printtime(ti.min));
	debug("ti_finish_last %u [%s, %s]\n", ti.who, startstr, printtime(ti.max));
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

	char tsstr[64], startstr[64];
	strcpy(tsstr, printtime(ts));
	strcpy(startstr, printtime(start_ts));
	debug("process_pay %s %u %d [%s, %s]\n", tsstr, id, value, startstr, printtime(end_ts));
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

	debug("> %s", line);
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

void ge_show() {
	DBC *cur;
	DBT key, data;
	int ret, dbflags = DB_FIRST;

	CBUG(gedb->cursor(gedb, NULL, &cur, 0));

	memset(&data, 0, sizeof(DBT));
	memset(&key, 0, sizeof(DBT));

	while (1) {
		unsigned value;

		ret = cur->get(cur, &key, &data, dbflags);

		if (ret == DB_NOTFOUND)
			return;

		CBUG(ret);
		dbflags = DB_NEXT;
		value = * (unsigned *) data.data;
		if (value) {
			char from_name[32], to_name[32];

			gi_get(from_name, * (unsigned *) key.data);
			gi_get(to_name, ((unsigned *) key.data)[1]);

			if (value > 0)
				printf("%s owes %s %.2f€\n", to_name, from_name, ((float) value) / 100.0f);
			else
				printf("%s owes %s %.2f€\n", from_name, to_name, ((float) value) / 100.0f);
		}
	}
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
	ti_init();

	while ((linelen = getline(&line, &linesize, fp)) >= 0)
		process_line(line);

	free(line);

	if (ferror(fp))
		err(EXIT_FAILURE, "getline");

	ge_show();

	return EXIT_SUCCESS;
}
