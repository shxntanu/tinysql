#ifndef INPUT_BUFFER_H
#define INPUT_BUFFER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct InputBuffer_t {
  char *buffer;
  size_t buffer_length;
  ssize_t input_length;
};
typedef struct InputBuffer_t InputBuffer;

InputBuffer *new_input_buffer();
void read_input(InputBuffer *input_buffer);
void print_prompt();

#endif INPUT_BUFFER_H
