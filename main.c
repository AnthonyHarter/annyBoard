/*

    "anny" board (those who know :desolate:)

    Overview:

    -my beloved SDL2 handles graphics.
    -SDL_ttf handles text.
    -I made a custom plugin system, so we load corner tiles from ./tiles/*.so
    -I made the background to just be some low-resolution procedural noise texture
    thats upscaled
    -Github contributors are loaded from contributors.txt. Add yourself in a pr.
    -TRhe footer just scrolls horizontally.

    This is designed to run PURELY on linux/x11, MAYBE wayland


    and yeah, its just c. if you dont like that, go rewrite it in something else or something idk
    i thought about using so many worse languages (rust (stfu okay i know im a stereotype), zig (my beloved), pure javascrip[t] (hahahahahaha))
    so i dont wanna hear it. i barely enjoy coding at this point c is the only thing barely keeping me on board with the 
    major.

    if you think im going to write proper grammar comments too youre in for a TREAT today
    */
#define _GNU_SOURCE

#include <SDL2/SDL.h>        
#include <SDL2/SDL_ttf.h>    
#include <SDL2/SDL_image.h> 
#include <curl/curl.h>       

#include <dirent.h>          
#include <dlfcn.h>           
#include <stdio.h>           
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h> //IF NO ONE GOT ME I KNOW UINT32_T GOT ME
#include <stdlib.h>          

#include "tile_api.h"
#include "media.h"

//MODIFIABLE GLOBALS
#define WINDOW_TITLE "Shineman 425"

#define FONT_PATH "assets/font.ttf"

//these are the bakcground texture resolutions
#define BG_TEX_W 640
#define BG_TEX_H 360

//hehehehehehe 100000000000 tiles go brrrrrrrrrrrrrrrr silly pi DEFINITELY has memory for 1000000000 tiles!
#define MAX_TILES     128
#define MAX_CONTRIBS  256


//libcurl needs this for Reasons (basically it needs somewhere to put stuff it downloads into memory)
typedef struct {
    uint8_t *data;
    size_t   size;
} Memory;

//one contributor is displayed as username + label + avatar texture + pre-rendered label texture
typedef struct {
    char *username; //"Kenorbs"
    char *label; //"@ldecampo"

    SDL_Texture *avatar_tex; //avatar (may be NULL if download failed)
    int avatar_w, avatar_h; //avatar size

    SDL_Texture *label_tex; //outline label texture (which we cache for fast scrolling)
    int label_w, label_h; //label texture size
} Contributor;

//the full scrolling footer strip and its scroll state
//as in, this is the thing that displays the github shit
typedef struct {
    Contributor items[MAX_CONTRIBS];
    int count;

    int avatar_draw_px; //what we scale avatars to
    int gap_px; //spacing between contributors
    int pad_px; //spacing between avatar and text

    double scroll_px; //pixel offset (increases over time, dont worry about it)
    double scroll_speed; //px/sec

    int strip_w; //width for a single loop of all contributors
    int strip_h; //height for footer drawing 
} ContributorStrip;

//lkoad tile plugin + its instance state
typedef struct {
    void *dl_handle; //handle from dlopen()
    const Tile *api; //plugin table from tile_get()
    void *state; //plugin instance created by api[0].create()
    char path[512]; //path for debugging
    char plugin_dir[512]; //path for tile locations
} LoadedTile;

//tthe thing that cycles through tiles over time
typedef struct {
    LoadedTile tiles[MAX_TILES];
    int count; //number of loaded plugins
    int current; //index of active tile
    double t_in_tile; //seconds spent on current tile
    double global_duration; //fallback duration when a tile doesn't specify one
    int frozen; //if 1, dont update, if 0, update
} TileManager;


typedef struct {
    void *dl_handle;
    const Tile *api;
    char so_path[512];
    char plugin_dir[512];
} TilePlugin;

typedef struct {
    const TilePlugin *plug; //which plugin this slot is currently showing
    void *state; //slot-specific instance 
    double t_in_tile;
    double duration;
    int plugin_index; //index into plugins[] (for uniqueness checks)
} TileSlot;

typedef struct {
    TilePlugin plugins[MAX_TILES];
    int plugin_count;

    TileSlot slots[4]; //0=TL,1=TR,2=BL,3=BR
    double global_duration;
} TileSystem;

//clamp a double between 0 and 1
static double clamp01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

//literally just eat whitespace at the front of your char* (tangent time:
/* 
we are going to call "strings" char*s. in fact, that is the ONLY time you will find that word 
in this entire program. they are char*s. i dont want to hear it. this is c.)
*/

//eat any whitespace or position characters at the beginning of a char* and whitespace at the end of it
static char* str_trim(char *s) {
    while (*s && (*s==' ' || *s=='\t' || *s=='\r' || *s=='\n')) s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1]==' ' || end[-1]=='\t' || end[-1]=='\r' || end[-1]=='\n')) end--;
    *end = 0;
    return s;
}

