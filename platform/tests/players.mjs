// The standard host's runs of several players with no window (tide/run.h's
// --players): each player is a process of its own, which the first starts,
// and they go a frame at a time together on a made-up network. Every
// player's pictures are read back (png_read.mjs) and checked, pixel by pixel.
//
//   node players.mjs <tide_platform_players> <tide_platform_players_menu> <tide_platform_players_leave> <scratch folder> [refused]
//
// players.tide's Main is the match's, so the first player opens it and the
// others join; players_menu.tide starts in a menu, and the script has its
// players open and join by the rooms' codes; players_leave.tide's script has
// its players leave, drop and rejoin. Without `refused`, the games
// run, which needs a desktop session with OpenGL, though nothing shows on it.
// With it, only what a run refuses before it opens anything, which needs no GPU.

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { readPng } from './png_read.mjs';

const [programArg, menuArg, leaveArg, scratchArg, mode] = process.argv.slice(2);
const program = resolve(programArg), menuProgram = resolve(menuArg), leaveProgram = resolve(leaveArg); // They run in the scratch folder
const scratch = resolve(scratchArg);
rmSync(scratch, { recursive: true, force: true });
mkdirSync(scratch, { recursive: true });

let failures = 0;
function check(ok, what) {
    console.log(`${ok ? 'ok' : 'FAIL'}: ${what}`);
    if (!ok) failures++;
}

// A run nobody watches, whoever runs the tests: a program that stops shows no dialog
function run(args, game = program) {
    const ran = spawnSync(game, args, { cwd: scratch, encoding: 'utf8', timeout: 100000, env: { ...process.env, TIDE_HIDDEN: '1' } });
    return { status: ran.status, out: ran.stdout || '', err: ran.stderr || '', error: ran.error };
}

function script(name, lines) {
    const path = join(scratch, name);
    writeFileSync(path, lines.join('\n') + '\n');
    return path;
}

if (mode === 'refused') {
    // A run that's refused says why, with exit code 2, and starts nothing
    const refused = (what, args, ...says) => {
        const ran = run(args);
        const said = says.every(text => ran.err.includes(text));
        const how = ran.status === 2 && said ? '' : `: exit ${ran.status}, "${ran.err.trim()}" ${ran.error ? ran.error.message : ''}`;
        check(ran.status === 2 && said, `${what} is refused${how}`);
    };
    refused('--players with no number', ['--frames', '5', '--players'], '--players takes how many players');
    refused('a run of no players', ['--frames', '5', '--players', '0'], '--players takes how many players', "not '0'");
    refused('a run of more players than a room has a code for', ['--frames', '5', '--players', '33'], 'from 1 to 32', "not '33'");
    refused('--players that is no number', ['--frames', '5', '--players', 'two'], "not 'two'");
    refused('--players in a run with a window', ['--players', '2'], '--players is for a run with no window', '--frames');
    refused("more players than the game's maxPlayers", ['--frames', '5', '--players', '5'], 'its maxPlayers is 4');
    refused('--players with --host', ['--frames', '5', '--players', '2', '--host'], '--players takes no --host, --join or --connect');
    refused('--players with --join', ['--frames', '5', '--players', '2', '--join', 'AAAAAA'], '--players takes no --host, --join or --connect');
    refused('a player with no first player to find', ['--frames', '5', '--players', '2', '--player', '2', '1'],
            "player 2 can't find the run's first player", '--players');

    const line = (text, players, ...says) =>
        refused(`the line "${text}" in a run of ${players || 'one'}`,
                ['--script', script('bad.txt', ['# A script', '3 key d', text, '9 quit']), ...(players ? ['--players', String(players)] : [])],
                'bad.txt:3:', ...says);
    line('5 player 3 key d', 2, "there's no player 3 in this run of 2 players", '--players 2');
    line('5 player 2 key d', 0, "there's no player 2 in a run of one", '--players 2 makes it a run of 2');
    line('5 player 2 key d', 1, "there's no player 2 in a run of one");
    line('5 player key d', 2, '`player` takes which player the line is for');
    line('5 player 0 key d', 2, '`player` takes which player the line is for');
    line('5 player 2', 2, 'nothing happens');
    line('5 player 2 quit', 2, '`quit` ends the run', 'it takes no player');
    line('5 player 2 kye d', 2, "'kye' isn't something a line does");
    line('5 leave', 2, '`leave` says whose match it is', '`5 player 2 leave`');
    line('5 drop', 0, '`drop` is for a run of several players', '--players 2');
    line('5 player 2 rejoin now', 2, "'now' is one word too many");
    check(!existsSync(join(scratch, 'x-1.png')), 'and nothing ran');
    process.exit(failures ? 1 : 0);
}

