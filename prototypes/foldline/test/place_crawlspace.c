static int crawl_node(const tide_world *world)
{
    return tide_get_CrawlHunt(world, find_beast(world, BeastKind_Spider)).node;
}

// A parcel brought down from the airlock, quarter-sized now, thrown from the first recess at the far strand: the spider
// runs east to wrap it, and the way to the second recess is clear of its legs.
static void crawl_to_refuge(tide_world *world, int id, float x)
{
    walk_to(world, id, x, -.5f);
    act(world, id, (PilotInput){.carry = true});
    CHECK(world->Fold.holding.at[id] == id + 1);
    walk_to(world, id, 0, 2);
    cross_fold(world, id, (tide_float3){0, 1.3f, 4}, 160);
    CHECK(near(pilot(world, id).scale, .25f, .001f));
    walk_to(world, id, -6, 8);
    walk_to(world, id, -6, 16);
    walk_to(world, id, -3, 16);
    walk_to(world, id, -3, 17.2f);
    // Out at the recess's mouth, lobbed over the near strand, under the roof, onto the far one
    walk_to(world, id, -3, 16.2f);
    aim_at(world, id, (tide_float3){3, 2.6f, 16});
    act(world, id, (PilotInput){.hurl = true});
    for (int tick = 0; tick < 300 && !(crawl_node(world) == 4 && tide_get_CrawlHunt(world, find_beast(world, BeastKind_Spider)).bait); tick++)
        tide_world_tick(world);
    CHECK(crawl_node(world) == 4 && tide_get_CrawlHunt(world, find_beast(world, BeastKind_Spider)).bait);
    walk_to(world, id, -3, 16.3f);
    walk_to(world, id, 1.3f, 16.3f);
    walk_to(world, id, 1.3f, 14.7f);
    walk_to(world, id, id == 0 ? 1.65f : 2.35f, 14.4f);
    act(world, id, (PilotInput){.carry = true});
    if (world->Fold.holding.at[id] < 3)
        fprintf(stderr, "crawl pickup: scale %.3f at %.3f %.3f; parcels (%.3f %.3f %.3f)x%.3f (%.3f %.3f %.3f)x%.3f strained %.3f\n",
                pilot(world,id).scale, pilot(world,id).position.x, pilot(world,id).position.z,
                world->Fold.spots.at[2].x, world->Fold.spots.at[2].y, world->Fold.spots.at[2].z, world->Fold.sizes.at[2],
                world->Fold.spots.at[3].x, world->Fold.spots.at[3].y, world->Fold.spots.at[3].z, world->Fold.sizes.at[3], world->Fold.strained.at[id]);
    CHECK(world->Fold.holding.at[id] >= 3);
    CHECK(world->Arena.caught == 0);
}

// From the second recess, a spare parcel at the strand behind: while the spider wraps it, by its hub and up to the
// last strand, whose touch brings it again, and into the third recess before it comes.
static void crawl_past_spider(tide_world *world, int id)
{
    // From just inside the recess, which it runs past: thrown out of its mouth and along the way
    walk_to(world, id, 1.6f, 15.1f);
    aim_at(world, id, (tide_float3){-2, 1.2f, 16.5f});
    act(world, id, (PilotInput){.hurl = true});
    for (int tick = 0; tick < 300 && tide_get_CrawlHunt(world, find_beast(world, BeastKind_Spider)).goal != 3; tick++) tide_world_tick(world);
    CHECK(tide_get_CrawlHunt(world, find_beast(world, BeastKind_Spider)).goal == 3);
    for (int tick = 0; tick < 300 && crawl_node(world) != 3; tick++) tide_world_tick(world);
    CHECK(crawl_node(world) == 3 && world->Arena.caught == 0);
    walk_to(world, id, 1.3f, 16);
    walk_to(world, id, 5, 16);
    walk_to(world, id, 5, 23);
    walk_to(world, id, .5f, 23);
    walk_to(world, id, id == 0 ? -1.5f : -.5f, 24.6f);
    CHECK(tide_get_CrawlHunt(world, find_beast(world, BeastKind_Spider)).goal == 5);
    ticks(world, 300);
    CHECK(world->Arena.caught == 0);
}

static void crawl_leave(tide_world *world, int id, float lift_x)
{
    walk_to(world, id, id == 0 ? -1.5f : -.5f, 23);
    walk_to(world, id, -5, 23);
    walk_to(world, id, -5, 29);
    walk_to(world, id, 11, 29);
    cross_fold(world, id, (tide_float3){12, .325f, 29}, 200);
    CHECK(near(pilot(world, id).scale, 1, .001f));
    walk_to(world, id, lift_x, 31);
    walk_to(world, id, lift_x, 35.1f);
}

static void place_crawlspace_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, CRAWLSPACE);
    ticks(&world, 100);
    CHECK(world.Place.dead && world.Fold.ends.at[1].size == .25f && world.Fold.ends.at[2].size == .25f);
    crawl_to_refuge(&world, 0, -2);
    crawl_past_spider(&world, 0);
    crawl_to_refuge(&world, 1, 2);
    crawl_past_spider(&world, 1);
    crawl_leave(&world, 0, 18.8f);
    crawl_leave(&world, 1, 21.2f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != CRAWLSPACE);
    tide_world_free(&world);

    // A body touching the first strand without a parcel brings the spider
    // down the track. Its ordinary catch rebuilds the passage for both.
    start(&world);
    choose(&world, 0, CRAWLSPACE);
    ticks(&world, 100);
    walk_to(&world, 0, 0, 2);
    cross_fold(&world, 0, (tide_float3){0, 1.3f, 4}, 160);
    walk_to(&world, 0, -6, 8);
    walk_to(&world, 0, -6, 16);
    aim_at(&world, 0, (tide_float3){5, .3f, 16});
    input(&world, 0, (PilotInput){.move = {0, 1}});
    bool caught = false;
    for (int step = 0; step < 900 && !caught; step++) {
        tide_world_tick(&world);
        caught = world.Arena.caught > 0;
    }
    CHECK(caught);
    input(&world, 0, (PilotInput){0});
    const int before_reset = world.Arena.chamber;
    ticks(&world, 100);
    CHECK(world.Arena.chamber == before_reset + 1 && world.Place.id == CRAWLSPACE);
    CHECK(pilot(&world, 0).scale == 1 && pilot(&world, 1).scale == 1);
    CHECK(pilot(&world, 0).position.z < -4 && pilot(&world, 1).position.z < -4);
    tide_world_free(&world);
}
