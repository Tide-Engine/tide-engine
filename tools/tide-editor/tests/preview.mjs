// Installed CLI integration: the installation needs its native toolchain and
// platform library, just as tide run does. All files remain in a private workdir.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { cpSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { readPng } from '../../../platform/tests/png_read.mjs';
import { createServer } from 'node:net';

const [cliArg, fixtureArg, workArg] = process.argv.slice(2);
const cli = resolve(cliArg), work = resolve(workArg);
assert.ok(work.endsWith('editor-preview-test'));
rmSync(work, {recursive:true, force:true}); mkdirSync(work, {recursive:true});
const project = join(work, 'project'); cpSync(resolve(fixtureArg), project, {recursive:true});
const prefab = join(project, 'Playground.tprefab'), game = join(project, 'game.tide');
const original = readFileSync(prefab, 'utf8');
writeFileSync(game, readFileSync(game, 'utf8') + `
local singleton PreviewLook { float strength = .5; }
event(Spawned) Setup(with Main) { Spawn(Playground); Debug.Assert(false, "game startup ran in authoring"); }
event(Spawned) SpawnModel(with Model) { Debug.Assert(false, "Spawned ran in authoring"); }
system Simulate(mut Health health) { health.value = 0; Debug.Assert(false, "simulation ran in authoring"); }
frag PreviewShade(Vertex3 vertex, out Color color, PreviewLook look) { color = Color(0, look.strength, 0); }
view RuntimeOnly() { Draw.Clear(Color.red); Debug.Assert(false, "runtime-only view ran in authoring"); }
[RunInEditor] view Settings(LevelSettings settings, mut PreviewLook look) { look.strength = settings.enemies / 10.0; }
[RunInEditor] view LocalSettings(local LevelSettings settings, mut PreviewLook look) { look.strength = settings.enemies / 10.0; }
[RunInEditor] view Render(Model model, World world, PreviewLook look) { Draw.Mesh(model.model.Value, world.LocalToWorld(this), PreviewShade); }
[RunInEditor] view LocalRender(local Model model, World world, PreviewLook look) { Draw.Mesh(model.model.Value, world.LocalToWorld(this), PreviewShade); }
[EditorOnly] view Authoring() { Draw.Circle(float2(0, -2), .4, Color.yellow); }
`);
const run = (...args) => {
    const r = spawnSync(cli, ['open', prefab, ...args], {encoding:'utf8', timeout:180000,
        env:{...process.env, TIDE_HIDDEN:'1'}});
    assert.ifError(r.error); return r;
};
const image = name => readPng(readFileSync(join(work, name)));
const count = (png, test) => {
    let n = 0;
    for (let y = 140; y < png.height - 230; y++) for (let x = 270; x < png.width - 405; x++) if (test(png.at(x,y))) n++;
    return n;
};
const green = p => p[0] < 8 && p[1] > 100 && p[2] < 8;
let r = run('--preview', '--capture', join(work, 'shared.png'));
assert.equal(r.status, 0, r.stderr + r.stdout);
assert.ok(count(image('shared.png'), green) > 100, 'game fragment shader renders in the native viewport');
assert.ok(count(image('shared.png'), p => p[0] > 240 && p[1] > 225 && p[2] < 8) > 50, 'EditorOnly view is included');
r = run('--local-preview', '--capture', join(work, 'invalid-local.png'));
assert.equal(r.status, 1); assert.match(r.stderr, /Entity, not LocalEntity/, 'placement still checks typed entity references');
writeFileSync(prefab, original.replace('        Target { entity = Crates.Left };\n', ''));
r = run('--local-preview', '--capture', join(work, 'local.png'), '--size', '1100', '650');
assert.equal(r.status, 0, r.stderr + r.stdout);
assert.ok(count(image('local.png'), green) > 20, 'local placement uses the same component and shader');
writeFileSync(prefab, original);
const script = join(work, 'edit.txt');
writeFileSync(script, [
    '2 mouse 1264 339', '3 button left',
    '8 key leftCtrl down', '9 key a', '10 key leftCtrl up', '12 text 9', '13 key enter',
    '16 capture draft.png', '18 mouse 338 38', '19 button left', '22 capture undone.png',
    '24 mouse 410 38', '25 button left', '28 capture redone.png',
    '30 mouse 80 798', '31 button left', // Project > Prefabs.
    '34 mouse 505 800', '35 button left', '38 capture nested-root.png', // Crate, the second declaration.
    '40 mouse 986 96', '41 button left', '44 capture disabled.png',
    '46 mouse 986 96', '47 button left', '50 capture reenabled.png', '51 quit',
].join('\n'));
r = run('--preview', '--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
assert.equal(readFileSync(prefab, 'utf8'), original, 'preview builds from the unsaved draft without writing the source');
assert.ok(count(image('draft.png'), p => green(p) && p[1] > 220) > 100, 'applied override reaches the actual shader');
assert.equal(count(image('undone.png'), p => green(p) && p[1] > 220), 0, 'undo rebuilds authored state');
assert.ok(count(image('redone.png'), p => green(p) && p[1] > 220) > 100, 'redo rebuilds authored state');
assert.ok(count(image('nested-root.png'), green) > 50, 'switching the selected prefab rebuilds its root');
assert.equal(count(image('nested-root.png'), p => green(p) && p[1] > 220), 0,
    'Project dock selects Crate rather than leaving the edited Playground preview');
assert.equal(count(image('disabled.png'), green), 0, 'disabling releases the module and restores fixed preview');
assert.ok(count(image('reenabled.png'), green) > 50, 'preview can be enabled again');
// Real DLL/CRT boundary in an embedded process: ordinary project printf must
// stay on ordinary stdio, never corrupt the binary PNG channel.
rmSync(join(project,'.tide','editor'),{recursive:true,force:true});
writeFileSync(join(project,'preview_log.c'),'#include <stdio.h>\nvoid PreviewStdout(void) { printf("preview stdout\\n"); fflush(stdout); }\n');
writeFileSync(game,readFileSync(game,'utf8')+'\nextern void PreviewStdout();\n[EditorOnly] view PipeLog() { PreviewStdout(); }\n');
r=run('--capture',join(work,'inspector-without-views.png'),'--size','1100','650');
assert.equal(r.status,0,r.stderr+r.stdout);
const inspectorWithoutViews=image('inspector-without-views.png');
const token='0123456789abcdef0123456789abcdef';
const server=createServer(socket=>{
    let hello=false;
    socket.on('data',data=>{
        bytes=Buffer.concat([bytes,data]);
        if(!hello){const end=bytes.indexOf(10);if(end<0)return;assert.equal(bytes.subarray(0,end).toString(),`TIDEHELLO 1 ${token}`);bytes=bytes.subarray(end+1);hello=true;server.close();}
        receive();
    });
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const child=spawn(join(dirname(cli),process.platform==='win32'?'tide-editor.exe':'tide-editor'),[prefab,'--project',project,'--embed',String(server.address().port),token,'--preview','--size','1100','650'],
    {windowsHide:true,env:{...process.env,TIDE_HIDDEN:'1'}});
let bytes=Buffer.alloc(0), waiting, said='', lastPng;
const ended=new Promise(resolve=>child.once('close',resolve));
child.stderr.on('data',data=>{said+=data;});
child.stdout.on('data',data=>{said+=data;});
function receive(){
    const end=bytes.indexOf(10);if(end<0)return;
    const header=/^TIDEFRAME 1 (\d+) [01] \d+ (\d+) \d+$/.exec(bytes.subarray(0,end).toString());
    if(!header){waiting?.reject(new Error('invalid binary header: '+bytes.subarray(0,end)));return;}
    assert.equal(+header[2],0,'a missing extern is a preview link failure, not a front-end diagnostic');
    const length=+header[1];if(bytes.length<end+1+length)return;
    const png=bytes.subarray(end+1,end+1+length);bytes=bytes.subarray(end+1+length);
    lastPng=png;
    const pending=waiting;waiting=null;pending?.resolve(readPng(png));
}
child.on('error',error=>waiting?.reject(error));
child.on('close',code=>waiting?.reject(new Error(`embedded preview stopped (${code}): ${said}`)));
const deadline=setTimeout(()=>{child.stdin.end();waiting?.reject(new Error('embedded shader preview timed out'));},120000);
const frame=(...events)=>{
    assert.ok(!waiting,'request one embedded frame at a time');
    const next=new Promise((resolve,reject)=>{waiting={resolve,reject};});
    child.stdin.write(events.map(event=>`INPUT ${event}\n`).join('')+'FRAME 1\n');
    return next;
};
const pixels=(png,left,top,right,bottom,test)=>{
    let n=0;
    for(let y=top;y<bottom;y++) for(let x=left;x<right;x++) if(test(png.at(x,y))) n++;
    return n;
};
const failureRed=p=>p[0]>180 && p[0]>p[1]+60 && p[1]>75 && p[1]<180 && p[2]>50 && p[2]<160;
const broken='\nextern void MissingPreviewFunction();\n[EditorOnly] view Broken() { MissingPreviewFunction(); }\n';
try {
    let rendered=false;
    for(let i=0;i<200&&!rendered;i++) {
        rendered=count(await frame(),green)>100;
        if(!rendered)await new Promise(resolve=>setTimeout(resolve,100));
    }
    assert.ok(rendered,'embedded native renderer must execute the game fragment shader');
    // Keep this process unbounded: bounded capture/script runs exit before drawing
    // a failed preview. Compare the badge with Console already selected in both.
    await frame('mouse 142 493','button left');await frame();
    const cleanConsole=await frame('mouse 1050 600');
    await frame('mouse 46 493','button left');await frame();
    await frame('mouse 1050 600');
    writeFileSync(game,readFileSync(game,'utf8')+broken);
    let failedFrame;
    for(let i=0;i<400;i++) {
        const current=await frame();
        if(pixels(current,10,632,840,648,failureRed)>20) { failedFrame=current;break; }
        await new Promise(resolve=>setTimeout(resolve,100));
    }
    assert.ok(failedFrame,'a failed preview must show a red status message at the minimum window size');
    writeFileSync(join(work,'embedded-preview-failed.png'),lastPng);
    let inspectorChanges=0;
    for(let y=56;y<628;y++) for(let x=710;x<1100;x++)
        if(failedFrame.at(x,y).some((value,i)=>value!==inspectorWithoutViews.at(x,y)[i])) inspectorChanges++;
    assert.equal(inspectorChanges,0,'compiled preview clip state must keep authored labels and wires out of the unchanged Inspector');
    assert.ok(pixels(failedFrame,10,514,700,624,failureRed)>20,
        'a new preview failure automatically opens Console with the build message');
    // Probe the neutral tab background above its caption, away from hover/focus.
    const activeTab=(png,x)=>{const p=png.at(x,482);return p[0]>55 && p[0]<75 && p[0]===p[1] && p[1]===p[2];};
    assert.ok(activeTab(failedFrame,142),'Console must be selected on the first failure edge');
    let badgeChanges=0;
    const text=p=>p.slice(0,3).every(v=>v>180);
    for(let y=486;y<501;y++) for(let x=100;x<184;x++)
        if(text(cleanConsole.at(x,y))!==text(failedFrame.at(x,y))) badgeChanges++;
    assert.ok(badgeChanges>5,'Console caption gains a visible failure badge');
    assert.ok(count(failedFrame,green)>100,'failed rebuilding retains the previous compiled geometry');
    await frame('mouse 46 493','button left');await frame();
    const projectFrame=await frame('mouse 1050 600');
    assert.ok(activeTab(projectFrame,46),'the same failure must not force Console open again after choosing Project');
    assert.ok(pixels(projectFrame,10,632,840,648,failureRed)>20,'the failure stays visible while browsing Project');
    assert.equal(pixels(projectFrame,10,514,700,624,failureRed),0,'Project remains browsable instead of showing Console rows');
    await frame('mouse 142 493','button left');await frame();
    assert.ok(pixels(await frame('mouse 1050 600'),10,514,700,624,failureRed)>20,
        'Console can be revisited to inspect the retained failure message');
} finally {
    clearTimeout(deadline);child.stdin.end();assert.equal(await ended,0,said);
}
// close is after both pipes drained; stderr delivery can lag the PNG stream.
assert.match(said,/preview stdout/,'project stdout is preserved outside binary frames');
r = run('--preview', '--capture', join(work, 'broken.png')); assert.equal(r.status, 1);
assert.match(r.stderr, /MissingPreviewFunction/);
console.log('ok: installed native shader preview, isolated startup, local placement, draft/undo/redo, selection, toggle, minimum-size failure UI and failed-build diagnostics');
