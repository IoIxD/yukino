/*
 * yukino -- portable screenshot utility
 * Copyright (C) 2026 Paper
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YUKINO_LAYER_SHELL_H_
#define YUKINO_LAYER_SHELL_H_

#include <SDL3/SDL.h>

/* nonzero if SDL is running on wayland */
int layer_shell_available(void);

/* window must've been created with
 * SDL_PROP_WINDOW_CREATE_WAYLAND_SURFACE_ROLE_CUSTOM_BOOLEAN.
 * w and h are logical (not pixel) sizes. */
int layer_shell_attach(SDL_Window *win, int w, int h);
void layer_shell_detach(void);

#endif