//read first line of a file; fallback if missing/empty. because we like rust rules, ill note 
//that the caller owns the resulting string
static char* read_first_line(const char *path, const char *fallback) {
    FILE *f = fopen(path, "r"); //if file missing, use fallback
    if (!f) return strdup(fallback);

    char buf[1024];
    if (!fgets(buf, sizeof(buf), f)) { //empty file or read error
        fclose(f);
        return strdup(fallback);
    }
    fclose(f);

    char *t = str_trim(buf); 
    if (!*t) return strdup(fallback);
    return strdup(t);
}

//get username from github profile link (supports https://github.com/user and github.com/user)
//returns malloc'd username string or null if not parseable. (does not have ownership responsibility)
static char* github_username_from_link(const char *line) {
    const char *p = strstr(line, "github.com/");
    if (!p) return NULL;
    p += strlen("github.com/"); //move pointer to username start

    //go until we have what we want
    size_t n = 0;
    while (p[n] &&
           p[n] != '/' && p[n] != '?' && p[n] != '#' &&
           p[n] != ' ' && p[n] != '\t' && p[n] != '\r' && p[n] != '\n') {
        n++;
    }
    if (n == 0) return NULL;

    //we know how long our username is, so get ready to allocate
    char *u = (char*)malloc(n + 1);
    //copy the username to u
    memcpy(u, p, n);
    u[n] = 0;
    return u;
}

//build "@username" label string. (does not have ownership responsibility)
static char* make_label(const char *username) {
    size_t n = strlen(username);
    char *label = (char*)malloc(n + 2);
    label[0] = '@';
    memcpy(label + 1, username, n);
    label[n + 1] = 0;
    return label;
}


//very fast PRNG used only to make procedural texture; deterministic per run
//yeah its just a bunch of bit shifts with funny primes lmao
static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

//more hashing and fun number functions for background
static double hash01i(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u ^ seed;
    h = hash_u32(h);
    return (h & 0xFFFFFF) / (double)0x1000000; //0..1
}

static double lerp(double a, double b, double t) { return a + (b - a) * t; }

//smoothing function for nice smoothing (awesome comment)
static double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }

