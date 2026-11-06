// Source-level stepping rules, shared between the browser Worker and Node tests.
// The CPU's instruction execution still happens exclusively in WebAssembly.
export const SOURCE_STEP_LIMIT = 20_000;

export function makeSourceLineLookup(entries) {
  if (!Array.isArray(entries) || entries.length > 65_536)
    throw new Error('Invalid source-step address map');
  const map = new Map();
  for (const entry of entries) {
    if (!entry || !Number.isInteger(entry.address) || entry.address < 0 || entry.address > 65535 ||
        !Number.isInteger(entry.line) || entry.line < 1 || entry.line > 200_001 ||
        map.has(entry.address))
      throw new Error('Invalid or duplicate source-step mapping');
    map.set(entry.address, entry.line);
  }
  return map;
}

// Evaluate AFTER executing an instruction. A breakpoint always wins, even if
// the PC remains on the same source line (e.g., a macro-expanded statement).
export function sourceStepOutcome({startLine, addressToLine, pc, halted, breakpoints, steps}) {
  if (halted) return 'halted';
  if (breakpoints.has(pc)) return 'breakpoint';
  if (startLine === undefined) return 'unmapped';
  const nextLine = addressToLine.get(pc);
  if (nextLine === undefined) return 'unmapped';
  if (nextLine !== startLine) return 'source-step';
  if (steps >= SOURCE_STEP_LIMIT) return 'source-step-limit';
  return null;
}
