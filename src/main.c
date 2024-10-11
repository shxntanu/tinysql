#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 100

struct InputBuffer_t {
  char *buffer;
  size_t buffer_length;
  ssize_t input_length;
};
typedef struct InputBuffer_t InputBuffer;

enum ExecuteResult_t {
  EXECUTE_SUCCESS,
  EXECUTE_TABLE_FULL,
  EXECUTE_DUPLICATE_KEY
};
typedef enum ExecuteResult_t ExecuteResult;

enum MetaCommandResult_t {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
};
typedef enum MetaCommandResult_t MetaCommandResult;

enum PrepareResult_t {
  PREPARE_SUCCESS,
  PREPARE_NEGATIVE_ID,
  PREPARE_STRING_TOO_LONG,
  PREPARE_SYNTAX_ERROR,
  PREPARE_UNRECOGNIZED_STATEMENT
};
typedef enum PrepareResult_t PrepareResult;

enum StatementType_t { STATEMENT_INSERT, STATEMENT_SELECT };
typedef enum StatementType_t StatementType;

enum NodeType_t { NODE_INTERNAL, NODE_LEAF };
typedef enum NodeType_t NodeType;

struct Row_t {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
};
typedef struct Row_t Row;

struct Statement_t {
  StatementType type;
  Row row_to_insert; // only used by insert statement
};
typedef struct Statement_t Statement;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

struct Pager_t {
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages;
  void *pages[TABLE_MAX_PAGES];
};
typedef struct Pager_t Pager;

struct Table_t {
  Pager *pager;
  uint32_t root_page_num;
};
typedef struct Table_t Table;

struct Cursor_t {
  Table *table;
  uint32_t page_num;
  uint32_t cell_num;
  bool end_of_table; // Indicates a position one past the last element
};
typedef struct Cursor_t Cursor;

