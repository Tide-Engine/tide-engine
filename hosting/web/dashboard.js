const $ = selector => document.querySelector(selector);
let account;
function element(tag, text, className) {
    const node = document.createElement(tag);
    if (text !== undefined) node.textContent = text;
    if (className) node.className = className;
    return node;
}
const money = micros => new Intl.NumberFormat(undefined, { style: 'currency', currency: account.currency }).format(micros / 100_000_000);
const time = stamp => new Date(stamp).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });
function notice(message = '') { $('#notice').textContent = message; }
async function api(path, value, method = value === undefined ? 'GET' : 'POST') {
    const response = await fetch(path, { method, credentials: 'same-origin',
        headers: value === undefined ? {} : { 'Content-Type': 'application/json', 'X-Tide-CSRF': account?.csrf || '' },
        body: value === undefined ? undefined : JSON.stringify(value) });
    const data = await response.json();
    if (!response.ok) { const error = new Error(data.error || 'This request failed.'); error.status = response.status; throw error; }
    return data;
}
function action(label, fn, danger = false) {
    const button = element('button', label, danger ? 'danger' : '');
    button.type = 'button';
    button.onclick = async () => {
        button.disabled = true;
        try { notice(); await fn(); await refresh(); }
        catch (error) { notice(error.message); }
        finally { button.disabled = false; }
    };
    return button;
}
function empty(container, message) { container.append(element('p', message, 'empty')); }
function renderProjects() {
    const root = $('#projects'); root.replaceChildren();
    for (const project of account.projects) {
        const card = element('article', undefined, 'project');
        const head = element('div', undefined, 'project-head');
        head.append(element('strong', project.name), element('span', project.release ? 'Deployed' : 'Awaiting deployment', 'badge'));
        card.append(head, element('p', `${project.preset} · ${project.maximum} maximum matches · ${project.lifetime === 'explicit' ? 'Until ended' : 'Ends when empty'}`));
        card.append(element('code', `tide deploy --project ${project.name}`));
        card.append(action(project.clientCreate ? 'Restrict creation to trusted API' : 'Allow clients to create matches',
            () => api(`/api/projects/${project.id}/policy`, { clientCreate: !project.clientCreate })));
        const credentials = element('div', undefined, 'actions');
        credentials.append(action('Create runtime credential', async () => {
            const key = await api(`/api/projects/${project.id}/credentials`, {});
            $('#logs').hidden = false;
            $('#logs').textContent = `Runtime credential (shown once, expires ${time(key.expires)}):\n${key.token}`;
            notice('Store this credential on your trusted backend. It can control this project’s matches and cannot deploy builds.');
        }));
        for (const key of account.credentials.filter(k => k.project === project.id)) credentials.append(action(`Revoke key ${key.hash.slice(0, 8)}`,
            () => api(`/api/projects/${project.id}/credentials/${key.hash}`, {}, 'DELETE'), true));
        card.append(credentials);
        if (project.release) {
            card.append(element('p', `Release ${project.release.slice(0, 10)}`));
            const actions = element('div', undefined, 'actions');
            const play = element('a', 'Open game ↗', 'button'); play.href = project.playUrl; play.target = '_blank'; play.rel = 'noopener';
            if (project.hasClient && project.clientCreate) actions.append(play);
            actions.append(action('Start a match', async () => {
                const match = await api(`/api/projects/${project.id}/matches`, {});
                if (match.endpoint) notice(`Match ${match.id} is ready: ${match.endpoint}`);
            }));
            card.append(actions);
            for (const release of account.releases.filter(r => r.project === project.id && r.id !== project.release)) {
                card.append(action(`Delete old release ${release.id.slice(0, 10)}`, async () => {
                    if (confirm('Delete this old release? Running and saved worlds that need it prevent deletion.'))
                        await api(`/api/projects/${project.id}/releases/${release.id}`, {}, 'DELETE');
                }, true));
            }
        }
        root.append(card);
    }
    if (!account.projects.length) empty(root, 'Create a project, then deploy from your game folder.');
}
function renderMatches() {
    const root = $('#matches'); root.replaceChildren();
    for (const match of account.matches.slice(0, 30)) {
        const card = element('article', undefined, 'match'), head = element('div', undefined, 'match-head');
        const project = account.projects.find(p => p.id === match.project);
        head.append(element('strong', `${project?.name || 'Game'} · ${match.id}`), element('span', match.state, `badge ${match.state}`));
        card.append(head, element('p', `${match.players} players · Release ${match.release.slice(0, 10)}`));
        if (match.memoryBytes) card.append(element('p', `WASM memory ${(match.memoryBytes / 1048576).toFixed(1)} MiB · ${match.cpuFraction === null ? 'Measuring CPU…' : `CPU ${(match.cpuFraction * 100).toFixed(1)}% of one core`}`));
        if (match.checkpoint_time) card.append(element('p', `Last completed save: ${time(match.checkpoint_time)}`));
        const actions = element('div', undefined, 'actions');
        if (['running', 'starting', 'recovering'].includes(match.state)) {
            actions.append(action('Suspend', () => api(`/api/matches/${match.id}/suspend`, {})));
            actions.append(action('End match', async () => {
                if (confirm('End this match? It will not automatically restart.')) await api(`/api/matches/${match.id}/end`, {});
            }, true));
            actions.append(action('View logs', async () => {
                const logs = await api(`/api/matches/${match.id}/logs`);
                $('#logs').hidden = false;
                $('#logs').textContent = logs.length ? logs.map(l => `${time(l.at)}  ${l.text}`).join('\n') : 'No runtime output for this match.';
            }));
        } else if (['suspended', 'failed'].includes(match.state)) actions.append(action('Resume', () => api(`/api/matches/${match.id}/resume`, {})));
        if (match.checkpoint_time) {
            const save = element('a', 'Download save', 'button'); save.href = `/api/matches/${match.id}/save`; actions.append(save);
            if (['suspended', 'failed', 'ended'].includes(match.state)) actions.append(action('Delete saved world', async () => {
                if (confirm('Permanently delete this world save? This cannot be undone.')) await api(`/api/matches/${match.id}/discard`, {});
            }, true));
        }
        if (['running', 'starting', 'recovering'].includes(match.state)) actions.append(action(
            match.lifetime === 'explicit' ? 'End when empty' : 'Keep until ended',
            () => api(`/api/matches/${match.id}/policy`, { lifetime: match.lifetime === 'explicit' ? 'occupied' : 'explicit', emptySeconds: match.empty_seconds })));
        if (project && match.hasClient && ['running', 'starting'].includes(match.state)) {
            const join = element('a', 'Join match ↗', 'button');
            join.href = `${project.playUrl}?match=${match.id}`; join.target = '_blank'; join.rel = 'noopener'; actions.prepend(join);
        }
        card.append(actions); root.append(card);
    }
    if (!account.matches.length) empty(root, 'Matches will appear here when players join or you start one.');
}
function renderActivity() {
    const root = $('#activity'); root.replaceChildren();
    for (const incident of account.incidents.slice(0, 15)) {
        const row = element('article', undefined, 'event');
        row.append(element('p', incident.message), element('time', time(incident.created))); root.append(row);
    }
    if (!account.incidents.length) empty(root, 'No hosting incidents or recovery events.');
    const ledger = $('#ledger'); ledger.replaceChildren();
    for (const entry of account.ledger.slice(0, 10)) {
        const row = element('div', undefined, 'entry'), description = element('div');
        const label = entry.reference.startsWith('payment/') ? 'Credit purchase'
            : entry.reference.startsWith('reversal/') ? 'Payment reversal'
            : entry.reference.startsWith('settle/') ? 'Unused credit returned' : 'Hosting allocation';
        description.append(element('span', label), element('time', time(entry.created)));
        row.append(description, element('strong', `${entry.amount > 0 ? '+' : ''}${money(entry.amount)}`)); ledger.append(row);
    }
    if (!account.ledger.length) empty(ledger, 'No charges yet.');
}
let first = true;
if (location.pathname === '/device') {
    const code = new URLSearchParams(location.search).get('code');
    const signIn = document.querySelector('a[href="/auth/github"]');
    if (signIn && /^[A-F0-9]{10}$/.test(code || '')) signIn.href = `/auth/github?code=${code}`;
}
async function refresh() {
    try { account = await api('/api/account'); }
    catch (error) {
        if (error.status === 401) { $('#welcome').hidden = false; $('#account').hidden = true; return; }
        throw error;
    }
    $('#welcome').hidden = true; $('#account').hidden = false;
    $('#identity').replaceChildren(element('span', account.login), action('Sign out', async () => {
        await api('/api/logout', {}); location.assign('/dashboard');
    }));
    $('#balance').textContent = money(account.balance);
    const running = account.matches.filter(m => ['starting', 'running', 'recovering', 'suspending'].includes(m.state)).length;
    $('#running').textContent = `${running} / ${account.maximum}`;
    $('#capacity').textContent = account.accepting ? 'On demand, within your limit.' : 'New allocations are temporarily disabled.';
    $('#region').textContent = account.region.toUpperCase();
    $('#retention').textContent = `Suspended saves are retained for ${account.retentionDays} days. Download or resume your world before retention ends.`;
    if (first) {
        $('#presets').replaceChildren(...Object.entries(account.presets).map(([id, preset]) => {
            const option = element('option', `${preset.label} · ${preset.memoryBytes / 1048576} MiB · ${preset.rate ? `${money(preset.rate * 3600)}/hour` : 'Not configured'}`);
            option.value = id; return option;
        }));
        $('#maximum').max = account.maximum;
        $('#topups').replaceChildren(...account.topups.map(amount => {
            const option = element('option', money(amount * 1000000)); option.value = amount; return option;
        }));
        $('#checkout').disabled = account.topups.length === 0;
        if (location.pathname === '/device') {
            $('#device').hidden = false;
            $('#device-form').elements.code.value = new URLSearchParams(location.search).get('code') || '';
        }
        first = false;
    }
    renderProjects(); renderMatches(); renderActivity();
}
$('#refresh').onclick = () => refresh().catch(error => notice(error.message));
$('#project-form').onsubmit = async event => {
    event.preventDefault(); const form = event.currentTarget, values = new FormData(form);
    try {
        await api('/api/projects', { name: values.get('name'), region: account.region, preset: values.get('preset'),
            maximum: Number(values.get('maximum')), lifetime: values.get('lifetime'), emptySeconds: Number(values.get('emptySeconds')) });
        form.reset(); await refresh(); notice('Project created. Run tide deploy from your game folder.');
    } catch (error) { notice(error.message); }
};
$('#credit-form').onsubmit = async event => {
    event.preventDefault(); $('#checkout').disabled = true;
    try { location.assign((await api('/api/checkout', { amount: Number(new FormData(event.currentTarget).get('amount')) })).url); }
    catch (error) { notice(error.message); $('#checkout').disabled = false; }
};
$('#device-form').onsubmit = async event => {
    event.preventDefault();
    try {
        await api('/api/device/approve', { code: String(new FormData(event.currentTarget).get('code')).toUpperCase() });
        $('#device').hidden = true; notice('Your CLI is authorized. You can return to your terminal.');
    } catch (error) { notice(error.message); }
};
await refresh().catch(error => notice(error.message));
setInterval(() => { if (!document.hidden && account) refresh().catch(error => notice(error.message)); }, 10000);
