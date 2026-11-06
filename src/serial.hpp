#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace z80emu {
// A minimal polled, full-duplex, byte-oriented console peripheral.
// Decode uses the low eight bits of the Z80 I/O address.
//   00 (OUT): transmit one character (no automatic local echo).
//   01 (IN):  receive one character from a FIFO, or 00 if empty.
//   02 (IN):  status: bit 0 = RX ready, bit 1 = TX ready (always 1).
// Characters are bytes, not UTF-8 Unicode code points.
class SerialConsole {
public:
    static constexpr uint8_t TX_DATA = 0x00;
    static constexpr uint8_t RX_DATA = 0x01;
    static constexpr uint8_t STATUS = 0x02;
    static constexpr size_t OUTPUT_HISTORY_LIMIT = 16384;

    struct Snapshot {
        std::string incoming;
        std::string outgoing;
        uint64_t transmitted = 0;
    };
    Snapshot saveState() const;
    void restoreState(const Snapshot& saved);
    void reset();
    void queueInput(std::string_view bytes);
    uint8_t receive();
    uint8_t status() const;
    void transmit(uint8_t byte);
    size_t pendingInput() const { return incoming_.size(); }
    const std::string& transcript() const { return outgoing_; }
    uint64_t transmittedBytes() const { return transmitted_; }

private:
    std::deque<uint8_t> incoming_;
    std::string outgoing_;
    uint64_t transmitted_ = 0;
};
}
