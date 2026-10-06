import assert from "node:assert/strict";
import test from "node:test";
import { verifyObjects } from "./compile.mjs";

const objects = (edited) => new Map(["game.o", "c/a.c.o", "c/b.c.o"].map((path) =>
    [path, { stamp: "rewritten", digest: path === edited ? "new calculation" : "old calculation" }]));

test("compile timings reject a stale edited object even when it was rewritten", () => {
    for (const scenario of ["tide-body", "c-body", "header"]) {
        assert.throws(() => verifyObjects(scenario, objects(), objects()), /edit did not change/);
    }
});

test("compile timings require the dependent object, not an unrelated rewrite", () => {
    assert.throws(() => verifyObjects("header", objects(), objects("c/b.c.o")), /c\/a.c.o/);
    assert.deepEqual(verifyObjects("header", objects(), objects("c/a.c.o")), ["c/a.c.o"]);
    assert.deepEqual(verifyObjects("c-body", objects(), objects("c/a.c.o")), ["c/a.c.o"]);
    assert.deepEqual(verifyObjects("tide-body", objects(), objects("game.o")), ["game.o"]);
});

test("a cold build must have objects and an unchanged build need not rewrite them", () => {
    assert.throws(() => verifyObjects("cold", new Map(), new Map()), /no cached objects/);
    assert.deepEqual(verifyObjects("unchanged", objects(), objects()), []);
});
