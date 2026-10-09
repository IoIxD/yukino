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
#include "sys/kwin.h"
#include "dbus/dbus-protocol.h"
#include "dbus/dbus.h"
#include "yukino_c.h"

#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Before we send all of them over, we sort them by their stack order, just like
 * X11. */
#define KWIN_SCRIPT_TEMPLATE \
	"const wins = workspace.windowList();\n" \
	"function cmp(a,b)\n" \
	"{\n" \
	"        return (a.active) ? 1 : (a.stackingOrder > b.stackingOrder) ? 1 : (a.stackingOrder < b.stackingOrder) ? -1 : 0;\n" \
	"}\n" \
	"wins.sort(cmp);\n" \
	"for (var i = 0; i < wins.length; i++)\n" \
	"        callDBus(\"us.tflc.yukino_kwin\", \"/\", \"%s\", \"Ping\", wins[i].x, wins[i].y, wins[i].width, wins[i].height, wins[i].clientGeometry.x, wins[i].clientGeometry.y, wins[i].clientGeometry.width, wins[i].clientGeometry.height, wins[i].internalId.toString());\n" \
	"callDBus(\"us.tflc.yukino_kwin\", \"/\", \"%s\", \"Ping\");\n"

/* These belong in a more generic spot */
static char *get_temp_filename(const char *ext)
{
	uint64_t uuid[2];
	char *temp_dir, *temp_file;

	if (yukino_random(uuid, sizeof(uuid)) < 0)
		return NULL;

	temp_dir = getenv("XDG_RUNTIME_DIR");
	temp_dir = temp_dir ? temp_dir : getenv("TEMP");
	temp_dir = temp_dir ? temp_dir : "/tmp";

	/* butter my biscuits */
	if (asprintf(&temp_file, "%s/yukino_%016" PRIx64 "%016" PRIx64 "%s",
			temp_dir, uuid[0], uuid[1], ext ? ext : "")
		< 0)
		return NULL;

	return temp_file;
}

static char *write_temp_file(const char *ext, const char *instance)
{
	char *x;
	FILE *f;

	x = get_temp_filename(ext);
	if (!x)
		return NULL;

	f = fopen(x, "wb");
	if (!f) {
		free(x);
		return NULL;
	}

	if (fprintf(f, KWIN_SCRIPT_TEMPLATE, instance, instance) < 0) {
		free(x);
		return NULL;
	}

	fclose(f);

	return x;
}

static yukino_result_t kwin_load_script(
	struct yukino_kwin *kwi, const char *script_path, int32_t *script_id)
{
	DBusMessage *msg;
	DBusMessageIter iter;
	DBusMessage *reply;

	/* Load up our script. This also has the advantage of telling us
	 * whether KWin is actually running or not */
	msg = dbus_message_new_method_call(
		"org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript");
	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &script_path);
	reply = dbus_connection_send_with_reply_and_block(kwi->conn, msg, -1, NULL);
	dbus_message_unref(msg);
	if (!reply)
		return YUKINO_RESULT_UNSUPPORTED; /* ??? */

	dbus_message_iter_init(reply, &iter);
	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
		dbus_message_unref(reply);
		return YUKINO_RESULT_UNSUPPORTED;
	}
	dbus_message_iter_get_basic(&iter, script_id);
	dbus_message_unref(reply);

	return YUKINO_RESULT_OK;
}

static yukino_result_t kwin_unload_script(
	struct yukino_kwin *kwi, const char *script_path)
{
	DBusMessage *msg;
	DBusMessageIter iter;
	DBusMessage *reply;

	msg = dbus_message_new_method_call(
		"org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript");
	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &script_path);
	dbus_connection_send(kwi->conn, msg, NULL);
	dbus_message_unref(msg);

	return YUKINO_RESULT_OK;
}

static yukino_result_t kwin_write_and_load_script(struct yukino_kwin *kwi,
	const char *instance, char **path, int32_t *script_id)
{
	char *p;

	p = write_temp_file(".js", instance);
	if (!p)
		return YUKINO_RESULT_UNSUPPORTED;

	if (kwin_load_script(kwi, p, script_id) < 0)
		return YUKINO_RESULT_UNSUPPORTED;

	*path = p;
	return YUKINO_RESULT_OK;
}

static yukino_result_t parse_uuid(const char *x, struct uuid *uuid)
{
	int r;

	r = sscanf(x,
		"{%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx}",
		&uuid->c[0], &uuid->c[1], &uuid->c[2], &uuid->c[3], &uuid->c[4],
		&uuid->c[5], &uuid->c[6], &uuid->c[7], &uuid->c[8], &uuid->c[9],
		&uuid->c[10], &uuid->c[11], &uuid->c[12], &uuid->c[13], &uuid->c[14],
		&uuid->c[15]);

	if (r != 16)
		return YUKINO_RESULT_INVALID_PARAM;

	return YUKINO_RESULT_OK;
}

