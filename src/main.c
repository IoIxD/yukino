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

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "yukino.h"

#include <limits.h>
#include <stdio.h>

#include <getopt.h>

/* ------------------------------------------------------------------------ */
/* grab the whole screen and shove it into a SDL_Surface */

struct sdl_take_pixel {
	SDL_Surface *sur;
	int x, y;
};

static yukino_result_t sdl_take_pixel(void *userdata, unsigned char rgb[3])
{
	struct sdl_take_pixel *sur = userdata;
	uint32_t *px;

	if (sur->x == sur->sur->w) {
		sur->y++;
		sur->x = 0;
	}

	px = (uint32_t *)((char *)sur->sur->pixels + (sur->sur->pitch * sur->y))
	     + sur->x;

	*px = 0xFF000000 | ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8)
	      | rgb[0];

	sur->x++;
	return YUKINO_RESULT_OK;
}

static SDL_Surface *sdl_screenshot(yukino_connection_t *conn, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h)
{
	struct sdl_take_pixel s;

	s.sur = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
	if (!s.sur)
		return NULL;

	s.x = s.y = 0;

	if (yukino_screenshot(conn, x, y, w, h, sdl_take_pixel, &s) < 0) {
		SDL_DestroySurface(s.sur);
		return NULL;
	}

	return s.sur;
}

/* Takes a screenshot of the whole display */
static SDL_Surface *sdl_screenshot_display(yukino_connection_t *conn)
{
	yukino_result_t r;
	uint32_t w, h;

	if ((r = yukino_display_resolution(conn, &w, &h)) < 0)
		return NULL;

	return sdl_screenshot(conn, 0, 0, w, h);
}

/* Works out where the primary display is. */
static yukino_result_t primary_display_rect(SDL_Rect *bounds, yukino_rect_t *pr)
{
	SDL_DisplayID *disp, primary;
	int count, i;
	int x0 = INT_MAX, y0 = INT_MAX;
	float density = 1.0f;

	primary = SDL_GetPrimaryDisplay();
	if (!primary || !SDL_GetDisplayBounds(primary, bounds))
		return YUKINO_RESULT_UNSUPPORTED;

	disp = SDL_GetDisplays(&count);
	if (!disp)
		return YUKINO_RESULT_UNSUPPORTED;

	/* the desktop origin, and the same density the backends use to size
	 * the whole-desktop screenshot */
	for (i = 0; i < count; i++) {
		const SDL_DisplayMode *mode;
		SDL_Rect r;

		if (!SDL_GetDisplayBounds(disp[i], &r))
			continue;

		if (r.x < x0)
			x0 = r.x;
		if (r.y < y0)
			y0 = r.y;

		mode = SDL_GetCurrentDisplayMode(disp[i]);
		if (mode && mode->pixel_density > density)
			density = mode->pixel_density;
	}

	SDL_free(disp);

	pr->x = SDL_lroundf((bounds->x - x0) * density);
	pr->y = SDL_lroundf((bounds->y - y0) * density);
	pr->w = SDL_lroundf(bounds->w * density);
	pr->h = SDL_lroundf(bounds->h * density);

	return YUKINO_RESULT_OK;
}

/* Takes a screenshot of the primary display */
static SDL_Surface *sdl_screenshot_primary(
	yukino_connection_t *conn, SDL_Rect *bounds, yukino_rect_t *pr)
{
	const SDL_DisplayMode *mode;
	SDL_Surface *sur, *scaled;
	float density = 1.0f;
	int w, h;

	if (primary_display_rect(bounds, pr) < 0)
		return NULL;

	sur = sdl_screenshot(conn, pr->x, pr->y, pr->w, pr->h);
	if (!sur)
		return NULL;

	/* The screenshot is at the scale of the highest density display, which
	 * may not be the primary one. Scale it to the primary display's own
	 * pixel size, so that texture pixels match window pixels. */
	mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
	if (mode && mode->pixel_density > 0.0f)
		density = mode->pixel_density;

	w = SDL_lroundf(bounds->w * density);
	h = SDL_lroundf(bounds->h * density);

	if (sur->w == w && sur->h == h)
		return sur;

	scaled = SDL_ScaleSurface(sur, w, h, SDL_SCALEMODE_LINEAR);
	SDL_DestroySurface(sur);

	return scaled;
}

