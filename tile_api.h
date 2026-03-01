#ifndef TILE_API_H
#define TILE_API_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TILE_API_VERSION 4

typedef struct TileContext TileContext;


//im gonna try to make it support images and videos

typedef struct HostMediaAPI {
    //here's where we deal with images
    void* (*image_load)(TileContext *ctx, const char *path); //returns ImageHandle*
    void  (*image_free)(TileContext *ctx, void *img);
    void  (*image_draw)(TileContext *ctx, void *img,
                        SDL_Renderer *r, const SDL_Rect *dst,
                        int keep_aspect, int cover); //keep_aspect=1 recommended, cover=1 means crop-to-fill

    //Here's where we deal with videos, which unfortunately is handled mostly
    //by the tile
    void* (*video_open)(TileContext *ctx, const char *path, int loop, int with_audio); //returns VideoHandle*
    void  (*video_close)(TileContext *ctx, void *vid);
    void  (*video_update)(TileContext *ctx, void *vid, double dt);
    void  (*video_draw)(TileContext *ctx, void *vid, SDL_Renderer *r, const SDL_Rect *dst,
                        int keep_aspect, int cover);

    // audio shit
    void* (*audio_play)(TileContext *ctx, const char *path, int loop, float volume);
    void  (*audio_stop)(TileContext *ctx, void *aud);

} HostMediaAPI;

struct TileContext {
    SDL_Renderer *renderer;
    int screen_w;
    int screen_h;

    TTF_Font *font_small;
    TTF_Font *font_medium;

    const HostMediaAPI *media;
};

typedef struct Tile {
    int api_version;

    const char* (*name)(void);

    void* (*create)(TileContext *ctx, const char *plugin_dir);
    void  (*destroy)(void *state);

    void  (*update)(void *state, double dt);
    void  (*on_show)(void *state);
    void  (*on_hide)(void *state); 
    void  (*render)(void *state, SDL_Renderer *r, const SDL_Rect *rect);

    double (*preferred_duration)(void);
} Tile;

typedef const Tile* (*tile_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif