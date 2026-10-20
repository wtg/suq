/*
 * suq: a keyboard-driven speaker queue.
 *
 * Typing always goes to the name prompt at the bottom; every other action is
 * a single key or a modifier chord, so the chair never has to reach for the
 * mouse while running a meeting.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "raylib.h"

#include "queue.h"

#define C_BG		(Color){ 16, 17, 20, 255 }
#define C_PANEL		(Color){ 25, 27, 32, 255 }
#define C_RAISED	(Color){ 37, 40, 48, 255 }
#define C_LINE		(Color){ 42, 45, 53, 255 }
#define C_FAINT		(Color){ 92, 97, 108, 255 }
#define C_DIM		(Color){ 140, 145, 155, 255 }
#define C_TEXT		(Color){ 212, 215, 221, 255 }
#define C_BRIGHT	(Color){ 250, 250, 251, 255 }
#define C_ACCENT	(Color){ 242, 184, 75, 255 }

#define MAX_SUGGESTIONS	6
#define HISTORY		64
#define FONT_CACHE	48
#define STATUS_SECONDS	3.0
#define FLASH_SECONDS	0.9

/* Key names in the platform's idiom: "⇧⌘Z" on a Mac, else "Ctrl+Shift+Z". */
#define K_MOD		keyname("\u2318", "Ctrl+")
#define K_SHIFT		keyname("\u21E7", "Shift+")
#define K_ENTER		keyname("\u23CE", "Enter")
#define K_BKSP		keyname("\u232B", "Bksp")
#define K_TAB		keyname("\u21E5", "Tab")
#define K_UPDOWN	keyname("\u2191\u2193", "Up/Down")
#define K_RIGHT		keyname("\u2192", "Right")

#define nitems(a)	(sizeof(a) / sizeof((a)[0]))

typedef enum {
	FACE_SANS,
	FACE_MONO,
	FACE_COUNT
} Face;

typedef enum {
	OVERLAY_NONE,
	OVERLAY_HELP,
	OVERLAY_RESET
} Overlay;

typedef struct {
	Queue	items[HISTORY];
	int	len;
	int	top;
} History;

typedef struct {
	Face	face;
	int	px;
	Font	font;
} Cached_Font;

typedef struct {
	const char	*key;
	const char	*label;
} Hint;

static const char *FACE_CANDIDATES[FACE_COUNT][6] = {
	[FACE_SANS] = {
		"/System/Library/Fonts/SFNS.ttf",
		"/System/Library/Fonts/Supplemental/Arial.ttf",
		"/usr/local/share/fonts/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"C:/Windows/Fonts/segoeui.ttf",
	},
	[FACE_MONO] = {
		"/System/Library/Fonts/SFNSMono.ttf",
		"/System/Library/Fonts/Supplemental/Courier New.ttf",
		"/usr/local/share/fonts/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
		"C:/Windows/Fonts/consola.ttf",
	},
};

/* Glyphs used by the interface beyond Latin text. */
static const int UI_SYMBOLS[] = {
	0x00B7, 0x2014, 0x2026, 0x203A, 0x2191, 0x2192, 0x2193, 0x21E5,
	0x21E7, 0x2318, 0x232B, 0x23CE,
};

#if defined(__APPLE__)
static const bool USE_SYMBOLS = true;
#else
static const bool USE_SYMBOLS = false;
#endif

static Queue		 state;
static Queue		 edit_before;
static History		 undo_history, redo_history;
static char		 state_path[1024], ui_path[1024];

static char		 input[NAME_LEN];
static int		 input_len;
static double		 input_changed_at;

static int		 suggestions[MAX_SUGGESTIONS];
static int		 suggestion_count;
static int		 suggestion_sel = -1;	/* -1: add text as typed */
static const char	*match_query;

static int		 selected = -1;		/* position in active queue */
static Overlay		 overlay;

static char		 status[192];
static double		 status_at = -100;
static int		 flash_who = -1;
static double		 flash_at = -100;

static float		 zoom = 1.0f;
static float		 dpi = 1.0f;

static Cached_Font	 font_cache[FONT_CACHE];
static int		 font_cache_len;
static const char	*face_path[FACE_COUNT];
static int		 codepoints[512];
static int		 codepoint_count;

static double		 wall_now(void);
static void		 say(const char *, ...);
static void		 flash(int);
static bool		 pressed(int);
static const char	*level_name(int);
static const char	*ordinal(int);
static const char	*plural(int, const char *, const char *);
static void		 format_elapsed(double, char *, size_t);
static void		 resolve_paths(void);
static void		 persist(void);
static void		 save_ui(void);
static void		 load_ui(void);
static void		 history_push(History *, const Queue *);
static bool		 history_pop(History *, Queue *);
static void		 edit_begin(void);
static bool		 edit_commit(void);
static void		 time_travel(History *, History *, const char *,
			    const char *);
static void		 init_fonts(void);
static Font		 font_for(Face, float);
static bool		 font_has(Font, int);
static const char	*keyname(const char *, const char *);
static const char	*cat(const char *, const char *);
static const char	*mod(const char *);
static const char	*shift_mod(const char *);
static const char	*either(const char *, const char *);
static float		 snap(float);
static float		 text_width(Face, float, float, const char *);
static float		 draw_text(Face, float, float, const char *, float,
			    float, Color);
static float		 draw_text_right(Face, float, const char *, float,
			    float, Color);
static const char	*fit(Face, float, const char *, float);
static void		 rounded(float, float, float, float, float, Color);
static void		 draw_hints(const Hint *, int, float, float, float,
			    float);
static int		 match_score(const char *, const char *);
static int		 compare_candidates(const void *, const void *);
static void		 refresh_suggestions(void);
static void		 input_changed(void);
static void		 input_clear(void);
static void		 input_set(const char *);
static bool		 input_append(int);
static void		 input_backspace(bool);
static void		 input_paste(void);
static void		 add_from_prompt(bool);
static void		 next_speaker(void);
static void		 call_selected(void);
static void		 remove_selected(void);
static void		 move_selected(int);
static void		 open_level(void);
static void		 close_level(void);
static void		 forget_suggestion(void);
static void		 reset(bool);
static void		 set_zoom(float);
static bool		 handle_overlay_keys(void);
static bool		 handle_chords(bool);
static bool		 handle_input(void);
static Color		 fade_to(Color, Color, float);
static float		 flash_amount(int);
static float		 draw_speaker(float, float, float, float);
static void		 draw_breadcrumb(float, float, float, float);
static void		 draw_queue(float, float, float, float, float);
static void		 draw_match(const char *, float, float, float, float,
			    bool);