/* ------------------------------------------------------------------------ */
/* save a portion of an SDL_Surface into a .png (or, really anything) file */

static yukino_result_t sdl_take_cb(void *conn, uint32_t x, uint32_t y,
	uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func, void *userdata)
{
	SDL_Surface *sur = conn;
	uint32_t *pixels;
	uint32_t i, j;
	yukino_result_t r;

	/* XXX need to make sure width/height are ok */
	pixels = sur->pixels;

	/* go go go */
	pixels += x;
	pixels = (uint32_t *)((char *)pixels + (y * sur->pitch));

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			unsigned char rgb[3];

			rgb[2] = pixels[j] >> 16;
			rgb[1] = pixels[j] >> 8;
			rgb[0] = pixels[j];

			if ((r = pixel_func(userdata, rgb)) < 0)
				return r;
		}
		pixels = (uint32_t *)((char *)pixels + sur->pitch);
	}

	return YUKINO_RESULT_OK;
}

static yukino_result_t stdio_write_cb(void *opaque, const void *data, size_t sz)
{
	if (fwrite(data, 1, sz, opaque) != sz)
		return YUKINO_RESULT_FILE_ERROR;
	return YUKINO_RESULT_OK;
}

static void sdl_write_surface_to_png(
	const char *f, SDL_Surface *sur, const yukino_rect_t *rect)
{
	FILE *fp;

	fp = fopen(f, "wb");
	if (!fp)
		return; /* oops */

	yukino_write_png(rect->x, rect->y, rect->w, rect->h, stdio_write_cb, fp,
		sdl_take_cb, sur);

	fclose(fp);
}

/* ------------------------------------------------------------------------ */
/* file dialog */

struct dialog_cb {
	/* Only need the pointer */
	char *file;

	/* Gets signaled when the dialog callback is finished */
	SDL_Semaphore *sem;
};

static void dialog_cb(void *userdata, const char *const *filelist, int filter)
{
	volatile struct dialog_cb *c = (volatile struct dialog_cb *)userdata;

	if (filelist && *filelist)
		c->file = strdup(filelist[0]);

	SDL_SignalSemaphore(c->sem);
}

/* ------------------------------------------------------------------------ */
/* window shit */

/* window lookup ... */
struct window {
	yukino_window_t win;

	unsigned int have_border : 1;

	/* 'border' is 'rect' with WM borders applied */
	yukino_rect_t rect;
	yukino_rect_t border;
};

static struct window *windows;
static size_t windows_size;
static size_t windows_alloc;

static void windows_fill(yukino_connection_t *conn)
{
	yukino_window_iter_t *wi;
	yukino_window_t win;

	if (yukino_window_iter_start(conn, NULL, &wi) < 0)
		return;

	while (yukino_window_iter(conn, wi, &win) == YUKINO_RESULT_OK) {
		struct window *w;

		/* allocate more space? */
		if (windows_size >= windows_alloc) {
			void *old = windows;

			windows_alloc
				= (windows_alloc) ? (windows_alloc * 2) : 16;
			windows = realloc(
				windows, windows_alloc * sizeof(*windows));

			if (!windows) {
				free(old);
				windows = NULL;
				windows_alloc = windows_size = 0;
				return;
			}
		}

		w = &windows[windows_size];

		if (yukino_window_position(conn, win, &w->rect) < 0)
			continue; /* ??? */

		if (yukino_window_decorated_position(conn, win, &w->border)
			>= 0)
			w->have_border = 1;
		w->win = win;

		windows_size++;
	}

	yukino_window_iter_end(conn, wi);
}

static yukino_result_t windows_query_at_point(
	int32_t x, int32_t y, yukino_rect_t *pr)
{
	/* there may be no window under the pointer -- in that case return
	 * YUKINO_RESULT_NONE */
	yukino_result_t r;
	size_t i;

	r = YUKINO_RESULT_NONE;

	for (i = 0; i < windows_size; i++) {
		struct window *win = &windows[i];

		/* check if our coordinates are inside the window */
		if (yukino_rect_has_point(&win->rect, x, y)) {
			*pr = win->rect;
			r = YUKINO_RESULT_OK;
		} else if (yukino_rect_has_point(&win->border, x, y)) {
			*pr = win->border;
			r = YUKINO_RESULT_OK;
		}
	}

	return r;
}

