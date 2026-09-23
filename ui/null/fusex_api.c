#include <stdio.h>
#include <string.h>
#include <libspectrum.h>

#include "debugger/debugger.h"
#include "memory_pages.h"
#include "spectrum.h"
#include "ui/ui.h"
#include "z80/z80.h"

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

int fusex_peek( int addr )
{
  return readbyte_internal( (libspectrum_word)addr );
}

void fusex_peek_block( int addr, int len, unsigned char *buf )
{
  for( int i = 0; i < len; i++ )
    buf[i] = readbyte_internal( (libspectrum_word)( ( addr + i ) & 0xffff ) );
}

int fusex_get_reg( int which )
{
  switch( which ) {
  case  0: return z80.pc.w;
  case  1: return z80.sp.w;
  case  2: return z80.af.w;
  case  3: return z80.bc.w;
  case  4: return z80.de.w;
  case  5: return z80.hl.w;
  case  6: return z80.af_.w;
  case  7: return z80.bc_.w;
  case  8: return z80.de_.w;
  case  9: return z80.hl_.w;
  case 10: return z80.ix.w;
  case 11: return z80.iy.w;
  case 12: return z80.i;
  case 13: return ( z80.r & 0x7f ) | ( z80.r7 & 0x80 );
  case 14: return z80.iff1;
  case 15: return z80.iff2;
  case 16: return z80.im;
  case 17: return z80.halted;
  }
  return -1;
}

struct text_buffer {
  char *buf;
  size_t size;
};

/* Appends text to a text_buffer, dropping what does not fit */
static void
append_text( const char *text, void *user )
{
  struct text_buffer *out = user;
  size_t length = strlen( out->buf );

  if( length + 1 < out->size )
    snprintf( out->buf + length, out->size - length, "%s", text );
}

static void
discard_text( const char *text, void *user )
{
}

int fusex_debugger_command( const char *command, char *output, int output_size )
{
  struct text_buffer out;
  int had_error;

  if( !command ) return 1;

  if( output && output_size > 0 ) {
    output[0] = '\0';
    out.buf = output;
    out.size = (size_t)output_size;
    ui_error_capture_begin( append_text, &out );
  } else {
    ui_error_capture_begin( discard_text, NULL );
  }

  debugger_command_evaluate( command );
  had_error = ui_error_capture_had_error();
  ui_error_capture_end();
  return had_error ? 1 : 0;
}

int fusex_run_until_break( int max_frames )
{
  if( max_frames <= 0 ) return 0;
  if( debugger_mode == DEBUGGER_MODE_HALTED ) debugger_run();
  return spectrum_do_frames_until_halt( max_frames );
}

void fusex_run_to_break( void )
{
  if( debugger_mode == DEBUGGER_MODE_HALTED ) debugger_run();
  spectrum_do_frames_until_halt( -1 );
}
