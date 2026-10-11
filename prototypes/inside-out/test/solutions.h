#pragma once

// Hand-built witnesses, checked against the production command handlers.
// Uppercase NESW belongs to seat 0, lowercase nesw to seat 1. Whitespace is
// ignored and separates the cooperative handoffs. '+' pushes the next room
// shell instead of entering it. These are valid solutions,
// not claims of shortest paths. The public game does not load this file.
static const char *const inside_out_solutions[12] = {
    "+E nnnw WSS",
    "NN nnnw WWS +E+E seenws SSWW",
    "wwwww NENN+W ESSWNN EEESWWNE+ES ssssss",
    "nnwnn +E seeeeee wwwwwwn SE+N seeeessw SEEN+W+W enneen SWWNNE+E NE+S WSSWW ennwwsssse",
    "NNNESSSNNNWWSSES nnn SEEEN+WN ws",
    "wwwwnww NNNNENWWW EESSS WN+E NNWS ESSW ssee",
    "nnwnn +E eeseees +E wnwwnwwseeeesenwwws SENNE",
    "nnne +E sssesssnnnwws EES",
    "ssw NNENWWW EESSS WN+E NNWS eeseeee ESSE+NNNW wwwws",
    "nnwnn +E ssennn sw NESWSS+W EESSWN ss",
    "NNNESSS eee wssesw SSWSSESSSWSE",
    "nnnwn +E SENWWN+N EEESWSESSSWS ssess"
};
