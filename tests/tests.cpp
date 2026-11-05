#include "cpu.hpp"
#include "loader.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace z80emu;
static int checks=0;
#define CHECK(c) do{++checks; if(!(c)) {std::cerr<<"FAIL "<<__FILE__<<":"<<__LINE__<<" "<<#c<<"\n"; std::exit(1);}}while(0)
static void code(Cpu& c,std::initializer_list<uint8_t> data, uint16_t start=0x100){c.reset(start,0xfffe,true);uint16_t a=start;for(uint8_t v:data)c.write(a++,v);}
static void run(Cpu& c,int max=1000){while(!c.reg.halted&&max-->0)c.step();CHECK(c.reg.halted);}
static void test_loaders(){
 Cpu c;auto f=std::string(EXAMPLE_DIR)+"/hello.hex";auto load=loadFile(c,f);CHECK(load.bytes==10);CHECK(load.entry==0x100);c.reset(load.entry);run(c);CHECK(c.mem[0x2000]==0x41);CHECK(c.mem[0x2001]==0x42);CHECK(c.cycles==45);
 auto t=loadFile(c,std::string(EXAMPLE_DIR)+"/hello.txt",0x100);CHECK(t.bytes==10);CHECK(c.mem[0x100]==0x21);
 namespace fs=std::filesystem;fs::path dir=fs::temp_directory_path()/"z80emu_test_files";fs::create_directories(dir);
 auto put=[&](const std::string& s,const std::string& text){std::ofstream out(dir/s,std::ios::binary);out<<text;return (dir/s).string();};
 auto should_fail=[&](const std::string& path,const std::string& fmt){bool fail=false;try{loadFile(c,path,{},fmt);}catch(const std::exception&){fail=true;}CHECK(fail);};
 should_fail(put("bad.hex","33 44 ZZ"),"hex");should_fail(put("bad_checksum.hex",":0100000000FE\n:00000001FF\n"),"ihex");
 should_fail(put("bad_length.hex",":0200000000FF\n:00000001FF\n"),"ihex");should_fail(put("bad_addr.hex",":020000040001F9\n:00000001FF\n"),"ihex");
 should_fail(put("missing_eof.hex",":01000000768A\n"),"ihex");
 CHECK(loadFile(c,put("raw.bin",std::string("\x21\x34\x12\x76",4)),0x400,"bin").bytes==4);
 CHECK(loadFile(c,put("continuous.txt","2100203E4176"),0x500,"hex").bytes==6);
 CHECK(c.mem[0x500]==0x21);
 CHECK(c.read16(0x400)==0x3421);CHECK(c.mem[0x403]==0x76);
 fs::remove_all(dir);
}
static void test_arithmetic(){
 Cpu c;code(c,{0x3e,0x7f,0xc6,0x01,0x76});run(c);CHECK(c.reg.a==0x80);CHECK((c.reg.f&(FS|FH|FP))==(FS|FH|FP));CHECK(!(c.reg.f&FC));
 code(c,{0x3e,0x00,0xd6,0x01,0x76});run(c);CHECK(c.reg.a==0xff);CHECK((c.reg.f&(FC|FN|FH))==(FC|FN|FH));
 code(c,{0x3e,0x09,0xc6,0x01,0x27,0x76});run(c);CHECK(c.reg.a==0x10);CHECK(!(c.reg.f&FC));
 code(c,{0x3e,0xff,0xc6,0x01,0x76});run(c);CHECK(c.reg.a==0);CHECK((c.reg.f&(FZ|FC|FH))==(FZ|FC|FH));
 code(c,{0x3e,0x80,0xfe,0x7f,0x76});run(c);CHECK(c.reg.a==0x80);CHECK(c.reg.f&FP);CHECK(c.reg.f&FN);
 code(c,{0x06,0xff,0x04,0x76});run(c);CHECK(c.reg.b==0);CHECK(c.reg.f&FZ);CHECK(c.reg.f&FH);CHECK(!(c.reg.f&FC));
 code(c,{0x06,0x80,0x05,0x76});run(c);CHECK(c.reg.b==0x7f);CHECK(c.reg.f&FP);
 code(c,{0x21,0xff,0x7f,0x01,0x01,0x00,0x09,0x76});run(c);CHECK(c.hl()==0x8000);CHECK(c.reg.f&FH);
}
static void test_flow(){
 Cpu c;code(c,{0x06,0x03,0x10,0xfe,0x76});run(c);CHECK(c.reg.b==0);CHECK(c.instructions==5);CHECK(c.cycles==45); // LD B,n 7; DJNZ 13+13+8; HALT 4
 code(c,{0xcd,0x08,0x01,0x76,0x00,0x00,0x00,0x00,0x3e,0x5a,0xc9});run(c);CHECK(c.reg.a==0x5a);CHECK(c.reg.sp==0xfffe);CHECK(c.cycles==17+7+10+4);
 code(c,{0x3e,0x00,0xb7,0x28,0x02,0x3e,0xff,0x76});run(c);CHECK(c.reg.a==0);CHECK(c.reg.f&FZ);
 code(c,{0x3e,0x12,0x08,0x3e,0x34,0x08,0x76});run(c);CHECK(c.reg.a==0x12);CHECK(c.reg.aa==0x34);
 code(c,{0x01,0xcd,0xab,0xc5,0x01,0x00,0x00,0xc1,0x76});run(c);CHECK(c.bc()==0xabcd);
 code(c,{0x21,0xff,0xff,0x36,0x76,0x76});run(c);CHECK(c.mem[0xffff]==0x76);
}
static void test_index(){
 Cpu c;code(c,{0xdd,0x21,0x00,0x20,0xdd,0x36,0xfe,0x55,0xdd,0x7e,0xfe,0x76});run(c);CHECK(c.reg.ix==0x2000);CHECK(c.mem[0x1ffe]==0x55);CHECK(c.reg.a==0x55);CHECK(c.cycles==14+19+19+4);
 code(c,{0xfd,0x21,0x00,0x30,0xfd,0x26,0x52,0xfd,0x2e,0x70,0x76});run(c);CHECK(c.reg.iy==0x5270);
 code(c,{0xdd,0x21,0x00,0x20,0x26,0x33,0xdd,0x74,0x02,0x76});run(c);CHECK(c.mem[0x2002]==0x33); // LD (IX+2),H uses H, not IXH
 code(c,{0xdd,0x21,0x00,0x20,0xdd,0xcb,0x01,0xc6,0x76});run(c);CHECK(c.mem[0x2001]==1);CHECK(c.reg.r==5);CHECK(c.cycles==14+23+4);
 code(c,{0xdd,0xcb,0xff,0x46,0x76});c.reg.ix=0x2000;c.mem[0x1fff]=1;run(c);CHECK(!(c.reg.f&FZ));CHECK(c.cycles==20+4);
 code(c,{0xdd,0x21,0x11,0x22,0xdd,0xe5,0xdd,0x21,0x00,0x00,0xdd,0xe1,0x76});run(c);CHECK(c.reg.ix==0x2211);
 CHECK(disassemble(c,0x100).length==4);CHECK(disassemble(c,0x100).text=="LD IX,$2211");
}
static void test_prefix(){
 Cpu c;code(c,{0x06,0x81,0xcb,0x00,0x76});run(c);CHECK(c.reg.b==3);CHECK(c.reg.f&FC);CHECK(c.cycles==7+8+4);
 code(c,{0x21,0x00,0x30,0x36,0x80,0xcb,0x7e,0x76});run(c);CHECK(!(c.reg.f&FZ));CHECK(c.reg.f&FS);
 code(c,{0xed,0x44,0x76});c.reg.a=1;run(c);CHECK(c.reg.a==0xff);CHECK(c.reg.f&FN);
 code(c,{0xed,0x47,0xed,0x57,0x76});c.reg.a=0x81;run(c);CHECK(c.reg.i==0x81);CHECK(c.reg.a==0x81);CHECK(!(c.reg.f&FP));
 code(c,{0x01,0x34,0x12,0xed,0x43,0x00,0x40,0x01,0x00,0x00,0xed,0x4b,0x00,0x40,0x76});run(c);CHECK(c.bc()==0x1234);
 code(c,{0x21,0x00,0x20,0x11,0x00,0x30,0x01,0x03,0x00,0xed,0xb0,0x76});c.mem[0x2000]=0x41;c.mem[0x2001]=0x42;c.mem[0x2002]=0x43;run(c);CHECK(c.bc()==0);CHECK(c.mem[0x3000]==0x41);CHECK(c.mem[0x3001]==0x42);CHECK(c.mem[0x3002]==0x43);CHECK(c.cycles==10+10+10+21+21+16+4);
 code(c,{0x3e,0x42,0xd3,0x10,0xdb,0x10,0x76});run(c);CHECK(c.reg.a==0x42);CHECK(c.output.size()==1);CHECK(c.output[0].port==0x4210);
}
static void test_serial_console(){
 Cpu c;
 CHECK(c.console.status()==2);CHECK(c.console.pendingInput()==0);
 CHECK(c.console.receive()==0);
 c.console.queueInput("Z\r");
 CHECK(c.console.status()==3);CHECK(c.console.pendingInput()==2);
 CHECK(c.console.receive()==uint8_t('Z'));CHECK(c.console.receive()==13);
 CHECK(c.console.status()==2);
 // Immediate I/O and full-width I/O both use low 8 bits for device decode.
 code(c,{0x3e,0x48,0xd3,0x00,0x01,0x00,0x99,0xed,0x79,0x76});
 run(c);CHECK(c.console.transcript()=="HH");
 CHECK(c.output.size()==2);CHECK(c.output[0].port==0x4800);CHECK(c.output[1].port==0x9900);
 // Host-supplied input, polled status and serial reads through instructions.
 code(c,{0xdb,0x02,0x32,0x00,0x20,0xdb,0x01,0x32,0x01,0x20,0xdb,0x02,0x32,0x02,0x20,0x76});
 c.console.queueInput("Q");run(c);
 CHECK(c.mem[0x2000]==3);CHECK(c.mem[0x2001]=='Q');CHECK(c.mem[0x2002]==2);
 CHECK(c.console.pendingInput()==0);
 // Non-console I/O ports continue acting as ordinary read/write latches.
 code(c,{0x3e,0xa5,0xd3,0x10,0xdb,0x10,0x76});run(c);
 CHECK(c.reg.a==0xa5);CHECK(c.output.size()==1);CHECK(c.output[0].port==0xa510);
 CHECK(c.console.transcript().empty());
 c.console.queueInput("unfinished");c.console.transmit('x');c.reset();
 CHECK(c.console.pendingInput()==0);CHECK(c.console.transcript().empty());
 for(size_t k=0;k<SerialConsole::OUTPUT_HISTORY_LIMIT+100;++k)c.console.transmit('x');
 CHECK(c.console.transcript().size()==SerialConsole::OUTPUT_HISTORY_LIMIT);
 CHECK(c.console.transcript().front()=='x');
 CHECK(c.console.transmittedBytes()==SerialConsole::OUTPUT_HISTORY_LIMIT+100);
 c.console.reset();CHECK(c.console.transmittedBytes()==0);
}
static void test_serial_examples(){
 Cpu c;
 auto hello=loadFile(c,std::string(EXAMPLE_DIR)+"/console_hello.hex");
 CHECK(hello.entry==0x0100);c.reset(hello.entry);run(c,1000);
 CHECK(c.console.transcript()=="Hello from Z80!\r\n");
 auto echo=loadFile(c,std::string(EXAMPLE_DIR)+"/console_echo.hex");
 CHECK(echo.entry==0x0100);c.reset(echo.entry);
 for(int i=0;i<30;++i)c.step(); // No input: poll loops without halting.
 CHECK(!c.reg.halted);CHECK(c.console.transcript()==">");
 c.console.queueInput("Euler\r");run(c,1000);
 CHECK(c.console.transcript()==">Euler\r\n");
 CHECK(c.console.pendingInput()==0);
}
static void test_monitor(){
 Cpu c;
 auto f=std::string(EXAMPLE_DIR)+"/monitor.hex";
 auto loaded=loadFile(c,f);
 CHECK(loaded.entry==0x0100);
 CHECK(loaded.bytes>400&&loaded.bytes<2000);
 c.reset(loaded.entry);
 // Exercise help, bad input, lowercase commands, deposit, dump and CALL/RET.
 c.console.queueInput("?\rD 2000\re 2000 3e 41 d3 00 c9\rd 2000\rG 2000\rD 2000\rE 2000 GG\rQ\r");
 run(c,150000);
 CHECK(c.mem[0x2000]==0x3e);
 CHECK(c.mem[0x2001]==0x41);
 CHECK(c.mem[0x2002]==0xd3);
 CHECK(c.mem[0x2003]==0x00);
 CHECK(c.mem[0x2004]==0xc9);
 const auto& out=c.console.transcript();
 CHECK(out.find("Z80/MON v0.3")!=std::string::npos);
 CHECK(out.find("2000: 3E 41 D3 00 C9")!=std::string::npos);
 CHECK(out.find("AReturned to monitor")!=std::string::npos);
 CHECK(out.find("? Syntax or hex error")!=std::string::npos);
 CHECK(out.find("Monitor halted")!=std::string::npos);
 CHECK(c.reg.sp==0xfffe);
 CHECK(c.console.pendingInput()==0);
}
static void test_all_opcodes(){Cpu c;for(int prefix: {0,0xcb,0xed,0xdd,0xfd})for(int i=0;i<256;++i){c.reset(0x100,0xfffe,true);uint16_t a=0x100;if(prefix)c.mem[a++]=prefix;c.mem[a++]=uint8_t(i);c.mem[a++]=0;c.mem[a++]=0;c.mem[a++]=0;if(i==0xcb&&prefix==0xdd)continue;try{c.step();}catch(...){std::cerr<<"Unexpected exception prefix="<<prefix<<" op="<<i<<"\n";CHECK(false);}CHECK(c.instructions==1);CHECK(c.cycles>0);}}
int main(){test_loaders();test_arithmetic();test_flow();test_index();test_prefix();test_serial_console();test_serial_examples();test_monitor();test_all_opcodes();std::cout<<"PASS: "<<checks<<" assertions across loaders, arithmetic, flow, indexed, serial console, Z80 monitor, ED/CB and opcode coverage\n";}
