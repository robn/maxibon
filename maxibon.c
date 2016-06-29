// clang -Wall -g -o maxibon maxibon.c `pkg-config --libs --cflags sdl2 freetype2`

#include <SDL.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_GLYPH_H

#define FONT_SIZE (64)

const char* FT_Error_String(FT_Error err) {
  #undef __FTERRORS_H__
  #define FT_ERRORDEF( e, v, s )  case e: return s;
  #define FT_ERROR_START_LIST     switch (err) {
  #define FT_ERROR_END_LIST       }
  #include FT_ERRORS_H
  return "[unknown error]";
}

// returns num bytes consumed, or 0 for end/bogus
int utf8_decode_char(uint32_t *chr, const char *src) {
  unsigned int c = * (const unsigned char *) src;
  if (!c) { *chr = c; return 0; }
  if (!(c & 0x80)) { *chr = c; return 1; }
  else if (c >= 0xf0) {
    if (!src[1] || !src[2] || !src[3]) return 0;
    c = (c & 0x7) << 18;
    c |= (src[1] & 0x3f) << 12;
    c |= (src[2] & 0x3f) << 6;
    c |= src[3] & 0x3f;
    *chr = c; return 4;
  }
  else if (c >= 0xe0) {
    if (!src[1] || !src[2]) return 0;
    c = (c & 0xf) << 12;
    c |= (src[1] & 0x3f) << 6;
    c |= src[2] & 0x3f;
    *chr = c; return 3;
  }
  else {
    if (!src[1]) return 0;
    c = (c & 0x1f) << 6;
    c |= src[1] & 0x3f;
    *chr = c; return 2;
  }
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("usage: maxibon <fontfile> <string>\n");
    return 1;
  }

  if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
    printf("SDL_Init: %s\n", SDL_GetError());
    return -1;
  }

  FT_Error fterr;
  FT_Library ft;

  fterr = FT_Init_FreeType(&ft);
  if (fterr) {
    printf("FT_Init_FreeType: %s\n", FT_Error_String(fterr));
    return -1;
  }

  FT_Face ftface;
  fterr = FT_New_Face(ft, argv[1], 0, &ftface);
  if (fterr) {
    printf("FT_New_Face: %s\n", FT_Error_String(fterr));
    return -1;
  }

  int line_height = 0;

  unsigned long table_length = 0;
  FT_Load_Sfnt_Table(ftface, FT_MAKE_TAG('s', 'b', 'i', 'x'), 0, 0, &table_length);
  if (table_length == 0)
    FT_Load_Sfnt_Table(ftface, FT_MAKE_TAG('C', 'B', 'D', 'T'), 0, 0, &table_length);
  if (table_length > 0) {
    printf("using color font\n");

    if (ftface->num_fixed_sizes > 0) {
      int best_match = 0;
      int diff = abs(FONT_SIZE - ftface->available_sizes[0].width);
      for (int i = 1; i < ftface->num_fixed_sizes; i++) {
        int ndiff = abs(FONT_SIZE - ftface->available_sizes[i].width);
        if (ndiff < diff) {
          best_match = i;
          diff = ndiff;
        }
      }
      line_height = ftface->available_sizes[best_match].height;
      fterr = FT_Select_Size(ftface, best_match);
      if (fterr) {
        printf("FT_Select_Size: %s\n", FT_Error_String(fterr));
        return -1;
      }
    }
  }
  else {
    printf("using normal font\n");
    fterr = FT_Set_Pixel_Sizes(ftface, 0, FONT_SIZE);
    if (fterr) {
      printf("FT_New_Face: %s\n", FT_Error_String(fterr));
      return -1;
    }
    line_height = (ftface->height >> 6) * (ftface->size->metrics.y_scale >> 16);
  }

  SDL_Window *win = SDL_CreateWindow(argv[1], 100, 100, 512, 128, SDL_WINDOW_SHOWN);
  if (!win) {
    printf("SDL_CreateWindow: %s\n", SDL_GetError());
    return -1;
  }

  SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!r) {
    printf("SDL_CreateRenderer: %s\n", SDL_GetError());
    return -1;
  }

  SDL_RenderClear(r);

  SDL_Color colors[256];
  for(int i = 0; i < 256; i++) {
    colors[i].r = colors[i].g = colors[i].b = i;
  }

  SDL_Surface *surface;
  SDL_Rect target_rect = { 0, 0, 0, 0 };

  char *bytes = argv[2];
  uint32_t chr;
  int n;
  while ((n = utf8_decode_char(&chr, bytes))) {
    bytes += n;

    uint32_t glyph_index = FT_Get_Char_Index(ftface, chr);
    if (!glyph_index) {
      printf("glyph not found in font\n");
      return -1;
    }

    fterr = FT_Load_Glyph(ftface, glyph_index, FT_LOAD_COLOR);
    if (fterr) {
      printf("FT_Load_Glyph: %s\n", FT_Error_String(fterr));
      return -1;
    }

    fterr = FT_Render_Glyph(ftface->glyph, FT_RENDER_MODE_NORMAL);
    if (fterr) {
      printf("FT_Render_Glyph: %s\n", FT_Error_String(fterr));
      return -1;
    }

    FT_Bitmap *ftbm = &ftface->glyph->bitmap;
    if (ftbm->pixel_mode == FT_PIXEL_MODE_BGRA) {
      printf("drawing color glyph: U+%X\n", chr);
      surface = SDL_CreateRGBSurfaceFrom(ftbm->buffer,
        ftbm->width,
        ftbm->rows,
        32,
        ftbm->pitch,
        0x00ff0000,
        0x0000ff00,
        0x000000ff,
        0xff000000
      );
      if (!surface) {
        printf("SDL_CreateRGBSurfaceFrom: %s\n", SDL_GetError());
        return -1;
      }
    }
    else {
      printf("drawing normal glyph: U+%X\n", chr);
      surface = SDL_CreateRGBSurfaceFrom(ftbm->buffer,
        ftbm->width,
        ftbm->rows,
        8,
        ftbm->pitch,
        0,
        0,
        0,
        0
      );
      if (!surface) {
        printf("SDL_CreateRGBSurfaceFrom: %s\n", SDL_GetError());
        return -1;
      }
      SDL_SetPaletteColors(surface->format->palette, colors, 0, 256);
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surface);
    SDL_FreeSurface(surface);

    target_rect.w = ftbm->width;
    target_rect.h = ftbm->rows;
    target_rect.y = line_height - ftface->glyph->bitmap_top;

    SDL_RenderCopy(r, tex, 0, &target_rect);
    SDL_DestroyTexture(tex);

    target_rect.x += ftface->glyph->advance.x >> 6;
  }

  SDL_RenderPresent(r);

  SDL_Event event;
  while (SDL_WaitEvent(&event)) {
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
      break;
  }

  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(win);

  SDL_Quit();

  return 0;
}
