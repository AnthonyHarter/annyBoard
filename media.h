#pragma once
#include <SDL2/SDL.h>
#include "tile_api.h"

typedef struct MediaSystem MediaSystem;

MediaSystem* media_system_create(SDL_Renderer *r);
void media_system_destroy(MediaSystem *ms);

//call this once per frame from the host to keep audio/video going
void media_system_update(MediaSystem *ms, double dt);

//let the function table see tiles 
const HostMediaAPI* media_system_api(MediaSystem *ms);

// an optional for if you want ctx[0].media funcs to find ms quickly 
void media_system_set_ctx(TileContext *ctx, MediaSystem *ms);
MediaSystem* media_system_from_ctx(TileContext *ctx);