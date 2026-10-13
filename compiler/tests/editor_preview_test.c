#include "tide/editor_preview.h"
#include "tide_test.h"

const tide_editor_preview_api *tide_editor_preview(void);
static int observed;
void Observe(int tag) { observed += tag; }

TIDE_TEST(editor_preview_authored_tree_and_isolation)
{
    const tide_editor_preview_api *api = tide_editor_preview();
    TIDE_REQUIRE(api && api->version == TIDE_EDITOR_PREVIEW_VERSION && api->views == 4);
    void *context = api->create(NULL, 0);
    TIDE_REQUIRE(context);
    TIDE_CHECK(observed == 0); // No Main, Spawned or runtime-only view startup.
    for (int frame = 0; frame < 2; frame++) {
        const tide_draw_list *draw = api->frame(context, .02f, (tide_rect){50,60,400+frame*100,300},
            tide_identity_f4x4(), tide_identity_f4x4(), NULL);
        TIDE_REQUIRE(draw);
        int circles = 0;
        for (uint32_t i = 0; i < draw->count; i++) {
            const tide_draw_command *c = &draw->commands[i];
            if (c->kind != TIDE_DRAW_CIRCLE) continue;
            TIDE_CHECK(c->a.x == 11 || c->a.x == 23); // Defaults/overrides, never the system's +100.
            TIDE_CHECK(c->a.y == (EDITOR_LOCAL ? 1 : 0) || c->a.y == (EDITOR_LOCAL ? 3 : 2));
            circles++;
        }
        TIDE_CHECK(circles == 4); // Shared and editor-only views, including nested child.
        TIDE_CHECK(observed == 0);
    }
    api->destroy(context);
    // Unloading and constructing a new authored world is repeatable.
    context = api->create(NULL, 0); TIDE_REQUIRE(context); api->destroy(context);
}
