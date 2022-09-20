/* SPDX-FileCopyrightText: 2022 Paulo Andre Azevedo Quirino
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * It is important that this program can be understood by people who are not
 * programmers, so I'm adding an in-depth description of the algorithm as
 * comments. The most important ones will be at the top of the functions. To
 * understand the algorithm, I recommend that you start from the bottom of this
 * file, and scroll up as needed. It might also be useful to lookup the
 * definition of specific functions if you want to know how they work in more
 * detail. For that it is enough that you search for "^process_start" for
 * example. It is also recommended that before you do that, you read the
 * README.md to understand the format of the input data files.
 *
 * Mind you, dates are expressed in ISO-8601 format to users, but internally
 * we use unix timestamps. This is to facilitate a user to analyse the input
 * data easily while permitting the software to evaluate datetimes
 * mathematically in a consistent way.
 *
 * Person ids are also particular in this way. In the input file they are
 * textual, but internally we use numeric ids to which they correspond.
 *
 * Currency values are read as float but internally they are integers.
 *
 * The general idea of the algorithm involves a few data structures:
 *
 * One of them is a weighted and directed graph, in which each node represents
 * a person, and the edges connecting the nodes represent the accumulated debt
 * between them.
 *
 * Another is a binary search tree (BST) that stores intervals of time, that
 * we query in order to find out who was present during the billing periods,
 * etc. Actually, there are two of these kinds of BSTs. One That only stores
 * intervals where the person is actually in the house (BST A), another that
 * stores intervals where the person is renting a room there, but might not be
 * present (BST B).
 *
 * Jump to the main function when you are ready to check out how it all works.
 *
 * Happy reading!
 */

#define _BSD_SOURCE
#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __OpenBSD__
#define TS_FMT "%lld"
#include <db4/db.h>
#include <sys/queue.h>
#else
#define TS_FMT "%ld"
#ifdef ALPINE
#include <stdint.h>
#include <db4/db.h>
#else
#include <db.h>
#endif
#include <bsd/sys/queue.h>
#endif
#include <time.h>

#define ndebug(fmt, ...) \
	if (pflags & PF_DEBUG) \
		fprintf(stderr, fmt, ##__VA_ARGS__)

#define graph_head(id, flags) \
	if (pflags & PF_GRAPH) \
		who_graph_line(id, flags);

#define debug(fmt, ...) \
	if (pflags & PF_DEBUG) { \
		graph_head(-1, 0); \
		fprintf(stderr, fmt, ##__VA_ARGS__); \
	}

#define CBUG(c) if (c) { fprintf(stderr, "CBUG! " #c " %s:%s:%d\n", \
		__FILE__, __FUNCTION__, __LINE__); raise(SIGINT); }

#define PAYER_TIP 1
#define USERNAME_MAX_LEN 32
#define DATE_MAX_LEN 20
#define CURRENCY_MAX_LEN 32

struct ti {
	time_t min, max;
	unsigned who;
};

struct isplit {
	time_t ts;
	int max;
	unsigned who;
};

struct who {
	unsigned who;
	SLIST_ENTRY(who) entry;
};

SLIST_HEAD(who_list, who);

struct match {
	struct ti ti;
	STAILQ_ENTRY(match) entry;
};

STAILQ_HEAD(match_stailq, match);

struct split {
	time_t min;
	time_t max;
	size_t who_list_l;
	struct who_list who_list;
	TAILQ_ENTRY(split) entry;
};

TAILQ_HEAD(split_tailq, split);

struct who_list who;

struct tidbs {
	DB *ti; // keys and values are struct ti
	DB *max; // secondary DB (BTREE) with interval max as key
	DB *id; // secondary DB (BTREE) with ids as primary key
} pdbs, npdbs;

enum pflags {
	PF_GRAPH = 1,
	PF_DEBUG = 2,
	PF_HUMAN = 4,
	PF_MACHINE = 8,
};

const time_t mtinf = (time_t) LONG_MIN; // minus infinite
const time_t tinf = (time_t) LONG_MAX; // infinite

DB *gdb = NULL; // graph primary DB (keys are usernames, values are user ids)
DB *igdb = NULL; // secondary DB to lookup usernames via ids

DB *gedb = NULL; // graph edge DB to lookup debt between participants (ids as key)

DB *whodb = NULL; // temporary DB for process_pay(). see ti_split()

DB *gwhodb = NULL; // DB for displaying timeline graph

static DB_ENV *dbe = NULL;

unsigned g_len = 0;
unsigned g_notfound = (unsigned) -1;
unsigned pflags = 0;

/* create a person that we can insert in the db of present people */
static inline struct who *
who_create(unsigned who)
{
	struct who *entry =
		(struct who *) malloc(sizeof(struct who));

	entry->who = who;
	return entry;
}

