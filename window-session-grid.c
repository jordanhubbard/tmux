/* $OpenBSD$ */

/*
 * Copyright (c) 2026 tmux contributors
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

static struct screen	*window_session_grid_init(struct window_mode_entry *,
			     struct cmd_find_state *, struct args *);
static void		 window_session_grid_free(struct window_mode_entry *);
static void		 window_session_grid_resize(struct window_mode_entry *,
			     u_int, u_int);
static void		 window_session_grid_update(struct window_mode_entry *);
static void		 window_session_grid_key(struct window_mode_entry *,
			     struct client *, struct session *,
			     struct winlink *, key_code, struct mouse_event *);

static void		 window_session_grid_draw(struct window_mode_entry *);
static void		 window_session_grid_build(struct window_mode_entry *);
static void		 window_session_grid_timer_callback(int, short, void *);

const struct window_mode window_session_grid_mode = {
	.name = "session-grid-mode",

	.init = window_session_grid_init,
	.free = window_session_grid_free,
	.resize = window_session_grid_resize,
	.update = window_session_grid_update,
	.key = window_session_grid_key,
};

#define SESSION_GRID_MAX 64

struct window_session_grid_data {
	struct screen	 screen;
	struct event	 timer;

	u_int		 selected;	/* index into session_ids[] */
	u_int		 nsessions;
	u_int		 session_ids[SESSION_GRID_MAX];

	/* Grid geometry. */
	u_int		 cols;
	u_int		 rows;
	u_int		 cellw;
	u_int		 cellh;
};

/*
 * Snapshot current sessions into the data arrays.  Called on init, update, and
 * when the session list may have changed.
 */
static void
window_session_grid_build(struct window_mode_entry *wme)
{
	struct window_session_grid_data	*data = wme->data;
	struct session			*s;
	u_int				 n;

	n = 0;
	RB_FOREACH(s, sessions, &sessions) {
		if (!session_alive(s))
			continue;
		if (n >= SESSION_GRID_MAX)
			break;
		data->session_ids[n++] = s->id;
	}
	data->nsessions = n;

	/* Clamp selection. */
	if (data->nsessions == 0)
		data->selected = 0;
	else if (data->selected >= data->nsessions)
		data->selected = data->nsessions - 1;
}

/*
 * Compute grid geometry from screen size and session count.
 * Uses the same algorithm as layout_set_tiled().
 */
static void
window_session_grid_compute(struct window_session_grid_data *data, u_int sx,
    u_int sy)
{
	u_int	n, rows, cols;

	n = data->nsessions;
	if (n == 0) {
		data->cols = data->rows = 1;
		data->cellw = sx;
		data->cellh = sy;
		return;
	}

	/* Compute rows and columns (same as layout_set_tiled). */
	rows = cols = 1;
	while (rows * cols < n) {
		rows++;
		if (rows * cols < n)
			cols++;
	}

	data->cols = cols;
	data->rows = rows;
	data->cellw = sx / cols;
	data->cellh = sy / rows;

	/* Enforce minimums so we don't crash on tiny screens. */
	if (data->cellw < 4)
		data->cellw = 4;
	if (data->cellh < 3)
		data->cellh = 3;
}

/*
 * Draw the session grid: bordered cells with session name labels and live
 * preview of each session's active pane.
 */
static void
window_session_grid_draw(struct window_mode_entry *wme)
{
	struct window_session_grid_data	*data = wme->data;
	struct screen			*s = &data->screen;
	struct screen_write_ctx		 ctx;
	struct session			*sess;
	struct grid_cell		 gc;
	u_int				 i, r, c, x, y;
	u_int				 sx, sy;
	char				*label;

	sx = screen_size_x(s);
	sy = screen_size_y(s);

	window_session_grid_compute(data, sx, sy);

	screen_write_start(&ctx, s);
	screen_write_clearscreen(&ctx, 8);

	for (i = 0; i < data->nsessions; i++) {
		sess = session_find_by_id(data->session_ids[i]);
		if (sess == NULL)
			continue;

		r = i / data->cols;
		c = i % data->cols;
		x = c * data->cellw;
		y = r * data->cellh;

		/*
		 * For the selected cell, draw with reversed border to
		 * highlight it.
		 */
		memcpy(&gc, &grid_default_cell, sizeof gc);
		gc.flags |= GRID_FLAG_NOPALETTE;
		if (i == data->selected)
			gc.attr |= GRID_ATTR_REVERSE;

		/* Draw bordered box. */
		screen_write_cursormove(&ctx, x, y, 0);
		screen_write_box(&ctx, data->cellw, data->cellh,
		    BOX_LINES_DEFAULT, &gc, NULL);

		/* Draw session name label centered in top border. */
		xasprintf(&label, " %s ", sess->name);
		if (strlen(label) > data->cellw - 2) {
			free(label);
			xasprintf(&label, " %.*s.. ",
			    (int)(data->cellw - 6), sess->name);
		}

		memcpy(&gc, &grid_default_cell, sizeof gc);
		gc.flags |= GRID_FLAG_NOPALETTE;
		if (i == data->selected)
			gc.attr |= GRID_ATTR_REVERSE;

		{
			u_int labelx;
			u_int labellen = (u_int)strlen(label);
			if (labellen < data->cellw - 2)
				labelx = x + (data->cellw - labellen) / 2;
			else
				labelx = x + 1;
			screen_write_cursormove(&ctx, labelx, y, 0);
			screen_write_puts(&ctx, &gc, "%s", label);
		}
		free(label);

		/* Render preview of session's active pane inside the cell. */
		if (data->cellw > 2 && data->cellh > 2) {
			u_int pw = data->cellw - 2;
			u_int ph = data->cellh - 2;
			struct window_pane *wp;

			wp = sess->curw->window->active;
			screen_write_cursormove(&ctx, x + 1, y + 1, 0);
			screen_write_preview(&ctx, &wp->base, pw, ph);
		}
	}

	screen_write_stop(&ctx);
}

