// Software rendering of the world into an RGB buffer (used for PPM/PNG frame dumps and as the viewer's map).
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "world.hpp"

namespace wsim {

enum class Overlay { Terrain, Food, Temperature };

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
    void put(int x, int y, std::array<uint8_t, 3> c) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t *p = &rgb[((size_t)y * w + x) * 3];
        p[0] = c[0], p[1] = c[1], p[2] = c[2];
    }
};

inline std::array<uint8_t, 3> terrainColor(Terrain t) {
    switch (t) {
        case Terrain::Water: return {40, 90, 200};
        case Terrain::Grass: return {70, 160, 60};
        case Terrain::Stone: return {130, 125, 115};
        case Terrain::Snow: return {235, 240, 245};
    }
    return {255, 0, 255};
}

// blue (cold) -> white -> red (warm), centred on the optimum body temperature
inline std::array<uint8_t, 3> temperatureColor(double t, double centre, double span) {
    const double v = std::max(-1.0, std::min(1.0, (t - centre) / span));
    if (v < 0) return {(uint8_t)(255 * (1 + v)), (uint8_t)(255 * (1 + v)), 255};
    return {255, (uint8_t)(255 * (1 - v)), (uint8_t)(255 * (1 - v))};
}

inline std::array<uint8_t, 3> tileColor(const Simulation &s, const Tile &t, Overlay o) {
    const auto &p = s.p;
    if (o == Overlay::Temperature) return temperatureColor(t.temperature, p.optimTemperature, 25.0);
    const double f = std::max(0.0, std::min(1.0, t.food / p.maxFoodOnTile));
    if (o == Overlay::Food) {
        const auto g = (uint8_t)(255 * f);
        return {(uint8_t)(g / 3), g, (uint8_t)(g / 4)};
    }
    // terrain, brighter with more food (as the original's 80 + 175 * food / max alpha over a grey background)
    const auto c = terrainColor(t.terrain);
    const double a = (80.0 + 175.0 * f) / 255.0;
    std::array<uint8_t, 3> out{};
    for (int k = 0; k < 3; k++) out[k] = (uint8_t)(a * c[k] + (1 - a) * 60);
    return out;
}

// scale pixels per tile; organisms as discs (radius size/2 tiles, at least 1.5 px) with a heading tick
inline Image render(const Simulation &s, int scale, Overlay overlay = Overlay::Terrain, bool organisms = true) {
    Image img;
    img.w = s.land.W * scale, img.h = s.land.H * scale;
    img.rgb.assign((size_t)img.w * img.h * 3, 0);
    for (int ty = 0; ty < s.land.H; ty++)
        for (int tx = 0; tx < s.land.W; tx++) {
            const auto c = tileColor(s, s.land.at(tx, ty), overlay);
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++) img.put(tx * scale + dx, ty * scale + dy, c);
        }
    if (!organisms) return img;
    for (const auto &o : s.organisms) {
        const double cx = (o.x + 0.5) * scale, cy = (o.y + 0.5) * scale;
        const double r = std::max(1.5, o.size / 2.0 * scale);
        const std::array<uint8_t, 3> col =
            o.wasHit ? std::array<uint8_t, 3>{255, 0, 0}
                     : std::array<uint8_t, 3>{(uint8_t)o.red, (uint8_t)o.green, (uint8_t)o.blue};
        for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
            for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
                const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
                if (d <= r) img.put(x, y, d > r - 1.0 ? std::array<uint8_t, 3>{20, 20, 20} : col);
            }
        for (double t = 0; t <= 1.6 * r; t += 0.5)
            img.put((int)(cx + t * std::cos(o.rotation)), (int)(cy + t * std::sin(o.rotation)), {0, 0, 0});
    }
    return img;
}

inline void writePpm(const std::string &path, const Image &img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "P6\n" << img.w << ' ' << img.h << "\n255\n";
    f.write((const char *)img.rgb.data(), (std::streamsize)img.rgb.size());
}

}  // namespace wsim