//since we're upscaling basically, we want to do it nicely when dealing with the noise  
static double value_noise(double x, double y, uint32_t seed) {
    int x0 = (int)floor(x);
    int y0 = (int)floor(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    double tx = x - (double)x0;
    double ty = y - (double)y0;

    double sx = smoothstep(tx);
    double sy = smoothstep(ty);

    double v00 = hash01i(x0, y0, seed);
    double v10 = hash01i(x1, y0, seed);
    double v01 = hash01i(x0, y1, seed);
    double v11 = hash01i(x1, y1, seed);

    double ix0 = lerp(v00, v10, sx);
    double ix1 = lerp(v01, v11, sx);
    return lerp(ix0, ix1, sy);
}

//epic function i literally stole from the internet LMAOOOO
static double fbm(double x, double y, uint32_t seed) {
    double sum = 0.0;
    double amp = 0.55;
    double freq = 1.0;

    sum += amp * value_noise(x * freq, y * freq, seed + (uint32_t)(1013));
    freq *= 2.0;
    amp *= 0.5;
    return sum;
}

//eender the background by filling a small texture and scaling it up (full ownership)
static void render_background(SDL_Renderer *r, SDL_Texture *bgTex,
                              int screen_w, int screen_h, double t_seconds) {
    void *pixels = NULL;
    int pitch = 0;
    if (SDL_LockTexture(bgTex, NULL, &pixels, &pitch) != 0) return;

    //this is how we control the morph of colors
    double morph = 0.5 + 0.5 * sin(t_seconds * 0.03);

    //this gives the "cloud"/"wave" background vibe
    double breath = 0.5 + 0.5 * sin(t_seconds * 0.13);


    //wowww blue and pink color woowwww so cool 
    //these are the two colors we're going to pulse between
    //if you fuck with these, i highly highly recommend soft, light colors
    double bR = 73,  bG = 219,  bB = 255; //blue
    double pR = 250, pG = 153,  pB = 255; //pink  

    //control clump size and motion speed.
    //larger scale means bigger blobs 
    double scale = 0.040;
    double speed = 0.03;
    

    //now we're going to change coordinates by a noisefield 
    //believe it or not i learned a lot of these tricks through minecraft minigame world shit
    //you use a ton of stuff like maps and gradient formulas and crazy stuff to make good randomized terrain
    uint32_t seedA = 0xA341316Cu;
    uint32_t seedB = 0xC8013EA4u;

    //loop through every pixel
    for (int y = 0; y < BG_TEX_H; y++) {
        uint32_t *row = (uint32_t*)((uint8_t*)pixels + y * pitch);

        for (int x = 0; x < BG_TEX_W; x++) {
            //figure out how far we are through looping through every pixel
            double fx = (double)x;
            double fy = (double)y;

            //calculate our speed and time into the units we like
            double tx = t_seconds * speed * 60.0;
            double ty = t_seconds * speed * 45.0;

            //get noise
            double nx = (fx * scale) + tx;
            double ny = (fy * scale) + ty;

            //we get a field to warp to (it makes noise)
            double warp = fbm(nx * 1.7, ny * 1.7, seedB);
            double wx = nx + (warp - 0.5) * 1.2;
            double wy = ny + (warp - 0.5) * 1.2;

            //we get a first field
            double v = fbm(wx, wy, seedA);

            //we get a second field because it generally looks good
            double v2 = fbm(wx * 1.9 + 12.3, wy * 1.9 - 7.1, seedA + 999);

            //combine the fields into one, with more weight on the first field. then clamp it
            double clump = 0.70 * v + 0.30 * v2;
            clump = clamp01(clump);

            //we increase blobness, then lower brightness kind of
            double blob = smoothstep(clump);
            blob = smoothstep(blob);

            //more pulsing 
            double pulse = 0.92 + 0.08 * breath;

            //shading is influenced by blob 
            //this is just a really fucking complicated way to make some spots lighter and darker
            double shade = 0.92 + (blob - 0.5) * 0.10;
            shade *= pulse;

            //blend colors together, then apply the shade we just made
            double R = lerp(bR, pR, morph);
            double G = lerp(bG, pG, morph);
            double B = lerp(bB, pB, morph);

            R = clamp01((R / 255.0) * shade) * 255.0;
            G = clamp01((G / 255.0) * shade) * 255.0;
            B = clamp01((B / 255.0) * shade) * 255.0;

            //add the pixel to our small image
            row[x] = (0xFFu << 24) | ((uint8_t)R << 16) | ((uint8_t)G << 8) | (uint8_t)B;
        }
    }

    //scale small image up to fit the screen
    SDL_UnlockTexture(bgTex);
    SDL_SetTextureScaleMode(bgTex, SDL_ScaleModeLinear);

    SDL_Rect dst = {0, 0, screen_w, screen_h};
    SDL_RenderCopy(r, bgTex, NULL, &dst);
}


//render text into a texture. returns SDL_Texture* and optionally width/height
static SDL_Texture* render_text(TTF_Font *font, SDL_Renderer *r, const char *text,
                                SDL_Color color, int *out_w, int *out_h) {
    //sdl_ttf renders to a surface first then we juts upload to a texture for fast drawing
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return NULL;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (out_w) *out_w = surf[0].w;
    if (out_h) *out_h = surf[0].h;

    //we dont need the surface anymore
    SDL_FreeSurface(surf); 
    return tex;
}

static void draw_text_centered(TTF_Font *font, SDL_Renderer *r,
                               const char *text, int cx, int cy) {
    SDL_Color white = {245,245,245,255};

    int w = 0, h = 0;
    SDL_Texture *tex = render_text(font, r, text, white, &w, &h);
    if (!tex) return;

    SDL_Rect dst = {cx - w/2, cy - h/2, w, h};
    SDL_RenderCopy(r, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
}


//literally all we are doing here is writing bits we callback to the memory buffer struct
static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    Memory *mem = (Memory*)userp;

    //Expand buffer to fit new bytes; if realloc fails, return 0 to abort transfer
    uint8_t *new_data = (uint8_t*)realloc(mem[0].data, mem[0].size + total);
    if (!new_data) return 0;

    mem[0].data = new_data;
    memcpy(mem[0].data + mem[0].size, contents, total);
    mem[0].size += total;
    return total;
}

//download https://github.com/<username>.png and return a texture but we also NULL on failure
static SDL_Texture* fetch_github_avatar(SDL_Renderer *r, const char *username,
                                       int *out_w, int *out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    CURL *curl = curl_easy_init();//create a request handle
    if (!curl) return NULL;

    char url[512];
    snprintf(url, sizeof(url), "https://github.com/%s.png", username);

    Memory mem = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);//what to fetch
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb); //the function to use to save data
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem); //specify the memory struct
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); //GitHub redirects to CDN
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "lounge-board/1.0"); //useragent

    CURLcode res = curl_easy_perform(curl); //attempt to download
    curl_easy_cleanup(curl);

    //fail 
    if (res != CURLE_OK || mem.size == 0) {
        free(mem.data);
        return NULL;
    }

    //wrap bytes as SDL_RWops so SDL_image can decode from memory (image processing shit)
    SDL_RWops *rw = SDL_RWFromMem(mem.data, (int)mem.size);
    if (!rw) { free(mem.data); return NULL; }

    //decode PNG bytes from SDL_Surface
    SDL_Surface *surf = IMG_LoadPNG_RW(rw);
    SDL_RWclose(rw);
    free(mem.data);

    if (!surf) return NULL;

    //convert the surface into a texture (this is a GPU upload)
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (out_w) *out_w = surf[0].w;
    if (out_h) *out_h = surf[0].h;

    SDL_FreeSurface(surf);
    return tex;
}