/* insert a list of present people into a split */
static int
who_list(DB *whodb, struct who_list *who)
{
	DBC *cur;
	DBT key, data;
	int res;
	int len = 0;

	CBUG(whodb->cursor(whodb, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	while (1) {
		struct who *entry;

		res = cur->c_get(cur, &key, &data, DB_NEXT);

		if (res == DB_NOTFOUND)
			break;

		CBUG(res);

		entry = who_create(* (unsigned *) key.data);;
		SLIST_INSERT_HEAD(who, entry, entry);
		len++;
	}

	CBUG(cur->close(cur));
	return len;
}

static inline void
who_graph_line(unsigned who_does, unsigned flags) {
	struct who_list whol;
	struct who *who;
	int len, did_id = 0;

	SLIST_INIT(&whol);
	len = who_list(gwhodb, &whol);

	if (flags)
		SLIST_FOREACH(who, &whol, entry) {
			if (who_does == who->who) {
				if (flags <= 2 || flags == 5)
					fputc('*', stderr);
			} else {
				switch (flags) {
				case 1:
					fputc('|', stderr);
					break;
				case 2:
					fputc('|', stderr);
					break;
				case 3:
					if (who_does > who->who)
						fputc('/', stderr);
					else
						fputc('|', stderr);
					break;
				case 4:
					if (who_does > who->who)
						fputc('\\', stderr);
					else
						fputc('|', stderr);
					break;
				case 5:
					fputc('|', stderr);
					break;
				}
			}
		}
	else
		SLIST_FOREACH(who, &whol, entry)
			fputc('|', stderr);

	fputc(' ', stderr);
}

/* get timestamp from ISO-8601 date string */
static time_t
sscantime(char *buf)
{
	char *aux;
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	aux = strptime(buf, "%Y-%m-%dTT%H:%M:%S", &tm);
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

/******
 * read functions
 ******/

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

static unsigned g_find(char *name);

/* read person nickname and convert it to existing numeric id */
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

/* read currency value and convert it to int */
static size_t
read_currency(int *target, char *line)
{
	char buf[CURRENCY_MAX_LEN];
	size_t len;

	len = read_word(buf, line, sizeof(buf));
	*target = (int) (strtof(buf, NULL) * 100.0f);

	return len;
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

/******
 * making secondary indices
 ******/

/* create person id to nickname HASH keys from nickname to person id HASH data
 */
static int
map_gdb_igdb(DB *sec, const DBT *key, const DBT *data, DBT *result)
{
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(unsigned);
	result->data = data->data;
	return 0;
}

/* create time interval BTREE keys from time interval HASH db*/
static int
map_tidb_timaxdb(DB *sec, const DBT *key, const DBT *data, DBT *result)
{
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(time_t);
	result->data = &((struct ti *) data->data)->max;
	return 0;
}

/* create id BTREE keys from time interval HASH db */
static int
map_tidb_tiiddb(DB *sec, const DBT *key, const DBT *data, DBT *result)
{
	memset(result, 0, sizeof(DBT));
	result->size = sizeof(unsigned);
	result->data = &((struct ti *) data->data)->who;
	return 0;
}

/******
 * key ordering compare functions
 ******/

/* compare two time intervals (for sorting BST items) */
static int
#ifdef __APPLE__
timax_cmp(DB *sec, const DBT *a_r, const DBT *b_r, size_t *locp)
#else
timax_cmp(DB *sec, const DBT *a_r, const DBT *b_r)
#endif
{
	time_t	a = * (time_t *) a_r->data,
		b = * (time_t *) b_r->data;
	return b > a ? -1 : (a > b ? 1 : 0);
}

/* compare two person ids (for sorting BST items) */
static int
#ifdef __APPLE__
tiid_cmp(DB *sec, const DBT *a_r, const DBT *b_r, size_t *locp)
#else
tiid_cmp(DB *sec, const DBT *a_r, const DBT *b_r)
#endif
{
	unsigned a = * (unsigned *) a_r->data,
		 b = * (unsigned *) b_r->data;
	return b > a ? -1 : (a > b ? 1 : 0);
}

/******
 * Database initializers
 ******/

/* initialize ti dbs */
static int
tidbs_init(struct tidbs *dbs)
{
	return db_create(&dbs->ti, dbe, 0)
		|| dbs->ti->open(dbs->ti, NULL, NULL, NULL, DB_HASH, DB_CREATE, 0664)

		|| db_create(&dbs->max, dbe, 0)
		|| dbs->max->set_bt_compare(dbs->max, timax_cmp)
		|| dbs->max->set_flags(dbs->max, DB_DUP)
		|| dbs->max->open(dbs->max, NULL, NULL, NULL, DB_BTREE, DB_CREATE, 0664)
		|| dbs->ti->associate(dbs->ti, NULL, dbs->max, map_tidb_timaxdb, DB_CREATE | DB_IMMUTABLE_KEY)

		|| db_create(&dbs->id, dbe, 0)
		|| dbs->id->set_bt_compare(dbs->id, tiid_cmp)
		|| dbs->id->set_flags(dbs->id, DB_DUP)
		|| dbs->id->open(dbs->id, NULL, NULL, NULL, DB_BTREE, DB_CREATE, 0664)
		|| dbs->ti->associate(dbs->ti, NULL, dbs->id, map_tidb_tiiddb, DB_CREATE | DB_IMMUTABLE_KEY);
}

/* Initialize all dbs */
static void
dbs_init()
{
	int ret = db_create(&gdb, dbe, 0)
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

		|| db_create(&gwhodb, dbe, 0)
		|| gdb->open(gwhodb, NULL, NULL, NULL, DB_BTREE, DB_CREATE, 0664);
	CBUG(ret);
}

/******
 * g (usernames to user ids) related functions
 ******/

/* insert new person id (auto-generated) */
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
	/* debug("g_insert %s id %u\n", name, g_len); */
	return g_len++;
}

/* find existing person id from their nickname */
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

/******
 * gi (ids to usernames) functions
 ******/

/* get person nickname from numeric id
 *
 * assumes strings of length 31 tops
 */
static char *
gi_get(unsigned id)
{
	DBT key, pkey, data;

	memset(&key, 0, sizeof(DBT));
	memset(&pkey, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &id;
	key.size = sizeof(id);;

	CBUG(igdb->pget(igdb, NULL, &key, &pkey, &data, 0));
	return pkey.data;
}

/******
 * ge (graph edges) functions
 ******/

/* get debt between people */
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

/* add debt between people */
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

	if (id_from > id_to)
		value = - cvalue - value;
	else
		value = cvalue + value;

	data.data = &value;
	data.size = sizeof(value);

	CBUG(gedb->put(gedb, NULL, &key, &data, 0));
}

/* show debt between a pair of two people */
static inline void
ge_show(unsigned from, unsigned to, int value)
{
	char *from_name_i = gi_get(from),
	     from_name[USERNAME_MAX_LEN];
	char *to_name;
	memcpy(from_name, from_name_i, strlen(from_name_i) + 1);
	to_name = gi_get(to);

	if (value > 0)
		printf("%s owes %s %.2f€\n", to_name, from_name,
				((float) value) / 100.0f);
	else
		printf("%s owes %s %.2f€\n", from_name, to_name,
				- ((float) value) / 100.0f);
}

/* show all debt between people */
static void
ge_show_all()
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
		if (value)
			ge_show(* (unsigned *) key.data,
					((unsigned *) key.data)[1], value);
	}

	CBUG(cur->close(cur));
}

/******
 * who (db of "current" people, for use in split calculation) related functions
 ******/

/* drop the db of present people */
static void
who_drop(DB *whodb)
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

	CBUG(cur->close(cur));
}

