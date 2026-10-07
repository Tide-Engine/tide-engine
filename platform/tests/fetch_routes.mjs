// The server the fetch tests fetch from (compiler/tests/e2e/fetch.tide, run
// natively by fetch.mjs and in a page by cmake/run_web_served_test.mjs with
// --routes): a path for each case. Anything else is the server's own 404.
//
// A route answers a request and says so; false for a path that isn't one.

const hello = 'hello, tide';

export default function routes(request, response) {
    const path = request.url.split('?')[0];
    switch (path) {
    case '/hello':
        response.writeHead(200, { 'content-type': 'text/plain', 'content-length': hello.length });
        response.end(hello);
        return true;
    case '/error':
        response.writeHead(500, { 'content-length': 4 });
        response.end('oops');
        return true;
    case '/moved': // Three hops, a relative path, a path from the root, and a whole URL
        response.writeHead(302, { location: 'moved2' });
        response.end();
        return true;
    case '/moved2':
        response.writeHead(301, { location: '/moved3' });
        response.end();
        return true;
    case '/moved3':
        response.writeHead(307, { location: `http://${request.headers.host}/hello` });
        response.end();
        return true;
    case '/loop':
        response.writeHead(302, { location: '/loop' });
        response.end();
        return true;
    case '/chunked': { // No length, so it goes in chunks: 100 of 1000 bytes
        response.writeHead(200, { 'content-type': 'application/octet-stream' });
        const chunk = Buffer.alloc(1000);
        for (let i = 0; i < chunk.length; i++) chunk[i] = i % 251;
        for (let i = 0; i < 100; i++) response.write(chunk);
        response.end();
        return true;
    }
    case '/big': // Says more than the limit, and then waits: the client leaves
        response.writeHead(200, { 'content-length': 70 * 1024 * 1024 });
        response.write('x');
        return true;
    case '/endless': { // Goes on past the limit, in chunks, until the client leaves
        response.writeHead(200, { 'content-type': 'application/octet-stream' });
        const block = Buffer.alloc(1 << 20, 7);
        let sent = 0;
        const more = () => {
            while (sent < 80 && !response.destroyed) {
                sent++;
                if (!response.write(block)) {
                    response.once('drain', more);
                    return;
                }
            }
            if (!response.destroyed) response.end();
        };
        more();
        return true;
    }
    case '/empty':
        response.writeHead(204);
        response.end();
        return true;
    case '/nothing':
        response.writeHead(200, { 'content-length': 0 });
        response.end();
        return true;
    default:
        return false;
    }
}