static void		 draw_suggestions(float, float, float, float);
static void		 draw_prompt(float, float, float, float, float);
static void		 draw_footer(float, float, float, float);
static float		 draw_panel(float, float, float);
static void		 draw_help(float);
static void		 draw_reset(float);
static void		 draw(void);

/*
 * Small utilities.
 */

static double
wall_now(void)
{
	return (double)time(NULL);
}

static void
say(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(status, sizeof(status), fmt, ap);
	va_end(ap);
	status_at = GetTime();
}

static void
flash(int who)
{
	flash_who = who;
	flash_at = GetTime();
}

static bool
pressed(int key)
{
	return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

static const char *
level_name(int i)
{
	static char	buf[4][32];
	static int	n;
	char		*b;

	b = buf[n++ % 4];
	if (i == 0) {
		snprintf(b, 32, "Main");
	} else {
		snprintf(b, 32, "Sub-motion %d", i);
	}
	return b;
}

static const char *
ordinal(int n)
{
	static char	 buf[16];
	const char	*suffix;

	suffix = "th";
	if (n % 100 < 11 || n % 100 > 13) {
		if (n % 10 == 1) {
			suffix = "st";
		}
		if (n % 10 == 2) {
			suffix = "nd";
		}
		if (n % 10 == 3) {
			suffix = "rd";
		}
	}
	snprintf(buf, sizeof(buf), "%d%s", n, suffix);
	return buf;
}

static const char *
plural(int n, const char *one, const char *many)
{
	static char	buf[4][48];
	static int	k;
	char		*b;

	b = buf[k++ % 4];
	snprintf(b, 48, "%d %s", n, n == 1 ? one : many);
	return b;
}

static void
format_elapsed(double secs, char *out, size_t size)
{
	long	t;

	t = secs < 0 ? 0 : (long)secs;
	if (t >= 3600) {
		snprintf(out, size, "%ld:%02ld:%02ld", t / 3600, t / 60 % 60,
		    t % 60);
	} else {
		snprintf(out, size, "%ld:%02ld", t / 60, t % 60);
	}
}

/*
 * Persistence.
 */

static void
resolve_paths(void)
{
	const char	*override, *home, *base;
	char		 dir[900];

	override = getenv("SUQ_STATE");
	if (override != NULL && *override != '\0') {
		snprintf(state_path, sizeof(state_path), "%s", override);
		snprintf(ui_path, sizeof(ui_path), "%s.ui", override);
		return;
	}
	if ((home = getenv("HOME")) == NULL) {
		home = ".";
	}
#if defined(_WIN32)
	if ((base = getenv("APPDATA")) == NULL) {
		base = ".";
	}
	snprintf(dir, sizeof(dir), "%s/suq", base);
#elif defined(__APPLE__)
	(void)base;
	snprintf(dir, sizeof(dir), "%s/Library/Application Support/suq", home);
#else
	base = getenv("XDG_STATE_HOME");
	if (base != NULL && *base != '\0') {
		snprintf(dir, sizeof(dir), "%s/suq", base);
	} else {
		snprintf(dir, sizeof(dir), "%s/.local/state/suq", home);
	}
#endif
	MakeDirectory(dir);
	snprintf(state_path, sizeof(state_path), "%s/queue.txt", dir);
	snprintf(ui_path, sizeof(ui_path), "%s/ui.txt", dir);
}

static void
persist(void)
{
	if (!q_save(&state, state_path)) {
		say("Couldn't save to %s", state_path);
	}
}

static void
save_ui(void)
{
	FILE	*f;

	if ((f = fopen(ui_path, "w")) == NULL) {
		return;
	}
	fprintf(f, "zoom %.2f\n", zoom);
	fclose(f);
}

static void
load_ui(void)
{
	FILE	*f;
	float	 z;

	if ((f = fopen(ui_path, "r")) == NULL) {
		return;
	}
	if (fscanf(f, "zoom %f", &z) == 1 && z >= 0.5f && z <= 3.0f) {
		zoom = z;
	}
	fclose(f);
}

/*
 * Undo.
 */

static void
history_push(History *h, const Queue *q)
{
	h->top = (h->top + 1) % HISTORY;
	memcpy(&h->items[h->top], q, sizeof(*q));
	if (h->len < HISTORY) {
		h->len++;
	}
}

static bool
history_pop(History *h, Queue *out)
{
	if (h->len == 0) {
		return false;
	}
	memcpy(out, &h->items[h->top], sizeof(*out));
	h->top = (h->top + HISTORY - 1) % HISTORY;
	h->len--;
	return true;
}

/*
 * Wrap every mutation in edit_begin/edit_commit; no-op edits leave no
 * history.
 */
static void
edit_begin(void)
{
	memcpy(&edit_before, &state, sizeof(state));
}

static bool
edit_commit(void)
{
	if (memcmp(&edit_before, &state, sizeof(state)) == 0) {
		return false;
	}
	history_push(&undo_history, &edit_before);
	redo_history.len = 0;
	persist();
	return true;
}

static void
time_travel(History *from, History *to, const char *verb, const char *done)
{
	static Queue	restored;

	if (!history_pop(from, &restored)) {
		say("Nothing to %s", verb);
		return;
	}
	history_push(to, &state);
	memcpy(&state, &restored, sizeof(state));
	selected = -1;
	refresh_suggestions();
	persist();
	say("%s", done);
}

/*
 * Fonts.
 */

static void
init_fonts(void)
{
	const char	*env[FACE_COUNT];
	size_t		 i;
	int		 c, f;

	for (c = 32; c < 127; c++)
		codepoints[codepoint_count++] = c;
	for (c = 160; c < 384; c++)
		codepoints[codepoint_count++] = c;
	for (i = 0; i < nitems(UI_SYMBOLS); i++)
		codepoints[codepoint_count++] = UI_SYMBOLS[i];

	env[FACE_SANS] = getenv("SUQ_FONT");
	env[FACE_MONO] = getenv("SUQ_FONT_MONO");
	for (f = 0; f < FACE_COUNT; f++) {
		if (env[f] != NULL && FileExists(env[f])) {
			face_path[f] = env[f];
			continue;
		}
		for (i = 0; i < nitems(FACE_CANDIDATES[f]) &&
		    FACE_CANDIDATES[f][i] != NULL; i++) {
			if (FileExists(FACE_CANDIDATES[f][i])) {
				face_path[f] = FACE_CANDIDATES[f][i];
				break;
			}
		}
	}
	if (face_path[FACE_MONO] == NULL) {
		face_path[FACE_MONO] = face_path[FACE_SANS];
	}
}

/*
 * Fonts are rasterized at the exact pixel size they're drawn at, so text
 * stays crisp at any zoom level and on high-DPI screens.
 */
static Font
font_for(Face face, float size)
{
	Font	font;
	int	i, px;

	if ((px = (int)roundf(size * dpi)) < 6) {
		px = 6;
	}
	for (i = 0; i < font_cache_len; i++) {
		if (font_cache[i].face == face && font_cache[i].px == px) {
			return font_cache[i].font;
		}
	}

	if (font_cache_len == FONT_CACHE) {
		for (i = 0; i < font_cache_len; i++) {
			if (face_path[font_cache[i].face] != NULL) {
				UnloadFont(font_cache[i].font);
			}
		}
		font_cache_len = 0;
	}
	font = GetFontDefault();
	if (face_path[face] != NULL) {
		font = LoadFontEx(face_path[face], px, codepoints,
		    codepoint_count);
		SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
	}
	font_cache[font_cache_len++] = (Cached_Font){ face, px, font };
	return font;
}

static bool
font_has(Font font, int cp)
{
	int	g;

	g = GetGlyphIndex(font, cp);
	return font.glyphs[g].value == cp && font.glyphs[g].image.width > 0;
}

/* Falls back to the word if the font can't draw the symbol. */
static const char *
keyname(const char *symbol, const char *word)
{
	const char	*p;
	Font		 f;
	int		 size;

	if (!USE_SYMBOLS) {
		return word;
	}
	f = font_for(FACE_SANS, 16);
	for (p = symbol; *p != '\0'; p += size) {
		if (!font_has(f, GetCodepointNext(p, &size))) {
			return word;
		}
	}
	return symbol;
}

static const char *
cat(const char *a, const char *b)
{
	static char	 buf[32][48];
	static int	 n;
	char		*out;

	out = buf[n++ % 32];
	snprintf(out, 48, "%s%s", a, b);
	return out;
}

static const char *
mod(const char *key)
{
	return cat(K_MOD, key);
}

static const char *
shift_mod(const char *key)
{
	if (USE_SYMBOLS) {
		return cat(K_SHIFT, mod(key));
	}
	return cat(K_MOD, cat(K_SHIFT, key));
}

static const char *
either(const char *a, const char *b)
{
	return cat(cat(a, "  "), b);
}

/*
 * Text drawing.
 */

static float
snap(float v)
{
	return roundf(v * dpi) / dpi;
}

static float
text_width(Face face, float size, float spacing, const char *t)
{
	return MeasureTextEx(font_for(face, size), t, size, spacing).x;
}

static float
draw_text(Face face, float size, float spacing, const char *t, float x,
    float y, Color c)
{
	Font	f;

	f = font_for(face, size);
	DrawTextEx(f, t, (Vector2){ snap(x), snap(y) }, size, spacing, c);
	return MeasureTextEx(f, t, size, spacing).x;
}

static float
draw_text_right(Face face, float size, const char *t, float right, float y,
    Color c)
{
	float	w;

	w = text_width(face, size, 0, t);
	draw_text(face, size, 0, t, right - w, y, c);
	return w;
}

/* Returns t, cut with an ellipsis so it fits in max_width. */
static const char *
fit(Face face, float size, const char *t, float max_width)
{
	static char	out[NAME_LEN + 8];
	int		len;

	snprintf(out, sizeof(out), "%s", t);
	if (text_width(face, size, 0, out) <= max_width) {
		return out;
	}
	len = (int)strlen(out);
	while (len > 0) {
		do
			len--;
		while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80);
		snprintf(out + len, sizeof(out) - (size_t)len, "\u2026");
		if (text_width(face, size, 0, out) <= max_width) {
			break;
		}
	}
	return out;
}

