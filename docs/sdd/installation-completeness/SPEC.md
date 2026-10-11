# HoloNight AI installation completeness

Baseline: `dbf192c39ea11aeaf901c0597a2ad2e3e3a3f96a`. Work package: IC-001.

Install `org.holonight.Chat.desktop` into the configured applications data directory,
with name `HoloNight AI`, direct command `holonight-chat`, icon `holonight-ai`,
`Terminal=false` and categories `Utility;Qt;`. Match Qt desktop-file identity to the launcher
without the suffix. Install the existing SVG into hicolor/scalable/apps; preserve existing
executable and D-Bus service installation. No activation or UI behavior changes.

Changes belong in data/applications, apps/chat/CMakeLists.txt and the application constructor.
Verify desktop-file validation and a clean Release build/staged install, including service files.
Publication and umbrella pin updates are separate handoff steps.

Verified 2026-10-11: fresh Release configure/build and clean staged install passed with GNU 16.2.1
and Qt 6.12.0 against the current pinned Config, System services and Qt providers. Complete logs
contain no build warnings. Source/staged desktop files validate; installed executables and original
service files are present, and fallback SVG matches the existing asset byte-for-byte.
Ownership, collision, migration and modified-file uninstall checks pass in the umbrella suite.
User confirmed application-menu visibility on 2026-10-11 and authorized commit/publication and umbrella pin updates.
Final ecosystem integration remains a separate umbrella check.
