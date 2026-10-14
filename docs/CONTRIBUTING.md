# Contributing to LIC Engine

## Development rules

- Keep changes focused and buildable. Prefer a working thin vertical slice over many empty subsystems.
- Do not report a feature as implemented until connected to the real runtime and tested.
- Preserve existing user work. Do not remove assets, project files, or configuration without a clear reason.
- Do not commit generated binaries, local IDE state, build directories, or machine-specific paths.
- Pin external dependencies and record licenses and attribution before adopting them.
- Keep platform-specific code behind platform interfaces.
- Avoid global mutable state, hidden ownership, silent exception swallowing, and unexplained magic numbers.
- Public headers should be self-contained and avoid large implementation-only dependencies.

## C++ style

- Use descriptive names and consistent formatting.
- Prefer RAII and explicit ownership; avoid raw owning pointers.
- Make lifetime and thread-safety contracts visible.
- Use const where it communicates intent; avoid premature template frameworks.
- Keep functions reviewable and testable.
- Document non-obvious invariants, not every line.

## Validation checklist

Before a change is ready:

1. Configure and build from a clean build directory.
2. Run relevant CTest tests.
3. Launch the affected sample/editor when the environment permits.
4. Check logs and compiler warnings; do not hide failures with broad warning suppression.
5. Test relevant failure paths: missing assets, unsupported GPU features, malformed scenes, and window resizing.
6. Update docs and sample projects to match actual behavior.
7. State which checks were run and which could not be run.

## Third-party code

Do not copy code or assets from other engines without checking license compatibility and attribution requirements. Use other engines as architectural inspiration, not as a source of unlicensed code or proprietary assets.

## Scope

Prioritize a reliable desktop foundation, then a scene editor and standalone runtime. Android and other targets follow when core abstractions and the desktop path are stable.