//free function for each contributor
static void contributor_free(Contributor *c) {
    if (c[0].avatar_tex) SDL_DestroyTexture(c[0].avatar_tex);
    if (c[0].label_tex)  SDL_DestroyTexture(c[0].label_tex);
    free(c[0].username);
    free(c[0].label);
    memset(c, 0, sizeof(*c));
}

//free entire strip 
static void contributors_strip_free(ContributorStrip *strip) {
    //iterate through contributors and free them via contributor_free()
    for (int i = 0; i < strip[0].count; i++) contributor_free(&strip[0].items[i]);
    memset(strip, 0, sizeof(*strip));
}

//load contributors from contributors.txt and build textures (pfps + labels)
static void contributors_strip_load(ContributorStrip *strip,
                                    SDL_Renderer *r,
                                    TTF_Font *font_footer,
                                    const char *path,
                                    int screen_w) {
    contributors_strip_free(strip); //start clean so reload doesn’t leak

    /*
    heres another good time to go on a tangent
    the fucking ->. operator is stupid
    pointers are like arrays. they have indicies. 
    -> makes no fucking sense. its literally just pointing to the first index 
    why would you have a specific operator for this. [0]. works THE SAME WAY 
    */

    strip[0].avatar_draw_px = 36; //draw avatars 36x36 px
    strip[0].gap_px = 32; //32 px between contributors
    strip[0].pad_px = 10; //10 px spacing between pfp and text
    strip[0].scroll_px = 0.0; //reset scroll on reload
    strip[0].scroll_speed = 25.0; //25 px/sec 

    FILE *f = fopen(path, "r");
    if (!f) {
        //if file missing, we keep strip[0].count = 0 and render a fail message after calling this function
        return;
    }

    //read the contributor file
    char buf[2048];
    while (fgets(buf, sizeof(buf), f) && strip[0].count < MAX_CONTRIBS) {
        char *t = str_trim(buf);
        if (!*t) continue; //ignore blank lines

        char *username = github_username_from_link(t);
        if (!username) continue; //skip invalid lines

        Contributor *c = &strip[0].items[strip[0].count++];
        memset(c, 0, sizeof(*c));

        c[0].username = username;
        c[0].label = make_label(username);

        //download avatar now
        c[0].avatar_tex = fetch_github_avatar(r, c[0].username, &c[0].avatar_w, &c[0].avatar_h);

        //get text label texture ready so footer scrolling is smooth every frame
        SDL_Color white = {255,255,255,255};
        c[0].label_tex = render_text(font_footer, r, c[0].label, white,
                                &c[0].label_w, &c[0].label_h);
        if (!c[0].label_tex) {
            //if text texture fails, drop this contributor to avoid crashes in rendering 
            //sorry...
            contributor_free(c);
            strip[0].count--;
            continue;
        }
    }

    fclose(f);

    //precompute strip width for seamless looping, its just the sum of the block widths and gaps
    strip[0].strip_w = 0;
    strip[0].strip_h = strip[0].avatar_draw_px;

    //build the strip
    for (int i = 0; i < strip[0].count; i++) {
        Contributor *c = &strip[0].items[i];

        //each contributor block width = avatar + pad + label
        int block_w = strip[0].avatar_draw_px + strip[0].pad_px + c[0].label_w;

        //height should at least fit label height too
        if (c[0].label_h > strip[0].strip_h) strip[0].strip_h = c[0].label_h;

        strip[0].strip_w += block_w;

        //add a gap after each block
        strip[0].strip_w += strip[0].gap_px;
    }

    //avoid zero width to prevent divide-by-zero-ish logic later
    //im a math person you cant expect me to not do this shit
    if (strip[0].strip_w <= 0) strip[0].strip_w = 1;
    //add big space after the contributgors are done
    strip[0].strip_w += screen_w;
}

//render one full strip instance starting at x_start (used twice)
static void contributors_strip_render_at(const ContributorStrip *strip, SDL_Renderer *r,
                                        int x_start, int y_center) {
    int x = x_start;

    for (int i = 0; i < strip[0].count; i++) {
        const Contributor *c = &strip[0].items[i];

        //align avatar and label vertically relative to footer center
        int av = strip[0].avatar_draw_px;
        int av_y = y_center - av / 2;

        //draw avatar if available; else draw a simple placeholder box
        SDL_Rect av_dst = {x, av_y, av, av};

        if (c[0].avatar_tex) {
            //we draw avatar as a square (GitHub avatar is usually square already so who fuckinhg cares)
            SDL_SetTextureBlendMode(c[0].avatar_tex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(r, c[0].avatar_tex, NULL, &av_dst);
        } else {
            //placeholder if avatar didn’t download (i love oswego wifi)
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 90);
            SDL_RenderFillRect(r, &av_dst);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 120);
            SDL_RenderDrawRect(r, &av_dst);
        }

        //draw label to the right of avatar
        int label_y = y_center - c[0].label_h / 2;
        SDL_Rect label_dst = {x + av + strip[0].pad_px, label_y, c[0].label_w, c[0].label_h};

        //blend stuff together
        SDL_SetTextureBlendMode(c[0].label_tex, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(r, c[0].label_tex, NULL, &label_dst);

        //advance x for next contributor block
        x += av + strip[0].pad_px + c[0].label_w + strip[0].gap_px;
    }
}

