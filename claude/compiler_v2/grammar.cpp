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

    void advance() {
        if (pos + 1 < (int)tokens.tokens.size()) pos++;
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
        error("expected expression");
        return nullptr;
    }

    std::unique_ptr<parse::Node> parseStmt() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kReturn) {
            advance();
            auto expr = parseExpr();
            if (!expr) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kReturnStmt;
            node->children.push_back(std::move(expr));
            return node;
        }
        if (t.kind == token::Kind::kIdentifier) {
            std::string callee = t.text;
            advance();
            if (!expect(token::Kind::kLParen, "(")) return nullptr;
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
        error("expected statement");
        return nullptr;
    }

    std::unique_ptr<parse::Node> parseFunctionDef() {
        if (peek().kind != token::Kind::kInt32) {
            error("expected function return type");
            return nullptr;
        }
        std::string ret_type = peek().text;
        advance();
        if (peek().kind != token::Kind::kIdentifier) {
            error("expected function name");
            return nullptr;
        }
        std::string name = peek().text;
        advance();
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;

        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kFunctionDef;
        node->name = std::move(name);
        node->return_type = std::move(ret_type);

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
