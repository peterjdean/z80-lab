#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "serial.hpp"

namespace z80emu {
constexpr uint8_t FS=0x80, FZ=0x40, FY=0x20, FH=0x10, FX=0x08, FP=0x04, FN=0x02, FC=0x01;
struct Registers {
    uint8_t a=0,f=0,b=0,c=0,d=0,e=0,h=0,l=0;
    uint8_t aa=0,ff=0,bb=0,cc=0,dd=0,ee=0,hh=0,ll=0;
    uint16_t ix=0,iy=0,sp=0xffff,pc=0;
    uint8_t i=0,r=0,im=0;
    bool iff1=false,iff2=false,halted=false;
};
struct PortWrite { uint16_t port; uint8_t value; };
class Cpu {
public:
    std::array<uint8_t,65536> mem{};
    std::array<uint8_t,256> ports{};
    std::vector<PortWrite> output;
    SerialConsole console;
    Registers reg{};
    uint64_t instructions=0, cycles=0;
    Cpu();
    void reset(uint16_t pc=0, uint16_t sp=0xffff, bool clear_memory=false);
    void step();
    uint8_t read(uint16_t address) const {return mem[address];}
    void write(uint16_t address,uint8_t value) {mem[address]=value;}
    uint16_t read16(uint16_t a) const;
    void write16(uint16_t a,uint16_t v);
    uint16_t af() const {return (reg.a<<8)|reg.f;}
    uint16_t bc() const {return (reg.b<<8)|reg.c;}
    uint16_t de() const {return (reg.d<<8)|reg.e;}
    uint16_t hl() const {return (reg.h<<8)|reg.l;}
    void setAF(uint16_t v) {reg.a=v>>8;reg.f=v;}
    void setBC(uint16_t v) {reg.b=v>>8;reg.c=v;}
    void setDE(uint16_t v) {reg.d=v>>8;reg.e=v;}
    void setHL(uint16_t v) {reg.h=v>>8;reg.l=v;}
private:
    uint8_t fetch(),fetchOpcode();
    uint16_t fetch16();
    void push(uint16_t), alu(int,uint8_t), cb(uint8_t,uint16_t,bool),ed(uint8_t);
    uint16_t pop();
    uint8_t in(uint16_t); void out(uint16_t,uint8_t);
    bool cond(int) const;
    uint8_t getReg(int,int,bool=false) const;
    void setReg(int,uint8_t,int,bool=false);
    uint16_t rp(int,int) const; void setRp(int,uint16_t,int);
    uint16_t rp2(int,int) const; void setRp2(int,uint16_t,int);
    uint16_t addrIndexed(int,int8_t) const;
    static bool parity(uint8_t);
    uint8_t inc8(uint8_t),dec8(uint8_t);
    void addHL(uint16_t,int),adcHL(uint16_t,bool),daa();
    void block(uint8_t);
};
struct Decoded { uint16_t address; int length; std::string bytes; std::string text; };
Decoded disassemble(const Cpu& cpu,uint16_t address);
std::string hex8(uint8_t); std::string hex16(uint16_t);
}
