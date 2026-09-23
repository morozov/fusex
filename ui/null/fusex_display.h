#ifndef FUSEX_DISPLAY_H
#define FUSEX_DISPLAY_H

#include <stdint.h>

typedef struct fusex_display_info {
  int width;
  int height;
  int border_width;
  int border_height;
} fusex_display_info_t;

typedef struct fusex_display_frame {
  const uint8_t *pixels;
  uint64_t generation;
} fusex_display_frame_t;

const fusex_display_info_t *fusex_display_info( void );
const fusex_display_frame_t *fusex_display_frame( void );

#endif
