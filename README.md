
# Quick Start
The file `run.sh` can be run to automatically install dependencies, compile all plugins (*USING A PRESET COMMAND),
compile the main and media files, and finally run the dashboard. This is recommended.

# Dev Setup
Install Dependencies

```
sudo apt update
sudo apt install -y build-essential libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev
```

Then, compile main. GCC is assumed.
```
gcc -o anny_board main.c media.c \
  `sdl2-config --cflags --libs` -lSDL2_ttf -lSDL2_image -lcurl -ldl -lm \
  -lavformat -lavcodec -lavutil -lswscale -lswresample
```

Use each plugin's compile command to compile it into an "so" file.

# Developer Commentary
This application is made using SDL2. Here are some other bullet points:

    -SDL_ttf handles text.
    -I made a custom plugin system, so we load corner tiles from ./plugins/*.so
    -I made the background to just be some low-resolution noise thats upscaled
    -Github contributors are loaded from contributors.txt. Add yourself in a pr.
    -This is designed to run PURELY on linux/x11, MAYBE wayland

## Plugins

If you want to make a plugin, it's pretty easy. Go look at `tile_api.h`. Here are some pointers (badumching):

The main thing you will be doing in your plugin is passing through the following Tile struct:
```

typedef struct Tile {
    int api_version; //this is the api version, so if I mess up and need to update the h file I'm able to track what's updated and more easily figure out what uses the correct Tile and what doesn't
    

    const char* (*name)(void); //name of the tile lmao

    void* (*create)(TileContext *ctx, const char *plugin_dir); //this is the function that will be run when your tile is created
    void  (*destroy)(void *state); //whereas this is the function that will be run when your tile is destroyed (free function, basically)

    void  (*update)(void *state, double dt); //This handles stuff like video frames, it probably won't be necessary for a lot of cases
    void  (*on_show)(void *state); //This is run when your tile is explicitly shown on screen. You can do stuff like change values, see group_of_the_day.c
    void  (*on_hide)(void *state); //This will be run when your tile is not on screen anymore
    void  (*render)(void *state, SDL_Renderer *r, const SDL_Rect *rect); //this is the function to actually render your texture. Don't worry, see the examples that I'm going to talk about soon

    double (*preferred_duration)(void); //the intended duration for the tile to be displayed, with 0 being "i dont care, the system can just pick for me"
} Tile;
```
I've included bonus comments so that you know what everything does

###Sample Plugins
There are many sample plugins for you to base your plugin off of! 
- `image_tile` displays a typical png image
- `video_tile` displays a video, ooh
- `fun_facts` just displays some hardcoded text to the screen
- `group_of_the_day` displays some text which is determined everytime it's shown, it shows off the on_show() functionality.

I can understand making a big thing in C, ESPECIALLY graphics can be daunting, but check out these files if you're worried. You'll definitely be able to figure it out. 

##Controls
I've included some simple keyboard controls for convenience 

- `r`: reloads everything
- `ESC`: quits the program
- `f`: toggles fullscreen
- `1`, `2`, `3`, `4`: toggles the frozen state of a tiles, when a tile is frozen it will not update


# IMPORTANT
This project uses the GNU v3.0 License. With that said, users do NOT have the right to use AI tools to modify
this project without explicit permission from the original author.
