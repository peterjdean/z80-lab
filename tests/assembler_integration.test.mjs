// Requires npm install. This test uses the actual third-party assembler, never
// a hand-written instruction encoder; run on Euler before browser testing.
import test from 'node:test';
import assert from 'node:assert/strict';
import {assembleZ80} from '../web/assembler-source.js';
function actualBytes(hex){
  const m=new Map();for(const line of hex.trim().split('\n')){
    const buf=Buffer.from(line.slice(1),'hex');if(buf[3]!==0)continue;
    const addr=buf.readUInt16BE(1);
    for(let i=0;i<buf[0];i++)m.set(addr+i,buf[4+i]);
  }return m;
}
test('ASM80 assembles print A and HALT at ORG $0100',async()=>{
  const result=await assembleZ80('        ORG $0100\n        LD A,$41\n        OUT ($00),A\n        HALT\n');
  const m=actualBytes(result.hex);
  assert.equal(result.entry,0x100);
  assert.deepEqual([0,1,2,3,4].map(i=>m.get(0x100+i)),[0x3e,0x41,0xd3,0x00,0x76]);
});
test('ASM80 resolves forward labels and JR displacement',async()=>{
  const result=await assembleZ80('        ORG $0100\n        LD B,3\nLOOP:   DJNZ LOOP\n        HALT\n');
  const m=actualBytes(result.hex);
  assert.deepEqual([0,1,2,3,4].map(i=>m.get(0x100+i)),[0x06,0x03,0x10,0xfe,0x76]);
});
test('ASM80 rejects unknown instructions and does not return code',async()=>{
  await assert.rejects(()=>assembleZ80('        ORG $0100\n        FOO A,B\n'),Error);
});

test('ASM80 emits source-line metadata for source debugging',async()=>{
  const program=`        ORG $0100
        LD B,16
        LD A,'A'
LOOP:   OUT ($00),A
        INC A
        DJNZ LOOP
        HALT
`;
  const result=await assembleZ80(program);
  assert.equal(result.sourceMap.addressToLine[0x100],2);
  assert.equal(result.sourceMap.addressToLine[0x102],3);
  assert.equal(result.sourceMap.addressToLine[0x104],4);
  assert.equal(result.sourceMap.addressToLine[0x105],undefined); // Port operand
  assert.equal(result.sourceMap.addressToLine[0x106],5);
  assert.equal(result.sourceMap.addressToLine[0x107],6);
  assert.equal(result.sourceMap.addressToLine[0x108],undefined); // DJNZ offset
  assert.equal(result.sourceMap.addressToLine[0x109],7);
  assert.deepEqual(result.sourceMap.lineToAddresses[5],[0x106]);
});
