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

std::string canonicalPath(std::string const& path) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(path, ec);
    if (ec) return path;
    return c.string();
}

std::string modulePath(std::string const& module) {
    std::string p = module;
    for (char& c : p) if (c == '.') c = '/';
    return p;
}

std::string moduleIdentity(std::string const& path,
                           std::vector<std::string> const& roots) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cp = fs::weakly_canonical(path, ec);
    if (ec) cp = fs::path(path).lexically_normal();
    bool found = false;
    std::size_t best_len = 0;
    fs::path best;
    for (auto const& r : roots) {
        fs::path cr = fs::weakly_canonical(r, ec);
        if (ec) continue;
        if (cr.filename().empty()) cr = cr.parent_path();   // a trailing '/'
        fs::path rel = cp.lexically_relative(cr);
        if (rel.empty() || rel == "." || *rel.begin() == "..") continue;
        std::size_t len = cr.string().size();
        if (!found || len > best_len) { found = true; best_len = len; best = rel; }
    }
    if (!found) return fs::path(path).stem().string();
    best.replace_extension();
    std::string m;
    for (auto const& part : best) {
        if (!m.empty()) m += '.';
        m += part.string();
    }
    return m;
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
