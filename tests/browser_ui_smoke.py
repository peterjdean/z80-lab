#!/usr/bin/env python3
"""Optional browser UI smoke test, using a mocked Worker instead of WASM.

Requires: python playwright, a locally available Chromium executable.
This verifies DOM control wiring only. It is NOT WebAssembly conformance testing.
"""
from pathlib import Path
import json
import shutil
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
HTML = ROOT / 'web/index.html'
APP = ROOT / 'web/app.js'
state_keys = ('a f b c d e h l aa ff bb cc dd ee hh ll i r ix iy sp pc im '
              'iff1 iff2 halted instructions cycles').split()
state = {key: 0 for key in state_keys}
state.update(pc=256, sp=65534)
mock = '''
window._messages=[];
window.Worker=class {
  constructor(){setTimeout(()=>this.onmessage({data:{type:'ready'}}),0);}
  postMessage(msg){
    window._messages.push(msg);
    if(msg.type==='load'){
      this.onmessage({data:{type:'loaded',count:10}});
      this.onmessage({data:{type:'state',state:STATE,
        rows:[{address:256,line:'0100  21 00 20  LD HL,$2000'},
              {address:259,line:'0103  3E 41  LD A,$41'}],
        memoryAddress:8192,bytes:Array(128).fill(0),running:false,
        breakpoints:[],followingPC:true,disasmAddress:256,
        canDisasmBack:false,stopReason:''}});
    }
  }
};'''.replace('STATE', json.dumps(state))

with sync_playwright() as p:
    chromium = shutil.which('chromium') or shutil.which('google-chrome')
    browser = p.chromium.launch(headless=True, executable_path=chromium,
                                args=['--no-sandbox'] if chromium else [])
    page = browser.new_page()
    html = HTML.read_text().replace('<script type="module" src="app.js"></script>', '')
    page.set_content(html)
    page.add_script_tag(content=mock)
    page.evaluate('window.fetch=async()=>({ok:true,arrayBuffer:async()=>new Uint8Array([0]).buffer})')
    app = APP.read_text().replace("new URL('./worker.js', import.meta.url)", "'worker.js'")
    page.add_script_tag(content=app)
    page.get_by_text('10 bytes loaded. Ready to execute.').wait_for()
    page.locator('#breakpoint').fill('0103')
    page.locator('#breakpoint-set').click()
    page.locator('#disassembly .instruction').first.click()
    page.locator('#disasm-at').fill('2000')
    page.locator('#disasm-go').click()
    page.locator('#disasm-next').click()
    page.locator('#memory-at').fill('2000')
    page.locator('#memory-set').click()
    page.locator('#memory-edit-at').fill('2000')
    page.locator('#memory-edit-bytes').fill('3E 41 D3 00 C9')
    page.locator('#memory-write').click()
    messages = page.evaluate('window._messages')
    assert any(m['type']=='breakpoint-add' and m['address']==259 for m in messages)
    assert any(m['type']=='breakpoint-toggle' and m['address']==256 for m in messages)
    assert any(m['type']=='disasm-goto' and m['address']==8192 for m in messages)
    assert any(m['type']=='write-memory' and m['address']==8192
               and m['bytes']==[62,65,211,0,201] for m in messages)
    assert any(m['type']=='memory' and m['address']==8192 for m in messages)
    page.locator('#pc-set-value').fill('2000')
    page.locator('#pc-set').click()
    page.locator('#asm-example').select_option('alphabet')
    page.locator('#asm-example-load').click()
    assert 'DJNZ LOOP' in page.locator('#asm-source').input_value()
    assert '3' in page.locator('#asm-line-numbers').inner_text()
    assert any(m['type']=='set-pc' and m['address']==8192 for m in page.evaluate('window._messages'))
    page.locator('#memory-edit-bytes').fill('GG')
    page.locator('#memory-write').click()
    assert 'Invalid hex byte' in page.locator('#message').inner_text()
    browser.close()
print('PASS: UI event wiring, navigation, edits, and invalid-input feedback (mocked Worker)')
