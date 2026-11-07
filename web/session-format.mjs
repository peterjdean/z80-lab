// Portable, versioned Z80/LAB session envelope. All data stays in the browser.
// CPU binary is validated again by the native API before state is replaced.
export const SESSION_FORMAT = 'z80-lab-session';
export const SESSION_VERSION = 1;

// Safari may append a suffix after the entire extension for duplicate
// downloads (for example ".z80session-2"). Distinct, timestamped names reduce
// collisions while keeping the valid .z80session suffix at the very end.
// The timestamp is UTC and includes milliseconds.
export function sessionDownloadName(date=new Date()) {
  const iso=date.toISOString();
  return 'z80-lab-session-' +
    iso.slice(0,10).replace(/-/g,'') + '-' +
    iso.slice(11,19).replace(/:/g,'') + '-' +
    iso.slice(20,23) + '.z80session';
}
const MAX_FILE_CHARS = 2_500_000;
const MAX_CPU_BYTES = 1_200_000;
const address = n => Number.isInteger(n) && n >= 0 && n <= 65535;
const err = message => { throw new Error(message); };

function binaryToBase64(bytes) {
  const parts = [];
  for (let i=0;i<bytes.length;i+=8192)
    parts.push(String.fromCharCode(...bytes.subarray(i,i+8192)));
  return btoa(parts.join(''));
}
function base64ToBinary(text) {
  if(typeof text!=='string'||text.length>Math.ceil(MAX_CPU_BYTES/3)*4+4||
    !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(text))
    err('Invalid CPU snapshot encoding');
  const raw=atob(text);
  if(raw.length<65868||raw.length>MAX_CPU_BYTES)err('Invalid CPU snapshot length');
  const bytes=new Uint8Array(raw.length);
  for(let i=0;i<raw.length;i++)bytes[i]=raw.charCodeAt(i);
  if(String.fromCharCode(...bytes.subarray(0,4))!=='Z8LS'||bytes[4]!==1||bytes[5]!==0)
    err('Unsupported CPU snapshot format');
  const size=new DataView(bytes.buffer).getUint32(6,true);
  if(size!==bytes.length)err('CPU snapshot size mismatch');
  return bytes;
}
function normalMap(map,source) {
  if(!map||!Array.isArray(map.entries)||map.entries.length>65536)
    err('Invalid saved source map');
  const maxLine=source.split('\n').length;
  const lineToAddresses=Object.create(null), addressToLine=Object.create(null), entries=[];
  const known=new Set();
  for(const e of map.entries) {
    if(!e||!address(e.address)||!Number.isInteger(e.length)||e.length<1||
      e.address+e.length>65536||!Number.isInteger(e.line)||e.line<1||e.line>maxLine||
      known.has(e.address))err('Invalid source-map address or line');
    known.add(e.address);
    entries.push({address:e.address,line:e.line,length:e.length});
    addressToLine[e.address]=e.line;
    (lineToAddresses[e.line]??=[]).push(e.address);
  }
  return {entries,addressToLine,lineToAddresses};
}
function validDebugger(d) {
  if(!d||![0,2_000_000,4_000_000,6_000_000,8_000_000].includes(d.clock)||
    !address(d.memoryAddress)||
    (d.disasmAddress!==null&&!address(d.disasmAddress))||
    !Array.isArray(d.breakpoints)||d.breakpoints.length>64||
    d.breakpoints.some(n=>!address(n))||new Set(d.breakpoints).size!==d.breakpoints.length||
    typeof d.stopReason!=='string'||d.stopReason.length>40)
    err('Invalid saved debugger settings');
  return {clock:d.clock,memoryAddress:d.memoryAddress,disasmAddress:d.disasmAddress,
    breakpoints:[...d.breakpoints],stopReason:d.stopReason};
}
function validWorkspace(w) {
  if(!w||typeof w.source!=='string'||w.source.length>200_000||
    typeof w.serial!=='string'||w.serial.length>16384||
    typeof w.mapActive!=='boolean')err('Invalid session editor/terminal data');
  const source=w.source, serial=w.serial;
  let assembled=null;
  if(w.assembled!==null) {
    const a=w.assembled;
    if(!a||typeof a.hex!=='string'||a.hex.length>500_000||
      typeof a.listing!=='string'||a.listing.length>500_000||
      !address(a.entry)||!Number.isInteger(a.count)||a.count<0||a.count>65536||
      a.source!==source)err('Invalid saved assembly result');
    assembled={hex:a.hex,listing:a.listing,entry:a.entry,count:a.count,
      source,sourceMap:normalMap(a.sourceMap,source)};
  }
  if(w.mapActive&&!assembled)err('Session source map has no assembly result');
  return {source,serial,assembled,mapActive:w.mapActive};
}
export function encodeSession({bytes,debugger:settings,workspace}) {
  if(!(bytes instanceof Uint8Array))err('Missing CPU snapshot bytes');
  const cpu=binaryToBase64(bytes);
  const json=JSON.stringify({format:SESSION_FORMAT,version:SESSION_VERSION,
    createdAt:new Date().toISOString(),cpu,
    debugger:validDebugger(settings),workspace:validWorkspace(workspace)},null,2);
  if(json.length>MAX_FILE_CHARS)err('Session exceeds maximum file size');
  return json+'\n';
}
export function decodeSession(text) {
  if(typeof text!=='string'||text.length>MAX_FILE_CHARS)err('Session file is too large');
  let payload;
  try {payload=JSON.parse(text);}catch {err('Not a valid JSON session file');}
  if(!payload||payload.format!==SESSION_FORMAT||payload.version!==SESSION_VERSION)
    err('Unsupported Z80/LAB session format/version');
  const bytes=base64ToBinary(payload.cpu);
  return {bytes,debugger:validDebugger(payload.debugger),
    workspace:validWorkspace(payload.workspace)};
}
