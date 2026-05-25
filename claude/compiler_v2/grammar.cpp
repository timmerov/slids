#include "grammar.h"

#include <memory>
#include <string>
#include <utility>

#include "diagnostic.h"
#include "parse.h"
#include "token.h"

namespace grammar {

namespace {

struct Parser {
    token::List const& tokens;
    parse::Tree& out;
    diagnostic::Sink& diag;
    int pos = 0;
    bool fatal = false;

    token::Token const& peek() const { return tokens.tokens[pos]; }

    token::Kind peekKind(int ahead) const {
        int p = pos + ahead;
        if (p >= static_cast<int>(tokens.tokens.size())) return token::Kind::kEndOfInput;
        return tokens.tokens[p].kind;
    }

    void advance() {
        if (pos + 1 < static_cast<int>(tokens.tokens.size())) pos++;
    }

    void error(std::string const& msg) {
        if (fatal) return;
        fatal = true;
        token::Token const& t = peek();
        diagnostic::report(diag, {t.file_id, pos, msg, {}});
    }

    bool expect(token::Kind kind, char const* name) {
        if (peek().kind != kind) {
            error(std::string("expected '") + name + "'");
            return false;
        }
        advance();
        return true;
    }

    bool isTypeStart(token::Kind k) const {
        return k == token::Kind::kInt32 || k == token::Kind::kChar;
    }

    std::string parseType() {
        token::Token const& t = peek();
        std::string type;
        if (t.kind == token::Kind::kInt32)      type = "int32";
        else if (t.kind == token::Kind::kChar)  type = "char";
        else {
            error("expected type");
            return "";
        }
        advance();
        if (peek().kind == token::Kind::kLBracket) {
            advance();
            if (!expect(token::Kind::kRBracket, "]")) return "";
            type += "[]";
        }
        return type;
    }

    std::unique_ptr<parse::Node> parseExpr() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kStringLiteral) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kStringLiteral;
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kIntLiteral) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kIntLiteral;
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kCharLiteral) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kCharLiteral;
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kIdentifier) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kIdentExpr;
            node->name = t.text;
            advance();
            return node;
        }
        error("expected expression");
        return nullptr;
    }

    std::unique_ptr<parse::Node> parseVarDeclStmt() {
        std::string type = parseType();
        if (fatal) return nullptr;
        if (peek().kind != token::Kind::kIdentifier) {
            error("expected variable name");
            return nullptr;
        }
        std::string name = peek().text;
        advance();
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kVarDeclStmt;
        node->name = std::move(name);
        node->return_type = std::move(type);
        if (peek().kind == token::Kind::kEquals) {
            advance();
            auto init = parseExpr();
            if (!init) return nullptr;
            node->children.push_back(std::move(init));
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    std::unique_ptr<parse::Node> parseAssignStmt() {
        std::string name = peek().text;
        advance();   // ident
        advance();   // =  (caller verified via lookahead)
        auto expr = parseExpr();
        if (!expr) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kAssignStmt;
        node->name = std::move(name);
        node->children.push_back(std::move(expr));
        return node;
    }

    std::unique_ptr<parse::Node> parseCallStmt() {
        std::string callee = peek().text;
        advance();   // ident
        advance();   // (  (caller verified via lookahead)
        auto arg = parseExpr();
        if (!arg) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kCallStmt;
        node->name = std::move(callee);
        node->children.push_back(std::move(arg));
        return node;
    }

    std::unique_ptr<parse::Node> parseReturnStmt() {
        advance();   // return
        auto expr = parseExpr();
        if (!expr) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kReturnStmt;
        node->children.push_back(std::move(expr));
        return node;
    }

    std::unique_ptr<parse::Node> parseStmt() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kReturn) return parseReturnStmt();
        if (isTypeStart(t.kind)) return parseVarDeclStmt();
        if (t.kind == token::Kind::kIdentifier) {
            token::Kind next = peekKind(1);
            if (next == token::Kind::kEquals) return parseAssignStmt();
            if (next == token::Kind::kLParen) return parseCallStmt();
            error("expected '=' or '(' after identifier");
            return nullptr;
        }
        error("expected statement");
        return nullptr;
    }

    std::unique_ptr<parse::Node> parseFunctionDef() {
        std::string ret_type = parseType();
        if (fatal) return nullptr;
        if (peek().kind != token::Kind::kIdentifier) {
            error("expected function name");
            return nullptr;
        }
        std::string name = peek().text;
        advance();
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;

        auto node = std::make_unique<parse::Node>();
        node->name = std::move(name);
        node->return_type = std::move(ret_type);

        if (peek().kind == token::Kind::kSemicolon) {
            advance();
            node->kind = parse::Kind::kFunctionDecl;
            return node;
        }
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;
        node->kind = parse::Kind::kFunctionDef;

        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("expected '}'");
                return nullptr;
            }
            auto stmt = parseStmt();
            if (!stmt) return nullptr;
            node->children.push_back(std::move(stmt));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        return node;
    }

    void parseProgram() {
        auto prog = std::make_unique<parse::Node>();
        prog->kind = parse::Kind::kProgram;
        while (!fatal) {
            while (peek().kind == token::Kind::kEndOfFile) advance();
            if (peek().kind == token::Kind::kEndOfInput) break;
            auto fn = parseFunctionDef();
            if (!fn) return;
            prog->children.push_back(std::move(fn));
        }
        if (!fatal) out.nodes.push_back(std::move(prog));
    }
};

}  // namespace

void run(token::List const& in, parse::Tree& out, diagnostic::Sink& diag) {
    Parser p{in, out, diag};
    p.parseProgram();
}

}  // namespace grammar
