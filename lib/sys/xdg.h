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

#ifndef YUKINO_SYS_XDG_H_
#define YUKINO_SYS_XDG_H_

#include "yukino.h"

#include <dbus/dbus.h>

/* This shit is for the xdg screenshot portal. Because wayland doesn't provide
 * this functionality automatically we are forced to basically figure out what
 * works, and this happens to be somewhat standard. */

struct yukino_xdg {
	DBusConnection *conn;
};

/* This works, but it's fairly inefficient as it takes a screenshot ;). */
yukino_result_t yukino_xdg_screenshot(struct yukino_xdg *xdg,
	yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
yukino_result_t yukino_xdg_screenshot_resolution(
	struct yukino_xdg *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h);
yukino_result_t yukino_xdg_screenshot_read(
	struct yukino_xdg *conn, yukino_screenshot_t *s, unsigned char rgb[3]);
yukino_result_t yukino_xdg_screenshot_delete(
	struct yukino_xdg *conn, yukino_screenshot_t *s);
yukino_result_t yukino_xdg_init(struct yukino_xdg *xdg);
void yukino_xdg_quit(struct yukino_xdg *xdg);

#endif
