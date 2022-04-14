/* SPDX-FileCopyrightText: 2022 Paulo Andre Azevedo Quirino
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
#include <ctype.h>

/* #define NDEBUG */
#ifdef NDEBUG
#define debug(...) do {} while (0)
#define CBUG(c) if (c) abort()
#else
#define debug(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)
#define CBUG(c) if (c) { fprintf(stderr, "CBUG! " #c " %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__); raise(SIGINT); } 
#endif

#define PAYER_TIP 1
#define USERNAME_MAX_LEN 32
#define DATE_MAX_LEN 20
#define CURRENCY_MAX_LEN 32

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

struct tidbs {
  DB *ti; // keys are struct ti_key, values are struct ti
  DB *max; // secondary DB (BTREE) with interval max as primary key
  DB *id; // secondary DB (BTREE) with ids as primary key
} pdbs, npdbs;

const time_t mtinf = (time_t) LONG_MIN; // minus infinite
const time_t tinf = (time_t) LONG_MAX; // infinite

DB *gdb = NULL; // graph primary DB (keys are usernames, values are user ids)
DB *igdb = NULL; // secondary DB to lookup usernames via ids

DB *gedb = NULL; // graph edge DB to lookup debt between participants (ids as key)

DB *whodb = NULL; // temporary DB for process_pay(). see ti_split()

static DB_ENV *dbe = NULL;

unsigned g_len = 0;
unsigned g_notfound = (unsigned) -1;

/* get timestamp from ISO-8601 date string */
static time_t
sscantime(char *buf)
{
	char *aux;
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	aux = strptime(buf, "%FT%T", &tm);
	if (!aux && !strptime(buf, "%F", &tm)) {
		debug("bad date '%s'\n", buf);
		err(EXIT_FAILURE, "Invalid date");
	}

	tm.tm_isdst = -1;
	return mktime(&tm);
}

// get ISO-8601 date string from timestamp
// only use this for debug (memory leak), or free pointer
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

static size_t
read_word(char *buf, char *input, size_t max_len)
{
	size_t ret = 0;

	for (; *input && isspace(*input); input++, ret++);

	for (;
			*input && !isspace(*input) && ret < max_len;
			input++, buf++, ret++)
		*buf = *input;

	CBUG(ret > max_len); // FIXME?
	*buf = '\0';

	return ret;
}

static unsigned g_find(char *name);

static size_t
read_id(unsigned *id, char *line)
{
	char username[USERNAME_MAX_LEN];
	size_t ret;

	ret = read_word(username, line, sizeof(username));
	*id = g_find(username);
	CBUG(*id == g_notfound);

	return ret;
}

static size_t
read_currency(int *target, char *line)
{
	char buf[CURRENCY_MAX_LEN];
	size_t len;

	len = read_word(buf, line, sizeof(buf));
	*target = (int) (strtof(buf, NULL) * 100.0f);

	return len;
}

static size_t
read_ts(time_t *target, char *line)
{
	char date_str[DATE_MAX_LEN];
	size_t ret;

	ret = read_word(date_str, line, sizeof(date_str));
	*target = sscantime(date_str);

	return ret;
}

static int
map_gdb_igdb(DB *sec, const DBT *key, const DBT *data, DBT *result)
{
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(unsigned);
	result->data = data->data;
	return 0;
}

