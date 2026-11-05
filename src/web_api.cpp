#include "web_api.hpp"
#include "cpu.hpp"
#include "loader.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {
z80emu::Cpu machine;
uint16_t entry = 0;
std::array<uint8_t, 65536> breakpoints{};
int breakpoint_count = 0;
std::string last_error;
std::string state_json;
std::string decoded_line;
std::string pending_output;
uint64_t output_cursor = 0;

std::string_view format_name(int f) {
    switch (f) { case 0: return "auto"; case 1: return "ihex"; case 2: return "hex";
                 case 3: return "bin"; default: return ""; }
}
void fail(const std::exception& e) { last_error = e.what(); }
}

extern "C" {
void z80_init(void) {
    machine.reset(0, 0xfffe, true);
    entry=0;
    pending_output.clear(); output_cursor=0; last_error.clear();
    breakpoints.fill(0); breakpoint_count=0;
}
int z80_load(const unsigned char* bytes, int length, int format, int origin) {
    try {
        last_error.clear();
        if (!bytes || length <= 0 || length > 1024*1024) throw std::runtime_error("Program must contain 1..1048576 bytes");
        if (format_name(format).empty()) throw std::runtime_error("Unknown loader format");
        if (origin < -1 || origin > 65535) throw std::runtime_error("Invalid load address");
        if (format == 3 && origin == -1) throw std::runtime_error("Binary requires an origin address");
        z80emu::Cpu replacement;
        replacement.reset(0, 0xfffe, true);
        const std::optional<uint16_t> pos = origin == -1 ? std::nullopt : std::optional<uint16_t>(uint16_t(origin));
        const auto result = z80emu::loadData(replacement,
            std::string_view(reinterpret_cast<const char*>(bytes),size_t(length)),pos,std::string(format_name(format)));
        const uint16_t start = result.has_entry ? result.entry : uint16_t(pos.value_or(0));
        replacement.reset(start, 0xfffe, false);
        machine = std::move(replacement);
        entry = start;
        output_cursor=0;pending_output.clear();
        breakpoints.fill(0); breakpoint_count=0;
        return int(result.bytes);
    } catch(const std::exception& e) { fail(e); return -1; }
}
int z80_set_breakpoint(int address, int enabled) {
    if (address < 0 || address > 65535 || (enabled != 0 && enabled != 1)) {
        last_error="Invalid breakpoint address or state"; return -1;
    }
    if (enabled && !breakpoints[size_t(address)] && breakpoint_count >= 64) {
        last_error="Maximum 64 breakpoints"; return -1;
    }
    if (enabled && !breakpoints[size_t(address)]) ++breakpoint_count;
    if (!enabled && breakpoints[size_t(address)]) --breakpoint_count;
    breakpoints[size_t(address)] = uint8_t(enabled);
    return 0;
}
void z80_clear_breakpoints(void) { breakpoints.fill(0); breakpoint_count=0; }
int z80_set_pc(int address) {
    if (address < 0 || address > 65535) { last_error="Invalid PC address"; return -1; }
    machine.reg.pc = uint16_t(address);
    machine.reg.halted = false;
    return 0;
}
void z80_reset(void) {
    machine.reset(entry,0xfffe,false);
    output_cursor=0;pending_output.clear();last_error.clear();
}
int z80_step(void) {
    if (machine.reg.halted) return 0;
    try { machine.step(); return 1; }
    catch (const std::exception& e) { fail(e); return -1; }
}
int z80_run_slice(int max_steps, int breakpoint) {
    last_error.clear();
    if(max_steps < 1 || max_steps > 20000 || breakpoint < -1 || breakpoint > 65535) {
        last_error="Invalid run slice";return -1;
    }
    int n=0;
    while (n<max_steps && !machine.reg.halted &&
           (breakpoint<0 || machine.reg.pc != uint16_t(breakpoint)) &&
           !breakpoints[machine.reg.pc]) {
        const int result=z80_step();
        if(result < 0) return -1;
        ++n;
    }
    return n;
}
int z80_queue_input(const unsigned char* bytes, int length) {
    if (length < 0 || length > 4096 || (!bytes && length)) {last_error="Invalid UART input";return -1;}
    if(length)machine.console.queueInput(std::string_view(reinterpret_cast<const char*>(bytes),size_t(length)));
    return length;
}
int z80_read_byte(int address) {return address>=0&&address<=65535 ? machine.mem[size_t(address)] : -1;}
int z80_write_byte(int address,int value) {
    if(address<0||address>65535||value<0||value>255)return -1;
    machine.mem[size_t(address)]=uint8_t(value);return 0;
}
int z80_memory_ptr(void) {return static_cast<int>(reinterpret_cast<uintptr_t>(machine.mem.data()));}
const char* z80_state_json(void) {
    const auto& r=machine.reg;
    std::ostringstream s;
    s << "{\"a\":"<<unsigned(r.a)<<",\"f\":"<<unsigned(r.f)
      <<",\"b\":"<<unsigned(r.b)<<",\"c\":"<<unsigned(r.c)
      <<",\"d\":"<<unsigned(r.d)<<",\"e\":"<<unsigned(r.e)
      <<",\"h\":"<<unsigned(r.h)<<",\"l\":"<<unsigned(r.l)
      <<",\"aa\":"<<unsigned(r.aa)<<",\"ff\":"<<unsigned(r.ff)
      <<",\"bb\":"<<unsigned(r.bb)<<",\"cc\":"<<unsigned(r.cc)
      <<",\"dd\":"<<unsigned(r.dd)<<",\"ee\":"<<unsigned(r.ee)
      <<",\"hh\":"<<unsigned(r.hh)<<",\"ll\":"<<unsigned(r.ll)
      <<",\"ix\":"<<r.ix<<",\"iy\":"<<r.iy
      <<",\"sp\":"<<r.sp<<",\"pc\":"<<r.pc
      <<",\"i\":"<<unsigned(r.i)<<",\"r\":"<<unsigned(r.r)
      <<",\"im\":"<<unsigned(r.im)
      <<",\"iff1\":"<<(r.iff1?"true":"false")
      <<",\"iff2\":"<<(r.iff2?"true":"false")
      <<",\"halted\":"<<(r.halted?"true":"false")
      <<",\"instructions\":"<<machine.instructions
      <<",\"cycles\":"<<machine.cycles
      <<",\"rxPending\":"<<machine.console.pendingInput()<<"}";
    state_json=s.str();return state_json.c_str();
}
const char* z80_disassembly(int address) {
    if(address<0||address>65535){decoded_line.clear();return decoded_line.c_str();}
    const auto d=z80emu::disassemble(machine,uint16_t(address));
    decoded_line=z80emu::hex16(uint16_t(address))+"  "+d.bytes+"  "+d.text;
    return decoded_line.c_str();
}
int z80_disassembly_length(int address) {
    return address>=0 && address<=65535 ? z80emu::disassemble(machine,uint16_t(address)).length : 0;
}
const unsigned char* z80_output_ptr(void) {
    const auto& history=machine.console.transcript();
    const uint64_t total=machine.console.transmittedBytes();
    const uint64_t oldest=total-history.size();
    const size_t off=size_t(output_cursor>=oldest?std::min<uint64_t>(output_cursor-oldest,history.size()):0);
    pending_output=history.substr(off);
    output_cursor=total;
    return reinterpret_cast<const unsigned char*>(pending_output.data());
}
int z80_output_length(void) {return int(pending_output.size());}
const char* z80_last_error(void) {return last_error.c_str();}
}
