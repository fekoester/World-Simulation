#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <map>
#include <random>
#include <tuple>

#include "wsim/render.hpp"
#include "wsim/world.hpp"

using namespace wsim;

static Params small(uint64_t seed, int organisms = 40) {
    Params p;
    p.seed = seed;
    p.width = p.height = 60;
    p.startingOrganisms = p.minOrganisms = organisms;
    return p;
}

TEST_CASE("Constants.ini is parsed with the right keys") {
    std::vector<std::string> warnings;
    Params p = loadParams(WSIM_SOURCE_DIR "/Constants.ini", &warnings);
    CHECK(p.frequency == doctest::Approx(1.0));
    CHECK(p.width == 125);
    CHECK(p.heatTimeFactor == doctest::Approx(0.07));                // was read from Energy_heat_production
    CHECK(p.energyHeatProduction == doctest::Approx(0.058));
    CHECK(p.temperatureFluctuationFactor == doctest::Approx(10.0));  // was written into the air-flux factor
    CHECK(p.temperatureAirFluxFactor == doctest::Approx(0.08));
    CHECK(p.highestHeatLoss == doctest::Approx(0.5));                // "Higehst_heat_loss_factor"
    CHECK(p.newlifeEnergy == doctest::Approx(500));
    CHECK(p.inputs() == 3 * 9 + 7 * 4 + 7);
    CHECK(warnings.empty());
}

TEST_CASE("brain: per-layer weights with biases, forward pass by hand") {
    Params p;
    p.viewRadius = 1, p.nextEntities = 0, p.numLayers = 3;  // 3 + 7 = 10 inputs, 5 hidden, 5 outputs
    std::mt19937_64 rng(1);
    Brain b(p, rng);
    REQUIRE(b.sizes() == std::vector<int>{10, 5, 5});
    CHECK(b.weights().size() == 11u * 5 + 6u * 5);
    std::vector<double> x(10);
    for (int i = 0; i < 10; i++) x[i] = 0.1 * i - 0.3;
    auto act = [&](double s) { return p.sigmoidStretchY * (1.0 / (1.0 + std::exp(p.sigmoidStretchX * s)) - 0.5); };
    std::vector<double> h(5), o(5);
    for (int j = 0; j < 5; j++) {
        double s = b.weight(0, 10, j);
        for (int k = 0; k < 10; k++) s += x[k] * b.weight(0, k, j);
        h[j] = act(s);
    }
    for (int j = 0; j < 5; j++) {
        double s = b.weight(1, 5, j);
        for (int k = 0; k < 5; k++) s += h[k] * b.weight(1, k, j);
        o[j] = act(s);
    }
    const auto out = b.forward(x, p);
    for (int j = 0; j < 5; j++) CHECK(out[j] == doctest::Approx(o[j]));
    for (double v : out) CHECK(std::fabs(v) < 0.5);
    // the second layer has its own weights: changing them leaves the hidden layer unchanged
    const auto hidden = b.activations()[1];
    b.weights().back() += 3.0;
    b.forward(x, p);
    CHECK(b.activations()[1] == hidden);
}

TEST_CASE("brain mutation") {
    Params p;
    std::mt19937_64 r1(5), r2(5);
    Brain a(p, r1), b(p, r2);
    CHECK(a.weights() == b.weights());
    p.mutationChance = 0.0;
    Brain c = a;
    c.mutate(p, r1);
    CHECK(c.weights() == a.weights());
    p.mutationChance = 0.5;
    c.mutate(p, r1);
    size_t changed = 0;
    for (size_t i = 0; i < a.weights().size(); i++) changed += c.weights()[i] != a.weights()[i];
    CHECK(changed > a.weights().size() / 2);
}

TEST_CASE("landscape: terrain, temperature, food, island and minimum land") {
    for (uint64_t seed : {1, 8, 21}) {
        Simulation s(small(seed, 0));
        const auto &L = s.land;
        CHECK(L.landFraction() >= s.p.minLandFraction);
        for (int x = 0; x < L.W; x++) CHECK(L.at(x, 0).terrain == Terrain::Water);  // island
        for (const auto &t : L.tiles) {
            CHECK(t.food >= 0.0);
            if (t.terrain == Terrain::Water) CHECK(std::fabs(t.temperature - s.p.waterTemperature) <= 1.5 + 1e-9);
            CHECK(t.temperature == t.originalTemperature);
        }
    }
}

TEST_CASE("nearest neighbours match brute force") {
    Params p = small(3, 300);
    Simulation s(p);
    s.snapshot();
    const auto &O = s.organisms;
    for (size_t i = 0; i < O.size(); i += 7) {
        for (double radius : {1.0, 5.0, 20.0, 500.0}) {
            std::vector<std::pair<double, size_t>> all;
            for (size_t j = 0; j < O.size(); j++) {
                if (j == i) continue;
                const double d2 = (O[j].x - O[i].x) * (O[j].x - O[i].x) + (O[j].y - O[i].y) * (O[j].y - O[i].y);
                if (d2 <= radius * radius) all.push_back({d2, j});
            }
            std::sort(all.begin(), all.end());
            if (all.size() > 4) all.resize(4);
            std::vector<size_t> expect;
            for (auto &e : all) expect.push_back(e.second);
            CHECK(s.nearest(i, 4, radius) == expect);
        }
    }
}

