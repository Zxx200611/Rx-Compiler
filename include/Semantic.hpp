#pragma once

#include "AstBuild.hpp"

namespace rxast {

struct SemanticResult {
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const noexcept { return diagnostics.empty(); }
};

SemanticResult checkProgram(const Program& program);
SemanticResult checkSource(const std::string& source, std::string sourceName = {});

} // namespace rxast
