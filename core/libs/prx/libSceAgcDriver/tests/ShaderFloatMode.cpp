#include "CacheKey.hpp"
#include "ControlFlow/RequestSerializer.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ShaderRecompiler {
bool DebugProbeActive() { return false; }
bool RayTracingStrict() { return false; }
bool RayTracingMiss() { return false; }
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        using namespace ShaderRecompiler;
        for (const auto& [stage, overflowBit] : std::array{
                std::pair{ShaderFloatModeRegister::Compute, 26u},
                std::pair{ShaderFloatModeRegister::Pixel, 29u},
                std::pair{ShaderFloatModeRegister::Geometry, 31u},
                std::pair{ShaderFloatModeRegister::Hull, 30u}}) {
            require(ShaderFloatMode::Decode(1u << overflowBit, stage) == ShaderFloatMode{0, false, false, true}, "FP16_OVFL used the wrong stage's bit");
            require(ShaderFloatMode::Decode(0x002c0000u, stage) == ShaderFloatMode{0xc0, true, false, false}, "the common float mode was decoded incorrectly");
            const auto mask = 0xff000u | (1u << 21u) | (1u << 23u) | (1u << overflowBit);
            require(ShaderFloatMode::Decode(~mask, stage) == ShaderFloatMode{}, "unrelated RSRC1 bits changed the float mode");
            require(ShaderFloatMode::Decode(mask, stage) == ShaderFloatMode{255, true, true, true}, "RSRC1 control fields were lost");
        }
        bool rejected = false;
        try {
            static_cast<void>(ShaderFloatMode::Decode(0, static_cast<ShaderFloatModeRegister>(255)));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "an unsupported shader register kind was accepted");

        RequestSerializer serializer;
        const auto legacy = serializer.Deserialize("NVNQQQwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAQEAAAABAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAHAAAAAQ==");
        require(!legacy.request.context.floatMode, "a version-12 capture acquired a float mode");
        require(legacy.request.context.compute->scratchDwords == 7 && legacy.request.target.narrowSubgroupClock, "version-12 capture fields were lost");
        const auto current = serializer.Deserialize("NVNQQQ0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAQEAAAABAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABwAAAAAEBAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAcAAAAB");
        require(current.request.context.floatMode == ShaderFloatMode{0xc0, true, true, false}, "a version-13 capture lost its recorded float mode");
        require(current.request.context.compute->scratchDwords == 7 && current.request.target.narrowSubgroupClock, "version-13 capture fields were lost");

        RecompileRequest request{};
        request.context.waveSize = 64;
        std::vector<std::uint64_t> unknownKey;
        RecompileCacheKey::BuildInterface(request, unknownKey);
        require(!serializer.Deserialize(serializer.Serialize(request)).request.context.floatMode, "an unknown mode became known");
        request.context.floatMode = ShaderFloatMode{};
        std::vector<std::uint64_t> zeroKey;
        RecompileCacheKey::BuildInterface(request, zeroKey);
        require(zeroKey != unknownKey, "an unknown mode aliases explicit zero in the prepared interface");
        const auto zeroHash = RecompileCacheKey::ContextHash(request);
        for (const auto stage : {ShaderStage::Compute, ShaderStage::Vertex, ShaderStage::Fragment, ShaderStage::Local, ShaderStage::TessellationControl, ShaderStage::TessellationEvaluation, ShaderStage::Geometry, ShaderStage::Mesh}) {
            request.shader.stage = stage;
            request.context.floatMode = ShaderFloatMode{};
            std::vector<std::uint64_t> baseKey;
            RecompileCacheKey::Build(request, baseKey);
            std::vector<std::uint64_t> baseInterface;
            RecompileCacheKey::BuildInterface(request, baseInterface);
            const auto baseHash = RecompileCacheKey::ContextHash(request);
            for (const auto bit : {12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 21u, 23u, 26u}) {
                request.context.floatMode = ShaderFloatMode::Decode(1u << bit, ShaderFloatModeRegister::Compute);
                std::vector<std::uint64_t> key, preparedKey;
                RecompileCacheKey::Build(request, key);
                RecompileCacheKey::BuildInterface(request, preparedKey);
                require(key != baseKey && preparedKey != baseInterface && RecompileCacheKey::ContextHash(request) != baseHash, "a float control bit was ignored by a cache identity");
                const auto replay = serializer.Deserialize(serializer.Serialize(request));
                std::vector<std::uint64_t> replayKey, replayInterface;
                RecompileCacheKey::Build(replay.request, replayKey);
                RecompileCacheKey::BuildInterface(replay.request, replayInterface);
                require(replay.request.context.floatMode == request.context.floatMode && replayKey == key && replayInterface == preparedKey && RecompileCacheKey::ContextHash(replay.request) == RecompileCacheKey::ContextHash(request), "a capture changed the mode or prepared identity");
            }
        }
        request.shader.stage = ShaderStage::Compute;
        request.context.floatMode.reset();
        require(RecompileCacheKey::ContextHash(request) != zeroHash, "the source memo aliases an unknown mode and explicit zero");
        for (const auto invalid : {"NVNQQQ0AAAA=", "NVNQQQ4AAAA="}) {
            rejected = false;
            try {
                static_cast<void>(serializer.Deserialize(invalid));
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected, "a truncated or unsupported capture was accepted");
        }
        std::cout << "shader float mode metadata passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