const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
const blue = [0, 0, 255], red = [255, 0, 0], green = [0, 255, 0], white = [255, 255, 255];
const magenta = [255, 0, 255], cyan = [0, 255, 255], yellow = [255, 255, 0], black = [0, 0, 0];
const bytes = name => readFileSync(join(scratch, name));
const read = name => readPng(bytes(name));

// A block is `color` at its middle and one pixel in from its corners, and not just outside it
function block(png, x, y, half, color) {
    const inside = [[x, y], [x - half + 1, y - half + 1], [x + half - 2, y + half - 2]];
    const outside = [[x - half - 2, y], [x + half + 1, y]];
    return inside.every(([px, py]) => same(png.at(px, py), color)) && outside.every(([px, py]) => !same(png.at(px, py), color));
}
const none = (png, x, y) => same(png.at(x, y), blue);
const rows = [30, 70, 110], colors = [red, green, yellow]; // A player's row and color, by the order it arrived in

// ---------------------------------------------------------------------------
// Two players, in a game whose Main is the match's: nothing in the script puts
// them in one match. Each holds its own D, at frames of the one script.
const play = script('play.txt', [
    '# Both are in the match by now, with nothing pressed',
    '40 capture joined.png',
    '',
    '# The first holds D, and then the second holds its own',
    '50 key d down',
    '60 player 2 key d down',
    '75 capture partway.png',
    '150 key d up',
    '160 player 2 key d up',
    '200 capture moved.png',
    '205 player 2 capture hers.png',
    '210 quit',
]);
const args = ['--players', '2', '--script', play, '--capture', join(scratch, 'last.png'), '--size', '320', '200'];
const ran = run(args);
check(ran.status === 0, `a run of two players ends with exit code 0${ran.status === 0 ? '' : `: ${ran.status} ${ran.error || ''}\n${ran.err}`}`);
if (ran.status !== 0) process.exit(1);
const names = ['joined', 'partway', 'moved', 'last'].flatMap(name => [`${name}-1.png`, `${name}-2.png`]).concat('hers-2.png');
const missing = names.filter(name => !existsSync(join(scratch, name)));
check(missing.length === 0, `each player's pictures have its number before the extension (${missing.join(', ') || 'all there'})`);
check(names.every(name => ran.out.includes(name)), 'and the run says it captured each');
check(!existsSync(join(scratch, 'hers-1.png')) && !existsSync(join(scratch, 'joined.png')) && !existsSync(join(scratch, 'last.png')),
      "a line that names a player is that player's alone, and no picture is without a number");
if (missing.length) process.exit(1);

for (const player of [1, 2]) {
    const joined = read(`joined-${player}.png`), partway = read(`partway-${player}.png`), moved = read(`moved-${player}.png`);
    const who = `player ${player}'s machine`;
    check(joined.width === 320 && joined.height === 200, `${who} draws at the size asked for (${joined.width} by ${joined.height})`);
    check(block(joined, 40, rows[0], 10, red) && block(joined, 40, rows[1], 10, green) && none(joined, 40, rows[2]),
          `${who} has both players in its match, each at its start`);
    check(block(joined, 10, rows[player - 1], 5, white) && none(joined, 10, rows[2 - player]), `and knows which of them is its own`);
    check(player === 1 ? block(joined, 300, 20, 10, white) : none(joined, 300, 20),
          player === 1 ? 'the first runs the server' : "the second doesn't run the server");
    check(block(joined, 300, 60, 10, cyan), `${who}'s match is in the first player's room, AAAAAA`);
    check(none(partway, 31, rows[0]) && none(partway, 31, rows[1]), `${who} sees both blocks under way, each by its own player's key`);
    check(block(moved, 200, rows[0], 10, red) && block(moved, 200, rows[1], 10, green),
          `${who} sees both where their 40 steps end`);
    if (player === 2) check(block(moved, 300, 100, 10, magenta), "the second's ping is a network's: more than nothing, and under 100 ms");
}
check(block(read('hers-2.png'), 200, rows[1], 10, green) && block(read('hers-2.png'), 10, rows[1], 5, white), "the second's own picture is its machine's");

