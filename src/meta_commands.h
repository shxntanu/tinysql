#include "input_buffer.h"
#include "utils.h"
#include <string.h>

/**
 * Non-SQL statements like .exit are called “meta-commands”. They all start with
 * a dot, so we check for them and handle them in a separate function.
 */
typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

MetaCommandResult do_meta_command(InputBuffer *input_buffer) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    close_input_buffer(input_buffer);
    exit(EXIT_SUCCESS);
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}