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

/* FIXME test stuff like 24-bit, 16-bit, 8-bit */
#define SAMPLE_DATA(rshift, bshift, gshift) \
	SAMPLE_PIXEL(0x12, 0x34, 0x56, rshift, bshift, gshift) \
	SAMPLE_PIXEL(0x56, 0x34, 0x12, rshift, bshift, gshift) \
	SAMPLE_PIXEL(0x56, 0x34, 0x12, rshift, bshift, gshift)

#define SAMPLE_DATA_FORMATS \
	SAMPLE_DATA_FORMAT( \
		rgbx, 32, 24, 16, 8, 0xFF000000, 0x00FF0000, 0x0000FF00) \
	SAMPLE_DATA_FORMAT(xrgb, 32, 16, 8, 0, 0x00FF0000, 0x0000FF00, 0x000000FF)

#define SAMPLE_PIXEL(r, g, b, rshift, bshift, gshift) \
	(((uint32_t)(r) << (rshift)) | ((uint32_t)(g) << (gshift)) \
		| ((uint32_t)(b) << (bshift))),
#define SAMPLE_DATA_FORMAT( \
	name, bpp, rshift, bshift, gshift, rmask, gmask, bmask) \
	static const uint##bpp##_t sample_data_##name[] \
		= {SAMPLE_DATA(rshift, gshift, bshift)};

SAMPLE_DATA_FORMATS

#undef SAMPLE_PIXEL
#undef SAMPLE_DATA_FORMAT

int main(void)
{
	struct yukino_image img;
	unsigned char rgb[3];

#define SAMPLE_PIXEL(r, g, b, rshift, bshift, gshift) \
	if (yukino_image_read(&img, rgb) < 0) \
		return 1; \
	if (rgb[0] != (r) || rgb[1] != (g) || rgb[2] != (b)) \
		return 1;
#define SAMPLE_DATA_FORMAT( \
	name, bpp, rshift, bshift, gshift, rmask, gmask, bmask) \
	yukino_image(&img, sample_data_##name, bpp, rmask, gmask, bmask, \
		YUKINO_IMAGE_ENDIAN_NATIVE, sizeof(sample_data_##name[0]), 0, 0, 1, \
		sizeof(sample_data_##name) / sizeof(sample_data_##name[0])); \
	SAMPLE_DATA(rshift, bshift, gshift)

	SAMPLE_DATA_FORMATS

#undef SAMPLE_PIXEL
#undef SAMPLE_DATA_FORMAT

	return 0;
}
