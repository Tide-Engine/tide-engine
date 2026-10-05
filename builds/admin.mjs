// The build service's admin command (see builds.mjs), with the admin secret in
// the environment as TIDE_BUILDS_ADMIN:
//
//   node admin.mjs game <id>              makes a game, and prints its key: this once
//   node admin.mjs key <id>               another key for a game; the old one stops working
//   node admin.mjs revoke <id> <build>    revokes a build: its link says it's gone
//
// The service is TIDE_BUILDS_URL's, or https://builds.tide-engine.dev. A
// game's key is what `tide publish` takes, in TIDE_GAME_KEY, and what asks for
// builds.

const [command, id, build] = process.argv.slice(2);
const service = (process.env.TIDE_BUILDS_URL || 'https://builds.tide-engine.dev').replace(/\/+$/, '');
const admin = process.env.TIDE_BUILDS_ADMIN;
const calls = {
    game: () => ['POST', '/games', { id }],
    key: () => ['POST', `/games/${id}/key`],
    revoke: () => ['DELETE', `/games/${id}/builds/${build}`],
};
if (!calls[command] || !id || (command === 'revoke' && !build)) {
    console.error('usage: node admin.mjs game <id> | key <id> | revoke <id> <build>');
    process.exit(2);
}
if (!admin) {
    console.error('admin: the admin secret goes in the environment, as TIDE_BUILDS_ADMIN');
    process.exit(2);
}
const [method, path, body] = calls[command]();
const response = await fetch(service + path, { method, headers: { authorization: `Bearer ${admin}` }, body: body && JSON.stringify(body) });
const answer = await response.json().catch(() => ({}));
if (!response.ok) {
    console.error(`admin: ${service} refused: ${answer.error || response.status}`);
    process.exitCode = 1;
} else if (command === 'revoke') {
    console.log(`Revoked ${answer.build}`);
} else {
    console.log(answer.key);
}
