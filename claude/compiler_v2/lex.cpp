#include "lex.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic.h"
#include "token.h"

namespace lex {

namespace {

// ---------------- low-level scanner over a Stream ----------------

char peek(Stream const& s) {
    if (s.pos >= (int)s.source->size()) return '\0';
    return (*s.source)[s.pos];
}

char peek2(Stream const& s) {
    if (s.pos + 1 >= (int)s.source->size()) return '\0';
    return (*s.source)[s.pos + 1];
}

char advance(Stream& s) {
    char c = (*s.source)[s.pos++];
    if (c == '\n') { s.line++; s.col = 1; }
    else { s.col++; }
    return c;
}

void setFatal(Stream& s, int line, int col, int len, std::string const& msg) {
    if (s.fatal) return;   // first error wins
    s.fatal = true;
    s.fatal_line = line;
    s.fatal_col = col;
    s.fatal_length = len;
    s.fatal_msg = msg;
}

token::Token errorToken(Stream const& s) {
    return {token::Kind::kError, "", s.file_id, s.fatal_line, s.fatal_col, s.fatal_length};
}

void skipWhitespaceAndComments(Stream& s) {
    int depth = 0;
    bool in_line = false;
    while (s.pos < (int)s.source->size()) {
        char c = peek(s);
        char d = peek2(s);

        if (!in_line && depth == 0 && (c == '/' || c == '*')) {
            int p = s.pos + 1;
            if (p < (int)s.source->size() && (*s.source)[p] == '\\') {
                int q = p + 1;
                while (q < (int)s.source->size()
                    && ((*s.source)[q] == ' ' || (*s.source)[q] == '\t' || (*s.source)[q] == '\r')) q++;
                if (q < (int)s.source->size() && (*s.source)[q] == '\n') {
                    int r = q + 1;
                    while (r < (int)s.source->size()
                        && ((*s.source)[r] == ' ' || (*s.source)[r] == '\t' || (*s.source)[r] == '\r')) r++;
                    if (r < (int)s.source->size()) {
                        char e = (*s.source)[r];
                        bool match = (c == '/' && (e == '/' || e == '*'))
                                  || (c == '*' && e == '/');
                        if (match) {
                            std::string pair = {c, e};
                            setFatal(s, s.line, s.col + 1, 1,
                                "escaped newline breaking comment token " + pair);
                            return;
                        }
                    }
                }
            }
        }

        if (c == '/' && d == '/') {
            in_line = true;
            advance(s); advance(s);
            continue;
        }
        if (c == '/' && d == '*') {
            depth++;
            advance(s); advance(s);
            continue;
        }
        if (c == '*' && d == '/') {
            if (depth == 0) {
                setFatal(s, s.line, s.col, 2, "unmatched */");
                return;
            }
            depth--;
            advance(s); advance(s);
            continue;
        }
        if (in_line) {
            if (c == '\n') { in_line = false; advance(s); continue; }
            if (c == '\\') {
                int p = s.pos + 1;
                while (p < (int)s.source->size()
                    && ((*s.source)[p] == ' ' || (*s.source)[p] == '\t'
                        || (*s.source)[p] == '\r')) p++;
                if (p < (int)s.source->size() && (*s.source)[p] == '\n') {
                    if (p != s.pos + 1) {
                        setFatal(s, s.line, s.col, 1,
                            "whitespace between line-continuation \\ and newline");
                        return;
                    }
                    advance(s); advance(s);
                    continue;
                }
            }
            advance(s);
            continue;
        }
        if (depth > 0) { advance(s); continue; }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(s);
            continue;
        }
        break;
    }
    if (depth > 0) setFatal(s, s.line, s.col, 1, "unterminated block comment");
}

token::Token readCharLiteral(Stream& s) {
    advance(s); // consume opening '
    int value = 0;
    if (peek(s) == '\\') {
        advance(s);
        char esc = advance(s);
        switch (esc) {
            case 'n':  value = '\n'; break;
            case 't':  value = '\t'; break;
            case '0':  value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            default:   value = esc;  break;
        }
    } else {
        value = (unsigned char)advance(s);
    }
    if (peek(s) == '\'') advance(s);
    return {token::Kind::kCharLiteral, std::to_string(value), 0, 0, 0, 0};
}

token::Token readString(Stream& s) {
    advance(s);
    std::string value;
    while (s.pos < (int)s.source->size() && peek(s) != '"') {
        char c = advance(s);
        if (c == '\\') {
            char esc = advance(s);
            switch (esc) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '\n': break;
                default:   value += esc;  break;
            }
        } else {
            value += c;
        }
    }
    if (peek(s) == '"') advance(s);
    return {token::Kind::kStringLiteral, value, 0, 0, 0, 0};
}

