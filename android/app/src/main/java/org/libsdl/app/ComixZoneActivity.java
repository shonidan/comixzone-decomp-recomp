package org.libsdl.app;

/* SDL2 is linked statically into libmain.so (source/CMakeLists.txt builds SDL_STATIC, no
 * SDL_SHARED), so there is no separate libSDL2.so to dlopen — only "main" exists. */
public class ComixZoneActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
