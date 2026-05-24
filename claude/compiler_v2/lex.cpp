#include "lex.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

#include "diagnostic.h"
#include "token.h"

namespace lex {

namespace {

struct Lexer {
    int file_id;
    std::string const& source;
    token::List& out;
    diagnostic::Sink& diag;
    int pos = 0;
    int line = 1;
    int col = 1;
    bool fatal = false;

    Lexer(int f, std::string const& s, token::List& o, diagnostic::Sink& d)
        : file_id(f), source(s), out(o), diag(d) {}

    char peek() {
        if (pos >= (int)source.size()) return '\0';
        return source[pos];
    }
    char peek2() {
        if (pos + 1 >= (int)source.size()) return '\0';
        return source[pos + 1];
    }
    char advance() {
        char c = source[pos++];
        if (c == '\n') { line++; col = 1; }
        else { col++; }
        return c;
    }

    void error(int err_line, int err_col, int len, std::string const& msg) {
        token::Token errtok{token::Kind::kError, "", err_line, err_col, len};
        token::add(out, errtok);
        int tok_index = (int)out.tokens.size() - 1;
        diagnostic::report(diag, {file_id, tok_index, msg, {}});
        fatal = true;
    }

    void skipWhitespaceAndComments() {
        int depth = 0;
        bool in_line = false;
        while (pos < (int)source.size()) {
            char c = peek();
            char d = peek2();
            if (c == '/' && d == '/') {
                in_line = true;
                advance(); advance();
                continue;
            }
            if (c == '/' && d == '*') {
                depth++;
                advance(); advance();
                continue;
            }
            if (c == '*' && d == '/') {
                if (depth == 0) {
                    error(line, col, 2, "unmatched */");
                    return;
                }
                depth--;
                advance(); advance();
                continue;
            }
            if (in_line) {
                if (c == '\n') {
                    in_line = false;
                    advance();
                    continue;
                }
                if (c == '\\') {
                    int p = pos + 1;
                    while (p < (int)source.size()
                        && (source[p] == ' ' || source[p] == '\t'
                            || source[p] == '\r')) p++;
                    if (p < (int)source.size() && source[p] == '\n') {
                        if (p != pos + 1) {
                            error(line, col, 1,
                                "whitespace between line-continuation \\ and newline");
                            return;
                        }
                        advance(); advance();
                        continue;
                    }
                }
                advance();
                continue;
            }
            if (depth > 0) { advance(); continue; }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
                continue;
            }
            break;
        }
        if (depth > 0) error(line, col, 1, "unterminated block comment");
    }

    token::Token readCharLiteral() {
        advance(); // consume opening '
        int value = 0;
        if (peek() == '\\') {
            advance();
            char esc = advance();
            switch (esc) {
                case 'n':  value = '\n'; break;
                case 't':  value = '\t'; break;
                case '0':  value = '\0'; break;
                case '\\': value = '\\'; break;
                case '\'': value = '\''; break;
                default:   value = esc;  break;
            }
        } else {
            value = (unsigned char)advance();
        }
        if (peek() == '\'') advance();
        return {token::Kind::kCharLiteral, std::to_string(value), 0, 0, 0};
    }

