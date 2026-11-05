#include "cpu.hpp"
#include "clock.hpp"
#include "loader.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
using namespace z80emu;
namespace {
volatile sig_atomic_t interrupted=0;
void onInterrupt(int){interrupted=1;}
uint64_t parseNumber(const std::string& s){
 if(s.empty())throw std::runtime_error("Missing number");
 int base=16;std::string v=s;if(v.size()>2&&v.substr(0,2)=="0x")v=v.substr(2);else if(v.back()=='h'||v.back()=='H')v.pop_back();else if(v.front()=='#')v.erase(0,1);
 if(v.empty())throw std::runtime_error("Empty hexadecimal number");
 size_t pos=0;auto n=std::stoull(v,&pos,base);
 if(pos!=v.size())throw std::runtime_error("Invalid hexadecimal number: "+s);
 return n;
}
uint16_t number16(const std::string& s){auto n=parseNumber(s);if(n>65535)throw std::runtime_error("Address exceeds FFFF: "+s);return uint16_t(n);}
uint64_t decimalCount(const std::string& s){size_t pos=0;auto n=std::stoull(s,&pos,10);if(pos!=s.size())throw std::runtime_error("Invalid step count: "+s);return n;}
struct RawTerminal {
 termios original{};bool active=false;bool cursor_hidden=false;
 explicit RawTerminal(bool hide_cursor=true):cursor_hidden(hide_cursor){if(isatty(STDIN_FILENO)&&tcgetattr(STDIN_FILENO,&original)==0){termios raw=original;raw.c_lflag &= ~(ICANON|ECHO);raw.c_iflag &= ~(IXON|ICRNL);if(!hide_cursor)raw.c_oflag &= ~ONLCR;raw.c_cc[VMIN]=1;raw.c_cc[VTIME]=0;if(tcsetattr(STDIN_FILENO,TCSANOW,&raw)==0){active=true;if(cursor_hidden)std::cout<<"\033[?25l"<<std::flush;}}}
 ~RawTerminal(){if(active){tcsetattr(STDIN_FILENO,TCSANOW,&original);if(cursor_hidden)std::cout<<"\033[0m\033[?25h\n"<<std::flush;else std::cout<<"\r\n"<<std::flush;}}
};
int keyAvailable(){fd_set f;FD_ZERO(&f);FD_SET(STDIN_FILENO,&f);timeval tv{};return select(STDIN_FILENO+1,&f,nullptr,nullptr,&tv)>0;}
int key(){unsigned char c=0;if(::read(STDIN_FILENO,&c,1)!=1)return -1;return c;}
void printRegisters(const Cpu& c){const auto& r=c.reg;
 std::cout<<"AF="<<hex16(c.af())<<" BC="<<hex16(c.bc())<<" DE="<<hex16(c.de())<<" HL="<<hex16(c.hl())
 <<" IX="<<hex16(r.ix)<<" IY="<<hex16(r.iy)<<" SP="<<hex16(r.sp)<<" PC="<<hex16(r.pc)<<"\n"
 <<"AF'="<<hex16(uint16_t(r.aa<<8|r.ff))<<" BC'="<<hex16(uint16_t(r.bb<<8|r.cc))
 <<" DE'="<<hex16(uint16_t(r.dd<<8|r.ee))<<" HL'="<<hex16(uint16_t(r.hh<<8|r.ll))
 <<" I="<<hex8(r.i)<<" R="<<hex8(r.r)<<" IM="<<unsigned(r.im)
 <<" IFF1="<<r.iff1<<" IFF2="<<r.iff2<<" HALT="<<r.halted<<"\n"
 <<"Flags S="<<bool(r.f&FS)<<" Z="<<bool(r.f&FZ)<<" 5="<<bool(r.f&FY)<<" H="<<bool(r.f&FH)
 <<" 3="<<bool(r.f&FX)<<" P/V="<<bool(r.f&FP)<<" N="<<bool(r.f&FN)<<" C="<<bool(r.f&FC)<<"\n"
 <<"Instructions="<<c.instructions<<" T-states="<<c.cycles<<"\n";
}
void dumpMemory(const Cpu& cpu,uint16_t address,size_t length){length=std::min(length,size_t(65536));for(size_t i=0;i<length;i+=16){uint16_t a=uint16_t(address+i);std::cout<<hex16(a)<<"  ";for(size_t j=0;j<16;++j){if(i+j<length)std::cout<<hex8(cpu.read(uint16_t(a+j)))<<' ';else std::cout<<"   ";}std::cout<<" |";for(size_t j=0;j<16&&i+j<length;++j){unsigned char v=cpu.read(uint16_t(a+j));std::cout<<(v>=32&&v<=126?char(v):'.');}std::cout<<"|\n";}}
// Return safe, printable output for a diagnostic line: never execute guest ANSI escapes.
std::string escapeGuestOutput(std::string_view data){
 std::string out;
 for(unsigned char b:data){
  switch(b){case '\r':out+="\\r";break;case '\n':out+="\\n";break;case '\t':out+="\\t";break;
   default:if(b>=32&&b<=126)out+=char(b);else out+="\\x"+hex8(b);break;
  }
 }
 return out;
}
// Convert the byte transcript into displayable text lines with control bytes sanitized.
std::vector<std::string> consoleLines(std::string_view data,int columns){
 std::vector<std::string> lines(1);
 bool afterCR=false;columns=std::max(1,columns);
 auto put=[&](char ch){if(int(lines.back().size())>=columns)lines.emplace_back();lines.back().push_back(ch);};
 for(unsigned char byte:data){
  if(byte=='\r'){lines.emplace_back();afterCR=true;continue;}
  if(byte=='\n'){if(!afterCR)lines.emplace_back();afterCR=false;continue;}
  afterCR=false;
  if(byte=='\b'){if(!lines.back().empty())lines.back().pop_back();continue;}
  if(byte=='\t'){for(int j=0;j<4;++j)put(' ');continue;}
  put(byte>=32&&byte<=126?char(byte):'.');
 }
 return lines;
}
void help(){std::cout<<"z80emu - generic 64K Z80 CPU debugger\n\n"
 <<"Usage: z80emu FILE [--format auto|hex|ihex|bin] [--address HEX] [--pc HEX]\n"
 <<"                    [--sp HEX] [--steps DECIMAL] [--trace] [--dump HEX:HEX]\n"
 <<"                    [--input TEXT] [--input-file FILE] [--console] [--clock HZ]\n\n"
 <<"Addresses and --dump arguments are hexadecimal (0100, 0x0100, or 0100h).\n"
 <<"Step counts are decimal. --steps runs without a TUI, also over SSH/non-TTY.\n"
 <<"--clock 4000000 or --clock 4MHz limits execution to 4 MHz of Z80 T-states.\n"
 <<"Also accepts e.g. 2MHz, 6MHz, 8MHz. Default: unlimited.\n"
 <<"The limit uses batch pacing, not cycle-accurate hardware timing.\n"
 <<"--trace prints a disassembly line for each instruction (best with --steps).\n"
 <<"Intel HEX supplies its own addresses; --address selects load location for bin/hex\n"
 <<"and overrides the initial Intel HEX entry PC if --pc is not provided.\n"
 <<"Virtual console: OUT to port 00 transmits; IN from 01 receives;\n"
 <<"IN from 02 returns status (bit0 RX ready, bit1 TX ready).\n"
 <<"--input TEXT queues TEXT followed by CR (repeatable).\n"
 <<"--input-file FILE queues raw bytes (repeatable).\n"
 <<"--console connects the keyboard directly to the virtual serial console.\n"
 <<"In --console mode, Enter sends CR; Ctrl-] exits to the host.\n"
 <<"TUI keys: S step, R run/pause, I enter input line, V console/memory,\n"
 <<"B breakpoint, G PC, M memory, P port log, X reset, ? help, Q quit.\n";
}
// Direct serial terminal mode. Guest TX bytes are restricted to printable characters
// and ordinary terminal controls: untrusted guest ESC/CSI bytes are never executed.
void runConsole(Cpu& cpu, ClockLimiter& clock){
 RawTerminal tty(false);
 if(!tty.active)throw std::runtime_error("--console requires an interactive terminal");
 std::signal(SIGINT,onInterrupt);
 std::cout<<"[Z80 serial console: Ctrl-] to exit]\r\n"<<std::flush;
 clock.restart(cpu.cycles);
 uint64_t displayed=0;
 while(!interrupted&&!cpu.reg.halted){
  // The instruction cap protects keyboard responsiveness; the T-state cap
  // prevents a slow virtual clock from blocking the host for whole seconds.
  const uint64_t deadline=cpu.cycles+clock.quantumTStates();
  for(int n=0;n<5000&&!cpu.reg.halted;++n){cpu.step();if(clock.enabled()&&cpu.cycles>=deadline)break;}
  clock.wait(cpu.cycles);
  const std::string& output=cpu.console.transcript();
  const uint64_t total=cpu.console.transmittedBytes();
  const uint64_t oldest=total-output.size();
  if(displayed<oldest)displayed=oldest; // long-running console: output history truncated
  for(size_t j=size_t(displayed-oldest);j<output.size();++j){
   unsigned char c=static_cast<unsigned char>(output[j]);
   if((c>=32&&c<=126) || c=='\r' || c=='\n' || c=='\b' || c=='\t')std::cout<<char(c);
   else std::cout<<'.';
  }
  displayed=total;
  std::cout<<std::flush;
  if(keyAvailable()){
   int k=key();
   if(k<0||k==29)break;  // Ctrl-]
   if(k=='\r'||k=='\n')cpu.console.queueInput("\r");
   else if(k==127)cpu.console.queueInput("\b");
   else if(k>=0&&k<=255)cpu.console.queueInput(std::string(1,char(k)));
  } else if(!clock.enabled()) usleep(10000);
 }
 // HALT's output was flushed by the loop body before termination.
 if(cpu.reg.halted)std::cout<<"\r\n[Z80 HALTED at PC="<<hex16(cpu.reg.pc)<<"]"<<std::flush;
}
class Tui {
 Cpu& cpu;ClockLimiter& clock;std::optional<uint64_t> clock_speed;uint16_t entry,initial_sp;uint16_t memory_view=0;std::set<uint16_t> breaks;
 std::string status="Ready";bool running=false,show_ports=false,show_console=true;std::array<uint8_t,65536> before{};
 int width=100,height=30;
 void size(){winsize ws{};if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0){if(ws.ws_col)width=ws.ws_col;if(ws.ws_row)height=ws.ws_row;}}
 void at(int y,int x,const std::string& s){if(y<1||y>height||x>width)return;std::cout<<"\033["<<y<<";"<<x<<"H"<<s.substr(0,size_t(std::max(0,width-x+1)));}
 void fullLine(int y,std::string s){if(y<1||y>height)return;if((int)s.size()<width)s+=std::string(width-s.size(),' ');at(y,1,s);}
 std::string ask(const std::string& prompt){running=false;std::string question=" "+prompt+" [hex] > ";fullLine(height,question);std::cout<<"\033["<<height<<";"<<(question.size()+1)<<"H\033[?25h"<<std::flush;std::string v;while(!interrupted){int k=key();if(k<0||k==27){v="";break;}if(k=='\r'||k=='\n')break;if(k==127||k==8){if(!v.empty()){v.pop_back();std::cout<<"\b \b"<<std::flush;}continue;}if(k>=32&&k<=126&&v.size()<20){v+=char(k);std::cout<<char(k)<<std::flush;}}std::cout<<"\033[?25l";return v;}
 std::optional<std::string> inputPrompt(const std::string& label){
  running=false;
  std::string value;std::string prefix=" "+label+" (Enter sends CR, Esc cancels) > ";
  std::cout<<"\033[?25h";
  while(!interrupted){
    const size_t avail=width>int(prefix.size()+3)?size_t(width-prefix.size()-2):1;
    std::string tail=value.substr(value.size()>avail?value.size()-avail:0);
    fullLine(height,prefix+tail);
    std::cout<<"\033["<<height<<";"<<(prefix.size()+tail.size()+1)<<"H"<<std::flush;
    int k=key();
    if(k<0||k==27){std::cout<<"\033[?25l";return std::nullopt;}
    if(k=='\r'||k=='\n'){std::cout<<"\033[?25l";return value;}
    if(k==127||k==8){if(!value.empty())value.pop_back();continue;}
    if(k>=32&&k<=126&&value.size()<1024)value.push_back(char(k));
  }
  std::cout<<"\033[?25l";return std::nullopt;
 }
 void consolePanel(int heading,int rows){
   fullLine(heading," VIRTUAL SERIAL CONSOLE   TX port 00 / RX port 01 / status port 02   (I: queue input + CR)");
   const auto& transcript=cpu.console.transcript();
   auto lines=consoleLines(transcript,width-4);
   int start=std::max(0,int(lines.size())-rows);
   for(int i=0;i<rows;++i){
     int index=start+i;
     fullLine(heading+i+1,index<int(lines.size())?"  "+lines[size_t(index)]:"");
   }
 }
 void refresh(){size();std::cout<<"\033[H\033[2J";if(width<80||height<22){fullLine(1,"Terminal needs at least 80 columns x 22 rows; resize or use --steps.");std::cout<<std::flush;return;}
  fullLine(1," Z80 EMULATOR  |  64 KiB RAM  |  Clock: "+describeClock(clock_speed)+"  |  "+(running?std::string("RUNNING (press any key)"):cpu.reg.halted?std::string("HALTED"):std::string("PAUSED")));
  fullLine(2,std::string(width,'='));
  const auto& r=cpu.reg;auto pair=[&](uint8_t h,uint8_t l){return hex16(uint16_t(h<<8|l));};
  at(3,2,"REGISTERS / FLAGS");at(3,42,"DISASSEMBLY (PC ->)");
  at(4,2,"AF  "+hex16(cpu.af())+"  AF' "+pair(r.aa,r.ff));
  at(5,2,"BC  "+hex16(cpu.bc())+"  BC' "+pair(r.bb,r.cc));
  at(6,2,"DE  "+hex16(cpu.de())+"  DE' "+pair(r.dd,r.ee));
  at(7,2,"HL  "+hex16(cpu.hl())+"  HL' "+pair(r.hh,r.ll));
  at(8,2,"IX  "+hex16(r.ix)+"  IY  "+hex16(r.iy));
  at(9,2,"PC  "+hex16(r.pc)+"  SP  "+hex16(r.sp));
  at(10,2,"I   "+hex8(r.i)+"    R   "+hex8(r.r)+"  IM "+std::to_string(r.im));
  at(11,2,"IFF1 "+std::to_string(r.iff1)+" IFF2 "+std::to_string(r.iff2));
  std::string f="S:"+std::to_string(bool(r.f&FS))+" Z:"+std::to_string(bool(r.f&FZ))+" H:"+std::to_string(bool(r.f&FH))+" P/V:"+std::to_string(bool(r.f&FP));
  at(12,2,f);at(13,2,"N:"+std::to_string(bool(r.f&FN))+" C:"+std::to_string(bool(r.f&FC))+" 5:"+std::to_string(bool(r.f&FY))+" 3:"+std::to_string(bool(r.f&FX)));
  uint16_t addr=r.pc;for(int i=0;i<11&&i<height-16;++i){Decoded d=disassemble(cpu,addr);std::string start=i==0?"-> ":breaks.count(addr)?" B ":"   ";at(4+i,42,start+hex16(addr)+"  "+d.bytes+std::string(d.bytes.size()<17?17-d.bytes.size():1,' ')+d.text);addr=uint16_t(addr+std::max(1,d.length));}
  int memrow=std::max(15,height-12);
  const bool twoPanels=memrow>=24; // On compact terminals, V switches the lower pane.
  if(twoPanels&&show_console){int consoleStart=16;consolePanel(consoleStart,memrow-consoleStart-2);}
  fullLine(memrow,std::string(width,'-'));
  if(!twoPanels&&show_console){
    consolePanel(memrow+1,7);
  }else{
    fullLine(memrow+1,show_ports?" LAST PORT WRITES (press P for memory)":" MEMORY  [address "+hex16(memory_view)+"]  (* byte changed since last step)");
    for(int i=0;i<7&&memrow+2+i<height-3;++i){
      if(show_ports){int idx=int(cpu.output.size())-1-i;if(idx>=0){auto v=cpu.output[size_t(idx)];fullLine(memrow+2+i," Port "+hex16(v.port)+" (low "+hex8(uint8_t(v.port))+") <- "+hex8(v.value));}else fullLine(memrow+2+i,"");continue;}
      uint16_t a=uint16_t((memory_view&0xfff0)+(i*16));std::string t=" "+hex16(a)+"  ";std::string ascii;
      for(int j=0;j<16;++j){uint16_t k=uint16_t(a+j);uint8_t v=cpu.read(k);t+=hex8(v)+(v!=before[k]?"*":" ");ascii+=(v>=32&&v<127?char(v):'.');}
      fullLine(memrow+2+i,t+" |"+ascii+"|");
    }
  }
  fullLine(height-3,std::string(width,'-'));
  fullLine(height-2," Instructions "+std::to_string(cpu.instructions)+"   T-states "+std::to_string(cpu.cycles)+"   Breakpoints "+std::to_string(breaks.size())+"   RX "+std::to_string(cpu.console.pendingInput())+"   Clock: "+describeClock(clock_speed)+"   "+status);
  fullLine(height-1," [S] Step  [R] Run/Pause  [I] Input line  [V] Console/Memory  [M] Memory");
  fullLine(height," [B] Breakpoint  [G] PC  [P] Ports  [X] Reset  [?] Help  [Q] Quit");
  std::cout<<std::flush;
 }
 void executeStep(){try{cpu.step();}catch(const std::exception& e){running=false;status=std::string("CPU error: ")+e.what();}if(cpu.reg.halted){running=false;status="HALT instruction executed";}}
 void runSlice(){const uint64_t deadline=cpu.cycles+clock.quantumTStates();for(int n=0;n<10000&&!interrupted&&running;++n){if(breaks.count(cpu.reg.pc)){running=false;status="Breakpoint "+hex16(cpu.reg.pc);break;}executeStep();if(cpu.reg.halted||(clock.enabled()&&cpu.cycles>=deadline))break;}if(keyAvailable()){int c=key();if(c!=-1){running=false;status="Paused by keyboard";if(std::tolower(static_cast<unsigned char>(c))=='i')handle(c);}}}
 void handle(int k){if(k<0)return;k=std::tolower(static_cast<unsigned char>(k));try{switch(k){
 case 'q':interrupted=1;break;
 case 's':running=false;before=cpu.mem;executeStep();status=cpu.reg.halted?"HALT":"Stepped one instruction";break;
 case 'r':if(cpu.reg.halted)status="CPU halted; reset to restart";else{running=!running;if(running)clock.restart(cpu.cycles);status=running?"Executing":"Paused";}break;
 case 'b':{std::string v=ask("Breakpoint address (default PC)");uint16_t a=v.empty()?cpu.reg.pc:number16(v);if(breaks.erase(a))status="Removed breakpoint "+hex16(a);else{breaks.insert(a);status="Added breakpoint "+hex16(a);}break;}
 case 'm':{auto v=ask("Memory address");if(!v.empty()){memory_view=number16(v);show_ports=false;show_console=false;}break;}
 case 'g':{auto v=ask("Set PC");if(!v.empty()){cpu.reg.pc=number16(v);cpu.reg.halted=false;status="PC set to "+hex16(cpu.reg.pc);}break;}
 case 'p':show_ports=!show_ports;show_console=false;break;
 case 'v':show_console=!show_console;break;
 case 'i':{auto line=inputPrompt("Console input");if(line){cpu.console.queueInput(*line);cpu.console.queueInput("\r");status="Queued "+std::to_string(line->size()+1)+" RX byte(s), including CR";}else status="Input cancelled";break;}
 case 'x':cpu.reset(entry,initial_sp,false);before=cpu.mem;running=false;memory_view=entry;status="CPU reset; RAM retained";break;
 case '?':status="S step R run I queue input V view M memory P ports B breakpoint G PC X reset Q quit";break;
 }}catch(const std::exception& e){status=e.what();}}
public:
 Tui(Cpu& c,ClockLimiter& pacing,std::optional<uint64_t> hz,uint16_t p,uint16_t sp):cpu(c),clock(pacing),clock_speed(hz),entry(p),initial_sp(sp),memory_view(p){before=cpu.mem;}
 void run(){RawTerminal tty;if(!tty.active)throw std::runtime_error("Interactive mode requires a TTY; try --steps N");
  std::signal(SIGINT,onInterrupt);refresh();
  while(!interrupted){
   if(running){
    before=cpu.mem; // Snapshot once per displayed frame, not once per instruction.
    const auto redraw_at=std::chrono::steady_clock::now()+std::chrono::milliseconds(70);
    do {runSlice();clock.wait(cpu.cycles);} while(running&&!interrupted&&std::chrono::steady_clock::now()<redraw_at);
   }else handle(key());
   if(!interrupted)refresh();
  }
  std::cout<<"\033[2J\033[H";}
};
}
int main(int argc,char** argv){
 try{std::string file,format="auto";std::optional<uint16_t> origin,pc;uint16_t sp=0xfffe;std::optional<uint64_t> steps;bool trace=false,console_mode=false;std::optional<uint64_t> clock_speed;std::optional<std::pair<uint16_t,size_t>> dump;std::vector<std::pair<bool,std::string>> inputs;
 for(int i=1;i<argc;i++){std::string a=argv[i];auto val=[&](){if(i+1>=argc)throw std::runtime_error("Missing value for "+a);return std::string(argv[++i]);};
  if(a=="--help"||a=="-h"){help();return 0;}
  else if(a=="--format")format=val();else if(a=="--address")origin=number16(val());else if(a=="--pc")pc=number16(val());else if(a=="--sp")sp=number16(val());else if(a=="--steps")steps=decimalCount(val());else if(a=="--clock")clock_speed=parseClock(val());else if(a=="--trace")trace=true;else if(a=="--console")console_mode=true;else if(a=="--input")inputs.emplace_back(false,val());else if(a=="--input-file")inputs.emplace_back(true,val());else if(a=="--dump"){auto s=val();auto k=s.find(':');if(k==std::string::npos)throw std::runtime_error("--dump expects ADDRESS:LENGTH (hex)");dump=std::make_pair(number16(s.substr(0,k)),size_t(parseNumber(s.substr(k+1))));}
  else if(a.rfind("-",0)==0)throw std::runtime_error("Unknown option: "+a);else if(!file.empty())throw std::runtime_error("Only one file may be loaded");else file=a;
 }
 if(file.empty()){help();return 2;}
 if(console_mode&&(steps||trace||dump))throw std::runtime_error("--console cannot be combined with --steps, --trace or --dump");
 Cpu c;auto loaded=loadFile(c,file,origin,format);
 uint16_t start=pc.value_or(origin.value_or(loaded.entry));c.reset(start,sp,false);
 for(const auto& [file,text]:inputs){
   if(!file){c.console.queueInput(text);c.console.queueInput("\r");}
   else{std::ifstream f(text,std::ios::binary);if(!f)throw std::runtime_error("Cannot open input file: "+text);std::string bytes((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());if(!f.eof()&&f.fail())throw std::runtime_error("Cannot read input file: "+text);c.console.queueInput(bytes);}
 }
 ClockLimiter clock(clock_speed);
 if(console_mode){runConsole(c,clock);return 0;}
 if(steps||!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)){
  uint64_t limit=steps.value_or(1000000);if(!steps)std::cerr<<"Non-interactive terminal: running up to 1,000,000 instructions (use --steps to override).\n";
  clock.restart(c.cycles);
  uint64_t next_clock_check=c.cycles+clock.quantumTStates();
  for(uint64_t j=0;j<limit&&!c.reg.halted;++j){
   if(trace){auto d=disassemble(c,c.reg.pc);std::cout<<hex16(d.address)<<"  "<<d.bytes<<"  "<<d.text<<'\n';}
   c.step();
   // Throttle in small batches. Single stepping through the TUI is unpaced.
   if(clock.enabled()&&c.cycles>=next_clock_check){clock.wait(c.cycles);next_clock_check=c.cycles+clock.quantumTStates();}
  }
  clock.wait(c.cycles);
  std::cout<<"Loaded "<<loaded.bytes<<" bytes. ";printRegisters(c);
  if(!c.console.transcript().empty())std::cout<<"Console output: \""<<escapeGuestOutput(c.console.transcript())<<"\"\n";
  if(dump)dumpMemory(c,dump->first,dump->second);
  return c.reg.halted?0:(steps?0:3);
 }
 Tui tui(c,clock,clock_speed,start,sp);tui.run();return 0;
 }catch(const std::exception& e){std::cerr<<"z80emu: "<<e.what()<<'\n';return 1;}
}
