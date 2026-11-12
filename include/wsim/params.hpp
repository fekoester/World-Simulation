// Simulation parameters and a small INI reader for Constants.ini.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace wsim {

// Defaults are the values of the shipped Constants.ini; the rest come from the original SimulationData.cpp.
struct Params {
    // ---- world
    uint64_t seed = 0;              // 0 = random seed
    double frequency = 1.0;         // Perlin frequency (number of noise periods across the map)
    int octaves = 7;
    bool island = true;             // subtract a radial falloff so the map is an island
    int width = 125, height = 125;  // tiles
    double minLandFraction = 0.15;  // regenerate worlds with less land than this
    double waterLevel = -0.43;      // terrain thresholds as fractions of the maximum height
    double grassLevel = 0.45;
    double stoneLevel = 0.75;
    double optTemperature = 32.0;   // warmest natural temperature (at the "optimum" height)
    double lowTemperature = -2.0;   // coldest natural temperature (at the highest point)
    double temperatureFluctuationFactor = 10.0;
    double waterTemperature = 3.0;
    double maxFoodOnTile = 10.0;
    double foodGrowthFactor = 0.00009;
    double chanceFoodGrowth = 1.0;
    double foodTemperatureFactor = 0.75;
    double foodHeightFactor = 0.5;
    double foodNoiseFactor = 30.0;
    double temperatureAirFluxFactor = 0.08;  // share of an organism's heat loss that warms its tile
    double tileTemperatureRelaxation = 0.001;
    // ---- life
    int startingOrganisms = 100;
    int minOrganisms = 100;          // random organisms are added when the population drops below this
    bool spawnOnLand = true;         // place new random organisms on land (the original placed them anywhere)
    double avgAgeDeath = 100.0;      // age grows by agePerStep; past this, death becomes likely
    double agePerStep = 0.001;
    double startingEnergy = 250.0;
    double maxEnergyFactor = 1250.0;  // max energy = factor * size^2
    double newlifeEnergy = 500.0;     // energy a parent pays for an offspring
    double foodEnergyFactor = 4.0;
    double consumptionFactor = 0.15;
    double sizeEnergyLossFactor = 0.00005;
    double startingTemperature = 37.0;
    double energyHeatProduction = 0.058;  // heat produced (and energy burnt) per step; evolves
    double heatTimeFactor = 0.07;
    double lowestEnergyHeatProduction = 0.001;
    double highestEnergyHeatProduction = 0.2;
    double heatLossFactor = 0.017;  // evolves
    double lowestHeatLoss = 0.001;
    double highestHeatLoss = 0.5;
    double coldDeathTemperature = 20.0;
    double heatDeathTemperature = 42.0;
    double optimTemperature = 37.0;  // body temperature at which an organism works at 100 %
    double movementSpeed = 0.05;
    double movementEnergyLoss = 0.05;
    double movementEnergyFactorWater = 1.5;
    double movementReductionWater = 0.2;
    double sizeOrganism = 1.0;
    double attackDamage = 0.2;
    double mutateStat = 0.07;
    // ---- brain
    int viewRadius = 3;          // the organism sees a viewRadius x viewRadius grid of tiles in front of it
    int nextEntities = 4;        // ... and the nearest nextEntities organisms
    double nextEntitiesRadius = 200.0;
    int numLayers = 3;           // including input and output layer
    double minRandomWeight = -5.0, maxRandomWeight = 5.0;
    double mutationChance = 0.07;
    double mutationPerturbation = 0.15;
    double sigmoidStretchX = 0.5, sigmoidStretchY = 1.0;

    int inputs() const { return 3 * viewRadius * viewRadius + 7 * nextEntities + 7; }
    int hidden() const { return std::max(1, inputs() / 2); }
    static constexpr int outputs = 5;  // forward, turn, breed, (unused, was heat control), attack

    void validate() const {
        if (width < 8 || height < 8) throw std::invalid_argument("the world must be at least 8 x 8 tiles");
        if (numLayers < 2) throw std::invalid_argument("Num_layers must be >= 2");
        if (viewRadius < 0 || nextEntities < 0) throw std::invalid_argument("negative sensor sizes");
        if (startingOrganisms < 0 || minOrganisms < 0) throw std::invalid_argument("negative organism counts");
        if (frequency <= 0) throw std::invalid_argument("Frequency must be positive");
    }
};

