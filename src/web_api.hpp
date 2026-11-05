#pragma once
// Stable, minimal C ABI. All addresses, lengths and counters use 32-bit arguments;
// 64-bit CPU timing counters are serialized as JSON rather than returned across JS ABI.
#ifdef __cplusplus
extern "C" {
#endif
void z80_init(void);
// format: 0 autodetect, 1 Intel HEX, 2 plain hex, 3 raw binary.
// origin: -1 uses Intel HEX entry/first record or address 0 for simple text;
// for raw binary specify an explicit 0..65535 origin.
// Returns number of loaded bytes, -1 on validation error; no partial writes on error.
int z80_load(const unsigned char* bytes, int length, int format, int origin);
void z80_reset(void); // RAM retained and reset to the last successfully loaded entry.
int z80_step(void);   // 1 if executed, 0 if already halted, -1 if execution fails.
int z80_run_slice(int max_steps, int breakpoint); // bounded; breakpoint -1 disables.
// Browser breakpoints, checked before each instruction; max 64 active addresses.
// These are independent of the legacy single-breakpoint argument above.
int z80_set_breakpoint(int address, int enabled); // 0 success, -1 invalid/limit
void z80_clear_breakpoints(void);
int z80_set_pc(int address); // keep RAM/registers, leave HALT and set execution address
int z80_queue_input(const unsigned char* bytes, int length); // byte-oriented UART
int z80_read_byte(int address);
int z80_write_byte(int address, int byte);
int z80_memory_ptr(void); // fixed 65536-byte array; pointer into WebAssembly linear memory
const char* z80_state_json(void);
const char* z80_disassembly(int address); // "0100  21 00 20  LD HL,$2000"
int z80_disassembly_length(int address);
const unsigned char* z80_output_ptr(void); // use z80_output_length immediately afterwards
int z80_output_length(void);
const char* z80_last_error(void);
#ifdef __cplusplus
}
#endif