token::Token readNumber(Stream& s) {
    std::string value;
    if (peek(s) == '0' && (peek2(s) == 'x' || peek2(s) == 'X')) {
        advance(s); advance(s);
        std::string digits;
        while (s.pos < (int)s.source->size() && (isxdigit(peek(s)) || peek(s) == '_'))
            digits += advance(s);
        std::string clean;
        for (char c : digits) if (c != '_') clean += c;
        uint64_t uval = std::stoull(clean, nullptr, 16);
        return {token::Kind::kUintLiteral, std::to_string(uval), 0, 0, 0, 0};
    }
    if (peek(s) == '0' && (peek2(s) == 'b' || peek2(s) == 'B')) {
        advance(s); advance(s);
        std::string digits;
        while (s.pos < (int)s.source->size() && (peek(s) == '0' || peek(s) == '1' || peek(s) == '_'))
            digits += advance(s);
        std::string clean;
        for (char c : digits) if (c != '_') clean += c;
        uint64_t uval = std::stoull(clean, nullptr, 2);
        return {token::Kind::kUintLiteral, std::to_string(uval), 0, 0, 0, 0};
    }
    while (s.pos < (int)s.source->size() && (isdigit(peek(s)) || peek(s) == '_'))
        value += advance(s);
    std::string clean;
    for (char c : value) if (c != '_') clean += c;
    bool is_float = false;
    if (peek(s) == '.' && peek2(s) != '.') {
        is_float = true;
        clean += advance(s);
        while (s.pos < (int)s.source->size() && (isdigit(peek(s)) || peek(s) == '_')) {
            char ch = advance(s);
            if (ch != '_') clean += ch;
        }
    }
    if (peek(s) == 'e' || peek(s) == 'E') {
        is_float = true;
        clean += advance(s);
        if (peek(s) == '+' || peek(s) == '-') clean += advance(s);
        if (!isdigit(peek(s))) {
            setFatal(s, s.line, s.col, 1, "malformed exponent: expected a digit");
            return {token::Kind::kError, clean, 0, 0, 0, 0};
        }
        while (s.pos < (int)s.source->size() && (isdigit(peek(s)) || peek(s) == '_')) {
            char ch = advance(s);
            if (ch != '_') clean += ch;
        }
    }
    return {is_float ? token::Kind::kFloatLiteral : token::Kind::kIntLiteral, clean, 0, 0, 0, 0};
}

token::Token readIdentifierOrKeyword(Stream& s) {
    std::string value;
    while (s.pos < (int)s.source->size() && (isalnum(peek(s)) || peek(s) == '_'))
        value += advance(s);

    struct KW { char const* name; token::Kind kind; };
    static KW const table[] = {
        {"int",      token::Kind::kInt},
        {"int8",     token::Kind::kInt8},
        {"int16",    token::Kind::kInt16},
        {"int32",    token::Kind::kInt32},
        {"int64",    token::Kind::kInt64},
        {"uint",     token::Kind::kUint},
        {"uint8",    token::Kind::kUint8},
        {"uint16",   token::Kind::kUint16},
        {"uint32",   token::Kind::kUint32},
        {"uint64",   token::Kind::kUint64},
        {"char",     token::Kind::kChar},
        {"intptr",   token::Kind::kIntptr},
        {"float32",  token::Kind::kFloat32},
        {"float",    token::Kind::kFloat},
        {"float64",  token::Kind::kFloat64},
        {"bool",     token::Kind::kBool},
        {"void",     token::Kind::kVoid},
        {"return",   token::Kind::kReturn},
        {"true",     token::Kind::kTrue},
        {"false",    token::Kind::kFalse},
        {"if",       token::Kind::kIf},
        {"else",     token::Kind::kElse},
        {"while",    token::Kind::kWhile},
        {"for",      token::Kind::kFor},
        {"break",    token::Kind::kBreak},
        {"continue", token::Kind::kContinue},
        {"enum",     token::Kind::kEnum},
        {"switch",   token::Kind::kSwitch},
        {"case",     token::Kind::kCase},
        {"default",  token::Kind::kDefault},
        {"new",      token::Kind::kNew},
        {"delete",   token::Kind::kDelete},
        {"nullptr",  token::Kind::kNullptr},
        {"self",     token::Kind::kSelf},
        {"import",   token::Kind::kImport},
        {"virtual",  token::Kind::kVirtual},
        {"sizeof",   token::Kind::kSizeof},
        {"op",       token::Kind::kOp},
        {"mutable",  token::Kind::kMutable},
        {"const",    token::Kind::kConst},
        {"alias",    token::Kind::kAlias},
        {"global",   token::Kind::kGlobal},
    };
    for (auto const& kw : table) {
        if (value == kw.name) return {kw.kind, value, 0, 0, 0, 0};
    }
    return {token::Kind::kIdentifier, value, 0, 0, 0, 0};
}

}  // namespace

