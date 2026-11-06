#!/usr/bin/env python3
"""UI-only Safari-like DOM wiring test (mocked Worker/ASM80, no WASM).

Run independently of the real Emscripten/browser integration acceptance on Euler.
"""
from pathlib import Path
import json
import shutil
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/'web/index.html').read_text().replace('<script type="module" src="app.js"></script>', '')
APP=(ROOT/'web/app.js').read_text().replace(
    "import {extractAssemblyDiagnostics} from './assembler-core.mjs';",
    "const extractAssemblyDiagnostics=error=>error.diagnostics||[];")
APP=APP.replace("new URL('./worker.js', import.meta.url)","'worker.js'")
assert "const assemblerPromise=import('./assembler.js').then(module=>{" in APP
result={
 'hex':':050100003E41D30076CC\n:00000001FF\n',
 'count':10,'entry':256,'listing':'0100 LD B,16',
 'sourceMap':{'addressToLine':{'256':2,'258':3,'260':4,'262':5},
   'lineToAddresses':{'2':[256],'3':[258],'4':[260],'5':[262]},
   'entries':[{'address':256,'line':2,'length':2},{'address':258,'line':3,'length':2},
              {'address':260,'line':4,'length':2},{'address':262,'line':5,'length':1}]}}
source_assembler='''const assemblerPromise=Promise.resolve({assembleZ80:async text=>{
    if(text.includes('FAKEOP')){
      const e=new Error('Line 4: Unknown mnemonic');
      e.diagnostics=[{line:4,message:'Unknown mnemonic'}];throw e;
    }
    return RESULT;
  }}).then(module=>{'''.replace('RESULT',json.dumps(result))
APP=APP.replace("const assemblerPromise=import('./assembler.js').then(module=>{",source_assembler)
state={key:0 for key in 'a f b c d e h l aa ff bb cc dd ee hh ll i r ix iy sp pc im iff1 iff2 halted instructions cycles'.split()}
state.update(pc=256,sp=65534)
mock='''
window._messages=[];window._pc=256;window._state=STATE;window._bp=new Set();
window.Worker=class {
  constructor(){setTimeout(()=>this.onmessage({data:{type:'ready'}}),0)}
  snapshot(){this.onmessage({data:{type:'state',state:{...window._state,pc:window._pc},
    rows:[{address:256,line:'0100 LD B,16'},{address:258,line:'0102 LD A,41'},
      {address:260,line:'0104 OUT (00),A'},{address:262,line:'0106 INC A'}],
    memoryAddress:8192,bytes:Array(128).fill(0),running:false,
    followingPC:true,canDisasmBack:false,disasmAddress:window._pc,
    breakpoints:[...window._bp],stopReason:'step'} });}
  postMessage(msg){
    window._messages.push(msg);
    if(msg.type==='load'){
      window._pc=256;window._bp.clear();
      setTimeout(()=>{this.onmessage({data:{type:'loaded',count:10}});this.snapshot()},0);
    }
    if(msg.type==='source-step'){window._pc=258;this.snapshot()}
    if(msg.type==='step'){window._pc=260;this.snapshot()}
    if(msg.type==='breakpoint-toggle'){
      if(window._bp.has(msg.address))window._bp.delete(msg.address);
      else window._bp.add(msg.address);this.snapshot();
    }
  }
};
'''.replace('STATE',json.dumps(state))
with sync_playwright() as p:
    chromium=shutil.which('chromium') or shutil.which('google-chrome')
    browser=p.chromium.launch(headless=True,executable_path=chromium,args=['--no-sandbox'] if chromium else [])
    page=browser.new_page()
    page.set_content(HTML)
    page.add_script_tag(content=mock)
    page.evaluate('window.fetch=async()=>({ok:true,arrayBuffer:async()=>new Uint8Array([0]).buffer})')
    page.on('pageerror',lambda e:print('PAGE ERROR:',e))
    page.add_script_tag(content=APP)
    page.get_by_text('10 bytes loaded. Ready to execute.').wait_for()
    assert page.locator('#source-step').is_disabled()
    page.locator('#asm-example').select_option('alphabet')
    page.locator('#asm-example-load').click()
    page.locator('#asm-assemble').click()
    page.get_by_text('Assembled 10 bytes and loaded at entry $0100.',exact=True).wait_for()
    assert page.locator('#source-step').is_enabled()
    page.locator('#source-step').click()
    assert page.locator('#asm-line-numbers .source-current').inner_text()=='3'
    sent=page.evaluate('window._messages')
    assert any(m['type']=='source-step' and len(m['entries'])==4 for m in sent)
    # Shortcut works from inside the source editor, without inserting a character.
    old=page.locator('#asm-source').input_value()
    page.locator('#asm-source').focus()
    page.locator('#asm-source').press('Shift+F10')
    assert sum(m['type']=='source-step' for m in page.evaluate('window._messages'))==2
    assert page.locator('#asm-source').input_value()==old
    # Ctrl+Enter triggers assemble while editing; then test inline errors.
    page.locator('#asm-source').fill('ORG $100\nNOP\nNOP\nFAKEOP A\n')
    assert page.locator('#source-step').is_disabled()
    page.locator('#asm-source').press('Control+Enter')
    page.get_by_text('Line 4: Unknown mnemonic',exact=True).first.wait_for()
    assert page.locator('#asm-diagnostics button').count()==1
    assert page.locator('#asm-highlight .source-error').count()==1
    assert page.locator('#asm-line-numbers .source-error').count()==1
    page.locator('#asm-diagnostics button').click()
    assert page.locator('#asm-source').evaluate('(el)=>el.selectionStart')==len('ORG $100\nNOP\nNOP\n')
    page.locator('#asm-source').fill('ORG $100\nHALT\n')
    assert page.locator('#asm-diagnostics button').count()==0
    assert page.locator('#asm-highlight .source-error').count()==0
    browser.close()
print('PASS: source-step UI, editor shortcuts, inline errors and diagnostic navigation (mocked Worker/assembler)')
