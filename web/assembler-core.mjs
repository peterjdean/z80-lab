// Dependency-neutral conversion from ASM80's documented per-line assembler
// result to strict Intel HEX records understood by our existing Z80 loader.
// Does NOT emulate the CPU or implement an assembler.
export function emitIntelHex(result) {
  if (!result || !Array.isArray(result.dump)) throw new Error('Unexpected assembler result (no listing).');
  const memory = new Map();
  let firstAddress = null;
  const listing = [];
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
    const label = row.label ? `${row.label}: ` : '';
    const params=Array.isArray(row.params)?row.params.join(', '):String(row.params ?? '');
    const operation = row.opcode ? `${row.opcode} ${params}` : '';
    listing.push(`${start.toString(16).toUpperCase().padStart(4,'0')}  ${bytes.map(x=>x.toString(16).toUpperCase().padStart(2,'0')).join(' ').padEnd(24)}  ${label}${operation}`.trimEnd());
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
  return {hex:records.join('\n')+'\n', entry, count:memory.size, listing:listing.join('\n')};
}

export function formatAssemblyError(error) {
  const items = error?.errors || (error?.error ? [error.error] : [error]);
  return items.map(item=> {
    const line = item?.wline?.numline ?? item?.wline?.line ?? item?.line;
    const message = item?.msg || item?.message || String(item);
    return `${line!=null?`Line ${line}: `:''}${message}`;
  }).join('\n');
}
