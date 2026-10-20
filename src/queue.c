#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

#define SAVE_MAGIC "suq 1"

static void level_init(Queue *, Level *);
static Level *level_by_id(Queue *, int);
static void end_turn(Queue *);
static void give_floor(Queue *, int, double);
static bool valid_name(const Queue *, int);
static bool load_from(Queue *, FILE *);

static void
level_init(Queue *q, Level *l)
{
        memset(l, 0, sizeof(*l));
        l->id = q->next_level_id++;
}

void
q_init(Queue *q)
{
        memset(q, 0, sizeof(*q));
        q->speaker = -1;
        q->depth = 1;
        level_init(q, &q->levels[0]);
}

Level *
q_active(Queue *q)
{
        return &q->levels[q->depth - 1];
}

const Level *
q_active_c(const Queue *q)
{
        return &q->levels[q->depth - 1];
}

/*
 * Strings.
 */

bool
str_ieq(const char *a, const char *b)
{
        for (; *a != '\0' && *b != '\0'; a++, b++) {
                if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
                        return false;
                }
        }
        return *a == *b;
}

bool
str_istarts(const char *s, const char *prefix)
{
        for (; *prefix != '\0'; s++, prefix++) {
                if (*s == '\0'
                    || tolower((unsigned char)*s)
                           != tolower((unsigned char)*prefix)) {
                        return false;
                }
        }
        return true;
}

const char *
str_ifind(const char *s, const char *needle)
{
        for (; *s != '\0'; s++) {
                if (str_istarts(s, needle)) {
                        return s;
                }
        }
        return NULL;
}

/*
 * Trims and collapses whitespace in place; returns the resulting length.
 */
int
name_normalize(char *s)
{
        char *r, *w;
        bool space;

        w = s;
        space = false;
        for (r = s; *r != '\0'; r++) {
                if (isspace((unsigned char)*r)) {
                        space = w != s;
                        continue;
                }
                if (space) {
                        *w++ = ' ';
                }
                space = false;
                *w++ = *r;
        }
        *w = '\0';
        return (int)(w - s);
}

/*
 * Roster.
 */

/* Case-insensitive lookup; -1 if unknown. */
int
q_find(const Queue *q, const char *name)
{
        int i;

        for (i = 0; i < q->name_count; i++) {
                if (str_ieq(q->names[i], name)) {
                        return i;
                }
        }
        return -1;
}

/* Finds or adds a name; -1 if the roster is full. */
int
q_intern(Queue *q, const char *name)
{
        int i;

        if ((i = q_find(q, name)) < 0) {
                if (q->name_count == MAX_NAMES) {
                        return -1;
                }
                i = q->name_count++;
                snprintf(q->names[i], NAME_LEN, "%s", name);
        }
        q->hidden[i] = false;
        return i;
}

/*
 * Queue operations.
 */

/* Index of who in the level's queue, or -1. */
int
q_position(const Level *l, int who)
{
        int i;

        for (i = 0; i < l->len; i++) {
                if (l->queue[i] == who) {
                        return i;
                }
        }
        return -1;
}

/* Turns taken on a level, including one in progress. */
int
q_turns(const Queue *q, const Level *l, int who)
{
        int n;

        n = l->turns[who];
        if (who == q->speaker && q->speaker_level_id == l->id) {
                n++;
        }
        return n;
}

static Level *
level_by_id(Queue *q, int id)
{
        int i;

        for (i = 0; i < q->depth; i++) {
                if (q->levels[i].id == id) {
                        return &q->levels[i];
                }
        }
        return NULL;
}

static void
end_turn(Queue *q)
{
        Level *l;

        if (q->speaker < 0) {
                return;
        }
        /* A turn on a sub-motion that has since closed counts for nothing. */
        if ((l = level_by_id(q, q->speaker_level_id)) != NULL) {
                l->turns[q->speaker]++;
        }
        q->speaker = -1;
}

