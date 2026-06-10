#include "config.h"

#include "gdbserver_remote_commands.h"
#include "gdbserver.h"
#include "debugger.h"
#include "ui/ui.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define PASSTHROUGH_OUTPUT_SIZE 2048

#include "fuse.h"
#include "libspectrum.h"
#include "peripherals/fs/xfs.h"
#include "peripherals/spectranet.h"
#include "snapshot.h"
#include "utils.h"

static const char *
skip_spaces( const char *text )
{
    while( text && *text && isspace( (unsigned char)*text ) ) text++;
    return text;
}

static uint8_t remote_command_help( const char *args GCC_UNUSED )
{
    const struct remote_command_entry_t *entry;

    gdbserver_send_remote_console_output("Supported commands:\n");

    for (entry = remote_commands; entry->name; entry++) {
        gdbserver_send_remote_console_output(entry->name);
        gdbserver_send_remote_console_output("\n");
    }

    gdbserver_send_remote_console_output(
        "Any other command is dispatched to the Fuse internal debugger "
        "(see monitor.md):\n"
        "  break [addr] [if cond]      port-, memory-, time-, event-watch\n"
        "  clear / delete / condition  manage breakpoints\n"
        "  set addr value              poke memory; set $var value\n"
        "  out port value              write to I/O port\n"
        "  finish / next / step        execution control\n"
        "Addresses accept absolute hex or 'source:page:offset' (RAM:5:0, "
        "ROM:0:0x38, etc.).\n");
    return 0;
}

static uint8_t remote_command_reset( const char *args GCC_UNUSED )
{
    return gdbserver_reset_via_remote_command();
}

static int
write_sna_snapshot( const char *filename )
{
    libspectrum_snap *snap;
    libspectrum_byte *buffer = NULL;
    size_t length = 0;
    int flags = 0;
    int error;

    snap = libspectrum_snap_alloc();
    if( !snap ) return 1;

    error = snapshot_copy_to( snap );
    if( error ) { libspectrum_snap_free( snap ); return error; }

    error = libspectrum_snap_write( &buffer, &length, &flags, snap,
                                    LIBSPECTRUM_ID_SNAPSHOT_SNA,
                                    fuse_creator, 0 );
    libspectrum_snap_free( snap );
    if( error ) return error;

    error = utils_write_file( filename, buffer, length );
    libspectrum_free( buffer );

    return error;
}

static uint8_t remote_command_dump( const char *args )
{
    char type[32];
    const char *filename;
    size_t type_len;

    args = skip_spaces( args );
    if( !args || !*args ) {
        gdbserver_send_remote_console_output(
            "Usage: dump <sna|spectranet-ram> <file>\n" );
        return 1;
    }

    filename = args;
    while( *filename && !isspace( (unsigned char)*filename ) ) filename++;

    type_len = filename - args;
    if( type_len == 0 || type_len >= sizeof( type ) ) return 1;

    memcpy( type, args, type_len );
    type[type_len] = '\0';

    filename = skip_spaces( filename );
    if( !filename || !*filename ) {
        gdbserver_send_remote_console_output(
            "Usage: dump <sna|spectranet-ram> <file>\n" );
        return 1;
    }

    if( !strcmp( type, "sna" ) ) {
        return write_sna_snapshot( filename ) ? 1 : 0;
    }

    if( !strcmp( type, "spectranet-ram" ) ) {
        return spectranet_dump_ram( filename ) ? 1 : 0;
    }

    gdbserver_send_remote_console_output(
        "Unsupported dump type. Supported: sna, spectranet-ram\n" );
    return 1;
}

static uint8_t remote_command_spectranet_info( const char *args GCC_UNUSED )
{
    spectranet_paging_info_t info = spectranet_get_paging_info();
    char buffer[128];

    snprintf( buffer, sizeof( buffer ),
              "Spectranet available: %s\n"
              "Spectranet paged in: %s\n"
              "Page A: 0x%02x\n"
              "Page B: 0x%02x\n",
              info.available ? "yes" : "no",
              info.paged ? "yes" : "no",
              info.page_a,
              info.page_b );
    gdbserver_send_remote_console_output( buffer );

    return 0;
}

