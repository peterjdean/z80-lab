// Tests the real worker.js control flow against an instrumented CPU mock.
// It does not replace a WebAssembly/Safari integration test.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const sourcePath = path.join(__dirname, '..', 'web', 'worker.js');
let source = fs.readFileSync(sourcePath, 'utf8');
source = source.replace(/^import createZ80 from '.\/z80wasm\.mjs';/m,
                        'const createZ80 = async () => mockWasm;');
source = source.replace(/import\.meta\.url/g, "'file:///z80lab/worker.js'");

const code = [
  [0x100, 0x103, 10, 'LD HL,$2000'],
  [0x103, 0x105, 7,  'LD A,$41'],
  [0x105, 0x106, 7,  'LD (HL),A'],
  [0x106, 0x107, 6,  'INC HL'],
  [0x107, 0x108, 4,  'INC A'],
  [0x108, 0x109, 7,  'LD (HL),A'],
  [0x109, 0x10a, 4,  'HALT'],
];
const instructions = new Map(code.map(([pc,next,cycles,label]) => [pc,{next,cycles,label}]));
let pc = 0, halted = false, count = 0, cycles = 0;
const bps = new Set();
const mem = new Uint8Array(65536);
const heap = new Uint8Array(100000);
const messages = [];
let nextString = 2;
const strings = new Map();
function ptr(s) { const p=nextString++; strings.set(p,s); return p; }
function state() { return {pc,halted,instructions:count,cycles,sp:0xfffe}; }
function oneStep() {
  if (halted) return 0;
  const instruction = instructions.get(pc);
  assert.ok(instruction, `unexpected execution at PC=$${pc.toString(16)}`);
  if (pc === 0x105) mem[0x2000] = 0x41;
  if (pc === 0x108) mem[0x2001] = 0x42;
  pc=instruction.next; cycles+=instruction.cycles; count++;
  if (instruction.label==='HALT') halted=true;
  return 1;
}
const mockWasm = {
  HEAPU8:heap,
  UTF8ToString(p){return strings.get(p) || '';},
  _malloc(){return 500;}, _free(){},
  _z80_init(){pc=0;halted=false;count=0;cycles=0;bps.clear();},
  _z80_load(){pc=0x100;halted=false;count=0;cycles=0;bps.clear();mem.fill(0);return 10;},
  _z80_set_breakpoint(address, enabled){if (enabled) bps.add(address); else bps.delete(address);return 0;},
  _z80_clear_breakpoints(){bps.clear();},
  _z80_set_pc(address){if(address<0||address>0xffff)return -1;pc=address;halted=false;return 0;},
  _z80_state_json(){return ptr(JSON.stringify(state()));},
  _z80_output_ptr(){return 0;}, _z80_output_length(){return 0;},
  _z80_disassembly(address){return ptr(`${address.toString(16)} ${instructions.get(address)?.label || 'NOP'}`);},
  _z80_disassembly_length(address){const x = instructions.get(address);return x ? x.next-address : 1;},
  _z80_read_byte(address){return mem[address];},
  _z80_run_slice(n){let executed=0; while(executed<n && !halted && !bps.has(pc)){oneStep();executed++;}return executed;},
  _z80_step:oneStep,
  _z80_last_error(){return ptr('mock error');},
};

const context = {
  mockWasm, URL, Uint8Array, Set, JSON, Math, Number, String, Error,
  performance, setTimeout, clearTimeout,
  postMessage(message){messages.push(message);},
};
vm.createContext(context);
vm.runInContext(source, context, {filename:'worker.js'});
const sleep = ms => new Promise(r => setTimeout(r,ms));
function send(data) {context.onmessage({data});}
async function waitFor(predicate, label) {
  for(let i=0;i<250;i++) {
    const s = messages.filter(m=>m.type==='state').at(-1);
    if(s && predicate(s)) return s;
    await sleep(5);
  }
  const s=messages.filter(m=>m.type==='state').at(-1);
  throw new Error(`Timeout waiting for ${label}; last state: ${JSON.stringify(s?.state)} reason: ${s?.stopReason}`);
}
async function load(){
  send({type:'load',bytes:new Uint8Array(10).buffer,format:1,origin:-1});
  await waitFor(s=>s.state.pc===0x100 && !s.running, 'load');
}
async function main() {
  for(let i=0;i<50&&!context.onmessage;i++)await sleep(5);
  assert.equal(typeof context.onmessage, 'function', 'worker did not start');
  await load();
  send({type:'breakpoint-add',address:0x103});
  send({type:'breakpoint-add',address:0x108});
  send({type:'run'});
  let s=await waitFor(s=>s.stopReason==='breakpoint' && s.state.pc===0x103 && !s.running, '$0103 breakpoint');
  assert.deepEqual([...s.breakpoints], [0x103,0x108]);
  assert.equal(s.state.instructions,1);
  send({type:'run'});
  s=await waitFor(s=>s.stopReason==='breakpoint' && s.state.pc===0x108 && !s.running, '$0108 breakpoint');
  assert.equal(s.state.instructions,5);
  assert.equal(s.state.pc,0x108);
  assert.equal(mem[0x2001],0);
  send({type:'run'});
  s=await waitFor(s=>s.stopReason==='halted' && s.state.halted, 'HALT');
  assert.equal(s.state.instructions,7);
  assert.equal(mem[0x2001],0x42);
  await load();
  send({type:'breakpoint-add',address:0x100});
  send({type:'run'});
  s=await waitFor(s=>s.stopReason==='breakpoint' && s.state.pc===0x100 && !s.running, 'entry PC breakpoint');
  assert.equal(s.state.instructions,0, 'Run must not silently skip a newly set breakpoint');
  send({type:'set-pc',address:0x108});
  s=await waitFor(s=>s.state.pc===0x108 && s.stopReason==='set-pc','Set PC');
  assert.equal(s.state.halted,false);
  console.log('PASS: $0103 -> $0108 -> HALT; initial PC breakpoint; Set PC');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