void print_row(Row *row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

/**
 * Nodes need to store some metadata in a header at the beginning of the page.
 * Every node will store what type of node it is, whether or not it is the root
 * node, and a pointer to its parent (to allow finding a node’s siblings). We
 * define constants for the size and offset of every header field.
 *
 * Little space inefficient to use an entire byte per boolean value in the
 * header, but this makes it easier to write code to access those values.
 * ------------------------------------------------------------
 * |            |           |                                 |
 * | Node Type  | Is Root?  |          Parent Pointer         |
 * |  (uint8)   |  (uint8)  |             (uint32)            |
 * |            |           |                                 |
 * ------------------------------------------------------------
 */

const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint32_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

/**
 * In addition to these common header fields, leaf nodes need to store how many
 * “cells” they contain. A cell is a key/value pair.
 * -------------------------------------------------
 * | Common Node Header | Number of cells (uint32) |
 * -------------------------------------------------
 */

const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                       LEAF_NODE_NUM_CELLS_SIZE +
                                       LEAF_NODE_NEXT_LEAF_SIZE;

/**
 * The body of a leaf node is an array of cells. Each cell is a key followed by
 * a value (a serialized row).
 */

const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT =
    (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

NodeType get_node_type(void *node) {
  uint8_t value = *(uint8_t *)(node + NODE_TYPE_OFFSET);
  return (NodeType)value;
}

void set_node_type(void *node, NodeType type) {
  uint8_t value = type;
  *(uint8_t *)(node + NODE_TYPE_OFFSET) = value;
}

bool is_node_root(void *node) {
  uint8_t value = *(uint8_t *)(node + IS_ROOT_OFFSET);
  return (bool)value;
}

void set_node_root(void *node, bool is_root) {
  uint8_t value = is_root;
  *((uint8_t *)(node + IS_ROOT_OFFSET)) = value;
}

// These methods return a pointer to the value in question, so they can be used
// both as a getter and a setter.

/**
 * Returns a pointer to the number of cells in a leaf node.
 *
 * This function calculates the address of the number of cells in a leaf node
 * by adding the offset for the number of cells to the base address of the node.
 *
 * @param node A pointer to the leaf node.
 * @return A pointer to the number of cells in the leaf node.
 */
uint32_t *leaf_node_num_cells(void *node) {
  return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

/**
 * Returns a pointer to a specific cell in a leaf node.
 *
 * This function calculates the address of a specific cell in a leaf node
 * by adding the header size and the offset for the cell number to the base
 * address of the node.
 *
 * @param node A pointer to the leaf node.
 * @param cell_num The index of the cell within the leaf node.
 * @return A pointer to the specified cell in the leaf node.
 */
void *leaf_node_cell(void *node, uint32_t cell_num) {
  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

/**
 * Returns a pointer to the key of a specific cell in a leaf node.
 *
 * This function calculates the address of the key of a specific cell in a leaf
 * node by using the leaf_node_cell function to get the cell and returning the
 * same pointer since the key is the first part of the cell.
 *
 * @param node A pointer to the leaf node.
 * @param cell_num The index of the cell within the leaf node.
 * @return A pointer to the key of the specified cell in the leaf node.
 */
void *leaf_node_key(void *node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num);
}

/**
 * Returns a pointer to the value of a specific cell in a leaf node.
 *
 * This function calculates the address of the value of a specific cell in a
 * leaf node by using the leaf_node_cell function to get the cell and adding the
 * size of the key to the cell address.
 *
 * @param node A pointer to the leaf node.
 * @param cell_num The index of the cell within the leaf node.
 * @return A pointer to the value of the specified cell in the leaf node.
 */
void *leaf_node_value(void *node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

uint32_t *leaf_node_next_leaf(void *node) {
  return (uint32_t *)(node + LEAF_NODE_NEXT_LEAF_OFFSET);
}

/**
 * Initializes a leaf node by setting the number of cells to 0.
 *
 * This function sets the number of cells in a leaf node to 0, effectively
 * initializing the node to an empty state.
 *
 * @param node A pointer to the leaf node.
 */
void initialize_leaf_node(void *node) {
  set_node_type(node, NODE_LEAF);
  set_node_root(node, false);
  *leaf_node_num_cells(node) = 0;
  *leaf_node_next_leaf(node) = 0; // 0 represents no sibling
}

/*
 * Internal Node Header Layout
 *
 * -------------------------------------------------------
 * | Common Node  |  Number of keys  |  Right child      |
 * |    Header    |     (uint32)     |  pointer (uint32) |
 * -------------------------------------------------------
 */
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                           INTERNAL_NODE_NUM_KEYS_SIZE +
                                           INTERNAL_NODE_RIGHT_CHILD_SIZE;

/*
 * Internal Node Body Layout
 *
 */
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;

uint32_t *internal_node_num_keys(void *node) {
  return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t *internal_node_right_child(void *node) {
  return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

uint32_t *internal_node_cell(void *node, uint32_t cell_num) {
  return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t *internal_node_child(void *node, uint32_t child_num) {
  uint32_t num_keys = *internal_node_num_keys(node);
  if (child_num > num_keys) {
    printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
    exit(EXIT_FAILURE);
  } else if (child_num == num_keys) {
    return internal_node_right_child(node);
  } else {
    return internal_node_cell(node, child_num);
  }
}

uint32_t *internal_node_key(void *node, uint32_t key_num) {
  return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

uint32_t get_node_max_key(void *node) {
  switch (get_node_type(node)) {
  case NODE_INTERNAL:
    return *internal_node_key(node, *internal_node_num_keys(node) - 1);
  case NODE_LEAF:
    return *(uint32_t *)leaf_node_key(node, *leaf_node_num_cells(node) - 1);
  }
}

void initialize_internal_node(void *node) {
  set_node_type(node, NODE_INTERNAL);
  set_node_root(node, false);
  *internal_node_num_keys(node) = 0;
}

void print_constants() {
  printf("ROW_SIZE: %d\n", ROW_SIZE);
  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

void indent(uint32_t level) {
  for (uint32_t i = 0; i < level; i++) {
    printf("  ");
  }
}

void serialize_row(Row *source, void *destination) {
  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
  memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
  memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserialize_row(void *source, Row *destination) {
  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void *get_page(Pager *pager, uint32_t page_num) {
  if (page_num > TABLE_MAX_PAGES) {
    printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
           TABLE_MAX_PAGES);
    exit(EXIT_FAILURE);
  }

  if (pager->pages[page_num] == NULL) {
    // Cache miss. Allocate memory and load from file.
    void *page = malloc(PAGE_SIZE);
    uint32_t num_pages = pager->file_length / PAGE_SIZE;

    // We might save a partial page at the end of the file
    if (pager->file_length % PAGE_SIZE) {
      num_pages += 1;
    }

    if (page_num <= num_pages) {
      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
      if (bytes_read == -1) {
        printf("Error reading file: %d\n", errno);
        exit(EXIT_FAILURE);
      }
    }

    pager->pages[page_num] = page;

    if (page_num >= pager->num_pages) {
      pager->num_pages = page_num + 1;
    }
  }

  return pager->pages[page_num];
}

void print_tree(Pager *pager, uint32_t page_num, uint32_t indentation_level) {
  void *node = get_page(pager, page_num);
  uint32_t num_keys, child;

  switch (get_node_type(node)) {
  case (NODE_LEAF):
    num_keys = *leaf_node_num_cells(node);
    indent(indentation_level);
    printf("- leaf (size %d)\n", num_keys);
    for (uint32_t i = 0; i < num_keys; i++) {
      indent(indentation_level + 1);
      printf("- %d\n", *(uint32_t *)leaf_node_key(node, i));
    }
    break;
  case (NODE_INTERNAL):
    num_keys = *internal_node_num_keys(node);
    indent(indentation_level);
    printf("- internal (size %d)\n", num_keys);
    for (uint32_t i = 0; i < num_keys; i++) {
      child = *internal_node_child(node, i);
      print_tree(pager, child, indentation_level + 1);

      indent(indentation_level + 1);
      printf("- key %d\n", *internal_node_key(node, i));
    }
    child = *internal_node_right_child(node);
    print_tree(pager, child, indentation_level + 1);
    break;
  }
}

/**
 * Uses binary search to search for a given key. If a given key is not
 * found, it returns position after which the key needs to be inserted
 *
 * @param table the table to be searched
 * @param page_num the page in which the key needs to be searched
 * @param key the key which needs to be searched for
 *
 * @return A pointer to the cursor at the given position
 */
Cursor *leaf_node_find(Table *table, uint32_t page_num, uint32_t key) {
  void *node = get_page(table->pager, page_num);
  uint32_t num_cells = *(uint32_t *)leaf_node_num_cells(node);

  Cursor *cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->page_num = page_num;

  uint32_t min_index = 0;
  uint32_t one_past_max_index = num_cells;
  while (one_past_max_index != min_index) {
    uint32_t index = (one_past_max_index + min_index) / 2;
    uint32_t key_at_index = *(uint32_t *)leaf_node_key(node, index);
    if (key == key_at_index) {
      cursor->cell_num = index;
      return cursor;
    }
    if (key < key_at_index) {
      one_past_max_index = index;
    } else {
      min_index = index + 1;
    }
  }

  // Key not found
  // return one position before where the key needs to be inserted
  cursor->cell_num = min_index;
  return cursor;
}

Cursor *internal_node_find(Table *table, uint32_t page_num, uint32_t key) {
  void *node = get_page(table->pager, page_num);
  uint32_t num_keys = *internal_node_num_keys(node);

  /* Binary Search */
  uint32_t min_index = 0;
  uint32_t max_index = num_keys;

  while (min_index != max_index) {
    uint32_t index = (min_index + max_index) / 2;
    uint32_t key_to_right = *internal_node_key(node, index);
    if (key_to_right >= key) {
      max_index = index;
    } else {
      min_index = index + 1;
    }
  }

  uint32_t child_num = *internal_node_child(node, min_index);
  void *child = get_page(table->pager, child_num);
  switch (get_node_type(child)) {
  case NODE_LEAF:
    return leaf_node_find(table, child_num, key);
  case NODE_INTERNAL:
    return internal_node_find(table, page_num, key);
  }
}

/**
 * Return the position of the given key.
 * If the key is not present, return the position
 * where it should be inserted
 *
 * @param table the table to be searched
 * @param key the key that needs to be searched
 *
 * @return A pointer to the cursor at the given position
 */
Cursor *table_find(Table *table, uint32_t key) {
  uint32_t root_page_num = table->root_page_num;
  void *root_node = get_page(table->pager, root_page_num);

  if (get_node_type(root_node) == NODE_LEAF) {
    return leaf_node_find(table, root_page_num, key);
  } else {
    return internal_node_find(table, root_page_num, key);
  }
}

void *cursor_value(Cursor *cursor) {
  uint32_t page_num = cursor->page_num;
  void *page = get_page(cursor->table->pager, page_num);
  return leaf_node_value(page, cursor->cell_num);
}

void cursor_advance(Cursor *cursor) {
  uint32_t page_num = cursor->page_num;
  void *node = get_page(cursor->table->pager, page_num);

  cursor->cell_num += 1;
  if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
    cursor->end_of_table = true;
  }
}

Cursor *table_start(Table *table) {
  Cursor *cursor = table_find(table, 0);

  void *node = get_page(table->pager, cursor->page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);
  cursor->end_of_table = (num_cells == 0);

  return cursor;
}

Pager *pager_open(const char *filename) {
  int fd = open(filename,
                O_RDWR |     // Read/Write mode
                    O_CREAT, // Create file if it does not exist
                S_IWUSR |    // User write permission
                    S_IRUSR  // User read permission
  );

  if (fd == -1) {
    printf("Unable to open file\n");
    exit(EXIT_FAILURE);
  }

  off_t file_length = lseek(fd, 0, SEEK_END);

  Pager *pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_length = file_length;
  pager->num_pages = (file_length / PAGE_SIZE);

  if (file_length % PAGE_SIZE != 0) {
    printf("Db file is not a whole number of pages. Corrupt file.\n");
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    pager->pages[i] = NULL;
  }

  return pager;
}

Table *db_open(const char *filename) {
  Pager *pager = pager_open(filename);

  Table *table = malloc(sizeof(Table));
  table->pager = pager;

  if (pager->num_pages == 0) {
    // New database file. Initialze page 0 as leaf node.
    void *root_node = get_page(pager, 0);
    initialize_leaf_node(root_node);
    set_node_root(root_node, true);
  }

  return table;
}

InputBuffer *new_input_buffer() {
  InputBuffer *input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;

  return input_buffer;
}

void print_prompt() { printf("tinysql > "); }

void read_input(InputBuffer *input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }

  // Ignore trailing newline
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}

void pager_flush(Pager *pager, uint32_t page_num) {
  if (pager->pages[page_num] == NULL) {
    printf("Tried to flush null page\n");
    exit(EXIT_FAILURE);
  }

  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

  if (offset == -1) {
    printf("Error seeking: %d\n", errno);
    exit(EXIT_FAILURE);
  }

  ssize_t bytes_written =
      write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

  if (bytes_written == -1) {
    printf("Error writing: %d\n", errno);
    exit(EXIT_FAILURE);
  }
}

void db_close(Table *table) {
  Pager *pager = table->pager;

  for (uint32_t i = 0; i < pager->num_pages; i++) {
    if (pager->pages[i] == NULL) {
      continue;
    }
    pager_flush(pager, i);
    free(pager->pages[i]);
    pager->pages[i] = NULL;
  }

  int result = close(pager->file_descriptor);
  if (result == -1) {
    printf("Error closing db file.\n");
    exit(EXIT_FAILURE);
  }
  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    void *page = pager->pages[i];
    if (page) {
      free(page);
      pager->pages[i] = NULL;
    }
  }
  free(pager);
}

MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    db_close(table);
    exit(EXIT_SUCCESS);
  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
    printf("Tree:\n");
    print_tree(table->pager, 0, 0);
    return META_COMMAND_SUCCESS;
  } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
    printf("Constants:\n");
    print_constants();
    return META_COMMAND_SUCCESS;
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement) {
  statement->type = STATEMENT_INSERT;

  char *keyword = strtok(input_buffer->buffer, " ");
  char *id_string = strtok(NULL, " ");
  char *username = strtok(NULL, " ");
  char *email = strtok(NULL, " ");

  if (id_string == NULL || username == NULL || email == NULL) {
    return PREPARE_SYNTAX_ERROR;
  }

  int id = atoi(id_string);
  if (id < 0) {
    return PREPARE_NEGATIVE_ID;
  }
  if (strlen(username) > COLUMN_USERNAME_SIZE) {
    return PREPARE_STRING_TOO_LONG;
  }
  if (strlen(email) > COLUMN_EMAIL_SIZE) {
    return PREPARE_STRING_TOO_LONG;
  }

  statement->row_to_insert.id = id;
  strcpy(statement->row_to_insert.username, username);
  strcpy(statement->row_to_insert.email, email);

  return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer *input_buffer,
                                Statement *statement) {
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
    return prepare_insert(input_buffer, statement);
  }
  if (strcmp(input_buffer->buffer, "select") == 0) {
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
  }

  return PREPARE_UNRECOGNIZED_STATEMENT;
}

/*
 * Until we start recycling free pages, new pages will always
 * go onto the end of the database file.
 *
 * For now, we’re assuming that in a database with N pages, page numbers 0
 * through N-1 are allocated. Therefore we can always allocate page number N for
 * new pages. Eventually after we implement deletion, some pages may become
 * empty and their page numbers unused. To be more efficient, we could
 * re-allocate those free pages.
 */
uint32_t get_unused_page_num(Pager *pager) { return pager->num_pages; }

void create_new_root(Table *table, uint32_t right_child_page_num) {
  void *root = get_page(table->pager, table->root_page_num);
  void *right_child = get_page(table->pager, right_child_page_num);
  uint32_t left_child_page_num = get_unused_page_num(table->pager);
  void *left_child = get_page(table->pager, left_child_page_num);

  memcpy(left_child, root, PAGE_SIZE);
  set_node_root(left_child, false);

  /* Root node is a new internal node with one key and two children */
  initialize_internal_node(root);
  set_node_root(root, true);
  *internal_node_num_keys(root) = 1;
  *internal_node_child(root, 0) = left_child_page_num;
  uint32_t left_child_max_key = get_node_max_key(left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
}

/*
 * Create a new node and move half the cells over.
 * Insert the new value in one of the two nodes.
 * Update parent or create a new parent.
 */
void leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value) {
  void *old_node = get_page(cursor->table->pager, cursor->page_num);
  uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
  void *new_node = get_page(cursor->table->pager, new_page_num);
  initialize_leaf_node(new_node);

  /** Whenever we split a leaf node, update the sibling pointers. The old leaf’s
   * sibling becomes the new leaf, and the new leaf’s sibling becomes whatever
   * used to be the old leaf’s sibling. */
  *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
  *leaf_node_next_leaf(old_node) = new_page_num;

  for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
    void *destination_node;
    if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
      destination_node = new_node;
    } else {
      destination_node = old_node;
    }

    uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
    void *destination = leaf_node_cell(destination_node, index_within_node);
    if (i == cursor->cell_num) {
      serialize_row(value, destination);
    } else if (i > cursor->cell_num) {
      memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
    } else {
      memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
    }
  }

  *(leaf_node_num_cells(old_node)) = LEAF_NODE_LEFT_SPLIT_COUNT;
  *(leaf_node_num_cells(new_node)) = LEAF_NODE_RIGHT_SPLIT_COUNT;

  if (is_node_root(old_node)) {
    return create_new_root(cursor->table, new_page_num);
  } else {
    printf("Need to implement updating parent after split\n");
    exit(EXIT_FAILURE);
  }
}

/**
 * Inserts a key-value pair into a leaf node of a B-tree.
 *
 * This function handles the insertion of a key-value pair into a leaf node.
 * If the node is full, it prints an error message and exits. Node splitting
 * is required but not implemented in this function.
 *
 * @param cursor A pointer to the Cursor structure, which indicates the position
 *               in the table where the insertion should occur.
 * @param key The key to be inserted into the leaf node.
 * @param value A pointer to the Row structure containing the value to be
 * inserted.
 */
void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value) {
  void *node = get_page(cursor->table->pager, cursor->page_num);

  uint32_t num_cells = *leaf_node_num_cells(node);

  // Node full
  if (num_cells >= LEAF_NODE_MAX_CELLS) {
    leaf_node_split_and_insert(cursor, key, value);
    return;
  }

  /**
   * If the cursor's cell number is less than the current number of cells, it
   * means the new cell needs to be inserted in the middle. We shift the
   * existing cells to the right to make room for the new cell.
   */
  if (cursor->cell_num < num_cells) {
    for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
      memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
             LEAF_NODE_CELL_SIZE);
    }
  }

  *(leaf_node_num_cells(node)) += 1;
  *((uint32_t *)leaf_node_key(node, cursor->cell_num)) = key;
  serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