// ---------------- public scanner API ----------------

Stream open(int file_id, std::string const& source) {
    Stream s;
    s.file_id = file_id;
    s.source = &source;
    return s;
}

token::Token next(Stream& s) {
    if (!s.pending.empty()) {
        token::Token t = s.pending.front();
        s.pending.pop_front();
        return t;
    }
    if (s.fatal) return errorToken(s);

    skipWhitespaceAndComments(s);
    if (s.fatal) return errorToken(s);

    int sl = s.line, sc = s.col, sp = s.pos;
    if (s.pos >= (int)s.source->size()) {
        return {token::Kind::kEndOfFile, "", s.file_id, sl, sc, 0};
    }

    char c = peek(s);
    token::Token t;
    if (c == '"')                    t = readString(s);
    else if (c == '\'')              t = readCharLiteral(s);
    else if (isdigit(c))             t = readNumber(s);
    else if (isalpha(c) || c == '_') t = readIdentifierOrKeyword(s);
    else {
        advance(s);
        switch (c) {
            case '(': t = {token::Kind::kLParen,    "(", 0, 0, 0, 0}; break;
            case ')': t = {token::Kind::kRParen,    ")", 0, 0, 0, 0}; break;
            case '{': t = {token::Kind::kLBrace,    "{", 0, 0, 0, 0}; break;
            case '}': t = {token::Kind::kRBrace,    "}", 0, 0, 0, 0}; break;
            case ';': t = {token::Kind::kSemicolon, ";", 0, 0, 0, 0}; break;
            case ',': t = {token::Kind::kComma,     ",", 0, 0, 0, 0}; break;
            case '+':
                if (peek(s) == '+')      { advance(s); t = {token::Kind::kPlusPlus, "++", 0, 0, 0, 0}; }
                else if (peek(s) == '=') { advance(s); t = {token::Kind::kPlusEq,   "+=", 0, 0, 0, 0}; }
                else                     {             t = {token::Kind::kPlus,     "+",  0, 0, 0, 0}; }
                break;
            case '-':
                if (peek(s) == '-')      { advance(s); t = {token::Kind::kMinusMinus, "--", 0, 0, 0, 0}; }
                else if (peek(s) == '=') { advance(s); t = {token::Kind::kMinusEq,    "-=", 0, 0, 0, 0}; }
                else                     {             t = {token::Kind::kMinus,      "-",  0, 0, 0, 0}; }
                break;
            case '*':
                if (peek(s) == '=') { advance(s); t = {token::Kind::kStarEq, "*=", 0, 0, 0, 0}; }
                else                {             t = {token::Kind::kStar,   "*",  0, 0, 0, 0}; }
                break;
            case '/':
                if (peek(s) == '=') { advance(s); t = {token::Kind::kSlashEq, "/=", 0, 0, 0, 0}; }
                else                {             t = {token::Kind::kSlash,   "/",  0, 0, 0, 0}; }
                break;
            case '%':
                if (peek(s) == '=') { advance(s); t = {token::Kind::kPercentEq, "%=", 0, 0, 0, 0}; }
                else                {             t = {token::Kind::kPercent,   "%",  0, 0, 0, 0}; }
                break;
            case '!':
                if (peek(s) == '=') { advance(s); t = {token::Kind::kNotEq, "!=", 0, 0, 0, 0}; }
                else                {             t = {token::Kind::kNot,   "!",  0, 0, 0, 0}; }
                break;
            case '=':
                if (peek(s) == '=') { advance(s); t = {token::Kind::kEqEq,   "==", 0, 0, 0, 0}; }
                else                {             t = {token::Kind::kEquals, "=",  0, 0, 0, 0}; }
                break;
            case '<':
                if (peek(s) == '<') {
                    advance(s);
                    if (peek(s) == '=') { advance(s); t = {token::Kind::kLShiftEq, "<<=", 0, 0, 0, 0}; }
                    else                {             t = {token::Kind::kLShift,   "<<",  0, 0, 0, 0}; }
                } else if (peek(s) == '=') { advance(s); t = {token::Kind::kLtEq, "<=", 0, 0, 0, 0}; }
                else if (peek(s) == '-' && peek2(s) == '-') {
                    advance(s); advance(s);
                    if (peek(s) == '>') { advance(s); t = {token::Kind::kArrowBoth, "<-->", 0, 0, 0, 0}; }
                    else                {             t = {token::Kind::kArrowLeft, "<--",  0, 0, 0, 0}; }
                }
                else { t = {token::Kind::kLt, "<", 0, 0, 0, 0}; }
                break;
            case '>':
                if (peek(s) == '>') {
                    advance(s);
                    if (peek(s) == '=') { advance(s); t = {token::Kind::kRShiftEq, ">>=", 0, 0, 0, 0}; }
                    else                {             t = {token::Kind::kRShift,   ">>",  0, 0, 0, 0}; }
                } else if (peek(s) == '=') { advance(s); t = {token::Kind::kGtEq, ">=", 0, 0, 0, 0}; }
                else                       {             t = {token::Kind::kGt,   ">",  0, 0, 0, 0}; }
                break;
            case '&':
                if (peek(s) == '&') {
                    advance(s);
                    if (peek(s) == '=') { advance(s); t = {token::Kind::kAndEq, "&&=", 0, 0, 0, 0}; }
                    else                {             t = {token::Kind::kAnd,   "&&",  0, 0, 0, 0}; }
                } else if (peek(s) == '=') { advance(s); t = {token::Kind::kBitAndEq, "&=", 0, 0, 0, 0}; }
                else                       {             t = {token::Kind::kBitAnd,   "&",  0, 0, 0, 0}; }
                break;
            case '|':
                if (peek(s) == '|') {
                    advance(s);
                    if (peek(s) == '=') { advance(s); t = {token::Kind::kOrEq, "||=", 0, 0, 0, 0}; }
                    else                {             t = {token::Kind::kOr,   "||",  0, 0, 0, 0}; }
                } else if (peek(s) == '=') { advance(s); t = {token::Kind::kBitOrEq, "|=", 0, 0, 0, 0}; }
                else                       {             t = {token::Kind::kBitOr,   "|",  0, 0, 0, 0}; }
                break;
            case '^': {
                int n = 1;
                while (peek(s) == '^') { advance(s); n++; }
                if (n >= 3) {
                    // All `^` tokens carry the same start position (matches v1 quirk).
                    int len = s.pos - sp;
                    t = {token::Kind::kBitXor, "^", s.file_id, sl, sc, len};
                    for (int k = 1; k < n; k++) {
                        s.pending.push_back({token::Kind::kBitXor, "^", s.file_id, sl, sc, len});
                    }
                } else if (n == 2) {
                    if (peek(s) == '=') { advance(s); t = {token::Kind::kXorXorEq, "^^=", 0, 0, 0, 0}; }
                    else                {             t = {token::Kind::kXorXor,   "^^",  0, 0, 0, 0}; }
                } else {
                    if (peek(s) == '=') { advance(s); t = {token::Kind::kBitXorEq, "^=", 0, 0, 0, 0}; }
                    else                {             t = {token::Kind::kBitXor,   "^",  0, 0, 0, 0}; }
                }
                break;
            }
            case '~':
                t = {token::Kind::kBitNot, "~", 0, 0, 0, 0};
                break;
            case '.':
                if (peek(s) == '.' && s.pos + 1 < (int)s.source->size() && (*s.source)[s.pos + 1] == '.') {
                    advance(s); advance(s);
                    t = {token::Kind::kEllipsis, "...", 0, 0, 0, 0};
                } else if (peek(s) == '.') {
                    advance(s);
                    t = {token::Kind::kDotDot, "..", 0, 0, 0, 0};
                } else {
                    t = {token::Kind::kDot, ".", 0, 0, 0, 0};
                }
                break;
            case ':':
                if (peek(s) == ':') { advance(s); t = {token::Kind::kColonColon, "::", 0, 0, 0, 0}; }
                else                {             t = {token::Kind::kColon,      ":",  0, 0, 0, 0}; }
                break;
            case '[': t = {token::Kind::kLBracket, "[", 0, 0, 0, 0}; break;
            case ']': t = {token::Kind::kRBracket, "]", 0, 0, 0, 0}; break;
            case '#':
                if (peek(s) == '#') { advance(s); t = {token::Kind::kHashHash, "##", 0, 0, 0, 0}; }
                else                {             t = {token::Kind::kHash,     "#",  0, 0, 0, 0}; }
                break;
            default:
                t = {token::Kind::kUnknown, std::string(1, c), 0, 0, 0, 0};
                break;
        }
    }

    if (s.fatal) return errorToken(s);

    // Fill position fields if a case left them at 0 (the `^`-run case sets its own).
    if (t.line == 0) {
        t.file_id = s.file_id;
        t.line = sl;
        t.col = sc;
        t.length = s.pos - sp;
    }
    return t;
}

