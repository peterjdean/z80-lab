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
#include <vector>
#include <cstring>
#include <limits>

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
// Snapshot version 1: little-endian, platform-independent, no C++ structure padding.
// The entire payload is checked before the live CPU is replaced.
constexpr size_t SNAPSHOT_LIMIT = 1'200'000;
struct SnapshotWriter {
    std::vector<uint8_t> data;
    void byte(uint8_t b) { data.push_back(b); }
    void number(uint64_t n, int width) {
        for (int i=0; i<width; ++i) byte(uint8_t(n >> (8*i)));
    }
    void bytes(const uint8_t* p, size_t n) { data.insert(data.end(),p,p+n); }
    void string(const std::string& v) {
        number(v.size(),4);
        bytes(reinterpret_cast<const uint8_t*>(v.data()),v.size());
    }
};
struct SnapshotReader {
    const uint8_t* data;
    size_t length, offset=0;
    void need(size_t n) {
        if (n > length-offset) throw std::runtime_error("Truncated CPU snapshot");
    }
    uint64_t number(int width) {
        need(size_t(width));uint64_t v=0;
        for (int i=0;i<width;++i) v |= uint64_t(data[offset++]) << (8*i);
        return v;
    }
    void bytes(uint8_t* dest,size_t n) { need(n);std::copy_n(data+offset,n,dest);offset+=n; }
    std::string string(size_t max) {
        const uint64_t n=number(4);
        if(n>max) throw std::runtime_error("Oversized CPU snapshot data");
        need(size_t(n));
        const std::string out(reinterpret_cast<const char*>(data+offset),size_t(n));
        offset+=size_t(n);
        return out;
    }
};
std::vector<uint8_t> build_snapshot() {
    const auto uart=machine.console.saveState();
    if (uart.incoming.size() > 1'000'000 || uart.outgoing.size() > z80emu::SerialConsole::OUTPUT_HISTORY_LIMIT)
        throw std::runtime_error("Serial queues exceed snapshot limit");
    SnapshotWriter w;
    w.data.reserve(66000 + uart.incoming.size() + uart.outgoing.size());
    for (const uint8_t b : {'Z','8','L','S'}) w.byte(b);
    w.number(1,2);                // format version
    w.number(0,4);                // total size patched below
    w.number(entry,2);
    w.bytes(machine.mem.data(),machine.mem.size());
    w.bytes(machine.ports.data(),machine.ports.size());
    const auto& r=machine.reg;
    for (const uint8_t b : {r.a,r.f,r.b,r.c,r.d,r.e,r.h,r.l,
                            r.aa,r.ff,r.bb,r.cc,r.dd,r.ee,r.hh,r.ll}) w.byte(b);
    for (const uint16_t v : {r.ix,r.iy,r.sp,r.pc}) w.number(v,2);
    for (const uint8_t b : {r.i,r.r,r.im, uint8_t(r.iff1),uint8_t(r.iff2),uint8_t(r.halted)}) w.byte(b);
    w.number(machine.output.size(),2);
    for(const auto& event : machine.output) {w.number(event.port,2);w.byte(event.value);}
    w.number(machine.instructions,8);
    w.number(machine.cycles,8);
    w.number(uart.transmitted,8);
    w.string(uart.incoming);
    w.string(uart.outgoing);
    if (w.data.size()>SNAPSHOT_LIMIT) throw std::runtime_error("CPU snapshot exceeds size limit");
    const uint32_t size=uint32_t(w.data.size());
    for(int i=0;i<4;++i)w.data[6+i]=uint8_t(size>>(8*i));
    return w.data;
}
void restore_snapshot(const uint8_t* bytes, size_t length) {
    if (!bytes || length<65868 || length>SNAPSHOT_LIMIT)
        throw std::runtime_error("Invalid CPU snapshot length");
    SnapshotReader r{bytes,length};
    for (const unsigned char b : {'Z','8','L','S'}) {
        if(r.number(1)!=b)throw std::runtime_error("Not a Z80/LAB CPU snapshot");
    }
    if(r.number(2)!=1)throw std::runtime_error("Unsupported CPU snapshot version");
    if(r.number(4)!=length)throw std::runtime_error("CPU snapshot size mismatch");
    const auto saved_entry=uint16_t(r.number(2));
    z80emu::Cpu replacement;
    r.bytes(replacement.mem.data(),replacement.mem.size());
    r.bytes(replacement.ports.data(),replacement.ports.size());
    auto& x=replacement.reg;
    for (uint8_t* b : {&x.a,&x.f,&x.b,&x.c,&x.d,&x.e,&x.h,&x.l,
                        &x.aa,&x.ff,&x.bb,&x.cc,&x.dd,&x.ee,&x.hh,&x.ll}) *b=uint8_t(r.number(1));
    for (uint16_t* v : {&x.ix,&x.iy,&x.sp,&x.pc}) *v=uint16_t(r.number(2));
    x.i=uint8_t(r.number(1));x.r=uint8_t(r.number(1));x.im=uint8_t(r.number(1));
    auto boolean=[&r](){const uint64_t v=r.number(1);if(v>1)throw std::runtime_error("Invalid CPU flag");return v!=0;};
    x.iff1=boolean();x.iff2=boolean();x.halted=boolean();
    if(x.im>2)throw std::runtime_error("Invalid interrupt mode");
    const uint64_t output_count=r.number(2);
    if(output_count>512) throw std::runtime_error("Invalid port history size");
    for(size_t i=0;i<output_count;++i) {
        const auto port=uint16_t(r.number(2));const auto value=uint8_t(r.number(1));
        replacement.output.push_back({port,value});
    }
    replacement.instructions=r.number(8);
    replacement.cycles=r.number(8);
    const uint64_t transmitted=r.number(8);
    z80emu::SerialConsole::Snapshot uart;
    uart.incoming=r.string(1'000'000);
    uart.outgoing=r.string(z80emu::SerialConsole::OUTPUT_HISTORY_LIMIT);
    uart.transmitted=transmitted;
    if(transmitted<uart.outgoing.size() || r.offset!=length)
        throw std::runtime_error("Invalid CPU snapshot serial data or trailing bytes");
    replacement.console.restoreState(uart);
    machine=std::move(replacement);
    entry=saved_entry;
    output_cursor=machine.console.transmittedBytes();
    pending_output.clear();
    breakpoints.fill(0);breakpoint_count=0;
    last_error.clear();
}

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
int z80_snapshot_size(void) {
    try {last_error.clear();return int(build_snapshot().size());}
    catch(const std::exception& e){fail(e);return -1;}
}
int z80_snapshot_save(unsigned char* dest,int capacity) {
    try {
        last_error.clear();
        const auto data=build_snapshot();
        if(!dest || capacity<0 || size_t(capacity)<data.size())
            throw std::runtime_error("Snapshot output buffer too small");
        std::copy(data.begin(),data.end(),dest);
        return int(data.size());
    }catch(const std::exception& e){fail(e);return -1;}
}
int z80_snapshot_restore(const unsigned char* data,int length) {
    try {last_error.clear();if(length<0)throw std::runtime_error("Negative CPU snapshot length");
        restore_snapshot(data,size_t(length));return 0;
    }catch(const std::exception& e){fail(e);return -1;}
}

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
