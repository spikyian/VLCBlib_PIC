# Host tests for the CAN drivers

These run the library's CAN drivers on a PC, against a register model of each
CAN peripheral and a simulated CAN bus. Nothing here is built for the PIC.

```
test/can/run.sh [path to VLCB-defs]     # default: ../VLCB-defs beside the library
```

Needs gcc, python3, git (a clone, not a downloaded zip) and a POSIX shell (Linux, macOS, or WSL on Windows).

## What runs

Each node is a shared object holding one copy of a driver, compiled for one
family with one set of options, and its own register model. The bus loads one
copy per node and moves frames between them with CAN arbitration (lowest
identifier first, a data frame before an RTR). Each node has its own clock,
running a few per cent apart, as real oscillators do.

Two drivers are built for each family:

- **existing**: the drivers as they were before the shared CAN service,
  `can18_can_2.c` / `can18_ecan.c` taken from git at `CAN_LEGACY_REV`
  (default `5366386`; `make_legacy.py` adds the model's sync points to a copy),
  kept to compare against
- **shared**: the library's driver now, `can_service.c` on `can_hw_q83.c` /
  `can_hw_k80.c`

each with and without `CAN_ADDITIONAL_CANID_CHECKS` and `CONSUMED_EVENTS`,
plus `CAN_RX_ACCEPT`.

The tests cover: a frame end to end; six factory-reset nodes enumerating;
a CANID conflict; the RTR reply's CANID; a received RTR not reported as a
message; answering RTRs while the application is busy; frames held until a
CANID, and the back-off when none is free; consuming own events; floods
(order kept, nothing lost unless an overrun is counted); a full transmit
buffer; ESD and diagnostics; mixed buses (existing and shared, Q83 and K80);
and, for the Q83, that the existing and shared drivers put identical frames
on the bus and leave the peripheral in an identical state.

Tests the existing K80 driver is expected to fail are marked so: they
reproduce its defects, and count as a failure only if they pass.

## The register models

`q83/xc.h` follows DS40002265C chapter 38 (FIFO loading and UINC, TXREQ,
the receive FIFO, mode change); the comment at its top lists what is modelled.
`k80/xc.h` models the ECAN as `can18_ecan.c` uses it: the K80 data sheet was
not used, so that behaviour is inherited from the driver and unverified. A
write's effect happens when the driver's `CAN_HW_SYNC()` seam (empty on the
PIC) or the test calls the model.
