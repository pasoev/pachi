#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "joseki/joseki.h"
#include "joseki/base.h"


/* Internal engine state. */
struct joseki_engine {
	int debug_level;
	int size;

	struct board *b[16]; // boards with reversed color, mirrored and rotated
};

/* We will record the joseki positions into incrementally-built
 * joseki_pats[]. */


static char *
joseki_play(struct engine *e, struct board *b, struct move *m)
{
	struct joseki_engine *j = e->data;

	if (!b->moves) {
		/* Reset boards. */
		j->size = board_size(b);
		for (int i = 0; i < 16; i++) {
			board_resize(j->b[i], j->size - 2);
			board_clear(j->b[i]);
		}
	}

	assert(!is_resign(m->coord));
	if (is_pass(m->coord))
		return NULL;
	//printf("%s %d\n", coord2sstr(m->coord, b), coord_quadrant(m->coord, b));
	/* Ignore moves in different quadrants. */
	if (coord_quadrant(m->coord, b) > 0)
		return NULL;

	//printf("%"PRIhash" %"PRIhash"\n", j->b[0]->qhash[0], b->qhash[0]);
	assert(j->b[0]->qhash[0] == b->qhash[0]);

	/* Record next move in all rotations and update the hash. */
	for (int i = 0; i < 16; i++) {
#define HASH_VMIRROR     1
#define HASH_HMIRROR     2
#define HASH_XYFLIP      4
#define HASH_OCOLOR      8
		int quadrant = 0;
		coord_t coord = m->coord;
		if (i & HASH_VMIRROR) {
			coord = coord_xy(b, coord_x(coord, b), board_size(b) - 1 - coord_y(coord, b));
			quadrant += 2;
		}
		if (i & HASH_HMIRROR) {
			coord = coord_xy(b, board_size(b) - 1 - coord_x(coord, b), coord_y(coord, b));
			quadrant++;
		}
		if (i & HASH_XYFLIP) {
			coord = coord_xy(b, coord_y(coord, b), coord_x(coord, b));
			if (quadrant == 1)
				quadrant = 2;
			else if (quadrant == 2)
				quadrant = 1;
		}
		enum stone color = m->color;
		if (i & HASH_OCOLOR)
			color = stone_other(color);

		coord_t **ccp = &joseki_pats[j->b[i]->qhash[quadrant] & joseki_hash_mask].moves[color - 1];

		int count = 1;
		if (*ccp) {
			for (coord_t *cc = *ccp; !is_pass(*cc); cc++) {
				count++;
				if (*cc == coord) {
					//printf("(%"PRIhash", %d) !+ %s\n", j->b[i]->qhash[0], count, coord2sstr(coord, b));
					goto already_have;
				}
			}
		}

		*ccp = realloc(*ccp, (count + 1) * sizeof(coord_t));
		//printf("(%"PRIhash", %d) + %s\n", j->b[i]->qhash[quadrant], count, coord2sstr(coord, b));
		(*ccp)[count - 1] = coord;
		(*ccp)[count] = pass;

already_have: {
		struct move m2 = { .coord = coord, .color = color };
		board_play(j->b[i], &m2);
	      }
	}

	return NULL;
}

static coord_t *
joseki_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	fprintf(stderr, "genmove command not available in joseki scan!\n");
	exit(EXIT_FAILURE);
}

void
joseki_done(struct engine *e)
{
	struct joseki_engine *j = e->data;
	struct board *b = board_init();
	board_resize(b, j->size - 2);
	board_clear(b);

	for (hash_t i = 0; i < 1 << joseki_hash_bits; i++) {
		for (int j = 0; j < 2; j++) {
			static const char cs[] = "bw";
			if (!joseki_pats[i].moves[j])
				continue;
			printf("%" PRIhash " %c", i, cs[j]);
			coord_t *cc = joseki_pats[i].moves[j];
			int count = 0;
			while (!is_pass(*cc)) {
				printf(" %s", coord2sstr(*cc, b));
				cc++, count++;
			}
			printf(" %d\n", count);
		}
	}

	board_done(b);
}


struct joseki_engine *
joseki_state_init(char *arg)
{
	struct joseki_engine *j = calloc2(1, sizeof(struct joseki_engine));

	for (int i = 0; i < 16; i++)
		j->b[i] = board_init();

	j->debug_level = 1;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					j->debug_level = atoi(optval);
				else
					j->debug_level++;

			} else {
				fprintf(stderr, "joseki: Invalid engine argument %s or missing value\n", optname);
				exit(EXIT_FAILURE);
			}
		}
	}

	return j;
}

struct engine *
engine_joseki_init(char *arg, struct board *b)
{
	struct joseki_engine *j = joseki_state_init(arg);
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "Joseki Engine";
	e->comment = "You cannot play Pachi with this engine, it is intended for special development use - scanning of joseki sequences fed to it within the GTP stream.";
	e->genmove = joseki_genmove;
	e->notify_play = joseki_play;
	e->done = joseki_done;
	e->data = j;
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;

	return e;
}
