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

/* URI decoding */
#include "yukino.h"

#include <string.h>

static inline yukino_result_t uri_decode_halfbyte(
	unsigned char p, unsigned char *r)
{
	if (p >= '0' && p <= '9') {
		*r = p - '0';
		return YUKINO_RESULT_OK;
	}

	if (p >= 'A' && p <= 'F') {
		*r = p - 'A' + 10;
		return YUKINO_RESULT_OK;
	}

	return YUKINO_RESULT_INVALID_PARAM;
}

/* Caller must check for length of p is >= 2 */
static inline yukino_result_t uri_decode_byte(
	const unsigned char *p, unsigned char *r)
{
	unsigned char rr;
	yukino_result_t res;

	if ((res = uri_decode_halfbyte(p[0], &rr)) < 0)
		return res;
	*r = rr << 4;
	if ((res = uri_decode_halfbyte(p[1], &rr)) < 0)
		return res;
	*r |= rr;

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_uri_decode(
	const char *p_, size_t len, unsigned char **res, size_t *res_size)
{
	unsigned char *r;
	size_t i, j;
	const unsigned char *p;
	yukino_result_t re;

	p = (const unsigned char *)p_;

	r = malloc(len + 1);
	if (!r)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	for (i = 0, j = 0;;) {
		/* string.h routines are extremely optimized */
		size_t copylen;
		const unsigned char *pp;

		pp = memchr(p + i, '%', len - i);
		if (pp) {
			copylen = pp - p;
		} else {
			copylen = len - i;
		}

		memcpy(r + j, p + i, copylen);
		i += copylen;
		j += copylen;

		/* Done? */
		if (!pp)
			break;

		/* p[i] points to the percent sign, jump past it */
		i++;
		if ((i + 2) >= len) {
			free(r);
			return YUKINO_RESULT_INVALID_PARAM;
		}

		/* AND TAKE WHAT'S OURS */
		if ((re = uri_decode_byte(p + i, r + j)) < 0) {
			free(r);
			return re;
		}

		i += 2;
		j++;
	}

	r[j] = 0;
	/* Try reallocing to the actual size, if it returns NULL just put the old
	 * buffer in */
	*res = realloc(r, j + 1);
	if (!*res)
		*res = r;
	if (res_size)
		*res_size = j;

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_uri_get_file_path(const char *p, char **res)
{
	if (strncmp(p, "file://", 7))
		return YUKINO_RESULT_INVALID_PARAM;

	/* Meh */
	p += 7;
	return yukino_uri_decode(p, strlen(p), (unsigned char **)res, NULL);
}
