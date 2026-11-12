// World-Simulation core: a Perlin-noise island with food and temperature, and organisms with evolving brains
// and bodies. No graphics dependency; every random choice comes from one generator seeded with Params::seed.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "../../third_party/PerlinNoise.hpp"
#include "brain.hpp"
#include "params.hpp"

namespace wsim {

constexpr double kPi = 3.14159265358979323846;

enum class Terrain : uint8_t { Water, Grass, Stone, Snow };
enum class Death : uint8_t { None, Energy, Temperature, Age, Attack };

struct Tile {
    double height = 0, temperature = 25, originalTemperature = 25, food = 0;
    Terrain terrain = Terrain::Grass;
};

class Landscape {
public:
    Landscape() = default;

    // Worlds with less than minLandFraction land (with a low-frequency map some seeds are all sea) are
    // regenerated with the next noise seed from the same generator, so this stays deterministic.
    template <class Rng>
    Landscape(const Params &p, Rng &rng) {
        for (int attempt = 0; attempt < 50; attempt++) {
            generate(p, rng);
            if (landFraction() >= p.minLandFraction) break;
        }
    }

    double landFraction() const {
        size_t land = 0;
        for (const auto &t : tiles) land += t.terrain != Terrain::Water;
        return tiles.empty() ? 0.0 : double(land) / tiles.size();
    }

    template <class Rng>
    void generate(const Params &p, Rng &rng) {
        W = p.width, H = p.height;
        tiles.assign((size_t)p.width * p.height, Tile{});
        std::uniform_int_distribution<uint32_t> seeds(0, 10000000);
        PerlinNoise perlin(seeds(rng));
        const double fx = W / p.frequency, fy = H / p.frequency;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) at(x, y).height = perlin.octaveNoise0_1(x / fx, y / fy, p.octaves) - 0.5;
        // Terrain thresholds are fractions of the maximum of the raw noise, taken before the island falloff
        // (this is what the original effectively used, and what Constants.ini is tuned for).
        heightMax = std::numeric_limits<double>::lowest();
        for (const auto &t : tiles) heightMax = std::max(heightMax, t.height);
        if (heightMax <= 0) heightMax = 1e-9;
        if (p.island) {  // subtract 1 - exp(m (dx^6 + dy^6)): ~0 in the middle, rising steeply towards the edges
            const double m = std::log(0.4) / std::pow(W / 2.0, 6);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    at(x, y).height -= 1.0 - std::exp(m * (std::pow(x - W / 2.0, 6) + std::pow(y - H / 2.0, 6)));
        }
        heightMin = std::numeric_limits<double>::max();
        for (const auto &t : tiles) heightMin = std::min(heightMin, t.height);

        for (auto &t : tiles) {
            if (t.height <= heightMax * p.waterLevel) t.terrain = Terrain::Water;
            else if (t.height < heightMax * p.grassLevel) t.terrain = Terrain::Grass;
            else if (t.height < heightMax * p.stoneLevel) t.terrain = Terrain::Stone;
            else t.terrain = Terrain::Snow;
        }

        // temperature: warmest at the "optimum" height, falling linearly to lowTemperature at the top
        perlin.reseed(seeds(rng));
        const double optimumHeight = std::fabs(heightMax * p.waterLevel + heightMax * p.grassLevel) / 2.0;
        const double slope = (p.lowTemperature - p.optTemperature) / std::max(1e-9, heightMax - optimumHeight);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                Tile &t = at(x, y);
                const double noise = perlin.octaveNoise0_1(x / fx, y / fy, p.octaves) - 0.5;
                if (t.terrain != Terrain::Water)
                    t.temperature = slope * std::fabs(t.height - optimumHeight) + p.optTemperature +
                                    noise * (p.optTemperature - p.lowTemperature) / p.temperatureFluctuationFactor;
                else
                    t.temperature = noise * 3.0 + p.waterTemperature;
                t.originalTemperature = t.temperature;
            }