// Minimal INI parser: [sections], key = value, comments with ';', '#' or '//'. Keys are case-insensitive.
class Ini {
public:
    explicit Ini(const std::string &path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("cannot open " + path);
        std::string line, section;
        int n = 0;
        while (std::getline(f, line)) {
            n++;
            auto trim = [](std::string s) {
                const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
                return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
            };
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#' || line.rfind("//", 0) == 0) continue;
            if (line[0] == '[') {
                section = lower(trim(line.substr(1, line.find(']') - 1)));
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                warnings.push_back("line " + std::to_string(n) + ": ignored '" + line + "'");
                continue;
            }
            std::string value = line.substr(eq + 1);
            const auto c = value.find(';');
            if (c != std::string::npos) value = value.substr(0, c);
            values_[section + "." + lower(trim(line.substr(0, eq)))] = trim(value);
        }
    }

    bool has(const std::string &section, const std::string &key) const {
        return values_.count(lower(section) + "." + lower(key)) > 0;
    }
    double real(const std::string &section, const std::string &key, double fallback) const {
        auto it = values_.find(lower(section) + "." + lower(key));
        if (it == values_.end()) return fallback;
        char *end = nullptr;
        const double v = std::strtod(it->second.c_str(), &end);
        if (end == it->second.c_str()) throw std::runtime_error("[" + section + "] " + key + " is not a number");
        return v;
    }
    int integer(const std::string &s, const std::string &k, int fallback) const { return (int)real(s, k, fallback); }
    bool boolean(const std::string &s, const std::string &k, bool fallback) const {
        auto it = values_.find(lower(s) + "." + lower(k));
        if (it == values_.end()) return fallback;
        const std::string v = lower(it->second);
        return v == "1" || v == "true" || v == "yes" || v == "on";
    }

    std::vector<std::string> warnings;

private:
    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }
    std::map<std::string, std::string> values_;
};

