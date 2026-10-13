// A real page, keyboard/pointer input and two WebRTC peers. Read only the
// public netcode report and rendered labels/minimap; no game test exports.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {existsSync} from 'node:fs';
import {mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import os from 'node:os';
import path from 'node:path';
import {createRelay} from '../../../relay/relay.mjs';
import {stopBrowser} from '../../inside-out/test/browser-process.mjs';
import {connectDevTools} from '../../inside-out/test/browser-devtools.mjs';
import {installFrameControl} from '../../inside-out/test/frame-control.mjs';

const [pageFile, outputDirectory, backend='webgl'] = process.argv.slice(2);
assert.ok(pageFile && outputDirectory);
assert.ok(['webgl','webgpu'].includes(backend));
const chrome = [process.env.TIDE_BROWSER, 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    '/usr/bin/google-chrome', '/usr/bin/chromium', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome']
    .find(p=>p && existsSync(p));
if (!chrome) {console.log('SKIPPED: no Chrome found'); process.exit(0);}
await mkdir(outputDirectory,{recursive:true});
const profile=await mkdtemp(path.join(os.tmpdir(),'tide-hot-swap-'));
const game=await readFile(pageFile);
const sockets=new Set();
const track=s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));};
const relay=createRelay({iceServers:[]}); relay.on('connection',track);
await new Promise(resolve=>relay.listen(0,'127.0.0.1',resolve));
const server=createServer((req,res)=>{
    res.setHeader('Cross-Origin-Opener-Policy','same-origin');
    res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
    res.setHeader('Cache-Control','no-store');
    if (new URL(req.url,'http://localhost').pathname==='/prototypes/hot-swap/') res.writeHead(200,{'content-type':'text/html'}).end(game);
    else res.writeHead(204).end();
}); server.on('connection',track);
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin=`http://127.0.0.1:${server.address().port}`;
const relayURL=`ws://127.0.0.1:${relay.address().port}`;
const gpu=backend==='webgl' ? ['--use-angle=swiftshader','--enable-unsafe-swiftshader'] :
    ['--enable-unsafe-webgpu','--use-webgpu-adapter=swiftshader',...(process.platform==='win32'?['--use-angle=d3d11-warp']:[])];
const browser=spawn(chrome,['--headless','--no-sandbox','--no-first-run','--no-default-browser-check','--disable-extensions',
    '--mute-audio','--disable-background-timer-throttling','--disable-renderer-backgrounding','--disable-backgrounding-occluded-windows',
    '--allow-loopback-in-peer-connection','--disable-features=WebRtcHideLocalIpsWithMdns',
    '--remote-debugging-pipe',`--user-data-dir=${profile}`,...gpu,'about:blank'],
    {windowsHide:true,stdio:['ignore','ignore','pipe','pipe','pipe']});
