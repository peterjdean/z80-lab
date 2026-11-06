import {test} from 'node:test';
import assert from 'node:assert/strict';
import {encodeSession,decodeSession} from '../web/session-format.mjs';

function fixture() {
  const bytes=new Uint8Array(65868);
  bytes.set([90,56,76,83,1,0]); // Z8LS v1
  new DataView(bytes.buffer).setUint32(6,bytes.length,true);
  bytes[12+256]=0x42;
  return {bytes,debugger:{clock:4_000_000,memoryAddress:8192,
     disasmAddress:null,breakpoints:[256,260],stopReason:'breakpoint'},
    workspace:{source:'ORG $0100\nLD A,1\nHALT\n',serial:'Hello\n',mapActive:true,
      assembled:{source:'ORG $0100\nLD A,1\nHALT\n',hex:':00000001FF\n',listing:'0100 LD A,1',
        count:3,entry:256,sourceMap:{entries:[{address:256,line:2,length:2},
          {address:258,line:3,length:1}]}}}};
}
test('round trip preserves binary, settings, serial, source and mapped addresses',()=>{
  const state=fixture();
  const decoded=decodeSession(encodeSession(state));
  assert.deepEqual(decoded.bytes,state.bytes);
  assert.deepEqual(decoded.debugger,state.debugger);
  assert.equal(decoded.workspace.source,state.workspace.source);
  assert.equal(decoded.workspace.serial,'Hello\n');
  assert.equal(decoded.workspace.assembled.sourceMap.addressToLine[256],2);
  assert.deepEqual(decoded.workspace.assembled.sourceMap.lineToAddresses[2],[256]);
});
test('rejects invalid formats and malformed CPU snapshot without restoring',()=>{
  const text=encodeSession(fixture());
  const tamper=modifier=>JSON.stringify(modifier(JSON.parse(text)));
  assert.throws(()=>decodeSession(tamper(s=>{s.version=999;return s;})),/Unsupported/);
  assert.throws(()=>decodeSession(tamper(s=>{s.cpu='AAAA';return s;})),/length/);
  assert.throws(()=>decodeSession(tamper(s=>{s.cpu='?';return s;})),/encoding/);
  assert.throws(()=>decodeSession(tamper(s=>{s.debugger.breakpoints=[-1];return s;})),/debugger/);
  assert.throws(()=>decodeSession(tamper(s=>{s.workspace.assembled.sourceMap.entries[0].line=500;return s;})),/source-map/);
  assert.throws(()=>decodeSession(tamper(s=>{s.workspace.mapActive=true;s.workspace.assembled=null;return s;})),/source map/);
});
test('session may have source without assembled code and unchanged UART',()=>{
  const initial=fixture();
  initial.workspace.assembled=null;
  initial.workspace.mapActive=false;
  const restored=decodeSession(encodeSession(initial));
  assert.equal(restored.workspace.assembled,null);
  assert.equal(restored.workspace.mapActive,false);
});
