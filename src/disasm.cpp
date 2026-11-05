#include "cpu.hpp"
#include <cstdio>
#include <string>
using namespace z80emu;
std::string z80emu::hex8(uint8_t n){char b[8];std::snprintf(b,sizeof b,"%02X",n);return b;}
std::string z80emu::hex16(uint16_t n){char b[8];std::snprintf(b,sizeof b,"%04X",n);return b;}
Decoded z80emu::disassemble(const Cpu& cpu,uint16_t address){
 uint16_t pc=address;auto u8=[&](){uint8_t v=cpu.read(pc);pc=uint16_t(pc+1);return v;};
 auto u16=[&](){uint8_t l=u8(),h=u8();return uint16_t(l|(h<<8));};
 int pre=0,prefix_count=0;uint8_t op=u8();while((op==0xdd||op==0xfd)&&prefix_count<16){pre=op==0xdd?1:2;++prefix_count;op=u8();}
 const char* rn[]={"B","C","D","E","H","L","(HL)","A"};
 const char* rps[]={"BC","DE","HL","SP"},*rps2[]={"BC","DE","HL","AF"};
 const char* cc[]={"NZ","Z","NC","C","PO","PE","P","M"};
 const char* al[]={"ADD A,","ADC A,","SUB ","SBC A,","AND ","XOR ","OR ","CP "};
 auto ix=pre==1?std::string("IX"):std::string("IY");
 auto disp=[&](){int8_t d=static_cast<int8_t>(u8());return "("+ix+(d<0?"-":"+")+hex8(uint8_t(d<0?-int(d):d))+"h)";};
 auto rr=[&](int i){if(!pre||i==6)return std::string(rn[i]);if(i==4)return ix+"H";if(i==5)return ix+"L";return std::string(rn[i]);};
 auto rp=[&](int i,bool stack=false){return (pre&&i==2)?ix:std::string(stack?rps2[i]:rps[i]);};
 auto nn=[&](){return std::string("$")+hex16(u16());};
 auto n=[&](){return std::string("$")+hex8(u8());};
 auto rel=[&](){int8_t d=static_cast<int8_t>(u8());return std::string("$")+hex16(uint16_t(pc+d));};
 std::string text;
 if(prefix_count==16){text="DB $"+hex8(cpu.read(address));pc=uint16_t(address+1);}
 else if(op==0xcb){
  std::string operand;uint8_t o=0;
  if(pre){operand=disp();o=u8();}else{o=u8();operand=rn[o&7];}
  int x=o>>6,y=(o>>3)&7,z=o&7;const char* shift[]={"RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"};
  if(x==0)text=std::string(shift[y])+" "+operand;
  else text=std::string(x==1?"BIT ":x==2?"RES ":"SET ")+std::to_string(y)+","+operand;
  if(pre&&z!=6&&x!=1)text+=","+std::string(rn[z]);
 }else if(op==0xed){
  uint8_t o=u8();int x=o>>6,y=(o>>3)&7,z=o&7,p=y>>1,q=y&1;
  if(x==1){switch(z){
  case 0:text="IN "+(y==6?std::string("(C)"):std::string(rn[y])+",(C)");break;
  case 1:text="OUT (C),"+(y==6?std::string("0"):std::string(rn[y]));break;
  case 2:text=std::string(q?"ADC HL,":"SBC HL,")+rps[p];break;
  case 3:{std::string a=nn();text=q?"LD "+std::string(rps[p])+",("+a+")":"LD ("+a+"),"+rps[p];break;}
  case 4:text="NEG";break;
  case 5:text=y==1?"RETI":"RETN";break;
  case 6:{int modes[]={0,0,1,2,0,0,1,2};text="IM "+std::to_string(modes[y]);break;}
  case 7:{const char* names[]={"LD I,A","LD R,A","LD A,I","LD A,R","RRD","RLD","NOP*","NOP*"};text=names[y];break;}
  }}else if((o&0xe4)==0xa0&&(o&7)<=3){const char* names[]={"LDI","CPI","INI","OUTI","LDD","CPD","IND","OUTD","LDIR","CPIR","INIR","OTIR","LDDR","CPDR","INDR","OTDR"};int i=((o>>3)&3)*4+(o&3);text=names[i];}
  else text="NOP* (ED $"+hex8(o)+")";
 }else{
 int x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1;
 if(x==0){switch(z){
 case 0:if(y==0)text="NOP";else if(y==1)text="EX AF,AF'";else if(y==2)text="DJNZ "+rel();else if(y==3)text="JR "+rel();else text="JR "+std::string(cc[y-4])+","+rel();break;
 case 1:text=!q?"LD "+rp(p)+","+nn():"ADD "+rp(2)+","+rp(p);break;
 case 2:if(p<=1)text=q?"LD A,("+std::string(rps[p])+")":"LD ("+std::string(rps[p])+"),A";else{auto a=nn();text=p==2?(q?"LD "+rp(2)+",("+a+")":"LD ("+a+"),"+rp(2)):(q?"LD A,("+a+")":"LD ("+a+"),A");}break;
 case 3:text=std::string(q?"DEC ":"INC ")+rp(p);break;
 case 4:case 5:case 6:{std::string operand=(pre&&y==6)?disp():rr(y);text=z==4?"INC "+operand:z==5?"DEC "+operand:"LD "+operand+","+n();break;}
 case 7:{const char* names[]={"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"};text=names[y];break;}
 }}else if(x==1){if(op==0x76)text="HALT";else{std::string rdest=rr(y),rsrc=rr(z);if(pre&&(y==6||z==6)){auto d=disp();if(y==6){rdest=d;rsrc=rn[z];}else{rdest=rn[y];rsrc=d;}}text="LD "+rdest+","+rsrc;}}
 else if(x==2){std::string r=pre&&z==6?disp():rr(z);text=std::string(al[y])+r;}
 else{switch(z){
 case 0:text="RET "+std::string(cc[y]);break;
 case 1:if(!q)text="POP "+rp(p,true);else{const char* special[]={"RET","EXX","JP (HL)","LD SP,HL"};text=special[p];if(pre&&p==2)text="JP ("+ix+")";if(pre&&p==3)text="LD SP,"+ix;}break;
 case 2:{auto a=nn();text="JP "+std::string(cc[y])+","+a;break;}
 case 3:switch(y){case 0:text="JP "+nn();break;case 2:text="OUT ("+n()+"),A";break;case 3:text="IN A,("+n()+")";break;case 4:text="EX (SP),"+rp(2);break;case 5:text="EX DE,HL";break;case 6:text="DI";break;case 7:text="EI";break;default:text="DB $"+hex8(op);break;}break;
 case 4:{auto a=nn();text="CALL "+std::string(cc[y])+","+a;break;}
 case 5:text=!q?"PUSH "+rp(p,true):(p==0?"CALL "+nn():"DB $"+hex8(op));break;
 case 6:text=std::string(al[y])+n();break;
 case 7:text="RST $"+hex8(uint8_t(y*8));break;
 }}
 }
 int length=uint16_t(pc-address);if(length<=0||length>64)length=1;
 std::string bytes;for(int i=0;i<length&&i<7;i++){if(i)bytes+=' ';bytes+=hex8(cpu.read(uint16_t(address+i)));}if(length>7)bytes+=" ...";
 return {address,length,bytes,text};
}
