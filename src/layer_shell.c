/*
 * yukino -- portable screenshot utility
 * Copyright (C) 2026 Paper
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "yukino.h"
#include "layer_shell.h"

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-output-unstable-v1.h"

/* ------------------------------------------------------------------------ */
/* turn a roleless SDL wayland surface into a wlr layer surface */

struct output {
	struct wl_output *output;
	struct zxdg_output_v1 *xdg_output;
	uint32_t version;

	/* logical position and size in the global compositor space */
	int32_t x, y, w, h;
};

/* stinky global var */
static struct {
	struct output *outputs;
	size_t outputs_size;
	struct zxdg_output_manager_v1 *om;

	struct wl_display *display;
	struct wl_registry *registry;
	struct zwlr_layer_shell_v1 *shell;
	struct zwlr_layer_surface_v1 *layer;

	SDL_Window *win;
	int configured;
} ls;

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer,
	uint32_t serial, uint32_t w, uint32_t h)
{
	zwlr_layer_surface_v1_ack_configure(layer, serial);

	/* custom surfaces get no size notifications, so tell SDL ourselves.
	 * the window is high-dpi, so this is the logical size */
	if (w && h)
		SDL_SetWindowSize(ls.win, w, h);

	ls.configured = 1;
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer)
{
	/* compositor wants us gone; treat it like escape */
	SDL_Event ev;

	SDL_zero(ev);
	ev.type = SDL_EVENT_KEY_DOWN;
	ev.key.key = SDLK_ESCAPE;
	SDL_PushEvent(&ev);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

/* ------------------------------------------------------------------------ */
/* outputs; we only care where they are */

static void xdg_output_logical_position(
	void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y)
{
	struct output *o = &ls.outputs[(size_t)(uintptr_t)data];

	o->x = x;
	o->y = y;
}

static void xdg_output_logical_size(
	void *data, struct zxdg_output_v1 *xdg_output, int32_t w, int32_t h)
{
	struct output *o = &ls.outputs[(size_t)(uintptr_t)data];

	o->w = w;
	o->h = h;
}

static void xdg_output_done(void *data, struct zxdg_output_v1 *xdg_output)
{
}

static void xdg_output_name(
	void *data, struct zxdg_output_v1 *xdg_output, const char *name)
{
}

static void xdg_output_description(
	void *data, struct zxdg_output_v1 *xdg_output, const char *desc)
{
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xdg_output_logical_position,
	.logical_size = xdg_output_logical_size,
	.done = xdg_output_done,
	.name = xdg_output_name,
	.description = xdg_output_description,
};

static int get_desktop_rect_in_points(struct output *o, size_t os, yukino_rect_t *r)
{
	size_t i;
	int32_t x0, x1, y0, y1;

	if (!os) return -1;

	x0 = y0 = INT32_MAX;
	x1 = y1 = INT32_MIN;
	for (i = 0; i < os; i++) {
		/* f'(x) */
		int32_t dx0, dx1, dy0, dy1;

		dx0 = o[i].x;
		dy0 = o[i].y;
		dx1 = dx0 + o[i].w;
		dy1 = dy0 + o[i].h;

		if (x0 > dx0) x0 = dx0;
		if (x1 < dx1) x1 = dx1;
		if (y0 > dy0) y0 = dy0;
		if (y1 < dy1) y1 = dy1;
	}

	r->x = x0;
	r->y = y0;
	r->w = x1 - x0;
	r->h = y1 - y0;

	return 0;
}

static int check_monitor_in_corner(struct output *o,
	const yukino_rect_t *desk_rect, uint32_t *flags)
{
	int32_t x0, x1, y0, y1, dx0, dx1, dy0, dy1;
	uint32_t i, f;

	x0 = o->x;
	x1 = x0 + o->w;
	y0 = o->y;
	y1 = y0 + o->h;
	dx0 = desk_rect->x;
	dx1 = dx0 + desk_rect->w;
	dy0 = desk_rect->y;
	dy1 = dy0 + desk_rect->h;

	i = f = 0;
#define CHECK(coord, pos) \
do { \
	if (coord == d##coord) { \
		f |= ZWLR_LAYER_SURFACE_V1_ANCHOR_##pos; \
		i++; \
	} \
} while (0)
	CHECK(x0, LEFT);
	CHECK(x1, RIGHT);
	CHECK(y0, TOP);
	CHECK(y1, BOTTOM);
#undef CHECK

	if (i < 2)
		return -1; /* Fail */

	*flags = f;
	return 0;
}

/* returns the output whose top left corner is closest to (0,0) */
static struct wl_output *output_in_corner(uint32_t *f)
{
	yukino_rect_t desk_res;
	size_t i;

	if (get_desktop_rect_in_points(ls.outputs, ls.outputs_size, &desk_res) < 0)
		return NULL;

	for (i = 0; i < ls.outputs_size; i++)
		if (check_monitor_in_corner(ls.outputs + i, &desk_res, f) == 0)
			return ls.outputs[i].output;

	return NULL;
}

/* ------------------------------------------------------------------------ */

static void registry_global(void *data, struct wl_registry *registry,
	uint32_t id, const char *interface, uint32_t version)
{
	if (SDL_strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		if (version > 4)
			version = 4;
		ls.shell = wl_registry_bind(
			registry, id, &zwlr_layer_shell_v1_interface, version);
	} else if (SDL_strcmp(interface, wl_output_interface.name) == 0) {
		struct output *o;
		void *old = ls.outputs;

		ls.outputs = realloc(ls.outputs,
			(ls.outputs_size + 1) * sizeof(*ls.outputs));
		if (!ls.outputs) {
			ls.outputs = old;
			return;
		}

		if (version > 3)
			version = 3;

		o = &ls.outputs[ls.outputs_size];
		o->x = o->y = o->w = o->h = 0;
		o->xdg_output = NULL;
		o->version = version;
		o->output = wl_registry_bind(
			registry, id, &wl_output_interface, version);

		ls.outputs_size++;
	} else if (SDL_strcmp(interface, zxdg_output_manager_v1_interface.name)
		   == 0) {
		if (version > 3)
			version = 3;
		ls.om = wl_registry_bind(registry, id,
			&zxdg_output_manager_v1_interface, version);
	}
}

static void registry_global_remove(
	void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

int layer_shell_available(void)
{
	return SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0;
}

int layer_shell_attach(SDL_Window *win, int w, int h)
{
	SDL_PropertiesID props = SDL_GetWindowProperties(win);
	struct wl_surface *surface;
	struct wl_output *output;
	uint32_t flags;

	ls.win = win;
	ls.display = SDL_GetPointerProperty(
		props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
	surface = SDL_GetPointerProperty(
		props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
	if (!ls.display || !surface)
		return -1;

	ls.registry = wl_display_get_registry(ls.display);
	wl_registry_add_listener(ls.registry, &registry_listener, NULL);
	/* first roundtrip gets the globals */
	wl_display_roundtrip(ls.display);

	/* the output manager may be advertised after some outputs, so only
	 * hook up xdg_outputs once we've seen everything */
	if (ls.om) {
		size_t i;

		for (i = 0; i < ls.outputs_size; i++) {
			struct output *o = &ls.outputs[i];

			o->xdg_output = zxdg_output_manager_v1_get_xdg_output(
				ls.om, o->output);
			/* index rather than pointer, since realloc moves
			 * things */
			zxdg_output_v1_add_listener(
				o->xdg_output, &xdg_output_listener, (void *)(uintptr_t)i);
		}

		/* second roundtrip gets the xdg_output events */
		wl_display_roundtrip(ls.display);
	}

	if (!ls.shell) {
		SDL_Log("compositor doesn't support wlr-layer-shell");
		return -1;
	}

	output = output_in_corner(&flags);
	if (!output) {
		SDL_Log("What kind of fuckery is happening in your display setup?");
		return -1;
	}

	/* map onto whichever display sits at (or nearest) the origin. if
	 * there are no outputs this is NULL, which lets the compositor pick */
	ls.layer = zwlr_layer_shell_v1_get_layer_surface(ls.shell, surface,
		output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		"yukino");
	zwlr_layer_surface_v1_add_listener(ls.layer, &layer_listener, NULL);

	/* pin to the top left corner (0,0), and ignore everyone else's
	 * exclusive zones (panels etc.) so they don't push us around */
	zwlr_layer_surface_v1_set_anchor(ls.layer, flags);
	zwlr_layer_surface_v1_set_margin(ls.layer, 0, 0, 0, 0);
	zwlr_layer_surface_v1_set_size(ls.layer, w, h);
	zwlr_layer_surface_v1_set_exclusive_zone(ls.layer, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(ls.layer,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

	/* initial commit without a buffer, then wait for the configure
	 * before anything gets rendered into the surface */
	wl_surface_commit(surface);
	while (!ls.configured)
		if (wl_display_dispatch(ls.display) < 0)
			return -1;

	return 0;
}

void layer_shell_detach(void)
{
	size_t i;

	for (i = 0; i < ls.outputs_size; i++) {
		if (ls.outputs[i].xdg_output)
			zxdg_output_v1_destroy(ls.outputs[i].xdg_output);
		if (ls.outputs[i].version >= WL_OUTPUT_RELEASE_SINCE_VERSION)
			wl_output_release(ls.outputs[i].output);
		else
			wl_output_destroy(ls.outputs[i].output);
	}
	free(ls.outputs);
	if (ls.om)
		zxdg_output_manager_v1_destroy(ls.om);

	if (ls.layer)
		zwlr_layer_surface_v1_destroy(ls.layer);
	if (ls.shell)
		zwlr_layer_shell_v1_destroy(ls.shell);
	if (ls.registry)
		wl_registry_destroy(ls.registry);
	SDL_zero(ls);
}