static uint8_t remote_command_xfs_debug( const char *args )
{
    int enable;
    const char *end;
    size_t length;
    char buffer[64];

    args = skip_spaces( args );

    if( !args || !*args ) {
        gdbserver_send_remote_console_output(
            "Usage: xfs-debug <on|off|1|0>\n" );
        return 1;
    }

    end = args;
    while( *end && !isspace( (unsigned char)*end ) ) end++;
    length = end - args;

    if( length == 2 && !strncmp( args, "on", length ) ) {
        enable = 1;
    } else if( length == 1 && !strncmp( args, "1", length ) ) {
        enable = 1;
    } else if( length == 4 && !strncmp( args, "true", length ) ) {
        enable = 1;
    } else if( length == 3 && !strncmp( args, "off", length ) ) {
        enable = 0;
    } else if( length == 1 && !strncmp( args, "0", length ) ) {
        enable = 0;
    } else if( length == 5 && !strncmp( args, "false", length ) ) {
        enable = 0;
    } else {
        gdbserver_send_remote_console_output(
            "Usage: xfs-debug <on|off|1|0>\n" );
        return 1;
    }

    end = skip_spaces( end );
    if( end && *end ) {
        gdbserver_send_remote_console_output(
            "Usage: xfs-debug <on|off|1|0>\n" );
        return 1;
    }

    xfs_debug_enable( enable );

    snprintf( buffer, sizeof( buffer ), "XFS debug: %s\n",
              xfs_debug_is_enabled() ? "on" : "off" );
    gdbserver_send_remote_console_output( buffer );

    return 0;
}

const struct remote_command_entry_t remote_commands[] = {
    { "help", remote_command_help },
    { "reset", remote_command_reset },
    { "dump", remote_command_dump },
    { "spectranet-info", remote_command_spectranet_info },
    { "xfs-debug", remote_command_xfs_debug },
    { NULL, NULL }
};

/* Runs on the emulator main thread via gdbserver_execute_on_main_thread().
   data is the command string; response is a char[PASSTHROUGH_OUTPUT_SIZE]
   buffer that receives the command's output: ui_error() text plus anything the
   command writes to stdout (e.g. `print`, which uses printf). */
static uint8_t action_passthrough_eval(const void *data, void *response)
{
    const char *command = (const char *)data;
    char *output = (char *)response;
#ifndef WIN32
    int saved_stdout = -1;
    int pipefd[2] = { -1, -1 };

    /* Redirect stdout to a pipe for the duration of the evaluation. Read
       commands such as `print` write their result with printf, which the
       ui_error capture below does not intercept. (Disassembly goes to the GUI,
       not stdout, and remains a client-side concern.) */
    fflush(stdout);
    if (pipe(pipefd) == 0)
    {
        saved_stdout = dup(STDOUT_FILENO);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
    }
#endif

    ui_error_capture_begin(output, PASSTHROUGH_OUTPUT_SIZE);
    debugger_command_evaluate(command);
    ui_error_capture_end();

#ifndef WIN32
    if (saved_stdout != -1)
    {
        size_t len = strlen(output);
        char buf[256];
        ssize_t n;

        fflush(stdout);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);

        /* Append the captured stdout after any ui_error text. The read end is
           non-blocking so an empty pipe returns immediately. */
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        while (len + 1 < PASSTHROUGH_OUTPUT_SIZE &&
               (n = read(pipefd[0], buf, sizeof(buf))) > 0)
        {
            size_t take = (size_t)n;
            if (take > PASSTHROUGH_OUTPUT_SIZE - 1 - len)
                take = PASSTHROUGH_OUTPUT_SIZE - 1 - len;
            memcpy(output + len, buf, take);
            len += take;
        }
        output[len] = '\0';
        close(pipefd[0]);
    }
#endif

    return 0;
}

uint8_t remote_command_passthrough(const char *command)
{
    char output[PASSTHROUGH_OUTPUT_SIZE];

    if (!command || !*command)
        return 1;

    output[0] = '\0';

    /* Serialize against the emulator thread; debugger state (breakpoints,
       memory, registers) must not mutate while emulation is mid-instruction.
       Requires the stub to be trapped, which is the normal state when an
       agent is driving the gdbserver. */
    if (!gdbserver_execute_on_main_thread(action_passthrough_eval, command, output))
        return 1;

    if (output[0])
        gdbserver_send_remote_console_output(output);

    return 0;
}