/*

Okay, here's the general outline for how this will work:

At runtime we will create a DBus "server". This server will only be run when
the window iterators are processing.

When window_iter_start is called, we send an async message to KWin to start
running the script. In window_iter, we then have to retrieve the result of that
call to see if it is -1 (failure), then move on. Meanwhile the script will go
through all of the windows and send them off to us in an easy-to-parse format.

This means window_iter will be the function running the event loop for dbus.

window_iter_end, as a cleanup function, will just clean up. :)

*/

struct yukino_window_iter {
	/* pending is non-NULL on first call to window_iter and NULL from then on */
	DBusPendingCall *pending;

	/* instance name -- unique */
	char *instance, *script_path;
	int32_t script_id;

	unsigned int done : 1; /* flag */
};

static struct kwin_window *find_window_by_yukino_id(
	struct yukino_kwin *kwi, yukino_window_t win)
{
	if (win >= kwi->w_size)
		return NULL;

	return kwi->w + win;
}

static struct kwin_window *find_window_by_uuid(
	struct yukino_kwin *kwi, const struct uuid *uuid)
{
	size_t i;

	for (i = 0; i < kwi->w_size; i++)
		if (memcmp(&kwi->w[i].uuid, uuid, sizeof(struct uuid)) == 0)
			return kwi->w + i;

	return NULL;
}

static yukino_result_t update_window(
	struct yukino_kwin *kwi, const struct kwin_window *w, yukino_window_t *id)
{
	/* Look up the ID */
	struct kwin_window *kw;

	kw = find_window_by_uuid(kwi, &w->uuid);
	if (kw) {
		/* Just update the existing one */
		memcpy(kw, w, sizeof(struct kwin_window));
		*id = kw - kwi->w;
		return YUKINO_RESULT_OK;
	}

	/* Have to add more? ... Ugh */
	if (kwi->w_size >= kwi->w_alloc) {
		kwi->w_alloc = (kwi->w && kwi->w_alloc) ? (kwi->w_alloc << 1) : 32;
		void *n
			= realloc(kwi->w, (kwi->w_alloc << 1) * sizeof(struct kwin_window));
		if (!n)
			return YUKINO_RESULT_OUT_OF_MEMORY;
		kwi->w_alloc <<= 2;
		kwi->w = n;
	}

