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
#include "yukino_c.h"

#include "sys/xdg.h"

#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <inttypes.h>
#include <unistd.h> /* unlink */

#define TARGET_NONE         0 /* eh */
#define TARGET_SCREEN       1
#define TARGET_WINDOW       2
#define TARGET_AREA         4
#define TARGET_ACTIVEWINDOW 8

static yukino_result_t subscribe(
	struct yukino_xdg *conn, const char *request_path)
{
	DBusError error;
	char *match_rule;
	yukino_result_t r;

	dbus_error_init(&error);

	if (asprintf(&match_rule,
			"type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'",
			request_path)
		< 0) {
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}
	dbus_bus_add_match(conn->conn, match_rule, &error);

	free(match_rule);
	r = (dbus_error_is_set(&error)) ? YUKINO_RESULT_UNSUPPORTED
									: YUKINO_RESULT_OK;
	dbus_error_free(&error);
	return r;
}

static yukino_result_t predict_req_path(
	struct yukino_xdg *conn, const char *token, char **res)
{
	char *req_path;
	const char *uname, *uname_suf;
	yukino_result_t r;

	if (!token)
		return YUKINO_RESULT_UNSUPPORTED;

	uname = dbus_bus_get_unique_name(conn->conn);
	if (!uname || !*uname)
		return YUKINO_RESULT_UNSUPPORTED; /* ??? */
	uname++;

	uname_suf = strchr(uname, '.');
	if (!uname_suf)
		return YUKINO_RESULT_UNSUPPORTED;
	uname_suf++;

	if (asprintf(&req_path,
			"/org/freedesktop/portal/desktop/request/%.*s_%s/%s",
			(int)(uname_suf - uname - 1), uname, uname_suf, token)
		< 0)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	*res = req_path;
	return YUKINO_RESULT_OK;
}

static yukino_result_t send_method(struct yukino_xdg *conn, const char *token,
	uint32_t target, DBusPendingCall **pending)
{
	/* Send it off */
	DBusMessage *msg;
	DBusMessageIter iter, array_iter, dict_iter, variant_iter;

	msg = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
		"/org/freedesktop/portal/desktop", "org.freedesktop.portal.Screenshot",
		"Screenshot");
	if (!msg)
		return YUKINO_RESULT_UNSUPPORTED;

	dbus_message_iter_init_append(msg, &iter);
	{
		static const char *parent_window = "";
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_window);
	}

	dbus_message_iter_open_container(
		&iter, DBUS_TYPE_ARRAY, "{sv}", &array_iter);

	/* This isn't supported on KDE so we only send it if it's
	 * meaningful */
	if (target) {
		static const char *target_key = "target";

		dbus_message_iter_open_container(
			&array_iter, DBUS_TYPE_DICT_ENTRY, NULL, &dict_iter);
		dbus_message_iter_append_basic(
			&dict_iter, DBUS_TYPE_STRING, &target_key);
		dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_VARIANT,
			DBUS_TYPE_UINT32_AS_STRING, &variant_iter);
		dbus_message_iter_append_basic(
			&variant_iter, DBUS_TYPE_UINT32, &target);
		dbus_message_iter_close_container(&dict_iter, &variant_iter);
		dbus_message_iter_close_container(&array_iter, &dict_iter);
	}

	/* {"interactive": false} -- enabled by default, unnecessary */

	/* {"handle_token": token} */
	if (token) {
		static const char *token_key = "handle_token";

		dbus_message_iter_open_container(
			&array_iter, DBUS_TYPE_DICT_ENTRY, NULL, &dict_iter);
		dbus_message_iter_append_basic(
			&dict_iter, DBUS_TYPE_STRING, &token_key);
		dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_VARIANT,
			DBUS_TYPE_STRING_AS_STRING, &variant_iter);
		dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &token);
		dbus_message_iter_close_container(&dict_iter, &variant_iter);
		dbus_message_iter_close_container(&array_iter, &dict_iter);
	}

	dbus_message_iter_close_container(&iter, &array_iter);

	if (!dbus_connection_send_with_reply(conn->conn, msg, pending, -1))
		return YUKINO_RESULT_OUT_OF_MEMORY;
	dbus_message_unref(msg);
	dbus_connection_flush(conn->conn);
	return YUKINO_RESULT_OK;
}