// The same run again is the same pictures, byte for byte, on every player's machine
const first = names.map(bytes);
const again = run(args);
check(again.status === 0 && names.every((name, i) => bytes(name).equals(first[i])),
      'a run of two repeats exactly: the same pictures from both machines, byte for byte');

// Three: the others join in their order, so each one's row is its number's
const three = run(['--players', '3', '--frames', '90', '--capture', 'three.png', '--size', '320', '200']);
check(three.status === 0, `a run of three ends with exit code 0${three.status === 0 ? '' : `: ${three.status}\n${three.err}`}`);
if (three.status === 0) {
    for (const player of [1, 2, 3]) {
        const png = read(`three-${player}.png`);
        check(rows.every((y, i) => block(png, 40, y, 10, colors[i])), `player ${player}'s machine has all three in its match`);
        check(rows.every((y, i) => (i === player - 1 ? block(png, 10, y, 5, white) : none(png, 10, y))), `and its own is the ${['first', 'second', 'third'][player - 1]} to arrive`);
    }
}

// One: a run of one, whose network is made up too. Its picture has no number.
const one = run(['--players', '1', '--frames', '30', '--capture', 'one.png', '--size', '320', '200']);
check(one.status === 0 && existsSync(join(scratch, 'one.png')) && !existsSync(join(scratch, 'one-1.png')),
      `a run of one ends with exit code 0, and its picture has no number (${one.status})`);
if (one.status === 0 && existsSync(join(scratch, 'one.png'))) {
    const png = read('one.png');
    check(block(png, 40, rows[0], 10, red) && none(png, 40, rows[1]) && block(png, 300, 20, 10, white) && block(png, 300, 60, 10, cyan),
          'its match is open in its room, with nobody else in it');
}

// ---------------------------------------------------------------------------
// A player's game that stops ends the run, which says whose and when. The
// others don't wait for it, and none is left running.
const stopped = run(['--players', '2', '--script', script('stop.txt', ['30 player 2 key x', '2000 quit']), '--size', '320', '200']);
check(stopped.status !== 0 && stopped.status !== null && stopped.err.includes("player 2's game ended at frame 30")
      && stopped.err.includes('the run ends there'), `a player's game that stops ends the run, which says so (${stopped.status}: ${stopped.err.trim()})`);
check(stopped.err.includes('stopped, as the test asked'), "and what that game said is in the run's errors");

// A picture one player can't write fails the run, as it does a run of one
const lost = run(['--players', '2', '--script', script('lost.txt', ['5 player 2 capture no/such/folder.png', '2000 quit']), '--size', '320', '200']);
check(lost.status === 1 && lost.err.includes("can't write") && lost.err.includes("player 2's game ended at frame 5, with exit code 1"),
      `a picture one player can't write is exit code 1 (${lost.status}: ${lost.err.trim()})`);
const lostAll = run(['--players', '2', '--frames', '5', '--capture', join(scratch, 'no', 'such', 'folder.png'), '--size', '320', '200']);
check(lostAll.status === 1 && lostAll.err.includes("can't write"), `and so is one neither can (${lostAll.status})`);

