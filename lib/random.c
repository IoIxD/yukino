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

#include <fcntl.h>
#include <unistd.h>

yukino_result_t yukino_random(void *x, size_t n)
{
	int fd;
	ssize_t nr;

	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	nr = read(fd, x, n);
	close(fd);
	if (nr != n)
		return -1;

	return 0;
}
