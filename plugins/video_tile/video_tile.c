#include "../../tile_api.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/*
compile: 
gcc -fPIC -shared -o plugins/video_tile/video_tile.so   plugins/video_tile/video_tile.c   `sdl2-config --cflags --libs` -lSDL2_ttf -lSDL2_image -lm
*/

typedef struct {
    TileContext *ctx;
    char plugin_dir[512];
    void *vid;
} State;

//c moment
static const char* tile_name(void) { return "Video Tile"; }

//here's our function to actually load the video
static void open_video(State *s) {
    if (!s || s[0].vid) return;

    char path[1024];
    //path to the video, here it's just media.mp4, so basically just a default
    snprintf(path, sizeof(path), "%s/media.mp4", s[0].plugin_dir);

    if (s[0].ctx[0].media && s[0].ctx[0].media[0].video_open) {
        fprintf(stderr, "Video Tile: opening %s\n", path);
        s[0].vid = s[0].ctx[0].media[0].video_open(s[0].ctx, path, /*loop=*/1, /*with_audio=*/1);
        fprintf(stderr, "Video Tile: video_open returned %p\n", s[0].vid);
        if (!s[0].vid) fprintf(stderr, "Video Tile: failed to open %s\n", path);
    } else {
        fprintf(stderr, "Video Tile: media API missing (ctx[0].media=%p)\n", (void*)s[0].ctx[0].media);
    }
}

//if everything exists, close it 
static void close_video(State *s) {
    if (!s || !s[0].vid) return;
    if (s[0].ctx[0].media && s[0].ctx[0].media[0].video_close) {
        s[0].ctx[0].media[0].video_close(s[0].ctx, s[0].vid);
    }
    s[0].vid = NULL;
}

//create the tile
static void* tile_create(TileContext *ctx, const char *plugin_dir) {
    //we just set what we know to the state
    State *s = (State*)SDL_calloc(1, sizeof(State));
    s[0].ctx = ctx;
    strncpy(s[0].plugin_dir, plugin_dir, sizeof(s[0].plugin_dir) - 1);
    s[0].vid = NULL;
    return s;
}

//easy peasy way to get rid of the tile
static void tile_destroy(void *st) {
    State *s = (State*)st;
    if (!s) return;
    close_video(s);
    SDL_free(s);
}

//This is run on start, includes debug rn
static void tile_on_show(void *st) {
    State *s = (State*)st;
    open_video(s);
    fprintf(stderr, "Video Tile: on_show state=%p\n", st);
    fprintf(stderr, "Video Tile: on_hide state=%p\n", st);
}

//This is run when it's not the tile's turn to go, it make sure the video isnt in the background
static void tile_on_hide(void *st) {
    State *s = st;
    if (!s) return;
    if (s[0].vid && s[0].ctx[0].media && s[0].ctx[0].media[0].video_close) {
        s[0].ctx[0].media[0].video_close(s[0].ctx, s[0].vid);
        s[0].vid = NULL;
    }
}

//if everything exists, update the tile
static void tile_update(void *st, double dt) {
    State *s = (State*)st;
    if (s && s[0].vid && s[0].ctx[0].media && s[0].ctx[0].media[0].video_update) {
        s[0].ctx[0].media[0].video_update(s[0].ctx, s[0].vid, dt);
    }
}

//this is what we use to actually render the tile
static void tile_render(void *st, SDL_Renderer *r, const SDL_Rect *rect) {
    State *s = (State*)st;

    //draw a border so you can see the tile region even during failures
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, rect);

    if (!s || !s[0].vid || !s[0].ctx[0].media || !s[0].ctx[0].media[0].video_draw) return;

    //draw the video to the tile's constraints
    s[0].ctx[0].media[0].video_draw(s[0].ctx, s[0].vid, r, rect, /*keep_aspect=*/0, /*cover=*/0);
}

//requested duration of 10s
static double tile_duration(void) { return 10.0; }

//this is what we're passing through
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