#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace strings {

struct Pool {
    std::vector<std::string> texts;
};

int add(Pool& pool, std::string text);
void emit(Pool const& pool, std::ostream& out);

}  // namespace strings