/* ------------------------------------------------------------------------ */

static void fixup_xw(float *x, float *w, uint32_t m)
{
	if (*x < 0) {
		*w += *x;
		*x = 0;
	}

	if (*x + *w > m)
		*w = m - *x;
}

static void fixup(SDL_FRect *rect, SDL_Surface *sur)
{
	fixup_xw(&rect->x, &rect->w, sur->w);
	fixup_xw(&rect->y, &rect->h, sur->h);
}

/* for scaling points into pixels */
static void points_to_pixels(
	yukino_rect_t *out, const SDL_FRect *in, float density)
{
	out->x = SDL_lroundf(in->x * density);
	out->y = SDL_lroundf(in->y * density);
	out->w = SDL_lroundf(in->w * density);
	out->h = SDL_lroundf(in->h * density);
}

static void pixels_to_points(
	SDL_FRect *out, const yukino_rect_t *in, float density)
{
	out->x = in->x / density;
	out->y = in->y / density;
	out->w = in->w / density;
	out->h = in->h / density;
}

static int get_num_displays(void)
{
	SDL_DisplayID *disp;
	int r;

	disp = SDL_GetDisplays(&r);
	if (!disp)
		return -1;

	free(disp);
	return r;
}

int main(int argc, char *argv[])
{
	SDL_Surface *sur;
	SDL_Texture *tex;
	SDL_Window *win;
	SDL_Renderer *ren;
	SDL_Event ev;
	SDL_Cursor *cur;
	SDL_FRect sel;
	/* Mouse down, drag */
	enum { POINTS_DOWN, POINTS_DRAG, POINTS_MAX_ };
	SDL_FPoint points[POINTS_MAX_];
	int down = 0, drag = 0;
	char *file = NULL; /* output file */
	int opt;
	static struct option long_opts[] = {
		{"output", required_argument, 0, 'o'},
		{0},
	};
	int esc = 0;
	float density;
	bool under_wayland = (getenv("WAYLAND_DISPLAY") != NULL);
	SDL_Rect mon_bounds;
	yukino_rect_t mon;

	/* parse command line opts */
	while ((opt = getopt_long(argc, argv, "o:", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'o':
			/* output file */
			file = strdup(optarg);
			break;
		default:
			fprintf(stderr, "usage: %s [-o output.png]\n", argv[0]);
			return 1;
		}
	}

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	{
		yukino_connection_t *conn;

		if (yukino_connect(&conn) < 0)
			return 1; /* oops */

		/* Doesn't matter if locking and unlocking fails..
		 * It's only here in an attempt to maintain state */
		yukino_lock(conn);

		windows_fill(conn);

		if (under_wayland) {
			sur = sdl_screenshot_primary(conn, &mon_bounds, &mon);
		} else {
			sur = sdl_screenshot_display(conn);
		}

		yukino_unlock(conn);

		yukino_disconnect(conn);
	}

	if (!sur)
		return 1;

	{
		SDL_PropertiesID props = SDL_CreateProperties();

		SDL_SetNumberProperty(
			props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, sur->h);
		SDL_SetNumberProperty(
			props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, sur->w);
		SDL_SetBooleanProperty(
			props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
		SDL_SetBooleanProperty(props,
			SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, true);
		SDL_SetBooleanProperty(
			props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
		SDL_SetBooleanProperty(props,
			SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,
			true);
		SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
			under_wayland ? mon_bounds.x : 0);
		SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
			under_wayland ? mon_bounds.y : 0);

		/* On MATE, if there is only one display, the top bar will push
		 * the window down. Bypass that by setting the window to
		 * fullscreen.
		 *
		 * We explicitly DON'T do this for multi-display desktops,
		 * because that ends up having the window on only one display,
		 * which is Not What We Want
		 *
		 * we also do this on wayland where we only work with the
		 * primary display
		 * */
		if (under_wayland || get_num_displays() == 1) {
			SDL_SetBooleanProperty(props,
				SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
				true);
		}

		win = SDL_CreateWindowWithProperties(props);

		SDL_DestroyProperties(props);
	}

	if (!win)
		goto end;

	ren = SDL_CreateRenderer(win, NULL);

	if (!ren)
		goto end;

	density = SDL_GetWindowPixelDensity(win);

	tex = SDL_CreateTextureFromSurface(ren, sur);

	cur = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
	SDL_SetCursor(cur);

	do {
		float mx, my;
		yukino_rect_t w;
		int query = 0;

		SDL_GetMouseState(&mx, &my);

		/* ugh */
		mx *= density;
		my *= density;

		if (under_wayland) {
			query = windows_query_at_point(
				mx + mon.x, my + mon.y, &w);
		} else {
			query = windows_query_at_point(mx, my, &w);
		}

		/* Adjust selection */
		if (drag) {
			/* Lol wow SDL has a function for this */
			SDL_GetRectEnclosingPointsFloat(
				points, POINTS_MAX_, NULL, &sel);
		} else if (query == YUKINO_RESULT_OK) {
			w.x -= mon.x;
			w.y -= mon.y;
			pixels_to_points(&sel, &w, density);
		} else {
			/* Otherwise the "selection" is the window beneath the
			 * cursor. */
			sel.x = sel.y = 0;
			sel.w = sur->w / density;
			sel.h = sur->h / density;
		}

		/* crop any out-of-bounds selections (can happen if a window is
		 * partially offscreen) */
		fixup(&sel, sur);

		/* now we begin our blitting journey */
		SDL_RenderClear(ren);

		/* Apparently not supported everywhere... we'd need to store a
		 * separate texture thats at half visible if we want to support
		 * those platforms */
		SDL_SetTextureColorMod(tex, 127, 127, 127);
		SDL_RenderTexture(ren, tex, NULL, NULL);

		SDL_SetTextureColorMod(tex, 255, 255, 255);
		SDL_RenderTexture(ren, tex, &sel, &sel);

		SDL_RenderPresent(ren);

		/* When we create our window, it's hidden, to avoid showing a
		 * huge blank window on startup. Now we want to show the window
		 * since it's done rendering. */
		SDL_ShowWindow(win);

		switch (ev.type) {
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			down = 1;
			points[POINTS_DOWN].x = ev.button.x;
			points[POINTS_DOWN].y = ev.button.y;
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if (down)
				drag = 1;
			points[POINTS_DRAG].x = ev.motion.x;
			points[POINTS_DRAG].y = ev.motion.y;
			break;
		case SDL_EVENT_KEY_DOWN:
			if (ev.key.key == SDLK_ESCAPE) {
				esc = 1;
				goto out;
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP: goto out;
		}
	} while (SDL_WaitEvent(&ev));

out:
	/* We're done here, and hopefully we have a selection (right?)
	 * So, take what we have, and shove it into the png writer */

	/* No longer need any of this */
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_DestroyCursor(cur);

	/* User wants to exit out, so do it */
	if (esc)
		goto end;

	if (!file) {
		/* Ask the user where to save the damn file */
		volatile struct dialog_cb c;
		c.sem = SDL_CreateSemaphore(0);
		c.file = NULL;

		static const SDL_DialogFileFilter filters[] = {
			{"PNG (Portable Network Graphics)", "png"},
			{"All files",                       "*"  }
                };

		SDL_ShowSaveFileDialog(dialog_cb, (void *)&c, NULL, filters,
			SDL_arraysize(filters), NULL);

		/* Wait until the semaphore is signaled
		 * ...but we still need to handle events */
		while (!SDL_WaitSemaphoreTimeout(c.sem, 10))
			SDL_PumpEvents();

		SDL_DestroySemaphore(c.sem);

		file = c.file;
	}

	/* Save it */
	if (file) {
		yukino_rect_t w;
		points_to_pixels(&w, &sel, density);
		sdl_write_surface_to_png(file, sur, &w);
		free(file);
	}

end:
	SDL_DestroySurface(sur);
	return 0;
}