static char *get_token(void)
{
	uint64_t uuid[2];
	char *r;

	/* Generate a kind of random but not really UUID */
	if (yukino_random(uuid, sizeof(uuid)) < 0)
		return NULL;

	if (asprintf(&r, "yukino_%016" PRIx64 "%016" PRIx64, uuid[0], uuid[1]) < 0)
		return NULL;

	return r;
}

static yukino_result_t receive_message(
	struct yukino_xdg *conn, DBusPendingCall *pending, char **req)
{
	DBusMessage *msg;
	char *req_path;
	DBusError error;

	dbus_error_init(&error);

	dbus_pending_call_block(pending);
	msg = dbus_pending_call_steal_reply(pending);
	dbus_pending_call_unref(pending);

	if (!msg)
		return YUKINO_RESULT_UNSUPPORTED;

	if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR) {
		dbus_message_unref(msg);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	if (!dbus_message_get_args(
			msg, &error, DBUS_TYPE_OBJECT_PATH, &req_path, DBUS_TYPE_INVALID)) {
		dbus_error_free(&error);
		dbus_message_unref(msg);
		return YUKINO_RESULT_UNSUPPORTED;
	}
	*req = strdup(req_path);
	dbus_message_unref(msg);

	return YUKINO_RESULT_OK;
}

static yukino_result_t get_uri(struct yukino_xdg *conn, char **puri)
{
	yukino_result_t r;
	dbus_bool_t running;

	r = YUKINO_RESULT_UNSUPPORTED;
	running = TRUE;
	while (running && dbus_connection_read_write_dispatch(conn->conn, -1)) {
		DBusMessage *sig;
		DBusMessageIter sig_iter, res_dict, entry, variant;
		dbus_uint32_t response_code;

		sig = dbus_connection_pop_message(conn->conn);
		if (!sig)
			continue;

		if (!dbus_message_is_signal(
				sig, "org.freedesktop.portal.Request", "Response"))
			goto skip;

		dbus_message_iter_init(sig, &sig_iter);

		dbus_message_iter_get_basic(&sig_iter, &response_code);
		if (response_code != 0)
			goto skip;

		dbus_message_iter_next(&sig_iter);
		dbus_message_iter_recurse(&sig_iter, &res_dict);

		do {
			DBusMessageIter entry;
			const char *key;

			if (dbus_message_iter_get_arg_type(&res_dict)
				!= DBUS_TYPE_DICT_ENTRY)
				continue;

			dbus_message_iter_recurse(&res_dict, &entry);
			dbus_message_iter_get_basic(&entry, &key);

			if (strcmp(key, "uri") == 0) {
				const char *uri;

				dbus_message_iter_next(&entry);
				dbus_message_iter_recurse(&entry, &variant);
				dbus_message_iter_get_basic(&variant, &uri);

				*puri = strdup(uri);
				r = YUKINO_RESULT_OK;

				running = FALSE;
			}
		} while (dbus_message_iter_next(&res_dict));

	skip:
		dbus_message_unref(sig);
	}

	return r;
}

/* XDG variant of screenshot struct
 * We should have a generic implementation just reads on a membuf */
struct yukino_screenshot {
	DBusPendingCall *pending;
	char *token; /* Saved in case first call fails */
	struct yukino_image mbuf;

	uint32_t cx, cy, cw, ch;

	uint8_t *data;
};

