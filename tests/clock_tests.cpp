#include "clock.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace z80emu;
using namespace std::chrono;
namespace {
 void require(bool condition,const char* msg){if(!condition)throw std::runtime_error(msg);}
 void reject(const char* s){try{(void)parseClock(s);}catch(const std::exception&){return;}throw std::runtime_error(std::string("Accepted invalid clock: ")+s);}
}
int main(){try{
 require(parseClock("4000000")==4000000,"frequency in Hz");
 require(parseClock("4MHz")==4000000,"frequency in MHz");
 require(parseClock("2mhz")==2000000,"case insensitive units");
 require(parseClock("8MHz")==8000000,"8MHz");
 require(parseClock("250kHz")==250000,"kHz support");
 require(parseClock("123Hz")==123,"Hz suffix");
 require(describeClock(4000000)=="4 MHz","frequency label");
 require(ClockLimiter(4000000).quantumTStates()==40000,"10ms pacing quantum");
 require(ClockLimiter(500).quantumTStates()==5,"slow clock pacing quantum");
 require(describeClock(std::nullopt)=="unlimited","unlimited label");
 for(const char* s:{"0","-10","oops","4.0MHz","18446744073709551616","1001MHz","","10m"})reject(s);
 ClockLimiter unlimited;unlimited.restart(0);unlimited.wait(100000000);
 // Emulate 45 cycles at 500 Hz (90 ms) and make sure the limiter waits.
 ClockLimiter pacing(500);pacing.restart(0);
 const auto start=steady_clock::now();pacing.wait(45);
 auto elapsed=duration_cast<milliseconds>(steady_clock::now()-start).count();
 require(elapsed>=75,"paced interval was too short");
 require(elapsed<5000,"paced interval took excessively long");
 // New run must establish a fresh baseline: no catch-up or stale deadline.
 pacing.restart(45);const auto resumed=steady_clock::now();pacing.wait(50);
 auto restart_elapsed=duration_cast<milliseconds>(steady_clock::now()-resumed).count();
 require(restart_elapsed>=7,"restart failed to establish a new timing epoch");
 std::cout<<"Clock parsing, validation and pacing passed (90ms interval: "<<elapsed<<"ms)\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
