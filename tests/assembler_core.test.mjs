import test from 'node:test';
import assert from 'node:assert/strict';
import {emitIntelHex,formatAssemblyError} from '../web/assembler-core.mjs';
function decodeHex(text){
  const mem=new Map();let entry=-1, eof=false;
  for(const line of text.trim().split('\n')){
    assert.ok(/^:[0-9A-F]+$/.test(line));
    const data=Buffer.from(line.slice(1),'hex');
    assert.equal(data.reduce((a,b)=>a+b,0)&255,0);
    const len=data[0],addr=data.readUInt16BE(1),kind=data[3];
    assert.equal(data.length,len+5);
    if(kind===0)for(let i=0;i<len;i++)mem.set(addr+i,data[4+i]);
    else if(kind===5)entry=data.readUInt32BE(4);
    else if(kind===1)eof=true;
  }
  assert.ok(eof);return {mem,entry};
}
test('assemble listing bytes with two discontiguous ORG regions',()=>{
  const result=emitIntelHex({dump:[
    {addr:0x100,lens:[0x3e,0x41,0xd3,0x00,0x76],opcode:'LD',params:['A','$41']},
    {addr:0x2000,lens:[0x48,0x49],opcode:'DB',params:['$48','$49']}
  ],opts:{}});
  const parsed=decodeHex(result.hex);
  assert.equal(result.count,7);
  assert.equal(result.entry,0x100);
  assert.equal(parsed.entry,0x100);
  assert.deepEqual([...parsed.mem.keys()], [0x100,0x101,0x102,0x103,0x104,0x2000,0x2001]);
  assert.match(result.listing,/0100  3E 41 D3 00 76/);
});
test('honour explicit END entry and skip conditional blocks',()=>{
  const result=emitIntelHex({dump:[{addr:0x3000,lens:[0xaa],ifskip:true},{addr:0x400,lens:[0x76]}], opts:{ENT:0x400}});
  const parsed=decodeHex(result.hex);
  assert.equal(parsed.entry,0x400);assert.equal(parsed.mem.size,1);
});
test('reject malformed output and overlapping addresses',()=>{
  assert.throws(()=>emitIntelHex({dump:[]}),/No machine-code/);
  assert.throws(()=>emitIntelHex({dump:[{addr:0xffff,lens:[1,2]}]}),/beyond/);
  assert.throws(()=>emitIntelHex({dump:[{addr:0x200,lens:[1,2]},{addr:0x201,lens:[3]}]}),/Overlapping/);
  assert.throws(()=>emitIntelHex({dump:[{addr:0x100,lens:[1,Infinity]}]}),/invalid byte/);
});
test('error text preserves assembler line numbers',()=>{
  assert.match(formatAssemblyError({error:{msg:'Invalid mnemonic',wline:{numline:12}}}),/Line 12: Invalid mnemonic/);
});

test('source map associates only emitted instruction starts with source lines',()=>{
  const result=emitIntelHex({dump:[
    {numline:1,opcode:'ORG',addr:0x100,lens:[]},
    {numline:2,addr:0x100,lens:[0x21,0x00,0x20],opcode:'LD'},
    {numline:3,addr:0x103,lens:[0x3e,0x41],opcode:'LD'},
    {numline:4,addr:0x105,lens:[0x28,0x05],opcode:'JR'},
    {numline:5,addr:0x107,lens:[0xd3,0x00],opcode:'OUT'},
    {numline:5,addr:0x109,lens:[0x23],opcode:'INC'},
    {numline:6,addr:0x10a,lens:[0x76],opcode:'HALT'}
  ], opts:{}});
  assert.equal(result.sourceMap.addressToLine[0x107],5);
  assert.equal(result.sourceMap.addressToLine[0x108],undefined);
  assert.deepEqual(result.sourceMap.lineToAddresses[5],[0x107,0x109]);
  assert.equal(result.sourceMap.lineToAddresses[1],undefined);
  assert.deepEqual(result.sourceMap.entries[3],{address:0x107,line:5,length:2});
});
test('source map supports discontinuous regions and missing or included-line metadata',()=>{
  const result=emitIntelHex({dump:[
    {numline:2,addr:0x100,lens:[0x76]},
    {numline:9,addr:0x2000,lens:[0x48,0x49]},
    {addr:0x3000,lens:[0x00]},
    {numline:12,includedFile:'other.asm',addr:0x3001,lens:[0x00]}
  ],opts:{}});
  assert.deepEqual(result.sourceMap.lineToAddresses[9],[0x2000]);
  assert.equal(result.sourceMap.addressToLine[0x3000],undefined);
  assert.equal(result.sourceMap.addressToLine[0x3001],undefined);
});
