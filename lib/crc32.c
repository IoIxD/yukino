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

#define CRC32_POLYNOMIAL 0xEDB88320

/* This builds our CRC table at compile-time rather than runtime.
 * Note: We should have a way to change the polynomial at runtime too. */

#define CRC32_MASK(crc) (-((crc) & 1))

/* Does one iteration of the 8-time loop to generate one byte of the CRC table.
 */
#define CRC32_PRECALC_EX(crc) \
	(((crc) >> 1) ^ ((CRC32_POLYNOMIAL) & CRC32_MASK(crc)))

/* Does all eight iterations of the loop to generate one byte. */
#define CRC32_PRECALC_E(byte) \
	(CRC32_PRECALC_EX( \
		CRC32_PRECALC_EX(CRC32_PRECALC_EX(CRC32_PRECALC_EX(CRC32_PRECALC_EX( \
			CRC32_PRECALC_EX(CRC32_PRECALC_EX(CRC32_PRECALC_EX(byte)))))))))

/* Simple wrapper of CRC32_PRECALC_E that converts everything to uint32_t */
#define CRC32_PRECALC(byte) CRC32_PRECALC_E((uint32_t)(byte))

#define CRC32_PRECALC_0(byte) CRC32_PRECALC(byte), CRC32_PRECALC((byte) | 0x01)

#define CRC32_PRECALC_1(byte) \
	CRC32_PRECALC_0(byte), CRC32_PRECALC_0((byte) | UINT32_C(0x02))

#define CRC32_PRECALC_2(byte) \
	CRC32_PRECALC_1(byte), CRC32_PRECALC_1((byte) | UINT32_C(0x04))

#define CRC32_PRECALC_3(byte) \
	CRC32_PRECALC_2(byte), CRC32_PRECALC_2((byte) | UINT32_C(0x08))

#define CRC32_PRECALC_4(byte) \
	CRC32_PRECALC_3(byte), CRC32_PRECALC_3((byte) | UINT32_C(0x10))

#define CRC32_PRECALC_5(byte) \
	CRC32_PRECALC_4(byte), CRC32_PRECALC_4((byte) | UINT32_C(0x20))

#define CRC32_PRECALC_6(byte) \
	CRC32_PRECALC_5(byte), CRC32_PRECALC_5((byte) | UINT32_C(0x40))

#define CRC32_PRECALC_7(byte) \
	CRC32_PRECALC_6(byte), CRC32_PRECALC_6((byte) | UINT32_C(0x80))

static const uint32_t crc32_tab[256] = {CRC32_PRECALC_7(0)};

#undef CRC32_MASK
#undef CRC32_PRECALC_EX
#undef CRC32_PRECALC_E
#undef CRC32_PRECALC
#undef CRC32_PRECALC_0
#undef CRC32_PRECALC_1
#undef CRC32_PRECALC_2
#undef CRC32_PRECALC_3
#undef CRC32_PRECALC_4
#undef CRC32_PRECALC_5
#undef CRC32_PRECALC_6
#undef CRC32_PRECALC_7

static uint32_t crc32_c(uint32_t crc, const unsigned char *message, size_t sz)
{
	while (sz--)
		crc = (crc >> 8) ^ crc32_tab[(crc ^ *message++) & 0xFF];

	return crc;
}

/* tiny crc implementation */
uint32_t yukino_crc32(uint32_t crc, const unsigned char *message, size_t sz)
{
#ifdef __GNUC__
	/* abuse gcc aliasing shit */

	if (sz >= 4) {
		size_t msz;

		/* Align to dword offset */
		msz = ((uintptr_t)message & 3);

		crc = crc32_c(crc, message, msz);

		message += msz;
		sz -= msz;

		for (; sz >= 4; sz -= 4, message += 4) {
			crc ^= *(__attribute__((__may_alias__)) uint32_t *)message;

			crc = (crc >> 8) ^ crc32_tab[crc & 0xFF];
			crc = (crc >> 8) ^ crc32_tab[crc & 0xFF];
			crc = (crc >> 8) ^ crc32_tab[crc & 0xFF];
			crc = (crc >> 8) ^ crc32_tab[crc & 0xFF];
		}
	}
#endif

	return crc32_c(crc, message, sz);
}
