#include "clock.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace z80emu {
uint64_t parseClock(const std::string& value) {
 if(value.empty()) throw std::runtime_error("--clock needs a positive frequency in Hz (e.g. 4000000 or 4MHz)");
 std::string cleaned;
 for(unsigned char c : value) if(!std::isspace(c)) cleaned.push_back(char(std::tolower(c)));
 std::string digits=cleaned;
 uint64_t multiplier=1;
 if(digits.size()>3 && digits.substr(digits.size()-3)=="mhz") {
  digits.resize(digits.size()-3);multiplier=1000000;
 } else if(digits.size()>3 && digits.substr(digits.size()-3)=="khz") {
  digits.resize(digits.size()-3);multiplier=1000;
 } else if(digits.size()>2 && digits.substr(digits.size()-2)=="hz") {
  digits.resize(digits.size()-2);
 }
 if(digits.empty() || !std::all_of(digits.begin(),digits.end(),[](unsigned char c){return c>='0'&&c<='9';}))
  throw std::runtime_error("Invalid --clock frequency: "+value+" (try 4000000 or 4MHz)");
 size_t pos=0;uint64_t n=0;
 try {n=std::stoull(digits,&pos,10);} catch(const std::exception&) {throw std::runtime_error("--clock frequency out of range: "+value);}
 if(pos!=digits.size() || n==0 || n>1000000000ULL/multiplier)
  throw std::runtime_error("--clock must be in the range 1 to 1000000000 Hz");
 return n*multiplier;
}
std::string describeClock(std::optional<uint64_t> hz) {
 if(!hz)return "unlimited";
 if(*hz%1000000ULL==0)return std::to_string(*hz/1000000ULL)+" MHz";
 if(*hz%1000ULL==0)return std::to_string(*hz/1000ULL)+" kHz";
 return std::to_string(*hz)+" Hz";
}
void ClockLimiter::restart(uint64_t current_tstates) {
 start_tstates_=current_tstates;
 started_=std::chrono::steady_clock::now();
 initialized_=true;
}
void ClockLimiter::wait(uint64_t current_tstates) {
 if(!hz_)return;
 if(!initialized_||current_tstates<start_tstates_){restart(current_tstates);return;}
 // Work against an absolute deadline so batch-by-batch rounding does not drift.
 const double seconds=double(current_tstates-start_tstates_)/double(*hz_);
 const auto target=started_+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
 auto now=std::chrono::steady_clock::now();
 if(target<=now)return;  // Behind schedule: do not sleep or artificially slow further.
 std::this_thread::sleep_until(target);
}
}
