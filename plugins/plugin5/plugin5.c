#include "../../tile_api.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*

compile commmand:
gcc -fPIC -shared -o plugins/plugin5/plugin5.so \
  plugins/plugin5/plugin5.c \
  `sdl2-config --cflags --libs` -lSDL2_ttf -lSDL2_image -lm

*/

typedef struct {
    TileContext *ctx; //borrowed
    char plugin_dir[512]; //where state.json lives
    int prime;
    char last_date[16]; //"YYYY-MM-DD"

    int a; //random element in {1..p-1} for current display
    int inv; //inverse of a mod p
    int has_choice;

    SDL_Texture *t1; int t1w, t1h;
    SDL_Texture *t2; int t2w, t2h;
    SDL_Texture *t3; int t3w, t3h;
    SDL_Texture *t4; int t4w, t4h;
} State;


//This is important, it makes our char*s into textures
static SDL_Texture* make_text(SDL_Renderer *r, TTF_Font *font,
                              const char *msg, int *w, int *h) {
    SDL_Color white = {255,255,255,255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, msg, white);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    *w = surf[0].w; *h = surf[0].h;
    SDL_FreeSurface(surf);
    return tex;
}

//Free textures
static void free_texts(State *s) {
    if (s[0].t1) SDL_DestroyTexture(s[0].t1);
    if (s[0].t2) SDL_DestroyTexture(s[0].t2);
    if (s[0].t3) SDL_DestroyTexture(s[0].t3);
    if (s[0].t4) SDL_DestroyTexture(s[0].t4);
    s[0].t1=s[0].t2=s[0].t3=s[0].t4=NULL;
}

//Rebuild on reload
static void rebuild_texts(State *s) {
    free_texts(s);

    SDL_Renderer *r = s[0].ctx[0].renderer;

    char line1[128];
    char line2[128];

    snprintf(line1, sizeof(line1), "Plugin 5");
    snprintf(line2, sizeof(line2), "I need 5 plugins");


    s[0].t1 = make_text(r, s[0].ctx[0].font_medium, line1, &s[0].t1w, &s[0].t1h);
    s[0].t2 = make_text(r, s[0].ctx[0].font_small,  line2, &s[0].t2w, &s[0].t2h);
}

static const char* tile_name(void) { return "Group of the Day"; }

static void* tile_create(TileContext *ctx, const char *plugin_dir) {
    State *s = (State*)SDL_calloc(1, sizeof(State));
    s[0].ctx = ctx;
    strncpy(s[0].plugin_dir, plugin_dir, sizeof(s[0].plugin_dir)-1);



    s[0].has_choice = 0;
    rebuild_texts(s);
    return s;
}

static void tile_destroy(void *state) {
    State *s = (State*)state;
    free_texts(s);
    SDL_free(s);
}

static void tile_update(void *state, double dt) {
    (void)dt;
    //Nothing time-animated in this tile right now.
}




static void tile_render(void *state, SDL_Renderer *r, const SDL_Rect *rect) {
    State *s = (State*)state;

    //use a soft panel background so its distinguished from the main background
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 70);
    SDL_RenderFillRect(r, rect);

    //thin border
    SDL_SetRenderDrawColor(r, 255, 255, 255, 90);
    SDL_RenderDrawRect(r, rect);

    //simple layout
    int pad = rect[0].w / 18;
    int x = rect[0].x + pad;
    int y = rect[0].y + pad;

    if (s[0].t1) { SDL_Rect d = {x, y, s[0].t1w, s[0].t1h}; SDL_RenderCopy(r, s[0].t1, NULL, &d); y += s[0].t1h + pad/2; }
    if (s[0].t2) { SDL_Rect d = {x, y, s[0].t2w, s[0].t2h}; SDL_RenderCopy(r, s[0].t2, NULL, &d); y += s[0].t2h + pad/3; }
    if (s[0].t3) { SDL_Rect d = {x, y, s[0].t3w, s[0].t3h}; SDL_RenderCopy(r, s[0].t3, NULL, &d); y += s[0].t3h + pad/3; }
    if (s[0].t4) { SDL_Rect d = {x, y, s[0].t4w, s[0].t4h}; SDL_RenderCopy(r, s[0].t4, NULL, &d); }
}

//8 seconds is plenty long
static double tile_pref_duration(void) { return 8.0; }

//here's the thing we care about
static Tile TILE = {
    .api_version = TILE_API_VERSION,
    .name = tile_name,
    .create = tile_create,
    .destroy = tile_destroy,
    .update = tile_update,
    .render = tile_render,
    .preferred_duration = tile_pref_duration,
    .on_hide = NULL,
};

//give the tile to the main thing
const Tile* tile_get(void) { return &TILE; }