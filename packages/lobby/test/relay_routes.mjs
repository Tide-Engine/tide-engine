// The relay's lobbies (relay/lobbies.mjs), as routes of the server that
// serves the lobby package's test page (cmake/run_web_served_test.mjs with
// --routes): the page's players ask the server they came from, on the
// machine's clock, as a game's page asks a relay.

import { createLobbies } from '../../../relay/lobbies.mjs';

const clock = {
    now: () => performance.now(),
    wait(ms, call) {
        const timer = setTimeout(call, ms);
        timer.unref(); // A held answer never keeps the test's server from ending
        return () => clearTimeout(timer);
    },
};
// The test's four players ask from one address, more at once than a player would
const lobbies = createLobbies({ clock, limits: { requestsPerSecond: 1000, requestBurst: 10000 } });

export default function routes(request, response) {
    return lobbies.handle(request, response, request.socket.remoteAddress || '?');
}