//update and draw scroller in footer
static void contributors_strip_update_and_render(ContributorStrip *strip,
                                                SDL_Renderer *r,
                                                int screen_w,
                                                int footer_y,
                                                int footer_h,
                                                double dt) {
    int y_center = footer_y + footer_h / 2;

    if (strip[0].count == 0 || strip[0].strip_w <= 1) return;

    //advance scroll
    strip[0].scroll_px += strip[0].scroll_speed * dt;

    //keep scroll bounded using modulo so it never explodes
    while (strip[0].scroll_px >= strip[0].strip_w) strip[0].scroll_px -= strip[0].strip_w;
    while (strip[0].scroll_px < 0) strip[0].scroll_px += strip[0].strip_w;

    //start drawing so that content scrolls left smoothly
    int x = (int)(-strip[0].scroll_px);

    //draw enough repeated strips to cover the whole screen
    //this guarantees no gap even if strip_w < screen_w
    while (x < screen_w) {
        contributors_strip_render_at(strip, r, x, y_center);
        x += strip[0].strip_w;
    }
}

static int pick_unused_plugin_index(TileSystem *sys, int slot_id) {
    if (sys[0].plugin_count == 0) return -1;
    if (sys[0].plugin_count == 1) return 0; //can't avoid duplicates if only 1 exists lmao

    //]mark used indices by other slots
    int used[MAX_TILES] = {0};
    for (int i = 0; i < 4; i++) {
        if (i == slot_id) continue;
        int idx = sys[0].slots[i].plugin_index;
        if (idx >= 0 && idx < sys[0].plugin_count) used[idx] = 1;
    }

    //try random picks a few times
    for (int tries = 0; tries < 32; tries++) {
        int idx = rand() % sys[0].plugin_count;
        if (!used[idx]) return idx;
    }

    //fallback: first unused
    for (int i = 0; i < sys[0].plugin_count; i++) {
        if (!used[i]) return i;
    }

    return rand() % sys[0].plugin_count;
}

static void tile_slot_set(TileSystem *sys, TileContext *ctx, int slot_id, int plugin_index) {
    TileSlot *s = &sys[0].slots[slot_id];

    //destroy old state so corners are actually independent
    if (s[0].plug && s[0].state && s[0].plug[0].api[0].destroy) {
        s[0].plug[0].api[0].destroy(s[0].state);
    }

    s[0].plug = &sys[0].plugins[plugin_index];
    s[0].plugin_index = plugin_index;

    //create a fresh instance for THIS corner
    s[0].state = s[0].plug[0].api[0].create ? s[0].plug[0].api[0].create(ctx, s[0].plug[0].plugin_dir) : NULL;

    //on_show lets the tile do “once per appearance” work (like pick random a and inv)
    if (s[0].plug[0].api[0].on_show) s[0].plug[0].api[0].on_show(s[0].state);

    s[0].t_in_tile = 0.0;

    //per-tile duration if provided; else global
    double d = sys[0].global_duration;
    if (s[0].plug[0].api[0].preferred_duration) {
        double pd = s[0].plug[0].api[0].preferred_duration();
        if (pd > 0.1) d = pd;
    }
    s[0].duration = d;
}

static void tile_manager_init(TileManager *tm, double global_duration) {
    memset(tm, 0, sizeof(*tm)); //start with empty state
    tm[0].global_duration = global_duration;
    tm[0].current = 0;
    tm[0].t_in_tile = 0.0;
}

static void tile_manager_unload(TileManager *tm) {
    //we must destroy plugin state before unloading shared libraries
    for (int i = 0; i < tm[0].count; i++) {
        LoadedTile *lt = &tm[0].tiles[i];
        if (lt[0].api && lt[0].state && lt[0].api[0].destroy) lt[0].api[0].destroy(lt[0].state);
        if (lt[0].dl_handle) dlclose(lt[0].dl_handle);
    }
    memset(tm, 0, sizeof(*tm));
}

