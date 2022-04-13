#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <err.h>
#include <signal.h>
#ifdef __OpenBSD__
#include <db4/db.h>
#else
#include <db.h>
#endif
#include <sys/queue.h>

#define NDEBUG
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

struct ti_key {
	time_t min;
	unsigned who;
};

struct ti_split_i {
	time_t ts;
	int max;
	unsigned who;
};

struct who_entry {
	unsigned who;
	SLIST_ENTRY(who_entry) entry;
};

SLIST_HEAD(who_list, who_entry);

struct ti_split_f {
	time_t min;
	time_t max;
	size_t entries_l;
	struct who_list entries;
};

const time_t mtinf = (time_t) LONG_MIN;
const time_t tinf = (time_t) LONG_MAX;

DB *gdb = NULL;
DB *igdb = NULL;
DB *gedb = NULL;
DB *tidb = NULL;
DB *timaxdb = NULL;
DB *tiiddb = NULL;
DB *whodb = NULL; // temp
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

// memory leaky
char * printtime(time_t ts) {
	char *buf;
	struct tm tm;

	if (ts == mtinf)
		return "-inf";

	if (ts == tinf)
		return "inf";

	buf = (char *) malloc(64);
	tm = *localtime(&ts);

	if (tm.tm_sec || tm.tm_min || tm.tm_hour)
		strftime(buf, 64, "%FT%T", &tm);
	else
		strftime(buf, 64, "%F", &tm);

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

void ge_add(unsigned id_from, unsigned id_to, int value) {
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

	if (id_from > id_to) {
		debug("ge_add %u -> %u : %d\n", ids[0], ids[1], -value);
		value = - cvalue - value;
	} else {
		debug("ge_add %u -> %u : %d\n", ids[0], ids[1], value);
		value = cvalue + value;
	}

	data.data = &value;
	data.size = sizeof(value);

	CBUG(gedb->put(gedb, NULL, &key, &data, 0));
}

void who_init() {
	CBUG(db_create(&whodb, dbe, 0)
			|| gdb->open(whodb, NULL, NULL, "who", DB_HASH, DB_CREATE, 0664));
}

void who_drop() {
	DBC *cur;
	DBT key;
	DBT data;
	int res, dbflags = DB_FIRST;

	CBUG(whodb->cursor(whodb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	while (1) {
		res = cur->c_get(cur, &key, &data, dbflags);

		if (res == DB_NOTFOUND)
			break;

		CBUG(res);
		CBUG(cur->c_del(cur, 0));
		dbflags = DB_NEXT;
	}
}

void who_insert(unsigned who) {
	DBT key;
	DBT data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &who;
	key.size = sizeof(who);
	data.data = NULL;
	data.size = 0;

	CBUG(whodb->put(whodb, NULL, &key, &data, 0));
}

void who_remove(unsigned who) {
	DBT key;

	memset(&key, 0, sizeof(DBT));

	key.data = &who;
	key.size = sizeof(who);

	CBUG(whodb->del(whodb, NULL, &key, 0));
}

void who_list(struct ti_split_f *ti_split_f) {
	DBC *cur;
	DBT key;
	DBT data;
	int res, dbflags = DB_FIRST;

	CBUG(whodb->cursor(whodb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));
	ti_split_f->entries_l = 0;

	while (1) {
		res = cur->c_get(cur, &key, &data, dbflags);

		if (res == DB_NOTFOUND)
			break;

		CBUG(res);

		struct who_entry *entry = (struct who_entry *) malloc(sizeof(struct who_entry));

		entry->who = * (unsigned *) key.data;
		SLIST_INSERT_HEAD(&ti_split_f->entries, entry, entry);
		ti_split_f->entries_l++;
		dbflags = DB_NEXT;
	}
}

static int
map_tidb_timaxdb(DB *sec, const DBT *key, const DBT *data, DBT *result) {
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(time_t);
	result->data = &((struct ti *) data->data)->max;
	return 0;
}

static int
timax_cmp(DB *sec, const DBT *a_r, const DBT *b_r, size_t *locp)
{
	time_t		a = * (time_t *) a_r->data,
						b = * (time_t *) b_r->data;
	return b > a ? -1 : (a > b ? 1 : 0);
}

static int
map_tidb_tiiddb(DB *sec, const DBT *key, const DBT *data, DBT *result) {
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(unsigned);
	result->data = &((struct ti *) data->data)->who;
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
			|| tidb->open(tidb, NULL, NULL, "ti", DB_HASH, DB_CREATE, 0664));

	CBUG(db_create(&timaxdb, dbe, 0)
			|| timaxdb->set_bt_compare(timaxdb, tiid_cmp)
			|| timaxdb->set_flags(timaxdb, DB_DUP)
			|| timaxdb->open(timaxdb, NULL, NULL, "timax", DB_BTREE, DB_CREATE, 0664)
			|| tidb->associate(tidb, NULL, timaxdb, map_tidb_timaxdb, DB_CREATE));

	CBUG(db_create(&tiiddb, dbe, 0)
			|| tiiddb->set_bt_compare(tiiddb, tiid_cmp)
			|| tiiddb->set_flags(tiiddb, DB_DUP)
			|| tiiddb->open(tiiddb, NULL, NULL, "tiid", DB_BTREE, DB_CREATE, 0664)
			|| tidb->associate(tidb, NULL, tiiddb, map_tidb_tiiddb, DB_CREATE));
}

void ti_insert(unsigned id, time_t start, time_t end) {
	struct ti ti = { .min = start, .max = end, .who = id };
	struct ti_key ti_key = { .min = start, .who = id };
	DBT key;
	DBT data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &ti_key;
	key.size = sizeof(ti_key);
	data.data = &ti;
	data.size = sizeof(ti);

	CBUG(tidb->put(tidb, NULL, &key, &data, 0));

	debug("ti_insert %u [%s, %s]\n", id, printtime(start), printtime(end));
}

void ti_finish_last(unsigned id, time_t end) {
	struct ti ti;
	struct ti_key ti_key;
	DBC *cur;
	DBT key;
	DBT pkey;
	DBT data;
	int res, dbflags = DB_SET;

	CBUG(tiiddb->cursor(tiiddb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
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

	memset(&pkey, 0, sizeof(DBT));
	CBUG(cur->c_pget(cur, &key, &pkey, &data, DB_PREV));

	ti.max = end;
	data.data = &ti;

	CBUG(tidb->put(tidb, NULL, &pkey, &data, 0));
	debug("ti_finish_last %u [%s, %s]\n", ti.who, printtime(ti.min), printtime(ti.max));
}

// TODO max 32 matches
size_t ti_intersect(struct ti * matched, time_t start_ts, time_t end_ts)
{
	struct ti tmp;
	DBC *cur;
	DBT key;
	DBT data;
	size_t matched_l = 0;
	int ret, dbflags = DB_SET_RANGE;

	CBUG(timaxdb->cursor(timaxdb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &start_ts;
	key.size = sizeof(time_t);

	// walk right until end
	while (1) {
		ret = cur->c_get(cur, &key, &data, dbflags);

		if (ret == DB_NOTFOUND)
			break;

		CBUG(ret);

		dbflags = DB_NEXT;
		memcpy(&tmp, data.data, sizeof(struct ti));

		if (tmp.max >= start_ts && tmp.min < end_ts) {
			// its a match
			memcpy(&matched[matched_l++], &tmp, sizeof(struct ti));
			debug("match %u [%s, %s]\n", tmp.who, printtime(tmp.min), printtime(tmp.max));
		}
	}

	return matched_l;
}

static int
ti_split_i_cmp(const void *ap, const void *bp)
{
	struct ti_split_i a, b;
	memcpy(&a, ap, sizeof(struct ti_split_i));
	memcpy(&b, bp, sizeof(struct ti_split_i));
	if (b.ts > a.ts)
		return -1;
	if (a.ts > b.ts)
		return 1;
	if (b.max > a.max)
		return -1;
	if (a.max > b.max)
		return 1;
	return 0;
}

size_t
ti_split(struct ti_split_f *ti_split_f_arr, struct ti *matches, size_t matches_l)
{
	size_t ti_split_f_n;
	int i;
	struct ti_split_i ti_split_i_arr[matches_l * 2];

	for (i = 0; i < matches_l * 2; i += 2) {
		struct ti_split_i *ti_split_i = &ti_split_i_arr[i];
		struct ti *match = &matches[i / 2];

		ti_split_i->ts = match->min;
		ti_split_i->max = 0;
		ti_split_i->who = match->who;

		ti_split_i++;
		ti_split_i->ts = match->max;
		ti_split_i->max = 1;
		ti_split_i->who = match->who;
	}

	qsort(ti_split_i_arr, matches_l * 2, sizeof(struct ti_split_i), ti_split_i_cmp);

	who_drop();
	ti_split_f_n = 0;

	for (i = 0; i < matches_l * 2 - 1; i++) {
		struct ti_split_i *ti_split_i = &ti_split_i_arr[i];
		struct ti_split_i *ti_split_i2 = &ti_split_i_arr[i+1];
		time_t n, m;

		if (ti_split_i->max)
			who_remove(ti_split_i->who);
		else
			who_insert(ti_split_i->who);

		n = ti_split_i->ts;
		m = ti_split_i2->ts;

		if (n == m)
			continue;

		struct ti_split_f *ti_split_f = &ti_split_f_arr[ti_split_f_n];
		ti_split_f->min = n;
		ti_split_f->max = m;
		ti_split_f->entries_l = 0;
		SLIST_INIT(&ti_split_f->entries);
		who_list(ti_split_f);
		ti_split_f_n++;

		debug("ti_split_f [%s, %s] { ", printtime(n), printtime(m));

		struct who_entry *var, *tmp;
		SLIST_FOREACH_SAFE(var, &ti_split_f->entries, entry, tmp)
			debug("%u ", var->who);
		debug("}%d\n", 0);
	}

	return ti_split_f_n;
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
	ge_add(id_from, id_to, value);
}

// https://softwareengineering.stackexchange.com/questions/363091/split-overlapping-ranges-into-all-unique-ranges/363096#363096
void process_pay(time_t ts, char *line) {
	struct ti matches[32];
	struct ti_split_f ti_split_f_arr[64];
	char username[32], start_date_str[64], end_date_str[64];
	size_t matches_l, ti_split_f_n = 0;
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

	matches_l = ti_intersect(matches, start_ts, end_ts);

	// adjust min max to match query interval (needed?)
	{
		int i;
		for (i = 0; i < matches_l; i++) {
			struct ti *match = &matches[i];
			if (match->min < start_ts)
				match->min = start_ts;
			if (match->max > end_ts)
				match->max = end_ts;
		}
	}

	debug("%lu matches\n", matches_l);
	ti_split_f_n = ti_split(ti_split_f_arr, matches, matches_l);

	time_t bill_interval = end_ts - start_ts;

	int i = 0;
	for (i = 0; i < ti_split_f_n; i++) {
		struct ti_split_f *ti_split_f = &ti_split_f_arr[i];
		struct who_entry *var, *tmp;
		time_t interval = ti_split_f->max - ti_split_f->min;
		int cost = interval * value / (ti_split_f->entries_l * bill_interval);

		SLIST_FOREACH_SAFE(var, &ti_split_f->entries, entry, tmp)
			if (var->who != id)
				ge_add(id, var->who, cost);
	}

	debug("process_pay %s %u %d [%s, %s]\n", printtime(ts), id, value, printtime(start_ts), printtime(end_ts));
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
	int ret;

	CBUG(gedb->cursor(gedb, NULL, &cur, 0));

	memset(&data, 0, sizeof(DBT));
	memset(&key, 0, sizeof(DBT));

	while (1) {
		int value;

		ret = cur->get(cur, &key, &data, DB_NEXT);

		if (ret == DB_NOTFOUND)
			return;

		CBUG(ret);
		value = * (unsigned *) data.data;
		if (value) {
			char from_name[32], to_name[32];

			gi_get(from_name, * (unsigned *) key.data);
			gi_get(to_name, ((unsigned *) key.data)[1]);

			if (value > 0)
				printf("%s owes %s %.2f€\n", to_name, from_name, ((float) value) / 100.0f);
			else
				printf("%s owes %s %.2f€\n", from_name, to_name, - ((float) value) / 100.0f);
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
	who_init();

	while ((linelen = getline(&line, &linesize, fp)) >= 0)
		process_line(line);

	free(line);

	if (ferror(fp))
		err(EXIT_FAILURE, "getline");

	ge_show();

	return EXIT_SUCCESS;
}