const devtools=connectDevTools(browser);
const clients=[],errors=[];
let stderr='',stage='startup',ending=false;
browser.stderr.on('data',bytes=>stderr=(stderr+bytes).slice(-8000));
devtools.onDisconnect(e=>{if(!ending)errors.push(e.message);});
const pause=()=>new Promise(resolve=>setTimeout(resolve,0));
async function steps(count=1) {
    for(let i=0;i<count;i++) {
        const results=await Promise.all(clients.map(c=>c.evaluate('globalThis.__lastLightStep?.() ?? false',true)));
        assert.ok(results.every(r=>typeof r==='boolean'));
        await pause();
        assert.deepEqual(errors,[],`${stage}: browser/runtime/rendering errors`);
    }
}
async function until(predicate,label,timeout=45000) {
    const end=performance.now()+timeout;
    while(performance.now()<end) {if(await predicate())return;await steps();}
    throw new Error(`${stage}: timed out ${label}`);
}
async function client(name) {
    const target=await devtools.command('Target.createTarget',{url:'about:blank',newWindow:clients.length>0});
    const {sessionId}=await devtools.command('Target.attachToTarget',{targetId:target.targetId,flatten:true});
    const command=(method,params={})=>devtools.command(method,params,{sessionId,timeout:20000});
    devtools.onEvent(sessionId,event=>{
        if(event.method==='Runtime.exceptionThrown')errors.push(`${name}: ${event.params.exceptionDetails.exception?.description??event.params.exceptionDetails.text}`);
        if(event.method==='Runtime.consoleAPICalled' && event.params.type==='error')errors.push(`${name}: ${event.params.args.map(a=>a.value??a.description).join(' ')}`);
        if(event.method==='Inspector.targetCrashed')errors.push(`${name}: renderer crashed`);
    });
    await command('Runtime.enable'); await command('Page.enable');
    await command('Emulation.setDeviceMetricsOverride',{width:1280,height:800,deviceScaleFactor:1,mobile:false});
    await command('Page.addScriptToEvaluateOnNewDocument',{source:`
        (${installFrameControl.toString()})(window);
        window.__report={frames:0};window.__peers=[];window.__channels=[];
        const Peer=RTCPeerConnection;
        window.RTCPeerConnection=class extends Peer {
            constructor(...args){super(...args);window.__peers.push(this);this.addEventListener('datachannel',e=>window.__channels.push(e.channel));}
            createDataChannel(...args){const channel=super.createDataChannel(...args);window.__channels.push(channel);return channel;}
        };
        let config;
        Object.defineProperty(window,'Tide',{configurable:true,get:()=>config,set:value=>{
            config=value;value.checkGL=true;value.backend=${JSON.stringify(backend)};value.relay=${JSON.stringify(relayURL)};
            value.netcode=data=>{window.__report={frames:window.__report.frames+1,state:data[0],verified:data[4],room:value.room,peers:window.__peers.map(p=>p.connectionState)};};
        }});
    `});
    const c={name,command,
        async evaluate(expression,awaitPromise=false) {
            const result=await command('Runtime.evaluate',{expression,awaitPromise,returnByValue:true});
            assert.equal(result.exceptionDetails,undefined,JSON.stringify(result.exceptionDetails)); return result.result.value;
        },
        async key(code,key,down) {await command('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',code,key,windowsVirtualKeyCode:key.length===1?key.toUpperCase().charCodeAt(0):code==='Tab'?9:0});},
        async tap(code,key) {await this.key(code,key,true);await steps(3);await this.key(code,key,false);await steps(3);},
        async click(x,y) {await command('Input.dispatchMouseEvent',{type:'mousePressed',x,y,button:'left',clickCount:1});await steps(3);await command('Input.dispatchMouseEvent',{type:'mouseReleased',x,y,button:'left',clickCount:1});await steps(3);},
        async crop(clip) {const r=await command('Page.captureScreenshot',{format:'png',captureBeyondViewport:false,clip:{...clip,scale:1}});return Buffer.from(r.data,'base64');},
        async capture(label) {const r=await command('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});await writeFile(path.join(outputDirectory,`${backend}-${label}.png`),Buffer.from(r.data,'base64'));},
        async navigate(query='') {await command('Page.navigate',{url:`${origin}/prototypes/hot-swap/?backend=${backend}${query}`});await until(async()=>((await this.evaluate('window.__report'))?.frames??0)>5,`${name} draws the game`);}
    };
    clients.push(c);return c;
}
const hud={x:300,y:725,width:640,height:65};
const map={x:1090,y:169,width:47,height:31};
const different=(a,b,label)=>assert.notEqual(Buffer.compare(a,b),0,label);
try {
    const host=await client('Mint');
    stage='solo controls';await host.navigate();await host.capture('title');
    await host.click(450,488);await steps(12);
    const before=await host.crop(map);
    await host.key('KeyW','w',true);await steps(25);await host.key('KeyW','w',false);await steps(4);
    different(await host.crop(map),before,'sampled keyboard moves the creature on the minimap');
    const body=await host.crop(hud);await host.tap('Tab','Tab');await host.tap('KeyQ','q');await steps(6);
    different(await host.crop(hud),body,'Tab/Q changes the possessed body label');
    await host.tap('KeyH','h');await host.capture('help');await host.tap('KeyH','h');
    await host.tap('KeyM','m');await host.capture('map');await host.tap('KeyM','m');
    await host.tap('Escape','Escape');await steps(6);await host.click(640,450);await steps(12);
    assert.equal(Buffer.compare(await host.crop(hud),body),0,'room New game restores the starting body');
    stage='cooperative WebRTC';await host.navigate('&host');
    await until(async()=>/^[A-Z0-9]{6}$/.test((await host.evaluate('window.__report'))?.room??''),'host room');
    const room=(await host.evaluate('window.__report')).room;
    const friend=await client('Coral');await friend.navigate(`&join=${room}`);
    await until(async()=>{
        const a=await host.evaluate('window.__report'),b=await friend.evaluate('window.__report');
        return a.state===2&&b.state===2&&a.peers.includes('connected')&&b.peers.includes('connected')&&b.verified>15;
    },'two connected peers');await steps(20);
    const a0=await host.crop(map),b0=await friend.crop(map);
    await host.key('KeyW','w',true);await steps(25);await host.key('KeyW','w',false);await steps(15);
    different(await host.crop(map),a0,'host moves its own body');
    different(await friend.crop(map),b0,'friend sees the host move');
    const a1=await host.crop(map),b1=await friend.crop(map);
    await friend.key('KeyS','s',true);await steps(20);await friend.key('KeyS','s',false);await steps(15);
    different(await host.crop(map),a1,'host sees friend move');different(await friend.crop(map),b1,'friend moves its own body');
    await host.capture('cooperative-mint');await friend.capture('cooperative-coral');
    // Real command jump, with the partner staying in its body.
    const hostBody=await host.crop(hud),friendBody=await friend.crop(hud);
    await host.tap('Tab','Tab');await host.tap('KeyQ','q');await steps(15);
    different(await host.crop(hud),hostBody,'host possesses another machine in the shared match');
    assert.equal(Buffer.compare(await friend.crop(hud),friendBody),0,'partner keeps its body');
    // Clients use unordered, unreliable RTC channels, as the game does in play.
    const channels=await host.evaluate('window.__channels.map(c=>({ready:c.readyState,ordered:c.ordered,retries:c.maxRetransmits}))');
    assert.ok(channels.some(c=>c.ready==='open' && !c.ordered && c.retries===0),'match uses unordered unreliable RTC');
    assert.deepEqual(errors,[]);
    console.log(`HOT SWAP ${backend}: solo movement/possession/help/map and two-player WebRTC passed`);
} catch(error) {
    console.error(`HOT SWAP failed during ${stage}: ${error.stack}\n${stderr}`);process.exitCode=1;
} finally {
    ending=true;
    const stopped=await stopBrowser(browser,()=>devtools.command('Browser.close'));
    devtools.dispose();if(stopped)await rm(profile,{recursive:true,force:true});
    for(const socket of sockets)socket.destroy();
    await Promise.all([new Promise(r=>server.close(r)),new Promise(r=>relay.close(r))]);
}
