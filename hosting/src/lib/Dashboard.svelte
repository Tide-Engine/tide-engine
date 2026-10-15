<script>
    import { onMount } from 'svelte';
    let { data } = $props();
    let account = $derived(data.account);
    let message = $derived(data.message);
    let busy = $state(false), logs = $state(null), approved = $state(false), dark = $state(false);
    let deployToken = $state(null);
    const money = value => new Intl.NumberFormat('en', { style: 'currency', currency: account.currency }).format(value / 100_000_000);
    const time = stamp => new Intl.DateTimeFormat('en-GB', { dateStyle: 'medium', timeStyle: 'short', timeZone: 'UTC' }).format(stamp) + ' UTC';
    const running = $derived(account?.matches.filter(m => ['starting', 'running', 'recovering', 'suspending'].includes(m.state)).length || 0);
    function savedTheme() { try { return localStorage.getItem('theme'); } catch { return null; } }
    function setTheme(value) { dark = value; document.documentElement.classList.toggle('dark', value); }
    function toggleTheme() { setTheme(!dark); try { localStorage.setItem('theme', dark ? 'dark' : 'light'); } catch {} }
    async function api(path, value, method = value === undefined ? 'GET' : 'POST') {
        const response = await fetch(path, { method, credentials: 'same-origin',
            headers: value === undefined ? {} : { 'Content-Type': 'application/json', 'X-Tide-CSRF': account?.csrf || '' },
            body: value === undefined ? undefined : JSON.stringify(value) });
        const result = await response.json();
        if (!response.ok) { const error = new Error(result.error || 'This request failed.'); error.status = response.status; throw error; }
        return result;
    }
    async function refresh() {
        try { account = await api('/api/account'); }
        catch (error) { if (error.status === 401) { location.assign('/'); return; } throw error; }
    }
    async function run(fn) {
        busy = true; message = '';
        try { await fn(); await refresh(); } catch (error) { message = error.message; }
        finally { busy = false; }
    }
    async function createProject(event) {
        event.preventDefault(); const form = event.currentTarget, values = new FormData(form);
        await run(async () => {
            await api('/api/projects', { name: values.get('name'), region: account.region, preset: values.get('preset'),
                maximum: Number(values.get('maximum')), lifetime: values.get('lifetime'), emptySeconds: Number(values.get('emptySeconds')) });
            form.reset(); message = 'Project created. Run tide deploy from your game folder.';
        });
    }
    async function checkout(event) {
        event.preventDefault(); const amount = Number(new FormData(event.currentTarget).get('amount'));
        await run(async () => location.assign((await api('/api/checkout', { amount })).url));
    }
    async function billingLink(path) { location.assign((await api(`/api/billing/${path}`, {})).url); }
    async function extraUsage(event) {
        event.preventDefault(); const values = new FormData(event.currentTarget);
        await run(async () => {
            await api('/api/billing/extra', { enabled: values.has('enabled'), cap: Math.round(Number(values.get('cap')) * 100), amount: Number(values.get('amount')) });
            message = 'Extra-usage settings saved.';
        });
    }
    async function cancelRenewal() {
        const cancel = !account.billing.cancelAtEnd;
        if (cancel && !confirm('Cancel renewal? Your paid hosting plan stays available until the end of this billing period.')) return;
        await api('/api/billing/cancel', { cancel });
    }
    async function approve(event) {
        event.preventDefault(); const code = String(new FormData(event.currentTarget).get('code')).toUpperCase();
        await run(async () => { await api('/api/device/approve', { code }); approved = true; message = 'Your CLI is authorized. You can return to your terminal.'; });
    }
    async function credential(project) {
        const key = await api(`/api/projects/${project.id}/credentials`, {});
        logs = `Runtime credential (shown once, expires ${time(key.expires)}):\n${key.token}`;
        message = 'Store this credential on your trusted backend. It can control this project’s matches and cannot deploy builds.';
    }
    async function createDeployToken(event, project) {
        event.preventDefault(); const form = event.currentTarget, values = new FormData(form);
        await run(async () => {
            deployToken = await api(`/api/projects/${project.id}/deploy-tokens`, { name: values.get('name'), days: values.get('days') === 'never' ? null : Number(values.get('days')) });
            form.reset(); message = 'Deploy token created. Copy it now; it will only be shown once.';
        });
    }
    async function revokeDeployToken(project, key) {
        await api(`/api/projects/${project.id}/deploy-tokens/${key.hash}`, {}, 'DELETE');
        if (deployToken?.project === project.id) deployToken = null;
    }
    async function startMatch(project) {
        const match = await api(`/api/projects/${project.id}/matches`, {});
        if (match.endpoint) message = `Match ${match.id} is ready: ${match.endpoint}`;
    }
    async function viewLogs(match) {
        const entries = await api(`/api/matches/${match.id}/logs`);
        logs = entries.length ? entries.map(l => `${time(l.at)}  ${l.text}`).join('\n') : 'No runtime output for this match.';
    }
    async function confirmed(prompt, path, method = 'POST') { if (confirm(prompt)) await api(path, {}, method); }
    const entryLabel = reference => reference.startsWith('payment/') ? 'Credit purchase'
        : reference.startsWith('allowance/') ? 'Monthly hosting allowance' : reference.startsWith('expiry/') ? 'Unused monthly allowance expired'
        : reference.startsWith('reversal/') ? 'Payment reversal' : reference.startsWith('settle/') ? 'Unused credit returned' : 'Hosting allocation';
    onMount(() => {
        setTheme(document.documentElement.classList.contains('dark'));
        const system = matchMedia('(prefers-color-scheme: dark)');
        const follow = () => { if (savedTheme() === null) setTheme(system.matches); };
        system.addEventListener('change', follow);
        const url = new URL(location.href);
        if (url.searchParams.has('signin')) { url.searchParams.delete('signin'); history.replaceState(null, '', url.pathname + url.search + url.hash); }
        const timer = setInterval(() => { if (!document.hidden && account) refresh().catch(error => message = error.message); }, 10000);
        return () => { clearInterval(timer); system.removeEventListener('change', follow); };
    });
