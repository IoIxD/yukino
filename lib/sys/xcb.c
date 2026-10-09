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
	reply = xcb_get_window_attributes_reply(conn->conn_data.conn, cookie, NULL);

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
			conn->conn_data.atoms[ATOM_NET_CLIENT_LIST_STACKING], XCB_ATOM_ANY,
			0L, UINT_MAX);
	} else {
		query_tree(conn, wi, win);
	}

	*pwi = wi;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_window_iter(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *pw)
{
	if (!wi || !pw)
		return YUKINO_RESULT_INVALID_PARAM;

	if (!wi->w) {
		switch (wi->type) {
		case WITER_NET_CLIENT_LIST:
			wi->u.nlist.reply = xcb_get_property_reply(
				conn->conn_data.conn, wi->u.nlist.cookie, NULL);
			if (wi->u.nlist.reply) {
				wi->w = xcb_get_property_value(wi->u.nlist.reply);
				wi->wlen = xcb_get_property_value_length(wi->u.nlist.reply)
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
			wi->wlen = xcb_query_tree_children_length(wi->u.qtree.reply);
			break;
		}

		wi->wit = 0;
		wi->wacs = malloc(sizeof(*wi->wacs) * wi->wlen);
		if (wi->wacs) {
			int i;

			/* Could you pay me in advance? */
			for (i = 0; i < wi->wlen; i++)
				wi->wacs[i]
					= xcb_get_window_attributes(conn->conn_data.conn, wi->w[i]);
		}
	}

	for (; wi->wit < wi->wlen; wi->wit++) {
		xcb_get_window_attributes_cookie_t wacookie;
		xcb_get_window_attributes_reply_t *wareply;

		wacookie = (wi->wacs)
			? wi->wacs[wi->wit]
			: xcb_get_window_attributes(conn->conn_data.conn, wi->w[wi->wit]);

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
	trreply
		= xcb_translate_coordinates_reply(conn->conn_data.conn, trcookie, NULL);
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

	decreply = xcb_get_property_reply(conn->conn_data.conn, deccookie, NULL);
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

struct yukino_screenshot {
	xcb_get_image_cookie_t cookie;
	xcb_get_image_reply_t *reply;

	xcb_visualtype_t *vistype;

	struct yukino_image mbuf;

	uint32_t w, h;
};

static yukino_result_t yukino_xcb_screenshot(yukino_connection_t *conn, yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	yukino_screenshot_t *s;
	yukino_result_t r;
	xcb_window_t win;

	win = conn->conn_data.default_display_screen->root;

	/* wew */
	if ((r = yukino_screenshot_fix_resolution(conn, &w, &h)) < 0)
		return r;

	s = malloc(sizeof(*s));
	if (!s)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	/* Get the cookie first. */
	s->cookie = xcb_get_image(conn->conn_data.conn, XCB_IMAGE_FORMAT_Z_PIXMAP, win,
		x, y, w, h, 0xFFFFFFFF);
	/* We put this off until the first read */
	s->reply = NULL;
	s->w = w;
	s->h = h;

	s->vistype = find_visual_for_window(conn, win);
	if (!s->vistype) {
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	*ps = s;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_screenshot_resolution(yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h)
{
	*w = s->w;
	*h = s->h;
	return YUKINO_RESULT_OK;
}

#include <stdio.h>

static yukino_result_t yukino_xcb_screenshot_read(yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3])
{
	uint8_t *data;
	uint32_t pxl;

	/* Handle getting a reply if we don't have one already */
	if (!s->reply) {
		const xcb_format_t *fmt;
		const xcb_setup_t *setup;
		size_t stride;

		setup = xcb_get_setup(conn->conn_data.conn);

		s->reply = xcb_get_image_reply(conn->conn_data.conn, s->cookie, NULL);
		if (!s->reply)
			return YUKINO_RESULT_UNSUPPORTED;

		fmt = format_by_depth(setup, s->reply->depth);

		/* calculate stride */
		stride = s->w * fmt->bits_per_pixel;
		stride = stride + (stride % fmt->scanline_pad);
		stride >>= 3;

		/* Any way to tell xcb to give up on a request? :) */
		if ((fmt->bits_per_pixel > 32) || (xcb_get_image_data_length(s->reply) != (s->h * stride))) {
			free(s->reply);
			s->reply = NULL;
			return YUKINO_RESULT_UNSUPPORTED;
		}

		/* No need to perform clipping client-side, the server does it for us */
		yukino_image(&s->mbuf, xcb_get_image_data(s->reply), fmt->bits_per_pixel, s->vistype->red_mask, s->vistype->green_mask, s->vistype->blue_mask, setup->bitmap_format_bit_order, stride, 0, 0, s->w, s->h);
	}

	return yukino_image_read(&s->mbuf, rgb);
}

static yukino_result_t yukino_xcb_screenshot_delete(yukino_connection_t *conn, yukino_screenshot_t *s)
{
	free(s->reply);
	free(s);
	return YUKINO_RESULT_OK;
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

	conn->conn_data.conn = xcb_connect(NULL, &conn->conn_data.default_display);
	if (!conn->conn_data.conn) {
		free(conn);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	for (i = 0; i < ATOM_MAX_; i++)
		atom_cookies[i] = xcb_intern_atom(
			conn->conn_data.conn, 1, atom_names[i].len, atom_names[i].name);

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

	conn->screenshot = yukino_xcb_screenshot;
	conn->screenshot_resolution = yukino_xcb_screenshot_resolution;
	conn->screenshot_read = yukino_xcb_screenshot_read;
	conn->screenshot_delete = yukino_xcb_screenshot_delete;

	/* Use impl on top of screenshot */
	conn->take = NULL;

	for (i = 0; i < ATOM_MAX_; i++) {
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
			conn->conn_data.conn, atom_cookies[i], NULL);

		conn->conn_data.atoms[i] = (reply) ? reply->atom : XCB_ATOM_NONE;
	}

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
