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

struct bmp {
	yukino_write_cb write_cb;
	void *userdata;

	uint32_t x, w;
};

#define R_ASSERT(x) \
	do { \
		if ((r = (x)) < 0) \
			return r; \
	} while (0)

YUKINO_INLINE uint16_t bmp_bswap16(uint16_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return px[0] | ((uint16_t)px[1] << 8);
}

YUKINO_INLINE uint32_t bmp_bswap32(uint32_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16)
		| ((uint32_t)px[3] << 24);
}

YUKINO_INLINE yukino_result_t bmp_write(
	struct bmp *bmp, const void *x, size_t sz)
{
	return bmp->write_cb(bmp->userdata, x, sz);
}

YUKINO_INLINE yukino_result_t bmp_write_u16(struct bmp *bmp, uint16_t x)
{
	x = bmp_bswap16(x);
	return bmp_write(bmp, &x, sizeof(x));
}

YUKINO_INLINE yukino_result_t bmp_write_u32(struct bmp *bmp, uint32_t x)
{
	x = bmp_bswap32(x);
	return bmp_write(bmp, &x, sizeof(x));
}

YUKINO_INLINE yukino_result_t bmp_write_i32(struct bmp *bmp, int32_t x)
{
	return bmp_write_u32(bmp, *(uint32_t *)&x);
}

static yukino_result_t bmp_cb(void *userdata, const unsigned char rgb_[3])
{
	struct bmp *bmp = userdata;
	yukino_result_t r;
	unsigned char rgb[3];

	/* swap it around */
	rgb[0] = rgb_[2];
	rgb[1] = rgb_[1];
	rgb[2] = rgb_[0];

	/* 24-bit */
	R_ASSERT(bmp_write(bmp, rgb, 3));

	if (++bmp->x == bmp->w) {
		/* write pad */
		R_ASSERT(bmp_write(bmp, "\0\0\0", bmp->w % 4));
		bmp->x = 0;
	}

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_write_bmp(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_write_cb write_cb, void *userdata, yukino_image_proc_t take_cb,
	void *take_data)
{
	struct bmp bmp;
	yukino_result_t r;

	bmp.write_cb = write_cb;
	bmp.userdata = userdata;
	bmp.x = 0;
	bmp.w = w;

	/* write out the header :) */
	R_ASSERT(bmp_write(&bmp, "BM", 2));
	R_ASSERT(bmp_write_u32(&bmp, 14 + 40 + (w * h * 3)));
	R_ASSERT(bmp_write(&bmp, "\0\0\0\0\x36\0\0\0\x28\0\0", 12));
	R_ASSERT(bmp_write_u32(&bmp, w));
	R_ASSERT(bmp_write_i32(&bmp, -(int32_t)h));
	/* 24bpp */
	R_ASSERT(bmp_write(&bmp, "\1\0\x18\0\0\0\0\0\0\0\0", 12));
	R_ASSERT(bmp_write_u32(&bmp, w));
	R_ASSERT(bmp_write_i32(&bmp, -(int32_t)h));
	R_ASSERT(bmp_write(&bmp, "\0\0\0\0\0\0\0", 8));
	/* done with the hdr */

	return take_cb(take_data, x, y, w, h, bmp_cb, &bmp);
}
