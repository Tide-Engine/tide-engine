#include "tide/embed_game.h"
#include "tide_test.h"

TIDE_TEST(embed_packet_places_use_complete_logical_keys_across_private_slots)
{
    tide_entities a = {0}, b = {0};
    const tide_entity ka = {0x12345678abcdef01ull, 0xfedcba9876543210ull};
    const tide_entity kb = {ka.birth ^ (1ull << 63), ka.lineage};
    TIDE_REQUIRE(tide_entity_install(&a, ka));
    TIDE_REQUIRE(tide_entity_install(&b, kb));
    TIDE_CHECK(tide_entity_slot_index(&a, ka) == 0 && tide_entity_slot_index(&b, kb) == 0);
    tide_packet p = {0};
    tide_packet_begin(&p);
    tide_packet_entity_place(&p, ka, 2);
    tide_packet_entity_place(&p, kb, 9);
    TIDE_CHECK(tide_packet_find_entity(&p, ka) == 2);
    TIDE_CHECK(tide_packet_find_entity(&p, kb) == 9);
    tide_packet_entity_place(&p, ka, 7); // A later selected context replaces the same full key only.
    TIDE_CHECK(tide_packet_find_entity(&p, ka) == 7 && p.entity_place_count == 2);
    tide_packet_entity_place(&p, (tide_entity){0}, 1);
    TIDE_CHECK(tide_packet_find_entity(&p, (tide_entity){0}) == TIDE_PACKET_NONE);
    for (uint32_t i = 0; i < 3000; i++) tide_packet_entity_place(&p, (tide_entity){5, (uint64_t)i + 1u}, i);
    for (uint32_t i = 0; i < 3000; i++) TIDE_REQUIRE(tide_packet_find_entity(&p, (tide_entity){5, (uint64_t)i + 1u}) == i);
    const uint32_t capacity = p.entity_place_capacity;
    tide_packet_begin(&p);
    TIDE_CHECK(p.entity_place_count == 0 && p.entity_place_capacity == capacity);
    TIDE_CHECK(tide_packet_find_entity(&p, ka) == TIDE_PACKET_NONE);
    tide_packet_free(&p);
    TIDE_CHECK(p.entity_places == NULL && p.entity_place_capacity == 0);
    tide_entities_free(&a);
    tide_entities_free(&b);
}
