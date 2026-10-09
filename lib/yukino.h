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

#ifndef YUKINO_H_
#define YUKINO_H_

#ifdef _WIN32
# ifdef YUKINO_BUILD
#  define YUKINO_EXTERN __declspec(dllexport) extern
# else
#  define YUKINO_EXTERN __declspec(dllimport) extern
# endif
#elif defined(__GNUC__)
# define YUKINO_EXTERN __attribute__((__visibility__("default"))) extern
#else
# define YUKINO_EXTERN extern
#endif

/* don't care about c89 */
#include <stdint.h>
#include <stdlib.h>

/* typed enum */
#if (__STDC_VERSION__ >= 202311L)
# define YUKINO_TYPED_ENUM_BEGIN(name, type) \
	typedef enum : type {
# define YUKINO_TYPED_ENUM_END(name) \
	} name;
#else
# define YUKINO_TYPED_ENUM_BEGIN(name, type) \
	typedef type name; \
	enum {
# define YUKINO_TYPED_ENUM_END(name) \
	};
#endif

/* Every window system ever uses something that can fit in 64-bits,
 * or a pointer, but 64-bit everywhere is probably safer for the
 * foreseeable future. */
typedef uint64_t yukino_window_t;

/* Window iterator object -- opaque */
struct yukino_window_iter;
typedef struct yukino_window_iter yukino_window_iter_t;

/* Connection to the display */
struct yukino_connection;
typedef struct yukino_connection yukino_connection_t;

/* negative value == error */
typedef int32_t yukino_result_t;

/* TODO switch everything over to this */
typedef struct yukino_rect {
	int32_t x, y, w, h;
} yukino_rect_t;

YUKINO_TYPED_ENUM_BEGIN(yukino_result_t, int32_t)
	/* Error values, counting up from the lowest. */

	/* Yukino generic error codes:
	 *
	 * -0x80000000 - -0x7FFFFF00 */
	YUKINO_RESULT_UNSUPPORTED = INT32_MIN,
	YUKINO_RESULT_OUT_OF_MEMORY,
	YUKINO_RESULT_INVALID_PARAM,
	YUKINO_RESULT_FILE_ERROR,
	YUKINO_RESULT_NO,

	/* X11 server errors.
	 * These values map directly to the X11 error codes:
	 *
	 * -0x7FFFFEFF - -0x7FFFFE00  */
	YUKINO_RESULT_X11 = INT32_MIN + 0x101,

	/* Errors mapped by the connection at runtime.
	 *
	 * These are specific to the connection currently
	 * running. We count these backwards, and the errors
	 * are registered for each connection as they happen.
	 * This value is just a helper for the backend to know
	 * what the minimum value is before it starts colluding
	 * with other errors.
	 *
	 * -0x1 - -0x7FFFFDFF */
	YUKINO_RESULT_CONNECTION_END = INT32_MIN + 0x201,

	YUKINO_RESULT_OK = 0,
	/* For iterators -- should also set stuff to NULL? */
	YUKINO_RESULT_DONE,
	/* For querying window at point */
	YUKINO_RESULT_NONE,
YUKINO_TYPED_ENUM_END(yukino_result_t)

/* ------------------------------------------------------------------------ */
/* connect to the display */

YUKINO_EXTERN yukino_result_t yukino_connect(yukino_connection_t **conn);
YUKINO_EXTERN yukino_result_t yukino_disconnect(yukino_connection_t *conn);

/* ------------------------------------------------------------------------ */
/* act on the display ... not much else is useful here */

YUKINO_EXTERN yukino_result_t yukino_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h);

/* ------------------------------------------------------------------------ */
/* iterate through windows */

/* On X11, this loops from the backmost window to the frontmost window. */
YUKINO_EXTERN yukino_result_t yukino_window_iter_start(
	yukino_connection_t *conn,
	const yukino_window_t *win, /* NULL == operate on the root window */
	yukino_window_iter_t **pwi);
YUKINO_EXTERN yukino_result_t yukino_window_iter(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *pw);
YUKINO_EXTERN yukino_result_t yukino_window_iter_end(
	yukino_connection_t *conn, yukino_window_iter_t *wi);

