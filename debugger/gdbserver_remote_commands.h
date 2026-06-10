#ifndef FUSE_DEBUGGER_GDBSERVER_REMOTE_COMMANDS_H
#define FUSE_DEBUGGER_GDBSERVER_REMOTE_COMMANDS_H

#include <stdint.h>

typedef uint8_t (*remote_command_handler_t)(const char *args);

struct remote_command_entry_t {
    const char *name;
    remote_command_handler_t handler;
};

extern const struct remote_command_entry_t remote_commands[];

/* Dispatch a monitor command that does not match any entry in
   remote_commands[] to the Fuse internal debugger (debugger_command_evaluate).
   Any error text emitted via ui_error() during evaluation is captured and
   sent to the client via gdbserver_send_remote_console_output(). Returns 0
   on success, 1 if the command was syntactically rejected or the emulator
   was not in a state where it could be evaluated. */
uint8_t remote_command_passthrough(const char *command);

#endif
