#pragma once
#include "cpu.hpp"
#include <optional>
#include <string>
#include <string_view>
namespace z80emu {
struct LoadResult {uint16_t entry=0; size_t bytes=0; bool has_entry=false;};
LoadResult loadFile(Cpu&,const std::string& path,std::optional<uint16_t> origin={},std::string format="auto");
// Browser-safe loader: parses in-memory data, without requiring a host filesystem.
LoadResult loadData(Cpu&,std::string_view data,std::optional<uint16_t> origin={},std::string format="auto");
}