static void
rounded(float x, float y, float w, float h, float radius, Color c)
{
	float	shortest;

	shortest = w < h ? w : h;
	DrawRectangleRounded((Rectangle){ x, y, w, h },
	    shortest > 0 ? 2 * radius / shortest : 0, 8, c);
}

static void
draw_hints(const Hint *hints, int n, float x, float y, float size,
    float max_x)
{
	float	gap, w;
	int	i;

	gap = size * 1.4f;
	for (i = 0; i < n; i++) {
		w = text_width(FACE_SANS, size, 0, hints[i].key) + size * 0.4f +
		    text_width(FACE_SANS, size, 0, hints[i].label);
		if (x + w > max_x) {
			break;
		}
		x += draw_text(FACE_SANS, size, 0, hints[i].key, x, y, C_DIM) +
		    size * 0.4f;
		x += draw_text(FACE_SANS, size, 0, hints[i].label, x, y,
		    C_FAINT) + gap;
	}
}

/*
 * Prompt and suggestions.
 */

static int
match_score(const char *name, const char *query)
{
	const char	*p;

	if (str_ieq(name, query)) {
		return 0;
	}
	if (str_istarts(name, query)) {
		return 1;
	}
	for (p = name; *p != '\0'; p++) {
		if (*p == ' ' && str_istarts(p + 1, query)) {
			return 2;
		}
	}
	if (str_ifind(name, query) != NULL) {
		return 3;
	}
	return -1;
}

static int
compare_candidates(const void *a, const void *b)
{
	int	x, y, sx, sy;

	x = *(const int *)a;
	y = *(const int *)b;
	sx = match_score(state.names[x], match_query);
	sy = match_score(state.names[y], match_query);
	if (sx != sy) {
		return sx - sy;
	}
	if (state.last_used[x] != state.last_used[y]) {
		return state.last_used[y] - state.last_used[x];
	}
	return strcmp(state.names[x], state.names[y]);
}

static void
refresh_suggestions(void)
{
	static int	candidates[MAX_NAMES];
	int		i, n;

	suggestion_count = 0;
	suggestion_sel = -1;
	if (input_len == 0) {
		return;
	}

	n = 0;
	for (i = 0; i < state.name_count; i++) {
		if (!state.hidden[i] &&
		    match_score(state.names[i], input) >= 0) {
			candidates[n++] = i;
		}
	}
	match_query = input;
	qsort(candidates, (size_t)n, sizeof(candidates[0]),
	    compare_candidates);

	suggestion_count = n < MAX_SUGGESTIONS ? n : MAX_SUGGESTIONS;
	memcpy(suggestions, candidates,
	    (size_t)suggestion_count * sizeof(suggestions[0]));
	/*
	 * Pre-select only confident matches: whole-name or word prefixes.
	 * A bare substring hit ("dan" inside "Jordan") is more likely a new
	 * person.
	 */
	if (suggestion_count > 0 &&
	    match_score(state.names[suggestions[0]], input) <= 2) {
		suggestion_sel = 0;
	}
}

