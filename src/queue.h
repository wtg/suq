/*
 * Speaker queue model: a roster of names, a stack of queue levels (the main
 * queue plus nested sub-motions), and whoever currently holds the floor.
 *
 * Everything lives in one flat struct so that undo is a plain copy and the
 * model has no allocations to manage.
 */
#ifndef SUQ_QUEUE_H
#define SUQ_QUEUE_H

#define MAX_NAMES 512
#define MAX_LEVELS 8
#define MAX_QUEUE 128
#define NAME_LEN 64

typedef struct {
        int id; /* unique per opened level */
        int len;
        int queue[MAX_QUEUE]; /* name indices, front first */
        int turns[MAX_NAMES]; /* turns taken on this level */
} Level;

typedef struct {
        int name_count;
        char names[MAX_NAMES][NAME_LEN];
        int last_used[MAX_NAMES]; /* recency stamp for suggestions */
        bool hidden[MAX_NAMES]; /* forgotten: never suggested */
        int tick;

        int depth; /* active level is levels[depth - 1] */
        int next_level_id;
        Level levels[MAX_LEVELS];

        int speaker; /* name index, -1 if floor is open */
        int speaker_level_id; /* level the current turn counts for */
        double speaker_since; /* wall-clock seconds */
} Queue;

typedef enum { ENQ_OK, ENQ_ALREADY_QUEUED, ENQ_FULL } Enqueue_Result;

void q_init(Queue *);

Level *q_active(Queue *);
const Level *q_active_c(const Queue *);

int q_find(const Queue *, const char *);
int q_intern(Queue *, const char *);
int q_position(const Level *, int);
int q_turns(const Queue *, const Level *, int);

Enqueue_Result q_enqueue(Queue *, int, double);
void q_next(Queue *, double);
void q_call(Queue *, int, double);
void q_remove(Queue *, int);
bool q_move(Queue *, int, int);
bool q_open_level(Queue *);
int q_close_level(Queue *);
void q_reset(Queue *, bool);

bool q_save(const Queue *, const char *);
bool q_load(Queue *, const char *);

int name_normalize(char *);
bool str_ieq(const char *, const char *);
bool str_istarts(const char *, const char *);
const char *str_ifind(const char *, const char *);

#endif /* SUQ_QUEUE_H */
