import { mountRoom } from './room.js';
import * as Y from './vendor/yjs.js';

const app = document.querySelector('#app');
const modalRoot = document.querySelector('#modal-root');
const icons = {
  arrow: '<path d="m7 17 10-10M7 7h10v10"/>',
  arrowRight: '<path d="M4 9h10m-4-4 4 4-4 4"/>',
  chevron: '<path d="m7 4 5 5-5 5"/>',
  plus: '<path d="M9 3v12M3 9h12"/>',
  grid: '<rect x="2" y="2" width="5" height="5" rx="1"/><rect x="11" y="2" width="5" height="5" rx="1"/><rect x="2" y="11" width="5" height="5" rx="1"/><rect x="11" y="11" width="5" height="5" rx="1"/>',
  spark: '<path d="m9 2 1.8 5.2L16 9l-5.2 1.8L9 16l-1.8-5.2L2 9l5.2-1.8L9 2Z"/>',
  game: '<path d="M5 5h8c2 0 3 6 3 8 0 2-2 2-3 0l-1-1H6l-1 1c-1 2-3 2-3 0 0-2 1-8 3-8Z"/><path d="M6 7v4M4 9h4m4-1h.01M14 10h.01"/>',
  chat: '<path d="M15 11a2 2 0 0 1-2 2H7l-4 3V5a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v6Z"/><path d="M6 7h6M6 10h4"/>',
  pin: '<path d="M14 7c0 4-5 9-5 9S4 11 4 7a5 5 0 0 1 10 0Z"/><circle cx="9" cy="7" r="1.7"/>',
  image: '<rect x="2" y="2" width="14" height="14" rx="2"/><circle cx="6" cy="6" r="1.3"/><path d="m2 12 4-3 3 3 3-5 4 5"/>',
  history: '<path d="M3 5a7 7 0 1 1-1 7M2 2v4h4M9 5v4l3 2"/>',
  people: '<circle cx="7" cy="6" r="2.5"/><path d="M2 15v-1a5 5 0 0 1 10 0v1m0-12a2.5 2.5 0 0 1 0 5m2 3a4 4 0 0 1 2 4"/>',
  link: '<path d="m7 11 4-4m-5 5-1 1a3 3 0 0 1-4-4l4-4a3 3 0 0 1 4 0m0 0 1-1a3 3 0 0 1 4 4l-4 4a3 3 0 0 1-4 0" transform="translate(1 1)"/>',
  close: '<path d="m4 4 10 10M14 4 4 14"/>',
  menu: '<path d="M3 4h12M3 9h12M3 14h12"/>',
  logout: '<path d="M7 2H3v14h4m3-11 4 4-4 4M7 9h8"/>',
  upload: '<path d="M9 12V2M5 6l4-4 4 4M2 12v4h14v-4"/>',
  check: '<path d="m3 9 4 4 8-8"/>',
  clock: '<circle cx="9" cy="9" r="7"/><path d="M9 5v4l3 2"/>',
  monitor: '<rect x="2" y="2" width="14" height="10" rx="1"/><path d="M9 12v4M6 16h6"/>',
  fullscreen: '<path d="M6 2H2v4m10-4h4v4M2 12v4h4m10-4v4h-4"/>',
  refresh: '<path d="M15 7a6 6 0 0 0-10-4L2 6m0-4v4h4m-3 5a6 6 0 0 0 10 4l3-3m0 4v-4h-4"/>',
  file: '<path d="M10 2H3v14h12V7l-5-5Z"/><path d="M10 2v5h5M6 10h6M6 13h4"/>',
  send: '<path d="m3 10 6-6 6 6M9 4v11"/>',
  camera: '<path d="M6 4 7 2h4l1 2h3v11H3V4h3Z"/><circle cx="9" cy="9" r="3"/>',
  flag: '<path d="M4 16V2m0 1c4-3 6 3 10 0v7c-4 3-6-3-10 0"/>',
  heart: '<path d="M9 15 3 9a4 4 0 0 1 6-5 4 4 0 0 1 6 5l-6 6Z"/>',
  bolt: '<path d="m10 1-7 9h5l-1 7 8-10h-5l0-6Z"/>',
  gear: '<path d="m7 2 1-1h2l1 1 1 2h2l2 2-1 2 1 2-1 3h-2l-1 2-3 1-1-2-2-1-2 1-2-2 1-2-1-2 1-3h2l1-2Z"/><circle cx="9" cy="8" r="2.5"/>',
};
const icon = (name) => `<svg viewBox="0 0 18 18" aria-hidden="true">${icons[name] || icons.spark}</svg>`;
const escape = (value = '') => String(value).replace(/[&<>"']/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const initials = (name = '?') => name.trim().split(/\s+/).slice(0, 2).map((part) => part[0]).join('').toUpperCase();
const brand = () => '<a class="brand" href="#" data-action="dashboard"><span class="brand-mark" aria-hidden="true"></span><div>mockup<span>.games</span></div></a>';
const state = { user:null, configured:{}, projects:[], detail:null, panel:'chat', draft:'', polish:'polished', feedbackMode:false, pin:null, pinDraft:'', menu:false, roomStatus:'Connecting to the playtest…', assetUsage:'game', briefReady:false, briefError:'', view:'dashboard', admin:null, authProviders:[] };
const clientId = crypto.randomUUID();
let room = null;
let source = null;
let toastTimer;
let routeCounter = 0;
let authError = '';
let busy = false;
let modalReturnFocus = null;
let briefDoc = null;
let briefText = null;
let briefProjectId = null;
let briefWriteChain = Promise.resolve();
let pendingBriefUpdates = [];
let savedSelection = null;
let activityTimer;
let lastActivity = null;

async function api(path, options = {}) {
  const response = await fetch(path, { credentials:'same-origin', ...options, headers:{ ...(options.body ? {'Content-Type':'application/json'} : {}), ...options.headers }, body:options.body ? JSON.stringify(options.body) : undefined });
  let data;
  try { data = await response.json(); } catch { throw new Error(`The service returned an unexpected response (${response.status}). Please try again.`); }
  if (!response.ok) throw new Error(typeof data.error === 'string' ? data.error : data.error?.message || data.message || `The request failed (${response.status}).`);
  return data;
}
function toast(message) {
  const element = document.querySelector('#toast');
  element.textContent = message;
  element.classList.add('visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => element.classList.remove('visible'), 4500);
}
function relativeTime(value) {
  if (!value) return 'Just now';
  const minutes = Math.max(0, Math.round((Date.now() - new Date(value).getTime()) / 60000));
  if (minutes < 1) return 'Just now';
  if (minutes < 60) return `${minutes}m ago`;
  if (minutes < 1440) return `${Math.floor(minutes/60)}h ago`;
  return new Date(value).toLocaleDateString(undefined, {month:'short',day:'numeric'});
}
function fullTime(value) { return value ? new Date(value).toLocaleString() : ''; }
function avatar(name, title = name) { return `<span class="avatar" title="${escape(title)}">${escape(initials(name))}</span>`; }
function currentRevision() { return state.detail?.revisions.find((r) => r.id === (state.detail.room?.revisionId || state.detail.project.activeRevisionId)); }
function isPolished(value) { return value !== 'quick'; }
function generationAvailable() { return Boolean(state.configured.generation && state.configured.build); }
function polishName(value) { return isPolished(value) ? 'Polished prototype' : 'Quick prototype'; }
function projectPath(path = '') { return `/api/projects/${encodeURIComponent(state.detail.project.id)}${path}`; }
function inviteToken() { return location.pathname.match(/^\/(?:invite|join)\/([^/]+)\/?$/)?.[1] || null; }
function setRoute(projectId = null) {
  closeModal();
  const target = projectId ? `#project/${encodeURIComponent(projectId)}` : '#';
  if (location.hash === target || (!projectId && !location.hash)) route();
  else location.hash = target;
}
function stopRoom() {
  if (briefDoc && pendingBriefUpdates.length && briefProjectId) {
    navigator.sendBeacon(`/api/projects/${encodeURIComponent(briefProjectId)}/document`,new Blob([JSON.stringify({clientId,update:toBase64(Y.encodeStateAsUpdate(briefDoc))})],{type:'application/json'}));
  }
  source?.close(); source = null;
  room?.destroy(); room = null;
  briefDoc?.destroy(); briefDoc = null; briefText = null; briefProjectId = null; state.briefReady = false;
  pendingBriefUpdates = []; briefWriteChain = Promise.resolve();
  clearTimeout(activityTimer); lastActivity = null;
}
function showError(message, retry = true) {
  app.innerHTML = `<main class="invite-page">${brand()}<div class="modal"><h1>A little loading hiccup.</h1><p>${escape(message)}</p>${retry ? '<button class="button primary" data-action="retry">Try again</button>' : ''}</div></main>`;
}
function makerIllustration() {
  return `<div class="maker-illustration" aria-hidden="true"><svg viewBox="0 0 420 310"><ellipse cx="220" cy="266" rx="145" ry="21" fill="#0a0c1b" opacity=".45"/><g class="art-sparkles" fill="#fce888"><path d="m62 42 4 12 12 4-12 4-4 12-4-12-12-4 12-4Z"/><path d="m363 174 3 9 9 3-9 3-3 9-3-9-9-3 9-3Z"/><circle cx="342" cy="49" r="4"/><circle cx="69" cy="216" r="3"/></g><g class="art-floating" transform="translate(84 37) rotate(-9 125 108)"><rect x="11" y="15" width="253" height="192" rx="28" fill="#0b0d1d"/><rect x="3" y="4" width="253" height="192" rx="28" fill="#b5a5ff" stroke="#10132b" stroke-width="5"/><rect x="24" y="24" width="211" height="119" rx="12" fill="#20264c" stroke="#10132b" stroke-width="4"/><circle cx="198" cy="50" r="13" fill="#ffe590"/><path d="m28 122 56-61 37 40 28-27 82 65H28Z" fill="#736aca"/><path d="m28 129 43-33 35 21 51-45 74 58v10H28Z" fill="#b7e7be"/><rect x="113" y="76" width="25" height="29" rx="7" fill="#ff9d87" stroke="#10132b" stroke-width="3"/><path d="M119 87h2m8 0h2" stroke="#10132b" stroke-width="4" stroke-linecap="round"/><path d="M43 161v20m-10-10h20" stroke="#29254b" stroke-width="8" stroke-linecap="round"/><circle cx="204" cy="165" r="9" fill="#fce888" stroke="#29254b" stroke-width="3"/><circle cx="224" cy="177" r="9" fill="#ff9d87" stroke="#29254b" stroke-width="3"/><rect x="107" y="165" width="37" height="7" rx="3.5" fill="#71629c"/></g><g class="art-token" transform="translate(42 169) rotate(12 42 39)"><path d="m42 0 39 21v43L42 86 3 64V21Z" fill="#8bdaa8" stroke="#10132b" stroke-width="4"/><path d="m42 0 39 21-39 22L3 21Z" fill="#c6ffb1"/><path d="M42 43v43L3 64V21Z" fill="#83bd83"/><path d="m42 16 5 10 11 1-8 8 2 11-10-5-10 5 2-11-8-8 11-1Z" fill="#365149"/></g><g class="art-bubble" transform="translate(312 83) rotate(12)"><rect x="4" y="6" width="66" height="67" rx="18" fill="#0b0d1d"/><rect width="66" height="67" rx="18" fill="#ffad94" stroke="#10132b" stroke-width="4"/><path d="M19 32h28m-14-14v28" stroke="#6d3c4c" stroke-width="7" stroke-linecap="round"/></g><path d="M297 253c13-18 25-18 38-9" fill="none" stroke="#b5a5ff" stroke-width="4" stroke-linecap="round"/><path d="m327 233 10 12-15 1" fill="none" stroke="#b5a5ff" stroke-width="4" stroke-linecap="round"/></svg></div>`;
}
function socialLogo(provider) {
  if (provider === 'google') return '<img src="/google-g.png" width="18" height="18" alt="" class="social-logo">';
  return '<svg viewBox="0 0 24 24" aria-hidden="true" class="social-logo"><path fill="currentColor" d="M19.8 5.5a18.5 18.5 0 0 0-4.5-1.4l-.6 1.2a17 17 0 0 0-5.4 0l-.6-1.2a18.5 18.5 0 0 0-4.5 1.4C1.4 9.7.6 13.8 1 17.9a18 18 0 0 0 5.5 2.8l1.1-1.8-1.7-.8.4-.3a13 13 0 0 0 11.4 0l.4.3-1.7.8 1.1 1.8a18 18 0 0 0 5.5-2.8c.5-4.8-.8-8.8-3.2-12.4ZM8.5 15.5c-1.1 0-2-1-2-2.3s.9-2.3 2-2.3 2 1 2 2.3-.9 2.3-2 2.3Zm7 0c-1.1 0-2-1-2-2.3s.9-2.3 2-2.3 2 1 2 2.3-.9 2.3-2 2.3Z"/></svg>';
}
function socialAuthButtons() {
  return state.authProviders.map((provider) => `<a class="button social-button ${provider.id === 'discord' ? 'discord-button' : 'google-button'}" href="${escape(provider.href)}${inviteToken() ? `?returnTo=${encodeURIComponent(location.pathname)}` : ''}">${socialLogo(provider.id)}Continue with ${escape(provider.label)}</a>`).join('');
}
function renderAuth() {
  stopRoom();
  const social = state.authProviders.length > 0;
  app.innerHTML = `<main class="welcome"><section class="welcome-story">${brand()}<div class="welcome-copy"><p class="eyebrow"><span class="tiny-star">✦</span> Big imagination. Small playable worlds.</p><h1>Got an idea?<br><em>Press start.</em></h1><p>Make the game you can’t stop thinking about.<br>Build a little world. Bring your friends. Find the fun.</p>${makerIllustration()}<div class="welcome-tags"><span>${icon('spark')} Dream it up</span><span>${icon('game')} Play it out</span><span>${icon('people')} Make it together</span></div></div><footer class="welcome-footer"><span>For everyone with a game on their mind.</span><span class="mini-logo">A little idea can go a long way ↗</span></footer></section><section class="welcome-form"><div class="auth-card"><span class="auth-badge">${icon('flag')}Your adventure starts here</span><h2>Let’s make a little magic.</h2><p>${inviteToken() ? 'Your friends saved you a spot. Sign in to join their project.' : 'Your ideas, your people, your next playable obsession.'}</p><p id="oauth-error" class="error" role="alert">${escape(authError)}</p>${social ? `<div class="social-auth">${socialAuthButtons()}</div><p class="signin-note">New here? Your first sign-in creates your account.<br>Already part of the party? Use the way you joined.</p>` : `<div id="signin-unavailable" class="signin-unavailable"><span class="signin-unavailable-icon">${icon('game')}</span><h3>The lobby is warming up.</h3><p>Sign-in isn’t available just yet. Come back soon to start your next little adventure.</p><button class="button" data-action="retry">${icon('refresh')}Check again</button></div>`}<div class="signin-perks"><span>${icon('spark')}Give your ideas a home</span><span>${icon('people')}Bring your favorite people</span><span>${icon('history')}Pick up where you left off</span></div><p class="auth-note">Early access · Make, play, and experiment.<br>No payment required in this first version.</p></div></section></main>`;
}
function sidebar() {
  return `<aside class="sidebar">${brand()}<nav aria-label="Workspace"><button class="nav-item ${!state.detail && state.view !== 'admin' ? 'active' : ''}" data-action="dashboard">${icon('grid')}Your lobby<span class="nav-end">${state.projects.length}</span></button><button class="nav-item" data-action="new-project">${icon('plus')}New project</button>${state.user.isAdmin ? `<button class="nav-item ${state.view === 'admin' ? 'active' : ''}" data-action="admin">${icon('monitor')}Service overview</button>` : ''}</nav><div class="workspace-label">Your little worlds</div><nav class="sidebar-projects" aria-label="Projects" id="sidebar-projects">${projectNav()}</nav><div class="sidebar-footer"><div class="lobby-note">${icon('flag')}<div><strong>Keep chasing the fun.</strong><p>${state.projects.length ? `${state.projects.length} ${state.projects.length === 1 ? 'idea' : 'ideas'} with a place to grow.` : 'Your next adventure starts with an idea.'}</p></div></div><div class="user-row">${avatar(state.user.name)}<button class="user-info" data-action="account" title="Account and connected sign-ins"><strong>${escape(state.user.name)}</strong><small>Your account ${icon('gear')}</small></button><button class="icon-button" title="Sign out" aria-label="Sign out" data-action="logout">${icon('logout')}</button></div></div></aside>`;
}
function projectNav() {
  return state.projects.length ? state.projects.map((p) => `<button class="project-nav ${state.detail?.project.id === p.id ? 'active' : ''}" data-action="project" data-id="${escape(p.id)}"><span class="project-swatch"></span><span>${escape(p.title)}</span></button>`).join('') : '<p class="help" style="padding:0 12px">Your ideas will live here.</p>';
}
function topbar(project = false) {
  return `<header class="topbar"><div class="breadcrumbs"><button class="icon-button mobile-menu" data-action="menu" aria-label="Open navigation">${icon('menu')}</button><span class="crumb-parent">Your lobby</span>${project ? `${icon('chevron')}<strong id="crumb-title">${escape(state.detail.project.title)}</strong>` : `${icon('chevron')}<strong>Overview</strong>`}</div><div class="topbar-actions">${project ? `<div class="collaborators" id="collaborators">${presence()}</div><button class="button small" data-action="take-control" id="take-control">${icon('game')}<span class="take-control-label">Take control</span></button><button class="button primary small" data-action="share">${icon('people')}Share</button>` : '<span class="pill lime"><span class="dot"></span>Early access</span>'}</div></header>`;
}
function presence() {
  const online = [...new Map((state.detail?.room?.presence || []).map((p) => [p.userId,p])).values()];
  return `<div class="avatar-stack">${online.slice(0,4).map((p) => avatar(p.name, `${p.name} · in the playtest`)).join('')}</div><small>${online.length ? `${online.length} here` : 'Just you for now'}</small>`;
}
function renderDashboard() {
  state.detail = null; state.menu = false; state.view = 'dashboard';
  const playable = state.projects.filter((project) => project.activeRevisionId).length;
  app.innerHTML = `<div class="shell">${sidebar()}<main class="workspace-main">${topbar()}<div class="dashboard"><div class="dashboard-intro"><div><p class="eyebrow">${icon('spark')} Your maker lobby</p><h1>Hey ${escape(state.user.name.split(' ')[0])}. What if…?</h1><p>A tiny world, a strange mechanic, your next obsession. Follow the fun.</p></div><button class="button primary" data-action="new-project">${icon('plus')}New project</button></div><section class="create-card"><div class="create-card-copy"><span class="quest-badge">${icon('flag')}Your next adventure</span><h2>That idea deserves<br>a <em>play button.</em></h2><p>Tell us what you’re imagining. Make it real, pass the controller, and see what happens next.</p><button class="button primary" data-action="new-project">Let’s make a game ${icon('arrowRight')}</button></div>${makerIllustration()}<div class="hero-trail" aria-hidden="true"><span>01 · Dream</span><span>02 · Play</span><span>03 · Make it better</span></div></section><div class="lobby-stats"><span>${icon('spark')}<strong>${state.projects.length}</strong>${state.projects.length === 1 ? 'idea saved' : 'ideas saved'}</span><span>${icon('game')}<strong>${playable}</strong>${playable === 1 ? 'playable project' : 'playable projects'}</span><span>${icon('people')}Made for making together</span></div><div class="section-heading"><h2>Your projects <span class="heading-spark">✦</span></h2><small>${state.projects.length ? 'Pick up an adventure' : 'Every great game starts somewhere'}</small></div>${state.projects.length ? `<div class="project-grid">${state.projects.map(projectCard).join('')}</div>` : `<div class="dashboard-empty"><div class="empty">${icon('flag')}<h3>Your first adventure is waiting.</h3><p>Give that “what if” a name. You can figure out the rest as you play.</p><button class="button" data-action="new-project">${icon('plus')}Start your first project</button></div></div>`}</div></main></div>`;
}
function projectCard(project,index) {
  return `<button class="project-card card-color-${index%4}" data-action="project" data-id="${escape(project.id)}"><div class="project-card-art"><span class="pill">${icon(isPolished(project.polish) ? 'spark' : 'bolt')}${escape(polishName(project.polish))}</span><div class="project-token" aria-hidden="true"><span>${escape(initials(project.title))}</span>${icon('game')}</div><span class="card-spark card-spark-one" aria-hidden="true">✦</span><span class="card-spark card-spark-two" aria-hidden="true">+</span></div><div class="project-card-info"><h3>${escape(project.title)}</h3><p>${escape(project.idea)}</p><div class="project-card-progress"><span class="complete">${icon('check')}Idea saved</span><i></i><span class="${project.activeRevisionId ? 'complete' : ''}">${icon(project.activeRevisionId ? 'check' : 'game')}${project.activeRevisionId ? 'Playable' : 'Up next: play'}</span></div><div class="project-card-meta"><span>Edited ${escape(relativeTime(project.updatedAt))}</span><span class="card-enter">Open project ${icon('arrowRight')}</span></div></div></button>`;
}
function projectProgress() {
  if (!state.detail) return '';
  const revisions = state.detail.revisions.length;
  const refined = state.detail.revisions.some((revision) => revision.origin !== 'starter');
  return `<div class="project-progress"><div class="progress-path"><span class="progress-step complete">${icon('check')}<strong>Idea</strong></span><i></i><span class="progress-step ${revisions ? 'complete' : ''}">${icon(revisions ? 'check' : 'game')}<strong>Playable</strong></span><i></i><span class="progress-step ${refined ? 'complete' : ''}">${icon(refined ? 'check' : 'spark')}<strong>Keep exploring</strong></span></div><div class="progress-facts"><span>${revisions} ${revisions === 1 ? 'version' : 'versions'}</span><span>${state.detail.feedback.length} ${state.detail.feedback.length === 1 ? 'note' : 'notes'}</span></div></div>`;
}
function renderProject() {
  const project = state.detail.project;
  state.menu = false; state.view = 'project';
  app.innerHTML = `<div class="shell">${sidebar()}<main class="workspace-main">${topbar(true)}<div class="project-workspace"><section class="play-area"><header class="play-header"><div><p class="eyebrow">The playtest room</p><h1>${escape(project.title)}</h1><p>Try it. Break it. Make it a little better.</p></div><span class="pill">${icon('spark')}${escape(polishName(project.polish))}</span></header><div class="play-stage-wrap"><div class="stage-topline"><span class="stage-state"><span class="dot"></span><span id="stage-status">Opening your game…</span></span><span class="stage-device">${icon('monitor')}Web preview</span></div><div class="play-stage" id="game-stage"><iframe id="game-frame" title="Playable game preview" allow="autoplay; fullscreen; gamepad; cross-origin-isolated" hidden></iframe><video id="game-stream" autoplay playsinline hidden aria-label="Live playtest stream"></video><div class="stage-empty" id="stage-empty"><div class="empty">${icon('game')}<h3>Your game takes shape here.</h3><p>This project doesn’t have a playable build yet. Describe what you want to make in the conversation.</p></div></div><div class="game-overlay" id="game-overlay"><div id="game-pins"></div></div><div id="pin-editor-root"></div></div><div class="stage-bottomline"><span class="revision-label" id="revision-label">${icon('history')}No playable revision yet</span><div class="stage-controls"><button class="icon-button" data-action="reload-game" title="Reload the game" aria-label="Reload the game">${icon('refresh')}</button><button class="icon-button" data-action="fullscreen" title="Fullscreen preview" aria-label="Fullscreen preview">${icon('fullscreen')}</button></div></div></div><div class="play-toolbar"><p class="help">${icon('people')}Friends can watch and take control anytime.</p><div class="play-toolbar-actions"><button class="button small ghost" data-action="feedback-toggle" id="feedback-toggle">${icon('pin')}Leave feedback</button><button class="button small ghost" data-action="panel" data-panel="history">${icon('history')}Version history</button></div></div><div class="room-notice" id="room-notice"></div><div class="idea-strip">${icon('spark')}<div><p class="eyebrow">The starting idea</p><p>${escape(project.idea)}</p></div></div></section><aside class="right-panel" aria-label="Project workspace"><nav class="panel-tabs" aria-label="Project panels">${[['chat','chat','Create'],['feedback','pin','Feedback'],['assets','image','Assets'],['history','history','History']].map(([key,glyph,label]) => `<button class="panel-tab ${state.panel === key ? 'active' : ''}" data-action="panel" data-panel="${key}">${icon(glyph)}${label}</button>`).join('')}</nav><div class="panel-body" id="panel-body"></div><form class="composer" id="chat-form"><div class="generation-note" id="generation-note" ${state.configured.generation ? 'hidden' : ''}>Game creation is taking a break. You can still plan and play together.</div><div class="composer-box"><label class="sr-only" for="chat-prompt">Describe your game or a change</label><textarea id="chat-prompt" name="prompt" maxlength="16000" placeholder="What should we make next?" required>${escape(state.draft)}</textarea><div class="composer-bottom"><label class="sr-only" for="iteration-polish">Iteration focus</label><select id="iteration-polish" name="polish"><option value="polished" ${state.polish !== 'quick' ? 'selected' : ''}>✧ Polished prototype</option><option value="quick" ${state.polish === 'quick' ? 'selected' : ''}>↗ Quick prototype</option></select><button class="send-button" type="submit" title="Send to the agent" aria-label="Send to the agent">${icon('send')}</button></div></div><p class="help">${state.configured.generation ? 'Changes become candidates you can play and keep.' : 'Your idea is saved. Keep shaping it with your team.'}</p></form></aside></div></main></div>`;
  document.querySelector('.play-header').insertAdjacentHTML('afterend',`<div id="project-progress">${projectProgress()}</div>`);
  document.querySelector('.panel-tabs').firstElementChild.insertAdjacentHTML('afterend', `<button class="panel-tab ${state.panel === 'brief' ? 'active' : ''}" data-action="panel" data-panel="brief">${icon('file')}Brief</button>`);
  document.querySelector('#chat-form').insertAdjacentHTML('afterbegin','<p class="typing-indicator" id="typing-indicator" aria-live="polite"></p>');
  if (!generationAvailable()) {
    const note = document.querySelector('#generation-note');
    note.hidden = false;
    note.textContent = 'Game creation isn’t available right now. You can still plan, chat, share assets, and playtest together.';
    document.querySelector('#chat-prompt').placeholder = 'Talk through the idea with your team…';
    document.querySelector('.send-button').title = 'Save message for the team';
    document.querySelector('.send-button').setAttribute('aria-label','Save message for the team');
    document.querySelector('#chat-form>.help').textContent = 'Keep the ideas coming. Your messages are saved for the whole team.';
  }
  renderPanel();
  renderPins();
  room = mountRoom({ projectId:project.id, clientId, frame:document.querySelector('#game-frame'), video:document.querySelector('#game-stream'), overlay:document.querySelector('#game-overlay'), onStatus:roomStatus });
  room.update(state.detail);
  syncProject();
  openEvents();
  loadBrief();
}
function roomStatus(status) {
  const text = typeof status === 'string' ? status : status?.text || status?.status || '';
  state.roomStatus = text;
  const label = document.querySelector('#stage-status');
  if (label && text) label.textContent = text;
  const notice = document.querySelector('#room-notice');
  if (notice && typeof status === 'object' && status?.detail) notice.textContent = status.detail;
  const empty = document.querySelector('#stage-empty');
  const frame = document.querySelector('#game-frame');
  const video = document.querySelector('#game-stream');
  if (empty && currentRevision()) empty.hidden = true;
  if (typeof status === 'object' && status?.error && empty && frame?.hidden && video?.hidden) {
    empty.hidden = false;
    empty.innerHTML = `<div class="empty">${icon('monitor')}<h3>The playtest is connecting.</h3><p>${escape(status.detail || text)}</p></div>`;
  }
  renderPins();
}
function renderPanel() {
  const panel = document.querySelector('#panel-body');
  if (!panel || !state.detail) return;
  const nearBottom = panel.scrollHeight - panel.scrollTop - panel.clientHeight < 70;
  const detail = state.detail;
  document.querySelectorAll('.panel-tab').forEach((button) => button.classList.toggle('active', button.dataset.panel === state.panel));
  const composer = document.querySelector('#chat-form');
  if (composer) composer.hidden = state.panel !== 'chat';
  if (state.panel === 'brief' && panel.querySelector('#shared-brief:not(:disabled)')) { updateTypingIndicators(); return; }
  if (state.panel === 'chat') {
    panel.innerHTML = `<div class="panel-intro"><span class="agent-mark">${icon('spark')}</span><div>Your creative partner<small>A little conversation. A playable possibility.</small></div></div>${detail.messages.length ? detail.messages.map((message) => `<article class="message ${message.role === 'assistant' ? 'assistant' : 'user'}"><div class="message-head">${message.role === 'assistant' ? `<span class="agent-mark">${icon('spark')}</span>` : avatar(message.name || 'Teammate')}<strong>${escape(message.role === 'assistant' ? 'Mockup' : message.name || 'Teammate')}</strong><time title="${escape(fullTime(message.createdAt))}">${escape(relativeTime(message.createdAt))}</time></div><div class="message-body">${escape(message.text)}</div></article>`).join('') : `<div class="empty">${icon('spark')}<h3>Let’s give your idea a little life.</h3><p>Tell the agent what should happen, what it should feel like, or one thing you’d like to try.</p></div>`}${detail.jobs.filter((job) => !['succeeded'].includes(job.status)).map(jobCard).join('')}`;
    if (nearBottom) panel.scrollTop = panel.scrollHeight;
  } else if (state.panel === 'brief') {
    panel.innerHTML = `<div class="panel-section-head"><h3>The shared idea.</h3><small id="brief-status">${state.briefReady ? 'Saved live' : 'Connecting…'}</small></div><p class="asset-help">Shape the direction together. Everyone’s edits appear live, and the agent uses this brief for its next iteration.</p><label class="brief-label" for="shared-brief">What are we making?</label><textarea id="shared-brief" class="shared-brief" placeholder="What’s the world, the feeling, and the one thing that makes it fun?" ${state.briefReady ? '' : 'disabled'}>${escape(briefText?.toString() || detail.project.idea)}</textarea><p class="brief-presence" id="brief-presence"></p><p class="help">Edits merge as you type. You don’t need to take turns.</p><div class="error" id="brief-error" role="alert">${escape(state.briefError)}</div>${state.briefError ? '<button class="button small" data-action="retry-brief">Retry saving</button>' : ''}`;
    updateTypingIndicators();
  } else if (state.panel === 'feedback') {
    panel.innerHTML = `<div class="panel-section-head"><h3>Make the rough edges useful.</h3><small>${detail.feedback.length} notes</small></div><p class="asset-help">Point at a moment in the game and tell everyone what you noticed.</p><button class="button small ${state.feedbackMode ? 'feedback-on' : ''}" data-action="feedback-toggle">${icon('pin')}${state.feedbackMode ? 'Finish marking' : 'Pin a note in the game'}</button><div style="margin-top:20px">${detail.feedback.length ? detail.feedback.map((item,index) => `<article class="feedback-card"><div class="feedback-card-head"><span class="feedback-number">${index+1}</span><strong>${escape(item.name || 'Teammate')}</strong><time title="${escape(fullTime(item.createdAt))}">${escape(relativeTime(item.createdAt))}</time></div><p>${escape(item.text)}</p><div class="feedback-card-foot"><span>${icon('history')}${escape(revisionName(item.revisionId))}</span><span>${icon(item.capture?.screenshot ? 'camera' : 'pin')}${item.capture?.screenshot ? 'Moment captured' : 'Pinned note'}</span></div></article>`).reverse().join('') : `<div class="empty">${icon('pin')}<h3>“Right here. This bit.”</h3><p>Leave a pin to show what feels good, what feels strange, or where you got stuck.</p></div>`}</div>`;
  } else if (state.panel === 'assets') {
    panel.innerHTML = `<div class="panel-section-head"><h3>Make it look like your idea.</h3><small>${detail.assets.length} assets</small></div><p class="asset-help">Bring sprites, models, music, or a visual reference. Originals stay in your shared project library.</p><div class="upload-zone" id="upload-zone">${icon('upload')}<strong>A little of your world.</strong><p>Drop files here, or choose them below.</p><div class="upload-options"><label for="asset-usage">Use as</label><select id="asset-usage"><option value="game" ${state.assetUsage === 'game' ? 'selected' : ''}>Game assets</option><option value="reference" ${state.assetUsage === 'reference' ? 'selected' : ''}>Visual references</option></select></div><button class="button small" data-action="upload">Choose files</button><input id="asset-files" type="file" multiple hidden><p class="help" id="upload-status" role="status" style="margin:12px 0 0">Up to 10 MB per file</p></div>${detail.assets.length ? detail.assets.map((asset) => `<div class="asset-card"><div class="asset-icon">${asset.type?.startsWith('image/') && !asset.type.includes('svg') ? `<img src="${escape(asset.url)}" alt="" loading="lazy">` : icon(asset.type?.startsWith('image/') ? 'image' : 'file')}</div><div class="asset-info"><a href="${escape(asset.url)}" target="_blank" rel="noopener noreferrer" download="${escape(asset.name)}">${escape(asset.name)}</a><small>${formatBytes(asset.size)} · ${escape(relativeTime(asset.createdAt))}</small></div><span class="pill">${asset.usage === 'reference' ? 'Reference' : 'Game asset'}</span></div>`).reverse().join('') : '<p class="help">No assets yet. Your own art can arrive at any point.</p>'}`;
    setupDropZone();
  } else {
    panel.innerHTML = `<div class="panel-section-head"><h3>Every little evolution.</h3><small>${detail.revisions.length} versions</small></div><p class="asset-help">Try a candidate, or return to a previous version. The shared game only changes when someone keeps a version.</p>${detail.revisions.length ? [...detail.revisions].reverse().map((revision) => `<article class="revision-card"><div class="revision-card-header"><strong>Version ${escape(revision.number)}</strong><span class="pill ${revision.id === detail.project.activeRevisionId ? 'lime' : ''}">${revision.id === detail.project.activeRevisionId ? 'Shared version' : revision.status === 'candidate' ? 'Candidate' : 'Previous version'}</span></div><p>${escape(revision.summary || 'A new iteration.')}</p><div class="revision-card-footer"><time title="${escape(fullTime(revision.createdAt))}">${escape(relativeTime(revision.createdAt))}</time>${revision.id !== detail.project.activeRevisionId ? `<button class="button small" data-action="activate" data-id="${escape(revision.id)}">Keep this version ${icon('check')}</button>` : '<span class="help">Playing now</span>'}</div></article>`).join('') : `<div class="empty">${icon('history')}<h3>One idea. Many possibilities.</h3><p>Your playable versions will be kept here as the project develops.</p></div>`}`;
  }
  if (state.panel === 'history') {
    const revisions = [...detail.revisions].reverse();
    panel.querySelectorAll('.revision-card').forEach((card,index) => {
      const revision = revisions[index];
      const active = revision.id === (detail.room?.revisionId || detail.project.activeRevisionId);
      const footer = card.querySelector('.revision-card-footer');
      const label = footer.querySelector('.help');
      if (label) label.textContent = 'Shared version';
      const host = detail.room?.hostClientId === clientId;
      footer.insertAdjacentHTML('beforeend',active ? '<span class="help playing-label">In this playtest</span>' : `<button class="button small ghost" data-action="play-revision" data-id="${escape(revision.id)}" ${host ? '' : 'disabled title="The playtest host can switch versions"'}>${icon('game')}Play this version</button>`);
    });
  }
}
function formatBytes(size) { return size < 1024 ? `${size} B` : size < 1024*1024 ? `${Math.ceil(size/1024)} KB` : `${(size/(1024*1024)).toFixed(1)} MB`; }
function revisionName(id) { const revision = state.detail.revisions.find((r) => r.id === id); return revision ? `Version ${revision.number}` : 'Before the first build'; }
function jobCard(job) {
  const titles = {queued:'Your idea is in the queue',generating:'Working on your idea',building:'Building the playable version',failed:'This iteration needs another try'};
  return `<div class="job-card ${job.status === 'failed' ? 'failed' : ''}"><div class="job-title">${icon(job.status === 'failed' ? 'chat' : 'spark')}${escape(titles[job.status] || job.status)}</div><p>${escape(job.error || job.prompt)}</p></div>`;
}
function syncProject() {
  if (!state.detail || !document.querySelector('#game-frame')) return;
  const revision = currentRevision();
  document.querySelector('#sidebar-projects').innerHTML = projectNav();
  document.querySelector('#project-progress').innerHTML = projectProgress();
  document.querySelector('#collaborators').innerHTML = presence();
  const controller = state.detail.room?.controllerClientId === clientId;
  document.querySelector('#take-control').innerHTML = `${icon('game')}<span class="take-control-label">${controller ? 'You’re controlling' : 'Take control'}</span>`;
  document.querySelector('#take-control').classList.toggle('primary', controller);
  document.querySelector('#revision-label').innerHTML = `${icon('history')}${revision ? `Version ${escape(revision.number)}${revision.origin === 'starter' ? ' · Starter playground' : ''}` : 'No playable revision yet'}`;
  document.querySelector('#stage-empty').hidden = Boolean(revision);
  const toggle = document.querySelector('#feedback-toggle');
  toggle.classList.toggle('feedback-on', state.feedbackMode);
  toggle.innerHTML = `${icon('pin')}${state.feedbackMode ? 'Finish marking' : 'Leave feedback'}`;
  document.querySelector('#game-overlay').classList.toggle('feedback-mode', state.feedbackMode);
  renderPins();
  renderPanel();
  room?.update(state.detail);
}
function renderPins() {
  const pins = document.querySelector('#game-pins');
  if (!pins || !state.detail) return;
  const revision = state.detail.room?.revisionId || state.detail.project.activeRevisionId;
  const stage = document.querySelector('#game-stage');
  const video = document.querySelector('#game-stream');
  let width=stage.clientWidth, height=stage.clientHeight, left=0, top=0;
  if (!video.hidden && video.videoWidth) {
    const ratio=video.videoWidth/video.videoHeight;
    width=Math.min(stage.clientWidth,stage.clientHeight*ratio); height=width/ratio;
    left=(stage.clientWidth-width)/2; top=(stage.clientHeight-height)/2;
  }
  const position = (pin) => `left:${left+Math.max(0,Math.min(1,Number(pin.x)))*width}px;top:${top+Math.max(0,Math.min(1,Number(pin.y)))*height}px`;
  pins.innerHTML = state.detail.feedback.map((item,index) => item.revisionId === revision ? `<button class="pin-dot" data-action="view-pin" data-id="${escape(item.id)}" style="${position(item)}" title="${escape(item.name || 'Teammate')}: ${escape(item.text)}" aria-label="Feedback ${index+1}: ${escape(item.text)}">${index+1}</button>` : '').join('');
  if (state.pin) pins.insertAdjacentHTML('beforeend', `<span class="pin-dot" style="${position(state.pin)}" aria-hidden="true">+</span>`);
}
function renderPinEditor() {
  const root = document.querySelector('#pin-editor-root');
  if (!root) return;
  if (!state.pin) { root.innerHTML = ''; return; }
  root.innerHTML = `<form class="pin-editor" id="feedback-form"><p class="eyebrow">A note at this moment</p><label class="sr-only" for="feedback-text">What did you notice?</label><textarea id="feedback-text" name="text" placeholder="What did you notice? What should change?" maxlength="4000" required>${escape(state.pinDraft)}</textarea><div class="pin-editor-actions"><small>Attached to this version. A preview capture is included when available.</small><div><button class="button small ghost" data-action="cancel-pin" type="button">Cancel</button><button class="button primary small" type="submit">Save note</button></div></div></form>`;
  document.querySelector('#feedback-text').focus();
}
function openEvents() {
  source?.close();
  const projectId = state.detail.project.id;
  source = new EventSource(`/api/projects/${encodeURIComponent(projectId)}/events?clientId=${encodeURIComponent(clientId)}`);
  for (const type of ['snapshot','project','room','job','input','signal','presence','document']) source.addEventListener(type, (event) => {
    if (state.detail?.project.id !== projectId) return;
    let data;
    try { data = JSON.parse(event.data); } catch { return; }
    if (type === 'snapshot' || type === 'project') {
      state.detail = data;
      const index = state.projects.findIndex((p) => p.id === projectId);
      if (index >= 0) state.projects[index] = data.project;
      syncProject();
    } else if (type === 'room') {
      state.detail.room = data;
      document.querySelector('#collaborators').innerHTML = presence();
      room?.handleEvent(type, data);
      syncProject();
    } else if (type === 'presence') {
      const presence = state.detail.room.presence || [];
      const index = presence.findIndex((member) => member.clientId === data.clientId);
      if (index >= 0) presence[index] = {...presence[index],...data}; else presence.push(data);
      state.detail.room.presence = presence;
      room?.handleEvent(type,data);
      updateTypingIndicators();
    } else if (type === 'document') {
      if (briefDoc) Y.applyUpdate(briefDoc,fromBase64(data.update),'remote');
    } else if (type === 'job') {
      const index = state.detail.jobs.findIndex((job) => job.id === data.id);
      if (index >= 0) state.detail.jobs[index] = data; else state.detail.jobs.push(data);
      renderPanel();
    } else room?.handleEvent(type, data);
  });
  source.onerror = () => {
    const notice = document.querySelector('#room-notice');
    if (notice) notice.textContent = 'The live connection was interrupted. Reconnecting automatically…';
  };
  source.onopen = () => {
    const notice = document.querySelector('#room-notice');
    if (notice) notice.textContent = '';
  };
}
async function refreshProject() {
  if (!state.detail) return;
  state.detail = await api(projectPath());
  const index = state.projects.findIndex((p) => p.id === state.detail.project.id);
  if (index >= 0) state.projects[index] = state.detail.project;
  syncProject();
}
function toBase64(bytes) {
  let binary = '';
  for (let i=0;i<bytes.length;i+=8192) binary += String.fromCharCode(...bytes.subarray(i,i+8192));
  return btoa(binary);
}
function fromBase64(value) { return Uint8Array.from(atob(value), (char) => char.charCodeAt(0)); }
async function loadBrief() {
  const projectId = state.detail.project.id;
  briefProjectId = projectId;
  briefDoc = new Y.Doc();
  const doc = briefDoc;
  briefText = doc.getText('brief');
  doc.on('beforeTransaction',(transaction) => {
    const textarea = document.querySelector('#shared-brief');
    if (textarea && document.activeElement === textarea && transaction.origin !== 'local') savedSelection = {
      start:Y.createRelativePositionFromTypeIndex(briefText,textarea.selectionStart),
      end:Y.createRelativePositionFromTypeIndex(briefText,textarea.selectionEnd),
      direction:textarea.selectionDirection,
    };
    else savedSelection = null;
  });
  briefText.observe((event) => {
    if (doc !== briefDoc) return;
    const text = briefText.toString();
    state.detail.project.idea = text;
    const strip = document.querySelector('.idea-strip>div>p:last-child');
    if (strip) strip.textContent = text;
    const textarea = document.querySelector('#shared-brief');
    if (textarea && event.transaction.origin !== 'local') {
      textarea.value = text;
      if (savedSelection) {
        const start = Y.createAbsolutePositionFromRelativePosition(savedSelection.start,doc);
        const end = Y.createAbsolutePositionFromRelativePosition(savedSelection.end,doc);
        if (start && end) textarea.setSelectionRange(start.index,end.index,savedSelection.direction);
      }
    }
  });
  doc.on('update',(update,origin) => {
    if (origin === 'remote' || doc !== briefDoc) return;
    pendingBriefUpdates.push(update);
    try { sessionStorage.setItem(`mockup-brief-${projectId}`,toBase64(Y.encodeStateAsUpdate(doc))); } catch { /* Saving to the service remains the source of truth. */ }
    briefWriteChain = briefWriteChain.then(() => flushBrief(doc,projectId));
  });
  try {
    const result = await api(`/api/projects/${encodeURIComponent(projectId)}/document`);
    if (doc !== briefDoc) return;
    Y.applyUpdate(doc,fromBase64(result.update),'remote');
    try { const unsaved = sessionStorage.getItem(`mockup-brief-${projectId}`); if (unsaved) Y.applyUpdate(doc,fromBase64(unsaved),'local'); } catch { /* An invalid browser draft does not replace the shared document. */ }
    state.briefReady = true; state.briefError = '';
    if (state.panel === 'brief') renderPanel();
  } catch (error) {
    if (doc !== briefDoc) return;
    state.briefError = `The shared brief couldn’t connect: ${error.message}`;
    if (state.panel === 'brief') renderPanel();
  }
}
async function flushBrief(doc = briefDoc, projectId = briefProjectId) {
  if (!doc || doc !== briefDoc || !pendingBriefUpdates.length) return;
  const batch = pendingBriefUpdates.slice();
  updateBriefStatus('Saving…');
  try {
    await api(`/api/projects/${encodeURIComponent(projectId)}/document`,{method:'POST',body:{clientId,update:toBase64(Y.mergeUpdates(batch))}});
    if (doc !== briefDoc) return;
    pendingBriefUpdates.splice(0,batch.length);
    if (!pendingBriefUpdates.length) { try { sessionStorage.removeItem(`mockup-brief-${projectId}`); } catch { /* Storage may be unavailable in a private browser. */ } }
    state.briefError = '';
    updateBriefStatus(pendingBriefUpdates.length ? 'Saving…' : 'Saved live');
  } catch (error) {
    if (doc !== briefDoc) return;
    state.briefError = `Your latest edits haven’t reached the service. Keep this page open and retry: ${error.message}`;
    updateBriefStatus('Not saved yet');
  }
  const error = document.querySelector('#brief-error');
  if (error) {
    error.textContent = state.briefError;
    if (state.briefError && !error.querySelector('button')) error.insertAdjacentHTML('beforeend','<button class="button small" data-action="retry-brief" style="margin-top:12px">Retry saving</button>');
  }
}
function updateBriefStatus(text) { const element = document.querySelector('#brief-status'); if (element) element.textContent = text; }
function writeBrief(text) {
  if (!briefText || !state.briefReady) return;
  const previous = briefText.toString();
  if (previous === text) return;
  let start=0;
  while(start<previous.length && start<text.length && previous[start]===text[start]) start++;
  let oldEnd=previous.length, newEnd=text.length;
  while(oldEnd>start && newEnd>start && previous[oldEnd-1]===text[newEnd-1]) { oldEnd--; newEnd--; }
  briefDoc.transact(() => {
    if (oldEnd>start) briefText.delete(start,oldEnd-start);
    if (newEnd>start) briefText.insert(start,text.slice(start,newEnd));
  },'local');
}
function announceActivity(activity) {
  if (!state.detail) return;
  clearTimeout(activityTimer);
  if (lastActivity !== activity) {
    lastActivity = activity;
    api(projectPath('/presence'),{method:'POST',body:{clientId,activity}}).catch(() => {});
  }
  activityTimer = setTimeout(() => {
    lastActivity = null;
    if (state.detail) api(projectPath('/presence'),{method:'POST',body:{clientId,activity:null}}).catch(() => {});
  },2500);
}
function updateTypingIndicators() {
  const people = [...new Map((state.detail?.room?.presence || []).filter((person) => person.clientId !== clientId && person.activity).map((person) => [person.userId,person])).values()];
  const writing = people.filter((person) => person.activity === 'writing an idea').map((person) => person.name);
  const editing = people.filter((person) => person.activity === 'editing the brief').map((person) => person.name);
  const typing = document.querySelector('#typing-indicator');
  if (typing) typing.textContent = writing.length ? `${writing.join(', ')} ${writing.length === 1 ? 'is' : 'are'} writing…` : '';
  const brief = document.querySelector('#brief-presence');
  if (brief) brief.textContent = editing.length ? `${editing.join(', ')} ${editing.length === 1 ? 'is' : 'are'} editing with you` : 'A shared space for the whole team.';
}
const countFormat = new Intl.NumberFormat();
const moneyFormat = new Intl.NumberFormat(undefined,{style:'currency',currency:'USD',minimumFractionDigits:2,maximumFractionDigits:4});
function number(value) { return countFormat.format(Number(value) || 0); }
function cost(value) { return value == null ? 'Unavailable' : moneyFormat.format(value); }
function seconds(value) { return `${(Number(value) || 0).toFixed(1)} s`; }
function adminMetrics(summary) {
  return `<div class="metric-grid"><article class="metric-card"><span>Estimated model cost</span><strong>${cost(summary.estimatedCostUsd)}</strong><small>${summary.unpricedRequests ? `${number(summary.unpricedRequests)} unpriced requests · ${cost(summary.knownCostUsd)} known` : `${number(summary.pricedRequests)} priced requests`}</small></article><article class="metric-card"><span>Input tokens</span><strong>${number(summary.tokens.input)}</strong><small>${number(summary.tokens.cachedInput)} cached input tokens</small></article><article class="metric-card"><span>Output tokens</span><strong>${number(summary.tokens.output)}</strong><small>${number(summary.tokens.total)} total tokens</small></article><article class="metric-card"><span>Agent iterations</span><strong>${number(summary.jobs.total)}</strong><small>${number(summary.jobs.running)} active · ${number(summary.jobs.failed)} failed</small></article><article class="metric-card"><span>Build time</span><strong>${seconds(summary.buildSeconds)}</strong><small>${seconds(summary.modelSeconds)} model time</small></article><article class="metric-card"><span>Measured project storage</span><strong>${formatBytes(summary.totalBytes)}</strong><small>${formatBytes(summary.assetBytes)} assets · ${formatBytes(summary.artifactBytes)} published builds<br>${formatBytes(summary.workBytes)} work/cache · ${formatBytes(summary.feedbackBytes)} captures · ${formatBytes(summary.sourceBytes)} sources</small></article></div><p class="help admin-storage-note">${(summary.storageExclusions || []).map(escape).join(' ')}</p>`;
}
function renderAdmin() {
  const {summary,projects,recentJobs,service} = state.admin;
  state.detail = null; state.view = 'admin'; state.menu = false;
  app.innerHTML = `<div class="shell">${sidebar()}<main class="workspace-main">${topbar()}<div class="dashboard admin-dashboard"><div class="dashboard-intro"><div><p class="eyebrow">Owner workspace</p><h1>The whole maker world.</h1><p>Real provider usage, project resources, and the cost of every attempt.</p></div><button class="button" data-action="refresh-admin">${icon('refresh')}Refresh</button></div>${adminMetrics(summary)}<p class="help admin-cost-note">Estimated model cost uses the configured provider rates captured with each request. Missing rates remain unavailable. Build hosting, storage, and other infrastructure charges are not included.</p><div class="section-heading"><h2>All projects</h2><small>${number(projects.length)} projects · ${number(service.userCount)} accounts</small></div><div class="table-scroll"><table class="admin-table"><thead><tr><th>Project</th><th>Members</th><th>Tokens</th><th>Estimated model cost</th><th>Iterations</th><th>Storage</th></tr></thead><tbody>${projects.length ? projects.map((project) => `<tr><td><button class="table-project" data-action="admin-project" data-id="${escape(project.id)}">${escape(project.title)}</button><small>${escape(relativeTime(project.updatedAt))}</small></td><td>${number(project.members.length)}</td><td>${number(project.summary.tokens.total)}</td><td>${cost(project.summary.estimatedCostUsd)}</td><td>${number(project.summary.jobs.total)}<small>${number(project.summary.jobs.failed)} failed</small></td><td>${formatBytes(project.summary.totalBytes)}</td></tr>`).join('') : '<tr><td colspan="6">No projects yet.</td></tr>'}</tbody></table></div><div class="section-heading" style="margin-top:35px"><h2>Recent iterations</h2><small>Failures and retries included</small></div><div class="table-scroll"><table class="admin-table"><thead><tr><th>Request</th><th>Status</th><th>Input / output tokens</th><th>Estimated model cost</th><th>Build time</th></tr></thead><tbody>${recentJobs.length ? recentJobs.map((job) => `<tr><td><strong>${escape(job.projectTitle)}</strong><span class="table-prompt">${escape(job.prompt)}</span>${job.error ? `<small class="error">${escape(job.error)}</small>` : ''}</td><td><span class="pill ${job.status === 'failed' ? 'failed-pill' : ''}">${escape(job.status)}</span></td><td>${number(job.tokens.input)} / ${number(job.tokens.output)}<small>${number(job.tokens.cachedInput)} cached</small></td><td>${cost(job.estimatedCostUsd)}</td><td>${seconds(job.buildSeconds)}</td></tr>`).join('') : '<tr><td colspan="5">No agent iterations have run yet.</td></tr>'}</tbody></table></div><div class="service-resources"><span>Process memory <strong>${formatBytes(service.rssBytes)}</strong></span><span>JS heap <strong>${formatBytes(service.heapUsedBytes)}</strong></span><span>Running <strong>${number(service.activeJobs)}</strong></span><span>Queued <strong>${number(service.queuedJobs)}</strong></span><span>Uptime <strong>${Math.floor(service.uptimeSeconds/60)} min</strong></span></div></div></main></div>`;
}
async function adminProjectModal(id) {
  openModal(`${modalHeader('Project resources','Opening project…')}<p class="help">Reading the project’s current usage and revisions.</p>`);
  const detail = await api(`/api/admin/projects/${encodeURIComponent(id)}`);
  const project = state.admin.projects.find((item) => item.id === id);
  if (!modalRoot.firstElementChild) return;
  openModal(`${modalHeader('Project resources',detail.project.title)}<p>${escape(detail.project.idea)}</p>${adminMetrics(project.summary)}<div class="settings-note">${number(detail.members.length)} members · ${number(detail.revisions.length)} revisions · ${number(detail.assets.length)} assets · ${number(detail.feedback.length)} feedback notes<br>Latest revision: ${escape(detail.revisions.at(-1)?.summary || 'No revision yet')}</div><p class="help" style="margin-top:18px">This owner view is read only. Collaborative editing belongs to project members.</p>`);
}
async function route() {
  const counter = ++routeCounter;
  stopRoom();
  if (!state.user) { renderAuth(); return; }
  if (inviteToken()) { renderInvite(); return; }
  try {
    const projectId = location.hash.match(/^#project\/([^/]+)$/)?.[1];
    if (location.hash === '#admin' && state.user.isAdmin) {
      state.detail = null; state.view = 'admin';
      const data = await api('/api/admin');
      if (counter !== routeCounter) return;
      state.admin = data; renderAdmin();
    } else if (projectId) {
      const detail = await api(`/api/projects/${encodeURIComponent(decodeURIComponent(projectId))}`);
      if (counter !== routeCounter) return;
      state.detail = detail; state.polish = detail.project.polish; state.feedbackMode = false; state.pin = null; state.pinDraft = ''; state.draft = '';
      renderProject();
    } else {
      const data = await api('/api/projects');
      if (counter !== routeCounter) return;
      state.projects = data.projects;
      renderDashboard();
    }
  } catch (error) { if (counter === routeCounter) showError(error.message); }
}

function openModal(html) {
  modalReturnFocus = document.activeElement;
  modalRoot.innerHTML = `<div class="modal-backdrop" data-backdrop="true"><section class="modal" role="dialog" aria-modal="true" aria-labelledby="modal-title" tabindex="-1">${html}</section></div>`;
  document.body.style.overflow = 'hidden';
  const first = modalRoot.querySelector('input:not([type="radio"]),textarea,button');
  (first || modalRoot.querySelector('.modal'))?.focus();
}
function closeModal() {
  modalRoot.innerHTML = ''; document.body.style.overflow = '';
  modalReturnFocus?.focus(); modalReturnFocus = null;
}
function modalHeader(eyebrow,title) { return `<div class="modal-header"><div><p class="eyebrow">${escape(eyebrow)}</p><h2 id="modal-title">${escape(title)}</h2></div><button class="icon-button" data-action="close-modal" aria-label="Close dialog">${icon('close')}</button></div>`; }
function newProjectModal() {
  openModal(`${modalHeader('A little idea starts here','What are we making?')}<p>Give it a name and tell us what you picture. Start small. There’s plenty of room to change your mind.</p><form id="create-form"><div class="field"><label for="project-title">Project name</label><input id="project-title" name="title" placeholder="Something you’d love to play" required maxlength="100"></div><div class="field"><label for="project-idea">The idea</label><textarea id="project-idea" name="idea" placeholder="A cozy little game where you run a night-time flower shop for ghosts…" required minlength="10" maxlength="12000"></textarea></div><div class="field"><label>Where should we focus?</label><div class="polish-options"><label class="polish-option selected"><input type="radio" name="polish" value="polished" checked><span><strong>A polished little game</strong><small>A cohesive look, satisfying feel, and a small complete loop.</small></span></label><label class="polish-option"><input type="radio" name="polish" value="quick"><span><strong>Try the mechanics first</strong><small>Simple presentation. More room to explore the idea quickly.</small></span></label></div></div><p class="error" id="create-error" role="alert"></p><div class="modal-actions"><small>${state.configured.generation ? 'You can invite friends and bring your own assets next.' : 'Start with a playable playground while game creation is unavailable.'}</small><button class="button primary" type="submit">Create project ${icon('arrowRight')}</button></div></form>`);
}
async function shareModal() {
  openModal(`${modalHeader('Better with friends','Make room for everyone.')}<p>Invite your team to make changes, share assets, and playtest. Anyone in the room can take the controller.</p><div id="invite-link-area"><p class="help">Creating a project invitation…</p></div><div class="member-list"><p class="eyebrow">In this project</p>${state.detail.members.map((member) => `<div class="member-row">${avatar(member.name)}<div><strong>${escape(member.name)}</strong><small>${escape(member.email)}</small></div><span class="pill">${member.role === 'owner' ? 'Owner' : 'Teammate'}</span></div>`).join('')}</div>`);
  try {
    const result = await api(projectPath('/invites'), {method:'POST',body:{}});
    const link = result.url || `${location.origin}/invite/${encodeURIComponent(result.token)}`;
    const area = document.querySelector('#invite-link-area');
    if (area) area.innerHTML = `<label class="help" for="invite-link">Project invitation link</label><div class="invite-link" style="margin-top:8px"><input id="invite-link" value="${escape(new URL(link,location.origin).href)}" readonly><button class="button primary small" data-action="copy-invite">${icon('link')}Copy</button></div><p class="help" style="margin:10px 0 0">Anyone with this link can join the project. Only share it with people you want to collaborate with.</p>`;
  } catch (error) { const area = document.querySelector('#invite-link-area'); if (area) area.innerHTML = `<p class="error">${escape(error.message)}</p>`; }
}
function accountModal() {
  const linked = new Set(state.user.authMethods || []);
  openModal(`${modalHeader('Your player card','One you. All your projects.')}<div class="account-identity">${avatar(state.user.name)}<div><strong>${escape(state.user.name)}</strong><p>${escape(state.user.email)}</p></div></div>${state.authProviders.length ? `<h3 class="account-section-title">Ways to sign in</h3><p class="help">Connect an account so you can hop back in with a click.</p><div class="account-connections">${state.authProviders.map((provider) => `<div class="account-connection"><span class="connection-provider">${socialLogo(provider.id)}${escape(provider.label)}</span>${linked.has(provider.id) ? `<span class="pill connected-pill">${icon('check')}Connected</span>` : `<button class="button small ${provider.id === 'google' ? 'google-link' : ''}" data-action="link-provider" data-provider="${escape(provider.id)}">Connect ${escape(provider.label)}</button>`}</div>`).join('')}</div><p class="help" style="margin-top:18px">Connecting adds a sign-in method to this account. It never combines two separate accounts.</p>` : '<p class="help">Your projects stay together in this account. Connecting another sign-in isn’t available right now.</p>'}<p class="error" id="account-error" role="alert"></p>`);
}
function renderInvite() {
  app.innerHTML = `<main class="invite-page">${brand()}<section class="modal"><p class="eyebrow" style="color:var(--lime)">An idea is better with friends</p><h1>There’s a place for you.</h1><p>Join this shared project to playtest, leave notes, add your assets, and make the game together.</p><button class="button primary" data-action="join-invite">Join the project ${icon('arrowRight')}</button><p class="help" style="margin:18px 0 0">Joining as ${escape(state.user.name)}.</p><p class="error" id="invite-error" role="alert"></p></section></main>`;
}
function setupDropZone() {
  const zone = document.querySelector('#upload-zone');
  if (!zone) return;
  for (const type of ['dragenter','dragover']) zone.addEventListener(type, (event) => { event.preventDefault(); zone.classList.add('dragover'); });
  for (const type of ['dragleave','drop']) zone.addEventListener(type, (event) => { event.preventDefault(); zone.classList.remove('dragover'); });
  zone.addEventListener('drop', (event) => uploadFiles(event.dataTransfer.files));
}
async function uploadFiles(files) {
  const selected = Array.from(files || []);
  if (!selected.length) return;
  const usage = state.assetUsage;
  let count = 0;
  for (const file of selected) {
    const status = document.querySelector('#upload-status');
    if (file.size > 10*1024*1024) { toast(`${file.name} is larger than 10 MB.`); continue; }
    if (status) status.textContent = `Uploading ${file.name}…`;
    try {
      const data = await new Promise((resolve,reject) => { const reader = new FileReader(); reader.onload = () => resolve(String(reader.result).split(',')[1]); reader.onerror = () => reject(new Error(`Couldn’t read ${file.name}.`)); reader.readAsDataURL(file); });
      await api(projectPath('/assets'), {method:'POST',body:{name:file.name,type:file.type || 'application/octet-stream',data,usage}});
      count++;
    } catch (error) { toast(error.message); }
  }
  await refreshProject();
  if (count) toast(`${count === 1 ? 'Asset' : `${count} assets`} added to the shared library.`);
}

document.addEventListener('click', async (event) => {
  const button = event.target.closest('[data-action]');
  if (!button) {
    if (event.target.dataset.backdrop) closeModal();
    return;
  }
  event.preventDefault();
  const action = button.dataset.action;
  try {
    if (action === 'dashboard') { if (!state.user) return; if (inviteToken()) history.replaceState(null,'','/'); setRoute(); }
    else if (action === 'admin') { closeModal(); location.hash = '#admin'; }
    else if (action === 'refresh-admin') { button.disabled = true; state.admin = await api('/api/admin'); renderAdmin(); }
    else if (action === 'admin-project') await adminProjectModal(button.dataset.id);
    else if (action === 'project') setRoute(button.dataset.id);
    else if (action === 'menu') { state.menu = !state.menu; document.querySelector('.shell')?.classList.toggle('menu-open',state.menu); }
    else if (action === 'logout') { await api('/api/logout',{method:'POST',body:{}}); state.user = null; state.projects = []; state.detail = null; authError = ''; renderAuth(); }
    else if (action === 'new-project') newProjectModal();
    else if (action === 'close-modal') closeModal();
    else if (action === 'share') await shareModal();
    else if (action === 'account') accountModal();
    else if (action === 'link-provider') {
      button.disabled = true;
      try { const result = await api(`/api/auth/${encodeURIComponent(button.dataset.provider)}/link`,{method:'POST',body:{}}); location.assign(result.url); }
      catch (error) { document.querySelector('#account-error').textContent = error.message; button.disabled = false; }
    }
    else if (action === 'copy-invite') { const input = document.querySelector('#invite-link'); try { await navigator.clipboard.writeText(input.value); toast('Invitation link copied.'); } catch { input.select(); toast('Select and copy this invitation link.'); } }
    else if (action === 'panel') { state.panel = button.dataset.panel; renderPanel(); }
    else if (action === 'take-control') { await room?.takeControl(); }
    else if (action === 'feedback-toggle') { if (!currentRevision() && !state.detail.room?.revisionId) { toast('A playable version is needed before you can pin game feedback.'); return; } state.feedbackMode = !state.feedbackMode; if (state.feedbackMode) state.panel = 'feedback'; else { state.pin = null; state.pinDraft = ''; renderPinEditor(); } syncProject(); }
    else if (action === 'cancel-pin') { state.pin = null; state.pinDraft = ''; renderPinEditor(); renderPins(); }
    else if (action === 'view-pin') { state.panel = 'feedback'; renderPanel(); const item = state.detail.feedback.find((f) => f.id === button.dataset.id); if (item) toast(item.text); }
    else if (action === 'upload') document.querySelector('#asset-files').click();
    else if (action === 'retry-brief') { if (!state.briefReady) { briefDoc?.destroy(); await loadBrief(); } else await flushBrief(); }
    else if (action === 'reload-game') { const frame = document.querySelector('#game-frame'); if (frame.src && !frame.hidden) { frame.src = frame.src; toast('Restarted this playtest.'); } else toast('The playtest host can restart the game.'); }
    else if (action === 'fullscreen') { const stage = document.querySelector('#game-stage'); if (document.fullscreenElement) await document.exitFullscreen(); else if (stage.requestFullscreen) await stage.requestFullscreen(); else toast('Fullscreen is not available in this browser.'); }
    else if (action === 'activate') { button.disabled = true; await api(projectPath(`/revisions/${encodeURIComponent(button.dataset.id)}/activate`),{method:'POST',body:{expectedRevisionId:state.detail.project.activeRevisionId}}); await refreshProject(); toast('The shared version has been updated.'); }
    else if (action === 'play-revision') { button.disabled = true; await room.switchRevision(button.dataset.id); await refreshProject(); toast('The room is now playtesting this version.'); }
    else if (action === 'join-invite') {
      button.disabled = true;
      try { const result = await api(`/api/invites/${encodeURIComponent(inviteToken())}/join`,{method:'POST',body:{}}); const projects = await api('/api/projects'); state.projects = projects.projects; history.replaceState(null,'','/'); setRoute(result.project?.id || result.projectId); }
      catch (error) { document.querySelector('#invite-error').textContent = error.message; button.disabled = false; }
    }
    else if (action === 'retry') await initialize();
  } catch (error) { toast(error.message); button.disabled = false; }
});

document.addEventListener('submit', async (event) => {
  const form = event.target;
  if (!['create-form','chat-form','feedback-form'].includes(form.id)) return;
  event.preventDefault();
  if (busy) return;
  busy = true;
  const button = form.querySelector('[type="submit"]');
  button.disabled = true;
  const data = new FormData(form);
  try {
    if (form.id === 'create-form') {
      document.querySelector('#create-error').textContent = '';
      const result = await api('/api/projects',{method:'POST',body:Object.fromEntries(data)});
      state.projects.unshift(result.project); closeModal(); setRoute(result.project.id);
      if (generationAvailable()) {
        try {
          await api(`/api/projects/${encodeURIComponent(result.project.id)}/jobs`,{method:'POST',body:{prompt:String(data.get('idea')),polish:data.get('polish'),baseRevisionId:result.project.activeRevisionId}});
          toast('Your project is ready. The first iteration is queued.');
        } catch (error) { toast(`Project saved. The first iteration couldn’t start: ${error.message}`); }
      }
    } else if (form.id === 'chat-form') {
      const prompt = String(data.get('prompt')).trim();
      if (!prompt) return;
      if (generationAvailable()) await api(projectPath('/jobs'),{method:'POST',body:{prompt,polish:data.get('polish'),baseRevisionId:state.detail.project.activeRevisionId}});
      else await api(projectPath('/messages'),{method:'POST',body:{text:prompt}});
      state.draft = ''; document.querySelector('#chat-prompt').value = '';
      await refreshProject();
      toast(generationAvailable() ? 'Your iteration is queued.' : 'Message saved. Your team can pick up the conversation anytime.');
    } else {
      if (!state.pin) return;
      let capture = null;
      try { capture = await room?.capture(); } catch { /* A note is still useful when preview capture is unavailable. */ }
      await api(projectPath('/feedback'),{method:'POST',body:{text:String(data.get('text')).trim(),...state.pin,revisionId:state.detail.room?.revisionId || state.detail.project.activeRevisionId,capture}});
      state.pin = null; state.pinDraft = ''; state.feedbackMode = false;
      renderPinEditor(); await refreshProject(); toast('Feedback saved with this version.');
    }
  } catch (error) {
    if (form.id === 'create-form') { const target = document.querySelector('#create-error'); if (target) target.textContent = error.message; }
    else toast(error.message);
  } finally { busy = false; if (button.isConnected) button.disabled = false; }
});
document.addEventListener('input', (event) => {
  if (event.target.id === 'chat-prompt') { state.draft = event.target.value; announceActivity('writing an idea'); }
  if (event.target.id === 'shared-brief') { writeBrief(event.target.value); announceActivity('editing the brief'); }
  if (event.target.id === 'feedback-text') state.pinDraft = event.target.value;
});
document.addEventListener('change', (event) => {
  if (event.target.id === 'iteration-polish') state.polish = event.target.value;
  if (event.target.id === 'asset-usage') state.assetUsage = event.target.value;
  if (event.target.id === 'asset-files') uploadFiles(event.target.files);
  if (event.target.name === 'polish' && event.target.type === 'radio') document.querySelectorAll('.polish-option').forEach((option) => option.classList.toggle('selected',option.querySelector('input').checked));
});
document.addEventListener('click', (event) => {
  if (!state.feedbackMode || event.target.closest('[data-action]') || !event.target.closest('#game-overlay')) return;
  const rect = document.querySelector('#game-overlay').getBoundingClientRect();
  state.pin = room?.point ? room.point(event) : {x:Math.max(0,Math.min(1,(event.clientX-rect.left)/rect.width)),y:Math.max(0,Math.min(1,(event.clientY-rect.top)/rect.height))};
  if (!state.pin) return;
  renderPins(); renderPinEditor();
});
document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') { closeModal(); if (state.pin) { state.pin = null; state.pinDraft = ''; renderPinEditor(); renderPins(); } else if (state.feedbackMode) { state.feedbackMode = false; syncProject(); } if (state.menu) { state.menu = false; document.querySelector('.shell')?.classList.remove('menu-open'); } }
  if ((event.ctrlKey || event.metaKey) && event.key === 'Enter' && event.target.id === 'chat-prompt') document.querySelector('#chat-form').requestSubmit();
  if (event.key === 'Tab' && modalRoot.firstElementChild) {
    const elements = [...modalRoot.querySelectorAll('button:not(:disabled),a[href],input:not([type="hidden"]),textarea,select,[tabindex="0"]')].filter((element) => element.offsetParent);
    if (!elements.length) return;
    if (event.shiftKey && document.activeElement === elements[0]) { event.preventDefault(); elements.at(-1).focus(); }
    else if (!event.shiftKey && document.activeElement === elements.at(-1)) { event.preventDefault(); elements[0].focus(); }
  }
});
window.addEventListener('hashchange',route);
window.addEventListener('resize',renderPins);
window.addEventListener('beforeunload',stopRoom);
function applySession(session) {
  state.user = session.user;
  state.configured = session.configured || {};
  state.authProviders = (session.authProviders || []).filter((provider) => ['google','discord'].includes(provider.id));
}
function readAuthCallback() {
  const url = new URL(location.href);
  const code = url.searchParams.get('authError');
  const linked = url.searchParams.get('authLinked');
  const signedIn = url.searchParams.get('auth');
  const messages = {
    cancelled:'Sign-in was cancelled. You can try again whenever you’re ready.',
    state:'That sign-in request expired. Please start again.',
    provider:'We couldn’t finish signing in. Please try again.',
    unverified:'Please verify your email with that account first, then try again.',
    'account-exists':'An account already uses this email. Sign in the way you originally joined, then connect this sign-in method from your account.',
    'identity-linked':'That sign-in is already connected to another account. Use it to sign into that account instead.',
    'link-expired':'Your connection request expired. Open your account and try connecting again.',
  };
  if (code) authError = messages[code] || 'Sign-in couldn’t finish. Please try again.';
  if (code || linked || signedIn) { url.searchParams.delete('authError'); url.searchParams.delete('authLinked'); url.searchParams.delete('auth'); history.replaceState(null,'',`${url.pathname}${url.search}${url.hash}`); }
  return ['google','discord'].includes(linked) ? linked : null;
}
async function initialize() {
  try {
    const connectedProvider = readAuthCallback();
    const session = await api('/api/session'); applySession(session);
    if (state.user) { const data = await api('/api/projects'); state.projects = data.projects; }
    await route();
    if (state.user && authError) { accountModal(); const error = document.querySelector('#account-error'); if (error) error.textContent = authError; authError = ''; }
    if (state.user && connectedProvider) toast(`${connectedProvider === 'google' ? 'Google' : 'Discord'} connected. You’re ready for the next round.`);
  } catch (error) { showError(error.message); }
}
initialize();
