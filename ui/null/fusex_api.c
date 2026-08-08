/* In-process binding: a C API over the FuseX core, built as a
   shared library and driven in-process. Same glue stubs as hl_driver.c, but no
   main() — the library is loaded by the host process. */

#include <stdio.h>
#include <string.h>
#include <libspectrum.h>
#include "debugger/debugger.h"
#include "memory_pages.h"
#include "settings.h"
#include "spectrum.h"
#include "peripherals/joystick.h"
#include "ui/scaler/scaler.h"
#include "ui/ui.h"
#include "z80/z80.h"

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

/* Read a byte of memory through the current paging, the counterpart of
   fusex_poke(). Reading state this way costs one call; the alternative is
   fusex_save() and parsing the snapshot, which is a whole machine's worth of
   work for one byte.

   This reads what the Z80 reads, so an address is masked to 16 bits and one
   below $4000 returns ROM rather than being refused. */
int fusex_peek( int addr )
{
  return readbyte_internal( (libspectrum_word)addr );
}

/* Read len bytes from addr into buf, wrapping at the top of the address space
   as the Z80 does. For sampling a block of game state -- a struct, a table, a
   tile map -- in one call. */
void fusex_peek_block( int addr, int len, unsigned char *buf )
{
  for( int i = 0; i < len; i++ )
    buf[i] = readbyte_internal( (libspectrum_word)( ( addr + i ) & 0xffff ) );
}

/* One Z80 register, by the index the FUSEX_REG_* order below fixes:

     0 PC   1 SP   2 AF   3 BC   4 DE   5 HL
     6 AF'  7 BC'  8 DE'  9 HL' 10 IX  11 IY
    12 I   13 R   14 IFF1 15 IFF2 16 IM 17 halted

   R is reassembled from the two halves the emulator keeps it in, so it reads
   as the program would see it. Returns -1 for an index outside that set. */
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

/* ---- in-process debugger ---- */

/* Evaluate one command in Fuse's own debugger language and return 0, or 1 if
   the debugger reported a problem with it. "break 0xa167" sets an execution
   breakpoint, "break write 0x5b00" a watchpoint, "clear" and "delete" remove
   them; the language also prints memory and evaluates expressions. Passing the
   command language through rather than wrapping individual operations means
   the whole debugger is reachable, and nothing here needs changing when it
   gains a command.

   Diagnostics the command produces are copied into output as NUL-terminated
   text, truncated to output_size; pass NULL to discard them. The return value
   is the same either way, so a caller that only wants to know whether the
   command took does not need a buffer. Command OUTPUT is a separate thing from
   diagnostics and does not arrive here: `print` and the other read commands
   write to stdout (debugger/commandy.y), which this does not capture.

   The error return catches what the debugger itself objects to, which is not
   everything a caller can get wrong. In particular an address wider than 16
   bits is TRUNCATED rather than refused -- "break 0x1a167" silently sets a
   breakpoint at $A167 -- because the grammar hands an unbounded number to
   debugger_breakpoint_add_address(), which takes a libspectrum_word. A caller
   that builds an address from a variable should range-check it first.

   A breakpoint set here does nothing until the machine is run by
   fusex_run_until_break(). */
int fusex_debugger_command( const char *command, char *output, int output_size )
{
  char discard[256];
  int had_error;

  if( !command ) return 1;

  if( output && output_size > 0 ) {
    output[0] = '\0';
    ui_error_capture_begin( output, (size_t)output_size );
  } else {
    discard[0] = '\0';
    ui_error_capture_begin( discard, sizeof( discard ) );
  }

  debugger_command_evaluate( command );

  had_error = ui_error_capture_had_error();
  ui_error_capture_end();

  return had_error ? 1 : 0;
}

/* Run at most max_frames frames, returning 1 as soon as a breakpoint traps and
   0 if the frames run out first. A halted machine is resumed first, and a frame
   left part-way through by a trap is finished rather than restarted, so
   repeated calls walk from one breakpoint to the next. A max_frames of zero or
   less runs nothing and leaves a halted machine halted; use
   fusex_debugger_resume() to resume without running.

   WHERE IT STOPS. The debugger takes the trap before the instruction at the
   breakpoint address executes, but control comes back here only once that
   instruction has finished, so PC on return is the instruction after it -- with
   the exception of instructions that rewind PC themselves. HALT
   (z80/opcodes_base.c) and the repeating block ops LDIR/LDDR/CPIR/CPDR and the
   INIR/OTIR family (z80/z80_ed.c) re-point PC at themselves, so a breakpoint on
   one of those returns with PC still at the breakpoint address. The machine is
   at an instruction boundary either way, which is what sampling state at a
   known point in a program needs; a caller that wants the state BEFORE a
   particular instruction should break on the instruction before it.

   This does not work with the gdbserver enabled: debugger_trap() routes to
   gdbserver_activate() instead of the UI (debugger/debugger.c), so the null
   UI's ui_debugger_activate() -- which is what returns control at the trapping
   instruction -- never runs. The two are alternative ways to drive the same
   debugger, not layers.

   fusex_step() is the wrong way to run a machine that has breakpoints set: it
   has no way to report a trap. It does still run: a halted debugger checks
   every instruction, measured at 92 ms against 20 ms for 200 frames of this
   game. Drive with this instead once anything is set. */
int fusex_run_until_break( int max_frames )
{
  if( max_frames <= 0 ) return 0;
  if( debugger_mode == DEBUGGER_MODE_HALTED ) debugger_run();
  return spectrum_do_frames_until_halt( max_frames );
}

/* Frames the machine has completed since its last reset. Lets a caller check
   what fusex_step() and fusex_run_until_break() actually advanced, rather than
   inferring it from something the running program did. */
int fusex_frame_count( void )
{
  return (int)spectrum_frame_count();
}

/* Resume a machine stopped at a breakpoint, without running it.

   Removing a breakpoint does not do this: debugger_breakpoint_remove*() only
   downgrades the ACTIVE mode to INACTIVE and leaves HALTED alone, so a caller
   that deletes its breakpoints while stopped would otherwise leave the machine
   checking every instruction with nothing left to trap on. */
void fusex_debugger_resume( void )
{
  if( debugger_mode == DEBUGGER_MODE_HALTED ) debugger_run();
}

/* Whether the machine is stopped at a breakpoint. */
int fusex_debugger_halted( void )
{
  return debugger_mode == DEBUGGER_MODE_HALTED;
}

void fusex_end( void ) { fuse_end(); }
