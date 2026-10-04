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
   Returns 0 once the command has been evaluated, 1 if it was empty or the
   emulator was not in a state where it could be evaluated. */
uint8_t remote_command_passthrough(const char *command);

#endif
