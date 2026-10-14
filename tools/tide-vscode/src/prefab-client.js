// A thin IDE canvas. All authoring, widgets and rendering live in tide-editor.
// The VS Code adapter supplies a private send() bridge; no HTTP service opens.
function editorPage(bridge, nonce) {
    return `<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'nonce-${nonce}'; script-src 'nonce-${nonce}'">
<style nonce="${nonce}">html,body{margin:0;height:100%;background:#131a20;color:#dbe3ed;font:14px system-ui;overflow:auto}#status{padding:10px}img{display:block;width:100%;max-width:1440px;height:auto;outline:none;touch-action:none;user-select:none}#status:empty{display:none}</style></head>
<body><div id="status">Starting native prefab editor…</div><img id="editor" alt="Tide native prefab editor" tabindex="0">
<script nonce="${nonce}">
${bridge}
const image=document.getElementById('editor'), status=document.getElementById('status');
let events=[], waiting=false, timer, closed=false, presented=false;
const held=new Set();
function event(value){if(value.startsWith('mouse ')&&events.at(-1)?.startsWith('mouse '))events[events.length-1]=value;else if(events.length<200)events.push(value);}
function request(){if(closed||waiting||document.hidden)return;waiting=true;let count=events.findIndex(e=>/^(button|key) \\S+ (down|up)$/.test(e));count=count<0?events.length:count+1;const input=events.splice(0,count);send({type:'frame',events:input});}
window.tideFrame=function(frame){if(closed)return;image.onload=()=>{waiting=false;status.textContent='';if(!presented){presented=true;send({type:'presented'});}timer=setTimeout(request,50);};image.src='data:image/png;base64,'+frame.png;};
window.tideError=function(message){closed=true;clearTimeout(timer);status.textContent=message;};
function mouse(e){const r=image.getBoundingClientRect();event('mouse '+((e.clientX-r.left)*1440/r.width).toFixed(3)+' '+((e.clientY-r.top)*900/r.height).toFixed(3));}
const buttons=['left','middle','right'];
image.addEventListener('pointerdown',e=>{if(e.button>2)return;e.preventDefault();image.focus();image.setPointerCapture(e.pointerId);mouse(e);event('button '+buttons[e.button]+' down');held.add('button '+buttons[e.button]);});
image.addEventListener('pointermove',e=>{mouse(e);});
image.addEventListener('pointerup',e=>{if(e.button>2)return;mouse(e);event('button '+buttons[e.button]+' up');held.delete('button '+buttons[e.button]);});
image.addEventListener('contextmenu',e=>e.preventDefault());
image.addEventListener('wheel',e=>{e.preventDefault();mouse(e);event('scroll 0 '+Math.max(-5,Math.min(5,-e.deltaY/100)).toFixed(3));},{passive:false});
const keys={ControlLeft:'leftCtrl',ControlRight:'rightCtrl',ShiftLeft:'leftShift',ShiftRight:'rightShift',AltLeft:'leftAlt',AltRight:'rightAlt',Enter:'enter',NumpadEnter:'enter',Escape:'escape',Backspace:'backspace',Delete:'delete',Tab:'tab',ArrowLeft:'leftArrow',ArrowRight:'rightArrow',ArrowUp:'upArrow',ArrowDown:'downArrow',Space:'space'};
function key(e){return keys[e.code]||(/^Key[A-Z]$/.test(e.code)?e.code.slice(3).toLowerCase():/^Digit[0-9]$/.test(e.code)?'digit'+e.code.slice(5):null);}
image.addEventListener('keydown',e=>{if(e.metaKey)return;e.preventDefault();const k=key(e);if(k&&!e.repeat){event('key '+k+' down');held.add('key '+k);}if(!e.ctrlKey&&!e.altKey&&e.key.length===1&&e.key!=='\\n')event('text '+e.key);});
image.addEventListener('keyup',e=>{const k=key(e);if(k){e.preventDefault();event('key '+k+' up');held.delete('key '+k);}});
function release(){for(const k of held)event(k+' up');held.clear();}
image.addEventListener('blur',release);image.addEventListener('pointercancel',release);
document.addEventListener('visibilitychange',()=>{if(document.hidden)release();else request();});
window.tideAction=function(action){release();const key=action==='save'?'s':action==='undo'?'z':action==='redo'?'y':null;if(key){event('key leftCtrl');event('key '+key);request();}};
window.addEventListener('message',e=>{if(e.data?.type==='frame')window.tideFrame(e.data);if(e.data?.type==='error')window.tideError(e.data.message);if(e.data?.type==='action')window.tideAction(e.data.action);});
request();
</script></body></html>`;
}
module.exports = { editorPage };
