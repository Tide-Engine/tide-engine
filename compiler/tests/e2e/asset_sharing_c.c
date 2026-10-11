#include <stdint.h>
#include "tide/list.h"

void c_asset_edit(uint8_t *bytes, const int32_t count)
{
    if (count > 3) bytes[3] = 93;
}

void c_asset_out(tide_list_out *bytes)
{
    uint8_t *room = tide_list_out_room(bytes, 2);
    room[0] = 94;
    room[1] = 95;
    tide_list_out_count(bytes, 2);
}
