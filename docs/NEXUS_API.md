# LIC Engine NEXUS Scene API v1

NEXUS scripts can emit declarative ECS scene commands. Scripts do not receive raw pointers or unrestricted native access: they print `LICAPI` records and LIC Engine validates each command before mutating the world.

Commands apply when **Test Rules** runs. On **START RUN**, scene commands are replayed into the new run's world as well. Object IDs map to names `NEXUS_<id>`. Positions/scales use integer hundredths; colors and surface properties use integer thousandths.

- `lic_spawn_box(id, x, y, z, scale, r, g, b)`: create/replace a cube mesh node.
- `lic_set_position(id, x, y, z)`: set position.
- `lic_set_rotation(id, x, y, z)`: set Euler rotation in degrees.
- `lic_set_scale(id, x, y, z)`: set per-axis scale.
- `lic_set_color(id, r, g, b)`: set RGB from 0 to 1000.
- `lic_set_surface(id, metallic, roughness, emission)`: metallic [0,1000], roughness [40,1000], emission [0,4000].
- `lic_set_enabled(id, enabled)`: toggle rendering (0/1).
- `lic_destroy(id)`: destroy a script-owned node.

See `examples/nexus/SceneApiDemo.nx`. This is a validated setup/scene API; per-frame update, input and collision callbacks are not implemented yet. They require a persistent embedded NEXUS runtime instead of launching a compiled program once per operation.
