#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute);
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define TABLE_MAX_PAGES 100

// ROWS

typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);

const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

void serialize_row(Row *src, void *dst) {
  memcpy(dst + ID_OFFSET, &(src->id), ID_SIZE);
  memcpy(dst + USERNAME_OFFSET, &(src->username), USERNAME_SIZE);
  memcpy(dst + EMAIL_OFFSET, &(src->email), EMAIL_SIZE);
}

void deserialize_row(void *src, Row *dst) {
  memcpy(&(dst->id), src + ID_OFFSET, ID_SIZE);
  memcpy(&(dst->username), src + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(dst->email), src + EMAIL_OFFSET, EMAIL_SIZE);
}

// TABLE

/**
 * Page size 4 kilobytes because it’s the same size as a page used in the
 * virtual memory systems of most computer architectures.
 *
 * This means one page in our database corresponds to one page used by the
 * operating system. The operating system will move pages in and out of memory
 * as whole units instead of breaking them up.
 */
const uint32_t PAGE_SIZE = 4096;
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

typedef struct {
  uint32_t num_rows;
  void *pages[TABLE_MAX_PAGES];
} Table;

Table *new_table() {
  Table *table = (Table *)malloc(sizeof(Table));
  table->num_rows = 0;
  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    table->pages[i] = NULL;
  }
  return table;
}

void free_table(Table *table) {
  for (uint32_t i = 0; table->pages[i]; i++) {
    free(table->pages[i]);
  }
  free(table);
}

void *row_slot(Table *table, uint32_t row_num) {
  uint32_t num_page = row_num / ROWS_PER_PAGE;
  void *page = table->pages[num_page];

  // Page has not been allocated
  if (page == NULL) {
    page = table->pages[num_page] = malloc(PAGE_SIZE);
  }

  uint32_t row_offset = row_num % ROWS_PER_PAGE;
  uint32_t byte_offset = row_offset * ROW_SIZE;
  return page + byte_offset;
}

void print_prompt() { printf("tinysql > "); }

void print_row(Row *row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

typedef enum {
  PREPARE_SUCCESS,
  PREPARE_NEGATIVE_ID,
  PREPARE_UNRECOGNIZED_STATEMENT,
  PREPARE_STRING_TOO_LONG,
  PREPARE_SYNTAX_ERROR
} PrepareResult;

typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL } ExecuteResult;

typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

typedef struct {
  StatementType type;
  Row row_to_insert;
} Statement;