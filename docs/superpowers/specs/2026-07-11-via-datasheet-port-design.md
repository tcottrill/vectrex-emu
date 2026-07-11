# VIA 6522 datasheet-fix port (from Pet-GPT-2026) — Design

Date: 2026-07-11
Branch: `fix/via-datasheet-port`

## Background

The PET emulator's VIA 6522 core (`Pet-GPT-2026/petemu/petsrc/via6522.{h,cpp}`)
went through a tier-2 datasheet review with a 69-check test suite. The Vectrex
VIA (`emulator/project/via6522.{h,cpp}`) is a *different lineage* (vecx-derived,
C-style, half-cycle `sstep0/sstep1`, hooks into vecx.cpp) and already has its
own fix pass (T1CL read semantics, CB2 input decode, CA1/CB1 handshake restore,
SR shift-in sampling). A fix-by-fix comparison found only two safe, portable
gaps plus one missing asset: a regression test suite.

Explicitly **out of scope** (compared, rejected):

- N+2 timer reload timing (one-cycle 0xFFFF pass) — would shift T1 periods by
  ~1 cycle and visibly change vector lengths; Vectrex games are calibrated to
  current vecx timing.
- IRA/IRB input latching (ACR0/1) — unused by Vectrex software.
- T2 PB6 pulse-count mode (ACR5) — PB6 is a cartridge line; not used.
- Any change to T1/T2 counting, SR divide-by-2 clock, PB7 (/RAMP), or CB2
  (/BLANK) drive logic.

## Fix 1: T1LH write ($7) clears IFR6

`via_write_reg` case 0x7 currently only stores the latch byte. Per the
datasheet, writing T1L-H also clears the T1 interrupt flag (the "re-program
the next interval from inside the IRQ handler" ack idiom).

```c
case 0x7: via_t1lh = data;
          via_ifr &= ~0x40;
          int_update();
          break;
```

Reading T1LH stays clear-free (already correct).

## Fix 2: write-side port flag clears

The core clears CA/CB flags on ORA/ORB *reads* but not *writes*; the real chip
does both. Reuse the exact mask logic already present in the read paths:

- **ORB write (case 0x0):** clear IFR4 (CB1) always; clear IFR3 (CB2) unless
  CB2 is in an independent-input mode: `if ((via_pcr & 0xA0) != 0x20)`.
  Then `int_update()`.
- **ORA write (case 0x1):** clear IFR1 (CA1) always; clear IFR0 (CA2) unless
  independent: `if ((via_pcr & 0x0A) != 0x02)`. Then `int_update()`. Placed
  *before* the fallthrough to case 0xF so writes to register $F
  (ORA no-handshake) perform **no** clears, matching the datasheet and the
  PET core.
- **ORA read restructure (discovered during planning):** the read path puts
  the CA1/CA2 flag clears inside the shared `case 0xF` block, so reading
  register $F (no-handshake ORA) wrongly clears the flags today. Move the
  clears (and the CA2 handshake trigger) up into `case 0x1` before the
  fallthrough; `case 0xF` becomes a pure pin read.

## Fix 3: CA1/CB1 edge polarity uses wrong PCR bits (discovered during planning)

`via_signal_ca1_edge` selects positive/negative edge with `via_pcr & 0x02`
(a CA2-mode bit) and `via_signal_cb1_edge` with `via_pcr & 0x20` (a CB2-mode
bit). Per the datasheet, CA1 polarity is PCR bit 0 (`0x01`) and CB1 polarity
is PCR bit 4 (`0x10`) — same as the PET core. Currently dormant (nothing in
vecx.cpp calls these functions), so zero behavior change today; fixed for
correctness and future wiring (CA1 = PSG IO7).

## Test suite: `emulator/tests/via6522_tests.cpp`

Standalone, compiles `via6522.cpp` alone; stubs the four `via_hook_*`
functions normally provided by vecx.cpp (`via_hook_on_orb_written`,
`via_hook_on_ora_written`, `via_hook_read_port_a` returns `ora`,
`via_hook_get_compare_bit` returns 0). Uses the PET suite's tiny
CHECK/CHECK_EQ framework. Because the VIA state is static globals,
each test starts with `via_reset()` (no multi-instance tests).

Two categories:

1. **New-fix tests** — T1LH write acks IFR6; ORB write clears IFR4/IFR3;
   ORA write clears IFR1/IFR0; independent CB2/CA2 input modes exempt from
   the C2 clear; reg $F read/write performs no clears.
2. **Regression locks on existing behavior** (assert current, measured
   values — the point is to freeze timing, not correct it):
   reset defaults; T1 free-run period as currently produced; T1/T2 one-shot
   fires exactly once; T1CL/T2CL reads clear only the flag; SR mode-100 CB2
   square-wave period with pattern 0x55; CB2 input edge decode positive vs
   negative; CA1/CB1 edge sets flag and restores CA2/CB2 handshake level;
   `via_get_cb2_output()` mux (SR modes vs PCR level); IFR write-1-to-clear;
   IER set/clear semantics; IFR7 summary bit.

## Runner: `emulator/tests/run_tests.bat`

PET-style: vcvars64 + `cl /std:c++17`, builds and runs `via6522_tests.exe`
and the existing `test_host_view.cpp` (which currently has no runner).
Exit code 0/1, per-suite build logs.

## Acceptance

- All tests pass (new fixes green, regression locks unchanged).
- Emulator builds and a smoke run (boot + Mine Storm) looks/behaves normally.

## Risk handling

If a regression-lock test exposes the current core disagreeing with itself,
stop and report — do not silently "fix" locked behavior.
