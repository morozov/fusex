/* In-process binding: a C API over the FuseX core, built as a
   shared library and driven in-process. Same glue stubs as hl_driver.c, but no
   main() — the library is loaded by the host process. */

#include <stdio.h>
#include "settings.h"

extern int  fuse_init( int argc, char **argv );
extern int  fuse_end( void );
extern void spectrum_do_frame( void );
extern int  display_getpixel( int x, int y );
extern void keyboard_press( int key );
extern void keyboard_release( int key );
extern int  snapshot_read( const char *filename );
extern int  snapshot_write( const char *filename );
extern int  machine_reset( int hard_reset );

/* The null UI (null_ui.c, null_compat.c) and machine.c supply the glue the
   sockets-off/spectranet-off/headless build needs; this file is only the API. */

/* ---- public API ---- */

/* Boot a machine; turbo on (unthrottled). Returns 0 on success. */
int fusex_init( const char *machine )
{
  char *argv[3] = { "fusex", "--machine", (char *)machine };
  int r = fuse_init( 3, argv );
  settings_current.emulation_speed = 1000000;   /* bypass wall-clock pacing */
  return r;
}

/* Reset: load a start snapshot, or power-on reset if path is NULL. */
int fusex_reset( const char *snapshot )
{
  if( snapshot ) return snapshot_read( snapshot );
  return machine_reset( 1 );
}

/* Save current machine state to a snapshot (for capturing a start state). */
int fusex_save( const char *path ) { return snapshot_write( path ); }

/* Action: hold/release a key (keyboard_key_name; ASCII-valued for letters). */
void fusex_key_down( int key ) { keyboard_press( key ); }
void fusex_key_up( int key )   { keyboard_release( key ); }

/* Control: advance n frames (frame-skip). */
void fusex_step( int n_frames ) { for( int i = 0; i < n_frames; i++ ) spectrum_do_frame(); }

/* Observation: fill 256*192 bytes with play-area palette indices (0..15). */
void fusex_get_screen( unsigned char *buf )
{
  int k = 0;
  for( int y = 24; y < 24 + 192; y++ )
    for( int x = 32; x < 32 + 256; x++ )
      buf[k++] = (unsigned char)display_getpixel( x, y );
}

void fusex_end( void ) { fuse_end(); }
