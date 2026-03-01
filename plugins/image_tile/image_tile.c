#include "../../tile_api.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/*
compile

gcc -fPIC -shared -o plugins/image_tile/image_tile.so   plugins/image_tile/image_tile.c   `sdl2-config --cflags --libs` -lSDL2_ttf -lSDL2_image -lm

*/

typedef struct {
    TileContext *ctx;
    char plugin_dir[512];
    void *img;
} State;

static const char* tile_name(void) { return "Image Tile"; }

//This is just the function to load the image
static void open_image(State *s) {
    if (!s || s[0].img) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/image.png", s[0].plugin_dir);

    if (s[0].ctx[0].media && s[0].ctx[0].media[0].image_load) {
        fprintf(stderr, "Image Tile: loading %s\n", path);
        s[0].img = s[0].ctx[0].media[0].image_load(s[0].ctx, path);
        fprintf(stderr, "Image Tile: image_load returned %p\n", s[0].img);
        if (!s[0].img) fprintf(stderr, "Image Tile: failed to load %s\n", path);
    } else {
        fprintf(stderr, "Image Tile: media API missing (ctx[0].media=%p)\n", (void*)s[0].ctx[0].media);
    }
}
//and this is the one to close it
static void close_image(State *s) {
    if (!s || !s[0].img) return;

    if (s[0].ctx[0].media && s[0].ctx[0].media[0].image_free) {
        s[0].ctx[0].media[0].image_free(s[0].ctx, s[0].img);
    }
    s[0].img = NULL;
}

//we init our tile
static void* tile_create(TileContext *ctx, const char *plugin_dir) {
    State *s = (State*)SDL_calloc(1, sizeof(State));
    s[0].ctx = ctx;
    strncpy(s[0].plugin_dir, plugin_dir, sizeof(s[0].plugin_dir) - 1);
    s[0].img = NULL;
    return s;
}

//freeing tile
static void tile_destroy(void *st) {
    State *s = (State*)st;
    if (!s) return;
    close_image(s);
    SDL_free(s);
}

//this is the function that is run when it's our turn to display
static void tile_on_show(void *st) {
    State *s = (State*)st;
    open_image(s);
}

//this is run when we are done displaying
static void tile_on_hide(void *st) {
    State *s = (State*)st;
    close_image(s);
}

//this is a required function that we don't use
static void tile_update(void *st, double dt) {
    (void)st;
    (void)dt;
    /* nothing to update */
}

//here's how we actually display the image
static void tile_render(void *st, SDL_Renderer *r, const SDL_Rect *rect) {
    State *s = (State*)st;


    //as long as everything exists, draw it
    if (!s || !s[0].img || !s[0].ctx[0].media || !s[0].ctx[0].media[0].image_draw) return;
    s[0].ctx[0].media[0].image_draw(s[0].ctx, s[0].img, r, rect, /*keep_aspect=*/1, /*cover=*/0);
}

//requested duration of 10 seconds
static double tile_duration(void) { return 10.0; }

//here's the struct we're actually going to pass through
//we just pass through our functions that we made
static const Tile TILE = {
    .api_version = TILE_API_VERSION,
    .name = tile_name,
    .create = tile_create, 
    .destroy = tile_destroy,
    .update = tile_update,
    .on_show = tile_on_show,
    .on_hide = tile_on_hide,
    .render = tile_render,
    .preferred_duration = tile_duration
};

__attribute__((visibility("default")))
const Tile* tile_get(void) { return &TILE; }