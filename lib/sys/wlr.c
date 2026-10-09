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

#include "sys/wlr.h"

#include "stb_image.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h> /* unlink */

#include <xdg-output-unstable-v1.h>

struct yukino_wlr_display {
	struct wl_output *output;
	struct zxdg_output_v1 *xdg_output;
	struct wl_buffer *shm_buffer;
	struct wl_shm_pool *shm_pool;
	struct yukino_wlr *wlr;
	int buf_size;
	int x, y, w, h, mw, mh, stride;
	int rw, rh;
	int fd;
	void *buf, *ud, *pxl;
	int ready;
};

static void handle_xdg_output_logical_position(
	void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y)
{
	struct yukino_wlr_display *d = data;

	d->x = x;
	d->y = y;
}

static void handle_xdg_output_logical_size(void *data,
	struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height)
{
	struct yukino_wlr_display *d = data;

	d->mw = width;
	d->mh = height;
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
// Callback to handle global interface registry additions
static void registry_handle_global(void *data, struct wl_registry *registry,
	uint32_t id, const char *interface, uint32_t version)
{
	struct yukino_wlr *wlr = data;

	if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
		wlr->screencopy = wl_registry_bind(
			registry, id, &zwlr_screencopy_manager_v1_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		wlr->num_outputs++;
		if (!wlr->outputs) {
			wlr->outputs = malloc(sizeof(*wlr->outputs) * wlr->num_outputs);
		} else {
			void *n = realloc(
				wlr->outputs, sizeof(struct wl_output *) * wlr->num_outputs);
		}
		struct yukino_wlr_display *out = wlr->outputs[wlr->num_outputs - 1]
			= calloc(sizeof(struct yukino_wlr_display), 1);
		out->output = wl_registry_bind(registry, id, &wl_output_interface, 1);

		out->xdg_output
			= zxdg_output_manager_v1_get_xdg_output(wlr->om, out->output);
		zxdg_output_v1_add_listener(out->xdg_output, &xdg_output_listener, out);

		out->wlr = wlr;

	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		wlr->om = wl_registry_bind(
			registry, id, &zxdg_output_manager_v1_interface, 1);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		wlr->shm = wl_registry_bind(registry, id, &wl_shm_interface, version);
	}
}

static void registry_handle_global_remove(
	void *data, struct wl_registry *registry, uint32_t id)
{
}

static struct wl_registry_listener wlr_screencopy_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static void zwlr_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame,
	uint32_t format, uint32_t width, uint32_t height, uint32_t stride)
{
	struct yukino_wlr_display *display = data;
	int err;
	char temp_name[] = "/tmp/yukino-wl-shm-XXXXXXXX";

	display->buf_size = width * height * 4;

	display->fd = mkstemp(temp_name);
	if (display->fd >= 65536) {
		printf(
			"Amount of allocated buffers has gone above 65536! Cannot continue.\n");
		return;
	}
	if (display->fd == -1) {
		printf("failure setting up wl_shm: could not create file. %s.\n",
			strerror(errno));
		return;
	}

	unlink(temp_name);

  /* posix_fallocate returns the error rather than setting errno */
	if ((err = posix_fallocate(display->fd, 0, display->buf_size)) != 0) {
		printf(
			"failure setting up wl_shm (front buf): could not fallocate %llu "
			"bytes. %s.\n",
			(unsigned long long)display->buf_size, strerror(err));
		return;
	}
	if (ftruncate(display->fd, display->buf_size) != 0) {
		printf("failure setting up wl_shm: could not truncate. %s.\n",
			strerror(errno));
		return;
	}

	display->pxl = display->buf
		= mmap(NULL, display->buf_size, PROT_WRITE, MAP_SHARED, display->fd, 0);

	fsync(display->fd);

	if (!(display->shm_pool = wl_shm_create_pool(
			  display->wlr->shm, display->fd, display->buf_size))) {
		printf("failure setting up wl_shm: could not create pool.\n");
	}

	display->shm_buffer = wl_shm_pool_create_buffer(
		display->shm_pool, 0, width, height, stride, format);
	display->w = width;
	display->h = height;
	display->stride = stride;
	display->ready = 0;
	zwlr_screencopy_frame_v1_copy(frame, display->shm_buffer);
};
static void zwlr_flags(void *data,
	struct zwlr_screencopy_frame_v1 *zwlr_screencopy_frame_v1, uint32_t flags)
{
	struct yukino_wlr_display *conn = data;
};
static void zwlr_ready(void *data, struct zwlr_screencopy_frame_v1 *frame,
	uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
	struct yukino_wlr_display *display = data;
	display->ready = 1;
};
static void zwlr_failed(
	void *data, struct zwlr_screencopy_frame_v1 *zwlr_screencopy_frame_v1)
{
	struct yukino_wlr_display *conn = data;

	munmap(conn->buf, conn->buf_size);
	close(conn->fd);
};

