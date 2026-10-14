# NEXUS gameplay scripts in LIC Engine

LIC Engine bundles the NEXUS 1.1.0 compiler from [fanxcz/nexus-lang](https://github.com/fanxcz/nexus-lang). The editor's **NEXUS Rules** panel opens the project copy at:

`%LOCALAPPDATA%/LIC Engine/Projects/Default/Scripts/GameRules.nx` on Windows (the equivalent user-data project directory on Linux/macOS).

The bundled source of truth is `assets/scripts/GameRules.nx`. It declares the game-balance functions in NEXUS and prints typed `LICCFG_*=value` rows. On each new run, LIC Engine checks/builds the script with the NEXUS native backend, runs it, parses the output and clamps the values to safe ranges before starting the simulation. The panel supports **Save**, **Check**, **Build**, **Test Rules** and **Reload**. On failure the editor shows the compiler diagnostic and falls back to the built-in defaults.

## Script-controlled balance

The script controls player health and speed, pulse interval, dash cooldown, wave population and cap, enemy base health and wave scaling, elite enemy cadence and bonus health, scoring, level thresholds, XP pickup lifetime, spawn interval scaling, rapid-fire upgrade, heavy-round damage, vitality upgrade and mobility upgrade.

A minimal function:

```nx
fn player_health() -> i64 {
    return 125
}

fn main() {
    print("LICCFG_PLAYER_HEALTH=" + str_i64(player_health()))
}
```

Edit the functions in `GameRules.nx`, save, then click **Test Rules**. Start a new run with **START RUN** to apply the updated values to actual gameplay.

NEXUS currently acts as the game's balance/rules layer. Vulkan commands, the platform event loop, resource lifetime, and ECS storage remain native C++ because the current NEXUS compiler/runtime does not yet expose an in-process LIC Engine host API for per-frame script callbacks. The next integration milestone is an engine API that lets NEXUS scripts drive entity creation, transforms, input and gameplay callbacks directly.

## Compiler files in the shipped archive

Keep the full archive extracted. The compiler uses `runtime/runtime.cpp` to build NEXUS programs, with the `stdlib/` directory and compiler license alongside it. The editor also copies the bundled `assets/scripts/GameRules.nx` into the writable project on first launch, so personal edits are not overwritten by updates.
