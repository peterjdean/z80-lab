// Browser controller: all emulation runs in a dedicated WASM Web Worker.
const $ = id => document.getElementById(id);
const worker = new Worker(new URL('./worker.js', import.meta.url), {type:'module'});
const encoder = new TextEncoder();
let ready=false;
let serial='';
let current=null;
let pendingAssembly=null;
let pendingSession=null;
let sessionRequestId=0;
function sessionBusy(busy) {
  $('session-save').disabled=busy||!ready;
  $('session-open').classList.toggle('busy',busy);
  $('session-file').disabled=busy;
}
let loadedSourceMap=null;
let sourcePcLine=null;
let sourceBreakpointAddresses=new Set();
let viewingMemory=0x2000;
const toHex=(n,width=2)=>Number(n).toString(16).toUpperCase().padStart(width,'0');
const flagBit=(f,mask)=>f&mask?'1':'0';
function validAddress(text) {
  const value=text.trim().replace(/^\$|^0x/i,'');
  if(!/^[0-9a-f]{1,4}$/i.test(value))throw new Error('Enter a hex address (0000–FFFF).');
  return parseInt(value,16);
}
function parseBytes(text) {
  const cleaned=text.trim();
  if(!cleaned) throw new Error('Enter one or more hex bytes.');
  const tokens=cleaned.split(/[\s,]+/);
  const values=[];
  for(const token of tokens) {
    const trimmed=token.replace(/^\$|^0x/i,'');
    if(!/^[0-9a-f]{1,2}$/i.test(trimmed))
      throw new Error(`Invalid hex byte: ${token}. Separate bytes with spaces or commas.`);
    values.push(parseInt(trimmed,16));
  }
  if(values.length>128) throw new Error('A memory edit may contain at most 128 bytes.');
  return values;
}
function send(msg){if(ready)worker.postMessage(msg);}
function notice(text,isError=false){const el=$('message');el.textContent=text;el.classList.toggle('error',isError);}
const wasmControls=[...document.querySelectorAll('[data-requires-wasm]')];
function render(data) {
  const {state:st,rows,memoryAddress,bytes,running,breakpoints,followingPC,canDisasmBack,stopReason,disasmAddress}=data;
  current=st;
  viewingMemory=memoryAddress;
  for(const key of ['a','f','b','c','d','e','h','l','aa','ff','bb','cc','dd','ee','hh','ll']) $(key).textContent=toHex(st[key]);
  for(const key of ['ix','iy','sp','pc'])$(key).textContent=toHex(st[key],4);
  for(const key of ['i','r'])$(key).textContent=toHex(st[key]);
  for(const [key,bit] of [['S',128],['Z',64],['Y',32],['H',16],['X',8],['PV',4],['N',2],['C',1]]) {
    const el=$('flag-'+key);
    el.textContent=flagBit(st.f,bit);
    el.parentElement.classList.toggle('active',!!(st.f&bit));
  }
  $('iff').textContent=`${Number(st.iff1)}/${Number(st.iff2)}`;
  if(document.activeElement!==$('pc-set-value')) $('pc-set-value').value=toHex(st.pc,4);
  $('im').textContent=st.im;
  $('instructions').textContent=st.instructions.toLocaleString();
  $('cycles').textContent=st.cycles.toLocaleString();
  $('status').textContent=st.halted?'HALTED':running?'RUNNING':stopReason==='breakpoint'?'BREAKPOINT':'PAUSED';
  $('status').className=st.halted?'pill halted':running?'pill running':'pill';
  $('run').disabled=running||st.halted;
  $('step').disabled=running||st.halted;
  $('pause').disabled=!running;
  $('disasm-mode').textContent=followingPC?'Following PC · click to break':`Browsing $${toHex(disasmAddress,4)}`;
  $('disasm-back').disabled=!canDisasmBack;
  $('disasm-follow').disabled=followingPC;
  if(document.activeElement!==$('disasm-at'))$('disasm-at').value=toHex(disasmAddress,4);
  const bpSet=new Set(breakpoints);
  sourceBreakpointAddresses=bpSet;
  updateSourceDebugger(st.pc,running);
  $('disassembly').replaceChildren(...rows.map(row=>{
    const el=document.createElement('button');
    el.type='button';
    const isBP=bpSet.has(row.address);
    el.className='instruction'+(row.address===st.pc?' selected':'')+(isBP?' breakpoint':'');
    el.textContent=`${isBP?'●':' '} ${row.line}`;
    el.title=isBP?'Remove breakpoint':'Add breakpoint';
    el.setAttribute('aria-label',`${isBP?'Remove':'Add'} breakpoint at ${toHex(row.address,4)}: ${row.line}`);
    el.addEventListener('click',()=>send({type:'breakpoint-toggle',address:row.address}));
    return el;
  }));
  $('bp-count').textContent=`${breakpoints.length} active`;
  const list=$('breakpoint-list');
  if(!breakpoints.length) {
    const el=document.createElement('span');el.className='bp-empty';el.textContent='No breakpoints set';
    list.replaceChildren(el);
  } else list.replaceChildren(...breakpoints.map(address=>{
    const el=document.createElement('button');el.type='button';el.className='bp-chip';
    el.textContent=`$${toHex(address,4)} ×`;
    el.title=`Remove breakpoint $${toHex(address,4)}`;
    el.setAttribute('aria-label',`Remove breakpoint at ${toHex(address,4)}`);
    el.addEventListener('click',()=>send({type:'breakpoint-remove',address}));
    return el;
  }));
  if(document.activeElement!==$('memory-at')) $('memory-at').value=toHex(memoryAddress,4);
  if(document.activeElement!==$('memory-edit-at')) $('memory-edit-at').value=toHex(memoryAddress,4);
  $('memory').textContent=Array.from({length:8},(_,i)=>{
    const chunk=bytes.slice(i*16,(i+1)*16);
    const ascii=chunk.map(b=>b>=32&&b<=126?String.fromCharCode(b):'.').join('');
    return `${toHex((memoryAddress+i*16)&0xffff,4)}  ${chunk.map(b=>toHex(b)).join(' ')}  ${ascii}`;
  }).join('\n');
}
worker.onmessage = event=>{
  const data=event.data;
  if(data.type==='error'){
    if(pendingAssembly){$('asm-status').textContent='Load failed: '+data.message;$('asm-status').classList.add('error');pendingAssembly=null;}
    if(pendingSession){pendingSession=null;sessionBusy(false);}
    notice(data.message,true);return;
  }
  if(data.type==='info'){notice(data.message);return;}
  if(data.type==='ready'){
    ready=true;
    for(const el of wasmControls)el.disabled=false;
    sessionBusy(false);
    notice('Z80 ready. Load a sample or local HEX file.');
    $('sample').value='examples/hello.hex';
    loadSample('examples/hello.hex');
  }
  if(data.type==='loaded'){
    notice(`${data.count} bytes loaded. Ready to execute.`);
    if(pendingAssembly){
      const result=pendingAssembly;
      loadedSourceMap=source.value===result.source ? result.sourceMap : null;
      $('asm-status').textContent=`Assembled ${result.count} bytes and loaded at entry $${toHex(result.entry,4)}.`;
      pendingAssembly=null;
      paintEditor();
    }
  }
  if(data.type==='session-data') {
    const pending=pendingSession;
    if(!pending||pending.kind!=='save'||pending.id!==data.requestId)return;
    (async()=>{
      try {
        const {encodeSession}=await import('./session-format.mjs');
        const workspace={source:source.value,serial,assembled:assembled||null,
          mapActive:!!loadedSourceMap};
        const json=encodeSession({bytes:new Uint8Array(data.bytes),
          debugger:data.debugger,workspace});
        downloadText('z80-lab-session.z80session',json,'application/json');
        notice('Session saved locally. The Z80 is paused; Run to continue.');
      } catch(e){notice(`Session save failed: ${e.message}`,true);}
      finally {pendingSession=null;sessionBusy(false);}
    })();
    return;
  }
  if(data.type==='session-restored') {
    const pending=pendingSession;
    if(!pending||pending.kind!=='restore'||pending.id!==data.requestId)return;
    const w=pending.workspace;
    source.value=w.source;
    serial=w.serial;
    $('terminal').textContent=serial;
    $('terminal').scrollTop=$('terminal').scrollHeight;
    assembled=w.assembled;
    loadedSourceMap=w.mapActive && assembled ? assembled.sourceMap : null;
    $('asm-listing').textContent=assembled?.listing||'Assemble to see addresses and generated bytes.';
    $('asm-hex-save').disabled=!assembled;
    $('asm-status').classList.remove('error');
    $('asm-status').textContent=assembled?'Assembly listing restored from session.':'Source restored from session.';
    $('clock').value=String(pending.debugger.clock);
    $('asm-debug-status').textContent=loadedSourceMap?'Source mapping restored.':'Assemble & load to enable source debugging.';
    paintEditor();
    pendingSession=null;
    sessionBusy(false);
    notice('Session restored. CPU paused; Run to continue.');
    return;
  }
  if(data.type==='state') {
    if(data.output){
      serial+=data.output.replace(/\r\n/g,'\n').replace(/\r/g,'\n');
      if(serial.length>16384)serial=serial.slice(-16384);
      $('terminal').textContent=serial;
      $('terminal').scrollTop=$('terminal').scrollHeight;
    }
    render(data);
  }
};
worker.onerror=event=>notice(`Worker failed: ${event.message||'check browser console'}`,true);
function formatFor(filename) {
  const ext=filename.split('.').pop().toLowerCase();
  return ext==='bin'?3:ext==='hex'?1:ext==='txt'?2:0;
}
function loadBytes(bytes,filename,{fromAssembly=false}={}){
  if(!fromAssembly) clearSourceMap('A different program was loaded. Reassemble to restore source debugging.');
  const format=formatFor(filename);
  const origin=format===3?validAddress($('origin').value):-1;
  serial='';$('terminal').textContent='';
  worker.postMessage({type:'load',bytes:bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),format,origin});
}
async function loadSample(path){
  try{
    notice(`Loading ${path}…`);
    const response=await fetch(path);
    if(!response.ok)throw new Error(`HTTP ${response.status}`);
    loadBytes(new Uint8Array(await response.arrayBuffer()),path);
  }catch(e){notice(`Could not load sample: ${e.message}`,true);}
}
$('session-save').addEventListener('click',()=>{
  if(!ready||pendingSession)return;
  if(pendingAssembly){notice('Complete program loading before saving a session.',true);return;}
  pendingSession={kind:'save',id:++sessionRequestId};
  sessionBusy(true);
  notice('Pausing Z80 and preparing session…');
  worker.postMessage({type:'session-save',requestId:pendingSession.id});
});
$('session-file').addEventListener('change',async event=>{
  const file=event.target.files[0];
  if(!file||!ready||pendingSession)return;
  try {
    if(file.size>2_500_000)throw new Error('Session file is too large.');
    const {decodeSession}=await import('./session-format.mjs');
    const session=decodeSession(await file.text());
    if(pendingSession||pendingAssembly)throw new Error('Finish the previous operation first.');
    pendingSession={kind:'restore',id:++sessionRequestId,
      workspace:session.workspace,debugger:session.debugger};
    sessionBusy(true);
    notice('Restoring Z80 CPU and workspace…');
    worker.postMessage({type:'session-restore',requestId:pendingSession.id,
      debugger:session.debugger,bytes:session.bytes.buffer},[session.bytes.buffer]);
  }catch(error){notice('Session restore failed: '+error.message,true);}
  finally{event.target.value='';}
});
$('load-sample').addEventListener('click',()=>loadSample($('sample').value));
$('file').addEventListener('change',async e=>{
  const file=e.target.files[0];if(!file)return;
  try{loadBytes(new Uint8Array(await file.arrayBuffer()),file.name);}catch(error){notice(error.message,true);}
});
$('run').addEventListener('click',()=>send({type:'run'}));
$('pause').addEventListener('click',()=>send({type:'pause'}));
$('step').addEventListener('click',()=>send({type:'step'}));
$('reset').addEventListener('click',()=>{serial='';$('terminal').textContent='';send({type:'reset'});});
$('clock').addEventListener('change',e=>send({type:'clock',value:Number(e.target.value)}));
$('breakpoint-set').addEventListener('click',()=>{
  try{send({type:'breakpoint-add',address:validAddress($('breakpoint').value)});$('breakpoint').value='';}
  catch(e){notice(e.message,true);}
});
$('breakpoint').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();$('breakpoint-set').click();}});
$('breakpoint-clear').addEventListener('click',()=>send({type:'breakpoint-clear'}));
$('disasm-go').addEventListener('click',()=>{
  try{send({type:'disasm-goto',address:validAddress($('disasm-at').value)});}
  catch(e){notice(e.message,true);}
});
$('disasm-at').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();$('disasm-go').click();}});
$('disasm-next').addEventListener('click',()=>send({type:'disasm-next'}));
$('disasm-back').addEventListener('click',()=>send({type:'disasm-back'}));
$('disasm-follow').addEventListener('click',()=>send({type:'disasm-follow'}));
$('memory-set').addEventListener('click',()=>{
  try{send({type:'memory',address:validAddress($('memory-at').value)});}
  catch(e){notice(e.message,true);}
});
$('memory-at').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();$('memory-set').click();}});
$('memory-prev').addEventListener('click',()=>send({type:'memory',address:(viewingMemory-128+65536)&0xffff}));
$('memory-next').addEventListener('click',()=>send({type:'memory',address:(viewingMemory+128)&0xffff}));
$('memory-edit-form').addEventListener('submit',e=>{
  e.preventDefault();
  try{
    const address=validAddress($('memory-edit-at').value);
    const bytes=parseBytes($('memory-edit-bytes').value);
    if(address+bytes.length>65536)throw new Error('Edit would cross $FFFF.');
    if(loadedSourceMap && loadedSourceMap.entries.some(row=>address<row.address+row.length && row.address<address+bytes.length))
      clearSourceMap('Machine code was edited. Reassemble to restore source debugging.');
    send({type:'write-memory',address,bytes});
  }catch(error){notice(error.message,true);}
});
$('serial-form').addEventListener('submit',event=>{
  event.preventDefault();
  const value=$('serial-input').value;
  send({type:'input',bytes:encoder.encode(value+'\r')});
  $('serial-input').value='';
});
document.addEventListener('keydown',event=>{
  if(event.target.matches('input,textarea,select,button'))return;
  if(event.key==='F10'){event.preventDefault();send({type:'step'});}
  if(event.key==='F5'){event.preventDefault();send({type:'run'});}
});

