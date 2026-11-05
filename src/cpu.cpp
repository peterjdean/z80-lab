#include "cpu.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>
using namespace z80emu;

Cpu::Cpu(){ports.fill(0xff);}
void Cpu::reset(uint16_t pc,uint16_t sp,bool clear_memory){if(clear_memory) mem.fill(0);reg={};reg.pc=pc;reg.sp=sp;instructions=cycles=0;output.clear();ports.fill(0xff);console.reset();}
uint16_t Cpu::read16(uint16_t a) const {return uint16_t(read(a)|read(uint16_t(a+1))<<8);}
void Cpu::write16(uint16_t a,uint16_t v){write(a,v);write(uint16_t(a+1),v>>8);}
uint8_t Cpu::fetch(){uint8_t v=read(reg.pc);++reg.pc;return v;}
uint8_t Cpu::fetchOpcode(){uint8_t v=fetch(); reg.r=uint8_t((reg.r&0x80)|((reg.r+1)&0x7f));return v;}
uint16_t Cpu::fetch16(){uint8_t l=fetch(),h=fetch();return uint16_t(l|(h<<8));}
void Cpu::push(uint16_t v){--reg.sp;write(reg.sp,v>>8);--reg.sp;write(reg.sp,v);}
uint16_t Cpu::pop(){uint16_t v=read16(reg.sp);reg.sp=uint16_t(reg.sp+2);return v;}
uint8_t Cpu::in(uint16_t p){
 switch(uint8_t(p)){
  case SerialConsole::RX_DATA:return console.receive();
  case SerialConsole::STATUS:return console.status();
  default:return ports[p&255];
 }
}
void Cpu::out(uint16_t p,uint8_t v){
 ports[p&255]=v;
 if(uint8_t(p)==SerialConsole::TX_DATA)console.transmit(v);
 output.push_back({p,v});
 if(output.size()>512)output.erase(output.begin(),output.begin()+256);
}
bool Cpu::parity(uint8_t v){v^=v>>4;v^=v>>2;v^=v>>1;return !(v&1);}
bool Cpu::cond(int n) const {const bool c[8]={!(reg.f&FZ),bool(reg.f&FZ),!(reg.f&FC),bool(reg.f&FC),!(reg.f&FP),bool(reg.f&FP),!(reg.f&FS),bool(reg.f&FS)};return c[n&7];}
uint16_t Cpu::addrIndexed(int prefix,int8_t d) const{return uint16_t((prefix==1?reg.ix:reg.iy)+d);}
uint8_t Cpu::getReg(int n,int prefix,bool real_hl) const {
 switch(n){case 0:return reg.b;case 1:return reg.c;case 2:return reg.d;case 3:return reg.e;case 4:return !real_hl&&prefix?(prefix==1?reg.ix:reg.iy)>>8:reg.h;case 5:return !real_hl&&prefix?(prefix==1?reg.ix:reg.iy)&255:reg.l;case 6:return read(hl());default:return reg.a;}
}
void Cpu::setReg(int n,uint8_t v,int prefix,bool real_hl){switch(n){case 0:reg.b=v;break;case 1:reg.c=v;break;case 2:reg.d=v;break;case 3:reg.e=v;break;case 4:if(!real_hl&&prefix){uint16_t& rr=prefix==1?reg.ix:reg.iy;rr=(rr&0xff)|(uint16_t(v)<<8);}else reg.h=v;break;case 5:if(!real_hl&&prefix){uint16_t& rr=prefix==1?reg.ix:reg.iy;rr=(rr&0xff00)|v;}else reg.l=v;break;case 6:write(hl(),v);break;default:reg.a=v;}}
uint16_t Cpu::rp(int p,int pre) const {return p==0?bc():p==1?de():p==2?(pre?(pre==1?reg.ix:reg.iy):hl()):reg.sp;}
void Cpu::setRp(int p,uint16_t v,int pre){switch(p){case 0:setBC(v);break;case 1:setDE(v);break;case 2:if(pre)(pre==1?reg.ix:reg.iy)=v;else setHL(v);break;default:reg.sp=v;}}
uint16_t Cpu::rp2(int p,int pre) const {return p==3?af():rp(p,pre);}
void Cpu::setRp2(int p,uint16_t v,int pre){if(p==3)setAF(v);else setRp(p,v,pre);}
uint8_t Cpu::inc8(uint8_t a){uint8_t v=uint8_t(a+1);reg.f=(reg.f&FC)|(v&(FS|FY|FX))|(v==0?FZ:0)|((a&15)==15?FH:0)|(a==0x7f?FP:0);return v;}
uint8_t Cpu::dec8(uint8_t a){uint8_t v=uint8_t(a-1);reg.f=(reg.f&FC)|FN|(v&(FS|FY|FX))|(v==0?FZ:0)|((a&15)==0?FH:0)|(a==0x80?FP:0);return v;}
void Cpu::alu(int op,uint8_t v){
 uint8_t a=reg.a;int c=(op==1||op==3)?int(bool(reg.f&FC)):0;
 if(op==0||op==1||op==2||op==3||op==7){
    bool subtract=op==2||op==3||op==7;int result=subtract?int(a)-v-c:int(a)+v+c;
    uint8_t r=uint8_t(result);
    // CP copies undocumented X/Y flags from the *operand*, not the subtraction result.
    uint8_t f=(r&FS)|(op==7?(v&(FY|FX)):(r&(FY|FX)))|(r==0?FZ:0)|(subtract?FN:0);
    if(subtract){if(((a^v^r)&0x10))f|=FH;if(((a^v)&(a^r)&0x80))f|=FP;}
    else{if((a^v^r)&0x10)f|=FH;if((~(a^v)&(a^r)&0x80))f|=FP;}
    if(result<0||result>255)f|=FC;
    reg.f=f;if(op!=7)reg.a=r;return;
 }
 uint8_t r=op==4?(a&v):op==5?(a^v):(a|v);
 reg.a=r;reg.f=(r&(FS|FY|FX))|(r==0?FZ:0)|(parity(r)?FP:0)|(op==4?FH:0);
}
void Cpu::addHL(uint16_t x,int pre){uint32_t a=rp(2,pre),r=a+x;setRp(2,uint16_t(r),pre);reg.f=(reg.f&(FS|FZ|FP))|((r>>8)&(FY|FX))|(((a^x^r)&0x1000)?FH:0)|(r>0xffff?FC:0);}
void Cpu::adcHL(uint16_t x,bool subtract){uint32_t a=hl();int cin=(reg.f&FC)?1:0;int32_t v=subtract?int32_t(a)-x-cin:int32_t(a)+x+cin;uint16_t r=uint16_t(v);uint8_t f=((r>>8)&(FS|FY|FX))|(r==0?FZ:0)|(subtract?FN:0);if((a^x^r)&0x1000)f|=FH;if(subtract?bool((a^x)&(a^r)&0x8000):bool((~(a^x))&(a^r)&0x8000))f|=FP;if(v<0||v>65535)f|=FC;setHL(r);reg.f=f;}
void Cpu::daa(){uint8_t a=reg.a;bool neg=reg.f&FN,car=reg.f&FC;uint8_t adj=0;if((reg.f&FH)||((a&0x0f)>9))adj|=6;if(car||(a>0x99)){adj|=0x60;car=true;}uint8_t r=neg?uint8_t(a-adj):uint8_t(a+adj);reg.a=r;reg.f=(reg.f&FN)|(r&(FS|FY|FX))|(r==0?FZ:0)|(parity(r)?FP:0)|(((a^r)&0x10)?FH:0)|(car?FC:0);}
void Cpu::cb(uint8_t op,uint16_t address,bool indexed){int x=op>>6,y=(op>>3)&7,z=op&7;uint8_t v=indexed||z==6?read(address):getReg(z,0);uint8_t r=v,f=0;
 if(x==0){switch(y){case 0:r=uint8_t((v<<1)|(v>>7));f=(v>>7)&1;break;case 1:r=uint8_t((v>>1)|(v<<7));f=v&1;break;case 2:r=uint8_t((v<<1)|bool(reg.f&FC));f=(v>>7)&1;break;case 3:r=uint8_t((v>>1)|(bool(reg.f&FC)<<7));f=v&1;break;case 4:r=uint8_t(v<<1);f=(v>>7)&1;break;case 5:r=uint8_t((v>>1)|(v&0x80));f=v&1;break;case 6:r=uint8_t((v<<1)|1);f=(v>>7)&1;break;default:r=v>>1;f=v&1;}
    reg.f=f|(r&(FS|FY|FX))|(r==0?FZ:0)|(parity(r)?FP:0);
 } else if(x==1){bool zero=!(v&(1<<y));// BIT on memory takes the undocumented X/Y flags from MEMPTR high (effective address), not the byte read.
    uint8_t bits=(indexed||z==6)?uint8_t(address>>8):v;reg.f=(reg.f&FC)|FH|(bits&(FY|FX))|(zero?(FZ|FP):0)|((y==7&&!zero)?FS:0);return;
 }else if(x==2)r=uint8_t(v&~(1<<y));else r=uint8_t(v|(1<<y));
 if(indexed){write(address,r);if(z!=6)setReg(z,r,0);}else if(z==6)write(address,r);else setReg(z,r,0);
}
void Cpu::block(uint8_t op){
 bool decrement=op&8, repeat=op&0x10;int direction=decrement?-1:1;
 if((op&0xe7)==0xa0){ // LDI/LDD/LDIR/LDDR
   uint8_t v=read(hl());write(de(),v);setHL(uint16_t(hl()+direction));setDE(uint16_t(de()+direction));setBC(uint16_t(bc()-1));uint8_t mix=uint8_t(reg.a+v);reg.f=(reg.f&(FS|FZ|FC))|(bc()?FP:0)|(mix&FX)|((mix&0x02)?FY:0);cycles+=16;
   if(repeat&&bc()){reg.pc=uint16_t(reg.pc-2);cycles+=5;}
 }else if((op&0xe7)==0xa1){ // CPI/CPD/CPIR/CPDR
   uint8_t v=read(hl()),a=reg.a,res=uint8_t(a-v);setHL(uint16_t(hl()+direction));setBC(uint16_t(bc()-1));bool half=(a&15)<(v&15);uint8_t mix=uint8_t(res-(half?1:0));reg.f=(reg.f&FC)|FN|(res&FS)|(res==0?FZ:0)|(half?FH:0)|(bc()?FP:0)|(mix&FX)|((mix&0x02)?FY:0);cycles+=16;
   if(repeat&&bc()&&res){reg.pc=uint16_t(reg.pc-2);cycles+=5;}
 }else if((op&0xe7)==0xa2){ // INI/IND/INIR/INDR
   uint8_t v=in(bc());write(hl(),v);setHL(uint16_t(hl()+direction));reg.b=uint8_t(reg.b-1);int k=v+uint8_t(reg.c+direction);reg.f=(reg.b&(FS|FY|FX))|(reg.b==0?FZ:0)|(v&0x80?FN:0)|(k>255?(FH|FC):0)|(parity(uint8_t((k&7)^reg.b))?FP:0);cycles+=16;
   if(repeat&&reg.b){reg.pc=uint16_t(reg.pc-2);cycles+=5;}
 }else{ // OUTI/OUTD/OTIR/OTDR
   uint8_t v=read(hl());reg.b=uint8_t(reg.b-1);out(bc(),v);setHL(uint16_t(hl()+direction));int k=v+reg.l;reg.f=(reg.b&(FS|FY|FX))|(reg.b==0?FZ:0)|(v&0x80?FN:0)|(k>255?(FH|FC):0)|(parity(uint8_t((k&7)^reg.b))?FP:0);cycles+=16;
   if(repeat&&reg.b){reg.pc=uint16_t(reg.pc-2);cycles+=5;}
 }
}
void Cpu::ed(uint8_t op){int x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1;
 if(x==1){
  switch(z){
  case 0:{uint8_t v=in(bc());if(y!=6)setReg(y,v,0);reg.f=(reg.f&FC)|(v&(FS|FY|FX))|(v==0?FZ:0)|(parity(v)?FP:0);cycles+=12;return;}
  case 1:out(bc(),y==6?0:getReg(y,0));cycles+=12;return;
  case 2:adcHL(rp(p,0),!q);cycles+=15;return;
  case 3:{uint16_t a=fetch16();if(q)setRp(p,read16(a),0);else write16(a,rp(p,0));cycles+=20;return;}
  case 4: {uint8_t v=reg.a;reg.a=0;alu(2,v);cycles+=8;return;}
  case 5:reg.pc=pop();reg.iff1=reg.iff2;cycles+=14;return;
  case 6:{const uint8_t modes[]={0,0,1,2,0,0,1,2};reg.im=modes[y];cycles+=8;return;}
  case 7:
   switch(y){
   case 0:reg.i=reg.a;cycles+=9;return;
   case 1:reg.r=reg.a;cycles+=9;return;
   case 2:case 3:{uint8_t v=y==2?reg.i:reg.r;reg.a=v;reg.f=(reg.f&FC)|(v&(FS|FY|FX))|(v==0?FZ:0)|(reg.iff2?FP:0);cycles+=9;return;}
   case 4:case 5:{uint8_t v=read(hl());if(y==4){write(hl(),uint8_t((v>>4)|(reg.a<<4)));reg.a=uint8_t((reg.a&0xf0)|(v&15));}else{write(hl(),uint8_t((v<<4)|(reg.a&15)));reg.a=uint8_t((reg.a&0xf0)|(v>>4));}reg.f=(reg.f&FC)|(reg.a&(FS|FY|FX))|(reg.a==0?FZ:0)|(parity(reg.a)?FP:0);cycles+=18;return;}
   default:cycles+=8;return;
   }
  }
 }
 if((op&0xe4)==0xa0 && ((op&7)<=3)){block(op);return;}
 cycles+=8; // Unassigned ED opcodes act as two-byte NOPs on a Zilog Z80.
}
void Cpu::step(){
 if(reg.halted)return;
 ++instructions;int prefix=0, extra=0;uint8_t op=fetchOpcode();
 while(op==0xdd||op==0xfd){if(extra>=262144)throw std::runtime_error("Infinite DD/FD prefix stream");prefix=op==0xdd?1:2;extra+=4;op=fetchOpcode();}
 if(op==0xcb){
   if(prefix){int8_t d=static_cast<int8_t>(fetch());uint8_t sub=fetch();cb(sub,addrIndexed(prefix,d),true);cycles+=extra-4+((sub&0xc0)==0x40?20:23);}
   else{uint8_t sub=fetchOpcode();cb(sub,hl(),false);cycles+=((sub&7)==6?((sub&0xc0)==0x40?12:15):8);}
   return;
 }
 if(op==0xed){ed(fetchOpcode());cycles+=extra;return;}
 int x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1;cycles+=extra;
 // Index displacements are fetched after opcodes but before any immediate operand.
 bool mem_operand=(x==1&&op!=0x76&&(y==6||z==6))||(x==2&&z==6)||(x==0&&(z==4||z==5||z==6)&&y==6);
 uint16_t memaddr=hl();
 if(prefix&&mem_operand){memaddr=addrIndexed(prefix,static_cast<int8_t>(fetch()));cycles+=8;}
 auto get=[&](int n,bool force_real=false)->uint8_t{return n==6?read(memaddr):getReg(n,prefix,force_real);};
 auto set=[&](int n,uint8_t v,bool force_real=false){if(n==6)write(memaddr,v);else setReg(n,v,prefix,force_real);};
 if(x==0){switch(z){
  case 0:
   if(y==0)cycles+=4;
   else if(y==1){std::swap(reg.a,reg.aa);std::swap(reg.f,reg.ff);cycles+=4;}
   else{int8_t d=static_cast<int8_t>(fetch());bool take=y==2?(--reg.b!=0):y==3||cond(y-4);if(take)reg.pc=uint16_t(reg.pc+d);cycles+=y==2?(take?13:8):y==3?12:(take?12:7);}break;
  case 1:if(!q){setRp(p,fetch16(),prefix);cycles+=10;}else{addHL(rp(p,prefix),prefix);cycles+=11;}break;
  case 2:
   if(p==0||p==1){uint16_t a=p==0?bc():de();if(q)reg.a=read(a);else write(a,reg.a);cycles+=7;}
   else {uint16_t a=fetch16();if(p==2){if(q)setRp(2,read16(a),prefix);else write16(a,rp(2,prefix));cycles+=16;}else{if(q)reg.a=read(a);else write(a,reg.a);cycles+=13;}}break;
  case 3:setRp(p,uint16_t(rp(p,prefix)+(q?-1:1)),prefix);cycles+=6;break;
  case 4:set(y,inc8(get(y)));cycles+=y==6?11:4;break;
  case 5:set(y,dec8(get(y)));cycles+=y==6?11:4;break;
  case 6:set(y,fetch());cycles+=y==6?(prefix?11:10):7;break; // indexed base: +4 prefix +8 disp +11 =23? LD (IX+d),n is 19 -> corrected below
  case 7:{
   uint8_t a=reg.a,c=reg.f&FC;
   switch(y){
   case 0:reg.a=uint8_t((a<<1)|(a>>7));reg.f=(reg.f&(FS|FZ|FP))|(reg.a&(FY|FX))|(a>>7);break;
   case 1:reg.a=uint8_t((a>>1)|(a<<7));reg.f=(reg.f&(FS|FZ|FP))|(reg.a&(FY|FX))|(a&1);break;
   case 2:reg.a=uint8_t((a<<1)|bool(c));reg.f=(reg.f&(FS|FZ|FP))|(reg.a&(FY|FX))|(a>>7);break;
   case 3:reg.a=uint8_t((a>>1)|(bool(c)<<7));reg.f=(reg.f&(FS|FZ|FP))|(reg.a&(FY|FX))|(a&1);break;
   case 4:daa();break;
   case 5:reg.a=~a;reg.f=(reg.f&(FS|FZ|FP|FC))|FH|FN|(reg.a&(FY|FX));break;
   case 6:reg.f=(reg.f&(FS|FZ|FP))|(a&(FY|FX))|FC;break;
   case 7:reg.f=(reg.f&(FS|FZ|FP))|(a&(FY|FX))|(c?FH:0)|(c?0:FC);break;
   }cycles+=4;break;
  }}
 }else if(x==1){
  if(op==0x76){reg.halted=true;cycles+=4;}
  else{bool real=(prefix&&(y==6||z==6));set(y,get(z,real),real);cycles+=(y==6||z==6)?7:4;}
 }else if(x==2){alu(y,get(z));cycles+=z==6?7:4;}
 else{switch(z){
  case 0:if(cond(y)){reg.pc=pop();cycles+=11;}else cycles+=5;break;
  case 1:if(!q){setRp2(p,pop(),prefix);cycles+=10;}else switch(p){case 0:reg.pc=pop();cycles+=10;break;case 1:std::swap(reg.b,reg.bb);std::swap(reg.c,reg.cc);std::swap(reg.d,reg.dd);std::swap(reg.e,reg.ee);std::swap(reg.h,reg.hh);std::swap(reg.l,reg.ll);cycles+=4;break;case 2:reg.pc=rp(2,prefix);cycles+=4;break;case 3:reg.sp=rp(2,prefix);cycles+=6;break;}break;
  case 2:{uint16_t v=fetch16();if(cond(y))reg.pc=v;cycles+=10;break;}
  case 3:switch(y){
   case 0:reg.pc=fetch16();cycles+=10;break;
   case 2:{uint8_t port=fetch();out(uint16_t((reg.a<<8)|port),reg.a);cycles+=11;break;}
   case 3:{uint8_t port=fetch();reg.a=in(uint16_t((reg.a<<8)|port));cycles+=11;break;}
   case 4:{uint16_t v=read16(reg.sp);write16(reg.sp,rp(2,prefix));setRp(2,v,prefix);cycles+=19;break;}
   case 5:{uint16_t v=de();setDE(hl());setHL(v);cycles+=4;break;}
   case 6:reg.iff1=reg.iff2=false;cycles+=4;break;
   case 7:reg.iff1=reg.iff2=true;cycles+=4;break;
   default:throw std::runtime_error("internal prefix dispatch error");}
   break;
  case 4:{uint16_t v=fetch16();if(cond(y)){push(reg.pc);reg.pc=v;cycles+=17;}else cycles+=10;break;}
  case 5:if(!q){push(rp2(p,prefix));cycles+=11;}else{if(p!=0)throw std::runtime_error("internal prefix dispatch error");uint16_t v=fetch16();push(reg.pc);reg.pc=v;cycles+=17;}break;
  case 6:alu(y,fetch());cycles+=7;break;
  case 7:push(reg.pc);reg.pc=uint16_t(y*8);cycles+=11;break;
 }}
 // Compensate LD (IX/IY+d),n: base 10 + prefix 4 + displacement 5 = 19.
 if(prefix&&x==0&&z==6&&y==6)cycles-=4;
}
