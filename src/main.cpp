// World-Simulation: run headless (CSV statistics, frame dumps) or watch it in an SFML window.
//
//   wsim [--config Constants.ini] [--seed N] [--steps N] [--csv stats.csv] [--every 100]
//        [--dump-dir frames --dump-every 50 --scale 4] [--overlay terrain|food|temperature] [--no-window]
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "wsim/render.hpp"
#include "wsim/world.hpp"

#ifndef WSIM_NO_SFML
#include <SFML/Graphics.hpp>
#endif

using namespace wsim;

namespace {

struct Options {
    std::string config = "Constants.ini", csv, dumpDir, font = "assets/Mermaid1001.ttf";
    long seed = -1, steps = -1, every = 100, dumpEvery = 50;
    int scale = 4;
    Overlay overlay = Overlay::Terrain;
    bool window = true;
};

void usage() {
    std::cout << "usage: wsim [--config Constants.ini] [--seed N] [--steps N] [--csv stats.csv] [--every 100]\n"
                 "            [--dump-dir DIR] [--dump-every 50] [--scale 4] [--overlay terrain|food|temperature]\n"
                 "            [--no-window] [--font assets/Mermaid1001.ttf]\n"
                 "viewer: space pause, up/down speed, 1/2/3 overlay (terrain/food/temperature),\n"
                 "        left click select organism, N new world, Esc quit\n";
}

Overlay parseOverlay(const std::string &s) {
    if (s == "terrain") return Overlay::Terrain;
    if (s == "food") return Overlay::Food;
    if (s == "temperature") return Overlay::Temperature;
    throw std::invalid_argument("unknown overlay " + s);
}

const char *csvHeader =
    "step,population,births,deaths_energy,deaths_temperature,deaths_age,deaths_attack,respawns,mean_size,"
    "mean_heat_production,mean_heat_loss,mean_temperature,mean_energy,mean_generation,max_generation,total_food\n";

void csvRow(std::ostream &f, const Simulation &s) {
    const auto st = s.stats();
    const auto &c = s.counters;
    f << st.step << ',' << st.population << ',' << c.births << ',' << c.deaths[1] << ',' << c.deaths[2] << ','
      << c.deaths[3] << ',' << c.deaths[4] << ',' << c.respawns << ',' << st.meanSize << ',' << st.meanHeatProduction
      << ',' << st.meanHeatLoss << ',' << st.meanTemperature << ',' << st.meanEnergy << ',' << st.meanGeneration << ','
      << st.maxGeneration << ',' << st.totalFood << '\n';
}

std::string summary(const Simulation &s) {
    const auto st = s.stats();
    const auto &c = s.counters;
    std::ostringstream o;
    o.precision(4);
    o << "step " << st.step << "  seed " << s.p.seed << "\npopulation " << st.population << "\nbirths " << c.births
      << "  respawns " << c.respawns << "\ndeaths: energy " << c.deaths[1] << ", temperature " << c.deaths[2]
      << ",\n        age " << c.deaths[3] << ", attacked " << c.deaths[4] << "\nmean size " << st.meanSize
      << "\nmean heat production " << st.meanHeatProduction << "\nmean heat loss " << st.meanHeatLoss
      << "\nmean body temperature " << st.meanTemperature << "\nmean generation " << st.meanGeneration << " (max "
      << st.maxGeneration << ")\n";
    return o.str();
}

[[maybe_unused]] std::string describe(const Simulation &s, const Organism &o) {
    std::ostringstream t;
    t.precision(4);
    const Tile &tile = s.land.at(std::min(s.land.W - 1, (int)o.x), std::min(s.land.H - 1, (int)o.y));
    t << "organism " << o.id << " (parent " << o.parent << ")\ngeneration " << o.generation << "  age " << o.age
      << "\nenergy " << o.energy << " / " << o.maxEnergy(s.p) << "\nbody temperature " << o.temperature
      << "  efficiency " << s.efficiency(o) << "\nsize " << o.size << "\nheat production " << o.heatProduction
      << "  heat loss " << o.heatLoss << "\ntile: temperature " << tile.temperature << ", food " << tile.food << '\n';
    const auto &out = o.brain.outputs();
    if (out.size() >= 5)
        t << "outputs: forward " << out[0] << "\n  turn " << out[1] << "  breed " << out[2] << "  attack " << out[4]
          << '\n';
    return t.str();
}

#ifndef WSIM_NO_SFML
#if SFML_VERSION_MAJOR >= 3
#define KEY(k) sf::Keyboard::Key::k
#else
#define KEY(k) sf::Keyboard::k
#endif

void view(const Options &opt, const Params &params) {
    auto sim = std::make_unique<Simulation>(params);
    const unsigned W = (unsigned)sim->land.W, H = (unsigned)sim->land.H;
    const float scale = std::max(1.0f, std::min(900.0f / W, 900.0f / H));
    const unsigned panel = 360, winW = (unsigned)(W * scale) + panel, winH = std::max(560u, (unsigned)(H * scale));
#if SFML_VERSION_MAJOR >= 3
    sf::RenderWindow window(sf::VideoMode({winW, winH}), "World-Simulation");
    sf::Texture tex;
    (void)tex.resize({W, H});
    sf::Font font;
    const bool haveFont = font.openFromFile(opt.font);
#else
    sf::RenderWindow window(sf::VideoMode(winW, winH), "World-Simulation");
    sf::Texture tex;
    tex.create(W, H);
    sf::Font font;
    const bool haveFont = font.loadFromFile(opt.font);
#endif
    if (!haveFont) std::cerr << "font " << opt.font << " not found: the side panel has no text\n";
    window.setFramerateLimit(60);
    Overlay overlay = opt.overlay;
    bool paused = false;
    int stepsPerFrame = 1;
    uint64_t selected = 0;
    std::vector<uint8_t> rgba((size_t)W * H * 4, 255);

    auto drawText = [&](const std::string &str, float x, float y, unsigned size) {
        if (!haveFont) return;
#if SFML_VERSION_MAJOR >= 3
        sf::Text t(font, str, size);
        t.setPosition({x, y});
#else
        sf::Text t(str, font, size);
        t.setPosition(x, y);
#endif
        t.setFillColor(sf::Color(230, 230, 230));
        window.draw(t);
    };
    auto onKey = [&](auto code) {
        if (code == KEY(Escape)) window.close();
        else if (code == KEY(Space)) paused = !paused;
        else if (code == KEY(Up)) stepsPerFrame = std::min(1024, stepsPerFrame * 2);
        else if (code == KEY(Down)) stepsPerFrame = std::max(1, stepsPerFrame / 2);
        else if (code == KEY(Num1)) overlay = Overlay::Terrain;
        else if (code == KEY(Num2)) overlay = Overlay::Food;
        else if (code == KEY(Num3)) overlay = Overlay::Temperature;
        else if (code == KEY(N)) {
            Params p = sim->p;
            p.seed = 0;
            sim = std::make_unique<Simulation>(p);
            selected = 0;
        }
    };
    auto onClick = [&](float mx, float my) {
        const double x = mx / scale, y = my / scale;
        double best = 9.0;  // within 3 tiles
        selected = 0;
        for (const auto &o : sim->organisms) {
            const double d = (o.x + 0.5 - x) * (o.x + 0.5 - x) + (o.y + 0.5 - y) * (o.y + 0.5 - y);
            if (d < best) best = d, selected = o.id;
        }
    };

    while (window.isOpen()) {
#if SFML_VERSION_MAJOR >= 3
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) window.close();
            else if (const auto *k = event->getIf<sf::Event::KeyPressed>()) onKey(k->code);
            else if (const auto *m = event->getIf<sf::Event::MouseButtonPressed>())
                if (m->button == sf::Mouse::Button::Left) onClick((float)m->position.x, (float)m->position.y);
        }
#else
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            else if (event.type == sf::Event::KeyPressed) onKey(event.key.code);
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left)
                onClick((float)event.mouseButton.x, (float)event.mouseButton.y);
        }
