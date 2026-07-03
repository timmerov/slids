#include "strings.h"

#include <cstdio>
#include <ostream>
#include <utility>

namespace strings {

int add(Pool& pool, std::string text) {
    for (int i = 0; i < static_cast<int>(pool.texts.size()); i++) {
        if (pool.texts[i] == text) return i;
    }
    int id = static_cast<int>(pool.texts.size());
    pool.texts.push_back(std::move(text));
    return id;
}

void emit(Pool const& pool, std::ostream& out) {
    for (int i = 0; i < static_cast<int>(pool.texts.size()); i++) {
        std::string const& text = pool.texts[i];
        out << "@.str_" << i << " = private unnamed_addr constant ["
            << (text.size() + 1) << " x i8] c\"";
        for (unsigned char c : text) {
            if (c == '\\')      out << "\\\\";
            else if (c == '"')  out << "\\22";
            else if (c >= 0x20 && c < 0x7F) out << static_cast<char>(c);
            else {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\%02X", static_cast<unsigned>(c));
                out << buf;
            }
        }
        out << "\\00\"\n";
    }
}

}  // namespace strings
