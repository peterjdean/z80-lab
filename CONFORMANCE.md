# Z80 CPU Conformance — v0.3.3

## Independent test results: 67/67 on both suites

On **15 October 2026**, Euler (Ubuntu Server / ARM64 / Parallels) ran both of Frank Cringle's Z80 instruction exercisers against the v0.3.2 CPU implementation:

| Suite | Instruction groups passed | Reported errors | Emulated instructions | T-states | Wall time |
| --- | ---: | ---: | ---: | ---: | ---: |
| **ZEXDOC** | **67/67** | **0** | 5,764,169,610 | 46,734,975,782 | 36.1784 seconds |
| **ZEXALL** | **67/67** | **0** | 5,764,169,610 | 46,734,975,782 | 36.108 seconds |

The logs completed with `PASS: all 67 ZEX test cases`. Both suites reported no CRC mismatches. Earlier v0.3.1 runs had failed the DAA and BIT (HL) flag cases; those errors were corrected in v0.3.2. The harness's test-name counting logic was also fixed.

**v0.3.3 leaves the Z80 CPU interpreter unchanged** (`src/cpu.cpp` and `src/cpu.hpp` are identical to v0.3.2). It adds user-selectable virtual-time pacing to `z80emu`, an independent `src/clock.cpp` limiter, new tests and documentation, and improvements to the debugger's run-loop bookkeeping. The separate `z80zex` conformance runner is unchanged. This means the existing validated instruction engine remains intact, but v0.3.3 has *not been independently rerun through both complete ZEX suites* as part of this packaging check.

## Automated CTest suite

The project's nine automated tests cover CPU/loader/monitor behaviour, 1,049,600 exhaustive 8-bit ALU comparisons, 18,432 focused flag comparisons, ZEX runner accounting and rejection behaviour, clock parsing and pacing, invalid clock options, and both throttled/unlimited execution of a timed example.

Build and run them:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Optional: repeat ZEXDOC and ZEXALL on Euler

The external ZEX binaries are not redistributed with this MIT-licensed project. The included script downloads them from the public `agn453/ZEXALL` repository and verifies pinned hashes:

```bash
python3 scripts/fetch_zex.py
bash scripts/run_zex.sh --max-steps 100000000000
```

Logs are stored at `reports/zexdoc.log` and `reports/zexall.log`. The optional `--continue-on-error` flag collects more failing groups instead of stopping at the first CRC mismatch. Neither external suite is throttled by `--clock` — its target is `z80emu`, not `z80zex`. **Keep the tests unthrottled**: roughly 46.7 billion T-states would take over three hours at 4 MHz.

## Scope of conformance

The ZEX results are strong evidence of correct Z80 instruction/flag semantics for the cases covered by both exercisers, including the undocumented flag details exercised by ZEXALL. They do **not** certify transistor-level electrical behaviour, bus/machine-cycle exactness, external interrupts, memory contention, UART timing or other peripherals. In particular, `--clock` limits average T-state throughput; it does not emulate individual Z80 clock edges.

The CPU implementation is MIT-licensed. Frank Cringle's ZEX tests are GPL-2.0 and are kept outside the source distribution.
