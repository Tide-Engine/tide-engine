# Introduction

Tide is a game engine built for multiplayer first. Its simulation is deterministic: given the same inputs, it gives the same results on every machine, down to the last bit of every float. So players only send each other their inputs, and players on different machines, the web included, share one world.

Games are written in **Tide**, a language of its own, built around an ECS. It compiles to C, and the C to native code or WebAssembly, together with the engine, so calls between your code and the engine's cost nothing.

::: warning Early days
Tide is young, and its language changes freely until 1.0. The [language spec](../spec.md) says which parts are decided and which are still provisional.
:::

## The big ideas

**A game is data and systems.** Entities are made of components, which are plain data. Systems are code that runs every tick, once for each entity that has the components it asks for. Singletons hold what there's one of per world, like the score or the rules.

```csharp
component Health
{
    int value = 100;
}

system Poison(mut Health health, with Poisoned)
{
    health.value -= 1;
}
```

**Systems say what they touch.** `mut` means a system writes a component, and a plain parameter that it only reads it. `with` and `without` pick entities without reading anything. From that, the compiler plans which systems can run at the same time and which have to wait, and shows you why (see [the schedule](../game/schedule.md)).

**Everything is a match.** The simulation, called the match, is what every machine runs the same way. It runs on a server, and even single-player is a match on a server this machine runs itself. The engine has rollback netcode built in: clients predict ahead with no input delay, and correct themselves when the server disagrees. See [Matches](../multiplayer/matches.md).

**Local state is everything else.** Menus, settings, particles and the camera belong to one machine and are never sent. **Views** read the match and draw it once per frame, and they can change local state but never the match. The compiler enforces that boundary, so a view can't break the simulation by accident.

**Input is the only way in.** What a player does reaches the match as their input, sampled once per tick and sent over the network. Everything else follows from the simulation.

## A tick and a frame

The match advances in fixed **ticks**, 60 per second by default:

1. Systems run, in a fixed order that's the same on every machine.
2. Structural changes (spawning, adding and removing components, destroying) and events apply at the end of the tick, in the order they were made.

The screen updates every **frame**, as fast as the display allows:

1. Views run, drawing the match blended between its last two ticks, so motion is smooth at any tick rate.
2. The GUI draws over the world.

## Where to go next

- [Install tide](./install.md), then make [your first game](./first-game.md).
- Try the [live examples](./examples.md): small games that run in these pages, beside their code.
- Read what a game is made of, a topic at a time, starting with [components and entities](../game/entities.md).
- See how machines share a match, and how the engine keeps them in step: [matches](../multiplayer/matches.md), [networking](../multiplayer/networking.md) and [determinism](../multiplayer/determinism.md).
- Look things up in [the language](../language/basics.md) and the [tide command](../tools/cli.md).
- [Play falling sand](./sand.md) in your browser, alone or with friends.
- [Play Duel](./duel.md), a small 2D arena game against a practice bot or a friend.
- [Play Towers](./towers.md), a physics game for up to four, against bots or with friends in a room: a race to stack bricks into the tallest tower, and keep it standing.
- [Play Sumo](./sumo.md), heavy balls in a ring stacked with two thousand crates, for up to eight at once in a room.
