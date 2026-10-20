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
