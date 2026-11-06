import test from 'node:test';
import assert from 'node:assert/strict';
import {makeSourceLineLookup,sourceStepOutcome,SOURCE_STEP_LIMIT} from '../web/source-step.mjs';

const entries=[{address:0x100,line:2},{address:0x102,line:2},{address:0x104,line:3}];
const lines=makeSourceLineLookup(entries);
const decide=(pc,other={})=>sourceStepOutcome({startLine:2,addressToLine:lines,pc,
  halted:false,breakpoints:new Set(),steps:1,...other});
test('maps only source instruction start addresses',()=>{
  assert.equal(lines.get(0x100),2);
  assert.equal(lines.get(0x101),undefined);
  assert.equal(lines.get(0x104),3);
});
test('continues within one source line and stops at next line',()=>{
  assert.equal(decide(0x102),null);
  assert.equal(decide(0x104),'source-step');
});
test('breakpoint before the next source statement takes precedence',()=>{
  assert.equal(decide(0x102,{breakpoints:new Set([0x102])}),'breakpoint');
  assert.equal(decide(0x104,{breakpoints:new Set([0x104])}),'breakpoint');
});
test('halts and unmapped branches stop safely',()=>{
  assert.equal(decide(0x104,{halted:true}),'halted');
  assert.equal(decide(0x200),'unmapped');
  assert.equal(decide(0x102,{startLine:undefined}),'unmapped');
});
test('bounded loop on same source line',()=>{
  assert.equal(decide(0x100,{steps:SOURCE_STEP_LIMIT-1}),null);
  assert.equal(decide(0x100,{steps:SOURCE_STEP_LIMIT}),'source-step-limit');
});
test('rejects corrupt, duplicate and out-of-range source mappings',()=>{
  assert.throws(()=>makeSourceLineLookup([{address:-1,line:2}]),/Invalid/);
  assert.throws(()=>makeSourceLineLookup([{address:256,line:0}]),/Invalid/);
  assert.throws(()=>makeSourceLineLookup([{address:256,line:2},{address:256,line:4}]),/duplicate/);
  assert.throws(()=>makeSourceLineLookup('bad'),/Invalid/);
});
