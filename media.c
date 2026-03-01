#define _GNU_SOURCE
#include "media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL2/SDL_image.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>

//This handles media like images and videos. originally it was supposed to handle
//audio, until i realized how gross handling multiple inputs at the same time would be

//Simple image and video handlers

//images are literally just textures
typedef struct ImageHandle {
    SDL_Texture *tex;
    int w, h;
} ImageHandle;

//videos are a lot more annoying
typedef struct VideoHandle {
    //most of this stuff is just there to handle frames and updates
    AVFormatContext *fmt;
    AVCodecContext  *vdec;
    int vstream;

    AVFrame  *frame;
    AVFrame  *rgba;
    AVPacket *pkt;

    struct SwsContext *sws;
    uint8_t *rgba_buf;

    SDL_Texture *tex;
    int w, h;

    double fps;
    double frame_accum;
    int loop;
} VideoHandle;

//This actually displays media
struct MediaSystem {
    SDL_Renderer *r;
    HostMediaAPI api;
};

//handle ffmp(r)eg errors 
static void fferr(const char *where, int err) {
    char buf[256];
    av_strerror(err, buf, sizeof(buf));
    fprintf(stderr, "%s: %s (%d)\n", where, buf, err);
}

//store MediaSystem pointer in ctx by using a small side table mapping TileContext* to MediaSystem*. 
typedef struct CtxMap { TileContext *ctx; MediaSystem *ms; } CtxMap;
static CtxMap g_ctxmap[8];
static int g_ctxmap_n = 0;

//set up context for media system
//at this point, it's tomorrow and i'm going through this stuff 
//to rewrite comments. i have no fucking idea what i was cooking 
//here, but it works. it's not too horrible, but i really 
//dont feel like understanding what 3 am me did last night so
//i just wouldn't touch this particular part
void media_system_set_ctx(TileContext *ctx, MediaSystem *ms) {
    for (int i = 0; i < g_ctxmap_n; i++) {
        if (g_ctxmap[i].ctx == ctx) { g_ctxmap[i].ms = ms; return; }
    }
    if (g_ctxmap_n < (int)(sizeof(g_ctxmap) / sizeof(g_ctxmap[0]))) {
        g_ctxmap[g_ctxmap_n++] = (CtxMap){ ctx, ms };
    }
}

MediaSystem* media_system_from_ctx(TileContext *ctx) {
    for (int i = 0; i < g_ctxmap_n; i++) {
        if (g_ctxmap[i].ctx == ctx) return g_ctxmap[i].ms;
    }
    return NULL;
}

//here's where we make a rectangle that fits the aspect ratio, easy stuff
static void rect_fit_aspect(const SDL_Rect *dst, int src_w, int src_h, int cover, SDL_Rect *out) {
    //keep aspect in dst, either contain (cover=0) or cover (cover=1) 
    double dw = (double)dst[0].w, dh = (double)dst[0].h;
    double sw = (double)src_w, sh = (double)src_h;
    double s = cover ? fmax(dw / sw, dh / sh) : fmin(dw / sw, dh / sh);
    int w = (int)(sw * s + 0.5);
    int h = (int)(sh * s + 0.5);
    out[0].w = w; out[0].h = h;
    out[0].x = dst[0].x + (dst[0].w - w) / 2;
    out[0].y = dst[0].y + (dst[0].h - h) / 2;
}

//load an image texture
static void* host_image_load(TileContext *ctx, const char *path) {
    (void)ctx;
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        fprintf(stderr, "image_load failed (%s): %s\n", path, IMG_GetError());
        return NULL;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ctx[0].renderer, surf);
    ImageHandle *img = NULL;

    if (tex) {
        img = (ImageHandle*)calloc(1, sizeof(ImageHandle));
        img[0].tex = tex;
        img[0].w = surf[0].w;
        img[0].h = surf[0].h;
    } else {
        fprintf(stderr, "CreateTextureFromSurface failed (%s): %s\n", path, SDL_GetError());
    }

    SDL_FreeSurface(surf);
    return img;
}

//easy free funciton yyibbeeee
static void host_image_free(TileContext *ctx, void *img_) {
    (void)ctx;
    ImageHandle *img = (ImageHandle*)img_;
    if (!img) return;
    if (img[0].tex) SDL_DestroyTexture(img[0].tex);
    free(img);
}

//actually render an image 
static void host_image_draw(TileContext *ctx, void *img_, SDL_Renderer *r,
                            const SDL_Rect *dst, int keep_aspect, int cover) {
    (void)ctx;
    ImageHandle *img = (ImageHandle*)img_;
    if (!img || !img[0].tex) return;

    SDL_Rect d = *dst;
    if (keep_aspect) rect_fit_aspect(dst, img[0].w, img[0].h, cover, &d);
    SDL_RenderCopy(r, img[0].tex, NULL, &d);
}


