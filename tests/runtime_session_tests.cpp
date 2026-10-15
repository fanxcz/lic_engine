#include <lic/scripting/runtime_session.hpp>
#include <lic/scripting/scene_api.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: lic_runtime_session_tests <runtime-exe> <log-file>\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path executable = argv[1];
    const std::filesystem::path logFile = argv[2];
    lic::scripting::RuntimeSession session;
    const auto startup = session.start(executable, {}, executable.parent_path(), logFile,
                                       std::chrono::seconds(3));
    expect(startup.ok, startup.error.c_str());
    expect(startup.output.find("LICAPI SPAWN_BOX 900") != std::string::npos,
           "NEXUS on_start should create a mesh through the persistent runtime");
    expect(session.running(), "the runtime must stay alive after the initial response");

    lic::world::World world;
    const auto created = lic::scripting::apply_scene_commands(startup.output, world);
    expect(created.commands_applied == 2, "startup should apply two scene API commands");
    lic::world::Entity entity{};
    world.each<lic::world::NodeName>([&](lic::world::Entity candidate, const lic::world::NodeName& name) {
        if (name.value == "NEXUS_900") entity = candidate;
    });
    expect(world.alive(entity), "startup should create the runtime-owned test entity");

    const auto frame = session.exchange({1, 16, 100, -50, 1, 0}, std::chrono::seconds(2));
    expect(frame.ok, frame.error.c_str());
    expect(frame.output.find("LICAPI MOVE_BY 900 1 0 -0") != std::string::npos ||
           frame.output.find("LICAPI MOVE_BY 900 1 0 0") != std::string::npos,
           "on_update should receive frame delta and movement input");
    const auto applied = lic::scripting::apply_scene_commands(frame.output, world);
    expect(applied.commands_applied >= 2, "update commands should be applied to the same ECS world");
    expect(std::abs(world.get<lic::world::Transform>(entity).position[0] - 0.01F) < 0.0001F,
           "MOVE_BY from NEXUS should animate the ECS transform");
    expect(world.get<lic::world::Appearance>(entity).color[0] == 1.0F,
           "dash input should update model color via NEXUS");

    const auto nextFrame = session.exchange({1, 16, 0, 100, 0, 1}, std::chrono::seconds(2));
    expect(nextFrame.ok, nextFrame.error.c_str());
    expect(nextFrame.output.find("LICAPI MOVE_BY 900 0 0 1") != std::string::npos,
           "persistent runtime should respond to subsequent frame events");

    session.stop();
    expect(!session.running(), "stopping the script should close the persistent process");
    return EXIT_SUCCESS;
}
