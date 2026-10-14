import assert from 'node:assert/strict';
import { test } from 'node:test';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const {editorPage}=createRequire(import.meta.url)('../../tide-vscode/src/prefab-client.js');

function client(){
    const messages=[],listeners={},handlers={};
    const image={addEventListener:(name,callback)=>handlers[name]=callback,focus(){},setPointerCapture(){},getBoundingClientRect:()=>({left:0,top:0,width:720,height:450})};
    const status={textContent:''};
    const document={hidden:false,getElementById:id=>id==='editor'?image:status,addEventListener:(name,callback)=>listeners[name]=callback};
    let tick;
    const context={document,window:{addEventListener:(name,callback)=>listeners[name]=callback},setTimeout:callback=>{tick=callback;return 1;},clearTimeout(){},send:message=>messages.push(message)};
    const html=editorPage('','123');vm.runInNewContext(html.match(/<script nonce="123">([\s\S]*)<\/script>/)[1],context);
    return{messages,handlers,image,document,window:context.window,frame:()=>{context.window.tideFrame({png:'AAAA'});image.onload();tick?.();}};
}
test('IDE pointer transitions occupy separate frames, preserving fast clicks and drag origins',()=>{
    const c=client();assert.equal(c.messages[0].type,'frame');
    const pointer=(x,y)=>({clientX:x,clientY:y,button:0,pointerId:1,preventDefault(){}});
    c.handlers.pointerdown(pointer(100,100));c.handlers.pointermove(pointer(200,120));c.handlers.pointerup(pointer(200,120));
    c.frame();let request=c.messages.at(-1);assert.deepEqual(Array.from(request.events),['mouse 200.000 200.000','button left down']);
    c.frame();request=c.messages.at(-1);assert.deepEqual(Array.from(request.events),['mouse 400.000 240.000','button left up']);
    c.frame();assert.equal(c.messages.at(-1).events.length,0);
});
test('IDE Save is a single-frame control chord and blur releases held input',()=>{
    const c=client();c.window.tideAction('save');c.frame();
    assert.deepEqual(Array.from(c.messages.at(-1).events),['key leftCtrl','key s']);
    c.handlers.keydown({code:'KeyW',key:'w',ctrlKey:false,altKey:false,metaKey:false,repeat:false,preventDefault(){}});
    c.handlers.blur();c.frame();assert.deepEqual(Array.from(c.messages.at(-1).events),['key w down']);
    c.frame();assert.deepEqual(Array.from(c.messages.at(-1).events),['text w','key w up']);
});
