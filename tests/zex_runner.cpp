// CP/M compatibility harness for Frank Cringle's ZEXDOC and ZEXALL.
// Standalone test executable: does not change the emulator's virtual hardware.
#include "cpu.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

using z80emu::Cpu;

static uint64_t parseLimit(const std::string& s) {
    uint64_t n=0;
    auto [p,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if (ec!=std::errc{} || p!=s.data()+s.size() || !n)
        throw std::runtime_error("Invalid positive instruction limit: " + s);
    return n;
}

static std::vector<uint8_t> load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if(!file) throw std::runtime_error("Cannot open CP/M .com file: " + filename);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    if(bytes.empty() || bytes.size()>0xff00)
        throw std::runtime_error("Invalid .com length (must be 1..65280 bytes)");
    return bytes;
}

int main(int argc,char** argv) {
    if(argc < 2) {
        std::cerr << "Usage: z80zex path/to/zexdoc.com [--max-steps N] [--continue-on-error]\n"
                     "       z80zex path/to/zexall.com [--max-steps N] [--continue-on-error]\n"
                     "Runs a CP/M .COM instruction exerciser against the existing CPU core.\n"
                     "Exit 0: all 67 cases passed and program terminated; 1: failure; 124: step limit.\n";
        return 2;
    }
    try {
        uint64_t maxSteps=10000000000ULL;
        bool continueOnError=false;
        for(int i=2;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--continue-on-error") continueOnError=true;
            else if(arg=="--max-steps" && i+1<argc) maxSteps=parseLimit(argv[++i]);
            else throw std::runtime_error("Unknown or incomplete option: " + arg);
        }
        auto bytes=load(argv[1]);
        Cpu cpu;
        cpu.reset(0x100,0xf000,true);
        std::copy(bytes.begin(),bytes.end(),cpu.mem.begin()+0x100);
        // A .COM program is entered at 0100H with the CP/M BDOS vector at 0005H.
        // We intercept the *arrival* at BDOS instead of executing operating-system code.
        std::string line;
        bool failed=false;
        uint64_t passed=0, errors=0;
        auto started=std::chrono::steady_clock::now();
        auto countLine=[&](const std::string& outputLine) {
            // Valid group lines are padded with any number of dots, including
            // fewer than four: the old parser undercounted those groups.
            if(outputLine.size()>=4 && outputLine.ends_with("  OK")) ++passed;
            if(outputLine.find("ERROR")!=std::string::npos){failed=true;++errors;}
        };
        auto character=[&](uint8_t ch){
            if(ch=='\r') return;
            if(ch=='\n') {
                std::cout << line << '\n';
                countLine(line);
                line.clear();
            } else if(line.size()<4096) line.push_back(static_cast<char>(ch));
        };
        bool completed=false;
        uint64_t steps=0;
        for(;steps<maxSteps;++steps){
            if(cpu.reg.pc==0x0000){completed=true;break;}
            if(cpu.reg.pc==0x0005){
                switch(cpu.reg.c){
                    case 2: character(cpu.reg.e); break;
                    case 9: {
                        uint16_t addr=cpu.de();
                        bool terminated=false;
                        for(unsigned i=0;i<65536;++i) {
                            uint8_t c=cpu.read(addr++);
                            if(c=='$'){terminated=true;break;}
                            character(c);
                        }
                        if(!terminated)throw std::runtime_error("Unterminated CP/M function 9 string");
                        break;
                    }
                    default: throw std::runtime_error("Unsupported CP/M BDOS function: " + std::to_string(cpu.reg.c));
                }
                cpu.reg.pc=cpu.read16(cpu.reg.sp);cpu.reg.sp=uint16_t(cpu.reg.sp+2);
            }else if(cpu.reg.halted){throw std::runtime_error("Z80 HALT before CP/M termination");}
            else cpu.step();
            if(failed && !continueOnError)break;
            if(steps && steps%100000000ULL==0) {
                auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
                std::cerr << "[progress] " << steps/1000000 << "M steps, " << passed << " cases passed, "
                          << static_cast<int>(steps/elapsed/1000000) << " MIPS\n";
            }
        }
        if(!line.empty()){
            std::cout << line << '\n';
            countLine(line);
        }
        auto duration=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::cout << "\nZEX summary: " << passed << "/67 cases passed, " << errors << " errors; " << steps
                  << " CPU instructions; " << cpu.cycles << " T-states; " << duration << " seconds\n";
        if(failed){std::cerr << "FAIL: ZEX reported " << errors << " ERROR group(s)\n";return 1;}
        if(!completed){std::cerr << "INCOMPLETE: instruction limit reached (or stopped early)\n";return 124;}
        if(passed!=67){std::cerr << "FAIL: expected exactly 67 passing ZEX test cases\n";return 1;}
        std::cout << "PASS: all 67 ZEX test cases\n";
        return 0;
    }catch(const std::exception& ex){std::cerr << "ERROR: " << ex.what() << '\n';return 1;}
}
