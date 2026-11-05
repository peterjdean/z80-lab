// Independent exhaustive 8-bit ALU reference model: every A/operand/carry.
// This is deliberately independent of the CPU's implementation formulas.
#include "cpu.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
using namespace z80emu;

static bool evenParity(uint8_t v){int n=0;for(int bit=0;bit<8;++bit)n+=(v>>bit)&1;return (n%2)==0;}
static uint8_t refF(int op,unsigned a,unsigned b,unsigned carry,uint8_t& value){
    const bool sub=(op==2||op==3||op==7);
    const unsigned cin=(op==1||op==3)?carry:0;
    if(op==4||op==5||op==6){
        value=uint8_t(op==4?(a&b):op==5?(a^b):(a|b));
        return (value&(FS|FY|FX))|(value==0?FZ:0)|(evenParity(value)?FP:0)|(op==4?FH:0);
    }
    int wide=sub?int(a)-int(b)-int(cin):int(a)+int(b)+int(cin);
    value=uint8_t(wide);
    const bool half=sub?((int(a&15)-int(b&15)-int(cin))<0):((a&15)+(b&15)+cin)>15;
    bool over=sub?((a>=128)!=(b>=128) && (a>=128)!=(value>=128)):
                ((a>=128)==(b>=128) && (a>=128)!=(value>=128));
    uint8_t xy=op==7?uint8_t(b&(FY|FX)):uint8_t(value&(FY|FX));
    return (value&FS)|xy|(value==0?FZ:0)|(half?FH:0)|(over?FP:0)|
           (sub?FN:0)|((wide<0||wide>255)?FC:0);
}
int main(){
    Cpu c;
    const uint8_t opcodes[8]={0xc6,0xce,0xd6,0xde,0xe6,0xee,0xf6,0xfe};
    uint64_t count=0;
    c.mem[0x101]=0;
    for(int op=0;op<8;++op)for(unsigned a=0;a<256;++a)
      for(unsigned b=0;b<256;++b)for(unsigned carry=0;carry<2;++carry){
        c.reset(0x100,0xfffe,false);
        c.mem[0x100]=opcodes[op];c.mem[0x101]=uint8_t(b);
        c.reg.a=uint8_t(a);c.reg.f=carry?FC:0;
        uint8_t expectedA=0;
        uint8_t expectedF=refF(op,a,b,carry,expectedA);
        c.step();
        if(c.reg.f!=expectedF || c.reg.a!=(op==7?a:expectedA)){
          std::cerr<<"ALU mismatch op="<<op<<" A="<<a<<" value="<<b<<" carry="<<carry
                   <<" expected A="<<int(op==7?a:expectedA)<<" F="<<int(expectedF)
                   <<" actual A="<<int(c.reg.a)<<" F="<<int(c.reg.f)<<"\n";
          return 1;
        }
        ++count;
      }
    for(int opcode: {0x04,0x05})for(unsigned b=0;b<256;++b)for(unsigned carry=0;carry<2;++carry){
        c.reset(0x100,0xfffe,false);c.mem[0x100]=opcode;c.reg.b=uint8_t(b);c.reg.f=carry?FC:0;
        c.step();
        bool dec=opcode==0x05;
        uint8_t result=uint8_t(dec?b-1:b+1);
        uint8_t f=(result&(FS|FY|FX))|(result==0?FZ:0)|(carry?FC:0)|(dec?FN:0)|
                 ((dec?(b&15)==0:(b&15)==15)?FH:0)|
                 ((dec?b==128:b==127)?FP:0);
        if(c.reg.b!=result||c.reg.f!=f){std::cerr<<"INC/DEC mismatch\n";return 1;}
        ++count;
    }
    std::cout<<"PASS: "<<count<<" exhaustive 8-bit ALU flag/result comparisons\n";
}