static void
input_changed(void)
{
	input_changed_at = GetTime();
	selected = -1;
	refresh_suggestions();
}

static void
input_clear(void)
{
	input_len = 0;
	input[0] = '\0';
	input_changed();
}

static void
input_set(const char *s)
{
	snprintf(input, sizeof(input), "%s", s);
	input_len = (int)strlen(input);
	input_changed();
}

static bool
input_append(int cp)
{
	const char	*utf8;
	int		 n;

	utf8 = CodepointToUTF8(cp, &n);
	if (input_len + n >= NAME_LEN) {
		return false;
	}
	memcpy(input + input_len, utf8, (size_t)n);
	input_len += n;
	input[input_len] = '\0';
	return true;
}

static void
input_backspace(bool word)
{
	if (word) {
		while (input_len > 0 && input[input_len - 1] == ' ')
			input_len--;
		while (input_len > 0 && input[input_len - 1] != ' ')
			input_len--;
	} else {
		do
			input_len--;
		while (input_len > 0 &&
		    ((unsigned char)input[input_len] & 0xC0) == 0x80);
	}
	input[input_len] = '\0';
	input_changed();
}

static void
input_paste(void)
{
	const char	*clip, *p;
	int		 cp, size;

	if ((clip = GetClipboardText()) == NULL) {
		return;
	}
	for (p = clip; *p != '\0'; p += size) {
		if ((cp = GetCodepointNext(p, &size)) < 32) {
			cp = ' ';
		}
		if (cp == ' ' &&
		    (input_len == 0 || input[input_len - 1] == ' ')) {
			continue;
		}
		if (!input_append(cp)) {
			break;
		}
	}
	input_changed();
}

/*
 * Actions.
 */

static void
add_from_prompt(bool as_typed)
{
	Enqueue_Result	result;
	char		name[NAME_LEN];
	int		who;

	if (!as_typed && suggestion_sel >= 0) {
		snprintf(name, sizeof(name), "%s",
		    state.names[suggestions[suggestion_sel]]);
	} else {
		snprintf(name, sizeof(name), "%s", input);
	}
	if (name_normalize(name) == 0) {
		return;
	}

	edit_begin();
	if ((who = q_intern(&state, name)) < 0) {
		say("The roster is full");
		return;
	}
	result = q_enqueue(&state, who, wall_now());
	edit_commit();

	if (result == ENQ_ALREADY_QUEUED) {
		say("%s is already waiting at #%d", state.names[who],
		    q_position(q_active(&state), who) + 1);
	}
	if (result == ENQ_FULL) {
		say("This queue is full");
	}
	flash(who);
	input_clear();
}

static void
next_speaker(void)
{
	edit_begin();
	q_next(&state, wall_now());
	if (!edit_commit()) {
		say("No one is waiting");
		return;
	}
	selected = -1;
	if (state.speaker >= 0) {
		flash(state.speaker);
	}
}

static void
call_selected(void)
{
	edit_begin();
	q_call(&state, selected, wall_now());
	edit_commit();
	flash(state.speaker);
	selected = -1;
}

static void
remove_selected(void)
{
	int	who;

	who = q_active(&state)->queue[selected];
	edit_begin();
	q_remove(&state, selected);
	edit_commit();
	say("Removed %s", state.names[who]);
	/* Keep the cursor in place so several can be removed in a row. */
	if (selected >= q_active(&state)->len) {
		selected = q_active(&state)->len - 1;
	}
}

static void
move_selected(int delta)
{
	edit_begin();
	if (q_move(&state, selected, delta)) {
		selected += delta;
	}
	edit_commit();
}

static void
open_level(void)
{
	edit_begin();
	if (!q_open_level(&state)) {
		say("Sub-motions only nest %d deep", MAX_LEVELS - 1);
		return;
	}
	edit_commit();
	selected = -1;
}

static void
close_level(void)
{
	const char	*name;
	int		 dropped;

	name = level_name(state.depth - 1);
	edit_begin();
	if ((dropped = q_close_level(&state)) < 0) {
		say("Already at the main queue");
		return;
	}
	edit_commit();
	selected = -1;
	if (dropped > 0) {
		say("Closed %s, dropping %s \u2014 %s to undo", name,
		    plural(dropped, "waiting speaker", "waiting speakers"),
		    mod("Z"));
	} else {
		say("Closed %s", name);
	}
}

static void
forget_suggestion(void)
{
	int	who;

	if (suggestion_sel < 0) {
		return;
	}
	who = suggestions[suggestion_sel];
	edit_begin();
	state.hidden[who] = true;
	edit_commit();
	say("%s won't be suggested again", state.names[who]);
	refresh_suggestions();
}

static void
reset(bool forget_names)
{
	edit_begin();
	q_reset(&state, forget_names);
	edit_commit();
	overlay = OVERLAY_NONE;
	selected = -1;
	refresh_suggestions();
	say("Fresh start \u2014 %s to undo", mod("Z"));
}

static void
set_zoom(float z)
{
	if (z < 0.6f) {
		z = 0.6f;
	}
	if (z > 2.6f) {
		z = 2.6f;
	}
	zoom = roundf(z * 10) / 10;
	save_ui();
	say("Zoom %d%%", (int)roundf(zoom * 100));
}

/*
 * Input.
 */

static bool
handle_overlay_keys(void)
{
	bool	shift;

	if (overlay == OVERLAY_NONE) {
		return false;
	}
	while (GetCharPressed() != 0)
		;

	if (overlay == OVERLAY_HELP) {
		if (GetKeyPressed() != 0) {
			overlay = OVERLAY_NONE;
		}
	} else if (overlay == OVERLAY_RESET) {
		shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
		if (IsKeyPressed(KEY_ESCAPE)) {
			overlay = OVERLAY_NONE;
		}
		if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
			reset(shift);
		}
	}
	return true;
}

