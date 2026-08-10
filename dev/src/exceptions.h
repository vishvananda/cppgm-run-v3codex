#pragma once
#include <stdexcept>

static constexpr int CPPGM_EXIT_NOT_IMPLEMENTED = 86;

class NotImplementedException : public std::logic_error
{
public:
    NotImplementedException() : std::logic_error("not yet implemented") {}
};

// Semantic failures outside a template candidate's immediate substitution
// context must pass through candidate-local runtime-error handling.
class HardSemanticError : public std::logic_error
{
public:
    explicit HardSemanticError(const std::string& message)
        : std::logic_error(message) {}
};
