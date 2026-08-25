#!/usr/bin/env bash
#
# Build the null-UI shared library and install it into a prefix.
#
# The null UI has no display: it exists so a host process can load the emulator
# in-process through the C ABI in ui/null/fusex_api.c and drive it frame by
# frame. The library is platform-specific (Mach-O on macOS, ELF elsewhere), so
# it is built on the machine that runs it rather than distributed.
#
# Usage:  ./build_null.sh
#
# Environment:
#   PREFIX   install prefix (default on macOS: ~/Applications/FuseX)
#   JOBS     parallel compile jobs (default: all cores)
#
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )}"

case "$(uname -s)" in
  Darwin)
    LIB_NAME=libfusex.dylib
    # The user's own install location for FuseX, beside the FuseX.app bundle
    # that agents/BUILDING-macos.md installs. The prefix is self-contained, so
    # nothing outside it is written.
    PREFIX="${PREFIX:-$HOME/Applications/FuseX}"
    # autoreconf needs GNU libtoolize; Homebrew installs it prefixed.
    LIBTOOLIZE="${LIBTOOLIZE:-glibtoolize}"
    ;;
  *)
    LIB_NAME=libfusex.so
    LIBTOOLIZE="${LIBTOOLIZE:-libtoolize}"
    [ -n "${PREFIX:-}" ] || {
      echo "set PREFIX: this platform has no install location for the library" >&2
      exit 1; }
    ;;
esac
export LIBTOOLIZE

command -v "$LIBTOOLIZE" >/dev/null || { echo "missing: $LIBTOOLIZE" >&2; exit 1; }
for t in autoreconf automake autoconf pkg-config perl; do
  command -v "$t" >/dev/null || { echo "missing build tool: $t" >&2; exit 1; }
done

# A configure previously run INSIDE a source directory blocks every out-of-tree
# build of it ("source directory already configured"), and its leftover config.h
# is worse than the refusal: #include "config.h" searches the including file's
# own directory FIRST, so a stale header in the source tree shadows the one the
# build directory generated, whatever -I says. A UI_SDL2 config.h left beside
# fuse.c makes a null-UI build compile as SDL and fail on a missing SDL.h.
#
# All of it is generated output, so clear it. NOTE: this discards an in-source
# build of that tree; out-of-tree builds cannot coexist with one anyway.
# Stray objects are the subtler half. An out-of-tree build lists VPATH as the
# source tree, so a display.o left there from an in-source build satisfies make's
# prerequisite without being rebuilt -- and the link then fails looking for that
# bare name in the build directory. make distclean does not always reach them.
clear_in_source_build() {
  local stray
  if [ -f "$1/config.status" ] || [ -f "$1/config.h" ]; then
    echo "    clearing a previous in-source build in $2"
    ( cd "$1" && make distclean >/dev/null 2>&1 || true )
    rm -f "$1/config.status" "$1/config.cache" "$1/config.h" "$1/config.log"
  fi
  #
  # Ask git which of them are UNTRACKED. A source tree can track a prebuilt
  # archive -- data/win32/libWinSparkle.a is one -- and a sweep by extension
  # alone deletes it. --others lists only what git does not track, so a tracked
  # file cannot be reached however it is named.
  if git -C "$1" rev-parse --git-dir >/dev/null 2>&1; then
    stray=$(cd "$1" && git ls-files --others -- '*.o' '*.lo' '*.a' \
              | grep -v "^build-" | grep -v "^3rdparty/" || true)
    # git lists the names relative to the tree, so they are anchored to it. An
    # empty list must stay empty: anchoring one blank line yields the tree's own
    # path, and the sweep below then tries to delete the tree.
    [ -n "$stray" ] && stray=$(printf '%s\n' $stray | sed "s|^|$1/|")
  else
    stray=$(find "$1" -name '*.o' -o -name '*.lo' -o -name '*.a' \
              | grep -v "/build-" | grep -v "/3rdparty/" || true)
  fi
  if [ -n "$stray" ]; then
    echo "    removing $(printf '%s\n' $stray | wc -l | tr -d ' ') stray objects from $2"
    printf '%s\n' $stray | xargs rm -f
  fi
}

echo "==> building $LIB_NAME (jobs: $JOBS, prefix: $PREFIX)"

