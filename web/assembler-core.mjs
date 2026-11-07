// Dependency-neutral conversion from ASM80's documented per-line assembler
// result to strict Intel HEX records understood by our existing Z80 loader.
// Does NOT emulate the CPU or implement an assembler.
export function emitIntelHex(result) {
  if (!result || !Array.isArray(result.dump)) throw new Error('Unexpected assembler result (no listing).');
  const memory = new Map();
  let firstAddress = null;
  const listing = [];
  // Debug map is valid for this one assembled source only. The address keys
  // refer to first bytes of emitted rows, never arbitrary bytes within an
  // instruction (important for multi-byte Z80 instructions).
  const addressToLine = Object.create(null);
  const lineToAddresses = Object.create(null);
  const entries = [];
  for (const row of result.dump) {
    if (row.ifskip || !row.lens || row.lens.length === 0) continue;
    const start = Number(row.addr);
    if (!Number.isInteger(start) || start < 0 || start > 65535)
      throw new Error(`Invalid assembled address: ${row.addr}`);
    if (firstAddress === null) firstAddress = start;
    const bytes = [...row.lens];
    for (let i=0;i<bytes.length;i++) {
      const address = start + i;
      const value = bytes[i];
      if (address>65535) throw new Error(`Output extends beyond $FFFF at $${address.toString(16).toUpperCase()}.`);
      if (!Number.isInteger(value) || value<0 || value>255)
        throw new Error(`Assembler returned an invalid byte at $${address.toString(16).toUpperCase()}.`);
      if (memory.has(address)) throw new Error(`Overlapping output at $${address.toString(16).toUpperCase().padStart(4,'0')}.`);
      memory.set(address,value);
    }
    const line = Number(row.numline);
    if (Number.isInteger(line) && line > 0 && !row.includedFile) {
      // A single source line can expand into multiple emitted rows.
      if (addressToLine[start] === undefined) addressToLine[start] = line;
      if (!lineToAddresses[line]) lineToAddresses[line] = [];
      if (!lineToAddresses[line].includes(start)) lineToAddresses[line].push(start);
      entries.push({address:start, line, length:bytes.length});
    }
    const label = row.label ? `${row.label}: ` : '';
    const params=Array.isArray(row.params)?row.params.join(', '):String(row.params ?? '');
    const operation = row.opcode ? `${row.opcode} ${params}` : '';
    // Keep the hex column a fixed width. Long DB/DW rows continue on separate
    // addressed lines, with the source statement shown only on the first row.
    // This changes presentation only; Intel HEX and source mapping use all bytes.
    for (let offset=0;offset<bytes.length;offset+=8) {
      const chunk=bytes.slice(offset,offset+8);
      const hex=chunk.map(x=>x.toString(16).toUpperCase().padStart(2,'0')).join(' ');
      const address=(start+offset).toString(16).toUpperCase().padStart(4,'0');
      const sourceText=offset===0 ? `${label}${operation}` : '';
      listing.push(`${address}  ${hex.padEnd(24)}  ${sourceText}`.trimEnd());
    }
  }
  if (memory.size===0) throw new Error('No machine-code bytes were generated.');
  // The first emitted byte is the default entry. Optionally honour an explicit
  // END entry if the assembler provides a 16-bit numeric ENT value.
  const explicit = result.opts?.ENT;
  const entry = Number.isInteger(explicit) && explicit>=0 && explicit<=65535 ? explicit : firstAddress;
  const record = (address,type,data) => {
    const values = [data.length,address>>>8,address&255,type,...data];
    values.push((-values.reduce((a,b)=>a+b,0))&255);
    return ':'+values.map(v=>v.toString(16).toUpperCase().padStart(2,'0')).join('');
  };
  const addresses = [...memory.keys()].sort((a,b)=>a-b);
  const records=[];
  for(let index=0;index<addresses.length;) {
    const start=addresses[index];
    const data=[memory.get(start)];index++;
    while(index<addresses.length && data.length<16 && addresses[index]===start+data.length) {
      data.push(memory.get(addresses[index]));index++;
    }
    records.push(record(start,0,data));
  }
  // Type 05 start-linear address is supported by our Intel HEX loader.
  // Our machine has only 16-bit addresses, so the high word is zero.
  records.push(record(0,5,[0,0,entry>>>8,entry&255]));
  records.push(record(0,1,[]));
  return {hex:records.join('\n')+'\n', entry, count:memory.size,
    listing:listing.join('\n'), sourceMap:{addressToLine,lineToAddresses,entries}};
}

// Prefer ASM80's structured source location. Its unknown-instruction error can
// lack usable line metadata, however, so resolve that specific opcode only if
// it occurs on exactly one actual instruction line in this single-file source.
// Never guess between duplicate occurrences or parse arbitrary message text.
function uniqueOpcodeLine(message, source) {
  if (typeof source !== 'string') return null;
  const found = /(?:unrecognized|unknown)\s+(?:instruction|opcode)\s+([A-Za-z_.@$][\w.$@]*)/i.exec(message);
  if (!found) return null;
  const wanted = found[1].toUpperCase();
  let unique = null;
  for (const [index, raw] of source.split(/\r?\n/).entries()) {
    // Ignore comments; allow an optional label preceding the instruction.
    const text = raw.trimStart();
    if (!text || text.startsWith(';')) continue;
    const candidate = /^(?:[A-Za-z_.@$][\w.$@]*:\s*)?([A-Za-z_.@$][\w.$@]*)\b/.exec(text);
    if (candidate?.[1]?.toUpperCase() !== wanted) continue;
    if (unique !== null) return null; // Ambiguous, do not fabricate a location.
    unique = index + 1;
  }
  return unique;
}

export function extractAssemblyDiagnostics(error, source=null) {
  const items=error?.errors || (error?.error ? [error.error] : [error]);
  return items.map(item=>{
    const rawLine=item?.wline?.numline ?? item?.s?.numline ??
      item?.location?.line ?? item?.lineNumber ?? item?.wline?.line ?? item?.line;
    const rawMessage=item?.msg || item?.message || String(item);
    const text=String(rawMessage);
    const prefix=text.match(/^Line\s+(\d+):\s*/i);
    const numbered=Number(rawLine);
    const known=Number.isInteger(numbered)&&numbered>0?numbered:
      prefix?Number(prefix[1]):null;
    const message=prefix?text.slice(prefix[0].length):text;
    return {line:known??uniqueOpcodeLine(message,source),message};
  });
}
export function formatAssemblyError(error, source=null) {
  return extractAssemblyDiagnostics(error,source).map(({line,message})=>
    `${line!==null?`Line ${line}: `:''}${message}`).join('\n');
}