// Read Constants.ini (missing keys keep their defaults).
inline Params loadParams(const std::string &path, std::vector<std::string> *warnings = nullptr) {
    Ini ini(path);
    Params p;
    const std::string W = "World properties", L = "Life properties", N = "Network properties";
    p.seed = (uint64_t)ini.real(W, "Random seed", 0);
    p.frequency = ini.real(W, "Frequency", p.frequency);
    p.octaves = ini.integer(W, "Octaves", p.octaves);
    p.island = ini.boolean(W, "Island_mode", p.island);
    p.width = ini.integer(W, "Simulation_x", p.width);
    p.minLandFraction = ini.real(W, "Min_land_fraction", p.minLandFraction);
    p.height = ini.integer(W, "Simulation_y", p.height);
    p.waterLevel = ini.real(W, "Water_level", p.waterLevel);
    p.grassLevel = ini.real(W, "Grass_level", p.grassLevel);
    p.stoneLevel = ini.real(W, "Stone_level", p.stoneLevel);
    p.optTemperature = ini.real(W, "Opt_temperature", p.optTemperature);
    p.lowTemperature = ini.real(W, "Low_temperature", p.lowTemperature);
    p.temperatureFluctuationFactor = ini.real(W, "Temperature_fluctuation_factor", p.temperatureFluctuationFactor);
    p.waterTemperature = ini.real(W, "Temperature_of_water", p.waterTemperature);
    p.maxFoodOnTile = ini.real(W, "Max_food_on_tile", p.maxFoodOnTile);
    p.foodGrowthFactor = ini.real(W, "Food_growth_factor", p.foodGrowthFactor);
    p.chanceFoodGrowth = ini.real(W, "Chance_food_growth", p.chanceFoodGrowth);
    p.foodTemperatureFactor = ini.real(W, "Food_temperature_factor", p.foodTemperatureFactor);
    p.foodHeightFactor = ini.real(W, "Food_height_factor", p.foodHeightFactor);
    p.foodNoiseFactor = ini.real(W, "Food_noise_factor", p.foodNoiseFactor);
    p.temperatureAirFluxFactor = ini.real(W, "Temperature_air_flux_factor", p.temperatureAirFluxFactor);

    p.startingOrganisms = ini.integer(L, "Starting_number_organisms", p.startingOrganisms);
    p.minOrganisms = ini.integer(L, "Min_number_organisms", p.minOrganisms);
    p.spawnOnLand = ini.boolean(L, "Spawn_on_land", p.spawnOnLand);
    p.avgAgeDeath = ini.real(L, "Avg_age_death", p.avgAgeDeath);
    p.startingEnergy = ini.real(L, "Starting_energy", p.startingEnergy);
    p.maxEnergyFactor = ini.real(L, "Max_energy_factor", p.maxEnergyFactor);
    p.newlifeEnergy = ini.real(L, "Newlife_energy_constant", p.newlifeEnergy);
    p.foodEnergyFactor = ini.real(L, "Food_energy_factor", p.foodEnergyFactor);
    p.consumptionFactor = ini.real(L, "Consumption_food_factor", p.consumptionFactor);
    p.sizeEnergyLossFactor = ini.real(L, "Size_energy_loss_factor", p.sizeEnergyLossFactor);
    p.startingTemperature = ini.real(L, "Starting_temperature", p.startingTemperature);
    p.energyHeatProduction = ini.real(L, "Energy_heat_production", p.energyHeatProduction);
    p.heatTimeFactor = ini.real(L, "Heat_time_factor", p.heatTimeFactor);
    p.lowestEnergyHeatProduction = ini.real(L, "Lowest_energy_heat_production", p.lowestEnergyHeatProduction);
    p.highestEnergyHeatProduction = ini.real(L, "Highest_energy_heat_production", p.highestEnergyHeatProduction);
    p.heatLossFactor = ini.real(L, "Heat_loss_factor", p.heatLossFactor);
    p.lowestHeatLoss = ini.real(L, "Lowest_heat_loss_factor", p.lowestHeatLoss);
    // the original file spells it "Higehst"; accept both
    p.highestHeatLoss = ini.real(L, "Highest_heat_loss_factor", ini.real(L, "Higehst_heat_loss_factor", p.highestHeatLoss));
    p.coldDeathTemperature = ini.real(L, "Cold_temp_death", p.coldDeathTemperature);
    p.heatDeathTemperature = ini.real(L, "Heat_temp_death", p.heatDeathTemperature);
    p.optimTemperature = ini.real(L, "Optim_temperature", p.optimTemperature);
    p.movementSpeed = ini.real(L, "Movement_speed", p.movementSpeed);
    p.movementEnergyLoss = ini.real(L, "Movement_energy_loss", p.movementEnergyLoss);
    p.movementEnergyFactorWater = ini.real(L, "Movement_energy_factor_water", p.movementEnergyFactorWater);
    p.movementReductionWater = ini.real(L, "Movement_reduction_in_water", p.movementReductionWater);
    p.sizeOrganism = ini.real(L, "Size_organism", p.sizeOrganism);
    p.attackDamage = ini.real(L, "Attack_damage", p.attackDamage);
    p.mutateStat = ini.real(L, "Mutate_stat", p.mutateStat);
    p.agePerStep = ini.real(L, "Age_per_step", p.agePerStep);

    p.viewRadius = ini.integer(N, "Adjacent_tiles_radius", p.viewRadius);
    p.nextEntities = ini.integer(N, "Number_next_entities", p.nextEntities);
    p.nextEntitiesRadius = ini.real(N, "Radius_next_entities", p.nextEntitiesRadius);
    p.numLayers = ini.integer(N, "Num_layers", p.numLayers);
    p.minRandomWeight = ini.real(N, "Min_random_weight", p.minRandomWeight);
    p.maxRandomWeight = ini.real(N, "Max_random_weight", p.maxRandomWeight);
    p.mutationChance = ini.real(N, "Mutation_chance", p.mutationChance);
    p.mutationPerturbation = ini.real(N, "Mutation_perturbation", p.mutationPerturbation);
    p.sigmoidStretchX = ini.real(N, "Sigmoid_stretch_x", p.sigmoidStretchX);
    p.sigmoidStretchY = ini.real(N, "Sigmoid_stretch_y", p.sigmoidStretchY);
    if (warnings) *warnings = ini.warnings;
    p.validate();
    return p;
}

}  // namespace wsim
