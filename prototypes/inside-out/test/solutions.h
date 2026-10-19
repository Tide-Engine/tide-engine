#pragma once
// Authored production-command witnesses. Uppercase NESW is seat0, lowercase
// nesw seat1; whitespace marks handoffs. No shortest-path claim is implied.
static const char *const inside_out_solutions[20] = {
    "EEEN eeeees", "NWSSEN eeee WS", "NNWWWWSSEN eeeeeeww WS",
    "SWWESEEE swwwwsw",
    "EENN eeeessw",
    "eeeeessw SSSEEEEWWWW",
    "EEEWWWWW n EEEESEE",
    "WWENNN ee EESSWNSWWN e ennwseswwwee WNNESWSEEENNNWWN ess",
    "w EENNEEESSWWEESESWWWNWNWWW",
    "S n EEENE seee NSSWS s",
    "N ee NNNES w WE w",
    "EN nne EESE nws",
    "EE neee EEEEE n E",
    "wnnnss N s EES eeennwwseneswwse NWES e NWES neeswwne S e",
    "E ssssn SSSSSS e SNN wnnn",
    "SN nwwsessennn SN nsssss",
    "E s EEEESENNNN n NW s EEEWWW snn",
    "WSE nn EE nwss EWWWSES nwssw SSENNNNE nnnn",
    "EEN wwnnwne SESEE eesn E w N w W s WW sesense",
    "E s EESEENEN es N n N w SSSWWSWNW nesw EWWSSWWNN n"
};
// The challenges' witnesses: inside_out_solve's shortest routes, each exactly
// its puzzle's budget of moves.
static const char *const inside_out_challenge_solutions[3] = {
    "ENNWSN wwwwwnneee", "NWWSWS eesenen E w NNW nne", "SS wsnnenwwwsssse NN enne",
};

// A shortest solution of each campaign puzzle, as `inside_out_solve <n>`
// found it breadth first through the Move handler: each is its puzzle's par
// (LevelPar in levels.tide).
static const char *const inside_out_par_solutions[20] = {
    "EEEN eeeees",
    "NWSSENSW eeee",
    "NNWWWWSSENSW eeeeeeww",
    "SWWESEEE swwwwsw",
    "EENN eeeessw",
    "SSSE eeeeessw EEEWWWW",
    "EEEWWWWW n EEEESEE",
    "NWNN ee SW eswneenw EEWW w NNW weeess",
    "w EENNEEESSWWEESESWWWNWNWWW",
    "S n EEENE seee NSSWS s",
    "N ee NNNES w WE w",
    "EN nne EESE nws",
    "EE neee EEEEE n E",
    "WWNE w ENEESS nnn NWEN nwssenwne WE n WESS seswne S e",
    "E ssssn SSSSSS e SNN wnnn",
    "SN nwwsessennn SN nsssss",
    "E s EEEESENNNN n NW s EEEWWW snn",
    "WSE nn EE nwss EWWWSES nwssw SSENNNNE nnnn",
    "EEN wwnnwne SESEE eesn E w N w W s WW sesense",
    "E sese EWWW eennnsssswwwwnennwsn",
};
