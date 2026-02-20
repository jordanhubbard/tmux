/* $OpenBSD$ */

/*
 * Copyright (c) 2026 Jordan Hubbard
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

static struct screen	*window_grid_init(struct window_mode_entry *,
			    struct cmd_find_state *, struct args *);
static void		 window_grid_free(struct window_mode_entry *);
static void		 window_grid_resize(struct window_mode_entry *,
			    u_int, u_int);
static void		 window_grid_key(struct window_mode_entry *,
			    struct client *, struct session *, struct winlink *,
			    key_code, struct mouse_event *);

static void		 window_grid_timer_callback(int, short, void *);
static void		 window_grid_draw_screen(struct window_mode_entry *);
static void		 window_grid_build_items(struct window_mode_entry *);
static void		 window_grid_compute_layout(struct window_mode_entry *);

const struct window_mode window_grid_mode = {
	.name = "grid-mode",

	.init = window_grid_init,
	.free = window_grid_free,
	.resize = window_grid_resize,
	.key = window_grid_key,
};

enum window_grid_type {
	WINDOW_GRID_SESSIONS,
	WINDOW_GRID_WINDOWS,
};

struct window_grid_item {
	int	session_id;
	int	winlink_idx;	/* -1 for session mode */
};

struct window_grid_mode_data {
	struct screen		 screen;
	struct event		 timer;
	enum window_grid_type	 type;

	struct window_grid_item	*items;
	u_int			 nitems;

	u_int	columns, rows;		/* grid dimensions */
	u_int	cell_w, cell_h;		/* cell size in characters */
	u_int	cx, cy;			/* cursor position (col, row) */
	u_int	offset;			/* first visible row (scrolling) */
	u_int	total_rows;		/* total rows needed */
};

#define GRID_MIN_CELL_W 20
#define GRID_MIN_CELL_H 6

static void
window_grid_start_timer(struct window_mode_entry *wme)
{
	struct window_grid_mode_data	*data = wme->data;
	struct timeval			 tv = { .tv_sec = 1 };

	evtimer_add(&data->timer, &tv);
}

static void
window_grid_timer_callback(__unused int fd, __unused short events, void *arg)
{
	struct window_mode_entry		*wme = arg;
	struct window_pane			*wp = wme->wp;
	struct window_grid_mode_data	*data = wme->data;

	evtimer_del(&data->timer);

	window_grid_build_items(wme);
	window_grid_compute_layout(wme);

	/* Clamp cursor after item list change. */
	if (data->nitems == 0) {
		data->cx = 0;
		data->cy = 0;
	} else {
		u_int idx = data->cy * data->columns + data->cx;
		if (idx >= data->nitems) {
			idx = data->nitems - 1;
			data->cx = idx % data->columns;
			data->cy = idx / data->columns;
		}
	}

	window_grid_draw_screen(wme);
	wp->flags |= PANE_REDRAW;

	window_grid_start_timer(wme);
}

static void
window_grid_build_items(struct window_mode_entry *wme)
{
	struct window_grid_mode_data	*data = wme->data;
	struct session			*s;
	struct winlink			*wl;
	u_int				 n;

	free(data->items);
	data->items = NULL;
	data->nitems = 0;

	if (data->type == WINDOW_GRID_SESSIONS) {
		/* Count sessions first. */
		n = 0;
		RB_FOREACH(s, sessions, &sessions) {
			if (session_alive(s))
				n++;
		}
		if (n == 0)
			return;

		data->items = xcalloc(n, sizeof *data->items);
		data->nitems = 0;
		RB_FOREACH(s, sessions, &sessions) {
			if (!session_alive(s))
				continue;
			data->items[data->nitems].session_id = s->id;
			data->items[data->nitems].winlink_idx = -1;
			data->nitems++;
		}
	} else {
		/* Window mode: windows of the session that owns this pane. */
		s = NULL;
		if (wme->wp != NULL && wme->wp->window != NULL) {
			RB_FOREACH(s, sessions, &sessions) {
				if (!session_alive(s))
					continue;
				RB_FOREACH(wl, winlinks, &s->windows) {
					if (wl->window == wme->wp->window)
						goto found;
				}
			}
			s = NULL;
		}
found:
		if (s == NULL)
			return;

		n = 0;
		RB_FOREACH(wl, winlinks, &s->windows)
			n++;
		if (n == 0)
			return;

		data->items = xcalloc(n, sizeof *data->items);
		data->nitems = 0;
		RB_FOREACH(wl, winlinks, &s->windows) {
			data->items[data->nitems].session_id = s->id;
			data->items[data->nitems].winlink_idx = wl->idx;
			data->nitems++;
		}
	}
}