# --- 1. libspectrum -----------------------------------------------------------
#
# The build needs a libspectrum at least as new as the 3rdparty submodule. A
# system-installed one is typically older, and the build then fails deep in
# tape.c on a missing entry point rather than at configure time -- so the
# submodule is built and INSTALLED into a prefix, and configure is pointed at
# it explicitly. The install step is not optional: an uninstalled prefix makes
# configure fall back to the system copy without saying so.
SUBMODULE="$SRC/3rdparty/libspectrum"
[ -f "$SUBMODULE/configure.ac" ] || {
  echo "libspectrum submodule not checked out; run:" >&2
  echo "  git submodule update --init 3rdparty/libspectrum" >&2; exit 1; }

SPECTRUM_PREFIX="$SRC/build-libspectrum/prefix"
if [ ! -f "$SPECTRUM_PREFIX/lib/libspectrum.a" ]; then
  echo "==> libspectrum"
  clear_in_source_build "$SUBMODULE" "3rdparty/libspectrum"
  ( cd "$SUBMODULE" && autoreconf -fi -I m4 >/dev/null )
  mkdir -p "$SRC/build-libspectrum"
  ( cd "$SRC/build-libspectrum"
    # --disable-shared links libspectrum into the library, so the build output
    # depends on no libspectrum anywhere on the system. --with-pic is required
    # to link the static objects into a shared object on ELF targets.
    # The two --without/--with=none drop dependencies a headless run never uses:
    # RZX signature verification and WAV tape audio.
    ../3rdparty/libspectrum/configure --prefix="$SPECTRUM_PREFIX" \
      --without-libgcrypt --disable-shared --with-pic --with-wav-backend=none >/dev/null
    make -j"$JOBS" >/dev/null && make install >/dev/null )
else
  echo "==> libspectrum already built ($SPECTRUM_PREFIX)"
fi

# --- 2. fuse, null UI ---------------------------------------------------------
echo "==> configuring fuse"
clear_in_source_build "$SRC" "the checkout"
( cd "$SRC" && autoreconf -fi -I m4 >/dev/null 2>&1 )
# Start clean. Automake records each object's dependencies in a .Po file that
# names the config.h it was compiled against; when configure re-runs with
# different options, or a previous attempt failed part-way, those files describe
# a tree that no longer exists and make cannot recover on its own. A rebuild of
# fuse costs a couple of minutes, which is cheaper than diagnosing that.
BUILD="$SRC/build-null"
rm -rf "$BUILD"
mkdir -p "$BUILD"
( cd "$BUILD"
  ../configure --prefix="$PREFIX" \
    --with-null-ui --with-audio-driver=null \
    LIBSPECTRUM_CFLAGS="-I$SPECTRUM_PREFIX/include" \
    LIBSPECTRUM_LIBS="-L$SPECTRUM_PREFIX/lib -lspectrum -lbz2 -lz" >/dev/null )

# Generated sources (settings.h, the debugger's parser, the Z80 opcode tables)
# are automake BUILT_SOURCES: `make all` builds them first, but `make fuse` does
# not, and the compile then fails on a missing settings.h. There is no standard
# target for them, so ask make to print the variable and build those files.
echo "==> generated sources"
( cd "$BUILD"
  printf 'print-built-sources:\n\t@echo $(BUILT_SOURCES)\n' > .built-sources.mk
  make -j1 $(make -s -f Makefile -f .built-sources.mk print-built-sources) >/dev/null
  rm -f .built-sources.mk )

# The library is linked from the program's own objects, so the program is built
# first; `make` on its own additionally builds unittests, one of which does not
# link against the null UI, so build the two targets that are wanted instead.
echo "==> fuse"
( cd "$BUILD" && make -j"$JOBS" fuse >/dev/null )
echo "==> $LIB_NAME"
( cd "$BUILD" && make "$LIB_NAME" >/dev/null )

# --- 3. install ---------------------------------------------------------------
#
# install-exec-local carries the library; the rest of install puts the program
# and its data files under the same prefix.
echo "==> installing into $PREFIX"
( cd "$BUILD" && make install >/dev/null )

echo "==> done: $PREFIX/lib/$LIB_NAME"
