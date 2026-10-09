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

#ifndef YUKINO_SYS_WLR_H_
#define YUKINO_SYS_WLR_H_

#include "yukino.h"

#include "sys/wayland.h"

#include "wlr-screencopy-unstable-v1.h"

/* God bless anyone who has the wlr screenshot protocol and not dbus shit */

struct yukino_wlr {
	struct yukino_wayland *wl;
	struct zwlr_screencopy_manager_v1 *screencopy;
	struct yukino_wlr_display **outputs;
	int num_outputs;
	struct wl_shm *shm;
	struct zxdg_output_manager_v1 *om;
};

yukino_result_t yukino_wlr_screenshot(struct yukino_wlr *conn,
	yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
yukino_result_t yukino_wlr_screenshot_resolution(
	struct yukino_wlr *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h);
yukino_result_t yukino_wlr_screenshot_read(
	struct yukino_wlr *conn, yukino_screenshot_t *s, unsigned char rgb[3]);
yukino_result_t yukino_wlr_screenshot_delete(
	struct yukino_wlr *conn, yukino_screenshot_t *s);
yukino_result_t yukino_wlr_init(
	struct yukino_wlr *wlr, struct yukino_wayland *wl);
void yukino_wlr_quit(struct yukino_wlr *wlr, struct yukino_wayland *wl);
yukino_result_t yukino_wlr_display_resolution(
	struct yukino_wlr *conn, uint32_t *w, uint32_t *h);

#endif
