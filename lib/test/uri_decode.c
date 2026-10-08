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

static int check(const char *start, const char *expect)
{
	unsigned char *s;

	if (yukino_uri_decode(start, strlen(start), &s, NULL) < 0)
		return 1;
	if (strcmp((char *)s, expect) != 0)
		return 2;

	return 0;
}

int main(void)
{
	if (check("12345", "12345"))
		return 1;
	if (check("one%25two%20%50", "one\x25two\x20\x50"))
		return 1;
	return 0;
}
