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

#include "yukino.h"
#include "sys/wayland.h"

#include <wayland-client-protocol.h>
#include <xdg-output-unstable-v1.h>

#include <stdlib.h>
#include <string.h>

struct display_r {
	struct wl_output *output;
	struct zxdg_output_v1 *xdg_output;

	yukino_rect_t pixel;
	yukino_rect_t logical;
	// int32_t scale_factor;
	unsigned int has_logical_position : 1;
	unsigned int has_logical_size : 1;
};

struct display_data {
	struct display_r **monitors;
	size_t monitors_size;

	struct zxdg_output_manager_v1 *om;
	struct zwp_linux_dmabuf_v1 *dma;
};

static void handle_xdg_output_logical_position(
	void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y)
{
	struct display_r *d = data;

	d->logical.x = x;
	d->logical.y = y;
	d->has_logical_position = 1;
}

static void handle_xdg_output_logical_size(void *data,
	struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height)
{
	struct display_r *d = data;

	d->logical.w = width;
	d->logical.h = height;
	d->has_logical_size = 1;
}

static void handle_xdg_output_done(
	void *data, struct zxdg_output_v1 *xdg_output)
{
}

static void handle_xdg_output_name(
	void *data, struct zxdg_output_v1 *xdg_output, const char *name)
{
}

static void handle_xdg_output_description(
	void *data, struct zxdg_output_v1 *xdg_output, const char *description)
{
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	handle_xdg_output_logical_position,
	handle_xdg_output_logical_size,
	handle_xdg_output_done,
	handle_xdg_output_name,
	handle_xdg_output_description,
};

// Callback when a monitor sends its geometry data
static void output_handle_geometry(void *data, struct wl_output *wl_output,
	int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
	int32_t subpixel, const char *make, const char *model,
	int32_t transform)
{
	struct display_r *r = data;

	r->pixel.x = x;
	r->pixel.y = y;
}

// Callback when a monitor sends its resolution modes
static void output_handle_mode(void *data, struct wl_output *wl_output,
	uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
	struct display_r *r;

	// We look for the current active resolution mode
	if (!(flags & WL_OUTPUT_MODE_CURRENT))
		return;

	r = data;
	r->pixel.w = width;
	r->pixel.h = height;
}

static void output_handle_done(void *data, struct wl_output *wl_output)
{
}

static void output_handle_scale(
	void *data, struct wl_output *wl_output, int32_t factor)
{
	// struct display_r *r = data;

	// r->scale_factor = factor;
}

// Wire up the wl_output listener
static const struct wl_output_listener output_listener = {
	.geometry = output_handle_geometry,
	.mode = output_handle_mode,
	.done = output_handle_done,
	.scale = output_handle_scale,
};

// Callback to handle global interface registry additions
static void registry_handle_global(void *data, struct wl_registry *registry,
	uint32_t id, const char *interface, uint32_t version)
{
	struct display_data *state = data;

	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct display_r *m;

		state->monitors = realloc(
			state->monitors, sizeof(struct display_r *)
						 * (state->monitors_size + 1));
		m = state->monitors[state->monitors_size]
			= calloc(1, sizeof(struct display_r));

		m->output = wl_registry_bind(
			registry, id, &wl_output_interface, 2);
		wl_output_add_listener(m->output, &output_listener, m);

		m->xdg_output = zxdg_output_manager_v1_get_xdg_output(
			state->om, m->output);
		zxdg_output_v1_add_listener(
			m->xdg_output, &xdg_output_listener, m);

		state->monitors_size++;
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name)
		   == 0) {
		if (version > 3)
			version = 3;
		state->om = wl_registry_bind(registry, id,
			&zxdg_output_manager_v1_interface, version);
	}
}

static void registry_handle_global_remove(
	void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

yukino_result_t yukino_wayland_display_resolution(
	struct yukino_wayland *wl, uint32_t *w, uint32_t *h)
{
	struct display_data dd;
	int32_t minx, miny, maxx, maxy;
	size_t i;
	double scale;

	memset(&dd, 0, sizeof(dd));

	{
		struct wl_registry *registry
			= wl_display_get_registry(wl->display);
		wl_registry_add_listener(registry, &registry_listener, &dd);

		wl_display_roundtrip(wl->display);
		wl_display_roundtrip(wl->display);

		wl_registry_destroy(registry);
		if (dd.om)
			zxdg_output_manager_v1_destroy(dd.om);
		for (i = 0; i < dd.monitors_size; i++) {
			struct display_r *m = dd.monitors[i];

			if (m->xdg_output)
				zxdg_output_v1_destroy(m->xdg_output);
			if (m->output)
				wl_output_destroy(m->output);
		}
	}

	if (dd.monitors_size == 0)
		return YUKINO_RESULT_UNSUPPORTED;

	minx = miny = maxx = maxy = 0;

	scale = 0.0;
	for (i = 0; i < dd.monitors_size; i++) {
		struct display_r *m = dd.monitors[i];
		double s;

		if (!m->has_logical_size)
			continue;

		/* Round to nearest quarter. This sucks but it makes
		 * rounding errors (e.g. 1366 * 1.25) a bit less painful */
		s = round((double)m->pixel.w / m->logical.w * 4) / 4;
		if (scale < s)
			scale = s;
	}

	for (i = 0; i < dd.monitors_size; i++) {
		struct display_r *m = dd.monitors[i];
		int32_t x, y, w, h;

		x = m->logical.x * scale;
		y = m->logical.y * scale;
		w = m->logical.w * scale;
		h = m->logical.h * scale;

		if (x < minx)
			minx = x;
		if (y < miny)
			miny = y;
		if ((x + w) > maxx)
			maxx = x + w;
		if ((y + h) > maxy)
			maxy = y + h;

		free(m);
	}
	free(dd.monitors);

	int32_t total_width = maxx - minx;
	int32_t total_height = maxy - miny;

	*w = total_width;
	*h = total_height;

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_wayland_init(struct yukino_wayland *wl)
{
	memset(wl, 0, sizeof(*wl));

	wl->display = wl_display_connect(NULL);
	if (!wl->display)
		return YUKINO_RESULT_UNSUPPORTED;

	return YUKINO_RESULT_OK;
}

void yukino_wayland_quit(struct yukino_wayland *wl)
{
	if (!wl)
		return;

	if (wl->display)
		wl_display_disconnect(wl->display);
}