#endif
        if (!window.isOpen()) break;
        if (!paused)
            for (int i = 0; i < stepsPerFrame; i++) sim->step();

        const Image img = render(*sim, 1, overlay, false);
        for (size_t i = 0; i < (size_t)W * H; i++)
            for (int k = 0; k < 3; k++) rgba[i * 4 + k] = img.rgb[i * 3 + k];
        tex.update(rgba.data());
        window.clear(sf::Color(30, 30, 34));
        sf::Sprite map(tex);
#if SFML_VERSION_MAJOR >= 3
        map.setScale({scale, scale});
#else
        map.setScale(scale, scale);
#endif
        window.draw(map);

        const Organism *sel = nullptr;
        sf::CircleShape body;
        sf::RectangleShape tick;
        for (const auto &o : sim->organisms) {
            const float r = std::max(2.0f, (float)(o.size / 2.0 * scale));
            const float cx = (float)((o.x + 0.5) * scale), cy = (float)((o.y + 0.5) * scale);
            body.setRadius(r);
            body.setFillColor(o.wasHit ? sf::Color::Red : sf::Color(o.red, o.green, o.blue, o.alpha));
            body.setOutlineThickness(o.id == selected ? 2.5f : 0.8f);
            body.setOutlineColor(o.id == selected ? sf::Color::White : sf::Color(20, 20, 20));
            tick.setFillColor(sf::Color::Black);
#if SFML_VERSION_MAJOR >= 3
            body.setPosition({cx - r, cy - r});
            tick.setSize({1.6f * r, 1.5f});
            tick.setPosition({cx, cy});
            tick.setRotation(sf::radians((float)o.rotation));
#else
            body.setPosition(cx - r, cy - r);
            tick.setSize(sf::Vector2f(1.6f * r, 1.5f));
            tick.setPosition(cx, cy);
            tick.setRotation((float)(o.rotation * 180.0 / kPi));
#endif
            window.draw(body);
            window.draw(tick);
            if (o.id == selected) sel = &o;
        }
        if (!sel) selected = 0;

        const float px = W * scale + 14;
        const char *names[] = {"terrain", "food", "temperature"};
        drawText(std::string(paused ? "PAUSED  " : "") + "speed x" + std::to_string(stepsPerFrame) + "  overlay " +
                     names[(int)overlay],
                 px, 10, 15);
        drawText(summary(*sim), px, 34, 15);
        if (sel) {
            drawText(describe(*sim, *sel), px, 270, 14);
            // brain: one column per layer, neurons coloured by activation (blue < 0 < red)
            const auto &acts = sel->brain.activations();
            const float bx = px, by = 450, bw = panel - 30, bh = (float)winH - by - 10;
            for (size_t l = 0; l < acts.size(); l++) {
                const float x = bx + bw * (acts.size() == 1 ? 0.5f : (float)l / (acts.size() - 1));
                const size_t n = acts[l].size();
                for (size_t k = 0; k < n; k++) {
                    const double v = std::max(-1.0, std::min(1.0, acts[l][k] * (l == 0 ? 1.0 : 2.0)));
                    sf::CircleShape c(2.6f);
                    c.setFillColor(v >= 0 ? sf::Color(255, (uint8_t)(255 * (1 - v)), (uint8_t)(255 * (1 - v)))
                                          : sf::Color((uint8_t)(255 * (1 + v)), (uint8_t)(255 * (1 + v)), 255));
                    const float y = by + bh * (n == 1 ? 0.5f : (float)k / (n - 1));
#if SFML_VERSION_MAJOR >= 3
                    c.setPosition({x - 2.6f, y - 2.6f});
#else
                    c.setPosition(x - 2.6f, y - 2.6f);
#endif
                    window.draw(c);
                }
            }
        } else {
            drawText("click an organism to inspect it\n\nspace pause, up/down speed\n1/2/3 terrain/food/temperature\nN new world, Esc quit", px, 280, 14);
        }
        window.display();
    }
}
#endif

}  // namespace

