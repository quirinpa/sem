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

#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __OpenBSD__
#include <sys/queue.h>
#else
#include <bsd/sys/queue.h>
#endif

#include <qdb.h>
#include <it.h>

#include "common.h"

#define ndebug(fmt, ...) \
	if (pflags & PF_DEBUG) \
		fprintf(stderr, fmt, ##__VA_ARGS__)

#define PAYER_TIP 1
#define USERNAME_MAX_LEN 32
#define CURRENCY_MAX_LEN 32

enum pflags {
	PF_DEBUG = 1,
	PF_PRESENT = 2,
	PF_QUIET = 4,
};

typedef void (op_proc_t)(time_t ts, char *line);
op_proc_t op_start, op_stop, op_pause, op_resume, op_transfer, op_pay, op_buy;

struct op {
	char *name;
	op_proc_t *cb;
} op_map[] = {
	{ "START", op_start },
	{ "STOP", op_stop },
	{ "PAUSE", op_pause },
	{ "RESUME", op_resume },
	{ "TRANSFER", op_transfer },
	{ "PAY", op_pay },
	{ "BUY", op_buy },
};

unsigned op_hd, // ops
	 g_hd, // name to id
	 ig_hd, // id to name
	 ge_hd, // edge (id pair / debt)
	 gwho_hd, // id for graph
	 gnpwho_hd, // id for graph, no pause

	 p_itd, // pause / present
	 np_itd; // no pause

struct idm idm;

unsigned pflags = 0;

static inline void
who_graph_line(unsigned who_does, unsigned flags) {
	qdb_cur_t c = qdb_iter(gwho_hd, NULL);
	unsigned ref, ignore;

	if (flags)
		while (qdb_next(&ref, &ignore, &c)) {
			if (who_does == ref) {
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
					if (who_does > ref)
						fputc('/', stderr);
					else
						fputc('|', stderr);
					break;
				case 4:
					if (who_does > ref)
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
		while (qdb_next(&ref, &ignore, &c))
			fputc('|', stderr);

	fputc(' ', stderr);
}

/******
 * read functions
 ******/

/* read person nickname and convert it to existing numeric id */
static unsigned
read_id(char **line)
{
	unsigned id;
	char username[USERNAME_MAX_LEN];
	read_word(username, line, sizeof(username));
	CBUG(qdb_get(g_hd, &id, username));
	return id;
}

/* read currency value and convert it to int */
static inline long
read_currency(char **line)
{
	return (long) (strtof(*line, line) * 100.0f);
}

/* create person id to nickname secondary DB items */
int ig_assoc(void **result, void *key, void *value) {
	* (unsigned **) result = value;
	return 0;
}

/******
 * ge (graph edges) functions
 ******/

/* get debt between people */
static long
ge_get(unsigned id0, unsigned id1)
{
	unsigned ids[2];
	long ret;

	if (id0 > id1) {
		ids[0] = id1;
		ids[1] = id0;
	} else {
		ids[0] = id0;
		ids[1] = id1;
	}

	if (qdb_get(ge_hd, &ret, ids))
		return 0;

	return id0 > id1 ? -ret : ret;
}

/* add debt between people */
static void
ge_add(unsigned id_from, unsigned id_to, long value)
{
	unsigned ids[2];
	long cvalue;

	if (id_from > id_to) {
		ids[0] = id_to;
		ids[1] = id_from;
	} else {
		ids[0] = id_from;
		ids[1] = id_to;
	}

	cvalue = ge_get(id_from, id_to);

	if (id_from > id_to)
		value = - cvalue - value;
	else
		value = cvalue + value;

	qdb_put(ge_hd, ids, &value);
}

/* show debt between a pair of two people */
static inline void
ge_show(unsigned from, unsigned to, long value)
{
	char from_name[USERNAME_MAX_LEN];
	char to_name[USERNAME_MAX_LEN];
	qdb_pget(ig_hd, from_name, &from);
	qdb_pget(ig_hd, to_name, &to);

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
	qdb_cur_t c = qdb_iter(ge_hd, NULL);
	unsigned key[2];
	unsigned long value;

	while (qdb_next(key, &value, &c))
		ge_show(key[0], key[1], value);
}

/******
 * who (db of "current" people, for use in split calculation) related functions
 ******/

static inline void
who_present() {
	qdb_cur_t c = qdb_iter(gwho_hd, NULL);
	unsigned who, ignore;
	char name[USERNAME_MAX_LEN];
	qdb_get(ig_hd, name, &who);

	while (qdb_next(&who, &ignore, &c))
		printf("%c %s\n",
		       qdb_get(gwho_hd, &ignore, &who) ? 'P' : 'A',
		       name);
}

/* makes all provided matches lie within the provided interval [min, max] */
static inline long
pay(long long divident, long long divisor) {
	return (divident % divisor ? PAYER_TIP : 0)
		+ divident / divisor;
}

static inline void
line_finish(char *line)
{
	if (*line && *line != '\n') {
		if (*(line + 1) == '#')
			fprintf(stderr, "%s", line);
		else
			fprintf(stderr, " #%s", line);
	} else
		fputc('\n', stderr);
}

static inline void gdebug(time_t ts, unsigned id, char *label) {
	char user[USERNAME_MAX_LEN];
	qdb_pget(ig_hd, user, &id);
	char tss[DATE_MAX_LEN];
	printtime(tss, ts);
	fprintf(stderr, "%s %s %s", label, tss, user);
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
void op_pay(time_t ts, char *line)
{
	unsigned id;
	long value;
	time_t lmin = -1, min, max;
	long long bill_interval;

	id = read_id(&line);
	value = read_currency(&line);
	min = read_ts(&line);
	max = read_ts(&line);
	bill_interval = max - min;
	/* char label[DATE_MAX_LEN * 2 + 1]; */
	/* sprintf(label, "% */
	if (pflags & PF_DEBUG) {
		char mins[DATE_MAX_LEN], maxs[DATE_MAX_LEN];
		who_graph_line(id, 5);
		printtime(mins, min);
		printtime(maxs, max);
		gdebug(ts, id, "PAY");
		fprintf(stderr, " %ld %s %s", value, mins, maxs);
		line_finish(line);
	}

	it_cur_t c = it_iter(p_itd, min, max);
	unsigned who, count, cost, not_first = 0;
	long long interval;

	while (it_next(&min, &max, &count, &who, &c)) {
		if (lmin != min) {
			interval = max - min;
			cost = pay(interval * value, count * bill_interval);

			if (pflags & PF_DEBUG) {
				if (not_first)
					fprintf(stderr, "\n");
				not_first = 1;
				who_graph_line(-1, 0);
				char smaxs[DATE_MAX_LEN];
				printtime(smaxs, max);
				fprintf(stderr, "  %s %lld %d", smaxs, interval, cost);
			}
			lmin = min;
		}

		char name[USERNAME_MAX_LEN];
		qdb_pget(ig_hd, name, &who);
		if (who != id)
			ge_add(id, who, cost);
		ndebug(" %s", name);
	}
	ndebug("\n");
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
void op_buy(time_t ts, char *line) {
	it_cur_t c = it_iter(np_itd, ts, ts);
	unsigned id, who;
	long value, dvalue;
	time_t lmin = -1, min, tign;
	unsigned count;

	id = read_id(&line);
	value = read_currency(&line);
	if (pflags & PF_DEBUG) {
		who_graph_line(id, 5);
		gdebug(ts, id, "BUY");
		fprintf(stderr, " %ld", value);
		line_finish(line);
		who_graph_line(-1, 0);
	}

	// assert there are not multiple intervals with the same id?
	while (it_next(&tign, &tign, &count, &who, &c)) {
		char name[USERNAME_MAX_LEN];
		dvalue = pay(value, count);
		if (who != id)
			ge_add(id, who, pay(value, count));
		qdb_pget(ig_hd, name, &who);
		ndebug(" %ld %s", dvalue, name);
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
void op_transfer(time_t ts, char *line) {
	unsigned id_from, id_to;
	long value;

	id_from = read_id(&line);
	id_to = read_id(&line);
	value = read_currency(&line);

	if (pflags & PF_DEBUG) {
		char id_from_s[USERNAME_MAX_LEN];
		qdb_pget(ig_hd, id_from_s, &id_from);
		who_graph_line(id_from, 5);
		gdebug(ts, id_from, "BUY");
		fprintf(stderr, " %ld", value);
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
void op_stop(time_t ts, char *line) {
	char username[USERNAME_MAX_LEN];
	unsigned id;

	read_word(username, &line, sizeof(username));
	qdb_get(g_hd, &id, username);

	if (qdb_get(g_hd, &id, username)) {
		unsigned id = idm_new(&idm);
		qdb_put(g_hd, username, &id);
	}

	if (pflags & PF_DEBUG) {
		who_graph_line(id, 1);
		gdebug(ts, id, "STOP");
		line_finish(line);
		who_graph_line(id, 3);
		fputc('\n', stderr);
	}

	qdb_del(gwho_hd, &id, NULL);
	qdb_del(gnpwho_hd, &id, NULL);

	it_stop(p_itd, ts, id);
	it_stop(np_itd, ts, id);
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
void op_resume(time_t ts, char *line) {
	unsigned id, ignore;

	id = read_id(&line);
	CBUG(!qdb_get(gwho_hd, &ignore, &id));
	CBUG(qdb_get(gnpwho_hd, &ignore, &id));
	qdb_put(gwho_hd, &id, &id);
	if (pflags & PF_DEBUG) {
		who_graph_line(id, 4);
		fputc('\n', stderr);
		who_graph_line(id, 2);
		gdebug(ts, id, "RESUME");
		line_finish(line);
	}
	// TODO assert no interval for id at this ts
	it_start(p_itd, ts, id);
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
void op_pause(time_t ts, char *line) {
	unsigned id;

	id = read_id(&line);
	if (pflags & PF_DEBUG) {
		who_graph_line(id, 1);
		gdebug(ts, id, "PAUSE");
		line_finish(line);
		who_graph_line(id, 3);
		fputc('\n', stderr);
	}
	qdb_del(gwho_hd, &id, NULL);
	// TODO assert interval for id at this ts
	it_stop(p_itd, ts, id);
}

/* This function is for handling lines in the format:
 *
 * START <DATE> <PERSON_ID> [<PHONE_NUMBER> <EMAIL> ... <NAME>]
 *
 * So it reads a textual person id, then it inserts it into the graph as a
 * node, generating a numeric id. Then it inserts the time interval [DATE, +∞]
 * along with that numeric id into both BST A and BST B.
 */
void op_start(time_t ts, char *line) {
	char username[USERNAME_MAX_LEN];
	unsigned id;

	read_word(username, &line, sizeof(username));
	id = idm_new(&idm);
	qdb_put(g_hd, username, &id);
	qdb_put(gwho_hd, &id, &id);
	qdb_put(gnpwho_hd, &id, &id);
	if (pflags & PF_DEBUG) {
		who_graph_line(id, 4);
		fputc('\n', stderr);
		who_graph_line(id, 2);
		gdebug(ts, id, "START");
		line_finish(line);
	}
	it_start(p_itd, ts, id);
	it_start(np_itd, ts, id);
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
line_proc(char *line)
{
	char op_str[9], date_str[DATE_MAX_LEN];
	time_t ts;

	if (line[0] == '#' || line[0] == '\n')
		return;

	read_word(op_str, &line, sizeof(op_str));
	op_proc_t *cb;

	if (qdb_get(op_hd, &cb, op_str) < 0)
		return;

	ts = read_ts(&line);

	cb(ts, line);
}

static inline void
usage(char *prog)
{
	fprintf(stderr, "Usage: %s [-dpq]", prog);
	fprintf(stderr, "    Options:\n");
	fprintf(stderr, "        -d        display debug messages.\n");
	fprintf(stderr, "        -p        display who's present.\n");
	fprintf(stderr, "        -q        validate only.\n");
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

	while ((c = getopt(argc, argv, "dpq")) != -1) {
		switch (c) {
		case 'd':
			pflags |= PF_DEBUG;
			break;

		case 'p':
			pflags |= PF_PRESENT;
			break;

		case 'q':
			pflags |= PF_QUIET;
			break;
			
		default:
			usage(*argv);
			return 1;

		case '?':
			usage(*argv);
			return 0;
		}
	}

	qdb_init();
	qdb_reg("op", sizeof(op_proc_t *));

	op_hd = qdb_open(NULL, "s", "op", 0);

	g_hd = qdb_open(NULL, "s", "u", 0);
	ig_hd = qdb_open(NULL, "u", "s", QH_SEC);
	ge_hd = qdb_open(NULL, "ul", "ul", 0);
	gwho_hd = qdb_open(NULL, "u", "u", 0);
	gnpwho_hd = qdb_open(NULL, "u", "u", 0);
	qdb_assoc(ig_hd, g_hd, ig_assoc);

	p_itd = it_init(NULL);
	np_itd = it_init(NULL);

	for (int i = 0; i < 7; i++)
		qdb_put(op_hd, op_map[i].name, &op_map[i].cb);

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		line_proc(line);

	free(line);

	if (pflags & PF_QUIET)
		return EXIT_SUCCESS;

	if (pflags & PF_PRESENT)
		who_present();
	else
		ge_show_all();

	return EXIT_SUCCESS;
}
