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

#include "yukino_c.h"

yukino_result_t yukino_image(struct yukino_image *m, const void *buf,
	uint8_t bpp, uint32_t red_mask, uint32_t green_mask, uint32_t blue_mask,
	uint32_t endian,
	size_t stride,
	/* the coordinates etc. to clip to */
	uint32_t cx, uint32_t cy, uint32_t cw, uint32_t ch)
{
	m->ptr = buf;

	m->w = cw;
	m->h = ch;
	m->x = cx;
	m->y = cy;
	m->stride = stride;
	m->bpp = bpp;

#define FILL(color) \
	do { \
		m->color##_mask = color##_mask; \
		m->color##_shift = yukino_ctz32(m->color##_mask); \
		m->color##_div = m->color##_mask >> m->color##_shift; \
	} while (0)
	FILL(red);
	FILL(green);
	FILL(blue);
#undef FILL

	/* wow */
	m->ptr += cy * stride;

	if (endian == YUKINO_IMAGE_ENDIAN_NATIVE) {
		/* meh */
		union { uint32_t x; uint32_t c[4]; } e;
		e.x = 0x12345678;
		m->endian = (e.c[0] == 0x12) ? YUKINO_IMAGE_ENDIAN_BIG : YUKINO_IMAGE_ENDIAN_LITTLE;
	} else {
		m->endian = endian;
	}

	return YUKINO_RESULT_OK;
}

static uint32_t read_pixel(
	const uint8_t *p, uint32_t x, uint8_t bpp, uint32_t endian)
{
	uint32_t pxl;

	/* shift it to the right place */
	p += (x * bpp) >> 3;

	if (bpp == 4) {
		pxl = ((x & 1) != endian) ? (*p & 0xf) : (*p >> 4);
	} else if (endian) {
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

yukino_result_t yukino_image_read(struct yukino_image *s, unsigned char rgb[3])
{
	uint32_t pxl;

	if (s->y >= s->h)
		return YUKINO_RESULT_DONE;

	pxl = read_pixel(s->ptr, s->x, s->bpp, s->endian);

#define SCALE(x, color) \
	((((x) & s->color##_mask) >> s->color##_shift) * 255 / s->color##_div)
	rgb[0] = SCALE(pxl, red);
	rgb[1] = SCALE(pxl, green);
	rgb[2] = SCALE(pxl, blue);
#undef SCALE

	s->x++;
	if (s->x == s->w) {
		s->x = 0;
		s->y++;
		s->ptr += s->stride;
	}

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_image_resolution(struct yukino_image *m, uint32_t *w, uint32_t *h)
{
	*w = m->w;
	*h = m->h;
	return YUKINO_RESULT_OK;
}
