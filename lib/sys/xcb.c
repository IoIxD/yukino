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

#include <limits.h>
#include <stdlib.h>
#include <xcb/xcb.h>

enum {
	ATOM_NET_CLIENT_LIST_STACKING,
	ATOM_NET_FRAME_EXTENTS,

	/* array bounds */
	ATOM_MAX_,
};

static struct {
	const char *name;
	size_t len;
} atom_names[ATOM_MAX_] = {
#define NAME(x) {x, sizeof(x) - 1}
	NAME("_NET_CLIENT_LIST_STACKING"),
	NAME("_NET_FRAME_EXTENTS"),
#undef NAME
};

/* Define data specific to this connection */
struct yukino_connection_data {
	xcb_connection_t *conn;
	int default_display;
	xcb_screen_t *default_display_screen; /* cache this */

	/* :) */
	xcb_atom_t atoms[ATOM_MAX_];
};
#define YUKINO_CONNECTION_DATA 1
#include "../yukino_c.h"

/* xcb helper functions -- dont mind this */

static xcb_format_t *format_by_depth(const xcb_setup_t *setup, uint8_t depth)
{
	xcb_format_iterator_t fmt = xcb_setup_pixmap_formats_iterator(setup);

	for (; fmt.rem; xcb_format_next(&fmt))
		if (fmt.data->depth == depth)
			return fmt.data;

	return NULL;
}

/* teehee */
static xcb_screen_t *screen_of_display(xcb_connection_t *c, int screen)
{
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; screen--, xcb_screen_next(&iter))
		if (screen == 0)
			return iter.data;

	return NULL;
}

static xcb_visualtype_t *find_visual_by_id(
	xcb_screen_t *screen, xcb_visualid_t visid)
{
	/* Horrible */
	xcb_depth_iterator_t depth_iter
		= xcb_screen_allowed_depths_iterator(screen);

	for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
		xcb_visualtype_iterator_t visual_iter
			= xcb_depth_visuals_iterator(depth_iter.data);
		for (; visual_iter.rem; xcb_visualtype_next(&visual_iter))
			if (visid == visual_iter.data->visual_id)
				return visual_iter.data;
	}

	return NULL;
}

static xcb_visualtype_t *find_visual_for_window(
	yukino_connection_t *conn, xcb_window_t win)
{
	xcb_get_window_attributes_cookie_t cookie;
	xcb_get_window_attributes_reply_t *reply;
	xcb_visualtype_t *r;

	cookie = xcb_get_window_attributes(conn->conn_data.conn, win);
	reply = xcb_get_window_attributes_reply(
		conn->conn_data.conn, cookie, NULL);

	if (!reply)
		return NULL;

	r = find_visual_by_id(
		conn->conn_data.default_display_screen, reply->visual);

	free(reply);

	return r;
}

