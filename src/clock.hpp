#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace z80emu {
// A wall-clock limiter for the emulated CPU. It is not a real hardware clock:
// T-states are accounted for by Cpu::step(), and execution is paced in batches.
uint64_t parseClock(const std::string& value);
std::string describeClock(std::optional<uint64_t> hz);

class ClockLimiter {
public:
 explicit ClockLimiter(std::optional<uint64_t> hz = std::nullopt) : hz_(hz) {}
 bool enabled() const { return hz_.has_value(); }
 // About 10 milliseconds of virtual CPU work before the next pacing check.
 uint64_t quantumTStates() const { return hz_ ? std::max<uint64_t>(1, *hz_/100) : 0; }
 void restart(uint64_t current_tstates);
 void wait(uint64_t current_tstates);
private:
 std::optional<uint64_t> hz_;
 uint64_t start_tstates_ = 0;
 std::chrono::steady_clock::time_point started_{};
 bool initialized_ = false;
};
}