static void
window_grid_compute_layout(struct window_mode_entry *wme)
{
	struct window_grid_mode_data	*data = wme->data;
	struct screen			*s = &data->screen;
	u_int				 sx, sy, n;

	sx = screen_size_x(s);
	sy = screen_size_y(s);
	n = data->nitems;

	if (n == 0) {
		data->columns = 0;
		data->rows = 0;
		data->cell_w = 0;
		data->cell_h = 0;
		data->total_rows = 0;
		return;
	}

	/*
	 * Balanced grid: rows * columns >= n.
	 * Adapted from layout_set_tiled() in layout-set.c.
	 */
	data->rows = 1;
	data->columns = 1;
	while (data->rows * data->columns < n) {
		data->rows++;
		if (data->rows * data->columns < n)
			data->columns++;
	}

	/* Compute cell size from available screen space. */
	data->cell_w = sx / data->columns;
	data->cell_h = sy / data->rows;

	/* Enforce minimum cell size; reduce grid if needed. */
	if (data->cell_w < GRID_MIN_CELL_W && sx >= GRID_MIN_CELL_W) {
		data->columns = sx / GRID_MIN_CELL_W;
		if (data->columns == 0)
			data->columns = 1;
		data->cell_w = sx / data->columns;
	}
	if (data->cell_h < GRID_MIN_CELL_H && sy >= GRID_MIN_CELL_H) {
		u_int visible_rows = sy / GRID_MIN_CELL_H;
		if (visible_rows == 0)
			visible_rows = 1;
		data->cell_h = sy / visible_rows;
	}

	if (data->cell_w < 3)
		data->cell_w = 3;
	if (data->cell_h < 3)
		data->cell_h = 3;

	/* Total rows needed for all items. */
	data->total_rows = (n + data->columns - 1) / data->columns;
}

static void
window_grid_draw_screen(struct window_mode_entry *wme)
{
	struct window_grid_mode_data	*data = wme->data;
	struct screen			*s = &data->screen;
	struct screen_write_ctx		 ctx;
	struct grid_cell		 gc, sel_gc;
	u_int				 row, col, idx, px, py;
	u_int				 visible_rows, cw, ch;
	const char			*name;
	struct session			*sess;
	struct winlink			*wl;
	struct screen			*target;

	screen_write_start(&ctx, s);
	screen_write_clearscreen(&ctx, 8);

	if (data->nitems == 0 || data->columns == 0) {
		screen_write_stop(&ctx);
		return;
	}

	/* How many rows fit on screen. */
	visible_rows = screen_size_y(s) / data->cell_h;
	if (visible_rows == 0)
		visible_rows = 1;

	/* Clamp offset so cursor row is visible. */
	if (data->cy < data->offset)
		data->offset = data->cy;
	else if (data->cy >= data->offset + visible_rows)
		data->offset = data->cy - visible_rows + 1;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	memcpy(&sel_gc, &grid_default_cell, sizeof sel_gc);
	sel_gc.attr |= GRID_ATTR_REVERSE;

	cw = data->cell_w;
	ch = data->cell_h;

	for (row = data->offset;
	    row < data->total_rows && row < data->offset + visible_rows;
	    row++) {
		for (col = 0; col < data->columns; col++) {
			idx = row * data->columns + col;
			if (idx >= data->nitems)
				break;

			px = col * cw;
			py = (row - data->offset) * ch;

			/* Find target screen and name. */
			name = NULL;
			target = NULL;
			sess = session_find_by_id(
			    data->items[idx].session_id);
			if (sess == NULL)
				continue;

			if (data->items[idx].winlink_idx == -1) {
				/* Session mode. */
				name = sess->name;
				if (sess->curw != NULL &&
				    sess->curw->window != NULL &&
				    sess->curw->window->active != NULL)
					target = &sess->curw->window
					    ->active->base;
			} else {
				/* Window mode. */
				wl = winlink_find_by_index(
				    &sess->windows,
				    data->items[idx].winlink_idx);
				if (wl != NULL && wl->window != NULL) {
					name = wl->window->name;
					if (wl->window->active != NULL)
						target = &wl->window
						    ->active->base;
				}
			}
			if (name == NULL)
				name = "(dead)";

			/* Draw box. */
			screen_write_cursormove(&ctx, px, py, 0);
			if (row == data->cy && col == data->cx)
				screen_write_box(&ctx, cw, ch,
				    BOX_LINES_DEFAULT, &sel_gc, name);
			else
				screen_write_box(&ctx, cw, ch,
				    BOX_LINES_DEFAULT, &gc, name);

			/* Draw preview inside box. */
			if (target != NULL && cw > 2 && ch > 2) {
				screen_write_cursormove(&ctx,
				    px + 1, py + 1, 0);
				screen_write_preview(&ctx, target,
				    cw - 2, ch - 2);
			}
		}
	}

	screen_write_stop(&ctx);
}

