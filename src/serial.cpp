#include "serial.hpp"
#include <algorithm>

namespace z80emu {
void SerialConsole::reset() { incoming_.clear(); outgoing_.clear(); transmitted_=0; }
void SerialConsole::queueInput(std::string_view bytes) {
    for(unsigned char byte : bytes) incoming_.push_back(byte);
}
uint8_t SerialConsole::receive() {
    if(incoming_.empty()) return 0;
    uint8_t value=incoming_.front(); incoming_.pop_front(); return value;
}
uint8_t SerialConsole::status() const {
    return uint8_t(0x02 | (incoming_.empty() ? 0 : 0x01));
}
void SerialConsole::transmit(uint8_t byte) {
    ++transmitted_;
    outgoing_.push_back(static_cast<char>(byte));
    if(outgoing_.size() > OUTPUT_HISTORY_LIMIT)
        outgoing_.erase(0, outgoing_.size()-OUTPUT_HISTORY_LIMIT);
}
}
