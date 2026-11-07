#!/usr/bin/env python3
"""Exercise v0.8 session save/open UI with an instrumented Worker and codec.

This is a browser interaction test; real CPU fidelity is covered by native C++
snapshot tests and real Safari/WebAssembly remains an Euler acceptance check.
"""
from pathlib import Path
import json
import shutil
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/'web/index.html').read_text().replace('<script type="module" src="app.js"></script>', '')
APP=(ROOT/'web/app.js').read_text().replace("import {extractAssemblyDiagnostics} from './assembler-core.mjs';",'const extractAssemblyDiagnostics=()=>[];').replace("new URL('./worker.js', import.meta.url)","'worker.js'")
APP=APP.replace("import('./session-format.mjs')",'Promise.resolve(window._codec)')
result={
 'hex':':050100003E41D30076CC\n:00000001FF\n',
 'count':10,'entry':256,'listing':'0100 LD B,16',
 'sourceMap':{'addressToLine':{'256':2,'258':3,'260':4,'262':5},
  'lineToAddresses':{'2':[256],'3':[258],'4':[260],'5':[262]},
  'entries':[{'address':256,'line':2,'length':2},{'address':258,'line':3,'length':2},
  {'address':260,'line':4,'length':2},{'address':262,'line':5,'length':1}]}}
APP=APP.replace("const assemblerPromise=import('./assembler.js').then(module=>{",
    'const assemblerPromise=Promise.resolve({assembleZ80:async()=>('+json.dumps(result)+')}).then(module=>{')
state_keys=('a f b c d e h l aa ff bb cc dd ee hh ll i r ix iy sp pc im '
            'iff1 iff2 halted instructions cycles').split()
state={key:0 for key in state_keys}
state.update(pc=256,sp=65534)
MOCK='''
window._messages=[];
window._pc=256; window._bp=new Set(); window._clock=4000000;
window._state=STATE;
window._codec={
  encodeSession:({bytes,debugger:settings,workspace})=>{
    const json=JSON.stringify({bytes:[...bytes],debugger:settings,workspace});
    window._latestJson=json;return json;},
  sessionDownloadName:()=> 'z80-lab-session-20261107-122315-123.z80session',
  decodeSession:text=>{const saved=JSON.parse(text);
    return {bytes:new Uint8Array(saved.bytes),debugger:saved.debugger,workspace:saved.workspace};}
};
URL.createObjectURL=()=> 'blob:mock';
URL.revokeObjectURL=()=>{};
HTMLAnchorElement.prototype.click=function(){
  window._downloads=(window._downloads||0)+1;
  window._downloadName=this.download;
};
window.Worker=class {
  constructor(){window._worker=this;setTimeout(()=>this.onmessage({data:{type:'ready'}}),0)}
  snapshot(output=''){
    this.onmessage({data:{type:'state',state:{...window._state,pc:window._pc},
      rows:[{address:256,line:'0100 LD B,16'},{address:262,line:'0106 INC A'}],
      memoryAddress:8192,bytes:Array(128).fill(0),running:false,
      breakpoints:[...window._bp],clock:window._clock,
      followingPC:true,disasmAddress:window._pc,canDisasmBack:false,output,
      stopReason:'breakpoint'}});
  }
  postMessage(msg){
    window._messages.push(msg);
    if(msg.type==='load'){
      window._pc=256;window._bp.clear();
      setTimeout(()=>{this.onmessage({data:{type:'loaded',count:10}});this.snapshot()},0);
    }
    if(msg.type==='breakpoint-toggle'){
      if(window._bp.has(msg.address))window._bp.delete(msg.address);
      else window._bp.add(msg.address);
      this.snapshot();
    }
    if(msg.type==='session-save'){
      this.snapshot('');
      let bytes=new Uint8Array(65868);bytes.set([90,56,76,83,1,0]);
      this.onmessage({data:{type:'session-data',requestId:msg.requestId,
        bytes:bytes.buffer,debugger:{clock:window._clock,breakpoints:[...window._bp],
          memoryAddress:8192,disasmAddress:null,stopReason:'breakpoint'}}});
    }
    if(msg.type==='session-restore'){
      window._pc=262;
      window._bp=new Set(msg.debugger.breakpoints);
      window._clock=msg.debugger.clock;
      this.onmessage({data:{type:'session-restored',requestId:msg.requestId}});
      this.snapshot();
    }
    if(msg.type==='breakpoint-clear'){window._bp.clear();this.snapshot();}
    if(msg.type==='clock'){window._clock=msg.value;this.snapshot();}
  }
};
'''.replace('STATE',json.dumps(state))

