#include "db.h"

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
  uint32_t left_child_max_key = get_node_max_key(table->pager, left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
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

uint32_t *internal_node_num_keys(void *node) {
  return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t *internal_node_right_child(void *node) {
  return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

uint32_t *internal_node_cell(void *node, uint32_t cell_num) {
  return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t *node_parent(void *node) { return node + PARENT_POINTER_OFFSET; }

uint32_t get_node_max_key(Pager *pager, void *node) {
  if (get_node_type(node) == NODE_LEAF) {
    return *(uint32_t *)leaf_node_key(node, *leaf_node_num_cells(node) - 1);
  }
  void *right_child = get_page(pager, *internal_node_right_child(node));
  return get_node_max_key(pager, right_child);
}

uint32_t *internal_node_child(void *node, uint32_t child_num) {
  uint32_t num_keys = *internal_node_num_keys(node);
  if (child_num > num_keys) {
    printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
    exit(EXIT_FAILURE);
  } else if (child_num == num_keys) {
    uint32_t *right_child = internal_node_right_child(node);
    if (*right_child == INVALID_PAGE_NUM) {
      printf("Tried to access right child of node, but was invalid page\n");
      exit(EXIT_FAILURE);
    }
    return right_child;
  } else {
    uint32_t *child = internal_node_cell(node, child_num);
    if (*child == INVALID_PAGE_NUM) {
      printf("Tried to access child %d of node, but was invalid page\n",
             child_num);
      exit(EXIT_FAILURE);
    }
    return child;
  }
}

uint32_t *internal_node_key(void *node, uint32_t key_num) {
  return (void *)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

void internal_node_insert(Table *table, uint32_t parent_page_num,
                          uint32_t child_page_num) {
  /*
  Add a new child/key pair to parent that corresponds to child
  */

  void *parent = get_page(table->pager, parent_page_num);
  void *child = get_page(table->pager, child_page_num);
  uint32_t child_max_key = get_node_max_key(table->pager, child);
  uint32_t index = internal_node_find_child(parent, child_max_key);

  uint32_t original_num_keys = *internal_node_num_keys(parent);

  if (original_num_keys >= INTERNAL_NODE_MAX_KEYS) {
    internal_node_split_and_insert(table, parent_page_num, child_page_num);
    return;
  }

  uint32_t right_child_page_num = *internal_node_right_child(parent);
  /*
  An internal node with a right child of INVALID_PAGE_NUM is empty
  */
  if (right_child_page_num == INVALID_PAGE_NUM) {
    *internal_node_right_child(parent) = child_page_num;
    return;
  }

  void *right_child = get_page(table->pager, right_child_page_num);
  /*
  If we are already at the max number of cells for a node, we cannot increment
  before splitting. Incrementing without inserting a new key/child pair
  and immediately calling internal_node_split_and_insert has the effect
  of creating a new key at (max_cells + 1) with an uninitialized value
  */
  *internal_node_num_keys(parent) = original_num_keys + 1;

  if (child_max_key > get_node_max_key(table->pager, right_child)) {
    /* Replace right child */
    *internal_node_child(parent, original_num_keys) = right_child_page_num;
    *internal_node_key(parent, original_num_keys) =
        get_node_max_key(table->pager, right_child);
    *internal_node_right_child(parent) = child_page_num;
  } else {
    /* Make room for the new cell */
    for (uint32_t i = original_num_keys; i > index; i--) {
      void *destination = internal_node_cell(parent, i);
      void *source = internal_node_cell(parent, i - 1);
      memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
    }
    *internal_node_child(parent, index) = child_page_num;
    *internal_node_key(parent, index) = child_max_key;
  }
}

void update_internal_node_key(void *node, uint32_t old_key, uint32_t new_key) {
  uint32_t old_child_index = internal_node_find_child(node, old_key);
  *internal_node_key(node, old_child_index) = new_key;
}

uint32_t internal_node_find_child(void *node, uint32_t key) {
  /*
  Return the index of the child which should contain
  the given key.
  */

  uint32_t num_keys = *internal_node_num_keys(node);

  /* Binary search */
  uint32_t min_index = 0;
  uint32_t max_index = num_keys; /* there is one more child than key */

  while (min_index != max_index) {
    uint32_t index = (min_index + max_index) / 2;
    uint32_t key_to_right = *internal_node_key(node, index);
    if (key_to_right >= key) {
      max_index = index;
    } else {
      min_index = index + 1;
    }
  }

  return min_index;
}

void initialize_internal_node(void *node) {
  set_node_type(node, NODE_INTERNAL);
  set_node_root(node, false);
  *internal_node_num_keys(node) = 0;

  /*
  Necessary because the root page number is 0; by not initializing an internal
  node's right child to an invalid page number when initializing the node, we
  may end up with 0 as the node's right child, which makes the node a parent of
  the root
  */
  *internal_node_right_child(node) = INVALID_PAGE_NUM;
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
  uint32_t num_cells = *leaf_node_num_cells(node);

  Cursor *cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->page_num = page_num;
  cursor->end_of_table = false;

  // Binary search
  uint32_t min_index = 0;
  uint32_t one_past_max_index = num_cells;
  while (one_past_max_index != min_index) {
    uint32_t index = (min_index + one_past_max_index) / 2;
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

  cursor->cell_num = min_index;
  return cursor;
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

/*
 * Create a new node and move half the cells over.
 * Insert the new value in one of the two nodes.
 * Update parent or create a new parent.
 */
void leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value) {
  void *old_node = get_page(cursor->table->pager, cursor->page_num);
  uint32_t old_max = get_node_max_key(cursor->table->pager, old_node);
  uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
  void *new_node = get_page(cursor->table->pager, new_page_num);
  initialize_leaf_node(new_node);

  *(uint32_t *)(new_node) = *(uint32_t *)node_parent(old_node);

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
    uint32_t parent_page_num = *(uint32_t *)node_parent(old_node);
    uint32_t new_max = get_node_max_key(cursor->table->pager, old_node);
    void *parent = get_page(cursor->table->pager, parent_page_num);

    update_internal_node_key(parent, old_max, new_max);
    internal_node_insert(cursor->table, parent_page_num, new_page_num);
    return;
  }
}

void internal_node_split_and_insert(Table *table, uint32_t parent_page_num,
                                    uint32_t child_page_num) {
  uint32_t old_page_num = parent_page_num;
  void *old_node = get_page(table->pager, parent_page_num);
  uint32_t old_max = get_node_max_key(table->pager, old_node);
  void *child = get_page(table->pager, child_page_num);
  uint32_t child_max = get_node_max_key(table->pager, child);
  uint32_t new_page_num = get_unused_page_num(table->pager);

  /*
   Declaring a flag before updating pointers which
   records whether this operation involves splitting the root -
   if it does, we will insert our newly created node during
   the step where the table's new root is created. If it does
   not, we have to insert the newly created node into its parent
   after the old node's keys have been transferred over. We are not
   able to do this if the newly created node's parent is not a newly
   initialized root node, because in that case its parent may have existing
   keys aside from our old node which we are splitting. If that is true, we
   need to find a place for our newly created node in its parent, and we
   cannot insert it at the correct index if it does not yet have any keys
   */
  uint32_t splitting_root = is_node_root(old_node);

  void *parent;
  void *new_node;
  if (splitting_root) {
    create_new_root(table, new_page_num);
    parent = get_page(table->pager, table->root_page_num);
    /*
    If we are splitting the root, we need to update old_node to point
    to the new root's left child, new_page_num will already point to
    the new root's right child
    */
    old_page_num = *internal_node_child(parent, 0);
    old_node = get_page(table->pager, old_page_num);
  } else {
    parent = get_page(table->pager, *node_parent(old_node));
    new_node = get_page(table->pager, new_page_num);
    initialize_internal_node(new_node);
  }

  uint32_t *old_num_keys = internal_node_num_keys(old_node);

  uint32_t cur_page_num = *internal_node_right_child(old_node);
  void *cur = get_page(table->pager, cur_page_num);

  /*
  First put right child into new node and set right child of old node to invalid
  page number
  */
  internal_node_insert(table, new_page_num, cur_page_num);
  *node_parent(cur) = new_page_num;
  *internal_node_right_child(old_node) = INVALID_PAGE_NUM;
  /*
  For each key until you get to the middle key, move the key and the child to
  the new node
  */
  for (int i = INTERNAL_NODE_MAX_CELLS - 1; i > INTERNAL_NODE_MAX_CELLS / 2;
       i--) {
    cur_page_num = *internal_node_child(old_node, i);
    cur = get_page(table->pager, cur_page_num);

    internal_node_insert(table, new_page_num, cur_page_num);
    *node_parent(cur) = new_page_num;

    (*old_num_keys)--;
  }

  /*
  Set child before middle key, which is now the highest key, to be node's right
  child, and decrement number of keys
  */
  *internal_node_right_child(old_node) =
      *internal_node_child(old_node, *old_num_keys - 1);
  (*old_num_keys)--;

  /*
  Determine which of the two nodes after the split should contain the child to
  be inserted, and insert the child
  */
  uint32_t max_after_split = get_node_max_key(table->pager, old_node);

  uint32_t destination_page_num =
      child_max < max_after_split ? old_page_num : new_page_num;

  internal_node_insert(table, destination_page_num, child_page_num);
  *node_parent(child) = destination_page_num;

  update_internal_node_key(parent, old_max,
                           get_node_max_key(table->pager, old_node));

  if (!splitting_root) {
    internal_node_insert(table, *node_parent(old_node), new_page_num);
    *node_parent(new_node) = *node_parent(old_node);
  }
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