# Changelog

Changes to VLCBlib_PIC, newest first. Add each change under **Unreleased** as
it is merged; the Release workflow moves that section under the new version
and uses it as the release notes.

## Unreleased

- `vlcb_version.h`: the library version as macros (`VLCBLIB_VERSION_MAJOR`,
  `_MINOR`, `_PATCH`, `_PRERELEASE`, `_STRING`, `VLCBLIB_VERSION`). A module can
  set `VLCBLIB_REQUIRED` in module.h to refuse to build against an older library.
- Release workflow (Actions > Release): sets the version, tags it and creates the
  GitHub release in one step.
- CAN: the VLCB CAN protocol (CANID, self-enumeration, conflict detection, RTR
  reply, ENUM/CANID, diagnostics, ESD, Transport) is now in one place,
  `can_service.c`, used by both the Q83 and the K80 through a small controller
  layer (`can_hw.h`, `can_hw_q83.c`, `can_hw_k80.c`). Projects build the same
  files as before (`can18_can_2.c` or `can18_ecan.c`) and need no change.
  The K80 gains `CAN_ADDITIONAL_CANID_CHECKS` and five fixes (RTR replies and
  conflict detection under load, CANID bits in the RTR and RTR reply, RTR and
  zero-length frames reported as messages). New optional settings:
  `CAN_RX_ACCEPT(opc)`, `CAN_VIEW_SERVICE`, `CAN_HW_APP`. Host tests in `test/can`.
