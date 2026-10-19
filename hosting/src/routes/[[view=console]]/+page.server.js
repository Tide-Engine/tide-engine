const messages = {
    restricted: 'This GitHub account does not have hosting access yet. Hosting is invite-only; ask the operator to invite your account, then sign in again.',
    expired: 'Your sign-in expired or could not be verified. Start a fresh GitHub sign-in below.',
    unavailable: 'GitHub sign-in is temporarily unavailable. Please try again shortly.',
};

export function load({ platform, url, setHeaders }) {
    setHeaders({ 'cache-control': 'private, no-store', vary: 'Cookie' });
    const code = url.searchParams.get('code') || '';
    const reason = url.searchParams.get('signin');
    // This per-request property is supplied by the hosting HTTP server after
    // authenticating the session. It is not a header a client can supply.
    return { account: platform.req.tideHostingAccount || null,
        device: url.pathname === '/device', code: /^[A-F0-9]{10}$/.test(code) ? code : '',
        message: Object.hasOwn(messages, reason) ? messages[reason] : '' };
}
