#!/usr/bin/env python3
"""Playwright smoke test for v0.7 source mapping (mock assembler + mock Worker).

No network, wasm, or third-party assembler: verifies UI state and message wiring,
not executable CPU or ASM80 conformance. Run real ASM80 tests on Euler.
"""
from pathlib import Path
import json
import shutil
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/'web/index.html').read_text().replace('<script type="module" src="app.js"></script>', '')
APP=(ROOT/'web/app.js').read_text()

state_keys=('a f b c d e h l aa ff bb cc dd ee hh ll i r ix iy sp pc im '
            'iff1 iff2 halted instructions cycles').split()
state={key:0 for key in state_keys}
state.update(pc=0x100,sp=0xfffe)

mock='''
window._messages=[];
window._bp=new Set();
window._pc=256;
window._state=STATE;
window.Worker=class {
  constructor(){setTimeout(()=>this.onmessage({data:{type:'ready'}}),0)}
  snapshot(){
    this.onmessage({data:{type:'state',state:{...window._state,pc:window._pc},
      rows:[{address:256,line:'0100  06 10  LD B,16'},
            {address:258,line:'0102  3E 41  LD A,41'},
            {address:260,line:'0104  D3 00  OUT (00),A'},
            {address:262,line:'0106  3C     INC A'}],
      memoryAddress:8192, bytes:Array(128).fill(0), running:false,
      breakpoints:[...window._bp],followingPC:true,disasmAddress:window._pc,
      canDisasmBack:false,stopReason:''}});
  }
  postMessage(msg){
    window._messages.push(msg);
    if(msg.type==='load'){
      window._pc=256;
      window._bp.clear();
      setTimeout(()=>{this.onmessage({data:{type:'loaded',count:10}});this.snapshot()},0);
    }
    if(msg.type==='breakpoint-toggle'){
      if(window._bp.has(msg.address))window._bp.delete(msg.address);
      else window._bp.add(msg.address);
      this.snapshot();
    }
    if(msg.type==='write-memory')this.snapshot();
    if(msg.type==='step'){window._pc=262;this.snapshot()}
  }
};
'''.replace('STATE',json.dumps(state))
# The actual bundle is supplied by Euler build; mock only the assembler outcome.
result={
 'hex':':050100003E41D30076CC\\n:00000001FF\\n',
 'count':10,'entry':256,'listing':'0100 LD B,16',
 'sourceMap':{'addressToLine':{'256':2,'258':3,'260':4,'262':5,'263':6,'265':7},
   'lineToAddresses':{'2':[256],'3':[258],'4':[260],'5':[262],'6':[263],'7':[265]},
   'entries':[{'address':256,'line':2,'length':2},
              {'address':258,'line':3,'length':2},
              {'address':260,'line':4,'length':2},
              {'address':262,'line':5,'length':1},
              {'address':263,'line':6,'length':2},
              {'address':265,'line':7,'length':1}]}}
assert APP.count("const assemblerPromise=import('./assembler.js').then(module=>{")==1
APP=APP.replace("new URL('./worker.js', import.meta.url)","'worker.js'")
APP=APP.replace("const assemblerPromise=import('./assembler.js').then(module=>{",
  'const assemblerPromise=Promise.resolve({assembleZ80:async()=>('+json.dumps(result)+')}).then(module=>{')

with sync_playwright() as p:
    chromium=shutil.which('chromium') or shutil.which('google-chrome')
    browser=p.chromium.launch(headless=True,executable_path=chromium,args=['--no-sandbox'] if chromium else [])
    page=browser.new_page()
    page.set_content(HTML)
    page.add_script_tag(content=mock)
    page.evaluate('window.fetch=async()=>({ok:true,arrayBuffer:async()=>new Uint8Array([0]).buffer})')
    page.on('console',lambda msg:print('BROWSER',msg.type,msg.text))
    page.on('pageerror',lambda e:print('PAGE ERROR',e))
    page.add_script_tag(content=APP)
    page.get_by_text('10 bytes loaded. Ready to execute.').wait_for(timeout=3500)
    page.locator('#asm-example').select_option('alphabet')
    page.locator('#asm-example-load').click()
    page.locator('#asm-assemble').click()
    page.get_by_text('Assembled 10 bytes and loaded at entry $0100.',exact=True).wait_for(timeout=3500)
    buttons=page.locator('#asm-line-numbers button.asm-gutter-line')
    assert buttons.count()==6, buttons.count()
    assert page.locator('#asm-line-numbers button.source-current').inner_text()=='2'
    # Actual debugger breakpoint messages: source breakpoint on INC A at $0106.
    page.locator('#asm-line-numbers button.asm-gutter-line').filter(has_text='5').click()
    assert any(m['type']=='breakpoint-toggle' and m['address']==262 for m in page.evaluate('window._messages'))
    assert page.locator('#asm-line-numbers button.source-breakpoint').inner_text()=='5'
    page.locator('#step').click()
    assert page.locator('#asm-line-numbers button.source-current').inner_text()=='5'
    assert page.locator('#asm-debug-status').inner_text().startswith('PC $0106 · source line 5')
    # Edits outside code region keep the mapping.
    page.locator('#memory-edit-at').fill('2000')
    page.locator('#memory-edit-bytes').fill('41')
    page.locator('#memory-write').click()
    assert buttons.count()==6
    # Edits within code region invalidate correspondence.
    page.locator('#memory-edit-at').fill('0106')
    page.locator('#memory-edit-bytes').fill('3C')
    page.locator('#memory-write').click()
    assert page.locator('#asm-line-numbers button.asm-gutter-line').count()==0
    # Reassemble; modifying source also invalidates its mapping.
    page.locator('#asm-assemble').click()
    page.get_by_text('Assembled 10 bytes and loaded at entry $0100.',exact=True).wait_for(timeout=3500)
    page.locator('#asm-source').fill(page.locator('#asm-source').input_value()+'; changed\\n')
    assert page.locator('#asm-line-numbers button.asm-gutter-line').count()==0
    browser.close()
print('PASS: source-line mapping, gutter breakpoints, PC highlight, stale-map invalidation (mocked Worker/assembler)')