/* Returns true when the app should quit. */
static bool
handle_chords(bool shift)
{
	if (IsKeyPressed(KEY_Y) || (IsKeyPressed(KEY_Z) && shift)) {
		time_travel(&redo_history, &undo_history, "redo", "Redone");
	} else if (IsKeyPressed(KEY_Z)) {
		time_travel(&undo_history, &redo_history, "undo", "Undone");
	} else if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
		open_level();
	} else if (IsKeyPressed(KEY_LEFT_BRACKET)) {
		close_level();
	} else if (IsKeyPressed(KEY_R)) {
		overlay = OVERLAY_RESET;
	} else if (IsKeyPressed(KEY_D)) {
		forget_suggestion();
	} else if (IsKeyPressed(KEY_V)) {
		input_paste();
	} else if (pressed(KEY_BACKSPACE)) {
		input_clear();
	} else if (pressed(KEY_EQUAL) || pressed(KEY_KP_ADD)) {
		set_zoom(zoom + 0.1f);
	} else if (pressed(KEY_MINUS) || pressed(KEY_KP_SUBTRACT)) {
		set_zoom(zoom - 0.1f);
	} else if (IsKeyPressed(KEY_ZERO)) {
		set_zoom(1.0f);
	} else if (IsKeyPressed(KEY_F)) {
		ToggleBorderlessWindowed();
	} else if (IsKeyPressed(KEY_Q)) {
		return true;
	}
	return false;
}

/* Returns true when the app should quit. */
static bool
handle_input(void)
{
	bool	shift, alt, cmd, enter, typed, prompt_was_empty, at_ghost;
	int	cp, queued, pick;

	if (handle_overlay_keys()) {
		return false;
	}

	shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
	alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
	cmd = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) ||
	    IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

	if (cmd) {
		while (GetCharPressed() != 0)
			;
		return handle_chords(shift);
	}

	/* Space means "next" only on an empty prompt; names may have spaces. */
	prompt_was_empty = input_len == 0;
	typed = false;
	while ((cp = GetCharPressed()) != 0) {
		if (cp == ' ' && input_len == 0) {
			continue;
		}
		if (cp == '?' && input_len == 0) {
			overlay = OVERLAY_HELP;
			continue;
		}
		typed |= input_append(cp);
	}
	if (typed) {
		input_changed();
	}

	if (IsKeyPressed(KEY_SPACE) && prompt_was_empty) {
		next_speaker();
	}
	if (IsKeyPressed(KEY_F1)) {
		overlay = OVERLAY_HELP;
	}

	queued = q_active(&state)->len;
	enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);

	if (input_len > 0) {
		if (enter) {
			add_from_prompt(shift);
		}
		if (pressed(KEY_BACKSPACE)) {
			input_backspace(alt);
		}
		if (IsKeyPressed(KEY_ESCAPE)) {
			input_clear();
		}
		/* The list grows upward, so Up walks away from the prompt. */
		if (pressed(KEY_UP) && suggestion_sel < suggestion_count - 1) {
			suggestion_sel++;
		}
		if (pressed(KEY_DOWN) && suggestion_sel > -1) {
			suggestion_sel--;
		}
		at_ghost = suggestion_sel >= 0 && str_istarts(
		    state.names[suggestions[suggestion_sel]], input);
		if (IsKeyPressed(KEY_TAB) ||
		    (IsKeyPressed(KEY_RIGHT) && at_ghost)) {
			pick = suggestion_sel >= 0 ? suggestion_sel : 0;
			if (suggestion_count > 0) {
				input_set(state.names[suggestions[pick]]);
			}
		}
	} else if (queued > 0) {
		if (pressed(KEY_DOWN)) {
			if (shift && selected >= 0) {
				move_selected(1);
			} else if (selected < queued - 1) {
				selected++;
			}
		}
		if (pressed(KEY_UP)) {
			if (shift && selected >= 0) {
				move_selected(-1);
			} else if (selected < 0) {
				selected = queued - 1;
			} else if (selected > 0) {
				selected--;
			}
		}
		if (selected >= 0) {
			if (enter) {
				call_selected();
			} else if (IsKeyPressed(KEY_BACKSPACE) ||
			    IsKeyPressed(KEY_DELETE)) {
				remove_selected();
			} else if (IsKeyPressed(KEY_ESCAPE)) {
				selected = -1;
			}
		}
	}

	if (selected >= q_active(&state)->len) {
		selected = q_active(&state)->len - 1;
	}
	return false;
}

/*
 * Drawing.
 */

static Color
fade_to(Color from, Color to, float t)
{
	if (t < 0) {
		t = 0;
	}
	if (t > 1) {
		t = 1;
	}
	return (Color){
	    (unsigned char)(from.r + (to.r - from.r) * t),
	    (unsigned char)(from.g + (to.g - from.g) * t),
	    (unsigned char)(from.b + (to.b - from.b) * t),
	    (unsigned char)(from.a + (to.a - from.a) * t),
	};
}

static float
flash_amount(int who)
{
	float	t;

	if (who != flash_who) {
		return 0;
	}
	t = (float)((GetTime() - flash_at) / FLASH_SECONDS);
	return t >= 1 ? 0 : 1 - t * t;
}

static float
draw_speaker(float s, float x0, float x1, float y)
{
	const Level	*l, *spoke_on;
	const char	*name;
	char		 meta[NAME_LEN + 32], timer[32];
	float		 label_size, name_size, meta_size, timer_size;
	float		 timer_w, room, w, size;
	int		 i, level;

	label_size = 12 * s;
	name_size = 54 * s;
	meta_size = 15 * s;
	timer_size = 34 * s;

	draw_text(FACE_SANS, label_size, 2 * s, "SPEAKING", x0, y, C_FAINT);
	y += label_size + 10 * s;

	if (state.speaker < 0) {
		draw_text(FACE_SANS, name_size, 0, "Floor open", x0, y,
		    C_FAINT);
		y += name_size * 1.12f;
		l = q_active_c(&state);
		if (l->len > 0) {
			snprintf(meta, sizeof(meta), "space calls %s",
			    state.names[l->queue[0]]);
		} else {
			snprintf(meta, sizeof(meta),
			    "Type a name to add a speaker");
		}
		draw_text(FACE_SANS, meta_size, 0, meta, x0, y, C_FAINT);
		return y + meta_size * 1.3f;
	}

	format_elapsed(wall_now() - state.speaker_since, timer, sizeof(timer));
	timer_w = draw_text_right(FACE_MONO, timer_size, timer, x1,
	    y + (name_size - timer_size) * 0.55f, C_DIM);

	/* Long names shrink before they get cut. */
	name = state.names[state.speaker];
	room = x1 - x0 - timer_w - 24 * s;
	w = text_width(FACE_SANS, name_size, 0, name);
	size = name_size;
	if (w > room) {
		size = fmaxf(name_size * room / w, name_size * 0.6f);
	}
	draw_text(FACE_SANS, size, 0, fit(FACE_SANS, size, name, room), x0,
	    y + (name_size - size) * 0.6f,
	    fade_to(C_BRIGHT, C_ACCENT, flash_amount(state.speaker)));
	y += name_size * 1.12f;

	spoke_on = NULL;
	level = 0;
	for (i = 0; i < state.depth; i++) {
		if (state.levels[i].id == state.speaker_level_id) {
			spoke_on = &state.levels[i];
			level = i;
		}
	}
	if (spoke_on != NULL) {
		snprintf(meta, sizeof(meta), "%s \u00B7 %s turn",
		    level_name(level),
		    ordinal(q_turns(&state, spoke_on, state.speaker)));
	} else {
		snprintf(meta, sizeof(meta), "Closed sub-motion");
	}
	draw_text(FACE_SANS, meta_size, 0, meta, x0, y, C_DIM);
	return y + meta_size * 1.3f;
}