static void
give_floor(Queue *q, int who, double now)
{
        q->speaker = who;
        q->speaker_level_id = q_active(q)->id;
        q->speaker_since = now;
}

Enqueue_Result
q_enqueue(Queue *q, int who, double now)
{
        Level *l;
        int i, turns;

        l = q_active(q);
        if (q_position(l, who) >= 0) {
                return ENQ_ALREADY_QUEUED;
        }
        if (l->len == MAX_QUEUE) {
                return ENQ_FULL;
        }

        q->last_used[who] = ++q->tick;

        /* Fewer turns goes first; ties keep arrival order. */
        turns = q_turns(q, l, who);
        for (i = l->len; i > 0 && q_turns(q, l, l->queue[i - 1]) > turns; i--)
                ;
        memmove(&l->queue[i + 1],
                &l->queue[i],
                (size_t)(l->len - i) * sizeof(l->queue[0]));
        l->queue[i] = who;
        l->len++;

        if (q->speaker < 0) {
                q_next(q, now);
        }
        return ENQ_OK;
}

/* Ends the current turn and gives the floor to someone out of order. */
void
q_call(Queue *q, int pos, double now)
{
        Level *l;
        int who;

        l = q_active(q);
        if (pos < 0 || pos >= l->len) {
                return;
        }
        who = l->queue[pos];
        q_remove(q, pos);
        end_turn(q);
        give_floor(q, who, now);
}

/* Ends the current turn and calls the front of the queue. */
void
q_next(Queue *q, double now)
{
        if (q_active(q)->len > 0) {
                q_call(q, 0, now);
        } else {
                end_turn(q);
        }
}

void
q_remove(Queue *q, int pos)
{
        Level *l;

        l = q_active(q);
        if (pos < 0 || pos >= l->len) {
                return;
        }
        memmove(&l->queue[pos],
                &l->queue[pos + 1],
                (size_t)(l->len - pos - 1) * sizeof(l->queue[0]));
        /* Keep unused slots zeroed so equal states compare equal. */
        l->queue[--l->len] = 0;
}

bool
q_move(Queue *q, int pos, int delta)
{
        Level *l;
        int to, who;

        l = q_active(q);
        to = pos + delta;
        if (pos < 0 || pos >= l->len || to < 0 || to >= l->len) {
                return false;
        }
        who = l->queue[pos];
        l->queue[pos] = l->queue[to];
        l->queue[to] = who;
        return true;
}

bool
q_open_level(Queue *q)
{
        if (q->depth == MAX_LEVELS) {
                return false;
        }
        level_init(q, &q->levels[q->depth++]);
        return true;
}

/* Returns the number of people dropped, or -1 at the main level. */
int
q_close_level(Queue *q)
{
        Level *l;
        int dropped;

        if (q->depth == 1) {
                return -1;
        }
        l = &q->levels[--q->depth];
        dropped = l->len;
        memset(l, 0, sizeof(*l));
        return dropped;
}

void
q_reset(Queue *q, bool forget_names)
{
        Queue fresh;

        q_init(&fresh);
        if (!forget_names) {
                fresh.name_count = q->name_count;
                fresh.tick = q->tick;
                memcpy(fresh.names, q->names, sizeof(q->names));
                memcpy(fresh.last_used, q->last_used, sizeof(q->last_used));
                memcpy(fresh.hidden, q->hidden, sizeof(q->hidden));
        }
        fresh.next_level_id = q->next_level_id + 1;
        fresh.levels[0].id = q->next_level_id;
        *q = fresh;
}

/*
 * Persistence: a small line-oriented text file.  Names sit on their own
 * lines so they may contain anything except a newline.
 */

