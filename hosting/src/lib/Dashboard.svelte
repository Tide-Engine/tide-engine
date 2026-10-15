<script>
    import { onMount } from 'svelte';
    let { data } = $props();
    let account = $derived(data.account);
    let message = $derived(data.message);
    let busy = $state(false), logs = $state(null), approved = $state(false), dark = $state(false);
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
    async function approve(event) {
        event.preventDefault(); const code = String(new FormData(event.currentTarget).get('code')).toUpperCase();
        await run(async () => { await api('/api/device/approve', { code }); approved = true; message = 'Your CLI is authorized. You can return to your terminal.'; });
    }
    async function credential(project) {
        const key = await api(`/api/projects/${project.id}/credentials`, {});
        logs = `Runtime credential (shown once, expires ${time(key.expires)}):\n${key.token}`;
        message = 'Store this credential on your trusted backend. It can control this project’s matches and cannot deploy builds.';
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
            <section class="intro"><div><p class="eyebrow">YOUR WORKSPACE</p><h1>Hosting</h1><p class="muted">Your games, their worlds, and what they cost.</p></div>{@render action('Refresh', refresh)}</section>
            <section class="summary" aria-label="Hosting overview">
                <article><span class="muted">Prepaid credit</span><strong id="balance">{money(account.balance)}</strong><span class="small muted">Usage comes out of this balance.</span></article>
                <article><span class="muted">Running matches</span><strong id="running">{running} / {account.maximum}</strong><span class="small muted" id="capacity">{account.accepting ? 'On demand, within your limit.' : 'New allocations are temporarily disabled.'}</span></article>
                <article><span class="muted">Hosting region</span><strong id="region">{account.region.toUpperCase()}</strong><span class="small muted">One region per deployment.</span></article>
            </section>
            <section class="workspace"><div>
                <section class="panel"><div class="section-title"><h2>Projects</h2><span class="muted small">Build locally. Share a link.</span></div><div id="projects">
                    {#each account.projects as project (project.id)}
                        {@const preset = account.presets[project.preset]}
                        <article class="project"><div class="project-head"><strong>{project.name}</strong><span class="badge">{project.release ? 'Deployed' : 'Awaiting deployment'}</span></div>
                            <p>{project.preset} · {project.maximum} maximum matches · {project.lifetime === 'explicit' ? 'Until ended' : 'Ends when empty'}</p>
                            <p>{preset.rate ? `${money(preset.rate * 3600)}/hour per allocated match` : 'Hosting prices are not configured.'}</p>
                            <code>tide deploy --project {project.name}</code>
                            {@render action(project.clientCreate ? 'Restrict creation to trusted API' : 'Allow clients to create matches', () => api(`/api/projects/${project.id}/policy`, { clientCreate: !project.clientCreate }))}
                            <div class="actions">{@render action('Create runtime credential', () => credential(project))}
                                {#each account.credentials.filter(k => k.project === project.id) as key (key.hash)}
                                    {@render action(`Revoke key ${key.hash.slice(0, 8)}`, () => api(`/api/projects/${project.id}/credentials/${key.hash}`, {}, 'DELETE'), true)}
                                {/each}
                            </div>
                            {#if project.release}<p>Release {project.release.slice(0,10)}</p><div class="actions">
                                {#if project.hasClient && project.clientCreate}<a class="button" href={project.playUrl} target="_blank" rel="noopener">Open game ↗</a>{/if}
                                {@render action('Start a match', () => startMatch(project))}
                            </div>
                                {#each account.releases.filter(r => r.project === project.id && r.id !== project.release) as release (release.id)}
                                    {@render action(`Delete old release ${release.id.slice(0,10)}`, () => confirmed('Delete this old release? Running and saved worlds that need it prevent deletion.', `/api/projects/${project.id}/releases/${release.id}`, 'DELETE'), true)}
                                {/each}
                            {/if}
                        </article>
                    {:else}<p class="empty">Create a project, then deploy from your game folder.</p>{/each}
                </div>
                    <details class="create"><summary>Create a project</summary><form id="project-form" onsubmit={createProject}>
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
                <section class="panel"><p class="eyebrow">KEEP THINGS RUNNING</p><h2>Add hosting credit</h2><form id="credit-form" onsubmit={checkout}><label>Credit amount<select name="amount" id="topups">{#each account.topups as amount}<option value={amount}>{money(amount * 1000000)}</option>{/each}</select></label><button class="primary" id="checkout" disabled={busy || !account.topups.length}>Continue to payment ↗</button></form><p class="small muted">Checkout is handled by Stripe. Credit is added after payment is confirmed.</p></section>
                <section class="panel policy"><h2>You control the limit.</h2><p>Matches grow on demand up to your capacity limit.</p><p>When credit runs low, new matches stop. When it runs out, long-lived worlds save and suspend.</p><p id="retention">Suspended saves are retained for {account.retentionDays} days. Download or resume your world before retention ends.</p></section>
                <section class="panel"><h2>Recent usage &amp; credit</h2><div id="ledger">{#each account.ledger.slice(0,10) as entry}<div class="entry"><div><span>{entryLabel(entry.reference)}</span><time>{time(entry.created)}</time></div><strong>{entry.amount > 0 ? '+' : ''}{money(entry.amount)}</strong></div>{:else}<p class="empty">No charges yet.</p>{/each}</div></section>
            </aside></section>
        </div>
    {/if}
</main>
<footer><span>Tide hosting · Early access</span><a href="https://tide-engine.dev/tools/cli">CLI documentation ↗</a></footer>
