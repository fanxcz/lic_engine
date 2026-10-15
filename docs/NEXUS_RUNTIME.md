# Persistent NEXUS gameplay runtime

LIC Engine can run a compiled NEXUS runtime program as one persistent process. The editor sends event records over stdin and reads validated `LICAPI` commands over stdout; each script response ends with `LICFRAME_END`. The child has no shell access from the engine protocol and stderr is redirected to the project log. Frame responses have a time limit and the process is stopped when the session ends.

The sample `assets/scripts/GameRuntime.nx` defines `on_start()` and `on_update(delta_ms, move_x, move_z, dash, fire)`. The demo moves a script-owned mesh in response to input and changes its material. Input axes use centi-units per second; delta is milliseconds. The script API starts with `lic_spawn_box`, `lic_move_by`, `lic_set_color` and `lic_set_surface`.

The initial integration uses a persistent child process rather than embedding a VM in C++; this keeps the NEXUS compiler/runtime unchanged while making frame callbacks possible. Scripts are native compiled programs, so the complete compiler/runtime files must stay with LIC Engine.