static yukino_result_t yukino_xcb_disconnect(yukino_connection_t *conn)
{
	xcb_disconnect(conn->conn_data.conn);
	free(conn);

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	xcb_screen_t *scr = conn->conn_data.default_display_screen;

	/* Huh? */
	if (!scr)
		return YUKINO_RESULT_UNSUPPORTED;

	*w = scr->width_in_pixels;
	*h = scr->height_in_pixels;

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

struct yukino_window_iter {
	enum { WITER_QUERY_TREE, WITER_NET_CLIENT_LIST } type;

	union {
		struct {
			xcb_query_tree_cookie_t cookie;
			xcb_query_tree_reply_t *reply;
		} qtree;
		struct {
			xcb_get_property_cookie_t cookie;
			xcb_get_property_reply_t *reply;
		} nlist;
	} u;

	xcb_window_t *w;
	xcb_get_window_attributes_cookie_t *wacs;
	int wlen;
	int wit;
};

/* Here because we fallback if NET_CLIENT_LIST fails for whatever reason */
static void query_tree(yukino_connection_t *conn, yukino_window_iter_t *wi,
	const yukino_window_t *win)
{
	wi->type = WITER_QUERY_TREE;

	wi->u.qtree.cookie = xcb_query_tree(conn->conn_data.conn,
		win ? *win : conn->conn_data.default_display_screen->root);
}

static yukino_result_t yukino_xcb_window_iter_start(yukino_connection_t *conn,
	const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	yukino_window_iter_t *wi;

	wi = calloc(1, sizeof(*wi));
	if (!wi)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	if (!win && conn->conn_data.atoms[ATOM_NET_CLIENT_LIST_STACKING]) {
		wi->type = WITER_NET_CLIENT_LIST;

		wi->u.nlist.cookie = xcb_get_property(conn->conn_data.conn, 0,
			conn->conn_data.default_display_screen->root,
			conn->conn_data.atoms[ATOM_NET_CLIENT_LIST_STACKING],
			XCB_ATOM_ANY, 0L, UINT_MAX);
	} else {
		query_tree(conn, wi, win);
	}

	*pwi = wi;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_window_iter(yukino_connection_t *conn,
	yukino_window_iter_t *wi, yukino_window_t *pw)
{
	if (!wi || !pw)
		return YUKINO_RESULT_INVALID_PARAM;

	if (!wi->w) {
		switch (wi->type) {
		case WITER_NET_CLIENT_LIST:
			wi->u.nlist.reply = xcb_get_property_reply(
				conn->conn_data.conn, wi->u.nlist.cookie, NULL);
			if (wi->u.nlist.reply) {
				wi->w = xcb_get_property_value(
					wi->u.nlist.reply);
				wi->wlen = xcb_get_property_value_length(
						   wi->u.nlist.reply)
					   / sizeof(xcb_window_t);
				break;
			}

			query_tree(conn, wi, NULL);
			/* fallthrough */
		case WITER_QUERY_TREE:
			/* Are you, are you */
			wi->u.qtree.reply = xcb_query_tree_reply(
				conn->conn_data.conn, wi->u.qtree.cookie, NULL);
			if (!wi->u.qtree.reply) /* uh oh */
				return YUKINO_RESULT_UNSUPPORTED;

			wi->w = xcb_query_tree_children(wi->u.qtree.reply);
			wi->wlen = xcb_query_tree_children_length(
				wi->u.qtree.reply);
			break;
		}

		wi->wit = 0;
		wi->wacs = malloc(sizeof(*wi->wacs) * wi->wlen);
		if (wi->wacs) {
			int i;

			/* Could you pay me in advance? */
			for (i = 0; i < wi->wlen; i++)
				wi->wacs[i] = xcb_get_window_attributes(
					conn->conn_data.conn, wi->w[i]);
		}
	}

	for (; wi->wit < wi->wlen; wi->wit++) {
		xcb_get_window_attributes_cookie_t wacookie;
		xcb_get_window_attributes_reply_t *wareply;

		wacookie = (wi->wacs) ? wi->wacs[wi->wit]
				      : xcb_get_window_attributes(
						conn->conn_data.conn,
						wi->w[wi->wit]);

		wareply = xcb_get_window_attributes_reply(
			conn->conn_data.conn, wacookie, NULL);
		if (!wareply)
			continue; /* ??? */

		if (wareply->map_state != XCB_MAP_STATE_VIEWABLE)
			continue;

		*pw = wi->w[wi->wit++];
		return YUKINO_RESULT_OK;
	}

	return YUKINO_RESULT_DONE;
}

static yukino_result_t yukino_xcb_window_iter_end(
	yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	if (!wi)
		return YUKINO_RESULT_INVALID_PARAM;

	free(wi->wacs);

	switch (wi->type) {
	case WITER_QUERY_TREE: free(wi->u.qtree.reply); break;
	case WITER_NET_CLIENT_LIST: free(wi->u.nlist.reply); break;
	}

	free(wi);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* XXX: Need a "batch" API of sorts */

static yukino_result_t yukino_xcb_window_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	xcb_get_geometry_cookie_t cookie;
	xcb_get_geometry_reply_t *reply;
	xcb_translate_coordinates_cookie_t trcookie;
	xcb_translate_coordinates_reply_t *trreply;

	cookie = xcb_get_geometry(conn->conn_data.conn, win);
	trcookie = xcb_translate_coordinates(conn->conn_data.conn, win,
		conn->conn_data.default_display_screen->root, 0, 0);

	reply = xcb_get_geometry_reply(conn->conn_data.conn, cookie, NULL);
	trreply = xcb_translate_coordinates_reply(
		conn->conn_data.conn, trcookie, NULL);
	if (!reply || !trreply) {
		free(reply);
		free(trreply);
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}

	pr->x = trreply->dst_x;
	pr->y = trreply->dst_y;
	pr->w = reply->width;
	pr->h = reply->height;

	free(reply);
	free(trreply);

	return YUKINO_RESULT_OK;
}

#include <stdio.h>

static yukino_result_t yukino_xcb_window_decorated_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	xcb_get_property_cookie_t deccookie;
	xcb_get_property_reply_t *decreply;
	uint32_t *extents;
	yukino_result_t r;

	deccookie = xcb_get_property(conn->conn_data.conn, 0, win,
		conn->conn_data.atoms[ATOM_NET_FRAME_EXTENTS], XCB_ATOM_ANY, 0L,
		UINT_MAX);

	if ((r = yukino_xcb_window_position(conn, win, pr)) < 0)
		return r;

	decreply
		= xcb_get_property_reply(conn->conn_data.conn, deccookie, NULL);
	if (!decreply)
		return YUKINO_RESULT_UNSUPPORTED;

	if (xcb_get_property_value_length(decreply) < (4 * sizeof(uint32_t))) {
		free(decreply);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	extents = xcb_get_property_value(decreply);

	/* add it onto the position of the inner window */
	pr->x -= extents[0];
	pr->y -= extents[2];
	pr->w += extents[0] + extents[1];
	pr->h += extents[2] + extents[3];

	free(decreply);

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

static yukino_result_t yukino_xcb_lock(yukino_connection_t *conn)
{
	xcb_grab_server(conn->conn_data.conn);
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_unlock(yukino_connection_t *conn)
{
	xcb_ungrab_server(conn->conn_data.conn);
	xcb_flush(conn->conn_data.conn);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

static uint32_t read_pixel(
	const uint8_t *p, uint32_t x, uint8_t bpp, unsigned int big_endian)
{
	uint32_t pxl;

	/* shift it to the right place */
	p += (x * bpp) >> 3;

	if (bpp == 4) {
		pxl = ((x & 1) != big_endian) ? (*p & 0xf) : (*p >> 4);
	} else if (big_endian) {
		pxl = 0;

		switch (bpp) {
		case 32: pxl |= *p++; pxl <<= 8;
		case 24: pxl |= *p++; pxl <<= 8;
		case 16: pxl |= *p++; pxl <<= 8;
		case 8: pxl |= *p; break;
		}
	} else {
		pxl = 0;

		switch (bpp) {
		case 32: pxl |= p[3]; pxl <<= 8;
		case 24: pxl |= p[2]; pxl <<= 8;
		case 16: pxl |= p[1]; pxl <<= 8;
		case 8: pxl |= p[0]; break;
		}
	}

	return pxl;
}

static yukino_result_t yukino_xcb_take_window(yukino_connection_t *conn,
	yukino_window_t win, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_pixel_proc_t pixel_func, void *userdata)
{
	xcb_get_image_cookie_t cookie;
	xcb_get_image_reply_t *reply;
	uint8_t *data;
	uint8_t bpp;
	unsigned int big_endian;
	size_t stride;
	/* :) */
	uint32_t red_mask, green_mask, blue_mask;
	uint32_t red_shift, green_shift, blue_shift;
	uint32_t red_div, green_div, blue_div;

	{
		xcb_visualtype_t *vistype;

		/* meh */
		vistype = find_visual_for_window(conn, win);
		if (!vistype)
			return YUKINO_RESULT_UNSUPPORTED;

#define FILL(color) \
	do { \
		color##_mask = vistype->color##_mask; \
		color##_shift = yukino_ctz32(color##_mask); \
		color##_div = color##_mask >> color##_shift; \
	} while (0)

		FILL(red);
		FILL(green);
		FILL(blue);
#undef FILL
	}

	cookie = xcb_get_image(conn->conn_data.conn, XCB_IMAGE_FORMAT_Z_PIXMAP,
		win, x, y, w, h, 0xFFFFFFFF);

	reply = xcb_get_image_reply(conn->conn_data.conn, cookie, NULL);
	if (!reply)
		return YUKINO_RESULT_UNSUPPORTED;

	data = xcb_get_image_data(reply);

	{
		const xcb_setup_t *setup;
		const xcb_format_t *fmt;

		setup = xcb_get_setup(conn->conn_data.conn);
		big_endian = setup->bitmap_format_bit_order;

		fmt = format_by_depth(setup, reply->depth);

		bpp = fmt->bits_per_pixel;

		/* calculate stride */
		stride = w * bpp;
		stride = stride + (stride % fmt->scanline_pad);
		stride >>= 3;

		if ((bpp > 32)
			|| (xcb_get_image_data_length(reply) != (h * stride))) {
			/* something is horribly wrong */
			free(reply);
			return YUKINO_RESULT_UNSUPPORTED;
		}
	}

	/* note: reusing function args here as iterators */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			yukino_result_t r;
			uint32_t pxl;
			unsigned char rgb[3];

			pxl = read_pixel(data, x, bpp, big_endian);

#define SCALE(x, color) \
	((((x) & color##_mask) >> color##_shift) * 255 / color##_div)
			rgb[0] = SCALE(pxl, red);
			rgb[1] = SCALE(pxl, green);
			rgb[2] = SCALE(pxl, blue);
#undef SCALE

			if ((r = pixel_func(userdata, rgb)) < 0) {
				free(reply);
				return r;
			}
		}
		data += stride;
	}

	free(reply);

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_take(yukino_connection_t *conn, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func,
	void *userdata)
{
	return yukino_xcb_take_window(conn,
		conn->conn_data.default_display_screen->root, x, y, w, h,
		pixel_func, userdata);
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_xcb_connect(yukino_connection_t **pconn)
{
	yukino_connection_t *conn;
	xcb_intern_atom_cookie_t atom_cookies[ATOM_MAX_];
	int i;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	conn->conn_data.conn
		= xcb_connect(NULL, &conn->conn_data.default_display);
	if (!conn->conn_data.conn) {
		free(conn);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	for (i = 0; i < ATOM_MAX_; i++)
		atom_cookies[i] = xcb_intern_atom(conn->conn_data.conn, 1,
			atom_names[i].len, atom_names[i].name);

	/* Cache this at startup */
	conn->conn_data.default_display_screen = screen_of_display(
		conn->conn_data.conn, conn->conn_data.default_display);

	/* Fill the vtable */
	conn->disconnect = yukino_xcb_disconnect;
	conn->display_resolution = yukino_xcb_display_resolution;

	conn->window_iter_start = yukino_xcb_window_iter_start;
	conn->window_iter = yukino_xcb_window_iter;
	conn->window_iter_end = yukino_xcb_window_iter_end;

	conn->window_position = yukino_xcb_window_position;
	conn->window_decorated_position = yukino_xcb_window_decorated_position;

	conn->lock = yukino_xcb_lock;
	conn->unlock = yukino_xcb_unlock;

	conn->take = yukino_xcb_take;

	for (i = 0; i < ATOM_MAX_; i++) {
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
			conn->conn_data.conn, atom_cookies[i], NULL);

		conn->conn_data.atoms[i]
			= (reply) ? reply->atom : XCB_ATOM_NONE;
	}

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
