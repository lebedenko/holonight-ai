# Development setup

See [DEVELOPMENT.md](../DEVELOPMENT.md) for native CMake commands, installed provider
prefixes and the pinned shared acceptance image. Use `task build`, `task test`,
`task format`, and `task check`; installation/runtime changes need `task check:integration`.

`holonight-chat` is a regular Qt Quick desktop window. Offscreen tests do not require
a compositor session. Preserve generated QML metadata and run `task qmltypes-check`
after CMake/QML registration changes. Coverage remains available via `task coverage`.
