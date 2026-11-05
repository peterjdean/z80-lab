// ASM80 is a third-party MIT-licensed Z80 assembler, built into this site's
// own static JS bundle at release time; no browser CDN/network dependency.
import {compile} from '@asm80/core';
import {emitIntelHex, formatAssemblyError} from './assembler-core.mjs';
export {emitIntelHex, formatAssemblyError};
export async function assembleZ80(source) {
  if (typeof source !== 'string' || source.length>200_000)
    throw new Error('Assembly source must be text and at most 200 KB.');
  // v0.6 is single-file only. The compiler must never fetch URLs or files.
  const noFiles = {
    readFile: async () => { throw {msg:'INCLUDE/INCBIN is not supported in v0.6; keep the program in one file.'}; },
    resolvePath: name => name,
    childOpts: () => ({})
  };
  let result;
  try {
    result = await compile(source,noFiles,{assembler:'z80'},'main.z80');
  } catch(error) {throw new Error(formatAssemblyError(error));}
  return emitIntelHex(result);
}
