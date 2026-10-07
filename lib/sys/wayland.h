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

#ifndef YUKINO_SYS_WAYLAND_H_
#define YUKINO_SYS_WAYLAND_H_

#include "yukino.h"

#include <wayland-client.h>

/* Common function definitions for Wayland backends */
struct yukino_wayland {
	struct wl_display *display;
};

yukino_result_t yukino_wayland_display_resolution(
	struct yukino_wayland *wl, uint32_t *w, uint32_t *h);
yukino_result_t yukino_wayland_init(struct yukino_wayland *wl);
void yukino_wayland_quit(struct yukino_wayland *wl);

#endif /* YUKINO_SYS_WAYLAND_H_ */