// ---------------------------------------------------------------------------
// A game that starts in a menu: the script has the first player start a match
// and open it, and the second join by what it types.
const menu = script('menu.txt', [
    '# The first starts a match (S) and opens it (O)',
    '10 key s',
    '20 key o',
    '40 capture open.png',
    '',
    "# The second types a code that's no player's of the run",
    '50 player 2 text K7QF2M',
    '51 player 2 key enter',
    '60 capture nobodys.png',
    '65 player 2 key r',
    '',
    '# ...then its own room\'s, which it never opened',
    '70 player 2 text BBBBBB',
    '71 player 2 key enter',
    '80 capture unopened.png',
    '85 player 2 key r',
    '',
    "# ...then the first's, as a player would type it",
    '90 player 2 text aaa aaa',
    '91 player 2 key enter',
    '150 capture joined.png',
    '',
    "# It leaves (L), and comes back by an address, whose port is the match's (C)",
    '160 player 2 key l',
    '170 player 2 key c',
    '230 capture connected.png',
    '',
    '# ...and leaves for an address nobody is at (V), which never answers',
    '240 player 2 key l',
    '250 player 2 key v',
    '300 capture waiting.png',
    '600 capture nobody.png',
    '',
    "# It joins the first's room again, and then the first leaves its match",
    '610 player 2 key r',
    '620 player 2 text AAAAAA',
    '621 player 2 key enter',
    '700 capture back.png',
    '710 key l',
    '730 capture hostless.png',
    '740 quit',
]);
const menuArgs = ['--players', '2', '--script', menu, '--size', '320', '200'];
const menuRan = run(menuArgs, menuProgram);
check(menuRan.status === 0, `a run of two that starts in a menu ends with exit code 0${menuRan.status === 0 ? '' : `: ${menuRan.status}\n${menuRan.err}`}`);
if (menuRan.status === 0) {
    const shot = (name, player) => read(`${name}-${player}.png`);
    const connecting = png => block(png, 40, 40, 10, yellow), connected = png => block(png, 40, 40, 10, green);
    const offline = png => none(png, 40, 40);
    const server = png => block(png, 100, 40, 10, white);
    const inFirst = png => block(png, 160, 40, 10, cyan), inNone = png => none(png, 160, 40);
    const failed = png => block(png, 40, 100, 10, red), timedOut = png => block(png, 100, 100, 10, yellow);
    const serverLeft = png => block(png, 160, 100, 10, white);
    const blocks = png => [0, 1, 2, 3].filter(seat => block(png, 40 + seat * 40, 160, 10, black)).length;

    check(connected(shot('open', 1)) && server(shot('open', 1)) && inFirst(shot('open', 1)) && blocks(shot('open', 1)) === 1,
          "the first's match is open in its room, AAAAAA, with its own player in it");
    check(offline(shot('open', 2)) && inNone(shot('open', 2)) && blocks(shot('open', 2)) === 0, 'and the second is in no match: nothing joined it');
    check(failed(shot('nobodys', 2)) && offline(shot('nobodys', 2)), "a code that's no player's fails at once");
    check(failed(shot('unopened', 2)) && offline(shot('unopened', 2)) && !timedOut(shot('unopened', 2)),
          'and so does the room of a player who opened none: nobody waits for it');
    check(connected(shot('joined', 2)) && !server(shot('joined', 2)) && inFirst(shot('joined', 2)) && !failed(shot('joined', 2)),
          "the first's code, typed in lower case with a space, joins its match");
    check(blocks(shot('joined', 1)) === 2 && blocks(shot('joined', 2)) === 2, 'and both machines have both players in it');
    check(connected(shot('connected', 2)) && !server(shot('connected', 2)) && inNone(shot('connected', 2)),
          "an address reaches the match that's open on its port, in no room");
    check(blocks(shot('connected', 2)) >= 2, 'with its players');
    check(connecting(shot('waiting', 2)) && !timedOut(shot('waiting', 2)) && !failed(shot('waiting', 2)), 'an address nobody is at is waited for');
    check(offline(shot('nobody', 2)) && timedOut(shot('nobody', 2)), 'and times out, as one would on a network');
    check(connected(shot('nobody', 1)) && server(shot('nobody', 1)), "the first's match goes on meanwhile");
    check(connected(shot('back', 2)) && inFirst(shot('back', 2)) && !serverLeft(shot('back', 2)), "the second is back in the first's room");
    check(offline(shot('hostless', 2)) && serverLeft(shot('hostless', 2)) && !timedOut(shot('hostless', 2)) && !failed(shot('hostless', 2)),
          'a match whose host leaves ends for the other at once, as its server left: no relay is there to hand it over');
    check(offline(shot('hostless', 1)), 'and the first is in no match');

    const menuNames = ['open', 'nobodys', 'unopened', 'joined', 'connected', 'waiting', 'nobody', 'back', 'hostless']
        .flatMap(name => [`${name}-1.png`, `${name}-2.png`]);
    const menuFirst = menuNames.map(bytes);
    const menuAgain = run(menuArgs, menuProgram);
    check(menuAgain.status === 0 && menuNames.every((name, i) => bytes(name).equals(menuFirst[i])), 'and that run repeats exactly too');
}

