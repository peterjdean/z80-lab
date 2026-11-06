// Exercise the real worker command handling with mocked WebAssembly instructions.
// Tests stepping semantics and cooperative scheduling, not Z80 CPU emulation.
const test=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const {join}=require('node:path');
const vm=require('node:vm');
const {pathToFileURL}=require('node:url');
const root=join(__dirname,'..');
async function harness(jumps,haltAt=null){
  const helpers=await import(pathToFileURL(join(root,'web/source-step.mjs')).href);
  const events=[];
  const pending=new Map();let serial=0;
  const cpu={pc:256,cycles:0,halted:false,instructions:0};
  const wasm={
    UTF8ToString:ptr=>ptr==='state'?JSON.stringify(cpu):ptr==='dis'?'NOP':'',
    HEAPU8:new Uint8Array(1000000),
    _z80_init:()=>0,_z80_set_breakpoint:()=>0,_z80_set_pc:()=>0,
    _z80_snapshot_save:()=>0,_z80_snapshot_restore:()=>0,
    _z80_state_json:()=> 'state',_z80_disassembly:()=> 'dis',_z80_disassembly_length:()=>1,
    _z80_output_ptr:()=>0,_z80_output_length:()=>0,_z80_read_byte:()=>0,
    _z80_step:()=>{
      cpu.instructions++;
      cpu.pc=jumps(cpu.pc);
      cpu.cycles+=4;
      if(haltAt!==null&&cpu.pc===haltAt)cpu.halted=true;
      return 1;
    }
  };
  const source=readFileSync(join(root,'web/worker.js'),'utf8')
    .replace(/^import .*;\n/gm,'')
    .replace('new URL(filename,import.meta.url).href','filename');
  const context={...helpers,createZ80:async()=>wasm,Uint8Array,Set,Map,URL,
    performance:{now:()=>0},
    postMessage:message=>events.push(message),
    setTimeout:fn=>{const id=++serial;pending.set(id,fn);return id;},
    clearTimeout:id=>pending.delete(id),
    onmessage:null,console};
  vm.runInNewContext(source,context,{filename:'worker.js'});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(events[0]?.type,'ready',JSON.stringify(events));
  return {
    context,cpu,events,
    send:msg=>context.onmessage({data:msg}),
    flush:(limit=100)=>{
      for(let i=0;i<limit&&pending.size;i++){
        const [id,fn]=pending.entries().next().value;
        pending.delete(id);fn();
      }
      assert.equal(pending.size,0,'worker must finish within the scheduled tick limit');
    },
    flushOne:()=>{const [id,fn]=pending.entries().next().value;pending.delete(id);fn();},
  };
}
const entries=[{address:256,line:2},{address:258,line:2},{address:260,line:3}];
test('source step advances across two machine instructions of one source line',async()=>{
  const h=await harness(pc=>pc+2);
  h.send({type:'source-step',entries});h.flush();
  assert.equal(h.cpu.pc,260);
  assert.equal(h.cpu.instructions,2);
  assert.equal(h.events.at(-1).stopReason,'source-step');
});
test('source step stops before breakpoint on same line',async()=>{
  const h=await harness(pc=>pc+2);
  h.send({type:'breakpoint-add',address:258});
  h.send({type:'source-step',entries});h.flush();
  assert.equal(h.cpu.pc,258);assert.equal(h.cpu.instructions,1);
  assert.equal(h.events.at(-1).stopReason,'breakpoint');
});
test('starting on an existing breakpoint steps into the next source line',async()=>{
  const h=await harness(pc=>pc+2);
  h.send({type:'breakpoint-add',address:256});
  h.send({type:'source-step',entries});h.flush();
  assert.equal(h.cpu.pc,260);
  assert.equal(h.cpu.instructions,2);
  assert.equal(h.events.at(-1).stopReason,'source-step');
});
test('invalid source maps are rejected without touching CPU state',async()=>{
  const h=await harness(pc=>pc+2);
  h.send({type:'source-step',entries:[{address:-1,line:2}]});
  assert.equal(h.cpu.pc,256);assert.equal(h.cpu.instructions,0);
  assert.equal(h.events.at(-1).type,'error');
});
test('step over HALT terminates immediately',async()=>{
  const h=await harness(pc=>pc+2,258);
  h.send({type:'source-step',entries});h.flush();
  assert.equal(h.cpu.pc,258);assert.equal(h.events.at(-1).stopReason,'halted');
});
test('outside source map falls back to one CPU instruction',async()=>{
  const h=await harness(pc=>pc+2);h.cpu.pc=0x200;
  h.send({type:'source-step',entries});
  assert.equal(h.cpu.pc,0x202);assert.equal(h.cpu.instructions,1);
  assert.equal(h.events.at(-1).stopReason,'unmapped');
});
test('tight same-line branch stops at hard instruction limit',async()=>{
  const h=await harness(pc=>pc);
  h.send({type:'source-step',entries});h.flush();
  assert.equal(h.cpu.instructions,helpersLimit());
  assert.equal(h.events.at(-1).stopReason,'source-step-limit');
});
function helpersLimit(){return 20_000;}
test('Pause interrupts in-progress cooperative source step',async()=>{
  const h=await harness(pc=>pc);
  h.send({type:'source-step',entries});h.flushOne();
  assert.ok(h.cpu.instructions>0&&h.cpu.instructions<20_000);
  h.send({type:'pause'});h.flush();
  assert.equal(h.events.at(-1).stopReason,'paused');
});