with sync_playwright() as p:
    chromium=shutil.which('chromium') or shutil.which('google-chrome')
    browser=p.chromium.launch(headless=True,executable_path=chromium,args=['--no-sandbox'] if chromium else [])
    page=browser.new_page(accept_downloads=False)
    page.set_content(HTML)
    page.add_script_tag(content=MOCK)
    page.evaluate('window.fetch=async()=>({ok:true,arrayBuffer:async()=>new Uint8Array([0]).buffer})')
    page.on('pageerror',lambda e:print('PAGE ERROR:',e))
    page.add_script_tag(content=APP)
    page.get_by_text('10 bytes loaded. Ready to execute.').wait_for()
    page.locator('#asm-example').select_option('alphabet')
    page.locator('#asm-example-load').click()
    page.locator('#asm-assemble').click()
    page.get_by_text('Assembled 10 bytes and loaded at entry $0100.',exact=True).wait_for()
    page.locator('#asm-line-numbers button.asm-gutter-line').filter(has_text='5').click()
    page.evaluate("window._pc=262;window._worker.snapshot('ABC');")
    assert page.locator('#terminal').inner_text()=='ABC'
    assert page.locator('#asm-line-numbers button.source-breakpoint').inner_text()=='5'
    saved_source=page.locator('#asm-source').input_value()
    page.locator('#session-save').click()
    page.get_by_text('Session saved locally. The Z80 is paused; Run to continue.').wait_for()
    assert page.evaluate('window._downloads')==1
    assert page.evaluate('window._downloadName')=='z80-lab-session-20261107-122315-123.z80session'
    # Safari's duplicate filenames may end in .z80session-2. Do not filter
    # these from the native picker; decodeSession validates their contents.
    assert page.locator('#session-file').get_attribute('accept') is None
    # Capture the exact downloaded JSON in this test via a wrapper around codec.
    # The file content was encoded while a valid source map and breakpoint existed.
    # Change the emulator environment and then restore it.
    page.locator('#asm-source').fill('ORG $0200\nHALT\n')
    page.locator('#breakpoint-clear').click()
    page.locator('#clock').select_option('2000000')
    page.locator('#asm-listing').evaluate('(el)=>el.textContent="changed"')
    payload=page.evaluate('window._latestJson')
    page.locator('#session-file').set_input_files({'name':'z80-lab-session.z80session-2',
       'mimeType':'application/json','buffer':payload.encode()})
    page.get_by_text('Session restored. CPU paused; Run to continue.').wait_for()
    assert page.locator('#asm-source').input_value()==saved_source
    assert page.locator('#terminal').inner_text()=='ABC'
    assert page.locator('#clock').input_value()=='4000000'
    assert page.locator('#asm-line-numbers button.source-breakpoint').inner_text()=='5'
    assert page.locator('#asm-line-numbers button.source-current').inner_text()=='5'
    assert page.locator('#asm-listing').inner_text()=='0100 LD B,16'
    assert any(m['type']=='session-restore' for m in page.evaluate('window._messages'))
    browser.close()
print('PASS: UI save and restore actions restore source, serial, debugger and source highlights (mocked Worker)')
