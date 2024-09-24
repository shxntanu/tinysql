#ifndef STATEMENT_H
#define STATEMENT_H

#include "row.h"

enum StatementType_t { STATEMENT_INSERT, STATEMENT_SELECT };
typedef enum StatementType_t StatementType;

struct Statement_t {
  StatementType type;
  Row row_to_insert; // only used by insert statement
};
typedef struct Statement_t Statement;

#endif // STATEMENT_H