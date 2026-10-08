/*
 * libyukino -- portable screenshots
 * Copyright (C) 2026 Paper
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YUKINO_SYS_KWIN_H_
#define YUKINO_SYS_KWIN_H_

#include "yukino.h"

#include <stdint.h>

#include <dbus/dbus.h>

/* Meh */
struct uuid {
	unsigned char c[16];
};

/* Yukino window IDs under KWin are just indexes into this array,
 * since a UUID is too large to fit in a 64-bit integer. */
struct kwin_window {
	/* Geometry with frames */
	float bx, by, bw, bh;

	/* Geometry without frames */
	float x, y, w, h;

	/* UUID */
	struct uuid uuid;
};

struct yukino_kwin {
	DBusConnection *conn;

	/* do it like you do it to me */
	struct kwin_window *w;
	size_t w_size, w_alloc;
};

yukino_result_t yukino_kwin_window_iter_start(struct yukino_kwin *kwi,
	const yukino_window_t *win, yukino_window_iter_t **pwi);
yukino_result_t yukino_kwin_window_iter(
	struct yukino_kwin *kwi, yukino_window_iter_t *wi, yukino_window_t *win);
yukino_result_t yukino_kwin_window_iter_end(
	struct yukino_kwin *kwi, yukino_window_iter_t *wi);

yukino_result_t yukino_kwin_window_position(
	struct yukino_kwin *conn, yukino_window_t win, yukino_rect_t *pr);
yukino_result_t yukino_kwin_window_decorated_position(
	struct yukino_kwin *conn, yukino_window_t win, yukino_rect_t *pr);

yukino_result_t yukino_kwin_init(struct yukino_kwin *kwi);
void yukino_kwin_quit(struct yukino_kwin *kwi);

#endif /* YUKINO_SYS_KWIN_H_ */