	memcpy(kwi->w + kwi->w_size, w, sizeof(struct kwin_window));
	*id = kwi->w_size++;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_create_instance_name(yukino_window_iter_t *wi)
{
	if (asprintf(&wi->instance, "us.tflc.yukino_%0.*" PRIxPTR,
			(int)sizeof(uintptr_t), (uintptr_t)wi)
		< 0)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	return YUKINO_RESULT_OK;
}

/* FIXME this is broken if multiple window iterators are used in parallel
 * -- we should use the pointer as an identifier to disambiguate the script */
yukino_result_t yukino_kwin_window_iter_start(struct yukino_kwin *kwi,
	const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	DBusMessage *msg;
	char *a;
	yukino_window_iter_t *wi;
	yukino_result_t r;

	wi = calloc(1, sizeof(*wi));
	if ((r = yukino_create_instance_name(wi)) < 0) {
		free(wi);
		return r;
	}

	if ((r = kwin_write_and_load_script(
			 kwi, wi->instance, &wi->script_path, &wi->script_id))
		< 0) {
		free(wi->instance);
		free(wi);
		return r;
	}

	if (asprintf(&a, "/Scripting/Script%" PRId32, wi->script_id) < 0) {
		free(wi->script_path);
		free(wi->instance);
		free(wi);
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}

	/* Load */
	msg = dbus_message_new_method_call(
		"org.kde.KWin", a, "org.kde.kwin.Script", "run");
	free(a);
	if (!msg) {
		free(wi->script_path);
		free(wi->instance);
		free(wi);
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}

	/* Fire */
	if (!dbus_connection_send_with_reply(kwi->conn, msg, &wi->pending, -1)) {
		dbus_message_unref(msg);
		free(wi->script_path);
		free(wi->instance);
		free(wi);
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}

	dbus_message_unref(msg);
	*pwi = wi;

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_kwin_window_iter(
	struct yukino_kwin *kwi, yukino_window_iter_t *wi, yukino_window_t *win)
{
	if (wi->pending) {
		DBusMessage *msg;
		DBusMessageIter iter;

		dbus_pending_call_block(wi->pending);
		/* Get the reply and mark it so this doesn't get called again */
		msg = dbus_pending_call_steal_reply(wi->pending);
		wi->pending = NULL;

		dbus_message_iter_init(msg, &iter);
		if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
			return YUKINO_RESULT_INVALID_PARAM;
		/* No return value means success? */
		dbus_message_unref(msg);
	}

	if (wi->done)
		return YUKINO_RESULT_DONE;

	for (;;) {
		DBusMessage *msg;
		DBusMessageIter iter;
		const char *str;
		struct kwin_window w;

		msg = dbus_connection_pop_message(kwi->conn);
		if (!msg) {
			dbus_connection_read_write_dispatch(kwi->conn, -1);
			continue; /* Try again */
		}

		if (!dbus_message_is_method_call(msg, wi->instance, "Ping"))
			continue; /* Ignore */

		/* Empty message == end of list */
		if (!dbus_message_iter_init(msg, &iter)) {
			wi->done = 1;
			return YUKINO_RESULT_DONE;
		}

#define READ_VAL(type, x) \
	do { \
		if (dbus_message_iter_get_arg_type(&iter) != (type)) { \
			printf("%s line %d: Expected '%c', got '%c'\n", __FUNCTION__, \
				__LINE__, type, dbus_message_iter_get_arg_type(&iter)); \
			dbus_message_unref(msg); \
			return YUKINO_RESULT_UNSUPPORTED; \
		} \
\
		dbus_message_iter_get_basic(&iter, (x)); \
		dbus_message_iter_next(&iter); \
	} while (0)
#define READ_NUMBER(x) \
	do { \
		double d_; \
		int32_t i_; \
		int t; \
\
		t = dbus_message_iter_get_arg_type(&iter); \
		if (t == DBUS_TYPE_DOUBLE) { \
			READ_VAL(DBUS_TYPE_DOUBLE, &d_); \
			(x) = d_; \
		} else if (t == DBUS_TYPE_INT32) { \
			READ_VAL(DBUS_TYPE_INT32, &i_); \
			(x) = i_; \
		} \
	} while (0)
		/* Geometry with decorations */
		READ_NUMBER(w.bx);
		READ_NUMBER(w.by);
		READ_NUMBER(w.bw);
		READ_NUMBER(w.bh);

		/* Geometry without decorations */
		READ_NUMBER(w.x);
		READ_NUMBER(w.y);
		READ_NUMBER(w.w);
		READ_NUMBER(w.h);

		/* UUID */
		READ_VAL(DBUS_TYPE_STRING, &str);
#undef READ_FLOAT
#undef READ_VAL

		parse_uuid(str, &w.uuid);

		/* Add it to our list */
		update_window(kwi, &w, win);

		dbus_message_unref(msg);

		break;
	}

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_kwin_window_iter_end(
	struct yukino_kwin *kwi, yukino_window_iter_t *wi)
{
	/* Err, might want to empty the message queue?
	 * The pointer is not unique! It can be reused! */
	if (wi->pending)
		dbus_pending_call_unref(wi->pending);

	kwin_unload_script(kwi, wi->script_path);
	unlink(wi->script_path);
	free(wi->script_path);
	free(wi->instance);
	free(wi);

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* Specific window things */

yukino_result_t yukino_kwin_window_position(
	struct yukino_kwin *conn, yukino_window_t win, yukino_rect_t *pr)
{
	struct kwin_window *w = find_window_by_yukino_id(conn, win);
	if (!w)
		return YUKINO_RESULT_NONE;
	pr->x = w->x;
	pr->y = w->y;
	pr->w = ceil(w->w);
	pr->h = ceil(w->h);
	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_kwin_window_decorated_position(
	struct yukino_kwin *conn, yukino_window_t win, yukino_rect_t *pr)
{
	struct kwin_window *w = find_window_by_yukino_id(conn, win);
	if (!w)
		return YUKINO_RESULT_NONE;
	pr->x = w->bx;
	pr->y = w->by;
	pr->w = ceil(w->bw);
	pr->h = ceil(w->bh);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_kwin_init(struct yukino_kwin *kwi)
{
	DBusError err;
	static const char *match_rule
		= "type='method_call',interface='us.tflc.yukino_kwin',member='Ping'";

	dbus_error_init(&err);

	if (!kwi)
		return YUKINO_RESULT_INVALID_PARAM;

	memset(kwi, 0, sizeof(*kwi));

	kwi->conn = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if (!kwi->conn)
		return YUKINO_RESULT_UNSUPPORTED;
	dbus_connection_set_exit_on_disconnect(kwi->conn, FALSE);

	/* Ok, now our script is running and we have to set up our shit */
	if (!dbus_bus_request_name(kwi->conn, "us.tflc.yukino_kwin", 0, &err))
		return YUKINO_RESULT_UNSUPPORTED;

	dbus_bus_add_match(kwi->conn, match_rule, NULL);
	/* STFU */
	dbus_connection_read_write_dispatch(kwi->conn, -1);

	return YUKINO_RESULT_OK;
}

void yukino_kwin_quit(struct yukino_kwin *kwi)
{
	if (!kwi)
		return;

	dbus_connection_unref(kwi->conn);
}
