#include "token.h"

#include <filesystem>
#include <utility>

namespace token {

std::string templateSourcePath(std::string const& header_path) {
    std::filesystem::path hp(header_path);
    return (hp.parent_path() / (hp.stem().string() + ".sl")).string();
}

bool samePath(std::string const& a, std::string const& b) {
    std::error_code ea, eb;
    std::filesystem::path ca = std::filesystem::weakly_canonical(a, ea);
    std::filesystem::path cb = std::filesystem::weakly_canonical(b, eb);
    if (ea || eb) return a == b;
    return ca == cb;
}

void add(List& list, Token const& tok) {
    list.tokens.push_back(tok);
}

int openFile(List& list, std::string path, std::string source, int imported_by) {
    File f;
    f.path = std::move(path);
    f.source = std::move(source);
    f.imported_by = imported_by;
    f.line_starts.push_back(0);
    for (int i = 0; i < (int)f.source.size(); i++) {
        if (f.source[i] == '\n') f.line_starts.push_back(i + 1);
    }
    int id = (int)list.files.size();
    list.files.push_back(std::move(f));
    return id;
}

}  // namespace token