        // initial food from temperature, height and noise (half as much, by magnitude, in water)
        perlin.reseed(seeds(rng));
        std::uniform_real_distribution<double> u(0.0, 1.0);
        for (int y = 1; y < H - 1; y++)
            for (int x = 1; x < W - 1; x++) {
                if (u(rng) >= p.chanceFoodGrowth) continue;
                Tile &t = at(x, y);
                const double noise = perlin.octaveNoise0_1(x / fx, y / fy, p.octaves) - 0.5;
                const double v =
                    t.temperature * p.foodTemperatureFactor + t.height * p.foodHeightFactor + noise * p.foodNoiseFactor;
                const double food = v * u(rng);  // uniform between 0 and v
                t.food = t.terrain != Terrain::Water ? std::max(0.0, food) : std::fabs(food) / 2.0;
            }
    }

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }
    Tile &at(int x, int y) { return tiles[(size_t)y * W + x]; }
    const Tile &at(int x, int y) const { return tiles[(size_t)y * W + x]; }
    // tile under a continuous position (clamped)
    Tile &under(double x, double y) {
        return at(std::clamp((int)x, 0, W - 1), std::clamp((int)y, 0, H - 1));
    }

    int W = 0, H = 0;
    std::vector<Tile> tiles;
    double heightMax = 0, heightMin = 0;
};

struct Organism {
    uint64_t id = 0, parent = 0;
    double x = 0, y = 0, rotation = 0;
    double forward = 0, turn = 0;  // this step's movement
    int red = 0, green = 0, blue = 0, alpha = 255;
    double size = 1, heatProduction = 0, heatLoss = 0;
    double energy = 0, age = 0, temperature = 37, fitness = 0;
    int generation = 0;
    bool wasHit = false;
    Death death = Death::None;
    Brain brain;

    bool alive() const { return death == Death::None; }
    double maxEnergy(const Params &p) const { return p.maxEnergyFactor * size * size; }
};

struct Counters {
    long births = 0, respawns = 0;
    std::array<long, 5> deaths{};  // indexed by Death
    long totalDeaths() const { return deaths[1] + deaths[2] + deaths[3] + deaths[4]; }
};

class Simulation {
public:
    explicit Simulation(Params params) : p(std::move(params)) {
        p.validate();
        if (p.seed == 0) p.seed = std::random_device{}() | 1ULL;
        rng.seed(p.seed);
        land = Landscape(p, rng);
        cellSize_ = 4.0;
        gw_ = std::max(1, (int)std::ceil(p.width / cellSize_));
        gh_ = std::max(1, (int)std::ceil(p.height / cellSize_));
        for (int i = 0; i < p.startingOrganisms; i++) organisms.push_back(randomOrganism());
    }

    // ---- one simulation step ------------------------------------------------------------------------------
    void step() {
        snapshot();
        for (auto &o : organisms) o.wasHit = false;
        std::vector<Organism> children;
        const size_t n = organisms.size();
        for (size_t i = 0; i < n; i++) {
            Organism &o = organisms[i];
            if (!o.alive()) continue;  // killed by an attack earlier in this step
            const auto &out = o.brain.forward(sense(i), p);
            move(o, out);
            if (heat(o) && energyUse(o)) {
                eat(o, out);
                attack(i, out);
                if (ageing(o)) breed(o, out, children);
                o.fitness += 0.1;
            }
        }
        for (const auto &o : organisms)
            if (!o.alive()) counters.deaths[(int)o.death]++;
        organisms.erase(std::remove_if(organisms.begin(), organisms.end(), [](const Organism &o) { return !o.alive(); }),
                        organisms.end());
        counters.births += (long)children.size();
        for (auto &c : children) organisms.push_back(std::move(c));
        while ((int)organisms.size() < p.minOrganisms) organisms.push_back(randomOrganism()), counters.respawns++;
        updateWorld();
        steps++;
    }

