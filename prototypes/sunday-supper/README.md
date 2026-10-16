# SUNDAY SUPPER

A first-person horror story for **one or two**. Your car dies on a back road
in Kansas, October 1998, and the only light for miles is the Hale farm. Walt
and Ruth take you in for the weekend. Their son Dale doesn't say much.

This is the first playable slice: Friday evening on the road, supper with the
Hales, the night in their guest room, and Saturday morning in the barn. It ends
there for now.

Play at <https://tide-engine.dev/prototypes/sunday-supper/>. **Play** is alone;
**Host a room** and send the code to play it with a friend, one traveller each.

## Playing

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Walk | WASD | Left stick |
| Look | Mouse | Right stick |
| Run | Shift | Left stick press |
| Use what you look at | E or left click | South button |
| Phone light on and off | F | North button |
| Look at the phone | Q (held) | West button |
| Answer when a choice is offered | 1, 2, 3 | D-pad left, up, right |
| The room's menu | Escape, or **Menu** | |

Either traveller can answer a choice; whoever presses first speaks. The story
waits for both of you where it needs you together (inside the house, at the
table, in the guest room, in the barn). Two of you are needed to lift the feed
sacks and to pull the tarp; alone, you can do both by yourself.

Development keys, not in play's interface: F2 the porch, F3 supper, F4 the
night, F5 the morning, F6 the barn.

## Build and run

From the repository root:

```sh
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target sunday_supper sunday_supper_test
ctest --preset debug -R '^sunday_supper'
```

For the browser page, and the browser test on both backends:

```sh
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target sunday_supper_page sunday_supper_test
ctest --preset web-debug -R '^sunday_supper'
```

Serve `docs/static` and visit `/prototypes/sunday-supper/`. Or run
`tide run` in this folder.

## Files

| File | What's in it |
| --- | --- |
| `game.tide` | The travellers, the input, joining and leaving, starting over |
| `farm.tide` | The farm as one plan: walls, floors, stairs, doors, furniture, spots |
| `walk.tide` | Walking, collision, what a traveller looks at and uses, doors, sacks, the tarp |
| `story.tide`, `script.tide`, `direct.tide` | The story as a script of steps, the slice's script, and what runs it |
| `cast.tide` | Walt, Ruth and Dale: walking, posing, the watcher in the corn |
| `scenery.tide`, `placing.tide`, `kit.tide` | The farm's meshes, and where the kit models stand |
| `render.tide`, `draw.tide` | The picture: light, materials, the phone light's shadows, the people |
| `sound.tide`, `ui.tide` | What's heard, and what's written over the picture |
| `art/` | The people (Quaternius) and the props (Kenney): see `art/CREDITS.md` |
| `sfx/` | Sounds made by `sfx/make_sfx.py` from noise and tones |
| `test/` | The story played through with no window, and the browser test |
