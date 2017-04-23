SDL_CFLAGS = `sdl2-config --cflags`
SDL_LFLAGS = `sdl2-config --libs`

GTK_CFLAGS = `pkg-config --cflags gtk+-3.0`
GTK_LFLAGS = `pkg-config --libs gtk+-3.0`

eggbrowser:
	$(CXX) -g --std=c++11 -Ilibs/nanovg/src -DGLEW_STATIC $(SDL_CFLAGS) $(GTK_CFLAGS) EggBrowser/FileSystem.cpp EggBrowser/main.cpp EggBrowser/noc_file_dialog.cpp libs/glew/glew.c libs/nanovg/src/nanovg.c -o $@ $(SDL_LFLAGS) $(GTK_LFLAGS) -lGL