// ---------------------------------------------------------------------------
// A script's leave, drop and rejoin, in a game whose Main is the match's. A
// PlayerID's row is white while it's in the match and black once it left; a
// fourth row would be a player who came back as someone new.
const leave = script('leave.txt', [
    '40 capture all.png',
    '',
    '# The second leaves, as Session.Leave does: the match hears it at once',
    '50 player 2 leave',
    '60 capture left.png',
    '',
    '# The third drops: silent both ways, which nobody hears until it times out',
    '70 player 3 drop',
    '100 capture dropping.png',
    '420 capture dropped.png',
    '',
    '# Both rejoin, as the same players',
    '430 player 2 rejoin',
    '431 player 3 rejoin',
    '520 capture back.png',
    '',
    "# A drop the match hasn't noticed yet: the rejoin takes over, with no leaving at all",
    '530 player 2 drop',
    '540 player 2 rejoin',
    '600 capture takeover.png',
    '',
    "# The host leaves: the others' match ends",
    '610 player 1 leave',
    '640 capture hostless.png',
    '650 quit',
]);
const leaveArgs = ['--players', '3', '--script', leave, '--size', '320', '200'];
const leaveRan = run(leaveArgs, leaveProgram);
check(leaveRan.status === 0, `a run whose players leave, drop and rejoin ends with exit code 0${leaveRan.status === 0 ? '' : `: ${leaveRan.status}\n${leaveRan.err}`}`);
if (leaveRan.status === 0) {
    const shot = (name, player) => read(`${name}-${player}.png`);
    const seat = (png, i) => block(png, 40, 30 + i * 40, 10, white) ? 'in' : block(png, 40, 30 + i * 40, 10, black) ? 'left' : 'none';
    const seats = png => [0, 1, 2, 3].map(i => seat(png, i)).join(' ');
    const connected = png => block(png, 300, 20, 10, green), timedOut = png => block(png, 300, 60, 10, yellow);
    const serverLeft = png => block(png, 300, 100, 10, white);

    for (const player of [1, 2, 3]) check(seats(shot('all', player)) === 'in in in none' && connected(shot('all', player)), `player ${player} starts in the match with the others`);
    check(seats(shot('left', 1)) === 'in left in none', `the match hears the second leave at once (${seats(shot('left', 1))})`);
    check(!connected(shot('left', 2)) && seats(shot('left', 2)) === 'none none none none' && !timedOut(shot('left', 2)),
          'and the second is in no match, having left rather than timed out');
    check(seats(shot('dropping', 1)) === 'in left in none' && connected(shot('dropping', 3)),
          "a drop isn't heard at first: the match and the dropped player each still think the other is there");
    check(seats(shot('dropped', 1)) === 'in left left none', `until the match times it out (${seats(shot('dropped', 1))})`);
    check(timedOut(shot('dropped', 3)) && !connected(shot('dropped', 3)), 'and the dropped player times out the match');
    check(seats(shot('back', 1)) === 'in in in none', `both rejoin as the same players: their rows, and no new one (${seats(shot('back', 1))})`);
    check(connected(shot('back', 2)) && connected(shot('back', 3)) && seats(shot('back', 3)) === 'in in in none', 'and they see the match again');
    check(seats(shot('takeover', 1)) === 'in in in none' && connected(shot('takeover', 2)),
          `a rejoin before the match noticed the drop takes over, with nobody leaving (${seats(shot('takeover', 1))})`);
    check(!connected(shot('hostless', 1)) && serverLeft(shot('hostless', 2)) && serverLeft(shot('hostless', 3)),
          "a host that leaves ends the others' match, as their server left");

    const leaveNames = ['all', 'left', 'dropping', 'dropped', 'back', 'takeover', 'hostless']
        .flatMap(name => [1, 2, 3].map(player => `${name}-${player}.png`));
    const leaveFirst = leaveNames.map(bytes);
    const leaveAgain = run(leaveArgs, leaveProgram);
    check(leaveAgain.status === 0 && leaveNames.every((name, i) => bytes(name).equals(leaveFirst[i])), 'and that run repeats exactly too');
}

process.exit(failures ? 1 : 0);