/* put a person in the db of present people */
static void
who_insert(DB *whodb, unsigned who)
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

/* remove a person from the db of present people */
static void
who_remove(DB *whodb, unsigned who) {
	DBT key;

	memset(&key, 0, sizeof(DBT));

	key.data = &who;
	key.size = sizeof(who);

	CBUG(whodb->del(whodb, NULL, &key, 0));
}

/******
 * ti (struct ti to struct ti primary db) related functions
 ******/
 
/* insert a time interval into an AVL */
static void
ti_insert(struct tidbs *dbs, unsigned id, time_t start, time_t end)
{
	struct ti ti = { .min = start, .max = end, .who = id };
	DBT key, data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &ti;
	key.size = sizeof(ti);
	data.data = &ti;
	data.size = sizeof(ti);

	CBUG(dbs->ti->put(dbs->ti, NULL, &key, &data, 0));

	/* debug("ti_insert %u [%s, %s]\n", id, printtime(start), printtime(end)); */
}

/* finish the last found interval at the provided timestamp for a certain
 * person id
 */
static void
ti_finish_last(struct tidbs *dbs, unsigned id, time_t end)
{
	struct ti ti;
	DBT key, data;
	DBT pkey;
	DBC *cur;
	int res, dbflags = DB_SET;

	CBUG(dbs->id->cursor(dbs->id, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &id;
	key.size = sizeof(id);

	/* debug("ti_finish_last %d", id); */

	do {
		CBUG(cur->c_get(cur, &key, &data, dbflags)); // even DB_NOTFOUND
		/* ndebug(" %d", * (unsigned *) key.data); */
		CBUG(* (unsigned *) key.data != id);
		memcpy(&ti, data.data, sizeof(ti));
		dbflags = DB_NEXT;
		/* ndebug("(" TS_FMT "," TS_FMT ")", ti.min, ti.max); */
	} while (ti.max != tinf);

	/* ndebug("\n"); */
	CBUG(cur->del(cur, 0));
	cur->close(cur);
	memset(&key, 0, sizeof(DBT));
	key.data = data.data = &ti;
	key.size = data.size = sizeof(ti);
	/* ti.who = id; */
	ti.max = end;
	data.data = &ti;
	data.size = sizeof(ti);
	CBUG(dbs->ti->put(dbs->ti, NULL, &key, &data, 0));
	/* debug("ti_finish_last %u [%s, %s]\n", */
	/* 		ti.who, printtime(ti.min), printtime(ti.max)); */
}

/* intersect an interval with an AVL of intervals */
static inline unsigned
ti_intersect(struct tidbs *dbs, struct match_stailq *matches, time_t min, time_t max)
{
	struct ti tmp;
	DBC *cur;
	DBT key, data;
	int ret = 0, dbflags = DB_SET_RANGE;

	STAILQ_INIT(matches);
	CBUG(dbs->max->cursor(dbs->max, NULL, &cur, 0));

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = &min;
	key.size = sizeof(time_t);

	while (1) {
		int res = cur->c_get(cur, &key, &data, dbflags);

		if (res == DB_NOTFOUND)
			break;

		CBUG(res);

		dbflags = DB_NEXT;
		memcpy(&tmp, data.data, sizeof(struct ti));

		if (tmp.max >= min && tmp.min < max) {
			// its a match
			struct match *match = (struct match *) malloc(sizeof(struct match));
			memcpy(&match->ti, &tmp, sizeof(tmp));
			STAILQ_INSERT_TAIL(matches, match, entry);
			ret++;
		}
	}

	cur->close(cur);
	return ret;
}

/* intersect a point with an AVL of intervals */
static inline unsigned
ti_pintersect(struct tidbs *dbs, struct match_stailq *matches, time_t ts)
{
	return ti_intersect(dbs, matches, ts, ts);
}

/******
 * matches related functions
 ******/

/* debugs an array of matches */
static void
matches_debug(struct match_stailq *matches)
{
	struct match *match;
	int i;
	graph_head(-1, 0);
	fprintf(stderr, "matches_debug");
	STAILQ_FOREACH(match, matches, entry) {
		fprintf(stderr, " (%s, " TS_FMT ", " TS_FMT ")", gi_get(match->ti.who), match->ti.min, match->ti.max);
	}
	fputc('\n', stderr);
}

/* makes all provided matches lie within the provided interval [min, max] */
static inline void
matches_fix(struct match_stailq *matches, time_t min, time_t max)
{
	struct match *match;

	STAILQ_FOREACH(match, matches, entry) {
		if (match->ti.min < min)
			match->ti.min = min;
		if (match->ti.max > max)
			match->ti.max = max;
	}
}

/******
 * isplit related functions
 ******/

/* compares isplits, so that we can sort them */
static int
isplit_cmp(const void *ap, const void *bp)
{
	struct isplit a, b;
	memcpy(&a, ap, sizeof(struct isplit));
	memcpy(&b, bp, sizeof(struct isplit));
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

// assumes isplits is of size matches_l * 2
/* creates intermediary isplits */
static inline struct isplit *
isplits_create(struct match_stailq *matches, size_t matches_l) {
	struct isplit *isplits = (struct isplit *) malloc(sizeof(struct isplit) * matches_l * 2);
	struct match *match;
	unsigned i = 0;

	STAILQ_FOREACH(match, matches, entry) {
		struct isplit *isplit = isplits + i * 2;
		isplit->ts = match->ti.min;
		isplit->max = 0;
		isplit->who = match->ti.who;
		isplit++;
		isplit->ts = match->ti.max;
		isplit->max = 1;
		isplit->who = match->ti.who;
		i ++;
	};

	return isplits;
}

/* debugs intermediary isplits */
static inline void
isplits_debug(struct isplit *isplits, unsigned matches_l) {
	int i;
	struct isplit *isplit;

	fprintf(stderr, "isplits_debug ");
	for (i = 0; i < matches_l * 2; i++) {
		struct isplit *isplit = isplits + i;
		fprintf(stderr, "(" TS_FMT ", %d, %s) ", isplit->ts, isplit->max, gi_get(isplit->who));
	}
	fputc('\n', stderr);
}

/******
 * split related functions
 ******/

/* Creates one split from its interval, and the list of people that are present
 */
static inline struct split *
split_create(time_t min, time_t max)
{
	struct split *split = (struct split *) malloc(sizeof(struct split));
	split->min = min;
	split->max = max;
	SLIST_INIT(&split->who_list);
	split->who_list_l = who_list(whodb, &split->who_list);
	return split;
}

/* Creates splits from the intermediary isplit array */
static inline void
splits_create(
		struct split_tailq *splits,
		struct isplit *isplits,
		size_t matches_l)
{
	int i;

	TAILQ_INIT(splits);

	who_drop(whodb);
	for (i = 0; i < matches_l * 2 - 1; i++) {
		struct isplit *isplit = isplits + i;
		struct isplit *isplit2 = isplits + i + 1;
		struct split *split;
		time_t n, m;

		if (isplit->max)
			who_remove(whodb, isplit->who);
		else
			who_insert(whodb, isplit->who);

		n = isplit->ts;
		m = isplit2->ts;

		if (n == m)
			continue;

		split = split_create(n, m);
		TAILQ_INSERT_TAIL(splits, split, entry);
	}
}

/* Just something to debug a tail queue of splits */
static inline void
splits_debug(struct split_tailq *splits)
{
	struct split *split;

	graph_head(-1, 0);
	fprintf(stderr, "splits_debug ");
	TAILQ_FOREACH(split, splits, entry) {
		struct who *who, *tmp;
		fprintf(stderr, "(" TS_FMT ", " TS_FMT ", { ", split->min, split->max);

		SLIST_FOREACH_SAFE(who, &split->who_list, entry, tmp)
			fprintf(stderr, "%s ", gi_get(who->who));

		fprintf(stderr, "}) ");
	}
	fprintf(stderr, "\n");
}

/* From a list of matched intervals, this creates the tail queue of splits
 */
static void
splits_init(struct split_tailq *splits, struct match_stailq *matches, unsigned matches_l)
{
	struct isplit *isplits;
	struct isplit *isplit;
	struct isplit *buf;
	int i = 0;

	isplits = isplits_create(matches, matches_l);
	/* isplits_debug(isplits, matches_l); */
	qsort(isplits, matches_l * 2, sizeof(struct isplit), isplit_cmp);
	/* isplits_debug(isplits, matches_l); */
	splits_create(splits, isplits, matches_l);
	free(isplits);
}

/* Obtains a tail queue of splits from the intervals that intersect the query
 * interval [min, max]
 */
static void
splits_get(struct split_tailq *splits, struct tidbs *dbs, time_t min, time_t max)
{
	struct match_stailq matches;
	unsigned matches_l = ti_intersect(dbs, &matches, min, max);
	/* matches_debug(&matches); */
	matches_fix(&matches, min, max);
	/* matches_debug(&matches); */
	return splits_init(splits, &matches, matches_l);
}

/* Inserts a tail queue of splits within another, before the element provided
 */
static inline void
splits_concat_before(
		struct split_tailq *target,
		struct split_tailq *origin,
		struct split *before)
{
	struct split *split, *tmp;
	TAILQ_FOREACH_SAFE(split, origin, entry, tmp) {
		TAILQ_REMOVE(origin, split, entry);
		TAILQ_INSERT_BEFORE(before, split, entry);
	}
}

/* Fills the spaces between splits (or on empty splits) with splits from BST B,
 * in order to resolve the situation where none of the people are present for
 * periods of time within the billing period (the aforementined gaps).
 */
static inline void
splits_fill(struct split_tailq *splits, time_t min, time_t max)
{
	struct split *split, *tmp;
	struct split_tailq more_splits;
	time_t last_max;

	split = TAILQ_FIRST(splits);
	if (!split) {
		splits_get(splits, &npdbs, min, max);
		return;
	}

	last_max = min;

	if (split->min > last_max) {
		struct split_tailq more_splits;
		splits_get(&more_splits, &npdbs, last_max, split->min);
		splits_concat_before(splits, &more_splits, split);
	}

	last_max = split->max;

	TAILQ_FOREACH_SAFE(split, splits, entry, tmp) {
		if (!split->who_list_l) {
			struct split_tailq more_splits;
			splits_get(&more_splits, &npdbs, split->min, split->max);
			splits_concat_before(splits, &more_splits, split);
			TAILQ_REMOVE(splits, split, entry);
		}

		last_max = split->max;
	}

	if (max > last_max) {
		struct split_tailq more_splits;
		splits_get(&more_splits, &npdbs, last_max, max);
		TAILQ_CONCAT(splits, &more_splits, entry);
	}
}

/* adds debt for each person of the tail queue of splits to the payer of the
 * bill
 */
static inline void
splits_pay(
		struct split_tailq *splits, unsigned payer,
		int value, time_t bill_interval)
{
	struct split *split;

	TAILQ_FOREACH(split, splits, entry) {
		struct who *who;
		time_t interval = split->max - split->min;
		int cost = PAYER_TIP + interval * value
			/ (split->who_list_l * bill_interval);
		if (pflags & PF_DEBUG) {
			graph_head(-1, 0);
			if (pflags & PF_HUMAN) {
				char *smaxs = printtime(split->max);
				if (pflags & PF_MACHINE)
					fprintf(stderr, "  " TS_FMT ":%s " TS_FMT " %d", split->max, smaxs, interval, cost);
				else
					fprintf(stderr, "  %s " TS_FMT " %d", smaxs, interval, cost);
				free(smaxs);
			} else
				fprintf(stderr, "  " TS_FMT " " TS_FMT " %d", split->max, interval, cost);
		}

		SLIST_FOREACH(who, &split->who_list, entry) {
			if (who->who != payer)
				ge_add(payer, who->who, cost);
			ndebug(" %s", gi_get(who->who));
		}
		ndebug("\n");
	}
}

/* Frees a tail queue of splits */
static void
splits_free(struct split_tailq *splits)
{
	struct split *split, *split_tmp;

	TAILQ_FOREACH_SAFE(split, splits, entry, split_tmp) {
		struct who *who, *who_tmp;

		SLIST_FOREACH_SAFE(who, &split->who_list, entry, who_tmp) {
			SLIST_REMOVE(&split->who_list, who, who, entry);
			free(who);
		}

		TAILQ_REMOVE(splits, split, entry);
	}
}

static inline void
line_finish(char *line)
{
	if (*line && *line != '\n')
		fprintf(stderr, " #%s", line);
	else
		fputc('\n', stderr);
}

/******
 * functions that process a valid type of line
 ******/

// https://softwareengineering.stackexchange.com/questions/363091/split-overlapping-ranges-into-all-unique-ranges/363096#363096

/* This function is for handling lines in the format:
 *
 * PAY <DATE> <PERSON_ID> <AMOUNT> <START_DATE> <END_DATE> [...]
 *
 * It represents the act of paying a bill. It reads the id of the payer, a
 * monetary value, the start date and the end date for the billing period.
 * Then it fetches the intervals of BST A which match this time period, and it
 * "trims" them so that they lie only within it. After this, it splits the
 * billing period into sections (or splits) where the number of people present
 * differ. For a more visual representation of what is going on:
 *       l
 * o------------o - - - - - - - - - - - - -o
 *       el
 * o---------------------------------------o
 *       q
 * o---------------------------------------o
 * 
 * o------------o--------------------------o
 * x            w                          y
 *
 * For the billing period [x, y], we get the intervals:
 *
 * ([x, w], l), ([x, y], el), ([x, y], q).
 *
 * From this we construct the following splits:
 *
 * ([x, w], { l, el, q }), ([w, y], { el, q })
 *
 * There is also the possibility that there are periods of time that no one is
 * present, and this is also handled here. If no one is present but they are
 * still renting a room there, we introduce splits with the intervals from
 * BST B in the available gaps.
 *
 * From the obtained "splits" we then proceed to calculate how much each person
 * has to pay. Suppose the amount of the bill is A, and we have the splits
 * described above. For the first split, the amount per person would be:
 * PAYER_TIP + (w - x) * A / (y - x) * 3, and for the second:
 * PAYER_TIP + (y - w) * A / (y - x) * 2.
 *
 * I will spare you the gory details for now, but you can check out the
 * functions above.
 */
static inline void
process_pay(time_t ts, char *line)
{
	struct split_tailq splits;
	unsigned id;
	int value;
	time_t min, max;

	line += read_id(&id, line);
	line += read_currency(&value, line);
	line += read_ts(&min, line);
	line += read_ts(&max, line);
	if (pflags & (PF_GRAPH | PF_DEBUG)) {
		graph_head(id, 5);
		if (pflags & PF_HUMAN) {
			char *tss = printtime(ts), *mins = printtime(min), *maxs = printtime(max);
			if (pflags & PF_MACHINE)
				fprintf(stderr, TS_FMT ":%s PAY %s %d " TS_FMT ":%s " TS_FMT ":%s", ts, tss, gi_get(id), value, min, mins, max, maxs);
			else
				fprintf(stderr, "PAY %s %s %d %s %s", tss, gi_get(id), value, mins, maxs);
			free(tss);
			free(mins);
			free(maxs);
		} else
			fprintf(stderr, TS_FMT " PAY %s %d " TS_FMT " " TS_FMT "", ts, gi_get(id), value, min, max);
		line_finish(line);
	}

	splits_get(&splits, &pdbs, min, max);
	splits_fill(&splits, min, max);
	/* splits_debug(&splits); */
	splits_pay(&splits, id, value, max - min);
	/* splits_free(&splits); */
}

/* This function is for handling lines in the format:
 *
 * BUY <DATE> <PERSON_ID> <AMOUNT> [DESCRIPTION]
 *
 * It reads an existing id much like process_resume, and it also reads a
 * currency value. Then it sees which time intervals in BST B intersect with
 * the provided DATE. Then it divides the value by the number of matches it
 * got, and it adds PAYER_TIP to the result, which was added to make sure the
 * payer of the bill or of the shared goods does not lose money with rounding
 * errors. After this, it iterates over all the matches, adding the result
 * money owed to the read id (the payer), from the person that interval
 * belongs to.
 */
static inline void
process_buy(time_t ts, char *line)
{
	struct match_stailq matches;
	struct match *match;
	unsigned id;
	int value, i, dvalue;

	line += read_id(&id, line);
	read_currency(&value, line);
	if (pflags & (PF_GRAPH | PF_DEBUG)) {
		graph_head(id, 5);
		if (pflags & PF_HUMAN) {
			char *tss = printtime(ts);
			if (pflags & PF_MACHINE)
				fprintf(stderr, TS_FMT ":%s BUY %s %d", ts, tss, gi_get(id), value);
			else
				fprintf(stderr, "BUY %s %s %d", tss, gi_get(id), value);
			free(tss);
		} else
			fprintf(stderr, TS_FMT " BUY %s %d", ts, gi_get(id), value);
		line_finish(line);
	}

	unsigned matches_l = ti_pintersect(&npdbs, &matches, ts);
	dvalue = value / matches_l + PAYER_TIP;

	debug("  %d", dvalue);

	// assert there are not multiple intervals with the same id?
	STAILQ_FOREACH(match, &matches, entry) {
		if (match->ti.who != id)
			ge_add(id, match->ti.who, dvalue);
		ndebug(" %s", gi_get(match->ti.who));
	}

	ndebug("\n");
}

/* This function is for handling lines in the format:
 *
 * TRANSFER <DATE> <FROM_PERSON_ID> <TO_PERSON_ID> <AMOUNT>
 *
 * It reads two existing PERSON_IDs and obtains their numeric ids, which are
 * assumed to exist. It also reads a currency value. Then it adds the value to
 * the edge between the first id and the second id, increasing the debt that
 * the second person owes to the first one by that value.
 */
static inline void
process_transfer(time_t ts, char *line)
{
	unsigned id_from, id_to;
	int value;

	line += read_id(&id_from, line);
	line += read_id(&id_to, line);
	line += read_currency(&value, line);

	if (pflags & (PF_GRAPH | PF_DEBUG)) {
		char *id_from_si = gi_get(id_from);
		char id_from_s[USERNAME_MAX_LEN];
		memcpy(id_from_s, id_from_si, strlen(id_from_si) + 1);
		graph_head(id_from, 5);
		if (pflags & PF_HUMAN) {
			char *tss = printtime(ts);
			if (pflags & PF_MACHINE)
				fprintf(stderr, TS_FMT ":%s TRANSFER %s %s %d", ts, tss, id_from_s, gi_get(id_to), value);
			else
				fprintf(stderr, "TRANSFER %s %s %s %d", tss, id_from_s, gi_get(id_to), value);
			free(tss);
		} else
			fprintf(stderr, TS_FMT " TRANSFER %s %s %d", ts, id_from_s, gi_get(id_to), value);
		line_finish(line);
	}

	ge_add(id_from, id_to, value);
}

/* This function is for handling lines in the format:
 *
 * STOP <DATE> <PERSON_ID>
 *
 * It reads a PERSON_ID, and it checks if there is a correspondant graph node
 * (and so a numeric id). If there is one, it finishes the last intervals in
 * both BSTs (much like process_pause does for BST A). If there isn't a graph
 * node for that user (and therefore no numeric id), it generates one. Then it
 * inserts the time interval [-∞, DATE] (and the newly created id) into both
 * BSTs.
 */
static inline void
process_stop(time_t ts, char *line)
{
	char username[USERNAME_MAX_LEN];
	unsigned id;

	read_word(username, line, sizeof(username));
	id = g_find(username);
	if (pflags & (PF_GRAPH | PF_DEBUG)) {
		graph_head(id, 1);
		if (pflags & PF_HUMAN) {
			char *tss = printtime(ts);
			if (pflags & PF_MACHINE)
				fprintf(stderr, TS_FMT ":%s STOP %s", ts, tss, gi_get(id));
			else
				fprintf(stderr, "STOP %s %s", tss, gi_get(id));
			free(tss);
		} else
			fprintf(stderr, TS_FMT " STOP %s", ts, gi_get(id));
		line_finish(line);
		if (pflags & PF_GRAPH) {
			graph_head(id, 3); fputc('\n', stderr);
		}
	}
	who_remove(gwhodb, id);

	if (id != g_notfound) {
		ti_finish_last(&pdbs, id, ts);
		ti_finish_last(&npdbs, id, ts);
	} else {
		id = g_insert(username);
		ti_insert(&pdbs, id, mtinf, ts);
		ti_insert(&npdbs, id, mtinf, ts);
	}
}

/* This function is for handling lines in the format:
 *
 * RESUME <DATE> <PERSON_ID>
 *
 * So it reads an existing PERSON_ID and obtains the correspondant numeric id,
 * then it inserts the interval [DATE, +∞] (and the id) into BST A. It differs
 * from process_start in the sense that it assumes that the numeric id is
 * already present, and also because it never inserts the interval into BST B.
 */
static inline void
process_resume(time_t ts, char *line)
{
	unsigned id;

	read_id(&id, line);
	who_insert(gwhodb, id);
	if (pflags & (PF_GRAPH | PF_DEBUG)) {
		if (pflags & PF_GRAPH) {
			graph_head(id, 4); fputc('\n', stderr);
		}
		graph_head(id, 2);
		if (pflags & PF_HUMAN) {
			char *tss = printtime(ts);
			if (pflags & PF_MACHINE)
				fprintf(stderr, TS_FMT ":%s RESUME %s", ts, tss, gi_get(id));
			else
				fprintf(stderr, "RESUME %s %s", tss, gi_get(id));
			free(tss);
		} else
			fprintf(stderr, TS_FMT " RESUME %s", ts, gi_get(id));
		line_finish(line);
	}
	// TODO assert no interval for id at this ts
	ti_insert(&pdbs, id, ts, tinf);
}

/* This function is for handling lines in the format:
 *
 * PAUSE <DATE> <PERSON_ID>
 *
 * As such, it reads an existing PERSON_ID, and fetches the numeric id to which
 * it corresponds. After that, it grabs the last time interval that is present
 * that belongs to that person, and finishes it at the read DATE
 * (only in BST A).
 *
 * For example, if the preceding line was:
 *
 * START <DATE_A> joseph
 *
 * And therefore, the most recent interval for joseph was [DATE_A, +∞], a line
 * like:
 *
 * PAUSE <DATE_B> joseph
 *
 * Would update this interval to [DATE_A, DATE_B], but only for BST A.
 */
static inline void
process_pause(time_t ts, char *line)
{
	unsigned id;

	read_id(&id, line);
	if (pflags & (PF_GRAPH | PF_DEBUG)) {
		graph_head(id, 1);
		if (pflags & PF_HUMAN) {
			char *tss = printtime(ts);
			if (pflags & PF_MACHINE)
				fprintf(stderr, TS_FMT ":%s PAUSE %s", ts, tss, gi_get(id));
			else
				fprintf(stderr, "PAUSE %s %s", tss, gi_get(id));
			free(tss);
		} else
			fprintf(stderr, TS_FMT " PAUSE %s", ts, gi_get(id));
		line_finish(line);
		if (pflags & PF_GRAPH) {
			graph_head(id, 3); fputc('\n', stderr);
		}
	}
	who_remove(gwhodb, id);
	// TODO assert interval for id at this ts
	ti_finish_last(&pdbs, id, ts);
}

/* This function is for handling lines in the format:
 *
 * START <DATE> <PERSON_ID> [<PHONE_NUMBER> <EMAIL> ... <NAME>]
 *
 * So it reads a textual person id, then it inserts it into the graph as a
 * node, generating a numeric id. Then it inserts the time interval [DATE, +∞]
 * along with that numeric id into both BST A and BST B.
 */
static inline void
process_start(time_t ts, char *line)
{
	char username[USERNAME_MAX_LEN];
	unsigned id;

	read_word(username, line, sizeof(username));
	id = g_insert(username);
	who_insert(gwhodb, id);
	if (pflags & (PF_GRAPH | PF_DEBUG)) {
		if (pflags & PF_GRAPH) {
			graph_head(id, 4); fputc('\n', stderr);
		}
		graph_head(id, 2);
		if (pflags & PF_HUMAN) {
			char *tss = printtime(ts);
			if (pflags & PF_MACHINE)
				fprintf(stderr, TS_FMT ":%s START %s", ts, tss, gi_get(id));
			else
				fprintf(stderr, "START %s %s", tss, gi_get(id));
			free(tss);
		} else
			fprintf(stderr, TS_FMT " START %s", ts, gi_get(id));
		line_finish(line);
	}
	ti_insert(&pdbs, id, ts, tinf);
	ti_insert(&npdbs, id, ts, tinf);
}

/******
 * etc
 ******/

/* This function is what processes each line. For each of them, it first checks
 * if it starts with a "#", in other words, if it is totally commented out.
 * If it is, it ignores this line. If it isn't, it then proceeds to read a
 * word: the TYPE of operation or event that the line represents. It also reads
 * the DATE. After this, it checks what the TYPE of operation is. Depending on
 * that, it does different things. In all valid cases (and for every different
 * TYPE), it calls a function named process_<TYPE> (lowercase), all of these
 * functions receive the DATE that was read, and also a pointer to the part of
 * the line that wasn't read yet.
 *
 * Check out the functions in the section above to understand how these work
 * internally.
 */
static void
process_line(char *line)
{
	char op_type_str[9], date_str[DATE_MAX_LEN];
	time_t ts;

	if (line[0] == '#')
		return;

	/* debug("%s", line); */
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

void
usage(char *prog)
{
	fprintf(stderr, "Usage: %s [-gdhm]", prog);
	fprintf(stderr, "    Options:\n");
	fprintf(stderr, "        -g        display graph.\n");
	fprintf(stderr, "        -d        display debug messages.\n");
	fprintf(stderr, "        -h        display dates in ISO 8601 format.\n");
	fprintf(stderr, "        -m        display dates in both unix timestamp and ISO 8601 formats.\n");
}

/* The main function is the entry point to the application. In this case, it
 * is very basic. What it does is it reads each line that was fed in standard
 * input. This allows you to feed it any file you want by running:
 *
 * $ cat file.txt | ./sem
 *
 * You can also just run "./sem", input manually, and then hit ctrl+D.
 *
 * For each line read, it then calls process_line, with that line as an
 * argument (a pointer). Look at process_line right above this comment to
 * understand how it works.
 *
 * After reading each line in standard input, the program shows the debt
 * that was calculated, that is owed between the people (ge_show_all).
 */
int
main(int argc, char *argv[])
{
	char *line = NULL;
	ssize_t linelen;
	size_t linesize;
	int ret;
	char c;

	while ((c = getopt(argc, argv, "gdhm")) != -1) {
		switch (c) {
		case 'd':
			pflags |= PF_DEBUG;
			break;

		case 'g':
			pflags |= PF_GRAPH;
			break;
			
		case 'h':
			pflags |= PF_HUMAN;
			break;

		case 'm':
			pflags |= PF_MACHINE | PF_HUMAN;
			break;
			
		default:
			usage(*argv);
			return 1;

		case '?':
			usage(*argv);
			return 0;
		}
	}

	dbs_init();

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		process_line(line);

	free(line);

	ge_show_all();

	return EXIT_SUCCESS;
}