    // ---- sensing --------------------------------------------------------------------------------------------
    // Inputs, all relative to the organism's heading (forward axis f, left axis l):
    //   view grid: viewRadius x viewRadius tiles in front (row i ahead, column j across) -> temperature / optimum,
    //              food / max food, number of other organisms on the tile
    //   nearest nextEntities organisms within nextEntitiesRadius -> (forward, lateral offset) / radius,
    //              size / start size, alpha, blue, green, red / 255   (zeros if fewer are in range)
    //   own body -> temperature / optimum, energy / max energy, size / start size, alpha, red, green, blue / 255
    // Neighbour positions are those at the start of the step, so all organisms perceive the same snapshot.
    std::vector<double> sense(size_t i) const {
        const Organism &o = organisms[i];
        std::vector<double> in;
        in.reserve((size_t)p.inputs());
        const double c = std::cos(o.rotation), s = std::sin(o.rotation);
        const int R = p.viewRadius;
        const int ox = (int)snapX_[i], oy = (int)snapY_[i];
        for (int a = 0; a < R; a++) {
            for (int b = 0; b < R; b++) {
                const double lat = b - (R - 1) / 2.0;
                const double px = o.x + a * c - lat * s, py = o.y + a * s + lat * c;
                const int tx = (int)std::floor(px), ty = (int)std::floor(py);
                if (!land.inside(tx, ty)) {
                    in.insert(in.end(), {0.0, 0.0, 0.0});
                    continue;
                }
                const Tile &t = land.at(tx, ty);
                int count = occupancy_[(size_t)ty * p.width + tx];
                if (tx == ox && ty == oy) count--;  // not counting itself
                in.push_back(t.temperature / p.optimTemperature);
                in.push_back(t.food / p.maxFoodOnTile);
                in.push_back((double)count);
            }
        }
        for (size_t j : nearest(i, p.nextEntities, p.nextEntitiesRadius)) {
            const Organism &q = organisms[j];
            const double dx = snapX_[j] - snapX_[i], dy = snapY_[j] - snapY_[i];
            in.push_back((dx * c + dy * s) / p.nextEntitiesRadius);
            in.push_back((-dx * s + dy * c) / p.nextEntitiesRadius);
            in.push_back(q.size / p.sizeOrganism);
            in.push_back(q.alpha / 255.0);
            in.push_back(q.blue / 255.0);
            in.push_back(q.green / 255.0);
            in.push_back(q.red / 255.0);
        }
        in.resize((size_t)(3 * R * R + 7 * p.nextEntities), 0.0);  // missing neighbours are zeros
        in.push_back(o.temperature / p.optimTemperature);
        in.push_back(o.energy / o.maxEnergy(p));
        in.push_back(o.size / p.sizeOrganism);
        in.push_back(o.alpha / 255.0);
        in.push_back(o.red / 255.0);
        in.push_back(o.green / 255.0);
        in.push_back(o.blue / 255.0);
        return in;
    }

    // Indices of the k organisms nearest to organism i (start-of-step positions) within radius, nearest first.
    std::vector<size_t> nearest(size_t i, int k, double radius) const {
        std::vector<std::pair<double, size_t>> best;  // (distance^2, index), sorted, at most k
        if (k <= 0) return {};
        const double r2 = radius * radius;
        const int cx = cellX(snapX_[i]), cy = cellY(snapY_[i]);
        const int maxRing = std::max(gw_, gh_);
        for (int ring = 0; ring <= maxRing; ring++) {
            for (int gy = cy - ring; gy <= cy + ring; gy++) {
                for (int gx = cx - ring; gx <= cx + ring; gx++) {
                    if (std::max(std::abs(gx - cx), std::abs(gy - cy)) != ring) continue;
                    if (gx < 0 || gy < 0 || gx >= gw_ || gy >= gh_) continue;
                    for (size_t j : cells_[(size_t)gy * gw_ + gx]) {
                        if (j == i) continue;
                        const double dx = snapX_[j] - snapX_[i], dy = snapY_[j] - snapY_[i], d2 = dx * dx + dy * dy;
                        if (d2 > r2) continue;
                        std::pair<double, size_t> e{d2, j};
                        if ((int)best.size() == k && !(e < best.back())) continue;
                        best.insert(std::upper_bound(best.begin(), best.end(), e), e);
                        if ((int)best.size() > k) best.pop_back();
                    }
                }
            }
            // everything outside rings 0..ring is at least ring * cellSize away
            const double reach = ring * cellSize_;
            if (reach * reach > r2) break;
            if ((int)best.size() == k && best.back().first <= reach * reach) break;
        }
        std::vector<size_t> idx;
        for (auto &e : best) idx.push_back(e.second);
        return idx;
    }

    // ---- statistics -----------------------------------------------------------------------------------------
    struct Stats {
        long step = 0;
        size_t population = 0;
        double meanSize = 0, meanHeatProduction = 0, meanHeatLoss = 0, meanTemperature = 0, meanEnergy = 0;
        double meanGeneration = 0, meanAge = 0, totalFood = 0;
        int maxGeneration = 0;
    };
    Stats stats() const {
        Stats st;
        st.step = steps;
        st.population = organisms.size();
        for (const auto &o : organisms) {
            st.meanSize += o.size, st.meanHeatProduction += o.heatProduction, st.meanHeatLoss += o.heatLoss;
            st.meanTemperature += o.temperature, st.meanEnergy += o.energy, st.meanGeneration += o.generation;
            st.meanAge += o.age, st.maxGeneration = std::max(st.maxGeneration, o.generation);
        }
        if (!organisms.empty()) {
            const double n = (double)organisms.size();
            st.meanSize /= n, st.meanHeatProduction /= n, st.meanHeatLoss /= n, st.meanTemperature /= n;
            st.meanEnergy /= n, st.meanGeneration /= n, st.meanAge /= n;
        }
        for (const auto &t : land.tiles) st.totalFood += t.food;
        return st;
    }

