#include "web_api.hpp"
#ifdef NDEBUG
#undef NDEBUG  // Keep test assertions active in Release builds.
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static std::string read_file(const char* filename) {
    std::ifstream f(std::filesystem::path(EXAMPLE_DIR)/filename,std::ios::binary);
    assert(f); return std::string(std::istreambuf_iterator<char>(f),{});
}
static int load(const std::string& s,int format=0,int origin=-1) {
    return z80_load(reinterpret_cast<const unsigned char*>(s.data()),int(s.size()),format,origin);
}
int main() {
    z80_init();
    assert(z80_state_json());
    assert(z80_read_byte(65536)==-1);
    assert(z80_write_byte(65536,0)==-1);
    assert(z80_write_byte(0x101,0x7f)==0);
    assert(z80_read_byte(0x101)==0x7f);
    assert(load(read_file("hello.hex"))==10);
    assert(std::string(z80_state_json()).find("\"pc\":256")!=std::string::npos);
    assert(std::string(z80_disassembly(0x100)).find("LD HL,$2000")!=std::string::npos);
    assert(z80_disassembly_length(0x100)==3);
    assert(z80_run_slice(100,-1)==7);
    assert(z80_read_byte(0x2000)==0x41);
    assert(z80_read_byte(0x2001)==0x42);
    assert(z80_run_slice(100,-1)==0); // HALT
    z80_reset();
    assert(z80_read_byte(0x2000)==0x41); // memory retained
    assert(z80_step()==1);
    assert(z80_run_slice(100,0x103)==0); // breakpoint is checked before execution
    assert(load(":010100007689\n:00000001FF\n")==-1); // bad checksum
    assert(std::string(z80_last_error()).find("checksum")!=std::string::npos);
    // An invalid file cannot overwrite the previous machine state.
    assert(z80_read_byte(0x2000)==0x41);
    assert(load(read_file("console_echo.hex"))>0);
    const std::string s="Hello\r";
    assert(z80_queue_input(reinterpret_cast<const unsigned char*>(s.data()),int(s.size()))==int(s.size()));
    assert(z80_run_slice(2000,-1)>0);
    const unsigned char* data=z80_output_ptr();
    const std::string output(reinterpret_cast<const char*>(data),size_t(z80_output_length()));
    assert(output==">Hello\r\n");
    assert(z80_output_length()==output.size()); // last packet stays until next drain
    z80_output_ptr(); assert(z80_output_length()==0);
    assert(load(std::string("3E41D30076"),2,0x100)==5);
    assert(z80_run_slice(20,-1)==3); // LD A, OUT, HALT
    assert(z80_output_length()==0);
    data=z80_output_ptr();
    assert(z80_output_length()==1 && data[0]=='A');
    assert(z80_run_slice(20001,-1)==-1);
    // Multiple independently managed breakpoints: both stop before execution.
    assert(load(read_file("hello.hex"))==10);
    assert(z80_set_breakpoint(0x103,1)==0);
    assert(z80_set_breakpoint(0x108,1)==0);
    assert(z80_set_breakpoint(0x10000,1)==-1);
    assert(z80_run_slice(100,-1)==1); // stops at 0103
    assert(z80_run_slice(100,-1)==0); // remains stopped
    assert(z80_step()==1);           // resume uses a single step
    assert(z80_run_slice(100,-1)==3); // stops at 0108
    assert(z80_run_slice(100,-1)==0);
    assert(z80_set_breakpoint(0x103,0)==0);
    assert(z80_set_breakpoint(0x103,0)==0); // idempotent removal
    z80_clear_breakpoints();
    assert(z80_run_slice(100,-1)==2);
    assert(z80_read_byte(0x2000)==0x41 && z80_read_byte(0x2001)==0x42);
    z80_reset();
    assert(z80_set_breakpoint(0x100,1)==0);
    z80_reset(); // reset keeps breakpoints in place
    assert(z80_run_slice(10,-1)==0);
    assert(load(read_file("hello.hex"))==10); // loading clears stale breakpoints
    assert(z80_run_slice(100,-1)==7);
    // Editing RAM through the browser API must not affect unrelated bytes.
    assert(z80_write_byte(0x2000,0x7E)==0);
    assert(z80_read_byte(0x2000)==0x7E && z80_read_byte(0x2001)==0x42);
    assert(z80_write_byte(0x2000,256)==-1);
    assert(z80_read_byte(0x2000)==0x7E);
    // Set PC to any valid 16-bit address, including after HALT; do not alter RAM.
    assert(load(read_file("hello.hex"))==10);
    assert(z80_run_slice(100,-1)==7); // halted
    assert(z80_set_pc(0x103)==0);
    assert(std::string(z80_state_json()).find("\"pc\":259")!=std::string::npos);
    assert(std::string(z80_state_json()).find("\"halted\":false")!=std::string::npos);
    assert(z80_read_byte(0x100)==0x21);
    assert(z80_set_pc(65536)==-1);
    assert(z80_step()==1);
    // Explicitly enforce bounded breakpoint storage and permit reuse of freed slots.
    z80_clear_breakpoints();
    for (int i=0; i<64; ++i) assert(z80_set_breakpoint(0x3000+i,1)==0);
    assert(z80_set_breakpoint(0x3040,1)==-1);
    assert(z80_set_breakpoint(0x3000,0)==0);
    assert(z80_set_breakpoint(0x3040,1)==0);
    z80_clear_breakpoints();
    std::cout<<"PASS: native browser C ABI, loaders, breakpoints, serial I/O and memory\n";
}