bool
q_save(const Queue *q, const char *path)
{
        const Level *l;
        FILE *f;
        char tmp[1024];
        int i, j, used;
        bool ok;

        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        if ((f = fopen(tmp, "w")) == NULL) {
                return false;
        }

        fprintf(f, SAVE_MAGIC "\n");
        fprintf(f, "names %d %d\n", q->name_count, q->tick);
        for (i = 0; i < q->name_count; i++)
                fprintf(f,
                        "%d %d %s\n",
                        q->last_used[i],
                        q->hidden[i],
                        q->names[i]);
        fprintf(f,
                "speaker %d %d %.3f\n",
                q->speaker,
                q->speaker_level_id,
                q->speaker_since);
        fprintf(f, "levels %d %d\n", q->depth, q->next_level_id);
        for (i = 0; i < q->depth; i++) {
                l = &q->levels[i];
                fprintf(f, "level %d %d", l->id, l->len);
                for (j = 0; j < l->len; j++)
                        fprintf(f, " %d", l->queue[j]);
                used = 0;
                for (j = 0; j < q->name_count; j++)
                        used += l->turns[j] > 0;
                fprintf(f, "\nturns %d", used);
                for (j = 0; j < q->name_count; j++) {
                        if (l->turns[j] > 0) {
                                fprintf(f, " %d:%d", j, l->turns[j]);
                        }
                }
                fprintf(f, "\n");
        }

        ok = !ferror(f);
        ok = fclose(f) == 0 && ok;
        return ok && rename(tmp, path) == 0;
}

static bool
valid_name(const Queue *q, int n)
{
        return n >= 0 && n < q->name_count;
}

static bool
load_from(Queue *q, FILE *f)
{
        Level *l;
        char line[256];
        int i, j, hidden, offset, used, n, t;

        if (fgets(line, sizeof(line), f) == NULL
            || strncmp(line, SAVE_MAGIC, strlen(SAVE_MAGIC)) != 0) {
                return false;
        }

        if (fscanf(f, "names %d %d\n", &q->name_count, &q->tick) != 2) {
                return false;
        }
        if (q->name_count < 0 || q->name_count > MAX_NAMES) {
                return false;
        }
        for (i = 0; i < q->name_count; i++) {
                offset = 0;
                if (fgets(line, sizeof(line), f) == NULL) {
                        return false;
                }
                if (sscanf(line, "%d %d %n", &q->last_used[i], &hidden, &offset)
                        != 2
                    || offset == 0) {
                        return false;
                }
                q->hidden[i] = hidden != 0;
                line[strcspn(line, "\r\n")] = '\0';
                snprintf(q->names[i], NAME_LEN, "%s", line + offset);
        }

        if (fscanf(f,
                   "speaker %d %d %lf\n",
                   &q->speaker,
                   &q->speaker_level_id,
                   &q->speaker_since)
            != 3) {
                return false;
        }
        if (q->speaker != -1 && !valid_name(q, q->speaker)) {
                return false;
        }

        if (fscanf(f, "levels %d %d", &q->depth, &q->next_level_id) != 2) {
                return false;
        }
        if (q->depth < 1 || q->depth > MAX_LEVELS) {
                return false;
        }
        for (i = 0; i < q->depth; i++) {
                l = &q->levels[i];
                if (fscanf(f, " level %d %d", &l->id, &l->len) != 2) {
                        return false;
                }
                if (l->len < 0 || l->len > MAX_QUEUE) {
                        return false;
                }
                for (j = 0; j < l->len; j++) {
                        if (fscanf(f, " %d", &l->queue[j]) != 1
                            || !valid_name(q, l->queue[j])) {
                                return false;
                        }
                }
                if (fscanf(f, " turns %d", &used) != 1) {
                        return false;
                }
                for (j = 0; j < used; j++) {
                        if (fscanf(f, " %d:%d", &n, &t) != 2
                            || !valid_name(q, n)) {
                                return false;
                        }
                        l->turns[n] = t;
                }
        }
        return true;
}

bool
q_load(Queue *q, const char *path)
{
        static Queue loaded;
        FILE *f;
        bool ok;

        if ((f = fopen(path, "r")) == NULL) {
                return false;
        }
        memset(&loaded, 0, sizeof(loaded));
        ok = load_from(&loaded, f);
        fclose(f);
        if (ok) {
                *q = loaded;
        }
        return ok;
}