static void video_free(MediaSystem *ms, VideoHandle *v);

//heres where things get juicyy
//we just use ffmpreg (yeah) to decode a video
//this was admittedly mostly taken from google lmao
static int video_open_decoder(VideoHandle *v, SDL_Renderer *r, const char *path) {
    v[0].fmt = NULL;
    if (avformat_open_input(&v[0].fmt, path, NULL, NULL) < 0) return 0;
    if (avformat_find_stream_info(v[0].fmt, NULL) < 0) return 0;

    v[0].vstream = -1;
    for (unsigned i = 0; i < v[0].fmt[0].nb_streams; i++) {
        if (v[0].fmt[0].streams[i][0].codecpar[0].codec_type == AVMEDIA_TYPE_VIDEO) { v[0].vstream = (int)i; break; }
    }
    if (v[0].vstream < 0) return 0;

    const AVCodec *dec = avcodec_find_decoder(v[0].fmt[0].streams[v[0].vstream][0].codecpar[0].codec_id);
    if (!dec) return 0;

    v[0].vdec = avcodec_alloc_context3(dec);
    if (!v[0].vdec) return 0;
    if (avcodec_parameters_to_context(v[0].vdec, v[0].fmt[0].streams[v[0].vstream][0].codecpar) < 0) return 0;
    if (avcodec_open2(v[0].vdec, dec, NULL) < 0) return 0;

    v[0].frame = av_frame_alloc();
    v[0].rgba  = av_frame_alloc();
    v[0].pkt   = av_packet_alloc();
    if (!v[0].frame || !v[0].rgba || !v[0].pkt) return 0;

    v[0].w = v[0].vdec[0].width;
    v[0].h = v[0].vdec[0].height;

    v[0].tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING, v[0].w, v[0].h);
    if (!v[0].tex) {
        fprintf(stderr, "video: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 0;
    }

    v[0].sws = sws_getContext(v[0].w, v[0].h, v[0].vdec[0].pix_fmt,
                            v[0].w, v[0].h, AV_PIX_FMT_RGBA,
                            SWS_BILINEAR, NULL, NULL, NULL);
    if (!v[0].sws) return 0;

    int buf_sz = av_image_get_buffer_size(AV_PIX_FMT_RGBA, v[0].w, v[0].h, 1);
    v[0].rgba_buf = (uint8_t*)av_malloc(buf_sz);
    if (!v[0].rgba_buf) return 0;

    av_image_fill_arrays(v[0].rgba[0].data, v[0].rgba[0].linesize, v[0].rgba_buf,
                         AV_PIX_FMT_RGBA, v[0].w, v[0].h, 1);

    AVRational fr = v[0].fmt[0].streams[v[0].vstream][0].avg_frame_rate.num ? v[0].fmt[0].streams[v[0].vstream][0].avg_frame_rate
                                                                    : v[0].fmt[0].streams[v[0].vstream][0].r_frame_rate;
    if (fr.num > 0 && fr.den > 0) v[0].fps = (double)fr.num / (double)fr.den;
    if (v[0].fps <= 1.0) v[0].fps = 30.0;

    v[0].frame_accum = 0.0;
    return 1;
}

//this is decoding a frame, again taken from google so i dont really know what exactly it does
static int video_decode_one(VideoHandle *v) {
    for (;;) {
        int rr = av_read_frame(v[0].fmt, v[0].pkt);
        if (rr < 0) return 0; /* EOF */
        if (v[0].pkt[0].stream_index != v[0].vstream) { av_packet_unref(v[0].pkt); continue; }

        if (avcodec_send_packet(v[0].vdec, v[0].pkt) == 0) {
            int got = 0;
            while (avcodec_receive_frame(v[0].vdec, v[0].frame) == 0) {
                sws_scale(v[0].sws,
                          (const uint8_t * const*)v[0].frame[0].data, v[0].frame[0].linesize,
                          0, v[0].h,
                          v[0].rgba[0].data, v[0].rgba[0].linesize);
                got = 1;
                break; /* one frame */
            }
            av_packet_unref(v[0].pkt);
            return got;
        }

        av_packet_unref(v[0].pkt);
    }
}