static void
window_grid_select(struct window_mode_entry *wme, struct client *c)
{
	struct window_grid_mode_data	*data = wme->data;
	u_int				 idx;
	struct session			*s;
	struct winlink			*wl;

	idx = data->cy * data->columns + data->cx;
	if (idx >= data->nitems)
		return;

	s = session_find_by_id(data->items[idx].session_id);
	if (s == NULL || !session_alive(s))
		return;

	if (data->type == WINDOW_GRID_SESSIONS) {
		if (c != NULL)
			server_client_set_session(c, s);
	} else {
		wl = winlink_find_by_index(&s->windows,
		    data->items[idx].winlink_idx);
		if (wl != NULL)
			session_select(s, wl->idx);
	}

	window_pane_reset_mode(wme->wp);
}

static struct screen *
window_grid_init(struct window_mode_entry *wme,
    __unused struct cmd_find_state *fs, struct args *args)
{
	struct window_pane		*wp = wme->wp;
	struct window_grid_mode_data	*data;
	struct screen			*s;

	wme->data = data = xcalloc(1, sizeof *data);

	if (args != NULL && args_has(args, 'w'))
		data->type = WINDOW_GRID_WINDOWS;
	else
		data->type = WINDOW_GRID_SESSIONS;

	data->items = NULL;
	data->nitems = 0;
	data->cx = 0;
	data->cy = 0;
	data->offset = 0;

	s = &data->screen;
	screen_init(s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	s->mode &= ~MODE_CURSOR;

	window_grid_build_items(wme);
	window_grid_compute_layout(wme);
	window_grid_draw_screen(wme);

	evtimer_set(&data->timer, window_grid_timer_callback, wme);
	window_grid_start_timer(wme);

	return (s);
}

static void
window_grid_free(struct window_mode_entry *wme)
{
	struct window_grid_mode_data	*data = wme->data;

	evtimer_del(&data->timer);
	free(data->items);
	screen_free(&data->screen);
	free(data);
}

static void
window_grid_resize(struct window_mode_entry *wme, u_int sx, u_int sy)
{
	struct window_grid_mode_data	*data = wme->data;
	struct screen			*s = &data->screen;

	screen_resize(s, sx, sy, 0);
	window_grid_compute_layout(wme);
	window_grid_draw_screen(wme);
}

static void
window_grid_key(struct window_mode_entry *wme, struct client *c,
    __unused struct session *s, __unused struct winlink *wl,
    key_code key, __unused struct mouse_event *m)
{
	struct window_grid_mode_data	*data = wme->data;
	u_int				 visible_rows;

	if (data->nitems == 0 || data->columns == 0) {
		switch (key) {
		case 'q':
		case '\033':	/* Escape */
			window_pane_reset_mode(wme->wp);
			break;
		}
		return;
	}

	visible_rows = screen_size_y(&data->screen) / data->cell_h;
	if (visible_rows == 0)
		visible_rows = 1;

	switch (key) {
	case KEYC_LEFT:
	case 'h':
		if (data->cx > 0)
			data->cx--;
		break;
	case KEYC_RIGHT:
	case 'l':
		if (data->cx + 1 < data->columns) {
			u_int next = data->cy * data->columns + data->cx + 1;
			if (next < data->nitems)
				data->cx++;
		}
		break;
	case KEYC_UP:
	case 'k':
		if (data->cy > 0)
			data->cy--;
		break;
	case KEYC_DOWN:
	case 'j':
		if (data->cy + 1 < data->total_rows) {
			u_int next = (data->cy + 1) * data->columns + data->cx;
			if (next < data->nitems)
				data->cy++;
		}
		break;
	case KEYC_PPAGE:
		if (data->cy >= visible_rows)
			data->cy -= visible_rows;
		else
			data->cy = 0;
		break;
	case KEYC_NPAGE:
		data->cy += visible_rows;
		if (data->cy >= data->total_rows)
			data->cy = data->total_rows - 1;
		/* Clamp cx if last row is partial. */
		{
			u_int idx = data->cy * data->columns + data->cx;
			if (idx >= data->nitems) {
				idx = data->nitems - 1;
				data->cx = idx % data->columns;
				data->cy = idx / data->columns;
			}
		}
		break;
	case '\r':	/* Enter */
		window_grid_select(wme, c);
		return;
	case 'q':
	case '\033':	/* Escape */
		window_pane_reset_mode(wme->wp);
		return;
	default:
		return;
	}

	window_grid_draw_screen(wme);
	wme->wp->flags |= PANE_REDRAW;
}
