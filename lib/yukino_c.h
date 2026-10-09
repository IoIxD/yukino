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

#ifndef YUKINO_C_H_
#define YUKINO_C_H_

#include "yukino.h"

#if defined(__x86_64__)
# define YUKINO_64BIT_ADLER32 1
#endif

#ifdef __GNUC__
# define YUKINO_INLINE static inline __attribute__((__always_inline__))
#else
# define YUKINO_INLINE static inline
#endif

/* Pass this value to yukino_screenshot to get a screenshot of the entire desktop */
#define YUKINO_SCREENSHOT_DESKTOP_RESOLUTION UINT32_C(0xFFFFFFFF)

/* private screenshot API; not currently exported as it isn't implemented
 * across all drivers. but this should be much more extensible in case we
 * ever want to implement e.g. reading floating point RGB values rather
 * than strictly 8-bit RGB. */
typedef struct yukino_screenshot yukino_screenshot_t;
/* This will eventually be renamed to yukino_screenshot once the old API
 * is no more */
yukino_result_t yukino_screenshot_create(yukino_connection_t *conn, yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
yukino_result_t yukino_screenshot_resolution(yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h);
/* returns YUKINO_RESULT_DONE if there are no bytes left */
yukino_result_t yukino_screenshot_read(yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3]);
yukino_result_t yukino_screenshot_delete(yukino_connection_t *conn, yukino_screenshot_t *s);

/* privates */
struct yukino_connection {
	/* Disconnect. This is the only function pointer that should never
	 * be NULL. */
	yukino_result_t (*disconnect)(yukino_connection_t *conn);

	/* Get the current *desktop* resolution (not display...) */
	yukino_result_t (*display_resolution)(
		yukino_connection_t *conn, uint32_t *w, uint32_t *h);

	/* Window iteration. It's done this way primarily so that one
	 * could queue up a bunch of window iterations, and benefit
	 * from xcb's async API. */
	yukino_result_t (*window_iter_start)(yukino_connection_t *conn,
		const yukino_window_t *win, yukino_window_iter_t **pwi);
	yukino_result_t (*window_iter)(yukino_connection_t *conn,
		yukino_window_iter_t *wi, yukino_window_t *pw);
	yukino_result_t (*window_iter_end)(
		yukino_connection_t *conn, yukino_window_iter_t *wi);

	/* Grabs the window position. */
	yukino_result_t (*window_position)(
		yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr);

	/* Grabs the window's decorated position */
	yukino_result_t (*window_decorated_position)(
		yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr);

	/* Takes a screenshot and passes each pixel to pixel_func, in order
	 * of left-right top-down. */
	yukino_result_t (*take)(yukino_connection_t *conn, uint32_t x, uint32_t y,
		uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func, void *userdata);

	/* Takes a screenshot. Then you can do a bunch of different things */
	yukino_result_t (*screenshot)(yukino_connection_t *conn, yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
	yukino_result_t (*screenshot_resolution)(yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h);
	yukino_result_t (*screenshot_read)(yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3]);
	yukino_result_t (*screenshot_delete)(yukino_connection_t *conn, yukino_screenshot_t *s);

	/* Locks and unlocks the display server, if possible, otherwise
	 * return YUKINO_RESULT_UNSUPPORTED. This does not have to be
	 * reference counted (and should *not* be -- that's done at
	 * the top level already). */
	yukino_result_t (*lock)(yukino_connection_t *conn);
	yukino_result_t (*unlock)(yukino_connection_t *conn);

	/* Locking reference count. */
	uint32_t lock_ref;

#ifdef YUKINO_CONNECTION_DATA
	/* Private data for each backend to define. */
	struct yukino_connection_data conn_data;
#endif
};

/* CRC32 with the PNG polynomial */
uint32_t yukino_crc32(uint32_t crc, const unsigned char *msg, size_t sz);

/* Adler-32.
 *
 * Due to the way we calculate the hash, it is significantly faster to
 * do this all here. */
struct yukino_adler32 {
#ifdef YUKINO_64BIT_ADLER32
	uint64_t a, b;
#else
	uint32_t a, b;
#endif

	/* this can stay 32-bit */
	uint32_t n;
};

void yukino_adler32_init(struct yukino_adler32 *a32);
void yukino_adler32(
	struct yukino_adler32 *a32, const unsigned char *msg, size_t sz);
uint32_t yukino_adler32_get(struct yukino_adler32 *a32);

#ifdef YUKINO_XCB
/* initializes a connection */
yukino_result_t yukino_xcb_connect(yukino_connection_t **pconn);
#endif

#ifdef YUKINO_GIO
/* initializes a connection */
yukino_result_t yukino_gio_connect(yukino_connection_t **pconn);
#endif

YUKINO_INLINE unsigned int yukino_ctz32(uint32_t x)
{
#ifdef __GNUC__
	return __builtin_ctz(x);
#else
	unsigned int i;

	if (!x)
		return 32;

	for (i = 0; !(x & 1); x >>= 1, i++)
		;

	return i;
#endif
}

YUKINO_INLINE unsigned int yukino_popcnt32(uint32_t x)
{
#ifdef __GNUC__
	return __builtin_popcount(x);
#else
	unsigned int i, j;

	for (i = 0, j = 0; j < (sizeof(x) * CHAR_BIT); x >>= 1, j++)
		i += (x & 1);

	return i;
#endif
}

/* URI decoding.
 *
 * Consumes the input buffer p of size len, and puts the decoded form
 * into *res.
 * The buffer put into *res is always NUL terminated, so if you know
 * you just need a C string, you do not need to pass a res_size. */
yukino_result_t yukino_uri_decode(
	const char *p, size_t len, unsigned char **res, size_t *res_size);
yukino_result_t yukino_uri_get_file_path(const char *p, char **res);

/* Fill a buffer with random bytes
 * Currently hardcoded for /dev/urandom and has no fallback */
yukino_result_t yukino_random(void *x, size_t n);

yukino_result_t yukino_screenshot_fix_resolution(yukino_connection_t *conn, uint32_t *w, uint32_t *h);

#endif /* YUKINO_C_H_ */
