#include "scenes/scene.h"

namespace scenes {

bool Assets::load() {
    struct TextureFile {
        gfx::Texture* texture;
        const char* path;
    };
    const TextureFile textures[] = {
        {&liveries[0], "assets/models/su35s_vks.ftx"},
        {&liveries[1], "assets/models/su35s_901.ftx"},
        {&ocean, "assets/textures/ocean.ftx"},
        {&cloud, "assets/textures/cloud.ftx"},
        {&sun, "assets/textures/sun.ftx"},
        {&glow, "assets/textures/fx_glow.ftx"},
        {&smoke, "assets/textures/fx_smoke.ftx"},
        {&flame, "assets/textures/fx_flame.ftx"},
        {&ring, "assets/textures/fx_ring.ftx"},
        {&glint, "assets/textures/fx_glint.ftx"},
        {&shadow, "assets/textures/fx_shadow.ftx"},
        {&line, "assets/textures/fx_line.ftx"},
        {&ribbon, "assets/textures/fx_ribbon.ftx"},
        {&waves, "assets/textures/fx_waves.ftx"},
    };
    if (!su35s.load("assets/models/su35s.fsm")) {
        error = "assets/models/su35s.fsm";
        return false;
    }
    for (const auto& t : textures) {
        if (!t.texture->load(t.path)) {
            error = t.path;
            return false;
        }
    }
    return true;
}

}  // namespace scenes