static void
window_session_grid_start_timer(struct window_mode_entry *wme)
{
	struct window_session_grid_data	*data = wme->data;
	struct timeval			 tv = { .tv_sec = 1 };

	evtimer_add(&data->timer, &tv);
}

static void
window_session_grid_timer_callback(__unused int fd, __unused short events,
    void *arg)
{
	struct window_mode_entry		*wme = arg;
	struct window_session_grid_data	*data = wme->data;

	evtimer_del(&data->timer);

	window_session_grid_build(wme);
	window_session_grid_draw(wme);
	wme->wp->flags |= PANE_REDRAW;

	window_session_grid_start_timer(wme);
}

static struct screen *
window_session_grid_init(struct window_mode_entry *wme,
    __unused struct cmd_find_state *fs, __unused struct args *args)
{
	struct window_pane			*wp = wme->wp;
	struct window_session_grid_data		*data;
	struct screen				*s;

	wme->data = data = xcalloc(1, sizeof *data);

	evtimer_set(&data->timer, window_session_grid_timer_callback, wme);

	s = &data->screen;
	screen_init(s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	s->mode &= ~MODE_CURSOR;

	window_session_grid_build(wme);
	window_session_grid_draw(wme);

	window_session_grid_start_timer(wme);

	return (s);
}

static void
window_session_grid_free(struct window_mode_entry *wme)
{
	struct window_session_grid_data	*data = wme->data;

	evtimer_del(&data->timer);
	screen_free(&data->screen);
	free(data);
}

static void
window_session_grid_resize(struct window_mode_entry *wme, u_int sx, u_int sy)
{
	struct window_session_grid_data	*data = wme->data;
	struct screen			*s = &data->screen;

	screen_resize(s, sx, sy, 0);
	window_session_grid_draw(wme);
}

static void
window_session_grid_update(struct window_mode_entry *wme)
{
	/* Re-snapshot session list in case sessions were created/destroyed. */
	window_session_grid_build(wme);
	window_session_grid_draw(wme);
	wme->wp->flags |= PANE_REDRAW;
}

static void
window_session_grid_key(struct window_mode_entry *wme, struct client *c,
    __unused struct session *s, __unused struct winlink *wl, key_code key,
    struct mouse_event *m)
{
	struct window_session_grid_data	*data = wme->data;
	struct session			*target;
	u_int				 old_selected;

	old_selected = data->selected;

	switch (key) {
	case 'q':
	case '\033': /* Escape */
		window_pane_reset_mode(wme->wp);
		return;
	case KEYC_LEFT:
	case 'h':
		if (data->selected > 0)
			data->selected--;
		break;
	case KEYC_RIGHT:
	case 'l':
		if (data->selected + 1 < data->nsessions)
			data->selected++;
		break;
	case KEYC_UP:
	case 'k':
		if (data->selected >= data->cols)
			data->selected -= data->cols;
		break;
	case KEYC_DOWN:
	case 'j':
		if (data->selected + data->cols < data->nsessions)
			data->selected += data->cols;
		break;
	case '(':
		if (data->selected > 0)
			data->selected--;
		else if (data->nsessions > 0)
			data->selected = data->nsessions - 1;
		break;
	case ')':
		if (data->nsessions > 0)
			data->selected = (data->selected + 1) % data->nsessions;
		break;
	case '\r': /* Enter */
		if (data->nsessions == 0)
			break;
		target = session_find_by_id(
		    data->session_ids[data->selected]);
		if (target != NULL && c != NULL) {
			window_pane_reset_mode(wme->wp);
			server_client_set_session(c, target);
			return;
		}
		break;
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		{
			u_int idx = key - '0';
			if (idx < data->nsessions)
				data->selected = idx;
		}
		break;
	case KEYC_MOUSEDOWN1_PANE:
		if (m != NULL && m->valid && data->cellw > 0 &&
		    data->cellh > 0) {
			u_int mc = m->x / data->cellw;
			u_int mr = m->y / data->cellh;
			u_int idx = mr * data->cols + mc;
			if (idx < data->nsessions)
				data->selected = idx;
		}
		break;
	case KEYC_DOUBLECLICK1_PANE:
		if (m != NULL && m->valid && data->cellw > 0 &&
		    data->cellh > 0) {
			u_int mc = m->x / data->cellw;
			u_int mr = m->y / data->cellh;
			u_int idx = mr * data->cols + mc;
			if (idx < data->nsessions) {
				data->selected = idx;
				target = session_find_by_id(
				    data->session_ids[idx]);
				if (target != NULL && c != NULL) {
					window_pane_reset_mode(wme->wp);
					server_client_set_session(c, target);
					return;
				}
			}
		}
		break;
	default:
		return;
	}

	if (data->selected != old_selected) {
		window_session_grid_draw(wme);
		wme->wp->flags |= PANE_REDRAW;
	}
}