static struct zwlr_screencopy_frame_v1_listener wlr_frame_listener = {
	.buffer = zwlr_buffer,
	.flags = zwlr_flags,
	.ready = zwlr_ready,
	.failed = zwlr_failed,
};

yukino_result_t yukino_wlr_take(struct yukino_wlr *conn, uint32_t x, uint32_t y,
	uint32_t width, uint32_t height, yukino_pixel_proc_t pixel_func,
	void *userdata)
{
	yukino_result_t r;
	struct zwlr_screencopy_frame_v1 *frame = NULL;
	int mx, my;

	for (int i = 0; i < conn->num_outputs; i++) {
		struct yukino_wlr_display *out = conn->outputs[i];
		frame = zwlr_screencopy_manager_v1_capture_output(
			conn->screencopy, 0, out->output);
		zwlr_screencopy_frame_v1_add_listener(frame, &wlr_frame_listener, out);

		out->rw = width;
		out->rh = height;
		out->ud = userdata;

		while (!out->ready) {
			wl_display_roundtrip(conn->wl->display);
		}
	}

	for (my = 0; my < height; my++) {
		for (mx = 0; mx < width; mx++) {
			int monitor_found = 0;
			for (int i = 0; i < conn->num_outputs; i++) {
				struct yukino_wlr_display *out = conn->outputs[i];

				if (mx > out->x && mx < out->x + out->mw && my > out->y
					&& my < out->y + out->mh) {
					monitor_found = 1;
					yukino_result_t res
						= pixel_func(userdata, (uint8_t *)out->pxl + (mx * 4));
					if (res < 0) {
						return res;
					}
					break;
				}
			}
			if (monitor_found != 1) {
				static uint8_t dummy[3] = {0};
				yukino_result_t res = pixel_func(userdata, dummy);
				if (res < 0) {
					return res;
				}
			}
		}
		for (int i = 0; i < conn->num_outputs; i++) {
			struct yukino_wlr_display *out = conn->outputs[i];
			out->pxl += out->stride;
		}
	}

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_wlr_init(
	struct yukino_wlr *wlr, struct yukino_wayland *wl)
{
	if (!wlr)
		return YUKINO_RESULT_INVALID_PARAM;

	memset(wlr, 0, sizeof(*wlr));

	wlr->wl = wl;

	struct wl_registry *registry = wl_display_get_registry(wl->display);

	wl_registry_add_listener(registry, &wlr_screencopy_listener, wlr);

	wl_display_roundtrip(wl->display);
	wl_display_roundtrip(wl->display);

	if (wlr->screencopy == NULL) {
		return YUKINO_RESULT_UNSUPPORTED;
	}

	printf("wlr yay\n");

	return YUKINO_RESULT_OK;
}

void yukino_wlr_quit(struct yukino_wlr *wlr, struct yukino_wayland *wl)
{
	if (!wlr)
		return;
}
