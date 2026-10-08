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

#include <string.h>
#include <stdio.h>

static int check(const char *s, size_t n, uint32_t expect)
{
	struct yukino_adler32 a32;

	yukino_adler32_init(&a32);

	yukino_adler32(&a32, "12345", 5);

	return (yukino_adler32_get(&a32) != expect);
}

int main(void)
{
	if (check("12345", 5, 49807616))
		return 1;
	return 0;
}
