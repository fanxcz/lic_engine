# NEXUS scripting in LIC Engine

LIC Engine bundles the NEXUS 1.1.0 compiler from [fanxcz/nexus-lang](https://github.com/fanxcz/nexus-lang). The scene editor opens **NEXUS Rules** from the top toolbar and creates the project script at:

`%LOCALAPPDATA%/LIC Engine/Projects/Default/Scripts/GameRules.nx` on Windows (the equivalent user-data project directory on Linux/macOS).

The compiler is a real NEXUS program compiler—not a NEXUS-like parser written in the engine. The panel provides **Save**, **Check**, **Build**, and **Test Rules**. VOIDBOUND compiles and runs this script at the start of each new run. If the compiler or script fails, the game logs the diagnostic and keeps safe built-in defaults.

## Game-rule output API

A rules script uses the ordinary NEXUS `print()` function. The engine reads the following output keys and clamps them to safe ranges:

| Output line | Meaning | Accepted range |
|---|---|---:|
| `LICCFG_PLAYER_HEALTH=100` | Starting and maximum health | 25–500 |
| `LICCFG_PLAYER_SPEED=230` | Player move speed, value divided by 100 | 80–600 |
| `LICCFG_ENEMY_SPEED=68` | Base enemy speed, value divided by 100 | 20–250 |
| `LICCFG_FIRE_INTERVAL_MS=240` | Time between pulses | 80–2000 ms |
| `LICCFG_DASH_COOLDOWN_MS=1350` | Dash cooldown | 250–5000 ms |

Minimal example:

```nx
fn main() {
    print("LICCFG_PLAYER_HEALTH=125")
    print("LICCFG_PLAYER_SPEED=270")
    print("LICCFG_ENEMY_SPEED=72")
    print("LICCFG_FIRE_INTERVAL_MS=190")
    print("LICCFG_DASH_COOLDOWN_MS=1100")
}
```

Save the script and click **Test Rules** to compile and preview its output. Press **Play Game** and choose **START RUN** to apply it to the actual game simulation. Values absent from the output retain the defaults. The NEXUS native backend currently supports a subset of the full language; use the NEXUS diagnostics shown in the panel if a construct is unsupported.

The packaged NEXUS executable looks for `runtime/runtime.cpp` beside the compiler. Keep the complete LIC Engine archive extracted; copying `nexus.exe` alone will not include its required runtime source or standard library.