TEST_CASE("sensing is relative to the heading") {
    Params p = small(4, 2);
    p.minOrganisms = 0;
    Simulation s(p);
    auto &a = s.organisms[0], &b = s.organisms[1];
    a.x = 30.2, a.y = 30.2, a.rotation = 0.0;
    b.x = 32.2, b.y = 30.2;
    b.red = 255, b.green = 0, b.blue = 51;
    s.snapshot();
    const int base = 3 * p.viewRadius * p.viewRadius;
    auto in = s.sense(0);
    REQUIRE((int)in.size() == p.inputs());
    CHECK(in[base + 0] == doctest::Approx(2.0 / p.nextEntitiesRadius));  // ahead
    CHECK(in[base + 1] == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(in[base + 4] == doctest::Approx(0.2));  // blue
    CHECK(in[base + 6] == doctest::Approx(1.0));  // red
    CHECK(in[base + 7] == 0.0);                   // no second neighbour
    a.rotation = kPi / 2;                         // facing +y: b is now on the right (negative lateral)
    in = s.sense(0);
    CHECK(in[base + 0] == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(in[base + 1] == doctest::Approx(-2.0 / p.nextEntitiesRadius));
    // own body block at the end: temperature, energy, size, alpha, red, green, blue
    const auto &o = s.organisms[0];
    CHECK(in[in.size() - 7] == doctest::Approx(o.temperature / p.optimTemperature));
    CHECK(in[in.size() - 2] == doctest::Approx(o.green / 255.0));
    // the centre view tile is its own tile and does not count itself
    const int centre = ((p.viewRadius - 1) / 2) * 3 + 2;  // row 0 (here), middle column
    CHECK(in[centre] == 0.0);
}

TEST_CASE("efficiency is clamped to [0, 1]") {
    Simulation s(small(5, 1));
    Organism o = s.organisms[0];
    o.temperature = s.p.optimTemperature;
    CHECK(s.efficiency(o) == 1.0);
    o.temperature = -100;
    CHECK(s.efficiency(o) == 0.0);
}

TEST_CASE("the same seed gives the same run") {
    Params p = small(11, 60);
    Simulation a(p), b(p);
    for (int i = 0; i < 400; i++) a.step(), b.step();
    REQUIRE(a.organisms.size() == b.organisms.size());
    for (size_t i = 0; i < a.organisms.size(); i++) {
        CHECK(a.organisms[i].id == b.organisms[i].id);
        CHECK(a.organisms[i].x == b.organisms[i].x);
        CHECK(a.organisms[i].energy == b.organisms[i].energy);
    }
    CHECK(a.counters.births == b.counters.births);
    Params q = p;
    q.seed = 12;
    Simulation c(q);
    for (int i = 0; i < 400; i++) c.step();
    CHECK(c.organisms[0].x != a.organisms[0].x);
}

TEST_CASE("invariants over a run with lots of breeding and mutation") {
    Params p = small(13, 80);
    p.newlifeEnergy = 260;  // breed often
    p.mutateStat = 1.0;     // mutate every trait of every child
    p.foodGrowthFactor = 0.001;
    Simulation s(p);
    std::map<uint64_t, std::tuple<double, double, double, int, uint64_t>> traits;
    long initial = (long)s.organisms.size();
    for (const auto &o : s.organisms) CHECK(s.land.under(o.x, o.y).terrain != Terrain::Water);  // spawn on land
    for (int step = 0; step < 1500; step++) {
        s.step();
        for (const auto &o : s.organisms) {
            CHECK(o.alive());
            CHECK(o.x >= 0.0);
            CHECK(o.x <= p.width - 1.0);
            CHECK(o.y >= 0.0);
            CHECK(o.y <= p.height - 1.0);
            CHECK(o.energy <= o.maxEnergy(p) + 1e-9);
            CHECK(o.heatProduction >= p.lowestEnergyHeatProduction);
            CHECK(o.heatProduction <= p.highestEnergyHeatProduction);
            CHECK(o.heatLoss >= p.lowestHeatLoss);
            CHECK(o.heatLoss <= p.highestHeatLoss);
            CHECK(o.size >= 0.1);
            CHECK(o.alpha >= 128);
            auto key = std::make_tuple(o.heatProduction, o.heatLoss, o.size, o.red, o.parent);
            auto [it, fresh] = traits.emplace(o.id, key);
            if (!fresh) CHECK(it->second == key);  // a body never changes during its life (breeding used to mutate the parent)
        }
        CHECK((int)s.organisms.size() >= p.minOrganisms);
        if (step % 100 == 0)
            for (int y = 1; y < s.land.H - 1; y++)
                for (int x = 1; x < s.land.W - 1; x++) CHECK(s.land.at(x, y).food <= p.maxFoodOnTile + 1e-9);
    }
    const auto &c = s.counters;
    CHECK(c.births > 50);
    CHECK(initial + c.births + c.respawns - c.totalDeaths() == (long)s.organisms.size());
}

TEST_CASE("rendering") {
    Simulation s(small(2, 10));
    for (auto ov : {Overlay::Terrain, Overlay::Food, Overlay::Temperature}) {
        Image img = render(s, 3, ov);
        CHECK(img.w == 180);
        CHECK(img.rgb.size() == 180u * 180u * 3u);
    }
}

TEST_CASE("invalid parameters are rejected") {
    Params p = small(1);
    p.width = 4;
    CHECK_THROWS_AS(Simulation{p}, std::invalid_argument);
    p = small(1);
    p.numLayers = 1;
    CHECK_THROWS_AS(Simulation{p}, std::invalid_argument);
}
