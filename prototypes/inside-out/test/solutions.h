#pragma once

// Hand-built witnesses, checked against the production command handlers.
// Uppercase NESW belongs to seat 0, lowercase nesw to seat 1. Whitespace is
// ignored and separates the cooperative handoffs. These are valid solutions,
// not claims of shortest paths. The public game does not load this file.
static const char *const inside_out_solutions[12] = {
    "E nnnw WSS",
    "NN nnnw WWS EE seenws SSWW",
    "wwwww NENNW ESSWNN SWWNES ssssss",
    "nnwnn E seeeeee wwwwwwn SEN seeeessw SEENWW enneen SWWNNEE NES WSSWW ennwwsssse",
    "NNNESSSNNNWWSSES nnn SEEENWN ws",
    "wwwwnw NNNNEN w WWW EESSS WNE NNWS ESSW ssee",
    "nnwnn E eeseees E wnwwnwwseeeesenwwws SENNE",
    "nnne E sssesssnnnwws EES",
    "ss NNEN w WWW EESSS WNE NNWS eeseeee ESSENNNW wwwws",
    "nnwnn E ssennn sw NESWSSW EESSWN ss",
    "NNNESSS eee wssesw SSWSSESSSWSE",
    "nnnwn E SENWWNN EEESWSESSSWS ssess"
};