ExecuteResult execute_insert(Statement *statement, Table *table) {
  void *node = get_page(table->pager, table->root_page_num);
  uint32_t num_cells = *(uint32_t *)leaf_node_num_cells(node);

  Row *row_to_insert = &(statement->row_to_insert);

  uint32_t key_to_insert = row_to_insert->id;
  Cursor *cursor = table_find(table, key_to_insert);

  // if the position in which the key needs to be inserted lies in between the
  // cells, check for duplicate keys
  if (cursor->cell_num < num_cells) {
    uint32_t key_at_index = *(uint32_t *)leaf_node_key(node, cursor->cell_num);
    if (key_at_index == key_to_insert) {
      return EXECUTE_DUPLICATE_KEY;
    }
  }

  leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

  free(cursor);

  return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement *statement, Table *table) {
  Cursor *cursor = table_start(table);

  Row row;
  while (!(cursor->end_of_table)) {
    deserialize_row(cursor_value(cursor), &row);
    print_row(&row);
    cursor_advance(cursor);
  }

  free(cursor);

  return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement *statement, Table *table) {
  switch (statement->type) {
  case (STATEMENT_INSERT):
    return execute_insert(statement, table);
  case (STATEMENT_SELECT):
    return execute_select(statement, table);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Must supply a database filename.\n");
    exit(EXIT_FAILURE);
  }

  char *filename = argv[1];
  Table *table = db_open(filename);

  InputBuffer *input_buffer = new_input_buffer();
  while (true) {
    print_prompt();
    read_input(input_buffer);

    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer, table)) {
      case (META_COMMAND_SUCCESS):
        continue;
      case (META_COMMAND_UNRECOGNIZED_COMMAND):
        printf("Unrecognized command '%s'\n", input_buffer->buffer);
        continue;
      }
    }

    Statement statement;
    switch (prepare_statement(input_buffer, &statement)) {
    case (PREPARE_SUCCESS):
      break;
    case (PREPARE_NEGATIVE_ID):
      printf("ID must be positive.\n");
      continue;
    case (PREPARE_STRING_TOO_LONG):
      printf("String is too long.\n");
      continue;
    case (PREPARE_SYNTAX_ERROR):
      printf("Syntax error. Could not parse statement.\n");
      continue;
    case (PREPARE_UNRECOGNIZED_STATEMENT):
      printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
      continue;
    }

    switch (execute_statement(&statement, table)) {
    case (EXECUTE_SUCCESS):
      printf("Executed.\n");
      break;
    case (EXECUTE_TABLE_FULL):
      printf("Error: Table full.\n");
      break;
    case (EXECUTE_DUPLICATE_KEY):
      printf("Error: Duplicate Key.\n");
      break;
    }
  }

  free(input_buffer);
  for (int i = 0; i < TABLE_MAX_PAGES; i++) {
    free(table->pager->pages[i]);
  }
  free(table->pager);
  free(table);
}
