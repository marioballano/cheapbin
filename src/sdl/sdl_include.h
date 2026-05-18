#ifndef CHEAPBIN_SDL_INCLUDE_H
#define CHEAPBIN_SDL_INCLUDE_H

#if defined(__has_include)
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#  elif __has_include(<SDL.h>)
#    include <SDL.h>
#  else
#    include <SDL.h>
#  endif
#elif defined(__APPLE__)
#  include <SDL2/SDL.h>
#else
#  include <SDL.h>
#endif

#endif