/* ------------------------------------------------------------------------ */
/* window utils */

/* XXX have a yukino_rect instead? */
YUKINO_EXTERN yukino_result_t yukino_window_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr);

YUKINO_EXTERN yukino_result_t yukino_window_decorated_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr);

/* ------------------------------------------------------------------------ */
/* display lock/unlock. this could come in handy if say, we wanted to take
 * a full display screenshot, take note of the position of all of the
 * windows, and THEN let the user crop it
 *
 * This function is reference counted.
 *
 * BUT if we crash we'd end up leaving x11 in a buggy state ... */

YUKINO_EXTERN yukino_result_t yukino_lock(yukino_connection_t *conn);
YUKINO_EXTERN yukino_result_t yukino_unlock(yukino_connection_t *conn);

/* ------------------------------------------------------------------------ */
/* simple image writers -- these compile to a few kilobyte each
 *
 * if zlib is compiled in, then clearly png is the best choice. */

/* writes a pixel value */
typedef yukino_result_t (*yukino_pixel_proc_t)(
	void *userdata, const unsigned char rgb[3]);

/* writes bytes ... */
typedef yukino_result_t (*yukino_write_cb)(
	void *userdata, const void *bytes, size_t size);

/* image iteration function */
typedef yukino_result_t (*yukino_image_proc_t)(void *conn, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func,
	void *userdata);

/* writes an image in the given format */
#define YUKINO_WRITE(N) \
	YUKINO_EXTERN yukino_result_t yukino_write_##N(uint32_t x, uint32_t y, \
		uint32_t w, uint32_t h, yukino_write_cb write_cb, void *write_data, \
		yukino_image_proc_t take_cb, void *take_data);

YUKINO_WRITE(bmp)
YUKINO_WRITE(png)
YUKINO_WRITE(ppm)

#undef YUKINO_WRITE

/* ------------------------------------------------------------------------ */
/* take a screenshot */

/* moving this here */
typedef struct yukino_screenshot yukino_screenshot_t;

/* Magic width/height value, passed into either the w or h of
 * yukino_screenshot_create to just get a screenshot of the desktop. */
#define YUKINO_SCREENSHOT_DESKTOP_RESOLUTION UINT32_C(0xFFFFFFFF)

YUKINO_EXTERN yukino_result_t yukino_screenshot_create(
	yukino_connection_t *conn, yukino_screenshot_t **ps, uint32_t x, uint32_t y,
	uint32_t w, uint32_t h);
YUKINO_EXTERN yukino_result_t yukino_screenshot_resolution(
	yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w,
	uint32_t *h);
/* returns YUKINO_RESULT_DONE if there are no bytes left */
YUKINO_EXTERN yukino_result_t yukino_screenshot_read(
	yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3]);
YUKINO_EXTERN yukino_result_t yukino_screenshot_delete(
	yukino_connection_t *conn, yukino_screenshot_t *s);

YUKINO_EXTERN yukino_result_t yukino_screenshot(yukino_connection_t *conn,
	uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_pixel_proc_t pixel_func, void *userdata);

#define YUKINO_SCREENSHOT(N) \
	YUKINO_EXTERN yukino_result_t yukino_screenshot_##N( \
		yukino_connection_t *conn, uint32_t x, uint32_t y, uint32_t w, \
		uint32_t h, yukino_write_cb write_cb, void *userdata);

YUKINO_SCREENSHOT(ppm)
YUKINO_SCREENSHOT(png)
YUKINO_SCREENSHOT(bmp)

#undef YUKINO_SCREENSHOT

YUKINO_EXTERN yukino_result_t yukino_rect_has_point(
	const yukino_rect_t *r, int32_t x, int32_t y);

/* ------------------------------------------------------------------------ */

YUKINO_EXTERN const char *yukino_error_name(yukino_connection_t *conn, yukino_result_t r);
YUKINO_EXTERN const char *yukino_error_description(yukino_connection_t *conn, yukino_result_t r);

#endif