// ---------------- ImportWrapper ----------------

namespace {

struct ImportWrapper {
    token::List& out;
    diagnostic::Sink& diag;
    std::vector<std::string> const& import_paths;
    std::set<std::string>& imported_once;
    bool fatal = false;

    void processFile(int file_id, std::string const& source_dir);
};

void reportAt(diagnostic::Sink& diag, int file_id, int tok_index, std::string const& msg) {
    diagnostic::report(diag, {file_id, tok_index, msg, {}});
}

int addAndIndex(token::List& out, token::Token const& t) {
    token::add(out, t);
    return (int)out.tokens.size() - 1;
}

void ImportWrapper::processFile(int file_id, std::string const& source_dir) {
    Stream s = lex::open(file_id, out.files[file_id].source);

    int depth = 0;
    std::vector<char> brackets;   // expected close chars

    enum class State { Normal, SawImport, SawImportIdent };
    State state = State::Normal;
    token::Token saved_import{};
    token::Token saved_ident{};

    // handle() returns false when this file's processing must stop.
    std::function<bool(token::Token const&)> handle = [&](token::Token const& t) -> bool {
        // Terminal handling: kEndOfFile flushes any buffered pushback, checks
        // depth, emits the kEndOfFile, and stops this file.
        if (t.kind == token::Kind::kEndOfFile) {
            if (state == State::SawImport) {
                token::add(out, saved_import);
            } else if (state == State::SawImportIdent) {
                token::add(out, saved_import);
                token::add(out, saved_ident);
            }
            state = State::Normal;
            if (depth != 0) {
                int idx = addAndIndex(out, t);
                reportAt(diag, file_id, idx, "unterminated open bracket at end of file");
                fatal = true;
                return false;
            }
            token::add(out, t);
            return false;
        }
        if (t.kind == token::Kind::kError) {
            int idx = addAndIndex(out, t);
            reportAt(diag, file_id, idx, s.fatal_msg);
            fatal = true;
            return false;
        }

        switch (state) {
            case State::Normal: {
                // Bracket-kind tracking ( ) { } [ ] only.
                if (t.kind == token::Kind::kLParen)         { brackets.push_back(')'); depth++; }
                else if (t.kind == token::Kind::kLBrace)    { brackets.push_back('}'); depth++; }
                else if (t.kind == token::Kind::kLBracket)  { brackets.push_back(']'); depth++; }
                else if (t.kind == token::Kind::kRParen
                      || t.kind == token::Kind::kRBrace
                      || t.kind == token::Kind::kRBracket) {
                    char want = (t.kind == token::Kind::kRParen) ? ')'
                              : (t.kind == token::Kind::kRBrace) ? '}'
                              :                                    ']';
                    if (brackets.empty()) {
                        int idx = addAndIndex(out, t);
                        reportAt(diag, file_id, idx, std::string("unmatched '") + want + "'");
                        fatal = true;
                        return false;
                    }
                    if (brackets.back() != want) {
                        int idx = addAndIndex(out, t);
                        reportAt(diag, file_id, idx,
                            std::string("mismatched bracket: expected '") + brackets.back()
                            + "', got '" + want + "'");
                        fatal = true;
                        return false;
                    }
                    brackets.pop_back();
                    depth--;
                }

                // Import detection at file scope.
                if (depth == 0 && t.kind == token::Kind::kImport) {
                    saved_import = t;
                    state = State::SawImport;
                    return true;
                }
                token::add(out, t);
                return true;
            }
            case State::SawImport: {
                if (t.kind == token::Kind::kIdentifier) {
                    saved_ident = t;
                    state = State::SawImportIdent;
                    return true;
                }
                // pushback
                state = State::Normal;
                token::add(out, saved_import);
                return handle(t);
            }
            case State::SawImportIdent: {
                if (t.kind == token::Kind::kSemicolon) {
                    state = State::Normal;
                    std::string module = saved_ident.text;
                    std::string header = module + ".slh";

                    std::vector<std::string> search;
                    search.push_back(source_dir.empty() ? std::string(".") : source_dir);
                    for (auto& p : import_paths) search.push_back(p);

                    std::string found_path;
                    for (auto& dir : search) {
                        std::filesystem::path candidate = std::filesystem::path(dir) / header;
                        std::error_code ec;
                        if (std::filesystem::exists(candidate, ec)) {
                            found_path = candidate.string();
                            break;
                        }
                    }
                    if (found_path.empty()) {
                        int idx = addAndIndex(out, saved_ident);
                        reportAt(diag, file_id, idx,
                            "cannot find '" + header + "' on the import path");
                        fatal = true;
                        return false;
                    }
                    if (!imported_once.insert(found_path).second) {
                        return true;   // already imported elsewhere
                    }

                    std::ifstream f(found_path);
                    if (!f.is_open()) {
                        int idx = addAndIndex(out, saved_ident);
                        reportAt(diag, file_id, idx,
                            "cannot open '" + found_path + "'");
                        fatal = true;
                        return false;
                    }
                    std::stringstream buf;
                    buf << f.rdbuf();
                    std::string imported_source = buf.str();

                    int new_file_id = token::openFile(out, found_path,
                        std::move(imported_source), file_id);
                    std::string new_source_dir =
                        std::filesystem::path(found_path).parent_path().string();

                    processFile(new_file_id, new_source_dir);
                    return !fatal;
                }
                // pushback
                state = State::Normal;
                token::add(out, saved_import);
                token::add(out, saved_ident);
                return handle(t);
            }
        }
        return true;
    };

    while (true) {
        token::Token t = lex::next(s);
        if (!handle(t)) break;
    }
}

}  // namespace

// ---------------- top-level entry ----------------

void run(std::string const& root_path,
         std::vector<std::string> const& import_paths,
         token::List& out, diagnostic::Sink& diag) {
    std::ifstream f(root_path);
    if (!f.is_open()) {
        diagnostic::report(diag, {-1, -1, "cannot open '" + root_path + "'", {}});
        return;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    std::string root_source = buf.str();

    int root_file_id = token::openFile(out, root_path, std::move(root_source), -1);
    std::string source_dir =
        std::filesystem::path(root_path).parent_path().string();

    std::set<std::string> imported_once;
    ImportWrapper wrap{out, diag, import_paths, imported_once};
    wrap.processFile(root_file_id, source_dir);

    if (!wrap.fatal) {
        token::Token eoi{token::Kind::kEndOfInput, "", -1, 0, 0, 0};
        token::add(out, eoi);
    }
}

}  // namespace lex
