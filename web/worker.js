// Z80 engine runs in a module worker; no WASM threads or SharedArrayBuffer.
import createZ80 from './z80wasm.mjs';

let wasm;
let running = false;
let scheduled = null;
let clock = 4_000_000; // T-states/s; 0 = unlimited.
const breakpoints = new Set();
let memoryAddress = 0x2000;
let disasmAddress = null; // null = follow PC
let disasmHistory = [];
let nextDisasmAddress = 0;
let epochTime = 0;
let epochCycles = 0;
let lastSnapshot = 0;
let lastTickTime = 0;
let stopReason = '';

const validAddress = n => Number.isInteger(n) && n >= 0 && n <= 0xffff;
const hex4 = n => n.toString(16).toUpperCase().padStart(4, '0');
function sendError(err) { postMessage({type:'error', message:err instanceof Error ? err.message:String(err)}); }
function info(message) { postMessage({type:'info', message}); }
function asText(ptr) { return wasm.UTF8ToString(ptr); }
function state() { return JSON.parse(asText(wasm._z80_state_json())); }
function output() {
  const ptr = wasm._z80_output_ptr();
  const len = wasm._z80_output_length();
  if (!len) return '';
  const data = wasm.HEAPU8.slice(ptr, ptr + len);
  let text = '';
  for (let i=0; i<data.length; i+=4096) text += String.fromCharCode(...data.subarray(i,i+4096));
  return text;
}
function snapshot() {
  const s = state();
  const rows = [];
  const start = disasmAddress === null ? s.pc : disasmAddress;
  let address = start;
  for (let i=0; i<12; i++) {
    rows.push({address, line:asText(wasm._z80_disassembly(address))});
    address = (address + Math.max(1, wasm._z80_disassembly_length(address))) & 0xffff;
  }
  nextDisasmAddress = address;
  const bytes = [];
  for (let i=0;i<128;i++) bytes.push(wasm._z80_read_byte((memoryAddress+i)&0xffff));
  postMessage({type:'state', state:s, rows, memoryAddress, bytes, output:output(),
    running, clock, breakpoints:[...breakpoints].sort((a,b)=>a-b),
    disasmAddress:start, followingPC:disasmAddress===null, canDisasmBack:disasmHistory.length>0,
    stopReason});
  lastSnapshot=performance.now();
}
function stop() {
  running=false;
  if (scheduled!==null) clearTimeout(scheduled);
  scheduled=null;
}
function resetEpoch() {
  epochTime=performance.now();
  lastTickTime=epochTime;
  epochCycles=state().cycles;
}
function tick() {
  scheduled=null;
  if (!running) return;
  try {
    const start=performance.now();
    if (clock>0 && start-lastTickTime>1000) resetEpoch();
    lastTickTime=start;
    let st=state();
    let total=0;
    while (running && !st.halted && !breakpoints.has(st.pc)) {
      if (performance.now()-start>8) break;
      let count=2000;
      if (clock>0) {
        const allowed=(performance.now()-epochTime)*clock/1000+epochCycles;
        const delta=allowed-st.cycles;
        if (delta<=0) break;
        count=Math.max(1,Math.min(2000,Math.floor(delta/10)));
      }
      const n=wasm._z80_run_slice(count,-1);
      if (n<0) throw new Error(asText(wasm._z80_last_error()));
      total+=n;
      st=state();
      if (!n || total>=15000) break;
    }
    if (st.halted || breakpoints.has(st.pc)) {
      stop();
      stopReason=st.halted?'halted':'breakpoint';
      if (stopReason==='breakpoint') info(`Breakpoint reached at $${hex4(st.pc)}.`);
      snapshot(); return;
    }
    if (performance.now()-lastSnapshot>=80) snapshot();
    scheduled=setTimeout(tick,clock===0?0:2);
  } catch(error) {
    stop();stopReason='error';sendError(error);snapshot();
  }
}
function allocBytes(bytes, callback) {
  const ptr=wasm._malloc(bytes.length||1);
  if (!ptr) throw new Error('Out of WASM memory');
  try {wasm.HEAPU8.set(bytes,ptr);return callback(ptr);}
  finally {wasm._free(ptr);}
}
function setBreakpoint(address, enabled) {
  if (!validAddress(address)) throw new Error('Invalid breakpoint address');
  const result=wasm._z80_set_breakpoint(address,enabled?1:0);
  if (result<0) throw new Error(asText(wasm._z80_last_error()));
  if (enabled) breakpoints.add(address); else breakpoints.delete(address);
}
function acceptMessage(event) {
  const msg=event.data;
  try {
    switch(msg.type) {
      case 'session-save': {
        // Worker messages are serialised: stop before copying CPU and UART state.
        const previousReason=stopReason;
        stop();
        stopReason=previousReason==='breakpoint'?'breakpoint':'paused';
        snapshot(); // First flush any pending serial output to the UI.
        const length=wasm._z80_snapshot_size();
        if(length<0)throw new Error(asText(wasm._z80_last_error()));
        const ptr=wasm._malloc(length);
        if(!ptr)throw new Error('Out of WASM memory');
        let bytes;
        try {
          const written=wasm._z80_snapshot_save(ptr,length);
          if(written!==length)throw new Error(asText(wasm._z80_last_error())||'Snapshot length changed');
          bytes=wasm.HEAPU8.slice(ptr,ptr+length);
        } finally {wasm._free(ptr);}
        postMessage({type:'session-data',requestId:msg.requestId,bytes:bytes.buffer,
          debugger:{clock,breakpoints:[...breakpoints].sort((a,b)=>a-b),
            memoryAddress,disasmAddress,stopReason}},[bytes.buffer]);
        break;
      }
      case 'session-restore': {
        const d=msg.debugger;
        if(!d||typeof d!=='object'||!validAddress(d.memoryAddress)||
          (d.disasmAddress!==null&&!validAddress(d.disasmAddress))||
          ![0,2_000_000,4_000_000,6_000_000,8_000_000].includes(d.clock)||
          !Array.isArray(d.breakpoints)||d.breakpoints.length>64||
          d.breakpoints.some(x=>!validAddress(x))||
          new Set(d.breakpoints).size!==d.breakpoints.length)
          throw new Error('Invalid session debugger settings');
        const bytes=new Uint8Array(msg.bytes);
        if(bytes.length<65868||bytes.length>1200000)throw new Error('Invalid CPU snapshot size');
        // Native restoration validates the whole snapshot transactionally.
        const result=allocBytes(bytes,p=>wasm._z80_snapshot_restore(p,bytes.length));
        if(result<0)throw new Error(asText(wasm._z80_last_error()));
        stop();
        clock=d.clock;
        memoryAddress=d.memoryAddress;
        disasmAddress=d.disasmAddress;
        disasmHistory=[];
        stopReason=d.stopReason==='breakpoint'?'breakpoint':'restored';
        breakpoints.clear();
        for(const address of d.breakpoints)setBreakpoint(address,true);
        resetEpoch();
        postMessage({type:'session-restored',requestId:msg.requestId});
        snapshot();
        break;
      }
      case 'load': {
        stop();
        const bytes=new Uint8Array(msg.bytes);
        const result=allocBytes(bytes,p=>wasm._z80_load(p,bytes.length,msg.format,msg.origin));
        if(result<0) throw new Error(asText(wasm._z80_last_error()));
        breakpoints.clear(); disasmAddress=null; disasmHistory=[];
        stopReason='';
        postMessage({type:'loaded',count:result});snapshot();break;
      }
      case 'reset':
        stop();wasm._z80_reset();disasmAddress=null;disasmHistory=[];
        stopReason='';snapshot();break;
      case 'step': {
        stop();const n=wasm._z80_step();
        if(n<0) throw new Error(asText(wasm._z80_last_error()));
        stopReason=state().halted?'halted':'step';snapshot();break;
      }
      case 'run': {
        if(running) break;
        let st=state();
        if(st.halted) {snapshot();break;}
        // Skip one instruction only when resuming a breakpoint stop.
        // If the user presses Run at a newly set breakpoint (including at
        // the entry PC), the breakpoint must fire BEFORE its instruction.
        if(stopReason==='breakpoint' && breakpoints.has(st.pc)) {
          const n=wasm._z80_step();
          if(n<0) throw new Error(asText(wasm._z80_last_error()));
          st=state();
          if(st.halted) {stopReason='halted';snapshot();break;}
        }
        running=true;stopReason='';resetEpoch();snapshot();
        scheduled=setTimeout(tick,0);break;
      }
      case 'pause':stop();stopReason='paused';snapshot();break;
      case 'clock':
        if(!Number.isFinite(msg.value)||msg.value<0||msg.value>1_000_000_000) throw new Error('Invalid clock rate');
        clock=msg.value;if(running)resetEpoch();snapshot();break;
      case 'set-pc': {
        if (!validAddress(msg.address)) throw new Error('Invalid PC address');
        stop();
        const result=wasm._z80_set_pc(msg.address);
        if(result<0) throw new Error(asText(wasm._z80_last_error()));
        disasmAddress=null;disasmHistory=[];stopReason='set-pc';
        info(`PC set to $${hex4(msg.address)}.`);snapshot();break;
      }
      case 'breakpoint-add':case 'breakpoint-remove':case 'breakpoint-toggle': {
        let enabled=msg.type==='breakpoint-add';
        if(msg.type==='breakpoint-toggle') enabled=!breakpoints.has(msg.address);
        setBreakpoint(msg.address,enabled);
        snapshot();break;
      }
      case 'breakpoint-clear':
        wasm._z80_clear_breakpoints();breakpoints.clear();snapshot();break;
      case 'memory':
        if(!validAddress(msg.address)) throw new Error('Invalid memory address');
        memoryAddress=msg.address;snapshot();break;
      case 'write-memory': {
        if(!validAddress(msg.address)) throw new Error('Invalid memory address');
        const bytes=new Uint8Array(msg.bytes);
        if(bytes.length<1||bytes.length>128||msg.address+bytes.length>65536)
          throw new Error('Enter 1–128 bytes without crossing $FFFF');
        stop();
        for(let i=0;i<bytes.length;i++) {
          if(wasm._z80_write_byte(msg.address+i,bytes[i])<0) throw new Error('Memory write failed');
        }
        memoryAddress=msg.address&0xfff0;
        stopReason='edited';
        info(`Wrote ${bytes.length} byte${bytes.length===1?'':'s'} at $${hex4(msg.address)}.`);
        snapshot();break;
      }
      case 'disasm-goto':
        if(!validAddress(msg.address)) throw new Error('Invalid disassembly address');
        disasmHistory=[];disasmAddress=msg.address;snapshot();break;
      case 'disasm-follow':disasmAddress=null;disasmHistory=[];snapshot();break;
      case 'disasm-next':
        disasmHistory.push(disasmAddress);
        if(disasmHistory.length>50)disasmHistory.shift();
        disasmAddress=nextDisasmAddress;snapshot();break;
      case 'disasm-back':
        if(disasmHistory.length)disasmAddress=disasmHistory.pop();
        snapshot();break;
      case 'input': {
        const data=new Uint8Array(msg.bytes);
        for(let i=0;i<data.length;i+=4096) {
          const part=data.subarray(i,i+4096);
          const count=allocBytes(part,p=>wasm._z80_queue_input(p,part.length));
          if(count<0) throw new Error(asText(wasm._z80_last_error()));
        }
        if(!running)snapshot();break;
      }
      default:throw new Error('Unknown worker operation');
    }
  } catch(e) {sendError(e);}
}
async function boot() {
  try {
    wasm=await createZ80({locateFile:filename=>new URL(filename,import.meta.url).href});
    if(!wasm.HEAPU8||!wasm._z80_init||!wasm._z80_set_breakpoint||!wasm._z80_set_pc||
      !wasm._z80_snapshot_save||!wasm._z80_snapshot_restore)
      throw new Error('Missing exported WebAssembly API');
    wasm._z80_init();
    onmessage=acceptMessage;
    postMessage({type:'ready'});
    snapshot();
  } catch(e) {sendError(e);}
}
void boot();
