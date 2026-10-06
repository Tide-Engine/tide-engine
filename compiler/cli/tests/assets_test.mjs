// Assets as tidec packs them (docs/spec.md, Assets): the pack a game gets, its
// C form for hosts that embed it, and URLs fetched once and kept in the game's
// .tide/ with the server's ETag, against a server on this machine: a first
// build fetches, the next asks whether the file changed and is told it hasn't,
// and with the server gone the build goes on from the cache, while a URL
// nothing has cached is an error.
//
//   node compiler/cli/tests/assets_test.mjs --tidec <tidec> --work <folder>

import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";

const opts = { tidec: null, work: null };
for (let i = 2; i < process.argv.length; i++) {
    if (process.argv[i] === "--tidec") opts.tidec = resolve(process.argv[++i]);
    else if (process.argv[i] === "--work") opts.work = resolve(process.argv[++i]);
    else throw new Error(`unknown option ${process.argv[i]}`);
}
if (!opts.tidec || !opts.work) throw new Error("pass --tidec <tidec> --work <folder>");

function check(condition, message) {
    if (!condition) throw new Error(message);
}

// The game: a PNG beside it, text, and a URL
rmSync(opts.work, { recursive: true, force: true });
mkdirSync(join(opts.work, "game", "art"), { recursive: true });
const png = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAEElEQVR42mP4z8DwHwohFABFzAf5jspQtgAAAABJRU5ErkJggg==", "base64");
writeFileSync(join(opts.work, "game", "art", "dot.png"), png);
writeFileSync(join(opts.work, "game", "rules.txt"), "Be kind.\n");

// A server that counts, says an ETag, and answers 304 to a matching If-None-Match
let requests = [];
let body = "Credits: everyone.\n";
const server = createServer((request, response) => {
    requests.push(request.headers["if-none-match"] || "");
    if (request.url !== "/credits.txt") {
        response.writeHead(404);
        response.end();
        return;
    }
    const etag = `"v${body.length}"`;
    if (request.headers["if-none-match"] === etag) {
        response.writeHead(304, { ETag: etag });
        response.end();
        return;
    }
    response.writeHead(200, { ETag: etag, "Content-Type": "text/plain" });
    response.end(body);
});
await new Promise((accept) => server.listen(0, "127.0.0.1", accept));
const port = server.address().port;

function game(credits) {
    writeFileSync(join(opts.work, "game", "game.tide"), `asset Grid2<Color> dot = "art/dot.png";
asset string rules = "rules.txt";
asset string credits = "${credits}";

scene Main
{
}

singleton Seen
{
    int width;
    int chars;
}

system Note(mut Seen seen)
{
    seen.width = dot.size.x;
    seen.chars = rules.length + credits.length;
}
`);
}

// tidec on the game: asynchronous, so the server above answers meanwhile.
async function build(extra = []) {
    const out = join(opts.work, "out");
    mkdirSync(out, { recursive: true });
    return await new Promise((accept, reject) => {
        const child = spawn(opts.tidec, [join(opts.work, "game", "game.tide"), "-o", out, "--name", "game", "--game", join(opts.work, "game"), ...extra],
                            { windowsHide: true });
        let said = "";
        child.stdout.on("data", (data) => { said += data; });
        child.stderr.on("data", (data) => { said += data; });
        child.on("error", reject);
        child.on("close", (code) => accept({ code, said, out }));
    });
}

try {
    // A first build fetches the URL, packs everything, and the pack is one
    game(`http://127.0.0.1:${port}/credits.txt`);
    let r = await build(["--pack-c"]);
    check(r.code === 0, `the first build failed:\n${r.said}`);
    check(requests.length === 1 && requests[0] === "", "the first build should fetch the URL once, with no ETag");
    const pack = readFileSync(join(r.out, "game.pack"));
    check(pack.subarray(0, 8).toString() === "TIDEPACK", "the pack starts with its magic");
    check(pack.includes(Buffer.from("Credits: everyone.")) && pack.includes(Buffer.from("Be kind.")), "the pack holds the text");
    const c = readFileSync(join(r.out, "game_pack.c"), "utf8");
    check(c.includes("const unsigned char tide_game_pack[]") && c.includes(`tide_game_pack_size = ${pack.length};`), "the pack as C");
    const cache = join(opts.work, "game", ".tide", "assets", "urls");
    const cached = readdirSync(cache);
    check(cached.some((name) => name.endsWith(".etag")) && cached.length >= 2, `the URL is cached with its ETag: ${cached}`);

    // The next build asks whether it changed, and is told it hasn't
    r = await build();
    check(r.code === 0, `the second build failed:\n${r.said}`);
    check(requests.length === 2 && requests[1] === `"v${body.length}"`, `the second build asks with the ETag: ${JSON.stringify(requests)}`);

    // A changed file comes with a new ETag, and the pack changes
    body = "Credits: everyone, and you.\n";
    r = await build();
    check(r.code === 0, `the third build failed:\n${r.said}`);
    check(requests.length === 3, "the third build asks again");
    check(readFileSync(join(r.out, "game.pack")).includes(Buffer.from("and you")), "the pack has the new text");

    // The server gone: the build goes on from the cache
    await new Promise((accept) => server.close(accept));
    r = await build();
    check(r.code === 0, `the offline build failed:\n${r.said}`);
    check(readFileSync(join(r.out, "game.pack")).includes(Buffer.from("and you")), "the offline build packs what was cached");

    // ...but a URL nothing has cached is an error that says so
    game(`http://127.0.0.1:${port}/never.txt`);
    r = await build();
    check(r.code !== 0 && r.said.includes("can't be fetched"), `an uncached URL with no server is an error:\n${r.said}`);

    // A game with no assets gets no pack file, and as C an empty pack: its header alone
    writeFileSync(join(opts.work, "game", "game.tide"), "scene Main\n{\n}\n");
    rmSync(join(r.out, "game.pack"), { force: true });
    r = await build(["--pack-c"]);
    check(r.code === 0, `the plain build failed:\n${r.said}`);
    check(!existsSync(join(r.out, "game.pack")), "a game with no assets has no pack");
    check(readFileSync(join(r.out, "game_pack.c"), "utf8").includes("tide_game_pack_size = 32;"), "...and an empty pack as C: its header alone");
    console.log("ok");
} finally {
    server.close();
}
