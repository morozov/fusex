#include <stdio.h>
#include <libspectrum.h>

#include "fusex_display.h"

extern const fusex_display_info_t *null_ui_display_info( void );
extern const fusex_display_frame_t *null_ui_display_frame( void );

const fusex_display_info_t *
fusex_display_info( void )
{
  return null_ui_display_info();
}

const fusex_display_frame_t *
fusex_display_frame( void )
{
  return null_ui_display_frame();
}
