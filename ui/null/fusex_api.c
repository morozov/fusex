/* In-process binding: a C API over the FuseX core, built as a
   shared library and driven in-process. Same glue stubs as hl_driver.c, but no
   main() — the library is loaded by the host process. */

#include <stdio.h>
#include <string.h>
#include <libspectrum.h>
#include "settings.h"
#include "peripherals/joystick.h"
#include "ui/scaler/scaler.h"

extern int  fuse_init( int argc, char **argv );
extern int  fuse_end( void );
extern void spectrum_do_frame( void );
extern int  display_getpixel( int x, int y );
extern void keyboard_press( int key );
extern void keyboard_release( int key );
extern int  snapshot_read( const char *filename );
extern int  snapshot_write( const char *filename );
extern int  machine_reset( int hard_reset );
extern int  rzx_start_recording( const char *filename, int embed_snapshot );
extern int  rzx_stop_recording( void );
extern void writebyte_internal( libspectrum_word address, libspectrum_byte b );
extern void movie_start( const char *name );
extern void movie_stop( void );
extern int  movie_recording;

/* The null UI (null_ui.c, null_compat.c) and machine.c supply the glue the
   sockets-off/spectranet-off/headless build needs; this file is only the API. */

/* ---- public API ---- */

/* Initialize from a full argv (machine, peripherals, media file, ...) as fuse
   would parse from the command line; turbo on. Returns 0 on success. */
int fusex_init_argv( int argc, char **argv )
{
  int r = fuse_init( argc, argv );
  settings_current.emulation_speed = 1000000;   /* bypass wall-clock pacing */
  /* Route fusex_joystick() to the Kempston port (enable it with --kempston). */
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  return r;
}

/* Action: joystick button (LEFT=0, RIGHT=1, UP=2, DOWN=3, FIRE=4). */
void fusex_joystick( int button, int press )
{
  joystick_press( 0, (joystick_button)button, press );
}

/* Record an RZX of subsequent play (embeds the current state, so the .rzx
   replays standalone in FuseX). Stop to flush the file. */
int fusex_rzx_start( const char *path ) { return rzx_start_recording( path, 1 ); }
int fusex_rzx_stop( void ) { return rzx_stop_recording(); }

/* Whether RZX playback is still running: 0 once the recording's frames are
   exhausted. A renderer stops at the 0 transition instead of capturing the
   frozen post-recording screen. */
int fusex_rzx_playing( void ) { extern int rzx_playback; return rzx_playback; }

/* Record a Fuse Movie File (FMF) of subsequent play -- video + sound captured by the
   emulator's native movie recorder, faithful to live execution (no RZX replay). Convert
   the .fmf to MP4 with fmfconv/fmf2mp4. Returns nonzero if recording started. */
int fusex_movie_start( const char *path ) { movie_start( path ); return movie_recording; }
void fusex_movie_stop( void ) { movie_stop(); }

/* Observation: the play area run through any registered Fuse scaler, selected by
   its Fuse id ("tv2x", "tv3x", "paltv3x", "2x", "hq3x", ... -- the scaler_info ids
   from ui/scaler/scaler.c), returned as RGB24 at that scaler's scale factor. buf
   must hold (256*f)*(192*f)*3 bytes for scale factor f (<= 4). Returns the pixel
   count, or 0 for an unknown scaler. Uses the 16-bit scaler path with a 555 source,
   exactly as the macOS UI (cocoadisplay.m); the 32-bit procs are wired only on _WIN32.
   Integer scale factors only (the TV/PAL/plain/hq families); fractional ones mis-size. */
int fusex_get_screen_scaled( unsigned char *buf, const char *scaler_name )
{
  enum { W = 256, H = 192, PW = W + 3 };           /* PW = words per source row incl border */
  static libspectrum_word src[ ( H + 4 ) * PW ];
  static libspectrum_word dst[ ( H * 4 ) * ( W * 4 ) ];
  static libspectrum_word pal[16];
  static int inited = 0;
  ScalerProc *proc;
  int st, x, y, k, scale, ow, oh;
  if( !inited ) {
    static const unsigned char c[16][3] = {
      {0,0,0},{0,0,192},{192,0,0},{192,0,192},{0,192,0},{0,192,192},{192,192,0},{192,192,192},
      {0,0,0},{0,0,255},{255,0,0},{255,0,255},{0,255,0},{0,255,255},{255,255,0},{255,255,255} };
    int i;
    for( i = 0; i < 16; i++ )
      pal[i] = (libspectrum_word)( ( c[i][0] >> 3 ) | ( ( c[i][1] >> 3 ) << 5 ) | ( ( c[i][2] >> 3 ) << 10 ) );
    scaler_select_bitformat( 555 );   /* sets the 16-bit scaler masks; matches cocoadisplay.m */
    inited = 1;
  }
  st = scaler_get_type( scaler_name );
  if( st < 0 ) return 0;
  proc = scaler_get_proc16( st );
  if( !proc ) return 0;
  scale = (int)( scaler_get_scaling_factor( st ) + 0.5f );
  if( scale < 1 ) scale = 1;
  ow = W * scale; oh = H * scale;
  memset( src, 0, sizeof( src ) );
  for( y = 0; y < H; y++ ) {
    libspectrum_word *row = src + ( y + 2 ) * PW + 1;
    for( x = 0; x < W; x++ ) row[x] = pal[ display_getpixel( 32 + x, 24 + y ) & 0x0f ];
  }
  proc( (const libspectrum_byte *)( src + 2 * PW + 1 ), PW * 2,
        (libspectrum_byte *)dst, ow * 2, W, H );
  k = 0;
  for( y = 0; y < oh; y++ )
    for( x = 0; x < ow; x++ ) {
      libspectrum_word p = dst[ y * ow + x ];
      buf[k++] = (unsigned char)( ( p & 0x1f ) << 3 );
      buf[k++] = (unsigned char)( ( ( p >> 5 ) & 0x1f ) << 3 );
      buf[k++] = (unsigned char)( ( ( p >> 10 ) & 0x1f ) << 3 );
    }
  return ow * oh;
}

/* Convenience: boot a bare machine with no media. */
int fusex_init( const char *machine )
{
  char *argv[3] = { "fusex", "--machine", (char *)machine };
  return fusex_init_argv( 3, argv );
}

/* Reset: load a start snapshot, or power-on reset if path is NULL. */
int fusex_reset( const char *snapshot )
{
  if( snapshot ) return snapshot_read( snapshot );
  return machine_reset( 1 );
}

/* Save current machine state to a snapshot (for capturing a start state). */
int fusex_save( const char *path ) { return snapshot_write( path ); }

/* Patch a byte in Z80 RAM, so the host process can alter running machine state. */
void fusex_poke( int addr, int val )
{
  writebyte_internal( (libspectrum_word)addr, (libspectrum_byte)val );
}

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