yukino_result_t yukino_xdg_screenshot(struct yukino_xdg *conn,
	yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	yukino_result_t r;
	char *req_path, *uri, *token;
	yukino_screenshot_t *s;

	token = get_token();
	if (!token)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	s = calloc(1, sizeof(*s));
	if (!s) {
		free(token);
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}

	s->token = token;
	s->cx = x;
	s->cy = y;
	s->cw = w;
	s->ch = h;

	/* Avoid possible race by subscribing first
	 * with a prediction of the path */
	if (predict_req_path(conn, s->token, &req_path) >= 0) {
		subscribe(conn, req_path);
		free(req_path);
	}

	r = send_method(conn, s->token, TARGET_SCREEN, &s->pending);

	if (r >= 0) {
		*ps = s;
	} else {
		free(s->token);
		free(s);
	}

	/* Error */
	return r;
}

/* Receive the result :) */
static yukino_result_t yukino_xdg_receive(struct yukino_xdg *conn,
	yukino_screenshot_t *s)
{
	yukino_result_t r;
	char *uri, *path;
	int w, h, channels;

	/* Already done? */
	if (s->data)
		return YUKINO_RESULT_OK;
	if (!s->pending)
		return YUKINO_RESULT_INVALID_PARAM;

	{
		char *req_path;
		DBusPendingCall *pending;

		/* Make s->pending NULL so we don't do this again */
		pending = s->pending;
		s->pending = NULL;
		r = receive_message(conn, pending, &req_path);
		if (r < 0) {
			/* Oops */
			r = send_method(conn, s->token, TARGET_NONE, &pending);
			if (r < 0)
				return r;
			r = receive_message(conn, pending, &req_path);
			if (r < 0)
				return r;
		}

		/* This is a no-op if the token prediction was correct */
		subscribe(conn, req_path);
		free(req_path);
	}

	dbus_connection_flush(conn->conn);

	/* This is the call that blocks */
	r = get_uri(conn, &uri);
	if (r < 0)
		return r;

	/* Now just get the full path and read it all in */
	r = yukino_uri_get_file_path(uri, &path);
	if (r < 0)
		return r;

	/* don't really care about the channels */
	s->data = stbi_load(path, &w, &h, &channels, 4);
	unlink(path);
	free(path);

	/* Set these to the proper values */
	if (s->cw == YUKINO_SCREENSHOT_DESKTOP_RESOLUTION) s->cw = w;
	if (s->ch == YUKINO_SCREENSHOT_DESKTOP_RESOLUTION) s->ch = h;

	/* And we're off */
	yukino_image(&s->mbuf, s->data, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, YUKINO_IMAGE_ENDIAN_BIG, w * 4, s->cx, s->cy, s->cw, s->ch);

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_xdg_screenshot_resolution(struct yukino_xdg *conn,
	yukino_screenshot_t *s, uint32_t *w, uint32_t *h)
{
	yukino_result_t r;

	if ((r = yukino_xdg_receive(conn, s)) < 0)
		return r;

	return yukino_image_resolution(&s->mbuf, w, h);
}

yukino_result_t yukino_xdg_screenshot_read(struct yukino_xdg *conn,
	yukino_screenshot_t *s, unsigned char rgb[3])
{
	yukino_result_t r;

	if ((r = yukino_xdg_receive(conn, s)) < 0)
		return r;

	return yukino_image_read(&s->mbuf, rgb);
}

yukino_result_t yukino_xdg_screenshot_delete(struct yukino_xdg *conn,
	yukino_screenshot_t *s)
{
	if (s->data) {
		free(s->data);
	} else if (s->pending) {
		/* Dumb */
		dbus_pending_call_unref(s->pending);
	}

	free(s->token);
	free(s);

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_xdg_init(struct yukino_xdg *xdg)
{
	if (!xdg)
		return YUKINO_RESULT_INVALID_PARAM;

	memset(xdg, 0, sizeof(*xdg));

	xdg->conn = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if (!xdg->conn)
		return YUKINO_RESULT_UNSUPPORTED;

	return YUKINO_RESULT_OK;
}

void yukino_xdg_quit(struct yukino_xdg *xdg)
{
	if (!xdg)
		return;

	dbus_connection_unref(xdg->conn);
}