//audio support was removed, so ignore audio stuff please
//this is when we deal with main.c actually opening a video
static void* host_video_open(TileContext *ctx, const char *path, int loop, int with_audio) {
    //get all our nice stuff set up
    (void)with_audio;

    MediaSystem *ms = media_system_from_ctx(ctx);
    if (!ms) return NULL;

    VideoHandle *v = (VideoHandle*)calloc(1, sizeof(VideoHandle));
    v[0].loop = loop;

    if (!video_open_decoder(v, ms[0].r, path)) {
        fprintf(stderr, "video_open: failed to open %s\n", path);
        video_free(ms, v);
        return NULL;
    }

    //decode first frame so we don't draw black 
    video_decode_one(v);

    //upload it 
    void *pixels = NULL; int pitch = 0;
    if (SDL_LockTexture(v[0].tex, NULL, &pixels, &pitch) == 0) {
        for (int y = 0; y < v[0].h; y++) {
            memcpy((uint8_t*)pixels + y * pitch,
                   v[0].rgba[0].data[0] + y * v[0].rgba[0].linesize[0],
                   (size_t)(v[0].w * 4));
        }
        SDL_UnlockTexture(v[0].tex);
    }

    return v;
}

//UH OH! big ass free function
static void video_free(MediaSystem *ms, VideoHandle *v) {
    (void)ms;
    if (!v) return;
    if (v[0].sws) sws_freeContext(v[0].sws);
    if (v[0].rgba_buf) av_free(v[0].rgba_buf);
    if (v[0].pkt) av_packet_free(&v[0].pkt);
    if (v[0].frame) av_frame_free(&v[0].frame);
    if (v[0].rgba) av_frame_free(&v[0].rgba);
    if (v[0].vdec) avcodec_free_context(&v[0].vdec);
    if (v[0].fmt) avformat_close_input(&v[0].fmt);
    if (v[0].tex) SDL_DestroyTexture(v[0].tex);
    free(v);
}

//handle videos being closed
static void host_video_close(TileContext *ctx, void *vid_) {
    (void)ctx;
    MediaSystem *ms = media_system_from_ctx(ctx);
    if (!ms) return;
    VideoHandle *v = (VideoHandle*)vid_;
    if (!v) return;
    video_free(ms, v);
}

//this happens a lot, videos update their frames
//again, a good chunk of this was pulled from google (not as much this time)
static void host_video_update(TileContext *ctx, void *vid_, double dt) {
    (void)ctx;
    VideoHandle *v = (VideoHandle*)vid_;
    if (!v) return;

    double frame_dt = 1.0 / v[0].fps;
    v[0].frame_accum += dt;

    int advanced = 0;
    while (v[0].frame_accum >= frame_dt) {
        v[0].frame_accum -= frame_dt;

        int got = video_decode_one(v);
        if (!got) {
            if (v[0].loop) {
                av_seek_frame(v[0].fmt, v[0].vstream, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(v[0].vdec);
                continue;
            }
            break;
        }
        advanced = 1;
    }

    //if it's time to go to a new frame, go to the new frame
    if (advanced) {
        void *pixels = NULL; int pitch = 0;
        if (SDL_LockTexture(v[0].tex, NULL, &pixels, &pitch) == 0) {
            for (int y = 0; y < v[0].h; y++) {
                memcpy((uint8_t*)pixels + y * pitch,
                       v[0].rgba[0].data[0] + y * v[0].rgba[0].linesize[0],
                       (size_t)(v[0].w * 4));
            }
            SDL_UnlockTexture(v[0].tex);
        }
    }
}

//here's what main.c will use to renbder a video 
static void host_video_draw(TileContext *ctx, void *vid_, SDL_Renderer *r,
                            const SDL_Rect *dst, int keep_aspect, int cover) {
    (void)ctx;
    VideoHandle *v = (VideoHandle*)vid_;
    if (!v || !v[0].tex) return;

    SDL_Rect d = *dst;
    if (keep_aspect) rect_fit_aspect(dst, v[0].w, v[0].h, cover, &d);
    SDL_RenderCopy(r, v[0].tex, NULL, &d);
}


MediaSystem* media_system_create(SDL_Renderer *r) {
    //ffmpreg init
    av_log_set_level(AV_LOG_ERROR);

    MediaSystem *ms = (MediaSystem*)calloc(1, sizeof(MediaSystem));
    ms[0].r = r;

    ms[0].api.image_load   = host_image_load;
    ms[0].api.image_free   = host_image_free;
    ms[0].api.image_draw   = host_image_draw;

    ms[0].api.video_open   = host_video_open;
    ms[0].api.video_close  = host_video_close;
    ms[0].api.video_update = host_video_update;
    ms[0].api.video_draw   = host_video_draw;

    //decap'd audio support hnandle
    ms[0].api.audio_play = NULL;
    ms[0].api.audio_stop = NULL;

    return ms;
}

//free function
void media_system_destroy(MediaSystem *ms) {
    if (!ms) return;
    free(ms);
}

//this is literally a useless function but it needs to exist
void media_system_update(MediaSystem *ms, double dt) {
    (void)ms;
    (void)dt;
}
//dont worry about this
const HostMediaAPI* media_system_api(MediaSystem *ms) {
    return ms ? &ms[0].api : NULL;
}