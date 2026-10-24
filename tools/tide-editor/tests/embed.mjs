import assert from 'node:assert/strict';
import { spawn,spawnSync } from 'node:child_process';
import { mkdirSync,readFileSync,writeFileSync,rmSync } from 'node:fs';
import { join,resolve } from 'node:path';
import { readPng } from '../../../platform/tests/png_read.mjs';
import { createServer } from 'node:net';

const [exeArg,workArg]=process.argv.slice(2), exe=resolve(exeArg),work=resolve(workArg);
assert.ok(work.endsWith('editor-embed'));
rmSync(work,{recursive:true,force:true});mkdirSync(work,{recursive:true});
writeFileSync(join(work,'tide.packages'),'');
writeFileSync(join(work,'triangle.obj'),'v 0 0 0\nv 4 0 0\nv 0 4 0\nf 1 2 3\n');
writeFileSync(join(work,'game.tide'),'scene Main {}\nasset Mesh<Vertex3> triangle = "triangle.obj";\ncomponent Model { Asset<Mesh<Vertex3>> model; }\n');
const file=join(work,'Level.tprefab');
const source='// Retain this comment.\nprefab Level {\n    Transform;\n    entity Far { Transform { localPosition = float3(0, 0, 1) }; Model { model = triangle }; }\n    entity Near { Transform { localPosition = float3(0, 0, -1) }; Model { model = triangle }; }\n}\n';
// Initially isolate Near so a mirrored camera error cannot swap identical meshes
// and accidentally pass the alignment pixel check. Then restore the overlap.
writeFileSync(file,source.replace('entity Far { Transform { localPosition = float3(0, 0, 1) }; Model { model = triangle }; }','entity Far { Transform { localPosition = float3(0, 0, 1) }; Model { model = default }; }'));
async function backend() {
    const token='0123456789abcdef0123456789abcdef';
    const server=createServer(socket=>{
        let hello=false;
        socket.on('data',data=>{
            bytes=Buffer.concat([bytes,data]);
            if(!hello){const end=bytes.indexOf(10);if(end<0)return;assert.equal(bytes.subarray(0,end).toString(),`TIDEHELLO 1 ${token}`);bytes=bytes.subarray(end+1);hello=true;server.close();}
            settle();
        });
    });
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    const child=spawn(exe,[file,'--project',work,'--embed',String(server.address().port),token],{windowsHide:true,env:{...process.env,TIDE_HIDDEN:'1'}});
    const ended=new Promise(resolve=>child.once('close',resolve));
    let bytes=Buffer.alloc(0), waiter, said='';
    child.stderr.on('data',data=>{said+=data;});
    child.on('error',error=>waiter?.reject(error));
    child.on('close',code=>waiter?.reject(new Error(`backend stopped (${code}): ${said}`)));
    function settle(){
        if(!waiter)return;
        const end=bytes.indexOf(10);if(end<0)return;
        const header=/^TIDEFRAME 1 (\d+) ([01]) (\d+) (\d+) (\d+)$/.exec(bytes.subarray(0,end).toString());assert.ok(header,bytes.subarray(0,end).toString());
        const size=+header[1];if(bytes.length<end+1+size)return;
        const png=bytes.subarray(end+1,end+1+size);bytes=bytes.subarray(end+1+size);
        const pending=waiter;waiter=null;pending.resolve({png,dirty:header[2]==='1',revision:+header[3],errors:+header[4]});
    }
    return {child,frame:async(...events)=>{
        const result=new Promise((resolve,reject)=>{assert.equal(waiter,undefined);waiter={resolve,reject};});
        child.stdin.write(events.map(event=>'INPUT '+event+'\n').join('')+'FRAME 1\n');
        const frame=await result;waiter=undefined;assert.equal(frame.errors,0,said);assert.equal(readPng(frame.png).width,1440);return frame;
    },close:async()=>{
        child.stdin.end();assert.equal(await ended,0,said);
    }};
}
const timeout=setTimeout(()=>{throw new Error('native embedding test timed out');},90000);
const app=await backend();
try {
    const initial=await app.frame();assert.equal(initial.dirty,false);
    writeFileSync(join(work,'initial.png'),initial.png);
    assert.ok(readPng(initial.png).at(450,365).slice(0,3).every(v=>v>60),'isometric source meshes align with markers/picking');
    writeFileSync(file,source);await new Promise(resolve=>setTimeout(resolve,550));await app.frame();
    // XY view, then pick a surface away from both origins. Near wins over Far.
    await app.frame('mouse 294 96','button left');await app.frame();
    await app.frame('mouse 600 300','button left');await app.frame();
    await app.frame('key w');await app.frame();
    await app.frame('mouse 715 391','button left down');
    await app.frame('mouse 811 391');
    await app.frame('button left up');
    const moved=await app.frame();assert.equal(moved.dirty,true);assert.ok(moved.revision>initial.revision);
    assert.equal(readFileSync(file,'utf8'),source,'drag stays unsaved');
    const draft=readFileSync(join(work,'.tide','editor', (await import('node:fs')).readdirSync(join(work,'.tide','editor'))[0]),'utf8');
    assert.match(draft,/entity Near \{ Transform \{ localPosition = float3\(2, 0, -1\)/);
    assert.match(draft,/entity Far \{ Transform \{ localPosition = float3\(0, 0, 1\)/);
    writeFileSync(join(work,'moved.png'),moved.png);
    // One drag is one undo. Redo reinstates it; Save uses the same transaction.
    await app.frame('key leftCtrl','key z');const undone=await app.frame();assert.equal(undone.dirty,false);
    await app.frame('key leftCtrl','key y');assert.equal((await app.frame()).dirty,true);
    await app.frame('key leftCtrl','key s');assert.equal((await app.frame()).dirty,false);
    const saved=readFileSync(file,'utf8');assert.match(saved,/Retain this comment/);assert.match(saved,/entity Near \{ Transform \{ localPosition = float3\(2, 0, -1\)/);
    // Rotation around local Z in XY (X/Y rotation rings are edge-on).
    await app.frame('key e');await app.frame();
    await app.frame('mouse 810 391','button left down');await app.frame('button left up');
    assert.equal((await app.frame()).dirty,false,'clicking a ring without a drag preserves the source');
    await app.frame('mouse 810 391','button left down');
    // Cross the editor's source-poll interval immediately after its own Save.
    await new Promise(resolve=>setTimeout(resolve,550));
    await app.frame('mouse 746 327');await app.frame('button left up');
    assert.equal((await app.frame()).dirty,true);
    await app.frame('key leftCtrl','key z');assert.equal((await app.frame()).dirty,false);
    // Axis scaling and Escape cancellation do not silently create overrides.
    await app.frame('key r');await app.frame();
    await app.frame('mouse 814 391','button left down');await app.frame('mouse 850 391');await app.frame('button left up');
    assert.equal((await app.frame()).dirty,true);
    await app.frame('key leftCtrl','key z');assert.equal((await app.frame()).dirty,false);
    await app.frame('key w');await app.frame();
    await app.frame('mouse 805 391','button left down');await app.frame('mouse 850 391','key escape');await app.frame('button left up');
    assert.equal((await app.frame()).dirty,false);assert.equal(readFileSync(file,'utf8'),saved);
} finally { await app.close();clearTimeout(timeout); }
console.log('ok: native frame transport, isometric alignment, mesh depth picking, move/rotate/scale, cancel, undo and EOF shutdown');
const invalid=createServer(socket=>socket.resume());await new Promise(resolve=>invalid.listen(0,'127.0.0.1',resolve));
for (const input of ['INPUT capture stolen.png\nFRAME 1\n','INPUT mouse 1 2\0\n','x'.repeat(5000)+'\n','FRAME 2\n']) {
    const result=spawnSync(exe,[file,'--project',work,'--embed',String(invalid.address().port),'0123456789abcdef0123456789abcdef'],{input,encoding:'utf8',windowsHide:true,timeout:15000,env:{...process.env,TIDE_HIDDEN:'1'}});
    assert.ifError(result.error);assert.equal(result.status,2,result.stderr);assert.match(result.stderr,/invalid embedding protocol/);
}
await new Promise(resolve=>invalid.close(resolve));
