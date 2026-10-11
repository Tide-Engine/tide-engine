#pragma once

// Hand-built witnesses, checked against the production command handlers.
// Uppercase NESW belongs to seat 0, lowercase nesw to seat 1. Whitespace is
// ignored and separates the cooperative handoffs. These are valid solutions,
// not claims of shortest paths. The public game does not load this file.
static const char *const inside_out_solutions[12] = {
    "E ese WSSS",
    "E eeeeeees WSS",
    "E eeeees WSS",
    "e EE ws SWWS",
    "eee NWS wsw ESSWWWW",
    "ENNWSWSE eeeesee WSS",
    "NNEES eeeeeeeeees WWSSS",
    "NNEES eeeeene sssss ess WWSSS",
    "e ESEN eeee EENNWS neses ESSSWWWW",
    "E eeeeeese SENSEENWW SWWNNEE NES WWWSS",
    "eeeee SEEN eeeee wwww EENNWWS wwwwwws WWSSS",
    "eeeee SEEN eeeee sen ee wnnws wswwww SWWNNENEEE SS NNWWS wwwwwws WWSSS"
};
