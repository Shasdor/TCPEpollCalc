#pragma once

#include <string>
#include <stdexcept>
#include <limits>
#include <cmath>

class ICalc {
public:
    virtual ~ICalc() = default;
    virtual double calculate(const std::string& expr) = 0;
};

class CalcImpl : public ICalc {
public:
    double calculate(const std::string& expr) override {
        if (expr.empty()) {
            throw std::invalid_argument("Empty expression");
        }

        validate_characters(expr);

        size_t pos = 0;
        double result = parse_expression(expr, pos);

        skip_spaces(expr, pos);
        if (pos != expr.size()) {
            throw std::runtime_error("Unexpected characters at position " + std::to_string(pos));
        }

        if (std::isinf(result)) {
            throw std::overflow_error("Arithmetic overflow");
        }

        return result;
    }

private:
    void validate_characters(const std::string& expr) {
        for (char c : expr) {
            if (!std::isdigit(c) && c != '+' && c != '-' && c != '*' &&
                c != '/' && c != '%' && c != '.' &&
                !std::isspace(static_cast<unsigned char>(c))) {
                throw std::runtime_error(std::string("Invalid character: ") + c);
            }
        }
    }

    static void skip_spaces(const std::string& s, size_t& pos) {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
    }

    double parse_expression(const std::string& s, size_t& pos) {
        double lhs = parse_term(s, pos);
        skip_spaces(s, pos);

        while (pos < s.size()) {
            char op = s[pos];
            if (op != '+' && op != '-') break;

            ++pos;
            double rhs = parse_term(s, pos);

            if (op == '+') {
                lhs += rhs;
            } else {
                lhs -= rhs;
            }

            skip_spaces(s, pos);
        }

        return lhs;
    }

    double parse_term(const std::string& s, size_t& pos) {
        double lhs = parse_factor(s, pos);
        skip_spaces(s, pos);

        while (pos < s.size()) {
            char op = s[pos];
            if (op != '*' && op != '/' && op != '%') break;

            ++pos;
            double rhs = parse_factor(s, pos);

            switch (op) {
                case '*':
                    lhs *= rhs;
                    break;
                case '/':
                    if (std::abs(rhs) < std::numeric_limits<double>::epsilon()) {
                        throw std::runtime_error("Division by zero");
                    }
                    lhs /= rhs;
                    break;
                case '%':
                    if (std::abs(rhs) < std::numeric_limits<double>::epsilon()) {
                        throw std::runtime_error("Modulo by zero");
                    }
                    lhs = std::fmod(lhs, rhs);
                    break;
            }

            skip_spaces(s, pos);
        }

        return lhs;
    }

    double parse_factor(const std::string& s, size_t& pos) {
        skip_spaces(s, pos);
        return parse_number(s, pos);
    }

    double parse_number(const std::string& s, size_t& pos) {
        skip_spaces(s, pos);

        if (pos >= s.size()) {
            throw std::runtime_error("Expected number");
        }

        bool negative = false;
        if (s[pos] == '-') {
            negative = true;
            ++pos;
        }

        if (pos >= s.size() || (!std::isdigit(s[pos]) && s[pos] != '.')) {
            throw std::runtime_error("Expected digit or decimal point after minus");
        }

        size_t start = pos;
        bool has_decimal = false;

        while (pos < s.size() &&
               (std::isdigit(s[pos]) || (!has_decimal && s[pos] == '.'))) {
            if (s[pos] == '.') has_decimal = true;
            ++pos;
        }

        try {
            double value = std::stod(s.substr(start, pos - start));
            return negative ? -value : value;
        } catch (...) {
            throw std::runtime_error("Invalid number format");
        }
    }
};
