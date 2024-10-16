#include "db.h"

Table *db_open(const char *filename) {
  Pager *pager = pager_open(filename);

  Table *table = malloc(sizeof(Table));
  table->pager = pager;
  table->root_page_num = 0;

  if (pager->num_pages == 0) {
    // New database file. Initialize page 0 as leaf node.
    void *root_node = get_page(pager, 0);
    initialize_leaf_node(root_node);
    set_node_root(root_node, true);
  }

  return table;
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
