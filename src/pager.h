#ifndef PAGER_H
#define PAGER_H

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

const uint32_t PAGE_SIZE = 4096;
const uint32_t TABLE_MAX_PAGES = 100;

struct Pager_t {
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages;
  void *pages[TABLE_MAX_PAGES];
};
typedef struct Pager_t Pager;

void *get_page(Pager *pager, uint32_t page_num);
Pager *pager_open(const char *filename);
void pager_flush(Pager *pager, uint32_t page_num);

#endif PAGER_H