#ifndef CORE_SHADER_RECOMPILER_SHADERFLOATMODE_HPP
#define CORE_SHADER_RECOMPILER_SHADERFLOATMODE_HPP

#include <cstdint>
#include <stdexcept>

namespace ShaderRecompiler {

enum class ShaderFloatModeRegister : std::uint8_t { Compute, Pixel, Geometry, Hull };

struct ShaderFloatMode {
    std::uint32_t floatMode = 0;
    bool dx10Clamp = false;
    bool ieeeMode = false;
    bool fp16Overflow = false;

    static constexpr ShaderFloatMode Decode(std::uint32_t rsrc1, ShaderFloatModeRegister stage) {
        std::uint32_t overflowBit;
        switch (stage) {
        case ShaderFloatModeRegister::Compute: overflowBit = 26; break;
        case ShaderFloatModeRegister::Pixel: overflowBit = 29; break;
        case ShaderFloatModeRegister::Geometry: overflowBit = 31; break;
        case ShaderFloatModeRegister::Hull: overflowBit = 30; break;
        default: throw std::runtime_error("unsupported shader float-mode register");
        }
        return {(rsrc1 >> 12u) & 0xffu, (rsrc1 & (1u << 21u)) != 0,
                (rsrc1 & (1u << 23u)) != 0, (rsrc1 & (1u << overflowBit)) != 0};
    }

    bool operator==(const ShaderFloatMode&) const = default;
};

}

#endif