static void
draw_breadcrumb(float s, float x0, float x1, float y)
{
	char	n[16];
	float	size, x;
	bool	active;
	int	i, len;

	size = 15 * s;
	x = x0;
	for (i = 0; i < state.depth; i++) {
		active = i == state.depth - 1;
		if (i > 0) {
			x += draw_text(FACE_SANS, size, 0, "  \u203A  ", x, y,
			    C_FAINT);
		}
		x += draw_text(FACE_SANS, size, 0, level_name(i), x, y,
		    active ? C_TEXT : C_DIM);
		if (!active && state.levels[i].len > 0) {
			snprintf(n, sizeof(n), " (%d)", state.levels[i].len);
			x += draw_text(FACE_SANS, size, 0, n, x, y, C_FAINT);
		}
	}
	if ((len = q_active_c(&state)->len) > 0) {
		draw_text_right(FACE_SANS, size,
		    plural(len, "waiting", "waiting"), x1, y, C_FAINT);
	}
}

static void
draw_queue(float s, float x0, float x1, float top, float bottom)
{
	const Level	*l;
	const char	*note;
	Color		 c;
	char		 num[8], more[48];
	float		 row_h, name_size, small_size, y, text_y, glow;
	float		 note_w, name_x;
	int		 fits, shown, first, row, i, who, turns, above, below;

	l = q_active_c(&state);
	row_h = 40 * s;
	name_size = 21 * s;
	small_size = 15 * s;

	if (l->len == 0) {
		draw_text(FACE_SANS, small_size, 0, state.depth > 1 ?
		    "This sub-motion's queue is empty" : "The queue is empty",
		    x0, top + (row_h - small_size) / 2, C_FAINT);
		return;
	}

	if ((fits = (int)((bottom - top) / row_h)) < 1) {
		fits = 1;
	}
	shown = l->len <= fits ? l->len : fits - 1;
	first = selected >= shown ? selected - shown + 1 : 0;

	for (row = 0; row < shown && first + row < l->len; row++) {
		i = first + row;
		who = l->queue[i];
		y = top + row * row_h;
		text_y = y + (row_h - name_size) / 2;

		if ((glow = flash_amount(who)) > 0) {
			rounded(x0 - 14 * s, y, x1 - x0 + 28 * s, row_h, 8 * s,
			    Fade(C_ACCENT, 0.22f * glow));
		}
		if (i == selected) {
			rounded(x0 - 14 * s, y, x1 - x0 + 28 * s, row_h, 8 * s,
			    C_RAISED);
			rounded(x0 - 14 * s, y + 9 * s, 3 * s, row_h - 18 * s,
			    1.5f * s, C_ACCENT);
		}

		snprintf(num, sizeof(num), "%d", i + 1);
		draw_text_right(FACE_MONO, small_size, num, x0 + 22 * s,
		    y + (row_h - small_size) / 2, i == 0 ? C_ACCENT : C_FAINT);

		note = NULL;
		turns = q_turns(&state, l, who);
		if (who == state.speaker) {
			note = "speaking now";
		} else if (turns > 0) {
			note = plural(turns, "turn", "turns");
		}
		note_w = 0;
		if (note != NULL) {
			note_w = draw_text_right(FACE_SANS, small_size, note,
			    x1, y + (row_h - small_size) / 2, C_FAINT);
		}

		name_x = x0 + 40 * s;
		c = i == 0 || i == selected ? C_BRIGHT : C_TEXT;
		draw_text(FACE_SANS, name_size, 0, fit(FACE_SANS, name_size,
		    state.names[who], x1 - name_x - note_w - 20 * s), name_x,
		    text_y, c);
	}

	if (shown < l->len) {
		below = l->len - first - shown;
		above = first;
		if (above > 0 && below > 0) {
			snprintf(more, sizeof(more),
			    "%d above \u00B7 %d more below", above, below);
		} else if (above > 0) {
			snprintf(more, sizeof(more), "%d more above", above);
		} else {
			snprintf(more, sizeof(more), "%d more", below);
		}
		draw_text(FACE_SANS, small_size, 0, more, x0 + 40 * s,
		    top + shown * row_h + (row_h - small_size) / 2, C_FAINT);
	}
}

/* Draws name with the part matching the prompt picked out. */
static void
draw_match(const char *name, float x, float y, float size, float max_w,
    bool active)
{
	const char	*hit, *p;
	Color		 rest;
	char		 buf[NAME_LEN + 8], seg[NAME_LEN + 8];

	snprintf(buf, sizeof(buf), "%s", fit(FACE_SANS, size, name, max_w));
	hit = str_istarts(buf, input) ? buf : NULL;
	for (p = buf; hit == NULL && *p != '\0'; p++) {
		if (*p == ' ' && str_istarts(p + 1, input)) {
			hit = p + 1;
		}
	}
	if (hit == NULL) {
		hit = str_ifind(buf, input);
	}

	rest = active ? C_TEXT : C_DIM;
	if (hit == NULL) {
		draw_text(FACE_SANS, size, 0, buf, x, y, rest);
		return;
	}
	snprintf(seg, sizeof(seg), "%.*s", (int)(hit - buf), buf);
	x += draw_text(FACE_SANS, size, 0, seg, x, y, rest);
	snprintf(seg, sizeof(seg), "%.*s", input_len, hit);
	x += draw_text(FACE_SANS, size, 0, seg, x, y, C_BRIGHT);
	draw_text(FACE_SANS, size, 0, hit + strlen(seg), x, y, rest);
}

