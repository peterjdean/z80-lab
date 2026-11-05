import {build} from 'esbuild';
import {mkdir} from 'node:fs/promises';
import {resolve} from 'node:path';
const output=resolve(process.argv[2] || 'build-wasm/site/assembler.js');
await mkdir(resolve(output,'..'),{recursive:true});
await build({
  entryPoints:['web/assembler-source.js'], outfile:output,
  bundle:true, platform:'browser', format:'esm', target:['safari15','chrome90','firefox90'],
  minify:true, legalComments:'eof', logLevel:'info', sourcemap:false
});
console.log('Bundled MIT-licensed ASM80 Z80 assembler:',output);