</script>

<svelte:head><title>Console | Tide Engine</title></svelte:head>
{#snippet action(label, fn, danger = false)}
    <button type="button" class:danger disabled={busy} onclick={() => run(fn)}>{label}</button>
{/snippet}

<header><div class="header-bar">
    <a class="brand" href="https://tide-engine.dev/"><img src="/favicon.svg" alt="" width="28" height="28"><span>Tide Engine</span></a>
    <span class="console-label">Console</span><nav aria-label="Main"><a href="https://tide-engine.dev/guide">Docs</a></nav>
    <div id="identity">{#if account}<span>{account.login}</span>{@render action('Sign out', async () => { await api('/api/logout', {}); location.assign('/'); })}{/if}</div>
    <button id="theme-toggle" class="theme-toggle" type="button" onclick={toggleTheme} aria-label={dark ? 'Switch to light theme' : 'Switch to dark theme'} title="Switch theme"><svg viewBox="0 0 24 24" aria-hidden="true"><path class="moon" d="M20.5 14.1A8.5 8.5 0 0 1 9.9 3.5a8.5 8.5 0 1 0 10.6 10.6Z"/><g class="sun"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></g></svg></button>
</div></header>
<main>
    <div id="notice" role="status" aria-live="polite">{message}</div>
    {#if !account}
        <section id="welcome" class="welcome">
            <p class="eyebrow">FROM YOUR MACHINE TO YOUR PLAYERS</p>
            <h1>A home for your<br>next multiplayer game.</h1>
            <p>Deploy your game, run matches, and keep track of your hosting in one place.</p>
            <a class="button primary" data-sveltekit-reload href={data.device && data.code ? `/auth/github?code=${data.code}` : '/auth/github'}>Continue with GitHub <span aria-hidden="true">↗</span></a>
            <p class="muted small">Hosting is currently invite-only. Charges use prepaid credit.</p>
        </section>
    {:else}
        <div id="account">
            {#if data.device && !approved}<section id="device" class="panel">
                <p class="eyebrow">CONNECT YOUR TERMINAL</p><h2>Authorize Tide CLI</h2>
                <p>Only approve this request if you just ran <code>tide login</code> and the code matches your terminal.</p>
                <form id="device-form" onsubmit={approve}><label>Login code<input name="code" value={data.code} required pattern={'[A-Fa-f0-9]{10}'} maxlength="10" autocomplete="off"></label><button class="primary" disabled={busy}>Authorize this CLI</button></form>
            </section>{/if}
            <section class="intro"><div><p class="eyebrow">YOUR WORKSPACE</p><h1>Hosting</h1><p class="muted">Your games, their worlds, and what they cost.</p></div><button type="button" disabled={busy} onclick={() => location.reload()}>Refresh</button></section>
            <section class="summary" aria-label="Hosting overview">
                <article><span class="muted">Available hosting credit</span><strong id="balance">{money(account.balance)}</strong><span class="small muted">{account.billing.required ? 'Monthly allowance is used first.' : 'Usage comes out of this balance.'}</span><a class="credit-link" href={account.billing.required ? '#subscription' : '#add-credit'}>{account.billing.required ? 'Plan & extra credit ↗' : 'Add credit ↗'}</a></article>
                <article><span class="muted">Running matches</span><strong id="running">{running} / {account.maximum}</strong><span class="small muted" id="capacity">{account.accepting ? 'On demand, within your limit.' : 'New allocations are temporarily disabled.'}</span></article>
                <article><span class="muted">Hosting region</span><strong id="region">{account.region.toUpperCase()}</strong><span class="small muted">One region per deployment.</span></article>
            </section>
            <section class="workspace"><div>
                <section class="panel"><div class="section-title"><h2>Projects</h2><span class="muted small">Build locally. Share a link.</span></div><div id="projects">
                    {#each account.projects as project (project.id)}
                        {@const preset = account.presets[project.preset]}
                        <article class="project"><div class="project-head"><strong>{project.name}</strong><span class="badge">{project.release ? 'Deployed' : 'Awaiting deployment'}</span></div>
                            {#if project.release}
                                {#if project.hasClient}
                                    <div class="project-play">
                                        {#if project.clientCreate}<a class="button primary play-game" href={project.playUrl} target="_blank" rel="noopener">Play game ↗</a>{/if}
                                        <a class="game-url" href={project.playUrl} target="_blank" rel="noopener">{project.playUrl}</a>
                                    </div>
                                    {#if !project.clientCreate}<p>Client match creation is restricted. Share a running match's URL to join it.</p>{/if}
                                {:else}<p>Server-only release. Deploy without <code>--server-only</code> to publish a playable web client.</p>{/if}
                            {/if}
                            <p>{project.preset} · {project.maximum} maximum matches · {project.lifetime === 'explicit' ? 'Until ended' : 'Ends when empty'}</p>
                            <p>{preset.rate ? `${money(preset.rate * 3600)}/hour per allocated match` : 'Hosting prices are not configured.'}</p>
                            <code>tide deploy --project {project.name}</code>
                            {@render action(project.clientCreate ? 'Restrict creation to trusted API' : 'Allow clients to create matches', () => api(`/api/projects/${project.id}/policy`, { clientCreate: !project.clientCreate }))}
                            <div class="actions">{@render action('Create runtime credential', () => credential(project))}
                                {#each account.credentials.filter(k => k.project === project.id) as key (key.hash)}
                                    {@render action(`Revoke key ${key.hash.slice(0, 8)}`, () => api(`/api/projects/${project.id}/credentials/${key.hash}`, {}, 'DELETE'), true)}
                                {/each}
                            </div>
                            <details class="create"><summary>CI deploy tokens</summary>
                                <p class="small muted">Upload builds for this project from CI. Save the token as a secret named <code>TIDE_HOSTING_TOKEN</code> and run <code>tide deploy --project {project.id}</code>.</p>
                                <form aria-label="Create CI deploy token" onsubmit={event => createDeployToken(event, project)}>
                                    <label>Token name<input name="name" placeholder="GitHub Actions" required maxlength="64" autocomplete="off"></label>
                                    <label>Expires after<select name="days"><option value="30">30 days</option><option value="90" selected>90 days</option><option value="365">1 year</option><option value="never">Never expires</option></select></label>
                                    <button disabled={busy}>Create deploy token</button>
                                </form>
                                {#if deployToken?.project === project.id}
                                    <p class="small">Shown once. {deployToken.expires === null ? 'Never expires.' : `Expires ${time(deployToken.expires)}.`}</p>
                                    <label>Deploy token<textarea readonly value={deployToken.token} spellcheck="false" rows="2"></textarea></label>
                                    <button type="button" onclick={() => deployToken = null}>Hide token</button>
                                {/if}
                                {#each account.deployTokens.filter(k => k.project === project.id) as key (key.hash)}
                                    <p class="small">{key.name} · {key.expires === null ? 'Never expires' : `expires ${time(key.expires)}`} · {key.hash.slice(0, 8)}</p>
                                    {@render action(`Revoke deploy token ${key.name}`, () => revokeDeployToken(project, key), true)}
                                {/each}
                            </details>
                            {#if project.release}<p>Release {project.release.slice(0,10)}</p>
                            <div class="actions">
                                {@render action('Start a match', () => startMatch(project))}
                            </div>
                                {#each account.releases.filter(r => r.project === project.id && r.id !== project.release) as release (release.id)}
                                    {@render action(`Delete old release ${release.id.slice(0,10)}`, () => confirmed('Delete this old release? Running and saved worlds that need it prevent deletion.', `/api/projects/${project.id}/releases/${release.id}`, 'DELETE'), true)}
                                {/each}
                            {/if}
                        </article>
                    {:else}<div class="deploy-start"><p>Deploy directly from your game folder.</p><pre><code>tide login
tide deploy</code></pre><p class="small muted">Your first deploy creates a project using the folder name and saves its link for future deploys. No dashboard setup needed.</p><a class="credit-link" href="https://tide-engine.dev/multiplayer/hosting">Deployment guide ↗</a></div>{/each}
                </div>
                    <details class="create"><summary>Create a project with custom settings</summary><form id="project-form" onsubmit={createProject}>
                        <label>Project name<input name="name" placeholder="my-game" required pattern={'[a-z][a-z0-9-]{0,47}'} maxlength="48"></label>
                        <div class="fields"><label>Match size<select name="preset" id="presets">{#each Object.entries(account.presets) as [id,preset]}<option value={id}>{preset.label} · {preset.memoryBytes / 1048576} MiB · {preset.rate ? `${money(preset.rate * 3600)}/hour` : 'Not configured'}</option>{/each}</select></label><label>Maximum matches<input type="number" name="maximum" value="1" min="1" id="maximum" max={account.maximum} required></label></div>
                        <label>World lifetime<select name="lifetime"><option value="occupied">End after players leave</option><option value="explicit">Run until explicitly ended</option></select></label>
                        <label>Empty grace (seconds)<input name="emptySeconds" type="number" min="5" max="3600" value="60" required></label>
                        <p class="small muted">Long-lived worlds keep running while empty and use credit until suspended or ended.</p><button class="primary" disabled={busy}>Create project</button>
                    </form></details>
                </section>
                <section class="panel"><h2>Matches</h2><div id="matches">
                    {#each account.matches.slice(0,30) as match (match.id)}
                        {@const project = account.projects.find(p => p.id === match.project)}
                        <article class="match"><div class="match-head"><strong>{project?.name || 'Game'} · {match.id}</strong><span class="badge {match.state}">{match.state}</span></div>
                            <p>{match.players} players · Release {match.release.slice(0,10)}</p>
                            {#if match.memoryBytes}<p>WASM memory {(match.memoryBytes / 1048576).toFixed(1)} MiB · {match.cpuFraction === null ? 'Measuring CPU…' : `CPU ${(match.cpuFraction * 100).toFixed(1)}% of one core`}</p>{/if}
                            {#if match.checkpoint_time}<p>Last completed save: {time(match.checkpoint_time)}</p>{/if}
                            {#if project && match.hasClient && ['running','starting'].includes(match.state)}
                                <label>Match URL<input class="match-url" readonly value={`${project.playUrl}?match=${match.id}`} onclick={event => event.currentTarget.select()}></label>
                            {/if}
                            <div class="actions">
                                {#if project && match.hasClient && ['running','starting'].includes(match.state)}<a class="button" href={`${project.playUrl}?match=${match.id}`} target="_blank" rel="noopener">Join match ↗</a>{/if}
                                {#if ['running','starting','recovering'].includes(match.state)}
                                    {@render action('Suspend', () => api(`/api/matches/${match.id}/suspend`, {}))}
                                    {@render action('End match', () => confirmed('End this match? It will not automatically restart.', `/api/matches/${match.id}/end`), true)}
                                    {@render action('View logs', () => viewLogs(match))}
                                {:else if ['suspended','failed'].includes(match.state)}{@render action('Resume', () => api(`/api/matches/${match.id}/resume`, {}))}{/if}
                                {#if match.checkpoint_time}<a class="button" href={`/api/matches/${match.id}/save`} data-sveltekit-reload>Download save</a>
                                    {#if ['suspended','failed','ended'].includes(match.state)}{@render action('Delete saved world', () => confirmed('Permanently delete this world save? This cannot be undone.', `/api/matches/${match.id}/discard`), true)}{/if}
                                {/if}
                                {#if ['running','starting','recovering'].includes(match.state)}{@render action(match.lifetime === 'explicit' ? 'End when empty' : 'Keep until ended', () => api(`/api/matches/${match.id}/policy`, { lifetime: match.lifetime === 'explicit' ? 'occupied' : 'explicit', emptySeconds: match.empty_seconds }))}{/if}
                            </div>
                        </article>
                    {:else}<p class="empty">Matches will appear here when players join or you start one.</p>{/each}
                </div>{#if logs !== null}<pre id="logs" aria-label="Match logs">{logs}</pre>{/if}</section>
                <section class="panel"><h2>Activity</h2><div id="activity">{#each account.incidents.slice(0,15) as incident}<article class="event"><p>{incident.message}</p><time>{time(incident.created)}</time></article>{:else}<p class="empty">No hosting incidents or recovery events.</p>{/each}</div></section>
            </div><aside>
                {#if account.billing.required}<section class="panel" id="subscription">
                    <p class="eyebrow">YOUR HOSTING PLAN</p><h2>{money(account.billing.monthlyAmount * 1000000)} / month</h2>
                    {#if account.paymentTestMode}<p class="billing-status">Stripe sandbox · No real money is charged.</p>{/if}
                    <p class="small muted">Includes {money(account.billing.monthlyAmount * 1000000)} of hosting usage each billing cycle. Unused monthly allowance expires.</p>
                    {#if ['past_due','incomplete','unpaid','paused'].includes(account.billing.status)}<p class="billing-status">Your subscription payment needs attention. Open Manage billing to update your payment method or pay the invoice.</p>{/if}
                    <dl class="billing-details"><dt>Monthly allowance left</dt><dd id="included-balance">{money(account.billing.included)}</dd><dt>Purchased credit</dt><dd id="purchased-balance">{money(account.billing.purchased)}</dd></dl>
                    {#if account.billing.active}
                        <p class="small muted">{account.billing.cancelAtEnd || account.billing.status === 'canceled' ? 'Paid access ends' : 'Current allowance expires'} {time(account.billing.periodEnd)}. Purchased credit carries forward.</p>
                    {:else}<p class="billing-status">Subscribe to run hosted matches. Deploying your game does not start a subscription.</p>{/if}
                    <div class="actions">
                        {#if ['none','canceled','incomplete_expired'].includes(account.billing.status)}<button class="primary" id="subscribe" disabled={busy || !account.billing.available} onclick={() => run(() => billingLink('subscribe'))}>{account.paymentTestMode ? 'Subscribe in sandbox ↗' : 'Subscribe ↗'}</button>{/if}
                        {#if account.billing.hasCustomer && account.billing.portal}{@render action('Manage billing ↗', () => billingLink('portal'))}{/if}
                        {#if account.billing.active && account.billing.status !== 'canceled'}{@render action(account.billing.cancelAtEnd ? 'Keep subscription' : 'Cancel renewal', cancelRenewal)}{/if}
                    </div>
                    {#if !account.billing.available}<p class="small muted">Subscriptions are not configured yet.</p>{/if}
                </section>{/if}
                <section class="panel" id="add-credit"><p class="eyebrow">KEEP THINGS RUNNING</p><h2>Add hosting credit</h2>
                    {#if account.billing.required}
                        <p class="small muted">Extra usage is optional. Enable it to use purchased credit and authorize automatic prepaid top-ups when credit runs low.</p>
                        <form id="extra-form" onsubmit={extraUsage}>
                            <label class="checkbox"><input type="checkbox" name="enabled" checked={account.billing.extraEnabled}> Enable extra usage &amp; automatic payments</label>
                            <label>Automatic top-up amount<select name="amount" value={account.billing.topup}>{#each account.topups as amount}<option value={amount}>{money(amount * 1000000)}</option>{/each}</select></label>
                            <label>Maximum extra purchases per billing cycle ({account.currency.toUpperCase()})<input name="cap" type="number" min="0" max="1000" step="1" value={account.billing.spendCap ? account.billing.spendCap / 100 : 20} required></label>
                            <p class="small muted">With extra usage enabled, you authorize charges to your saved payment method up to this cap. Credit is added only after payment succeeds. Failed payments pause automatic purchases.</p>
                            <button id="save-extra" disabled={busy || !account.billing.active}>Save extra-usage settings</button>
                        </form>
                        <p class="small muted">Extra purchases this cycle: {money(account.billing.spent * 1000000)} / {money(account.billing.spendCap * 1000000)}. Pending purchases count toward the cap.</p>
                        {#if account.billing.issue}<p class="billing-status" role="status">{account.billing.issue}</p>{/if}
                    {/if}
                    {#if account.paymentsAvailable}
                        {#if account.paymentTestMode}<p class="billing-status">Stripe sandbox · No real money is charged.</p>{/if}
                        <form id="credit-form" onsubmit={checkout}><label>Credit amount<select name="amount" id="topups">{#each account.topups as amount}<option value={amount}>{money(amount * 1000000)}</option>{/each}</select></label><button class="primary" id="checkout" disabled={busy || (account.billing.required && (!account.billing.active || !account.billing.extraEnabled))}>{account.paymentTestMode ? 'Continue to test payment ↗' : 'Continue to payment ↗'}</button></form><p class="small muted">{account.billing.required && !account.billing.extraEnabled ? 'Enable extra usage above to purchase additional credit.' : 'Checkout is handled by Stripe. Credit is added after payment is confirmed.'}</p>
                    {:else}<p id="credit-unavailable" class="billing-status">Credit purchases are not available yet.</p><p class="small muted">You’ll be able to choose an amount here when payments are enabled.</p>{/if}
                </section>
                <section class="panel policy"><h2>You control the limit.</h2><p>Matches grow on demand up to your capacity limit.</p><p>When credit runs low, new matches stop. When it runs out, long-lived worlds save and suspend.</p><p id="retention">Suspended saves are retained for {account.retentionDays} days. Download or resume your world before retention ends.</p></section>
                <section class="panel"><h2>Recent usage &amp; credit</h2><div id="ledger">{#each account.ledger.slice(0,10) as entry}<div class="entry"><div><span>{entryLabel(entry.reference)}</span><time>{time(entry.created)}</time></div><strong>{entry.amount > 0 ? '+' : ''}{money(entry.amount)}</strong></div>{:else}<p class="empty">No charges yet.</p>{/each}</div></section>
            </aside></section>
        </div>
    {/if}
</main>
<footer><span>Tide hosting · Early access</span><a href="https://tide-engine.dev/tools/cli">CLI documentation ↗</a></footer>