static void
draw_suggestions(float s, float x, float w, float bottom)
{
	const Level	*l;
	char		 note[48];
	float		 row_h, size, small, inset, h, y, ry, note_w;
	int		 i, who, pos, turns;

	if (suggestion_count == 0) {
		return;
	}
	row_h = 34 * s;
	size = 18 * s;
	small = 14 * s;
	inset = 6 * s;
	h = suggestion_count * row_h + 2 * inset;
	y = bottom - h;
	rounded(x - 1, y - 1, w + 2, h + 2, 10 * s, C_LINE);
	rounded(x, y, w, h, 10 * s, C_PANEL);

	l = q_active_c(&state);
	for (i = 0; i < suggestion_count; i++) {
		who = suggestions[i];
		ry = y + h - inset - (i + 1) * row_h;
		if (i == suggestion_sel) {
			rounded(x + inset, ry, w - 2 * inset, row_h, 7 * s,
			    C_RAISED);
		}

		note[0] = '\0';
		pos = q_position(l, who);
		turns = q_turns(&state, l, who);
		if (who == state.speaker) {
			snprintf(note, sizeof(note), "speaking");
		} else if (pos >= 0) {
			snprintf(note, sizeof(note), "waiting #%d", pos + 1);
		} else if (turns > 0) {
			snprintf(note, sizeof(note), "%s",
			    plural(turns, "turn", "turns"));
		}
		note_w = draw_text_right(FACE_SANS, small, note, x + w - 18 * s,
		    ry + (row_h - small) / 2,
		    who == state.speaker || pos >= 0 ? C_ACCENT : C_FAINT);

		draw_match(state.names[who], x + 18 * s,
		    ry + (row_h - size) / 2, size, w - 54 * s - note_w,
		    i == suggestion_sel);
	}
}

static void
draw_prompt(float s, float x, float w, float y, float h)
{
	const char	*name;
	char		 ghost[NAME_LEN + 16];
	double		 since;
	float		 size, text_y, tx;

	size = 21 * s;
	text_y = y + (h - size) / 2;
	rounded(x, y, w, h, 10 * s, C_PANEL);

	tx = x + 18 * s;
	draw_text(FACE_SANS, size, 0, "+", tx, text_y,
	    input_len > 0 ? C_ACCENT : C_FAINT);
	tx += 26 * s;

	if (input_len > 0) {
		tx += draw_text(FACE_SANS, size, 0, input, tx, text_y,
		    C_BRIGHT);
	} else if (selected < 0) {
		draw_text(FACE_SANS, size, 0, "Add a speaker", tx + 3 * s,
		    text_y, C_FAINT);
	}

	/* Steady while typing, then a slow blink. */
	since = GetTime() - input_changed_at;
	if (since < 0.6 || fmod(since, 1.1) < 0.65) {
		DrawRectangleRec((Rectangle){ snap(tx + 1 * s), text_y + 1 * s,
		    fmaxf(2 * s, 1), size }, C_ACCENT);
	}

	if (suggestion_sel < 0) {
		return;
	}
	name = state.names[suggestions[suggestion_sel]];
	if (str_istarts(name, input)) {
		draw_text(FACE_SANS, size, 0, name + input_len, tx + 3 * s,
		    text_y, C_FAINT);
	} else {
		snprintf(ghost, sizeof(ghost), "  \u2192 %s", name);
		draw_text(FACE_SANS, size, 0, ghost, tx + 3 * s, text_y,
		    C_FAINT);
	}
}

static void
draw_footer(float s, float x0, float x1, float y)
{
	const Level	*l;
	Hint		 hints[8];
	char		 totals[64];
	double		 age;
	float		 size, totals_w, max_x, alpha;
	bool		 picked;
	int		 i, t, turns, people, n;

	size = 13.5f * s;

	/* Right: totals for the active queue level. */
	l = q_active_c(&state);
	turns = people = 0;
	for (i = 0; i < state.name_count; i++) {
		t = q_turns(&state, l, i);
		turns += t;
		people += t > 0;
	}
	snprintf(totals, sizeof(totals), "%s \u00B7 %s",
	    plural(people, "speaker", "speakers"),
	    plural(turns, "turn", "turns"));
	totals_w = draw_text_right(FACE_SANS, size, totals, x1, y, C_FAINT);
	max_x = x1 - totals_w - 24 * s;

	if ((age = GetTime() - status_at) < STATUS_SECONDS) {
		alpha = 1;
		if (age > STATUS_SECONDS - 0.4) {
			alpha = (float)((STATUS_SECONDS - age) / 0.4);
		}
		draw_text(FACE_SANS, size, 0,
		    fit(FACE_SANS, size, status, max_x - x0), x0, y,
		    Fade(C_ACCENT, alpha));
		return;
	}

	n = 0;
	if (input_len > 0) {
		picked = suggestion_sel >= 0 &&
		    !str_ieq(state.names[suggestions[suggestion_sel]], input);
		hints[n++] = (Hint){ K_ENTER, "add" };
		if (picked) {
			hints[n++] = (Hint){ cat(K_SHIFT, K_ENTER),
			    "add as typed" };
		}
		if (suggestion_count > 0) {
			hints[n++] = (Hint){ K_TAB, "complete" };
		}
		if (suggestion_count > 1) {
			hints[n++] = (Hint){ K_UPDOWN, "choose" };
		}
		if (suggestion_sel >= 0) {
			hints[n++] = (Hint){ mod("D"), "forget name" };
		}
		hints[n++] = (Hint){ "esc", "clear" };
	} else if (selected >= 0) {
		hints[n++] = (Hint){ K_ENTER, "give floor" };
		hints[n++] = (Hint){ cat(K_SHIFT, K_UPDOWN), "move" };
		hints[n++] = (Hint){ K_BKSP, "remove" };
		hints[n++] = (Hint){ "esc", "done" };
	} else {
		if (l->len > 0) {
			hints[n++] = (Hint){ "space", "next" };
		} else if (state.speaker >= 0) {
			hints[n++] = (Hint){ "space", "end turn" };
		}
		if (l->len > 0) {
			hints[n++] = (Hint){ K_UPDOWN, "select" };
		}
		hints[n++] = (Hint){ mod("]"), "sub-motion" };
		if (state.depth > 1) {
			hints[n++] = (Hint){ mod("["), "close it" };
		}
		hints[n++] = (Hint){ mod("Z"), "undo" };
		hints[n++] = (Hint){ "?", "help" };
	}
	draw_hints(hints, n, x0, y, size, max_x);
}

static float
draw_panel(float s, float w, float h)
{
	float	sw, sh, x, y;

	sw = (float)GetScreenWidth();
	sh = (float)GetScreenHeight();
	DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, Fade(BLACK, 0.6f));
	x = roundf((sw - w) / 2);
	y = roundf((sh - h) / 2);
	rounded(x - 1, y - 1, w + 2, h + 2, 14 * s, C_LINE);
	rounded(x, y, w, h, 14 * s, C_PANEL);
	return y;
}

