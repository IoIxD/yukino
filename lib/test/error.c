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

/* FIXME allow creating a custom backend for tests, so this won't be a no-op on
 * systems without a graphical environment */

#include <stdio.h>
#include <string.h>

int main(void)
{
	yukino_connection_t *s;
	yukino_result_t r, rr;
	const char *str;

	if (yukino_connect(&s) < 0)
		return 0; /* eh okay */

	if (yukino_error_register(s, "com.Balls", "deez nuts", &r) < 0)
		return 1;
	if (yukino_error_register(s, "com.Balls", "wow", &rr) < 0)
		return 2;
	/* Errors with identical names should be merged */
	if (rr != r)
		return 3;
	if (yukino_error_register(s, "com.Balls2", "deez", &rr) < 0)
		return 4;
	/* Errors with different names should have unique codes */
	if (rr == r)
		return 5;

	/* Error names should be retrievable after the fact */
	str = yukino_error_name(s, r);
	if (!str)
		return 6;
	if (strcmp(str, "com.Balls"))
		return 7;
	str = yukino_error_name(s, rr);
	if (!str)
		return 8;
	if (strcmp(str, "com.Balls2"))
		return 9;

	/* Error descriptions should be retrievable after the fact */
	str = yukino_error_description(s, r);
	if (!str)
		return 10;
	/* Only the first description is used, any after are discarded. */
	if (strcmp(str, "deez nuts"))
		return 11;
	str = yukino_error_description(s, rr);
	if (!str)
		return 12;
	if (strcmp(str, "deez"))
		return 13;

	yukino_disconnect(s);

	return 0;
}
