#pragma once

#include <madrona/types.hpp>
#include <madrona/render/mw.hpp>
#include <madrona/importer.hpp>
#include <madrona/exec_mode.hpp>
#include <memory>

namespace madrona::viz {

class Viewer {
public:
    struct Config {
        int gpuID;
        uint32_t renderWidth;
        uint32_t renderHeight;
        uint32_t numWorlds;
        uint32_t maxViewsPerWorld;
        uint32_t maxInstancesPerWorld;
        ExecMode execMode;
    };

    Viewer(const Config &cfg);
    Viewer(Viewer &&o);

    ~Viewer();

    CountT loadObjects(Span<const imp::SourceObject> objs);

    const render::RendererBridge * rendererBridge() const;

    void loop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}