static int
map_tidb_timaxdb(DB *sec, const DBT *key, const DBT *data, DBT *result)
{
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
map_tidb_tiiddb(DB *sec, const DBT *key, const DBT *data, DBT *result)
{
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

static int
tidbs_init(struct tidbs *dbs)
{
  return db_create(&dbs->ti, dbe, 0)
    || dbs->ti->open(dbs->ti, NULL, NULL, NULL, DB_HASH, DB_CREATE, 0664)

    || db_create(&dbs->max, dbe, 0)
    || dbs->max->set_bt_compare(dbs->max, tiid_cmp)
    || dbs->max->set_flags(dbs->max, DB_DUP)
    || dbs->max->open(dbs->max, NULL, NULL, NULL, DB_BTREE, DB_CREATE, 0664)
    || dbs->ti->associate(dbs->ti, NULL, dbs->max, map_tidb_timaxdb, DB_CREATE)

    || db_create(&dbs->id, dbe, 0)
    || dbs->id->set_bt_compare(dbs->id, tiid_cmp)
    || dbs->id->set_flags(dbs->id, DB_DUP)
    || dbs->id->open(dbs->id, NULL, NULL, NULL, DB_BTREE, DB_CREATE, 0664)
    || dbs->ti->associate(dbs->ti, NULL, dbs->id, map_tidb_tiiddb, DB_CREATE);
}

static void
dbs_init()
{
	CBUG(
			db_create(&gdb, dbe, 0)
			|| gdb->open(gdb, NULL, NULL, NULL, DB_HASH, DB_CREATE, 0664)

			|| db_create(&igdb, dbe, 0)
			|| igdb->open(igdb, NULL, NULL, NULL, DB_HASH, DB_CREATE, 0664)
			|| gdb->associate(gdb, NULL, igdb, map_gdb_igdb, DB_CREATE)

			|| db_create(&gedb, dbe, 0)
			|| gedb->open(gedb, NULL, NULL, NULL, DB_HASH, DB_CREATE, 0664)

      || tidbs_init(&pdbs)
      || tidbs_init(&npdbs)

			|| db_create(&whodb, dbe, 0)
			|| gdb->open(whodb, NULL, NULL, NULL, DB_HASH, DB_CREATE, 0664)
			);
}

unsigned
g_insert(char *name)
{
	DBT key, data;

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

static unsigned
g_find(char *name)
{
	DBT key, data;
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
static void
gi_get(char * buffer, unsigned id)
{
	DBT key, pkey, data;

	memset(&key, 0, sizeof(DBT));
	memset(&pkey, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &id;
	key.size = sizeof(id);;

	CBUG(igdb->pget(igdb, NULL, &key, &pkey, &data, 0));
	strlcpy(buffer, (char *) pkey.data, 31);
}

static int
ge_get(unsigned id0, unsigned id1)
{
	unsigned ids[2];
	DBT key, data;
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

static void
ge_add(unsigned id_from, unsigned id_to, int value)
{
	unsigned ids[2];
	DBT key, data;
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
		value = - cvalue - value; // FIXME?
	} else {
		debug("ge_add %u -> %u : %d\n", ids[0], ids[1], value);
		value = cvalue + value;
	}

	data.data = &value;
	data.size = sizeof(value);

	CBUG(gedb->put(gedb, NULL, &key, &data, 0));
}

static void
who_drop()
{
	DBC *cur;
	DBT key, data;
	int res;

	CBUG(whodb->cursor(whodb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	while (1) {
		res = cur->c_get(cur, &key, &data, DB_NEXT);

		if (res == DB_NOTFOUND)
			break;

		CBUG(res);
		CBUG(cur->c_del(cur, 0));
	}
}

static void
who_insert(unsigned who)
{
	DBT key, data;

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

static void
who_list(struct ti_split_f *ti_split_f)
{
	DBC *cur;
	DBT key, data;
	int res;

	CBUG(whodb->cursor(whodb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));
	ti_split_f->entries_l = 0;

	while (1) {
		struct who_entry *entry;

		res = cur->c_get(cur, &key, &data, DB_NEXT);

		if (res == DB_NOTFOUND)
			break;

		CBUG(res);

		entry = (struct who_entry *) malloc(sizeof(struct who_entry));

		entry->who = * (unsigned *) key.data;
		SLIST_INSERT_HEAD(&ti_split_f->entries, entry, entry);
		ti_split_f->entries_l++;
	}
}

static void
ti_insert(struct tidbs dbs, unsigned id, time_t start, time_t end)
{
	struct ti ti = { .min = start, .max = end, .who = id };
	struct ti_key ti_key = { .min = start, .who = id };
	DBT key, data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &ti_key;
	key.size = sizeof(ti_key);
	data.data = &ti;
	data.size = sizeof(ti);

	CBUG(dbs.ti->put(dbs.ti, NULL, &key, &data, 0));

	debug("ti_insert %u [%s, %s]\n", id, printtime(start), printtime(end));
}

static void
ti_finish_last(struct tidbs dbs, unsigned id, time_t end)
{
	struct ti ti;
	struct ti_key ti_key;
	DBC *cur;
	DBT key, pkey, data;
	int res, dbflags = DB_SET;

	CBUG(dbs.id->cursor(dbs.id, NULL, &cur, 0));

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

	CBUG(dbs.ti->put(dbs.ti, NULL, &pkey, &data, 0));
	debug("ti_finish_last %u [%s, %s]\n",
			ti.who, printtime(ti.min), printtime(ti.max));
}

// TODO max 32 matches prevent buffer overflow
static inline size_t
ti_intersect(struct tidbs dbs, struct ti * matched, time_t start_ts, time_t end_ts)
{
	struct ti tmp;
	DBC *cur;
	DBT key, data;
	size_t matched_l = 0;
	int ret, dbflags = DB_SET_RANGE;

	CBUG(dbs.max->cursor(dbs.max, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &start_ts;
	key.size = sizeof(time_t);

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
			debug("match %u [%s, %s]\n",
					tmp.who, printtime(tmp.min), printtime(tmp.max));
		}
	}

	return matched_l;
}

static inline size_t
ti_pintersect(struct tidbs dbs, struct ti * matched, time_t ts)
{
  return ti_intersect(dbs, matched, ts, ts);
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

static void
process_start(time_t ts, char *line)
{
	char username[USERNAME_MAX_LEN];
	unsigned id;

	read_word(username, line, sizeof(username));
	id = g_insert(username);
	ti_insert(pdbs, id, ts, tinf);
	ti_insert(npdbs, id, ts, tinf);
}

static void
process_stop(time_t ts, char *line)
{
	char username[USERNAME_MAX_LEN];
	unsigned id;

	read_word(username, line, sizeof(username));
	id = g_find(username);

	if (id != g_notfound) {
		ti_finish_last(pdbs, id, ts);
		ti_finish_last(npdbs, id, ts);
  } else {
		id = g_insert(username);
		ti_insert(pdbs, id, mtinf, ts);
		ti_insert(npdbs, id, mtinf, ts);
	}
}

static void
process_transfer(time_t ts, char *line)
{
	unsigned id_from, id_to;
	int value;

	line += read_id(&id_from, line);
	line += read_id(&id_to, line);
	line += read_currency(&value, line);

	ge_add(id_from, id_to, value);
}

// https://softwareengineering.stackexchange.com/questions/363091/split-overlapping-ranges-into-all-unique-ranges/363096#363096
static void
process_pay(time_t ts, char *line)
{
	struct ti matches[32];
	struct ti_split_f ti_split_f_arr[64];
	size_t matches_l, ti_split_f_n = 0;
	unsigned id;
	int value;
	time_t start_ts, end_ts;

	line += read_id(&id, line);
	line += read_currency(&value, line);
	line += read_ts(&start_ts, line);
	line += read_ts(&end_ts, line);

	matches_l = ti_intersect(pdbs, matches, start_ts, end_ts);

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
		int cost = PAYER_TIP + interval * value / (ti_split_f->entries_l * bill_interval);

		SLIST_FOREACH_SAFE(var, &ti_split_f->entries, entry, tmp)
			if (var->who != id)
				ge_add(id, var->who, cost);
	}

	debug("process_pay %s %u %d [%s, %s]\n", printtime(ts), id, value, printtime(start_ts), printtime(end_ts));
}

static void
process_pause(time_t ts, char *line)
{
	unsigned id;

	read_id(&id, line);

	// TODO assert interval for id at this ts
	ti_finish_last(pdbs, id, ts);
}

static void
process_resume(time_t ts, char *line)
{
	unsigned id;

	read_id(&id, line);

	// TODO assert no interval for id at this ts
	ti_insert(pdbs, id, ts, tinf);
}

static void
process_buy(time_t ts, char *line)
{
	struct ti matches[32];
  size_t matches_l;
	unsigned id;
	int value, i, dvalue;

	line += read_id(&id, line);
	read_currency(&value, line);

  matches_l = ti_pintersect(npdbs, matches, ts);
  dvalue = value / matches_l + PAYER_TIP;
  debug("process_buy %d %lu %d\n", value, matches_l, dvalue);

  // assert there are not multiple intervals with the same id?
  for (i = 0; i < matches_l; i++)
    if (matches[i].who != id)
      ge_add(id, matches[i].who, dvalue);
}

static void
process_line(char *line)
{
	char op_type_str[9], date_str[DATE_MAX_LEN];
	time_t ts;

	if (line[0] == '#')
		return;

	debug("> %s", line);
	line += read_word(op_type_str, line, sizeof(op_type_str));
	line += read_ts(&ts, line);

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
	case 'B':
		return process_buy(ts, line);
	}

error:
	err(EXIT_FAILURE, "Invalid format");
}

static void
ge_show()
{
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
			char from_name[USERNAME_MAX_LEN], to_name[USERNAME_MAX_LEN];

			gi_get(from_name, * (unsigned *) key.data);
			gi_get(to_name, ((unsigned *) key.data)[1]);

			if (value > 0)
				printf("%s owes %s %.2f€\n", to_name, from_name, ((float) value) / 100.0f);
			else
				printf("%s owes %s %.2f€\n", from_name, to_name, - ((float) value) / 100.0f);
		}
	}
}

int
main()
{
	FILE *fp = fopen("data.txt", "r");
	char *line = NULL;
	ssize_t linelen;
	size_t linesize;
	int ret;

	if (fp == NULL)
		err(EXIT_FAILURE, "Unable to open file");

	dbs_init();

	while ((linelen = getline(&line, &linesize, fp)) >= 0)
		process_line(line);

	free(line);

	if (ferror(fp))
		err(EXIT_FAILURE, "getline");

	ge_show();

	return EXIT_SUCCESS;
}