static void
draw_help(float s)
{
	const Hint rows[] = {
		{ NULL, "Speakers" },
		{ K_ENTER, "Add the highlighted name, or what you typed" },
		{ cat(K_SHIFT, K_ENTER), "Add exactly what you typed" },
		{ either(K_TAB, K_RIGHT), "Complete the name" },
		{ "space", "End this turn and call the next speaker" },
		{ NULL, "Waiting list (with an empty prompt)" },
		{ K_UPDOWN, "Select someone waiting" },
		{ cat(K_SHIFT, K_UPDOWN), "Move them up or down" },
		{ K_ENTER, "Give them the floor now" },
		{ K_BKSP, "Take them off the list" },
		{ NULL, "Sub-motions" },
		{ mod("]"),
		    "Open a sub-motion with its own queue and turn counts" },
		{ mod("["), "Close it and return to the queue above" },
		{ NULL, "General" },
		{ either(mod("Z"), shift_mod("Z")), "Undo, redo" },
		{ mod("D"), "Stop suggesting the highlighted name" },
		{ mod("R"), "Start fresh" },
		{ either(mod("="), mod("-")), "Zoom" },
		{ mod("F"), "Full screen" },
	};
	float	units, size, row_h, head_h, pad, key_col, h, w, x, y;
	size_t	i;

	/* Shrink to fit short windows. */
	units = 2 * 28 + 34 + 15 + 31;
	for (i = 0; i < nitems(rows); i++)
		units += rows[i].key != NULL ? 25 : 36;
	s = fminf(s, (GetScreenHeight() - 24.0f) / units);

	size = 15 * s;
	row_h = 25 * s;
	head_h = 36 * s;
	pad = 28 * s;
	h = units * s;
	key_col = 0;
	for (i = 0; i < nitems(rows); i++) {
		if (rows[i].key != NULL) {
			key_col = fmaxf(key_col,
			    text_width(FACE_SANS, size, 0, rows[i].key));
		}
	}
	key_col += 24 * s;
	w = fminf(600 * s, GetScreenWidth() - 32.0f);
	y = draw_panel(s, w, h) + pad;
	x = (GetScreenWidth() - w) / 2 + pad;

	draw_text(FACE_SANS, 22 * s, 0, "Keys", x, y, C_BRIGHT);
	y += 34 * s;
	draw_text(FACE_SANS, size, 0, fit(FACE_SANS, size,
	    "People with fewer turns are placed ahead of those with more.",
	    w - 2 * pad), x, y, C_DIM);
	y += size;

	for (i = 0; i < nitems(rows); i++) {
		if (rows[i].key == NULL) {
			draw_text(FACE_SANS, 11.5f * s, 1.5f * s,
			    TextToUpper(rows[i].label), x,
			    y + head_h - 11.5f * s - 8 * s, C_FAINT);
			y += head_h;
			continue;
		}
		draw_text(FACE_SANS, size, 0, rows[i].key, x, y, C_TEXT);
		draw_text(FACE_SANS, size, 0, fit(FACE_SANS, size,
		    rows[i].label, w - 2 * pad - key_col), x + key_col, y,
		    C_DIM);
		y += row_h;
	}
	draw_text(FACE_SANS, 13 * s, 0, "Press any key to close", x,
	    y + 18 * s, C_FAINT);
}

static void
draw_reset(float s)
{
	Hint	hints[3];
	float	pad, w, h, x, y;

	pad = 28 * s;
	w = fminf(460 * s, GetScreenWidth() - 32.0f);
	h = 2 * pad + 112 * s;
	y = draw_panel(s, w, h) + pad;
	x = (GetScreenWidth() - w) / 2 + pad;
	draw_text(FACE_SANS, 22 * s, 0, "Start fresh?", x, y, C_BRIGHT);
	y += 38 * s;
	draw_text(FACE_SANS, 15 * s, 0, fit(FACE_SANS, 15 * s,
	    "Clears every queue and turn count.", w - 2 * pad), x, y, C_DIM);
	y += 46 * s;
	hints[0] = (Hint){ K_ENTER, "reset" };
	hints[1] = (Hint){ cat(K_SHIFT, K_ENTER), "also forget names" };
	hints[2] = (Hint){ "esc", "cancel" };
	draw_hints(hints, nitems(hints), x, y, 14 * s, x + w - 2 * pad);
}

static void
draw(void)
{
	float	s, sw, sh, pad, x0, x1, y;
	float	footer_y, prompt_h, prompt_y, box_x, box_w;

	s = zoom;
	sw = (float)GetScreenWidth();
	sh = (float)GetScreenHeight();
	pad = roundf(fminf(40 * s, sw * 0.06f));
	x0 = pad;
	x1 = sw - pad;

	ClearBackground(C_BG);

	y = draw_speaker(s, x0, x1, pad);
	y += 26 * s;
	DrawRectangleRec((Rectangle){ x0, snap(y), x1 - x0,
	    fmaxf(1 / dpi, 1 * s / 2) }, C_LINE);
	y += 24 * s;
	draw_breadcrumb(s, x0, x1, y);
	y += 32 * s;

	footer_y = sh - pad * 0.75f - 14 * s;
	prompt_h = 50 * s;
	prompt_y = footer_y - 18 * s - prompt_h;
	box_x = x0 - 14 * s;
	box_w = x1 - x0 + 28 * s;

	draw_queue(s, x0, x1, y, prompt_y - 14 * s);
	draw_suggestions(s, box_x, box_w, prompt_y - 8 * s);
	draw_prompt(s, box_x, box_w, prompt_y, prompt_h);
	draw_footer(s, x0, x1, footer_y);

	if (overlay == OVERLAY_HELP) {
		draw_help(s);
	}
	if (overlay == OVERLAY_RESET) {
		draw_reset(s);
	}
}

int
main(void)
{
	int	i;

	SetTraceLogLevel(LOG_ERROR);
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI |
	    FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
	InitWindow(880, 680, "suq");
	SetWindowMinSize(520, 440);
	SetExitKey(KEY_NULL);
	SetTargetFPS(60);

	resolve_paths();
	q_init(&state);
	if (FileExists(state_path) && !q_load(&state, state_path)) {
		say("Couldn't read %s; starting fresh", state_path);
	}
	load_ui();

	dpi = GetWindowScaleDPI().x;
	init_fonts();
	input_changed_at = GetTime();

	while (!WindowShouldClose()) {
		dpi = GetWindowScaleDPI().x;
		if (handle_input()) {
			break;
		}
		BeginDrawing();
		draw();
		EndDrawing();
	}

	for (i = 0; i < font_cache_len; i++) {
		if (face_path[font_cache[i].face] != NULL) {
			UnloadFont(font_cache[i].font);
		}
	}
	CloseWindow();
	return 0;
}