    // body efficiency: 1 at the optimum body temperature, falling linearly, never below 0
    double efficiency(const Organism &o) const {
        return std::clamp(1.0 - std::fabs(o.temperature - p.optimTemperature) / p.optimTemperature, 0.0, 1.0);
    }

    Params p;
    std::mt19937_64 rng;
    Landscape land;
    std::vector<Organism> organisms;
    Counters counters;
    long steps = 0;

private:
    double uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }
    int uniformInt(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }
    // with probability mutateStat: a change of up to +-10 % of the value
    double mutation(double v) { return uniform(0.0, 1.0) < p.mutateStat ? v * uniform(-0.1, 0.1) : 0.0; }

    Organism randomOrganism() {
        Organism o;
        o.id = ++lastId_;
        o.x = uniform(0.0, p.width - 1.0);
        o.y = uniform(0.0, p.height - 1.0);
        for (int attempt = 0; p.spawnOnLand && attempt < 1000 && land.under(o.x, o.y).terrain == Terrain::Water; attempt++)
            o.x = uniform(0.0, p.width - 1.0), o.y = uniform(0.0, p.height - 1.0);
        o.rotation = uniform(0.0, 2 * kPi);
        o.red = uniformInt(0, 255), o.green = uniformInt(0, 255), o.blue = uniformInt(0, 255);
        o.alpha = uniformInt(128, 255);
        o.size = std::max(0.1, p.sizeOrganism + mutation(p.sizeOrganism));
        o.heatProduction = std::clamp(p.energyHeatProduction + mutation(p.energyHeatProduction),
                                      p.lowestEnergyHeatProduction, p.highestEnergyHeatProduction);
        o.heatLoss = std::clamp(p.heatLossFactor + mutation(p.heatLossFactor), p.lowestHeatLoss, p.highestHeatLoss);
        o.energy = p.startingEnergy;
        o.temperature = p.startingTemperature;
        o.brain = Brain(p, rng);
        return o;
    }

public:
    // Record start-of-step positions, the neighbour grid and tile occupancy (called by step(); public for tests).
    void snapshot() {
        const size_t n = organisms.size();
        snapX_.resize(n), snapY_.resize(n);
        cells_.assign((size_t)gw_ * gh_, {});
        occupancy_.assign((size_t)p.width * p.height, 0);
        for (size_t i = 0; i < n; i++) {
            snapX_[i] = organisms[i].x, snapY_[i] = organisms[i].y;
            cells_[(size_t)cellY(snapY_[i]) * gw_ + cellX(snapX_[i])].push_back(i);
            const int tx = std::clamp((int)snapX_[i], 0, p.width - 1), ty = std::clamp((int)snapY_[i], 0, p.height - 1);
            occupancy_[(size_t)ty * p.width + tx]++;
        }
    }