// Set PC is deliberately independent of editing RAM.
$('pc-set').addEventListener('click',()=>{
  try {send({type:'set-pc',address:validAddress($('pc-set-value').value)});}
  catch(e){notice(e.message,true);}
});
$('pc-set-value').addEventListener('keydown',e=>{
  if(e.key==='Enter'){e.preventDefault();$('pc-set').click();}
});

// The assembler is an independently bundled third-party library, not part of
// the validated WASM CPU. No assembly or source code leaves this browser.
const assemblyExamples={
  print:`        ORG $0100
START:  LD HL,MESSAGE
LOOP:   LD A,(HL)
        OR A
        JR Z,DONE
        OUT ($00),A
        INC HL
        JR LOOP
DONE:   HALT
MESSAGE: DB "Hello from Z80/LAB!",13,10,0
`,
  alphabet:`        ORG $0100
        LD B,16
        LD A,'A'
LOOP:   OUT ($00),A
        INC A
        DJNZ LOOP
        HALT
`,
  memory:`        ORG $0100
START:  LD HL,$2000
        LD A,$41
        LD (HL),A
        INC HL
        INC A
        LD (HL),A
        HALT
`
};
let assembled = null;
let assemblerModule;
const assemblerPromise=import('./assembler.js').then(module=>{
  assemblerModule=module;
  $('asm-status').textContent='Assembler ready · single-file ASM80 syntax';
  return module;
}).catch(error=>{
  $('asm-status').textContent='Assembler unavailable: build the browser assembler bundle on Euler.';
  $('asm-status').classList.add('error');
  console.error('ASM80 initialisation failed:',error);
  return null;
});
const source=$('asm-source'), highlighted=$('asm-highlight'), lineNumbers=$('asm-line-numbers');
const escapeMarkup=text=>text.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
const opcodeWords=new Set(('ADC ADD AND BIT CALL CCF CP CPD CPDR CPI CPIR CPL DAA DEC DI DJNZ EI EX EXX HALT IM IN INC IND INDR INI INIR JP JR LD LDD LDDR LDI LDIR NEG NOP OR OTDR OTIR OUT OUTD OUTI POP PUSH RES RET RETI RETN RL RLA RLC RLCA RLD RR RRA RRC RRCA RRD RST SBC SCF SET SLA SLL SRA SRL SUB XOR').split(' '));
const directives=new Set('ORG EQU DB DW DS DEFB DEFW DEFS END IF ELSE ENDIF INCLUDE INCBIN MACRO ENDM'.split(' '));
// Preserve comments and quoted strings; tokenise only the code portion.
function paintCode(line) {
  const rx=/(;.*$|"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|\b[A-Za-z_][\w']*:?|\$[0-9a-f]+\b|\b0x[0-9a-f]+\b|\b\d+\b)/gi;
  let result='',last=0,match;
  while((match=rx.exec(line))!==null){
    result+=escapeMarkup(line.slice(last,match.index));
    const text=match[0],word=text.replace(/:$/,'').toUpperCase();
    let name='';
    if(text[0]===';')name='asm-comment';
    else if(text[0]==='"'||text[0]==="'")name='asm-string';
    else if(/^\$|^0x|^\d/.test(text))name='asm-number';
    else if(text.endsWith(':'))name='asm-label';
    else if(opcodeWords.has(word))name='asm-keyword';
    else if(directives.has(word))name='asm-dir';
    result+=name?`<span class="${name}">${escapeMarkup(text)}</span>`:escapeMarkup(text);
    last=rx.lastIndex;
  }
  return result+escapeMarkup(line.slice(last));
}
function clearSourceMap(message='Source changed. Reassemble to restore source debugging.') {
  loadedSourceMap=null;
  sourcePcLine=null;
  if($('asm-debug-status')) {
    $('asm-debug-status').textContent=message;
    $('asm-debug-status').classList.add('unmapped');
  }
  paintEditor();
}
function mappedLine(address) {
  return loadedSourceMap?.addressToLine?.[address] ?? null;
}
function updateSourceDebugger(pc,running=false) {
  const line=mappedLine(pc);
  const changed=line!==sourcePcLine;
  sourcePcLine=line;
  const gutter=[...lineNumbers.children];
  for(let i=0;i<gutter.length;i++) {
    const el=gutter[i];
    const addresses=loadedSourceMap?.lineToAddresses?.[i+1] || [];
    const hasBreakpoint=addresses.some(address=>sourceBreakpointAddresses.has(address));
    el.classList.toggle('source-current',i+1===line);
    el.classList.toggle('source-breakpoint',hasBreakpoint);
    el.setAttribute('aria-current',i+1===line?'true':'false');
    if(el.tagName==='BUTTON')el.setAttribute('aria-pressed',String(hasBreakpoint));
  }
  const lines=[...highlighted.children].filter(el=>el.classList.contains('asm-source-line'));
  for(let i=0;i<lines.length;i++)lines[i].classList.toggle('source-current',i+1===line);
  const status=$('asm-debug-status');
  status.classList.toggle('unmapped',!loadedSourceMap||line===null);
  if(!loadedSourceMap) {
    // Preserve helpful stale-map notices until the next successful assembly.
    if(!status.textContent)status.textContent='Assemble & load to enable source debugging.';
  } else if(line!==null) status.textContent=`PC $${toHex(pc,4)} · source line ${line}${running?' · running':''}`;
  else status.textContent=`PC $${toHex(pc,4)} is outside known source instruction starts.`;
  if(changed && line!==null && !running && document.activeElement!==source){
    const top=(line-1)*20;
    if(top<source.scrollTop || top+20>source.scrollTop+source.clientHeight)
      source.scrollTop=Math.max(0,top-source.clientHeight/2+10);
    syncEditorScroll();
  }
}
function paintEditor(){
  const lines=source.value.split('\n');
  highlighted.innerHTML=lines.map(line=>`<span class="asm-source-line">${paintCode(line)||' '}</span>`).join('\n')+'\n';
  lineNumbers.replaceChildren(...lines.map((_,i)=>{
    const addresses=loadedSourceMap?.lineToAddresses?.[i+1]||[];
    const el=document.createElement(addresses.length?'button':'span');
    el.className='asm-gutter-line';el.textContent=String(i+1);
    if(addresses.length){
      el.type='button';
      el.title=`Toggle breakpoint at $${toHex(addresses[0],4)} (source line ${i+1})`;
      el.setAttribute('aria-label',el.title);
      el.addEventListener('click',()=>send({type:'breakpoint-toggle',address:addresses[0]}));
    }
    return el;
  }));
  syncEditorScroll();
  if(current)updateSourceDebugger(current.pc,false);
}
function syncEditorScroll(){
  highlighted.scrollTop=source.scrollTop;
  highlighted.scrollLeft=source.scrollLeft;
  lineNumbers.scrollTop=source.scrollTop;
}
source.addEventListener('input',()=>{assembled=null;$('asm-hex-save').disabled=true;clearSourceMap();});
source.addEventListener('scroll',syncEditorScroll);
source.addEventListener('keydown',event=>{
  if(event.key!=='Tab')return;
  event.preventDefault();const start=source.selectionStart,end=source.selectionEnd;
  source.setRangeText('    ',start,end,'end');assembled=null;$('asm-hex-save').disabled=true;clearSourceMap();
});
source.value=assemblyExamples.print;paintEditor();
function downloadText(name,text,mime='text/plain'){
  const url=URL.createObjectURL(new Blob([text],{type:mime}));
  const a=document.createElement('a');a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();
  // URL is only released after a later user action/browser event, so Safari
  // has had time to resolve the generated download.
  setTimeout(()=>URL.revokeObjectURL(url),5000);
}
$('asm-example-load').addEventListener('click',()=>{
  source.value=assemblyExamples[$('asm-example').value]||assemblyExamples.print;
  assembled=null;$('asm-hex-save').disabled=true;clearSourceMap();
  $('asm-status').textContent='Example loaded. Assemble & load to run.';
});
$('asm-file').addEventListener('change',async event=>{
  const file=event.target.files[0];if(!file)return;
  try{
    if(file.size>200_000)throw new Error('Assembly source exceeds 200 KB.');
    source.value=await file.text();assembled=null;$('asm-hex-save').disabled=true;clearSourceMap();
    $('asm-status').textContent=`Opened ${file.name} locally.`;
  }catch(error){$('asm-status').textContent=error.message;$('asm-status').classList.add('error');}
});
$('asm-save').addEventListener('click',()=>downloadText('program.asm',source.value));
$('asm-hex-save').addEventListener('click',()=>{
  if(assembled)downloadText('program.hex',assembled.hex);
});
$('asm-assemble').addEventListener('click',async()=>{
  if(!ready)return;
  $('asm-status').classList.remove('error');
  $('asm-status').textContent='Assembling…';
  pendingAssembly=null;
  $('asm-assemble').disabled=true;
  try{
    const module=await assemblerPromise;
    if(!module)throw new Error('Assembler bundle not available. Run the assembler build on Euler.');
    const assemblyText=source.value;
    const result=await module.assembleZ80(assemblyText);
    if(source.value!==assemblyText)throw new Error('Source changed during assembly; assemble again.');
    result.source=assemblyText;
    assembled=result;
    $('asm-listing').textContent=result.listing||'No listing generated';
    $('asm-hex-save').disabled=false;
    // Conversion to Intel HEX is the sole bridge to the unchanged Z80 core.
    const bytes=encoder.encode(result.hex);
    pendingAssembly=result;
    loadBytes(bytes,'program.hex',{fromAssembly:true});
    $('asm-status').textContent=`Assembled ${result.count} bytes; loading at entry $${toHex(result.entry,4)}…`;
    $('pc-set-value').value=toHex(result.entry,4);
  }catch(error){
    pendingAssembly=null;
    assembled=null;$('asm-hex-save').disabled=true;
    $('asm-status').classList.add('error');
    $('asm-status').textContent=error.message;
    $('asm-listing').textContent=`Assembly failed:\n${error.message}`;
  }finally{$('asm-assemble').disabled=false;}
});
