// The organisms' brain: a fully connected network evolved by mutation (no learning during life).
#pragma once

#include <cmath>
#include <random>
#include <vector>

#include "params.hpp"

namespace wsim {

class Brain {
public:
    Brain() = default;

    // Layer sizes: inputs, (numLayers - 2) hidden layers of hidden(), outputs. Weights ~ U(minW, maxW).
    template <class Rng>
    Brain(const Params &p, Rng &rng) {
        sizes_.push_back(p.inputs());
        for (int i = 0; i < p.numLayers - 2; i++) sizes_.push_back(p.hidden());
        sizes_.push_back(Params::outputs);
        size_t n = 0;
        for (size_t l = 0; l + 1 < sizes_.size(); l++) {
            offsets_.push_back(n);
            n += (size_t)(sizes_[l] + 1) * sizes_[l + 1];  // +1: bias row
        }
        weights_.resize(n);
        std::uniform_real_distribution<double> u(p.minRandomWeight, p.maxRandomWeight);
        for (auto &w : weights_) w = u(rng);
        activations_.resize(sizes_.size());
        for (size_t l = 0; l < sizes_.size(); l++) activations_[l].assign(sizes_[l], 0.0);
    }

    // Each layer: a_{l+1} = Y * (sigmoid(-X * (W_l^T [a_l; 1])) - 1/2), outputs in (-Y/2, Y/2).
    const std::vector<double> &forward(const std::vector<double> &input, const Params &p) {
        activations_[0] = input;
        for (size_t l = 0; l + 1 < sizes_.size(); l++) {
            const int ni = sizes_[l], no = sizes_[l + 1];
            const double *w = &weights_[offsets_[l]];
            auto &out = activations_[l + 1];
            out.assign(no, 0.0);
            for (int k = 0; k <= ni; k++) {
                const double v = k < ni ? activations_[l][k] : 1.0;
                if (v == 0.0) continue;
                const double *row = w + (size_t)k * no;
                for (int j = 0; j < no; j++) out[j] += v * row[j];
            }
            for (auto &x : out) x = p.sigmoidStretchY * (1.0 / (1.0 + std::exp(p.sigmoidStretchX * x)) - 0.5);
        }
        return activations_.back();
    }

    // Every weight: with probability mutationChance perturb it by up to +-mutationPerturbation of its value,
    // and (independently) with probability mutationChance replace it by a new random weight.
    template <class Rng>
    void mutate(const Params &p, Rng &rng) {
        std::uniform_real_distribution<double> u(0.0, 1.0), pert(-p.mutationPerturbation, p.mutationPerturbation),
            fresh(p.minRandomWeight, p.maxRandomWeight);
        for (auto &w : weights_) {
            if (u(rng) < p.mutationChance) w += w * pert(rng);
            if (u(rng) < p.mutationChance) w = fresh(rng);
        }
    }

    const std::vector<int> &sizes() const { return sizes_; }
    const std::vector<double> &weights() const { return weights_; }
    std::vector<double> &weights() { return weights_; }
    const std::vector<double> &outputs() const { return activations_.back(); }
    const std::vector<std::vector<double>> &activations() const { return activations_; }
    // weight from unit k of layer l (k == sizes[l] is the bias) to unit j of layer l + 1
    double weight(size_t l, int k, int j) const { return weights_[offsets_[l] + (size_t)k * sizes_[l + 1] + j]; }

private:
    std::vector<int> sizes_;
    std::vector<size_t> offsets_;
    std::vector<double> weights_;
    std::vector<std::vector<double>> activations_;
};

}  // namespace wsim
