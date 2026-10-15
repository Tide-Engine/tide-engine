#include "tide/rows.h"

uint32_t visibility_present_rows;
uint32_t visibility_absent_rows;

void CountPresent(tide_rows *rows)
{
    visibility_present_rows += tide_rows_count(rows);
}

void CountAbsent(tide_rows *rows)
{
    visibility_absent_rows += tide_rows_count(rows);
}
