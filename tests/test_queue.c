#include <stdio.h>
#include <string.h>

#include "queue.h"

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__,		\
		    __LINE__, #cond);					\
		failures++;						\
	}								\
} while (0)

static int	failures;
static Queue	q;

static int
add(const char *name)
{
	int	who;

	who = q_intern(&q, name);
	q_enqueue(&q, who, 0);
	return who;
}

static const char *
at(int pos)
{
	return q.names[q_active(&q)->queue[pos]];
}

static const char *
speaker(void)
{
	return q.speaker < 0 ? "" : q.names[q.speaker];
}

static void
test_first_speaker_takes_floor(void)
{
	q_init(&q);
	add("Ada");
	CHECK(strcmp(speaker(), "Ada") == 0);
	CHECK(q_active(&q)->len == 0);
}

static void
test_fewer_turns_go_first(void)
{
	q_init(&q);
	add("Ada");
	add("Bo");
	q_next(&q, 0);		/* Ada has 1 turn, Bo speaking */
	q_next(&q, 0);		/* Bo has 1 turn, floor open */
	add("Ada");		/* takes the floor immediately */
	add("Bo");
	add("Cy");		/* no turns yet: ahead of Bo */
	CHECK(strcmp(at(0), "Cy") == 0);
	CHECK(strcmp(at(1), "Bo") == 0);
}

static void
test_speaker_can_requeue(void)
{
	q_init(&q);
	add("Ada");
	add("Bo");
	add("Ada");		/* turn in progress counts: behind Bo */
	CHECK(strcmp(at(0), "Bo") == 0);
	CHECK(strcmp(at(1), "Ada") == 0);
	CHECK(q_enqueue(&q, q_find(&q, "bo"), 0) == ENQ_ALREADY_QUEUED);
}

static void
test_names_match_case_insensitively(void)
{
	char	typed[] = "  ada   lovelace ";
	int	a;

	q_init(&q);
	name_normalize(typed);
	CHECK(strcmp(typed, "ada lovelace") == 0);
	a = q_intern(&q, "Ada Lovelace");
	CHECK(q_intern(&q, typed) == a);
	CHECK(q.name_count == 1);
}

static void
test_sub_motion_counts_separately(void)
{
	q_init(&q);
	add("Ada");
	add("Bo");
	q_open_level(&q);
	add("Cy");
	q_next(&q, 0);		/* Ada credited to main; Cy speaks */
	CHECK(strcmp(speaker(), "Cy") == 0);
	CHECK(q.levels[0].turns[q_find(&q, "Ada")] == 1);
	q_next(&q, 0);
	CHECK(q.levels[1].turns[q_find(&q, "Cy")] == 1);
	CHECK(q.levels[0].turns[q_find(&q, "Cy")] == 0);
	CHECK(q_close_level(&q) == 0);
	CHECK(q_close_level(&q) == -1);
	q_next(&q, 0);
	CHECK(strcmp(speaker(), "Bo") == 0);
}

static void
test_closed_level_turn_not_credited_to_reopened(void)
{
	q_init(&q);
	q_open_level(&q);
	add("Ada");		/* speaking on sub-motion 1 */
	q_close_level(&q);
	q_open_level(&q);	/* a different sub-motion, same depth */
	q_next(&q, 0);
	CHECK(q.levels[1].turns[q_find(&q, "Ada")] == 0);
	CHECK(q.speaker == -1);
}

static void
test_call_remove_move(void)
{
	q_init(&q);
	add("Ada");
	add("Bo");
	add("Cy");
	add("Di");
	q_call(&q, 2, 0);	/* Di out of order */
	CHECK(strcmp(speaker(), "Di") == 0);
	CHECK(q_move(&q, 0, 1));
	CHECK(strcmp(at(0), "Cy") == 0);
	CHECK(!q_move(&q, 1, 1));
	q_remove(&q, 0);
	CHECK(q_active(&q)->len == 1 && strcmp(at(0), "Bo") == 0);
}

static void
test_reset_keeps_names_unless_asked(void)
{
	q_init(&q);
	add("Ada");
	add("Bo");
	q_reset(&q, false);
	CHECK(q.name_count == 2 && q.speaker == -1);
	CHECK(q_active(&q)->len == 0);
	q_reset(&q, true);
	CHECK(q.name_count == 0);
}

static void
test_save_and_load_round_trip(void)
{
	static Queue	 loaded;
	const char	*path = "build/test_state.txt";

	q_init(&q);
	add("Ada Lovelace");
	add("Bo");
	add("Zoë");
	q_next(&q, 0);
	q_open_level(&q);
	add("Cy");
	q.hidden[q_find(&q, "Bo")] = true;
	q.speaker_since = 1234.5;

	CHECK(q_save(&q, path));
	CHECK(q_load(&loaded, path));
	CHECK(loaded.name_count == q.name_count);
	CHECK(strcmp(loaded.names[2], "Zoë") == 0);
	CHECK(loaded.hidden[1]);
	CHECK(loaded.depth == 2);
	CHECK(loaded.speaker == q.speaker);
	CHECK(loaded.speaker_since == q.speaker_since);
	CHECK(memcmp(loaded.levels, q.levels, sizeof(q.levels)) == 0);
	remove(path);
}

int
main(void)
{
	test_first_speaker_takes_floor();
	test_fewer_turns_go_first();
	test_speaker_can_requeue();
	test_names_match_case_insensitively();
	test_sub_motion_counts_separately();
	test_closed_level_turn_not_credited_to_reopened();
	test_call_remove_move();
	test_reset_keeps_names_unless_asked();
	test_save_and_load_round_trip();
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("all tests passed\n");
	return 0;
}