private:
    int cellX(double x) const { return std::clamp((int)(x / cellSize_), 0, gw_ - 1); }
    int cellY(double y) const { return std::clamp((int)(y / cellSize_), 0, gh_ - 1); }

    // output 0: forward speed, output 1: turning; both scaled by efficiency / size, slowed in water
    void move(Organism &o, const std::vector<double> &out) {
        const double eff = efficiency(o);
        const double water = land.under(o.x, o.y).terrain == Terrain::Water ? p.movementReductionWater : 1.0;
        o.turn = eff / o.size * p.movementSpeed * out[1];
        o.rotation = std::fmod(o.rotation + o.turn * water + 2 * kPi, 2 * kPi);
        o.forward = eff / o.size * p.movementSpeed * out[0];
        o.x = std::clamp(o.x + o.forward * std::cos(o.rotation) * water, 0.0, p.width - 1.0);
        o.y = std::clamp(o.y + o.forward * std::sin(o.rotation) * water, 0.0, p.height - 1.0);
    }

    // dT = h (P - k (T - T_tile)); part of the lost heat warms the tile. Returns false if the organism died.
    bool heat(Organism &o) {
        Tile &t = land.under(o.x, o.y);
        const double lost = p.heatTimeFactor * o.heatLoss * (o.temperature - t.temperature);
        o.temperature += p.heatTimeFactor * o.heatProduction - lost;
        t.temperature += lost * p.temperatureAirFluxFactor;
        if (o.temperature <= p.coldDeathTemperature || o.temperature >= p.heatDeathTemperature) {
            o.death = Death::Temperature;
            return false;
        }
        return true;
    }

    // upkeep: size^2, heat production, and movement (more expensive in water)
    bool energyUse(Organism &o) {
        const double water = land.under(o.x, o.y).terrain == Terrain::Water ? p.movementEnergyFactorWater : 1.0;
        o.energy -= o.size * o.size * p.sizeEnergyLossFactor + o.heatProduction +
                    p.movementEnergyLoss * water * o.forward * o.forward;
        if (o.energy <= 0.0) {
            o.death = Death::Energy;
            return false;
        }
        return true;
    }

    // Eats eff * consumption * (1 - |out0|): the faster it moves, the less it eats. Skips if it would overflow.
    void eat(Organism &o, const std::vector<double> &out) {
        const double food = efficiency(o) * p.consumptionFactor * (1.0 - std::fabs(out[0]));
        if (food <= 0 || o.energy + p.foodEnergyFactor * food > o.maxEnergy(p)) return;
        Tile &t = land.under(o.x, o.y);
        const double eaten = std::min(food, t.food);
        t.food -= eaten;
        o.energy = std::min(o.maxEnergy(p), o.energy + p.foodEnergyFactor * eaten);
    }

    // output 4 > 0: steal size * attackDamage energy from the nearest organism if it is within size / 2
    void attack(size_t i, const std::vector<double> &out) {
        Organism &o = organisms[i];
        if (out[4] <= 0.0 || o.size * p.attackDamage + o.energy > o.maxEnergy(p)) return;
        size_t target = i;
        for (size_t j : nearest(i, 4, o.size / 2.0))  // nearest first
            if (organisms[j].alive()) {
                target = j;
                break;
            }
        if (target == i) return;
        Organism &v = organisms[target];
        const double stolen = std::min(o.size * p.attackDamage, v.energy);
        v.energy -= stolen, v.wasHit = true;
        o.energy += stolen;
        if (v.energy <= 0.0) v.death = Death::Attack;
    }

    // past avgAgeDeath the chance to die per step is 1 % per unit of age beyond it
    bool ageing(Organism &o) {
        o.age += p.agePerStep;
        if (o.age > p.avgAgeDeath && uniform(0.0, 1.0) < 0.01 * (o.age - p.avgAgeDeath)) {
            o.death = Death::Age;
            return false;
        }
        return true;
    }

    // output 2 > 0 and enough energy: pay newlifeEnergy for one asexual offspring with mutated brain and body
    void breed(Organism &o, const std::vector<double> &out, std::vector<Organism> &children) {
        if (out[2] <= 0.0 || o.energy <= p.newlifeEnergy) return;
        o.energy -= p.newlifeEnergy;
        Organism c = randomOrganism();
        c.parent = o.id;
        c.brain = o.brain;
        c.brain.mutate(p, rng);
        c.x = o.x, c.y = o.y;
        c.generation = o.generation + 1;
        auto colour = [&](int v, int lo) { return std::clamp((int)std::lround(v + mutation(v)) + uniformInt(-4, 4), lo, 255); };
        c.red = colour(o.red, 0), c.green = colour(o.green, 0), c.blue = colour(o.blue, 0), c.alpha = colour(o.alpha, 128);
        c.size = std::max(0.1, o.size + mutation(o.size));
        c.heatProduction = std::clamp(o.heatProduction + mutation(o.heatProduction), p.lowestEnergyHeatProduction,
                                      p.highestEnergyHeatProduction);
        c.heatLoss = std::clamp(o.heatLoss + mutation(o.heatLoss), p.lowestHeatLoss, p.highestHeatLoss);
        children.push_back(std::move(c));
    }

    // tiles relax towards their natural temperature; food regrows on inner tiles: f += U(0,1) (max - f) g, capped at max
    void updateWorld() {
        for (auto &t : land.tiles) t.temperature += (t.originalTemperature - t.temperature) * p.tileTemperatureRelaxation;
        for (int y = 1; y < land.H - 1; y++)
            for (int x = 1; x < land.W - 1; x++) {
                if (p.chanceFoodGrowth < 1.0 && uniform(0.0, 1.0) >= p.chanceFoodGrowth) continue;
                Tile &t = land.at(x, y);
                t.food = std::min(p.maxFoodOnTile, t.food + uniform(0.0, 1.0) * (p.maxFoodOnTile - t.food) * p.foodGrowthFactor);
            }
    }

    double cellSize_ = 4.0;
    int gw_ = 1, gh_ = 1;
    std::vector<std::vector<size_t>> cells_;
    std::vector<double> snapX_, snapY_;
    std::vector<int> occupancy_;
    uint64_t lastId_ = 0;
};

}  // namespace wsim
