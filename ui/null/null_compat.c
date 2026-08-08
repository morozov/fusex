/* null_compat.c: build-completeness glue for the headless null build.

   The null build configures with --disable-sockets (so it needs neither
   Spectranet nor mbedTLS) and therefore excludes the socket compat layer and
   the Spectranet NIC engines. A few always-built units still reference those
   symbols (utils.c the socket helpers, debugger/vfile.c the xfs engine, and
   the gdbserver's xfs-debug monitor command); none are exercised headless, so
   trivial stubs satisfy the linker.

   It also supplies main(): the fork renames fuse.c's entry point to old_main
   and expects the UI layer to provide main() (the Cocoa app does); the null UI
   does so here.

   Copyright (c) 2026 Sergei Morozov

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "config.h"

#include "compat.h"
#include "peripherals/fs/xfs_engines.h"

/* Socket compat layer (excluded by --disable-sockets) */
void compat_socket_networking_init( void ) {}
void compat_socket_networking_end( void ) {}
int  compat_socket_close( compat_socket_t fd ) { (void)fd; return 0; }

/* Spectranet NIC xfs engine (excluded with sockets; referenced by debugger/vfile.c) */
struct xfs_engine_t xfs_ram_engine;
void xfs_reset( void ) {}

/* The gdbserver's xfs-debug monitor command is built unconditionally and asks
   the engine to log; with no engine here the setting has nothing to act on, so
   it stays off and reports itself off. */
void xfs_debug_enable( bool enable ) { (void)enable; }
bool xfs_debug_is_enabled( void ) { return false; }

/* Entry point: hand off to fuse.c's renamed main. */
extern int old_main( int argc, char **argv );

int
main( int argc, char **argv )
{
  return old_main( argc, argv );
}
