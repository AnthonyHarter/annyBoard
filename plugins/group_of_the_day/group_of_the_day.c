#include "../../tile_api.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*
This is a simple simple plugin, ignore the math I use
it's purpose is just to display some text on the screen
you'll be able to figure it out just fine, don't worry

compile commmand:
gcc -fPIC -shared -o plugins/group_of_the_day/group_of_the_day.so \
  plugins/group_of_the_day/group_of_the_day.c \
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

//easy prime checker
static int is_prime_int(int n) {
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; d * d <= n; d += 2) {
        if (n % d == 0) return 0;
    }
    return 1;
}

static int random_prime_under_10000(void) {
    //keep trying random odd numbers until prime. Fast enough for <10000.
    for (;;) {
        int n = (rand() % 10000);
        if (n < 2) continue;
        if ((n & 1) == 0) n++;
        if (n >= 10000) n = 9999;
        if (is_prime_int(n)) return n;
    }
}

//euclidean algorithm
static int egcd(int a, int b, int *x, int *y) {
    if (b == 0) { *x = 1; *y = 0; return a; }
    int x1=0, y1=0;
    int g = egcd(b, a % b, &x1, &y1);
    *x = y1;
    *y = x1 - (a / b) * y1;
    return g;
}

//find the modular inverse
static int modinv(int a, int p) {
    int x=0, y=0;
    int g = egcd(a, p, &x, &y);
    if (g != 1) return 0; //for prime p and 1<=a<p, gcd is 1 always
    int inv = x % p;
    if (inv < 0) inv += p;
    return inv;
}

//getdate
static void today_yyyy_mm_dd(char out[16]) {
    time_t raw = time(NULL);
    struct tm lt;
    localtime_r(&raw, &lt);
    strftime(out, 16, "%Y-%m-%d", &lt);
}

//hardcode state.json
static void state_path(State *s, char out[1024]) {
    snprintf(out, 1024, "%s/state.json", s[0].plugin_dir);
}

//update state
static void write_state_json(State *s) {
    char path[1024];
    state_path(s, path);

    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "{\"date\":\"%s\",\"prime\":%d}\n", s[0].last_date, s[0].prime);
    fclose(f);
}

//read state
static void read_state_json(State *s) {
    //Defaults if file missing/corrupt
    s[0].prime = 7919;
    strcpy(s[0].last_date, "1970-01-01");

    char path[1024];
    state_path(s, path);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    buf[n] = 0;

    //very small parsing, find "date":"...." and "prime":number
    char *d = strstr(buf, "\"date\"");
    if (d) {
        char *q = strchr(d, '"'); //first "
        if (q) q = strchr(q+1, '"'); //after date key
        if (q) q = strchr(q+1, '"'); //opening quote of value
        if (q) {
            q++;
            char *end = strchr(q, '"');
            if (end && (end - q) < 15) {
                char tmp[16] = {0};
                memcpy(tmp, q, (size_t)(end - q));
                tmp[end - q] = 0;
                strncpy(s[0].last_date, tmp, sizeof(s[0].last_date)-1);
            }
        }
    }

    char *p = strstr(buf, "\"prime\"");
    if (p) {
        char *colon = strchr(p, ':');
        if (colon) {
            int val = atoi(colon + 1);
            if (val > 1 && val < 10000 && is_prime_int(val)) s[0].prime = val;
        }
    }
}

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
    char line3[128];
    char line4[128];

    snprintf(line1, sizeof(line1), "Group of the day!");
    snprintf(line2, sizeof(line2), "Z/%dZ (p is prime)", s[0].prime);

    if (s[0].has_choice) {
        snprintf(line3, sizeof(line3), "Picked a = %d (mod %d)", s[0].a, s[0].prime);
        snprintf(line4, sizeof(line4), "a^{-1} mod %d = %d", s[0].prime, s[0].inv);
    } else {
        snprintf(line3, sizeof(line3), "Picking element...");
        snprintf(line4, sizeof(line4), "");
    }

    s[0].t1 = make_text(r, s[0].ctx[0].font_medium, line1, &s[0].t1w, &s[0].t1h);
    s[0].t2 = make_text(r, s[0].ctx[0].font_small,  line2, &s[0].t2w, &s[0].t2h);
    s[0].t3 = make_text(r, s[0].ctx[0].font_small,  line3, &s[0].t3w, &s[0].t3h);
    s[0].t4 = make_text(r, s[0].ctx[0].font_small,  line4, &s[0].t4w, &s[0].t4h);
}

static const char* tile_name(void) { return "Group of the Day"; }

static void* tile_create(TileContext *ctx, const char *plugin_dir) {
    State *s = (State*)SDL_calloc(1, sizeof(State));
    s[0].ctx = ctx;
    strncpy(s[0].plugin_dir, plugin_dir, sizeof(s[0].plugin_dir)-1);

    //seed rand once per process-ish, so we mix time and pointer to get basic randomness
    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)s);

    read_state_json(s);

    // Check if date changed, if so, pick a new prime and write it.
    char today[16];
    today_yyyy_mm_dd(today);
    if (strcmp(today, s[0].last_date) != 0) {
        s[0].prime = random_prime_under_10000();
        strncpy(s[0].last_date, today, sizeof(s[0].last_date)-1);
        write_state_json(s);
    }

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
    // Nothing time-animated in this tile right now.
}

static void tile_on_show(void *state) {
    State *s = (State*)state;

    //choose a random a in {1..p-1} and compute inverse mod p
    if (s[0].prime > 2) {
        s[0].a = 1 + (rand() % (s[0].prime - 1));
        s[0].inv = modinv(s[0].a, s[0].prime);
        s[0].has_choice = 1;
        //reload text now that we have a new number
        rebuild_texts(s);
    }
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
    .api_version = 4,
    .name = tile_name,
    .create = tile_create,
    .destroy = tile_destroy,
    .update = tile_update,
    .on_show = tile_on_show,
    .render = tile_render,
    .preferred_duration = tile_pref_duration,
    .on_hide = NULL
};

//give the tile to the main thing
const Tile* tile_get(void) { return &TILE; }