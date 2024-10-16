#include "db.h"

void indent(uint32_t level) {
  for (uint32_t i = 0; i < level; i++) {
    printf("  ");
  }
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

void print_constants() {
  printf("ROW_SIZE: %d\n", ROW_SIZE);
  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

void print_row(Row *row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
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