//attempt to load all .so files from a directory as tiles
static void tile_manager_load_dir(TileManager *tm, const char *plugins_root, TileContext *ctx) {
    DIR *root = opendir(plugins_root);
    if (!root) {
        fprintf(stderr, "tile loader: cannot open %s\n", plugins_root);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(root)) && tm[0].count < MAX_TILES) {
        if (strcmp(ent[0].d_name, ".") == 0 || strcmp(ent[0].d_name, "..") == 0) continue;

        //this is the plugin folder, ./plugins/<plugin_name>
        char plugin_dir[512];
        snprintf(plugin_dir, sizeof(plugin_dir), "%s/%s", plugins_root, ent[0].d_name);

        DIR *pd = opendir(plugin_dir);
        if (!pd) continue; //skip non-directories

        struct dirent *e2;
        while ((e2 = readdir(pd)) && tm[0].count < MAX_TILES) {
            size_t len = strlen(e2[0].d_name);
            if (len < 4 || strcmp(e2[0].d_name + len - 3, ".so") != 0) continue;

            //Full path to .so, ./plugins/<plugin_name>/<file>.so
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", plugin_dir, e2[0].d_name);

            void *h = dlopen(full, RTLD_NOW);
            if (!h) {
                fprintf(stderr, "dlopen failed for %s: %s\n", full, dlerror());
                continue;
            }
            fprintf(stderr, "Loaded SO: %s\n", full);

            dlerror(); //clear
            tile_get_fn get = (tile_get_fn)dlsym(h, "tile_get");
            const char *err = dlerror();
            if (err || !get) {
                fprintf(stderr, "dlsym(tile_get) failed for %s: %s\n", full, err ? err : "unknown");
                dlclose(h);
                continue;
            }

            const Tile *api = get();
            if (!api) {
                fprintf(stderr, "tile_get returned NULL for %s\n", full);
                dlclose(h);
                continue;
            }

            fprintf(stderr, "Tile name: %s | api_version=%d\n",
                    api[0].name ? api[0].name() : "(null)", api[0].api_version);

            if (api[0].api_version > TILE_API_VERSION || api[0].api_version < 2) {
                fprintf(stderr, "API version unsupported for %s (plugin=%d host=%d)\n",
                        full, api[0].api_version, TILE_API_VERSION);
                dlclose(h);
                continue;
            }

            //IMPORTANT: pass plugin_dir (the subfolder), not plugins_root
            void *state = api[0].create ? api[0].create(ctx, plugin_dir) : NULL;
            fprintf(stderr, "Created tile state for %s (state=%p, plugin_dir=%s)\n",
                    full, state, plugin_dir);

            LoadedTile *lt = &tm[0].tiles[tm[0].count++];
            memset(lt, 0, sizeof(*lt));
            lt[0].dl_handle = h;
            lt[0].api = api;
            lt[0].state = state;
            strncpy(lt[0].path, full, sizeof(lt[0].path) - 1);
            lt[0].path[sizeof(lt[0].path) - 1] = 0;

            //If you have on_show in API v2, calling it here ensures it initializes immediately.
            if (lt[0].api[0].on_show) lt[0].api[0].on_show(lt[0].state);

        }

        closedir(pd);
    }

    closedir(root);
}

//tile duration selectionm, tile’s preferred_duration if set and valid, otherwise global
static double tile_duration(const LoadedTile *lt, double global_duration) {
    if (lt[0].api && lt[0].api[0].preferred_duration) {
        double d = lt[0].api[0].preferred_duration();
        if (d > 0.1) return d;
    }
    return global_duration;
}

static void tile_manager_update(TileManager *tm, double dt) {
    if (tm[0].count <= 0) return;

    LoadedTile *cur = &tm[0].tiles[tm[0].current];

    // update only if not frozen (you said frozen means don't update)
    if (!tm[0].frozen) {
        if (cur[0].api && cur[0].api[0].update && cur[0].state) {
            cur[0].api[0].update(cur[0].state, dt);
        }

        tm[0].t_in_tile += dt;
        double dur = tile_duration(cur, tm[0].global_duration);

        if (tm[0].t_in_tile >= dur) {
            // *** NEW: tell current tile it's leaving ***
            if (cur[0].api && cur[0].api[0].on_hide && cur[0].state) {
                cur[0].api[0].on_hide(cur[0].state);
            }

            tm[0].t_in_tile = 0.0;
            tm[0].current = (tm[0].current + 1) % tm[0].count;

            LoadedTile *next = &tm[0].tiles[tm[0].current];

            if (next[0].api && next[0].api[0].on_show && next[0].state) {
                next[0].api[0].on_show(next[0].state);
            }
        }
    } else {
        // frozen: still allow render; no update / no timer advance
        // (do nothing here)
    }
}

static void tile_manager_render(TileManager *tm, SDL_Renderer *r, const SDL_Rect *rect) {
    if (tm[0].count <= 0) return;
    LoadedTile *cur = &tm[0].tiles[tm[0].current];
    if (cur[0].api && cur[0].api[0].render) cur[0].api[0].render(cur[0].state, r, rect);
}

static void tile_slot_render(TileSlot *s, SDL_Renderer *r, const SDL_Rect *rect) {
    if (!s[0].plug) return;
    if (s[0].plug[0].api[0].render) s[0].plug[0].api[0].render(s[0].state, r, rect);
}


