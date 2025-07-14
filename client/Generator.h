#pragma once

#include <string>
#include <random>
#include <stdexcept>

class IGenerator {
public:
    virtual ~IGenerator() = default;

    virtual std::string generate_expression(int n) = 0;
};

class Generator : public IGenerator {
public:
    Generator() : rng_(std::random_device{}()) {}

    std::string generate_expression(int n) override {
        if (n <= 0) {
            throw std::invalid_argument("Number of operands must be positive");
        }

        std::uniform_int_distribution<int> num_dist(1, 100);             
        std::uniform_int_distribution<int> op_dist(0, kNumOps - 1);      

        std::string expr;
        expr.reserve(n * 5);  

        for (int i = 0; i < n; ++i) {
            expr += std::to_string(num_dist(rng_));

            if (i < n - 1) {
                expr += kOps[op_dist(rng_)];
            }
        }

        expr += ' ';  
        return expr;
    }

private:
    std::mt19937 rng_;                              
    static constexpr const char* kOps = "+-*/";     
    static constexpr int kNumOps = 4;
};
