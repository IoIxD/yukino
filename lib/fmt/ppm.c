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

struct cbuserdata {
	yukino_write_cb write_cb;
	void *userdata;
};

static yukino_result_t ppm_cb(void *userdata, const unsigned char rgb[3])
{
	struct cbuserdata *ud = userdata;

	return ud->write_cb(ud->userdata, rgb, 3);
}

YUKINO_INLINE char *u32tostr(uint32_t x, char s[10])
{
	char *p = s + 9;

	while (x) {
		*p-- = (x % 10) + '0';
		x /= 10;
	}

	return p;
}

static yukino_result_t write_u32str(
	yukino_write_cb write_cb, void *userdata, uint32_t x)
{
	char xx[10];
	char *ptr;

	ptr = u32tostr(x, xx);

	return write_cb(userdata, ptr, 10 - (ptr - xx));
}

yukino_result_t yukino_write_ppm(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_write_cb write_cb, void *userdata, yukino_image_proc_t take_cb,
	void *take_data)
{
	struct cbuserdata ud = {write_cb, userdata};
	yukino_result_t r;

#define R_ASSERT(x) \
	if ((r = (x)) < 0) \
	return r
	R_ASSERT(write_cb(userdata, "P6\n", 3));
	R_ASSERT(write_u32str(write_cb, userdata, w));
	R_ASSERT(write_cb(userdata, " ", 1));
	R_ASSERT(write_u32str(write_cb, userdata, h));
	R_ASSERT(write_cb(userdata, "\n255\n", 5));
#undef R_ASSERT

	return take_cb(take_data, x, y, w, h, ppm_cb, &ud);
}
