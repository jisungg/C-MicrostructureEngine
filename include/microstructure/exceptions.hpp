#pragma once

#include <stdexcept>
#include <string>

namespace microstructure {

class MicrostructureError : public std::runtime_error {
public:
    explicit MicrostructureError(const std::string& message)
        : std::runtime_error(message) {}
};

class ValidationError : public MicrostructureError {
public:
    explicit ValidationError(const std::string& message)
        : MicrostructureError(message) {}
};

class BookInvariantError : public MicrostructureError {
public:
    explicit BookInvariantError(const std::string& message)
        : MicrostructureError(message) {}
};

class DuplicateOrderError : public MicrostructureError {
public:
    explicit DuplicateOrderError(const std::string& message)
        : MicrostructureError(message) {}
};

class OrderNotFoundError : public MicrostructureError {
public:
    explicit OrderNotFoundError(const std::string& message)
        : MicrostructureError(message) {}
};

class CrossedBookError : public MicrostructureError {
public:
    explicit CrossedBookError(const std::string& message)
        : MicrostructureError(message) {}
};

class ReplayError : public MicrostructureError {
public:
    explicit ReplayError(const std::string& message)
        : MicrostructureError(message) {}
};

} // namespace microstructure