int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); //0=nearest, 1=linear

    //SDL init is required before using any SDL calls, but SDL_INIT_TIMER helps for timing too
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    //SDL_ttf must be initialized before loading fonts
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    //SDL_image init isn’t required for IMG_LoadPNG_RW, but it’s good practice
    //and it also makes failures clearer if codecs are missing.
    int img_flags = IMG_INIT_PNG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    //libcurl global init
    //CURL_GLOBAL_DEFAULT sets up SSL and other bs
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "curl_global_init failed\n");
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    //create a resizable window (with fullscreen toggle)
    SDL_Window *win = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!win) {
        fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        curl_global_cleanup();
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    //create an accelerated renderer with vsync
    SDL_Renderer *r = SDL_CreateRenderer(
        win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!r) {
        fprintf(stderr, "CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        curl_global_cleanup();
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    //background, we stream the texture so we can update pixels every frame
    SDL_Texture *bgTex = SDL_CreateTexture(
        r,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        BG_TEX_W, BG_TEX_H
    );
    if (!bgTex) {
        fprintf(stderr, "CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        curl_global_cleanup();
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    //optional but helpful: explicitly request linear scale mode for background texture
    SDL_SetTextureScaleMode(bgTex, SDL_ScaleModeLinear);

    //now we just load fonts
    TTF_Font *font_title  = TTF_OpenFont(FONT_PATH, 40);
    TTF_Font *font_time   = TTF_OpenFont(FONT_PATH, 96);
    TTF_Font *font_footer = TTF_OpenFont(FONT_PATH, 28);
    if (!font_title || !font_time || !font_footer) {
        fprintf(stderr, "Failed to open font at %s\nTTF error: %s\n", FONT_PATH, TTF_GetError());
        SDL_DestroyTexture(bgTex);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        curl_global_cleanup();
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    //title is user-customizable by editing title.txt without recompiling
    char *title = read_first_line("title.txt", "Shineman 425");

    //load contributors from contributors.txt
    ContributorStrip contribs;
    memset(&contribs, 0, sizeof(contribs));

    int ww = 1280, hh = 720;
    SDL_GetWindowSize(win, &ww, &hh);
    contributors_strip_load(&contribs, r, font_footer, "contributors.txt", ww);

    //setup tile context (this is what plugins get)
    TileContext tctx;
    memset(&tctx, 0, sizeof(tctx)); //zero init

    tctx.renderer = r;
    tctx.screen_w = ww;
    tctx.screen_h = hh;
    tctx.font_small  = font_footer;
    tctx.font_medium = font_title;

    MediaSystem *media = media_system_create(r);
    tctx.media = media_system_api(media);
    media_system_set_ctx(&tctx, media);

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s\n", SDL_GetError());
        }
    }

    //we use 4 tile managers
    //this gives independence immediately without adding new loader APIs
    TileManager tm[4];
    for (int i = 0; i < 4; i++) {
        fprintf(stderr, "HOST: TileContext ptr=%p media=%p\n", (void*)&tctx, (void*)tctx.media);
        tile_manager_init(&tm[i], 8.0); //each corner cycles tiles
        tile_manager_load_dir(&tm[i], "./plugins", &tctx); //load same plugin set into each manager

        //if its frozen we dont
        if (tm[i].frozen) continue;

        //if there are enough tiles, start each corner at a different index so you don't see duplicates
        //(this is a best-effort uniqueness without a full scheduler)
        if (tm[i].count > 0) {
            LoadedTile *old = &tm[i].tiles[tm[i].current];
            if (old[0].api && old[0].api[0].on_hide && old[0].state) {
                old[0].api[0].on_hide(old[0].state);
            }

            tm[i].current = (tm[i].current + 1) % tm[i].count;
            tm[i].t_in_tile = 0.0;

            LoadedTile *nw = &tm[i].tiles[tm[i].current];
            if (nw[0].api && nw[0].api[0].on_show && nw[0].state) {
                nw[0].api[0].on_show(nw[0].state);
            }
        }
    }

    //timing setup for smooth downtime
    uint64_t last = SDL_GetPerformanceCounter();
    double t = 0.0;

    int running = 1;
    while (running) {
        //compute delta time in seconds
        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        t += dt;

        //handle events like quit, fullscreen toggle, reload dev helper
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode k = e.key.keysym.sym;

            if (k == SDLK_ESCAPE) running = 0;

            // Freeze/unfreeze per corner (toggle)
            if (k == SDLK_1) tm[0].frozen = !tm[0].frozen;
            if (k == SDLK_2) tm[1].frozen = !tm[1].frozen;
            if (k == SDLK_3) tm[2].frozen = !tm[2].frozen;
            if (k == SDLK_4) tm[3].frozen = !tm[3].frozen;

            if (k == SDLK_f) {
                //Toggle borderless fullscreen (good for TV kiosk)
                Uint32 flags = SDL_GetWindowFlags(win);
                int is_fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                SDL_SetWindowFullscreen(win, is_fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }

            if (k == SDLK_r) {
                //reload everything
                SDL_GetWindowSize(win, &ww, &hh);
                tctx.screen_w = ww;
                tctx.screen_h = hh;

                contributors_strip_load(&contribs, r, font_footer, "contributors.txt", ww);

                //reload all 4 managers so new plugins appear without restarting
                for (int i = 0; i < 4; i++) {
                    int was_frozen = tm[i].frozen;  // preserve freeze state across reload

                    tile_manager_unload(&tm[i]);
                    tile_manager_init(&tm[i], 8.0);
                    tm[i].frozen = was_frozen;

                    tile_manager_load_dir(&tm[i], "./plugins", &tctx);

                    if (tm[i].count > 0) {
                        tm[i].current = i % tm[i].count;
                        tm[i].t_in_tile = 0.0;
                    }
                }
            }
        }
        }

        //track window size so layout stays correct when resized
        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        tctx.screen_w = w;
        tctx.screen_h = h;

        //draw the animated background first
        render_background(r, bgTex, w, h, t);

        //define layout regions as percentages of the screen
        int top_h  = (int)(h * 0.14); //title bar height
        int foot_h = (int)(h * 0.12); //footer bar height
        int body_y = top_h;           //body starts right after title region
        int body_h = h - top_h - foot_h;

        //draw title at top center
        draw_text_centered(font_title, r, title, w / 2, top_h / 2);

        //center time/date chunk
        time_t raw = time(NULL);
        struct tm lt;
        localtime_r(&raw, &lt);

        char timebuf[64];
        char datebuf[128];

        //include seconds
        strftime(timebuf, sizeof(timebuf), "%I:%M:%S %p", &lt);
        strftime(datebuf, sizeof(datebuf), "%A, %B %d, %Y", &lt);

        int center_y = body_y + body_h / 2;

        draw_text_centered(font_time, r, timebuf, w / 2, center_y - 30);
        draw_text_centered(font_footer, r, datebuf, w / 2, center_y + 55);

        //rectangular corner tiles sized from the available body area
        int pad_x = (int)(w * 0.03); //horizontal margin from edges
        int pad_y = (int)(h * 0.03); //vertical margin from edges (within body)
        int gap_x = (int)(w * 0.25); //gap between left and right columns (through the center)
        int gap_y = (int)(body_h * 0.25); //vertical gap around the center time chunk

        //compute how much space we have for corner rectangles in the body
        int avail_w = w - 2 * pad_x - gap_x; //remaining width after margins + center gap
        int avail_h = body_h - 2 * pad_y - gap_y; //remaining height after margins + center gap

        //each corner rect gets half the width and half the height of the available space
        int tile_w = avail_w / 2;
        int tile_h = avail_h / 2;

        //safety clamps so weird window sizes don't produce negative/zero rects
        if (tile_w < 50) tile_w = 50;
        if (tile_h < 50) tile_h = 50;

        SDL_Rect tl = { pad_x,                 body_y + pad_y,                  tile_w, tile_h };
        SDL_Rect tr = { pad_x + tile_w + gap_x, body_y + pad_y,                  tile_w, tile_h };
        SDL_Rect bl = { pad_x,                 body_y + pad_y + tile_h + gap_y, tile_w, tile_h };
        SDL_Rect br = { pad_x + tile_w + gap_x, body_y + pad_y + tile_h + gap_y, tile_w, tile_h };


        // Update each corner manager independently (but don't advance if frozen)
        for (int i = 0; i < 4; i++) {
            if (!tm[i].frozen) {
                tile_manager_update(&tm[i], dt);
            }
        }

        // Best-effort uniqueness enforcement:
        // IMPORTANT: never modify a frozen corner's current tile.
        for (int i = 0; i < 4; i++) {
            if (tm[i].frozen) continue;
            if (tm[i].count <= 1) continue;

            for (int j = 0; j < i; j++) {
                if (tm[j].count <= 0) continue;

                // If another corner is frozen on some tile, don't collide with it.
                // If both are unfrozen, still avoid collisions.
                if (tm[i].count == tm[j].count && tm[i].current == tm[j].current) {
                    tm[i].current = (tm[i].current + 1) % tm[i].count;
                    tm[i].t_in_tile = 0.0;
                }
            }
        }

        //render the 4 corners (each is independent now)
        tile_manager_render(&tm[0], r, &tl);
        tile_manager_render(&tm[1], r, &tr);
        tile_manager_render(&tm[2], r, &bl);
        tile_manager_render(&tm[3], r, &br);

        //footer separator line
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 90);
        SDL_RenderDrawLine(r, 0, h - foot_h, w, h - foot_h);

        //footer contributors scroller
        int footer_y = h - foot_h;
        if (contribs.count > 0) {
            contributors_strip_update_and_render(&contribs, r, w, footer_y, foot_h, dt);
        } else {
            draw_text_centered(font_footer, r,
                "Add GitHub profile links (one per line) to contributors.txt",
                w / 2, footer_y + foot_h / 2);
        }

        //update videos
        media_system_update(media, dt);

        //present the frame
        SDL_RenderPresent(r);
    }

    //cleanup: unload all tile managers before SDL shutdown
    for (int i = 0; i < 4; i++) {
        tile_manager_unload(&tm[i]);
    }

    contributors_strip_free(&contribs);
    free(title);

    TTF_CloseFont(font_title);
    TTF_CloseFont(font_time);
    TTF_CloseFont(font_footer);

    SDL_DestroyTexture(bgTex);
    media_system_destroy(media);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);

    curl_global_cleanup();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}