    token::Token readString() {
        advance();
        std::string value;
        while (pos < (int)source.size() && peek() != '"') {
            char c = advance();
            if (c == '\\') {
                char esc = advance();
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
        if (peek() == '"') advance();
        return {token::Kind::kStringLiteral, value, 0, 0, 0};
    }

    token::Token readNumber() {
        std::string value;
        if (peek() == '0' && (peek2() == 'x' || peek2() == 'X')) {
            advance(); advance();
            std::string digits;
            while (pos < (int)source.size() && (isxdigit(peek()) || peek() == '_'))
                digits += advance();
            std::string clean;
            for (char c : digits) if (c != '_') clean += c;
            uint64_t uval = std::stoull(clean, nullptr, 16);
            return {token::Kind::kUintLiteral, std::to_string(uval), 0, 0, 0};
        }
        if (peek() == '0' && (peek2() == 'b' || peek2() == 'B')) {
            advance(); advance();
            std::string digits;
            while (pos < (int)source.size() && (peek() == '0' || peek() == '1' || peek() == '_'))
                digits += advance();
            std::string clean;
            for (char c : digits) if (c != '_') clean += c;
            uint64_t uval = std::stoull(clean, nullptr, 2);
            return {token::Kind::kUintLiteral, std::to_string(uval), 0, 0, 0};
        }
        while (pos < (int)source.size() && (isdigit(peek()) || peek() == '_'))
            value += advance();
        std::string clean;
        for (char c : value) if (c != '_') clean += c;
        bool is_float = false;
        if (peek() == '.' && peek2() != '.') {
            is_float = true;
            clean += advance();
            while (pos < (int)source.size() && (isdigit(peek()) || peek() == '_')) {
                char ch = advance();
                if (ch != '_') clean += ch;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            clean += advance();
            if (peek() == '+' || peek() == '-') clean += advance();
            if (!isdigit(peek())) {
                error(line, col, 1, "malformed exponent: expected a digit");
                return {token::Kind::kError, clean, 0, 0, 0};
            }
            while (pos < (int)source.size() && (isdigit(peek()) || peek() == '_')) {
                char ch = advance();
                if (ch != '_') clean += ch;
            }
        }
        return {is_float ? token::Kind::kFloatLiteral : token::Kind::kIntLiteral, clean, 0, 0, 0};
    }

    token::Token readIdentifierOrKeyword() {
        std::string value;
        while (pos < (int)source.size() && (isalnum(peek()) || peek() == '_'))
            value += advance();

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
            if (value == kw.name) return {kw.kind, value, 0, 0, 0};
        }
        return {token::Kind::kIdentifier, value, 0, 0, 0};
    }

    void tokenize() {
        int sl = 1, sc = 1, sp = 0;

        auto emit = [&](token::Kind k, std::string v) {
            token::Token t{k, std::move(v), sl, sc, pos - sp};
            token::add(out, t);
        };

        while (!fatal) {
            skipWhitespaceAndComments();
            if (fatal) break;
            sl = line; sc = col; sp = pos;
            if (pos >= (int)source.size()) {
                emit(token::Kind::kEof, "");
                break;
            }

            char c = peek();

            if (c == '"')                    { token::Token t = readString();             emit(t.kind, std::move(t.text)); }
            else if (c == '\'')              { token::Token t = readCharLiteral();        emit(t.kind, std::move(t.text)); }
            else if (isdigit(c))             { token::Token t = readNumber();             emit(t.kind, std::move(t.text)); }
            else if (isalpha(c) || c == '_') { token::Token t = readIdentifierOrKeyword(); emit(t.kind, std::move(t.text)); }
            else {
                advance();
                switch (c) {
                    case '(': emit(token::Kind::kLParen,    "("); break;
                    case ')': emit(token::Kind::kRParen,    ")"); break;
                    case '{': emit(token::Kind::kLBrace,    "{"); break;
                    case '}': emit(token::Kind::kRBrace,    "}"); break;
                    case ';': emit(token::Kind::kSemicolon, ";"); break;
                    case ',': emit(token::Kind::kComma,     ","); break;
                    case '+':
                        if (peek() == '+')      { advance(); emit(token::Kind::kPlusPlus, "++"); }
                        else if (peek() == '=') { advance(); emit(token::Kind::kPlusEq,   "+="); }
                        else                    {            emit(token::Kind::kPlus,     "+");  }
                        break;
                    case '-':
                        if (peek() == '-')      { advance(); emit(token::Kind::kMinusMinus, "--"); }
                        else if (peek() == '=') { advance(); emit(token::Kind::kMinusEq,    "-="); }
                        else                    {            emit(token::Kind::kMinus,      "-");  }
                        break;
                    case '*':
                        if (peek() == '=') { advance(); emit(token::Kind::kStarEq, "*="); }
                        else               {            emit(token::Kind::kStar,   "*");  }
                        break;
                    case '/':
                        if (peek() == '=') { advance(); emit(token::Kind::kSlashEq, "/="); }
                        else               {            emit(token::Kind::kSlash,   "/");  }
                        break;
                    case '%':
                        if (peek() == '=') { advance(); emit(token::Kind::kPercentEq, "%="); }
                        else               {            emit(token::Kind::kPercent,   "%");  }
                        break;
                    case '!':
                        if (peek() == '=') { advance(); emit(token::Kind::kNotEq, "!="); }
                        else               {            emit(token::Kind::kNot,   "!");  }
                        break;
                    case '=':
                        if (peek() == '=') { advance(); emit(token::Kind::kEqEq,   "=="); }
                        else               {            emit(token::Kind::kEquals, "=");  }
                        break;
                    case '<':
                        if (peek() == '<') {
                            advance();
                            if (peek() == '=') { advance(); emit(token::Kind::kLShiftEq, "<<="); }
                            else               {            emit(token::Kind::kLShift,   "<<");  }
                        } else if (peek() == '=') { advance(); emit(token::Kind::kLtEq, "<="); }
                        else if (peek() == '-' && peek2() == '-') {
                            advance(); advance();
                            if (peek() == '>') { advance(); emit(token::Kind::kArrowBoth, "<-->"); }
                            else               {            emit(token::Kind::kArrowLeft, "<--");  }
                        }
                        else                   {            emit(token::Kind::kLt, "<"); }
                        break;
                    case '>':
                        if (peek() == '>') {
                            advance();
                            if (peek() == '=') { advance(); emit(token::Kind::kRShiftEq, ">>="); }
                            else               {            emit(token::Kind::kRShift,   ">>");  }
                        } else if (peek() == '=') { advance(); emit(token::Kind::kGtEq, ">="); }
                        else                      {            emit(token::Kind::kGt,   ">");  }
                        break;
                    case '&':
                        if (peek() == '&') {
                            advance();
                            if (peek() == '=') { advance(); emit(token::Kind::kAndEq, "&&="); }
                            else               {            emit(token::Kind::kAnd,   "&&");  }
                        } else if (peek() == '=') { advance(); emit(token::Kind::kBitAndEq, "&="); }
                        else                      {            emit(token::Kind::kBitAnd,   "&");  }
                        break;
                    case '|':
                        if (peek() == '|') {
                            advance();
                            if (peek() == '=') { advance(); emit(token::Kind::kOrEq, "||="); }
                            else               {            emit(token::Kind::kOr,   "||");  }
                        } else if (peek() == '=') { advance(); emit(token::Kind::kBitOrEq, "|="); }
                        else                      {            emit(token::Kind::kBitOr,   "|");  }
                        break;
                    case '^': {
                        // first '^' already consumed; count remaining consecutive '^'s
                        int n = 1;
                        while (peek() == '^') { advance(); n++; }
                        if (n >= 3) {
                            for (int k = 0; k < n; k++) emit(token::Kind::kBitXor, "^");
                        } else if (n == 2) {
                            if (peek() == '=') { advance(); emit(token::Kind::kXorXorEq, "^^="); }
                            else               {            emit(token::Kind::kXorXor,   "^^");  }
                        } else {
                            if (peek() == '=') { advance(); emit(token::Kind::kBitXorEq, "^="); }
                            else               {            emit(token::Kind::kBitXor,   "^");  }
                        }
                        break;
                    }
                    case '~':
                        emit(token::Kind::kBitNot, "~");
                        break;
                    case '.':
                        if (peek() == '.' && pos + 1 < (int)source.size() && source[pos + 1] == '.') {
                            advance(); advance();
                            emit(token::Kind::kEllipsis, "...");
                        } else if (peek() == '.') {
                            advance();
                            emit(token::Kind::kDotDot, "..");
                        } else {
                            emit(token::Kind::kDot, ".");
                        }
                        break;
                    case ':':
                        if (peek() == ':') {
                            advance();
                            emit(token::Kind::kColonColon, "::");
                        } else {
                            emit(token::Kind::kColon, ":");
                        }
                        break;
                    case '[':
                        emit(token::Kind::kLBracket, "[");
                        break;
                    case ']':
                        emit(token::Kind::kRBracket, "]");
                        break;
                    case '#':
                        if (peek() == '#') {
                            advance();
                            emit(token::Kind::kHashHash, "##");
                        } else {
                            emit(token::Kind::kHash, "#");
                        }
                        break;
                    default:
                        emit(token::Kind::kUnknown, std::string(1, c));
                        break;
                }
            }
        }
    }
};

}  // namespace

void run(int file_id, std::string const& source, token::List& out, diagnostic::Sink& diag) {
    Lexer lexer{file_id, source, out, diag};
    lexer.tokenize();
}

}  // namespace lex