int main(int argc, char **argv) {
    Options opt;
    try {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument(a + " needs a value");
                return argv[++i];
            };
            if (a == "-h" || a == "--help") return usage(), 0;
            else if (a == "--config") opt.config = next();
            else if (a == "--seed") opt.seed = std::stol(next());
            else if (a == "--steps") opt.steps = std::stol(next());
            else if (a == "--csv") opt.csv = next();
            else if (a == "--every") opt.every = std::max(1L, std::stol(next()));
            else if (a == "--dump-dir") opt.dumpDir = next();
            else if (a == "--dump-every") opt.dumpEvery = std::max(1L, std::stol(next()));
            else if (a == "--scale") opt.scale = std::max(1, std::stoi(next()));
            else if (a == "--overlay") opt.overlay = parseOverlay(next());
            else if (a == "--font") opt.font = next();
            else if (a == "--no-window") opt.window = false;
            else throw std::invalid_argument("unknown argument " + a);
        }
        std::vector<std::string> warnings;
        Params params = loadParams(opt.config, &warnings);
        for (const auto &w : warnings) std::cerr << opt.config << ": " << w << '\n';
        if (opt.seed >= 0) params.seed = (uint64_t)opt.seed;

        if (opt.window && opt.steps < 0) {
#ifdef WSIM_NO_SFML
            std::cerr << "built without SFML: use --no-window --steps N\n";
            return 1;
#else
            view(opt, params);
            return 0;
#endif
        }
        if (opt.steps < 0) opt.steps = 10000;
        Simulation sim(params);
        std::printf("seed %llu, %dx%d tiles, %d brain inputs\n", (unsigned long long)sim.p.seed, sim.land.W,
                    sim.land.H, params.inputs());
        std::ofstream csv;
        if (!opt.csv.empty()) {
            csv.open(opt.csv);
            if (!csv) throw std::runtime_error("cannot write " + opt.csv);
            csv << csvHeader;
            csvRow(csv, sim);
        }
        if (!opt.dumpDir.empty()) std::filesystem::create_directories(opt.dumpDir);
        int frame = 0;
        for (long s = 1; s <= opt.steps; s++) {
            sim.step();
            if (csv && s % opt.every == 0) csvRow(csv, sim);
            if (!opt.dumpDir.empty() && s % opt.dumpEvery == 0) {
                char name[64];
                std::snprintf(name, sizeof name, "/frame_%05d.ppm", frame++);
                writePpm(opt.dumpDir + name, render(sim, opt.scale, opt.overlay));
            }
        }
        std::cout << summary(sim);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        usage();
        return 2;
    }
    return 0;
}
