#include "parser.h"
#include "lexer.h"
#include <stdexcept>
#include <functional>
#include <fstream>
#include <sstream>
#include <map>

Parser::Parser(std::vector<Token> tokens, std::string source_dir,
               std::vector<std::string> import_paths,
               std::shared_ptr<std::set<std::string>> imported_once)
    : tokens_(std::move(tokens)), pos_(0), source_dir_(std::move(source_dir)),
      import_paths_(std::move(import_paths)),
      imported_once_(imported_once ? std::move(imported_once)
                                   : std::make_shared<std::set<std::string>>()) {}

void Parser::declareVar(const std::string& name) {
    if (!scope_stack_.empty())
        scope_stack_.back().insert(name);
}

bool Parser::isInScope(const std::string& name) const {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it)
        if (it->count(name)) return true;
    return false;
}

Token& Parser::peek() { return tokens_[pos_]; }

Token& Parser::advance() {
    Token& t = tokens_[pos_];
    if (t.type != TokenType::kEof) pos_++;
    return t;
}

Token& Parser::expect(TokenType type, const std::string& msg) {
    if (peek().type != type)
        throw std::runtime_error("Line " + std::to_string(peek().line)
            + ": " + msg + ", got '" + peek().value + "'");
    return advance();
}

bool Parser::isTypeName(const Token& t) const {
    switch (t.type) {
        case TokenType::kInt: case TokenType::kInt8: case TokenType::kInt16:
        case TokenType::kInt32: case TokenType::kInt64:
        case TokenType::kUint: case TokenType::kUint8: case TokenType::kUint16:
        case TokenType::kUint32: case TokenType::kUint64: case TokenType::kChar:
        case TokenType::kIntptr:
        case TokenType::kFloat32: case TokenType::kFloat64:
        case TokenType::kBool: case TokenType::kVoid:
            return true;
        default: return false;
    }
}

bool Parser::isUserTypeName(const Token& t) const {
    return t.type == TokenType::kIdentifier;
}

bool Parser::isVarDeclLookahead() const {
    // pos_ is at an identifier that might be a type name.
    // scan past it (plus optional :Qualifier, template args and pointer/iterator suffix)
    // and check that an identifier (the variable name) follows.
    int i = pos_ + 1; // skip base type name
    // optional qualified-type suffix: : Identifier (e.g. Outer:Inner)
    if (i + 1 < (int)tokens_.size()
        && tokens_[i].type == TokenType::kColon
        && tokens_[i + 1].type == TokenType::kIdentifier) {
        i += 2;
    }
    // optional template args: <Types>
    if (i < (int)tokens_.size() && tokens_[i].type == TokenType::kLt) {
        i++;
        while (i < (int)tokens_.size() && tokens_[i].type != TokenType::kGt
               && tokens_[i].type != TokenType::kEof) i++;
        if (i >= (int)tokens_.size() || tokens_[i].type != TokenType::kGt) return false;
        i++;
    }
    // optional ^ or []
    if (i < (int)tokens_.size() && tokens_[i].type == TokenType::kBitXor) i++;
    else if (i + 1 < (int)tokens_.size()
             && tokens_[i].type == TokenType::kLBracket
             && tokens_[i+1].type == TokenType::kRBracket) i += 2;
    return i < (int)tokens_.size() && tokens_[i].type == TokenType::kIdentifier;
}

bool Parser::isTemplateCallLookahead() const {
    // pos_ points at '<'; scan forward to see if this is name<Type,...>(
    // Valid: tokens inside <> are type keywords, uppercase idents, ^, [], commas
    int i = pos_ + 1;
    while (i < (int)tokens_.size()) {
        const Token& ti = tokens_[i];
        if (ti.type == TokenType::kGt) {
            // found closing >; next must be (
            return (i + 1 < (int)tokens_.size())
                && tokens_[i + 1].type == TokenType::kLParen;
        }
        // allow type-name tokens and commas inside the angle brackets
        bool ok = isTypeName(ti)
               || isUserTypeName(ti)
               || ti.type == TokenType::kComma
               || ti.type == TokenType::kBitXor
               || ti.type == TokenType::kLBracket
               || ti.type == TokenType::kRBracket;
        if (!ok) return false;
        i++;
    }
    return false;
}

bool Parser::isInstantiationLookahead() const {
    // pos_ is at identifier; pos_+1 is '<'; scan type args and require '>' followed by
    // '(...)' (param signature) followed by ';'. Distinguishes from a class-template
    // definition `Name<T>(fields) { ... }` which has '{' after the parens, not ';'.
    int i = pos_ + 2;
    while (i < (int)tokens_.size()) {
        const Token& ti = tokens_[i];
        if (ti.type == TokenType::kGt) break;
        bool ok = isTypeName(ti) || isUserTypeName(ti)
               || ti.type == TokenType::kComma
               || ti.type == TokenType::kBitXor
               || ti.type == TokenType::kLBracket
               || ti.type == TokenType::kRBracket;
        if (!ok) return false;
        i++;
    }
    if (i >= (int)tokens_.size() || tokens_[i].type != TokenType::kGt) return false;
    i++; // past '>'
    if (i >= (int)tokens_.size() || tokens_[i].type != TokenType::kLParen) return false;
    int depth = 1;
    i++;
    while (i < (int)tokens_.size() && depth > 0) {
        if (tokens_[i].type == TokenType::kLParen) depth++;
        else if (tokens_[i].type == TokenType::kRParen) depth--;
        i++;
    }
    if (depth != 0) return false;
    return i < (int)tokens_.size() && tokens_[i].type == TokenType::kSemicolon;
}

bool Parser::isTemplateTypeArgLookahead() const {
    // pos_ is at '<'; scan forward to see if this is a template type-arg list.
    // Valid contents: type keywords, uppercase idents, ^, [], commas.
    int i = pos_ + 1;
    while (i < (int)tokens_.size()) {
        const Token& ti = tokens_[i];
        if (ti.type == TokenType::kGt) return true; // found closing '>'
        bool ok = isTypeName(ti) || isUserTypeName(ti)
               || ti.type == TokenType::kComma
               || ti.type == TokenType::kBitXor
               || ti.type == TokenType::kLBracket
               || ti.type == TokenType::kRBracket;
        if (!ok) return false;
        i++;
    }
    return false;
}

std::string Parser::parseTypeName() {
    std::string base;
    // anon-tuple type: (t1, t2, ...) — may carry trailing ^ or []
    if (peek().type == TokenType::kLParen) {
        advance(); // consume '('
        base = "(";
        bool first = true;
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            if (!first) base += ",";
            first = false;
            base += parseTypeName();
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')' in anon-tuple type");
        base += ")";
        // fall through to the trailing ^/[] loop below
        while (true) {
            if (peek().type == TokenType::kBitXor) { advance(); base += "^"; }
            else if (peek().type == TokenType::kLBracket
                       && pos_ + 1 < (int)tokens_.size()
                       && tokens_[pos_ + 1].type == TokenType::kRBracket) {
                advance(); advance();
                base += "[]";
            }
            else break;
        }
        return base;
    }
    if (isTypeName(peek())) base = advance().value;
    else if (isUserTypeName(peek())) base = advance().value;
    else throw std::runtime_error("Line " + std::to_string(peek().line)
        + ": expected type name, got '" + peek().value + "'");
    // qualified nested-type suffix: Outer:Inner — consume only when the next-next token is
    // an identifier (the variable name), not a '(' (which would be an external-method def).
    if (peek().type == TokenType::kColon
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kIdentifier
        && pos_ + 2 < (int)tokens_.size()
        && tokens_[pos_ + 2].type != TokenType::kLParen) {
        advance(); // consume ':'
        std::string inner = advance().value;
        base += "." + inner;
    } else {
        // unqualified — apply nested-alias map (e.g. "Inner" → "Outer.Inner" inside Outer's body)
        auto it = nested_alias_.find(base);
        if (it != nested_alias_.end()) base = it->second;
    }
    // template type instantiation in type position: Name<Type> → mangled as Name__Type
    if (peek().type == TokenType::kLt && isTemplateTypeArgLookahead()) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            std::string ta = parseTypeName();
            base += "__" + ta;
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "expected '>' after template type args");
    }
    // consume trailing ^ or [] for pointer/reference types — kept distinct:
    //   ^ = reference (no arithmetic allowed)
    //   [] = pointer  (arithmetic allowed: ++, --, +, -)
    while (true) {
        if (peek().type == TokenType::kBitXor) {
            advance();
            base += "^";
        } else if (peek().type == TokenType::kLBracket
                   && pos_ + 1 < (int)tokens_.size()
                   && tokens_[pos_ + 1].type == TokenType::kRBracket) {
            advance(); advance(); // consume [ ]
            base += "[]";
        } else {
            break;
        }
    }
    return base;
}

// --- Expression parsing ---

std::unique_ptr<Expr> Parser::parsePrimary() {
    Token t = peek();
    if (t.type == TokenType::kIntLiteral) {
        advance();
        return std::make_unique<IntLiteralExpr>(std::stoll(t.value));
    }
    if (t.type == TokenType::kUintLiteral) {
        advance();
        uint64_t uval = std::stoull(t.value);
        return std::make_unique<IntLiteralExpr>(static_cast<int64_t>(uval), false, true);
    }
    if (t.type == TokenType::kCharLiteral) {
        advance();
        return std::make_unique<IntLiteralExpr>(static_cast<int>(std::stoll(t.value)), /*is_char=*/true);
    }
    if (t.type == TokenType::kFloatLiteral) {
        advance();
        return std::make_unique<FloatLiteralExpr>(std::stod(t.value));
    }
    // type conversion: primitive_type(expr) — legacy form
    if (isTypeName(t) && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kLParen) {
        std::string type_name = advance().value; // consume type keyword
        advance(); // consume '('
        auto operand = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        return std::make_unique<TypeConvExpr>(type_name, std::move(operand));
    }
    // chainable type conversion without outer parens: int=expr, float32=expr, etc.
    // type keywords can't be variable names, so this is unambiguous in expression position
    if (isTypeName(t) && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kEquals) {
        std::string type_name = advance().value; // consume type keyword
        advance(); // consume '='
        auto operand = parseExpr();
        return std::make_unique<TypeConvExpr>(type_name, std::move(operand));
    }
    if (t.type == TokenType::kStringLiteral) {
        advance();
        std::string value = t.value;
        while (peek().type == TokenType::kStringLiteral)
            value += advance().value;
        return std::make_unique<StringLiteralExpr>(value);
    }
    if (t.type == TokenType::kTrue)  { advance(); return std::make_unique<IntLiteralExpr>(1); }
    if (t.type == TokenType::kFalse) { advance(); return std::make_unique<IntLiteralExpr>(0); }
    if (t.type == TokenType::kNullptr) { advance(); return std::make_unique<NullptrExpr>(); }
    if (t.type == TokenType::kNew) {
        advance();
        // placement new: new(addr) Type(args)
        if (peek().type == TokenType::kLParen) {
            advance();
            auto addr = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            std::string elem_type = parseTypeName();
            std::vector<std::unique_ptr<Expr>> args;
            if (peek().type == TokenType::kLParen) {
                advance();
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "expected ')'");
            }
            return std::make_unique<PlacementNewExpr>(std::move(addr), elem_type, std::move(args));
        }
        std::string elem_type = parseTypeName();
        if (peek().type == TokenType::kLBracket) {
            advance();
            auto count = parseExpr();
            expect(TokenType::kRBracket, "expected ']'");
            return std::make_unique<NewExpr>(elem_type, std::move(count));
        } else if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            return std::make_unique<NewScalarExpr>(elem_type, std::move(args));
        } else {
            // new Type; — equivalent to new Type()
            return std::make_unique<NewScalarExpr>(elem_type, std::vector<std::unique_ptr<Expr>>{});
        }
    }
    if (t.type == TokenType::kSizeof) {
        advance();
        expect(TokenType::kLParen, "expected '(' after sizeof");
        auto se = std::make_unique<SizeofExpr>();
        // Built-in type keywords can't be parsed as expressions, so route them
        // through parseTypeName() and carry the result as a VarExpr name.
        // All other forms (user identifiers, variables) parse as expressions
        // and are disambiguated in codegen against the symbol table.
        if (isTypeName(peek())) {
            se->operand = std::make_unique<VarExpr>(parseTypeName());
        } else {
            se->operand = parseExpr();
        }
        expect(TokenType::kRParen, "expected ')'");
        return se;
    }
    if (t.type == TokenType::kColonColon) {
        advance(); // consume '::'
        std::string fn = expect(TokenType::kIdentifier, "expected name after '::'").value;
        expect(TokenType::kLParen, "expected '(' after '::name'");
        std::vector<std::unique_ptr<Expr>> args;
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            args.push_back(parseExpr());
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
        auto call = std::make_unique<CallExpr>(fn, std::move(args));
        call->qualifier = "::";
        return call;
    }
    if (t.type == TokenType::kIdentifier) {
        advance();
        // qualified call: Name:method(args) — namespace function call.
        // Suppressed in contexts where ':' terminates the expression (e.g. case labels).
        if (colon_terminates_expr_ == 0
            && peek().type == TokenType::kColon
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIdentifier
            && pos_ + 2 < (int)tokens_.size()
            && tokens_[pos_ + 2].type == TokenType::kLParen) {
            advance(); // consume ':'
            std::string method = advance().value; // consume method identifier
            advance(); // consume '('
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            auto call = std::make_unique<CallExpr>(method, std::move(args));
            call->qualifier = t.value;
            return call;
        }
        // template call: name<TypeArg,...>(args)
        if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
            advance(); // consume '<'
            std::vector<std::string> type_args;
            while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
                type_args.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kGt, "expected '>'");
            expect(TokenType::kLParen, "expected '('");
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            auto call = std::make_unique<CallExpr>(t.value, std::move(args));
            call->type_args = std::move(type_args);
            return call;
        }
        if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            return std::make_unique<CallExpr>(t.value, std::move(args));
        }
        return std::make_unique<VarExpr>(t.value);
    }
    // type conversion expression: (Type=expr)
    // distinguished from grouped expression by TypeName immediately followed by =
    if (t.type == TokenType::kLParen
        && pos_ + 1 < (int)tokens_.size()
        && (isTypeName(tokens_[pos_ + 1]) || isUserTypeName(tokens_[pos_ + 1]))) {
        int saved = pos_;
        advance(); // consume '('
        std::string type_name = parseTypeName();
        if (peek().type == TokenType::kEquals) {
            advance(); // consume '='
            auto operand = parseExpr();
            expect(TokenType::kRParen, "expected ')' after type conversion");
            return std::make_unique<TypeConvExpr>(type_name, std::move(operand));
        }
        pos_ = saved; // not a type conversion — backtrack and fall through
    }
    if (t.type == TokenType::kLParen) {
        advance();
        auto first = parseExpr();
        if (peek().type == TokenType::kComma) {
            auto tuple = std::make_unique<TupleExpr>();
            tuple->values.push_back(std::move(first));
            while (peek().type == TokenType::kComma) {
                advance();
                tuple->values.push_back(parseExpr());
            }
            expect(TokenType::kRParen, "expected ')'");
            return tuple;
        }
        expect(TokenType::kRParen, "expected ')'");
        return first;
    }
    if (t.type == TokenType::kHashHash) {
        int src_line = t.line;
        advance();
        std::string kw = expect(TokenType::kIdentifier,
            "expected name, type, line, file, func, date, or time after ##").value;
        if (kw == "name" || kw == "type") {
            expect(TokenType::kLParen, "expected '(' after ##" + kw);
            auto operand = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            return std::make_unique<StringifyExpr>(kw, std::move(operand), src_line);
        }
        if (kw == "line" || kw == "file" || kw == "func" || kw == "date" || kw == "time")
            return std::make_unique<StringifyExpr>(kw, nullptr, src_line);
        throw std::runtime_error("Line " + std::to_string(src_line)
            + ": unknown ## operator '" + kw + "'");
    }
    throw std::runtime_error("Line " + std::to_string(t.line)
        + ": expected expression, got '" + t.value + "'");
}

// postfix: handle .field and .method(args) chaining
std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> base) {
    while (true) {
        if (peek().type == TokenType::kDot) {
            advance();
            // allow sizeof and ~ as method names
            std::string member;
            if (peek().type == TokenType::kSizeof)  { advance(); member = "sizeof"; }
            else if (peek().type == TokenType::kBitNot) { advance(); member = "~"; }
            else member = expect(TokenType::kIdentifier, "expected field or method name").value;
            if (peek().type == TokenType::kLParen) {
                advance();
                std::vector<std::unique_ptr<Expr>> args;
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "expected ')'");
                base = std::make_unique<MethodCallExpr>(std::move(base), member, std::move(args));
            } else {
                base = std::make_unique<FieldAccessExpr>(std::move(base), member);
            }
        } else if (peek().type == TokenType::kLBracket) {
            advance();
            auto idx = parseExpr();
            if (peek().type == TokenType::kDotDot) {
                advance();
                auto end_expr = parseExpr();
                expect(TokenType::kRBracket, "expected ']'");
                base = std::make_unique<SliceExpr>(std::move(base), std::move(idx), std::move(end_expr));
            } else {
                expect(TokenType::kRBracket, "expected ']'");
                base = std::make_unique<ArrayIndexExpr>(std::move(base), std::move(idx));
            }
        } else if (peek().type == TokenType::kBitXor) {
            // postfix ^ is dereference only when NOT followed by an expression operand
            // (if followed by identifier/literal/lparen it's binary XOR, handled by parseBitXor)
            int next = pos_ + 1;
            TokenType ntt = (next < (int)tokens_.size()) ? tokens_[next].type : TokenType::kEof;
            bool next_is_operand =
                ntt == TokenType::kIdentifier || ntt == TokenType::kIntLiteral ||
                ntt == TokenType::kStringLiteral || ntt == TokenType::kLParen ||
                ntt == TokenType::kTrue || ntt == TokenType::kFalse;
            if (next_is_operand) break; // leave ^ for parseBitXor
            advance();
            base = std::make_unique<DerefExpr>(std::move(base));
        } else if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
            std::string op = (peek().type == TokenType::kPlusPlus) ? "++" : "--";
            advance();
            // ptr++^ — post-inc/dec then deref: treat as PostIncDerefExpr
            if (peek().type == TokenType::kBitXor) {
                advance();
                base = std::make_unique<PostIncDerefExpr>(std::move(base), op);
            } else {
                std::string uop = (op == "++") ? "post++" : "post--";
                base = std::make_unique<UnaryExpr>(uop, std::move(base));
            }
        } else {
            break;
        }
    }
    return base;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    // #x — desugar to (##type(x), ##name(x), ^x)
    if (peek().type == TokenType::kHash) {
        int src_line = peek().line;
        advance();
        auto operand = parsePostfix(parsePrimary());
        auto* ve = dynamic_cast<VarExpr*>(operand.get());
        if (!ve)
            throw std::runtime_error("Line " + std::to_string(src_line)
                + ": # requires a simple variable name");
        std::string name = ve->name;
        auto tuple = std::make_unique<TupleExpr>();
        tuple->values.push_back(
            std::make_unique<StringifyExpr>("type", std::make_unique<VarExpr>(name), src_line));
        tuple->values.push_back(
            std::make_unique<StringifyExpr>("name", std::make_unique<VarExpr>(name), src_line));
        tuple->values.push_back(std::make_unique<AddrOfExpr>(std::move(operand)));
        return tuple;
    }
    // pointer reinterpret cast: <Type^> expr  or  <Type[]> expr  or  <intptr> expr
    if (peek().type == TokenType::kLt) {
        int saved = pos_;
        advance(); // tentatively consume <
        if (isTypeName(peek()) || isUserTypeName(peek())) {
            try {
                std::string target = parseTypeName(); // consumes type + optional ^/[]
                if (peek().type == TokenType::kGt) {
                    advance(); // consume >
                    auto operand = parseUnary();
                    return std::make_unique<PtrCastExpr>(target, std::move(operand));
                }
            } catch (...) {}
        }
        pos_ = saved; // not a cast — restore for comparison operator
    }
    if (peek().type == TokenType::kPlus) {
        advance();
        return parsePrimary();
    }
    if (peek().type == TokenType::kMinus) {
        advance();
        return std::make_unique<BinaryExpr>("-",
            std::make_unique<IntLiteralExpr>(0), parsePrimary());
    }
    if (peek().type == TokenType::kNot) {
        advance();
        return std::make_unique<UnaryExpr>("!", parseUnary());
    }
    if (peek().type == TokenType::kBitNot) {
        advance();
        return std::make_unique<UnaryExpr>("~", parseUnary());
    }
    // pre-increment/decrement: ++x, --x, ++(ptr^) — returns new value
    if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
        std::string op = (peek().type == TokenType::kPlusPlus) ? "pre++" : "pre--";
        advance();
        auto operand = parsePostfix(parsePrimary());  // handles ++(ref^), ++arr[i], etc.
        return std::make_unique<UnaryExpr>(op, std::move(operand));
    }
    // prefix ^ — take address: ^x, ^arr[i][j]
    if (peek().type == TokenType::kBitXor) {
        advance();
        auto operand = parsePostfix(parsePrimary());
        return std::make_unique<AddrOfExpr>(std::move(operand));
    }
    return parsePostfix(parsePrimary());
}

std::unique_ptr<Expr> Parser::parseMulDiv() {
    auto left = parseUnary();
    while (peek().type == TokenType::kStar  ||
           peek().type == TokenType::kSlash  ||
           peek().type == TokenType::kPercent) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseUnary());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAddSub() {
    auto left = parseMulDiv();
    while (peek().type == TokenType::kPlus ||
           peek().type == TokenType::kMinus) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseMulDiv());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseShift() {
    auto left = parseAddSub();
    while (peek().type == TokenType::kLShift ||
           peek().type == TokenType::kRShift) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseAddSub());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseRelational() {
    auto left = parseShift();
    while (peek().type == TokenType::kLt   ||
           peek().type == TokenType::kGt   ||
           peek().type == TokenType::kLtEq ||
           peek().type == TokenType::kGtEq) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseShift());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto left = parseRelational();
    while (peek().type == TokenType::kEqEq ||
           peek().type == TokenType::kNotEq) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseRelational());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitAnd() {
    auto left = parseEquality();
    while (peek().type == TokenType::kBitAnd) {
        advance();
        left = std::make_unique<BinaryExpr>("&", std::move(left), parseEquality());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitXor() {
    auto left = parseBitAnd();
    while (peek().type == TokenType::kBitXor) {
        advance();
        left = std::make_unique<BinaryExpr>("^", std::move(left), parseBitAnd());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitOr() {
    auto left = parseBitXor();
    while (peek().type == TokenType::kBitOr) {
        advance();
        left = std::make_unique<BinaryExpr>("|", std::move(left), parseBitXor());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto left = parseBitOr();
    while (peek().type == TokenType::kAnd) {
        advance();
        left = std::make_unique<BinaryExpr>("&&", std::move(left), parseBitOr());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseExpr() {
    auto left = parseLogicalAnd();
    while (peek().type == TokenType::kOr ||
           peek().type == TokenType::kXorXor) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseLogicalAnd());
    }
    return left;
}

// --- Statement parsing ---

std::unique_ptr<BlockStmt> Parser::parseBlock(std::vector<std::string> predeclare) {
    expect(TokenType::kLBrace, "expected '{'");
    scope_stack_.push_back({});
    for (auto& n : predeclare)
        scope_stack_.back().insert(n);
    auto block = std::make_unique<BlockStmt>();
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof)
        block->stmts.push_back(parseStmt());
    scope_stack_.pop_back();
    expect(TokenType::kRBrace, "expected '}'");
    return block;
}

// Normalize DerefExpr(UnaryExpr("post++"|"post--", VarExpr)) to PostIncDerefExpr,
// so that paren-wrapped post-inc-deref `(p++)^` produces the same AST as the
// unparenthesized `p++^`. Caller passes ownership; receives ownership back
// (possibly the same object if no rewrite was needed).
static std::unique_ptr<Expr> normalizePostIncDeref(std::unique_ptr<Expr> e) {
    auto* de = dynamic_cast<DerefExpr*>(e.get());
    if (!de) return e;
    auto* ue = dynamic_cast<UnaryExpr*>(de->operand.get());
    if (!ue || (ue->op != "post++" && ue->op != "post--")) return e;
    auto* ve = dynamic_cast<VarExpr*>(ue->operand.get());
    if (!ve) return e;
    std::string op = (ue->op == "post++") ? "++" : "--";
    return std::make_unique<PostIncDerefExpr>(
        std::make_unique<VarExpr>(ve->name), op);
}

std::unique_ptr<Stmt> Parser::buildAssignFromLhs(
        std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs, bool is_move) {
    lhs = normalizePostIncDeref(std::move(lhs));

    if (auto* ve = dynamic_cast<VarExpr*>(lhs.get())) {
        std::string name = ve->name;
        if (!isInScope(name) && !current_slid_fields_.count(name) && name != "self") {
            declareVar(name);
            return std::make_unique<VarDeclStmt>("", name, std::move(rhs),
                std::vector<std::unique_ptr<Expr>>{}, is_move);
        }
        return std::make_unique<AssignStmt>(name, std::move(rhs), is_move);
    }
    if (auto* pide = dynamic_cast<PostIncDerefExpr*>(lhs.get())) {
        if (is_move)
            throw std::runtime_error("move (<-) through post-inc-deref not supported");
        return std::make_unique<PostIncDerefAssignStmt>(
            std::move(pide->operand), pide->op, std::move(rhs));
    }
    if (auto* de = dynamic_cast<DerefExpr*>(lhs.get())) {
        if (is_move)
            throw std::runtime_error("move (<-) through deref not supported");
        return std::make_unique<DerefAssignStmt>(std::move(de->operand), std::move(rhs));
    }
    if (auto* fa = dynamic_cast<FieldAccessExpr*>(lhs.get())) {
        return std::make_unique<FieldAssignStmt>(
            std::move(fa->object), fa->field, std::move(rhs), is_move);
    }
    if (auto* ai = dynamic_cast<ArrayIndexExpr*>(lhs.get())) {
        return std::make_unique<IndexAssignStmt>(
            std::move(ai->base), std::move(ai->index), std::move(rhs), is_move);
    }
    throw std::runtime_error("invalid assignment target");
}

std::unique_ptr<Stmt> Parser::buildSwapFromLhs(
        std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs) {
    lhs = normalizePostIncDeref(std::move(lhs));
    rhs = normalizePostIncDeref(std::move(rhs));
    return std::make_unique<SwapStmt>(std::move(lhs), std::move(rhs));
}

std::unique_ptr<Stmt> Parser::buildCompoundAssignFromLhs(
        std::unique_ptr<Expr> lhs, const std::string& op,
        std::unique_ptr<Expr> rhs) {
    lhs = normalizePostIncDeref(std::move(lhs));
    if (!dynamic_cast<VarExpr*>(lhs.get())
        && !dynamic_cast<DerefExpr*>(lhs.get())
        && !dynamic_cast<FieldAccessExpr*>(lhs.get())
        && !dynamic_cast<ArrayIndexExpr*>(lhs.get())
        && !dynamic_cast<PostIncDerefExpr*>(lhs.get())) {
        throw std::runtime_error("compound assignment requires an lvalue");
    }
    return std::make_unique<CompoundAssignStmt>(
        std::move(lhs), op, std::move(rhs));
}

std::unique_ptr<Stmt> Parser::parseLvalueTail(std::unique_ptr<Expr> lhs) {
    static const std::map<TokenType, std::string> compound_ops = {
        {TokenType::kPlusEq,    "+"},
        {TokenType::kMinusEq,   "-"},
        {TokenType::kStarEq,    "*"},
        {TokenType::kSlashEq,   "/"},
        {TokenType::kPercentEq, "%"},
        {TokenType::kBitAndEq,  "&"},
        {TokenType::kBitOrEq,   "|"},
        {TokenType::kBitXorEq,  "^"},
        {TokenType::kLShiftEq,  "<<"},
        {TokenType::kRShiftEq,  ">>"},
        {TokenType::kAndEq,     "&&"},
        {TokenType::kOrEq,      "||"},
        {TokenType::kXorXorEq,  "^^"},
    };
    auto cop = compound_ops.find(peek().type);
    if (cop != compound_ops.end()) {
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildCompoundAssignFromLhs(std::move(lhs), cop->second, std::move(rhs));
    }
    if (peek().type == TokenType::kEquals) {
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildAssignFromLhs(std::move(lhs), std::move(rhs), false);
    }
    if (peek().type == TokenType::kArrowLeft) {
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildAssignFromLhs(std::move(lhs), std::move(rhs), true);
    }
    if (peek().type == TokenType::kArrowBoth) {
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildSwapFromLhs(std::move(lhs), std::move(rhs));
    }
    expect(TokenType::kSemicolon, "expected ';'");
    if (auto* mc = dynamic_cast<MethodCallExpr*>(lhs.get())) {
        return std::make_unique<MethodCallStmt>(
            std::move(mc->object), mc->method, std::move(mc->args));
    }
    return std::make_unique<ExprStmt>(std::move(lhs));
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    Token t = peek();

    // global-qualified call statement: ::name(args);
    if (t.type == TokenType::kColonColon) {
        auto call = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<ExprStmt>(std::move(call));
    }

    if (t.type == TokenType::kDelete) {
        advance();
        auto operand = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<DeleteStmt>(std::move(operand));
    }

    if (t.type == TokenType::kReturn) {
        advance();
        if (peek().type == TokenType::kSemicolon) {
            advance();
            return std::make_unique<ReturnStmt>(nullptr);
        }
        auto expr = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<ReturnStmt>(std::move(expr));
    }

    if (t.type == TokenType::kBreak) {
        advance();
        auto stmt = std::make_unique<BreakStmt>();
        if (peek().type == TokenType::kIntLiteral) {
            stmt->number = std::stoi(advance().value);
        } else if (peek().type == TokenType::kIdentifier
                || peek().type == TokenType::kFor
                || peek().type == TokenType::kWhile
                || peek().type == TokenType::kSwitch) {
            stmt->label = advance().value;
        }
        expect(TokenType::kSemicolon, "expected ';'");
        return stmt;
    }

    if (t.type == TokenType::kContinue) {
        advance();
        auto stmt = std::make_unique<ContinueStmt>();
        if (peek().type == TokenType::kIntLiteral) {
            stmt->number = std::stoi(advance().value);
        } else if (peek().type == TokenType::kIdentifier
                || peek().type == TokenType::kFor
                || peek().type == TokenType::kWhile
                || peek().type == TokenType::kSwitch) {
            stmt->label = advance().value;
        }
        expect(TokenType::kSemicolon, "expected ';'");
        return stmt;
    }

    // pre-increment/decrement statement: ++x;  ++ref^;  ++(ref^);
    if (t.type == TokenType::kPlusPlus || t.type == TokenType::kMinusMinus) {
        std::string op = (t.type == TokenType::kPlusPlus) ? "pre++" : "pre--";
        advance();
        auto operand = parsePostfix(parsePrimary());
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<ExprStmt>(
            std::make_unique<UnaryExpr>(op, std::move(operand)));
    }

    // naked block: { stmt; stmt; ... }
    if (t.type == TokenType::kLBrace) {
        return parseBlock();
    }

    if (t.type == TokenType::kIf) {
        advance();
        auto stmt = std::make_unique<IfStmt>();
        expect(TokenType::kLParen, "expected '('");
        stmt->cond = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        stmt->then_block = parseBlock();
        if (peek().type == TokenType::kElse) {
            advance();
            if (peek().type == TokenType::kIf) {
                auto else_block = std::make_unique<BlockStmt>();
                else_block->stmts.push_back(parseStmt());
                stmt->else_block = std::move(else_block);
            } else {
                stmt->else_block = parseBlock();
            }
        }
        return stmt;
    }

    if (t.type == TokenType::kWhile) {
        advance();
        auto stmt = std::make_unique<WhileStmt>();
        if (peek().type == TokenType::kLBrace) {
            // bottom-condition while: while { body } :label (cond);
            stmt->bottom_condition = true;
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
            } else {
                stmt->block_label = "while";
            }
            expect(TokenType::kLParen, "expected '('");
            stmt->cond = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
        } else {
            expect(TokenType::kLParen, "expected '('");
            if (peek().type == TokenType::kRParen)
                stmt->cond = std::make_unique<IntLiteralExpr>(1);  // while () == while (true)
            else
                stmt->cond = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
                expect(TokenType::kSemicolon, "expected ';'");
            } else {
                stmt->block_label = "while";
            }
        }
        return stmt;
    }

    if (t.type == TokenType::kSwitch) {
        return parseSwitchStmt();
    }

    if (t.type == TokenType::kFor) {
        advance();
        // for EnumType var in EnumType { ... }  — enum iteration
        // detected by: user type name, identifier, 'in', user type name (no '(')
        if (isUserTypeName(peek())) {
            int saved = pos_;
            std::string var_type = advance().value;   // e.g. "Piece"
            if (peek().type == TokenType::kIdentifier) {
                std::string var_name = advance().value;
                if (peek().type == TokenType::kIdentifier && peek().value == "in") {
                    advance(); // consume 'in'
                    if (isUserTypeName(peek())) {
                        auto stmt = std::make_unique<ForEnumStmt>();
                        stmt->var_type = var_type;
                        stmt->var_name = var_name;
                        stmt->enum_name = advance().value;
                        stmt->body = parseBlock();
                        if (peek().type == TokenType::kColon) {
                            advance();
                            stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
                            expect(TokenType::kSemicolon, "expected ';'");
                        } else {
                            stmt->block_label = "for";
                        }
                        return stmt;
                    }
                }
            }
            // not an enum-for — backtrack and fall through to range for
            pos_ = saved;
        }
        // type is optional — "for int i in" vs "for i in" (reuse existing var)
        std::string for_var_type, for_var_name;
        if (isTypeName(peek())) {
            for_var_type = parseTypeName();
            for_var_name = expect(TokenType::kIdentifier, "expected variable name").value;
        } else if (peek().type == TokenType::kIdentifier) {
            for_var_type = ""; // reuse existing variable
            for_var_name = advance().value;
        } else {
            throw std::runtime_error("Line " + std::to_string(peek().line)
                + ": expected variable name in for loop");
        }
        {
            Token in_tok = peek();
            if (in_tok.type != TokenType::kIdentifier || in_tok.value != "in") {
                throw std::runtime_error("Line " + std::to_string(in_tok.line)
                    + ": expected 'in'");
            }
            advance();
        }
        if (peek().type == TokenType::kStringLiteral || peek().type == TokenType::kIdentifier) {
            auto stmt = std::make_unique<ForArrayStmt>();
            stmt->var_name = for_var_name;
            stmt->array_expr = parseExpr();
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
                expect(TokenType::kSemicolon, "expected ';'");
            } else {
                stmt->block_label = "for";
            }
            return stmt;
        }
        expect(TokenType::kLParen, "expected '('");
        auto first_expr = parseExpr();
        if (peek().type == TokenType::kDotDot) {
            // for var in (start..end) — numeric range
            advance(); // consume '..'
            auto stmt = std::make_unique<ForRangeStmt>();
            stmt->var_type = for_var_type;
            stmt->var_name = for_var_name;
            stmt->range_start = std::move(first_expr);
            stmt->range_end = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
                expect(TokenType::kSemicolon, "expected ';'");
            } else {
                stmt->block_label = "for";
            }
            return stmt;
        } else {
            // for var in (expr, expr, ...) — tuple iteration
            auto stmt = std::make_unique<ForTupleStmt>();
            stmt->var_name = for_var_name;
            stmt->elements.push_back(std::move(first_expr));
            while (peek().type == TokenType::kComma) {
                advance(); // consume ','
                stmt->elements.push_back(parseExpr());
            }
            expect(TokenType::kRParen, "expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
                expect(TokenType::kSemicolon, "expected ';'");
            } else {
                stmt->block_label = "for";
            }
            return stmt;
        }
    }

    // tuple return nested function or tuple destructure: starts with '('
    if (t.type == TokenType::kLParen) {
        // scan to matching ')' and check what follows; also count outer-level
        // commas so single-element parens fall through to the lvalue branch
        // (anon-tuples have minimum size 2).
        int depth = 1, scan = pos_ + 1, outer_commas = 0;
        while (scan < (int)tokens_.size() && depth > 0) {
            if (tokens_[scan].type == TokenType::kLParen) depth++;
            else if (tokens_[scan].type == TokenType::kRParen) depth--;
            else if (depth == 1 && tokens_[scan].type == TokenType::kComma) outer_commas++;
            scan++;
        }
        // scan is now at the token after the matching ')'
        if (scan < (int)tokens_.size()
            && tokens_[scan].type == TokenType::kIdentifier
            && scan + 1 < (int)tokens_.size()
            && tokens_[scan + 1].type == TokenType::kLParen) {
            // (type name, ...) funcName() { ... } — nested function with tuple return
            auto stmt = std::make_unique<NestedFunctionDefStmt>();
            stmt->def = parseNestedFunctionDef();
            return stmt;
        } else if (outer_commas > 0
                   && scan < (int)tokens_.size()
                   && tokens_[scan].type == TokenType::kEquals) {
            // (type name, ...) = expr; — tuple destructure
            // slots may be: empty (skip), bare name (infer type), or type + name (declared).
            advance(); // consume '('
            auto td = std::make_unique<TupleDestructureStmt>();
            if (peek().type != TokenType::kRParen) {
                while (true) {
                    // empty slot — next is ',' or ')' where a field should start
                    if (peek().type == TokenType::kComma || peek().type == TokenType::kRParen) {
                        td->fields.emplace_back("", "");
                    } else {
                        std::string type;
                        std::string name;
                        // bare name form: identifier followed by ',' or ')'
                        if (peek().type == TokenType::kIdentifier
                            && pos_ + 1 < (int)tokens_.size()
                            && (tokens_[pos_ + 1].type == TokenType::kComma
                                || tokens_[pos_ + 1].type == TokenType::kRParen)) {
                            name = advance().value;
                        } else {
                            type = parseTypeName();
                            name = expect(TokenType::kIdentifier, "expected variable name").value;
                        }
                        td->fields.emplace_back(type, name);
                    }
                    if (peek().type == TokenType::kComma) { advance(); continue; }
                    break;
                }
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kEquals, "expected '='");
            td->init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            return td;
        } else if (outer_commas > 0
                   && scan < (int)tokens_.size()
                   && tokens_[scan].type == TokenType::kIdentifier
                   && scan + 1 < (int)tokens_.size()
                   && (tokens_[scan + 1].type == TokenType::kEquals
                       || tokens_[scan + 1].type == TokenType::kArrowLeft
                       || tokens_[scan + 1].type == TokenType::kSemicolon)) {
            // (t1, t2, ...) name [= expr | <- expr]; — anon-tuple typed var decl
            advance(); // consume '('
            std::string type = "(";
            bool first = true;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                if (!first) type += ",";
                first = false;
                type += parseTypeName();
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            type += ")";
            std::string name = expect(TokenType::kIdentifier, "expected variable name").value;
            if (peek().type == TokenType::kSemicolon) {
                advance();
                declareVar(name);
                return std::make_unique<VarDeclStmt>(type, name, nullptr);
            }
            if (peek().type == TokenType::kArrowLeft) {
                advance();
                auto init = parseExpr();
                expect(TokenType::kSemicolon, "expected ';'");
                declareVar(name);
                return std::make_unique<VarDeclStmt>(type, name, std::move(init),
                                                      std::vector<std::unique_ptr<Expr>>{}, true);
            }
            expect(TokenType::kEquals, "expected '='");
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init));
        }
        // paren-led lvalue statement: (lvalue) <op> rhs;  /  (expr);
        auto lhs = parsePostfix(parsePrimary());
        return parseLvalueTail(std::move(lhs));
    }

    // nested function definition: type name(...) { ... }
    // distinguish from var decl (type name = expr) by lookahead for '(' then '{' after ')'
    if (isTypeName(t)) {
        // lookahead: type identifier ( ... ) {
        int lookahead = pos_ + 1; // pos_ is at type, +1 is identifier
        if (lookahead < (int)tokens_.size()
            && tokens_[lookahead].type == TokenType::kIdentifier) {
            int after_name = lookahead + 1;
            if (after_name < (int)tokens_.size()
                && tokens_[after_name].type == TokenType::kLParen) {
                // scan forward past params to find matching ) then check for {
                int depth = 1;
                int scan = after_name + 1;
                while (scan < (int)tokens_.size() && depth > 0) {
                    if (tokens_[scan].type == TokenType::kLParen) depth++;
                    else if (tokens_[scan].type == TokenType::kRParen) depth--;
                    scan++;
                }
                // scan now points past the ')'
                if (scan < (int)tokens_.size()
                    && tokens_[scan].type == TokenType::kLBrace) {
                    auto stmt = std::make_unique<NestedFunctionDefStmt>();
                    stmt->def = parseNestedFunctionDef();
                    return stmt;
                }
            }
        }
        // not a nested function — fall through to variable declaration
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;

        // array declaration: Type name[d0][d1...] = ((..),(..),..);
        if (peek().type == TokenType::kLBracket) {
            auto arr = std::make_unique<ArrayDeclStmt>();
            arr->elem_type = type;
            arr->name = name;
            while (peek().type == TokenType::kLBracket) {
                advance();
                int dim = std::stoi(expect(TokenType::kIntLiteral, "expected array dimension").value);
                expect(TokenType::kRBracket, "expected ']'");
                arr->dims.push_back(dim);
            }
            // parse nested initializer lists: flatten into row-major order
            std::function<void()> parseInitList = [&]() {
                if (peek().type == TokenType::kLParen) {
                    advance();
                    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                        parseInitList();
                        if (peek().type == TokenType::kComma) advance();
                    }
                    expect(TokenType::kRParen, "expected ')'");
                } else {
                    arr->init_values.push_back(parseExpr());
                }
            };
            if (peek().type == TokenType::kEquals) {
                advance();
                parseInitList();
            }
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(arr->name);
            return arr;
        }

        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init), std::vector<std::unique_ptr<Expr>>{}, true);
        }
        if (peek().type == TokenType::kSemicolon) {
            advance();
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, nullptr);
        }
        expect(TokenType::kEquals, "expected '='");
        auto init = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        declareVar(name);
        return std::make_unique<VarDeclStmt>(type, name, std::move(init));
    }

    // user-defined type variable declaration: counter c; or counter c(5); or piece board[8][8] = ...
    if (t.type == TokenType::kIdentifier && isVarDeclLookahead()) {
        std::string type = parseTypeName(); // consumes Name plus any trailing ^ or []
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;

        // pointer or reference variable with initializer: Type[] ptr = expr  or  Type^ ref = expr
        if (peek().type == TokenType::kEquals) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init));
        }
        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init), std::vector<std::unique_ptr<Expr>>{}, true);
        }

        // array declaration: Type name[d0][d1] = (...)
        if (peek().type == TokenType::kLBracket) {
            auto arr = std::make_unique<ArrayDeclStmt>();
            arr->elem_type = type;
            arr->name = name;
            while (peek().type == TokenType::kLBracket) {
                advance();
                int dim = std::stoi(expect(TokenType::kIntLiteral, "expected array dimension").value);
                expect(TokenType::kRBracket, "expected ']'");
                arr->dims.push_back(dim);
            }
            std::function<void()> parseInitList = [&]() {
                if (peek().type == TokenType::kLParen) {
                    advance();
                    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                        parseInitList();
                        if (peek().type == TokenType::kComma) advance();
                    }
                    expect(TokenType::kRParen, "expected ')'");
                } else {
                    arr->init_values.push_back(parseExpr());
                }
            };
            if (peek().type == TokenType::kEquals) {
                advance();
                parseInitList();
            }
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(arr->name);
            return arr;
        }

        std::vector<std::unique_ptr<Expr>> ctor_args;
        if (peek().type == TokenType::kLParen) {
            advance();
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                ctor_args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
        }
        std::unique_ptr<Expr> init;
        if (peek().type == TokenType::kEquals) {
            advance();
            init = parseExpr();
        }
        expect(TokenType::kSemicolon, "expected ';'");
        declareVar(name);
        return std::make_unique<VarDeclStmt>(type, name, std::move(init), std::move(ctor_args));
    }

    // identifier — assignment, compound assignment, method call, or function call
    if (t.type == TokenType::kIdentifier) {
        std::string name = t.value;
        advance();

        // namespace-qualified call statement: Name:method(args);
        if (peek().type == TokenType::kColon
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIdentifier
            && pos_ + 2 < (int)tokens_.size()
            && tokens_[pos_ + 2].type == TokenType::kLParen) {
            advance(); // consume ':'
            std::string method = advance().value;
            advance(); // consume '('
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            auto call = std::make_unique<CallExpr>(method, std::move(args));
            call->qualifier = name;
            return std::make_unique<ExprStmt>(std::move(call));
        }

        // template call statement: name<Type,...>(args);
        if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
            advance(); // consume '<'
            std::vector<std::string> type_args;
            while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
                type_args.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kGt, "expected '>'");
            expect(TokenType::kLParen, "expected '('");
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            auto stmt = std::make_unique<CallStmt>(name, std::move(args));
            stmt->type_args = std::move(type_args);
            return stmt;
        }

        // function call statement
        if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            return std::make_unique<CallStmt>(name, std::move(args));
        }

        // identifier-led lvalue: parse the postfix chain and route through the
        // unified lvalue tail (handles =, <-, <->, compound op=, and bare expr).
        auto lhs = parsePostfix(std::make_unique<VarExpr>(name));
        return parseLvalueTail(std::move(lhs));
    }

    throw std::runtime_error("Line " + std::to_string(t.line)
        + ": unexpected token '" + t.value + "'");
}

// --- Top-level parsing ---

NestedFunctionDef Parser::parseNestedFunctionDef() {
    NestedFunctionDef fn;
    if (peek().type == TokenType::kLParen) {
        advance(); // consume '('
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "expected field name").value;
            fn.tuple_return_fields.emplace_back(type, name);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
    } else {
        fn.return_type = parseTypeName();
    }
    fn.name = expect(TokenType::kIdentifier, "expected function name").value;
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        fn.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    std::vector<std::string> param_names;
    for (auto& p : fn.params) param_names.push_back(p.second);
    fn.body = parseBlock(param_names);
    return fn;
}

MethodDef Parser::parseMethodDef() {
    MethodDef m;
    // new syntax: op overloads have no explicit return type (implied void / self)
    if (peek().type == TokenType::kIdentifier && peek().value == "op") {
        m.return_type = "void";
    } else {
        m.return_type = parseTypeName();
    }
    m.name = expect(TokenType::kIdentifier, "expected method name").value;
    if (m.name == "op" && peek().type == TokenType::kEquals)     { advance(); m.name = "op="; }
    else if (m.name == "op" && peek().type == TokenType::kArrowLeft) { advance(); m.name = "op<-"; }
    else if (m.name == "op" && peek().type == TokenType::kArrowBoth) { advance(); m.name = "op<->"; }
    else if (m.name == "op" && peek().type == TokenType::kPlus)    { advance(); m.name = "op+"; }
    else if (m.name == "op" && peek().type == TokenType::kMinus)   { advance(); m.name = "op-"; }
    else if (m.name == "op" && peek().type == TokenType::kStar)    { advance(); m.name = "op*"; }
    else if (m.name == "op" && peek().type == TokenType::kSlash)   { advance(); m.name = "op/"; }
    else if (m.name == "op" && peek().type == TokenType::kPlusEq)  { advance(); m.name = "op+="; }
    else if (m.name == "op" && peek().type == TokenType::kMinusEq) { advance(); m.name = "op-="; }
    else if (m.name == "op" && peek().type == TokenType::kStarEq)  { advance(); m.name = "op*="; }
    else if (m.name == "op" && peek().type == TokenType::kSlashEq) { advance(); m.name = "op/="; }
    else if (m.name == "op" && peek().type == TokenType::kBracketAssign) { advance(); m.name = "op[]="; }
    else if (m.name == "op" && peek().type == TokenType::kLBracket) {
        advance();
        expect(TokenType::kRBracket, "expected ']'");
        m.name = "op[]";
    }
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        m.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — no body
    } else {
        std::vector<std::string> param_names;
        for (auto& p : m.params) param_names.push_back(p.second);
        m.body = parseBlock(param_names);
    }
    return m;
}

SlidDef Parser::parseSlidDef() {
    SlidDef slid;
    slid.name = peek().value;
    advance(); // consume class name

    // a closed class accepts no more tuple-form reopens (bare-block reopens are
    // allowed via parseExternalMethodBlock and bypass this check).
    if (closed_classes_.count(slid.name))
        throw std::runtime_error("Line " + std::to_string(peek().line)
            + ": class '" + slid.name + "' is already complete; further field/declaration reopens are not permitted");

    // save outer's alias map; nested slids in this slid's body register short→canonical here
    auto saved_alias = nested_alias_;
    nested_alias_.clear();

    // template type parameters: Vector<T> or Pair<K, V>
    if (peek().type == TokenType::kLt) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            slid.type_params.push_back(
                expect(TokenType::kIdentifier, "expected type parameter name").value);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "expected '>' after type parameters");
    }

    // parse tuple: (type field_ = default, ...)
    expect(TokenType::kLParen, "expected '('");

    // leading ellipsis?
    if (peek().type == TokenType::kEllipsis) {
        advance(); // consume ...
        if (peek().type == TokenType::kComma) advance(); // consume , before fields
        if (peek().type == TokenType::kRParen) {
            // lone (...): disambiguate by whether the class has been seen before
            // in this TU. first occurrence ⇒ trailing-only (open incomplete);
            // subsequent ⇒ leading-only (closing reopen, no new fields).
            if (seen_classes_.count(slid.name))
                slid.has_leading_ellipsis = true;
            else
                slid.has_trailing_ellipsis = true;
        } else {
            slid.has_leading_ellipsis = true;
            slid.is_transport_impl = true;  // emits __$pinit and __$sizeof for the consumer
        }
    }

    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        // trailing ellipsis?
        if (peek().type == TokenType::kEllipsis) {
            slid.has_trailing_ellipsis = true;
            advance(); // consume ...
            if (peek().type == TokenType::kComma) advance();
            break; // ... must be last
        }
        FieldDef f;
        f.type = parseTypeName();
        f.name = expect(TokenType::kIdentifier, "expected field name").value;
        // inline fixed-size array field: char name_[16]
        if (peek().type == TokenType::kLBracket
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIntLiteral) {
            advance(); // consume [
            std::string sz = advance().value; // consume N
            expect(TokenType::kRBracket, "expected ']'");
            f.type += "[" + sz + "]";
        }
        if (peek().type == TokenType::kEquals) {
            advance();
            f.default_val = parseExpr();
        }
        slid.fields.push_back(std::move(f));
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");

    // record field names to prevent method-body field assignments from being mistaken for inferred declarations
    current_slid_fields_.clear();
    for (auto& f : slid.fields) current_slid_fields_.insert(f.name);
    all_slid_fields_[slid.name] = current_slid_fields_;

    // parse body: methods and optional constructor code
    expect(TokenType::kLBrace, "expected '{'");

    auto ctor_body = std::make_unique<BlockStmt>();
    bool has_ctor_code = false;

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        // explicit constructor: _() { ... }  or forward decl: _();
        if (peek().type == TokenType::kIdentifier && peek().value == "_"
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume _
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            if (slid.has_explicit_ctor_decl)
                throw std::runtime_error("Line " + std::to_string(peek().line)
                    + ": constructor already defined in '" + slid.name + "'");
            slid.has_explicit_ctor_decl = true; // declared — consumer must call ctor
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration only
            } else {
                slid.explicit_ctor_body = parseBlock();
            }
            continue;
        }
        // explicit destructor: ~() { ... }  or forward decl: ~();
        if (peek().type == TokenType::kBitNot
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume ~
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            if (slid.has_explicit_dtor_decl)
                throw std::runtime_error("Line " + std::to_string(peek().line)
                    + ": destructor already defined in '" + slid.name + "'");
            slid.has_explicit_dtor_decl = true; // declared — consumer must call dtor
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration only
            } else {
                slid.dtor_body = parseBlock();
            }
            continue;
        }
        // method definition: starts with a type name followed by identifier followed by (
        // also handles operator methods: op=(...)  op<-(...)  op+(...)  etc.
        auto isMethodDecl = [&]() {
            // new syntax: op<symbol>( without explicit return type
            if (peek().type == TokenType::kIdentifier && peek().value == "op") {
                if (pos_ + 1 >= (int)tokens_.size()) return false;
                auto t = tokens_[pos_ + 1].type;
                // op[]= : op + []= + (
                if (t == TokenType::kBracketAssign) {
                    return pos_ + 2 < (int)tokens_.size() && tokens_[pos_ + 2].type == TokenType::kLParen;
                }
                // op[] : op + [ + ] + (
                if (t == TokenType::kLBracket) {
                    return pos_ + 3 < (int)tokens_.size()
                        && tokens_[pos_ + 2].type == TokenType::kRBracket
                        && tokens_[pos_ + 3].type == TokenType::kLParen;
                }
                bool is_op_tok = (t == TokenType::kEquals   || t == TokenType::kArrowLeft
                    || t == TokenType::kArrowBoth || t == TokenType::kPlus   || t == TokenType::kMinus
                    || t == TokenType::kStar      || t == TokenType::kSlash  || t == TokenType::kPlusEq
                    || t == TokenType::kMinusEq   || t == TokenType::kStarEq || t == TokenType::kSlashEq
                    || t == TokenType::kEqEq      || t == TokenType::kNotEq
                    || t == TokenType::kLt        || t == TokenType::kGt
                    || t == TokenType::kLtEq      || t == TokenType::kGtEq);
                if (!is_op_tok) return false;
                if (pos_ + 2 >= (int)tokens_.size()) return false;
                return tokens_[pos_ + 2].type == TokenType::kLParen;
            }
            // regular method: return-type name(
            // return type may include pointer/iterator suffixes: ^ or []
            if (!(isTypeName(peek()) || isUserTypeName(peek()))) return false;
            int name_pos = pos_ + 1;
            while (name_pos < (int)tokens_.size()) {
                if (tokens_[name_pos].type == TokenType::kBitXor) {
                    name_pos++;
                } else if (tokens_[name_pos].type == TokenType::kLBracket
                           && name_pos + 1 < (int)tokens_.size()
                           && tokens_[name_pos + 1].type == TokenType::kRBracket) {
                    name_pos += 2;
                } else {
                    break;
                }
            }
            if (name_pos + 1 >= (int)tokens_.size()) return false;
            if (tokens_[name_pos].type != TokenType::kIdentifier) return false;
            if (tokens_[name_pos + 1].type == TokenType::kLParen) return true;
            // int op[]( pattern
            if (tokens_[name_pos].value == "op"
                    && tokens_[name_pos + 1].type == TokenType::kLBracket
                    && name_pos + 3 < (int)tokens_.size()
                    && tokens_[name_pos + 2].type == TokenType::kRBracket
                    && tokens_[name_pos + 3].type == TokenType::kLParen) return true;
            // old op= pattern: void op = (  (kept for backward compatibility)
            if (tokens_[name_pos].value != "op") return false;
            if (name_pos + 2 >= (int)tokens_.size()) return false;
            return (tokens_[name_pos + 1].type == TokenType::kEquals || tokens_[name_pos + 1].type == TokenType::kArrowLeft)
                && tokens_[name_pos + 2].type == TokenType::kLParen;
        };
        // nested slid def: Identifier ( <field-list-or-empty-or-...> ) { body }
        // distinguish from method decl (return type before identifier) and from a ctor
        // statement (call/assignment, terminated by ';'). Disambiguator: matching ')'
        // followed by '{' is unambiguously a slid body — calls/assignments end in ';'.
        auto isNestedSlidDecl = [&]() {
            if (peek().type != TokenType::kIdentifier) return false;
            if (pos_ + 1 >= (int)tokens_.size()) return false;
            if (tokens_[pos_ + 1].type != TokenType::kLParen
                && tokens_[pos_ + 1].type != TokenType::kLt) return false;
            // scan past the (...) (and optional <...> template params) to find a {
            int scan = pos_ + 1;
            if (tokens_[scan].type == TokenType::kLt) {
                int depth = 1;
                scan++;
                while (scan < (int)tokens_.size() && depth > 0) {
                    if (tokens_[scan].type == TokenType::kLt) depth++;
                    else if (tokens_[scan].type == TokenType::kGt) depth--;
                    scan++;
                }
                if (scan >= (int)tokens_.size() || tokens_[scan].type != TokenType::kLParen)
                    return false;
            }
            int depth = 1;
            scan++;
            while (scan < (int)tokens_.size() && depth > 0) {
                if (tokens_[scan].type == TokenType::kLParen) depth++;
                else if (tokens_[scan].type == TokenType::kRParen) depth--;
                scan++;
            }
            return scan < (int)tokens_.size() && tokens_[scan].type == TokenType::kLBrace;
        };
        if (isNestedSlidDecl()) {
            // save outer's per-slid context across the recursive call
            auto saved_fields = current_slid_fields_;
            SlidDef inner = parseSlidDef();
            current_slid_fields_ = saved_fields;
            // register short→canonical alias so subsequent methods of this outer
            // can refer to the nested type by its bare name
            std::string canonical = slid.name + "." + inner.name;
            nested_alias_[inner.name] = canonical;
            slid.nested_slids.push_back(std::move(inner));
            continue;
        }
        if (isMethodDecl()) {
            slid.methods.push_back(parseMethodDef());
        } else {
            // constructor code
            ctor_body->stmts.push_back(parseStmt());
            has_ctor_code = true;
        }
    }

    if (has_ctor_code)
        slid.ctor_body = std::move(ctor_body);

    expect(TokenType::kRBrace, "expected '}'");
    current_slid_fields_.clear();
    nested_alias_ = std::move(saved_alias);

    // register this class as seen (disambiguates lone `(...)` on next reopen)
    // and mark it closed if this reopen has no trailing `...`.
    seen_classes_.insert(slid.name);
    if (!slid.has_trailing_ellipsis) closed_classes_.insert(slid.name);

    return slid;
}

EnumDef Parser::parseEnumDef() {
    expect(TokenType::kEnum, "expected 'enum'");
    EnumDef e;
    e.name = expect(TokenType::kIdentifier, "expected enum name").value;
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        e.values.push_back(expect(TokenType::kIdentifier, "expected enum value").value);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    return e;
}

ExternalMethodDef Parser::parseExternalMethodDef() {
    ExternalMethodDef em;
    // optional return type (primitive keyword); absent for ctor/dtor
    if (isTypeName(peek())) em.return_type = parseTypeName();
    // TypeName:
    em.slid_name = expect(TokenType::kIdentifier, "expected class name").value;
    // set field names for this slid so assignments to fields aren't mistaken for inferred declarations
    {
        auto fit = all_slid_fields_.find(em.slid_name);
        current_slid_fields_ = (fit != all_slid_fields_.end()) ? fit->second : std::set<std::string>{};
    }
    expect(TokenType::kColon, "expected ':'");
    // method name, or _ (ctor), or ~ (dtor)
    if (peek().type == TokenType::kIdentifier && peek().value == "_") {
        em.method_name = "_"; advance();
    } else if (peek().type == TokenType::kBitNot) {
        em.method_name = "~"; advance();
    } else {
        em.method_name = expect(TokenType::kIdentifier, "expected method name").value;
    }
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        em.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — no body
    } else {
        std::vector<std::string> param_names;
        for (auto& p : em.params) param_names.push_back(p.second);
        em.body = parseBlock(param_names);
    }
    current_slid_fields_.clear();
    return em;
}

void Parser::parseExternalMethodBlock(Program& program) {
    // TypeName { [returnType] methodName(params) { body } ... }
    std::string slid_name = expect(TokenType::kIdentifier, "expected class name").value;
    seen_classes_.insert(slid_name);
    {
        auto fit = all_slid_fields_.find(slid_name);
        current_slid_fields_ = (fit != all_slid_fields_.end()) ? fit->second : std::set<std::string>{};
    }
    expect(TokenType::kLBrace, "expected '{'");
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        ExternalMethodDef em;
        em.slid_name = slid_name;
        // ctor: _() { ... }
        if (peek().type == TokenType::kIdentifier && peek().value == "_"
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume _
            em.method_name = "_";
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            em.body = parseBlock();
            program.external_methods.push_back(std::move(em));
            continue;
        }
        // dtor: ~() { ... }
        if (peek().type == TokenType::kBitNot
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume ~
            em.method_name = "~";
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            em.body = parseBlock();
            program.external_methods.push_back(std::move(em));
            continue;
        }
        // new syntax: op overloads have no explicit return type
        if (peek().type == TokenType::kIdentifier && peek().value == "op") {
            em.return_type = "void";
        } else {
            em.return_type = parseTypeName();
        }
        em.method_name = expect(TokenType::kIdentifier, "expected method name").value;
        if (em.method_name == "op" && peek().type == TokenType::kEquals)     { advance(); em.method_name = "op="; }
        else if (em.method_name == "op" && peek().type == TokenType::kArrowLeft) { advance(); em.method_name = "op<-"; }
        else if (em.method_name == "op" && peek().type == TokenType::kArrowBoth) { advance(); em.method_name = "op<->"; }
        else if (em.method_name == "op" && peek().type == TokenType::kPlus)    { advance(); em.method_name = "op+"; }
        else if (em.method_name == "op" && peek().type == TokenType::kMinus)   { advance(); em.method_name = "op-"; }
        else if (em.method_name == "op" && peek().type == TokenType::kStar)    { advance(); em.method_name = "op*"; }
        else if (em.method_name == "op" && peek().type == TokenType::kSlash)   { advance(); em.method_name = "op/"; }
        else if (em.method_name == "op" && peek().type == TokenType::kPlusEq)  { advance(); em.method_name = "op+="; }
        else if (em.method_name == "op" && peek().type == TokenType::kMinusEq) { advance(); em.method_name = "op-="; }
        else if (em.method_name == "op" && peek().type == TokenType::kStarEq)  { advance(); em.method_name = "op*="; }
        else if (em.method_name == "op" && peek().type == TokenType::kSlashEq) { advance(); em.method_name = "op/="; }
        expect(TokenType::kLParen, "expected '('");
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
            em.params.emplace_back(type, name);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
        if (peek().type == TokenType::kSemicolon) {
            advance(); // forward declaration — no body
        } else {
            std::vector<std::string> param_names;
            for (auto& p : em.params) param_names.push_back(p.second);
            em.body = parseBlock(param_names);
        }
        program.external_methods.push_back(std::move(em));
    }
    expect(TokenType::kRBrace, "expected '}'");
    current_slid_fields_.clear();
}

FunctionDef Parser::parseFunctionDef() {
    FunctionDef fn;
    if (peek().type == TokenType::kLParen) {
        advance(); // consume '('
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "expected field name").value;
            fn.tuple_return_fields.emplace_back(type, name);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
    } else {
        fn.return_type = parseTypeName();
    }
    {
        std::string fname = expect(TokenType::kIdentifier, "expected function name").value;
        if (fname == "op") {
            static const std::map<TokenType, std::string> op_map = {
                {TokenType::kPlus, "+"}, {TokenType::kMinus, "-"},
                {TokenType::kStar, "*"}, {TokenType::kSlash, "/"},
                {TokenType::kEqEq, "=="}, {TokenType::kNotEq, "!="},
                {TokenType::kLt, "<"}, {TokenType::kGt, ">"},
                {TokenType::kLtEq, "<="}, {TokenType::kGtEq, ">="},
            };
            auto it = op_map.find(peek().type);
            if (it != op_map.end()) { advance(); fname = "op" + it->second; }
        }
        fn.name = fname;
        fn.user_name = fname;
    }
    // template type params: funcname<T, U, ...>
    if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            fn.type_params.push_back(
                expect(TokenType::kIdentifier, "expected type parameter name").value);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "expected '>'");
    }
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        fn.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — body remains null
    } else {
        std::vector<std::string> param_names;
        for (auto& p : fn.params) param_names.push_back(p.second);
        fn.body = parseBlock(param_names);
    }
    return fn;
}

// helper: find a .slh by searching a list of directories, returns "" if not found
static std::string findHeader(const std::string& module,
                               const std::vector<std::string>& search_dirs) {
    for (auto& dir : search_dirs) {
        std::string p = dir.empty() ? module + ".slh" : dir + "/" + module + ".slh";
        std::ifstream test(p);
        if (test) return p;
    }
    return "";
}

Program Parser::parse() {
    Program program;
    while (peek().type != TokenType::kEof) {
        // import declaration: search export_path first, then import_paths, then source_dir
        if (peek().type == TokenType::kImport) {
            advance(); // consume 'import'
            std::string module = expect(TokenType::kIdentifier, "expected module name after 'import'").value;
            expect(TokenType::kSemicolon, "expected ';' after import");

            std::vector<std::string> search;
            for (auto& p : import_paths_) search.push_back(p);
            if (!source_dir_.empty()) search.push_back(source_dir_);
            if (search.empty()) search.push_back("");

            std::string header_path = findHeader(module, search);
            if (header_path.empty())
                throw std::runtime_error("import: cannot find '" + module + ".slh'");

            // import-once: skip if this header has already been loaded in this compile
            if (!imported_once_->insert(header_path).second) continue;

            program.imported_headers.push_back(header_path);
            std::ifstream in(header_path);
            std::ostringstream buf; buf << in.rdbuf();
            Lexer hdr_lexer(buf.str());
            Parser hdr_parser(hdr_lexer.tokenize(), source_dir_, import_paths_, imported_once_);
            Program hdr = hdr_parser.parse();

            // check before moving whether any functions or slids are template declarations
            bool has_templates = false;
            for (auto& fn : hdr.functions)
                if (!fn.type_params.empty()) { has_templates = true; break; }
            if (!has_templates)
                for (auto& slid : hdr.slids)
                    if (!slid.type_params.empty()) { has_templates = true; break; }

            for (auto& fn : hdr.functions)
                if (!fn.body) program.functions.push_back(std::move(fn));
            for (auto& slid : hdr.slids) {
                program.slid_modules[slid.name] = module;
                program.slids.push_back(std::move(slid));
            }

            // load template bodies from impl file: foo.slh -> foo.sl
            if (has_templates) {
                std::string impl_path = header_path.substr(0, header_path.size() - 4) + ".sl";
                std::ifstream impl_in(impl_path);
                if (impl_in) {
                    program.imported_headers.push_back(impl_path);
                    std::ostringstream impl_buf; impl_buf << impl_in.rdbuf();
                    Lexer impl_lexer(impl_buf.str());
                    auto impl_cache = std::make_shared<std::set<std::string>>();
                    impl_cache->insert(header_path);
                    impl_cache->insert(impl_path);
                    Parser impl_parser(impl_lexer.tokenize(), source_dir_, import_paths_, impl_cache);
                    Program impl_prog = impl_parser.parse();
                    for (size_t i = 0; i < impl_prog.functions.size(); i++) {
                        auto& fn = impl_prog.functions[i];
                        if (!fn.type_params.empty() && fn.body) {
                            fn.is_local = false;
                            fn.impl_module = module;
                            program.functions.push_back(std::move(fn));
                        }
                    }
                    for (size_t i = 0; i < impl_prog.slids.size(); i++) {
                        auto& impl_slid = impl_prog.slids[i];
                        if (impl_slid.type_params.empty()) continue;
                        impl_slid.is_local = false;
                        impl_slid.impl_module = module;
                        bool replaced = false;
                        for (auto& prog_slid : program.slids) {
                            if (prog_slid.name == impl_slid.name && !prog_slid.type_params.empty()) {
                                prog_slid = std::move(impl_slid);
                                replaced = true;
                                break;
                            }
                        }
                        if (!replaced)
                            program.slids.push_back(std::move(impl_slid));
                    }
                }
            }
        }
        // enum definition
        else if (peek().type == TokenType::kEnum) {
            program.enums.push_back(parseEnumDef());
        }
        // explicit template instantiation: Name<Types>(ParamTypes);
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLt
            && isInstantiationLookahead()) {
            std::string name = advance().value;
            advance(); // '<'
            std::vector<std::string> type_args;
            type_args.push_back(parseTypeName());
            while (peek().type == TokenType::kComma) { advance(); type_args.push_back(parseTypeName()); }
            expect(TokenType::kGt, "expected '>'");
            std::vector<std::string> param_types;
            expect(TokenType::kLParen, "expected '(' after instantiate type args");
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                param_types.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            program.instantiations.push_back({name, std::move(type_args), std::move(param_types)});
        }
        // slid class definition: bare identifier immediately followed by ( or <
        // (no return type prefix — that's what makes it a class, not a function)
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kLParen
                || tokens_[pos_ + 1].type == TokenType::kLt)) {
            program.slids.push_back(parseSlidDef());
        }
        // derived class definition or forward decl: Base : Derived(...)  or  Base : Derived;
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 3 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kColon
            && tokens_[pos_ + 2].type == TokenType::kIdentifier
            && (tokens_[pos_ + 3].type == TokenType::kLParen
                || tokens_[pos_ + 3].type == TokenType::kSemicolon)) {
            std::string base_name = advance().value; // consume base name
            advance();                                // consume ':'
            if (tokens_[pos_ + 1].type == TokenType::kSemicolon) {
                SlidDef fwd;
                fwd.name = advance().value;
                fwd.base_name = base_name;
                advance(); // consume ';'
                program.slids.push_back(std::move(fwd));
            } else {
                SlidDef slid = parseSlidDef();
                slid.base_name = base_name;
                program.slids.push_back(std::move(slid));
            }
        }
        // block-style external methods: TypeName { void method() { ... } ... }
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLBrace) {
            parseExternalMethodBlock(program);
        } else if (peek().type == TokenType::kLParen) {
            program.functions.push_back(parseFunctionDef());
        } else {
            // detect external method: [returnType] TypeName:
            // advance past return type (primitive or user type) when followed by another identifier
            int p = pos_;
            if ((isTypeName(tokens_[p]) || tokens_[p].type == TokenType::kIdentifier)
                && p + 1 < (int)tokens_.size()
                && tokens_[p + 1].type == TokenType::kIdentifier) p++;
            if (p + 1 < (int)tokens_.size()
                && tokens_[p].type == TokenType::kIdentifier
                && tokens_[p + 1].type == TokenType::kColon) {
                program.external_methods.push_back(parseExternalMethodDef());
            } else {
                program.functions.push_back(parseFunctionDef());
            }
        }
    }

    // hoist nested slid defs to top level. each nested slid is renamed
    // <Outer>.<Inner> so it's globally addressable. parent_name is taken by
    // value because push_back below may reallocate program.slids, which would
    // invalidate any reference into it.
    std::function<void(SlidDef&, std::string)> hoist =
        [&](SlidDef& s, std::string parent_name) {
            std::vector<SlidDef> nested = std::move(s.nested_slids);
            s.nested_slids.clear();
            for (auto& n : nested) {
                std::string canonical = parent_name + "." + n.name;
                n.name = canonical;
                hoist(n, canonical);
                program.slids.push_back(std::move(n));
            }
        };
    int top_count = (int)program.slids.size();
    for (int i = 0; i < top_count; i++) {
        hoist(program.slids[i], program.slids[i].name);
    }

    // collapse multiple reopens of the same class into one merged SlidDef.
    mergeReopens(program);

    // synthesize SlidDef entries for namespaces — slid names that appear only
    // in external_methods (block reopens or `void Name:fn()` defs) with no `Name(...)` data block.
    {
        std::set<std::string> existing;
        for (auto& s : program.slids) existing.insert(s.name);
        std::set<std::string> seen_ns;
        for (auto& em : program.external_methods) {
            if (existing.count(em.slid_name)) continue;
            if (!seen_ns.insert(em.slid_name).second) continue;
            SlidDef ns;
            ns.name = em.slid_name;
            ns.is_namespace = true;
            program.slids.push_back(std::move(ns));
        }
    }

    return program;
}

void Parser::mergeReopens(Program& program) {
    // group SlidDef indices by class name in source order. skip namespaces
    // (synthesized later) and template slids (have separate machinery).
    std::map<std::string, std::vector<int>> groups;
    for (int i = 0; i < (int)program.slids.size(); i++) {
        if (program.slids[i].is_namespace) continue;
        if (!program.slids[i].type_params.empty()) continue;
        groups[program.slids[i].name].push_back(i);
    }
    std::set<int> to_remove;
    for (auto& [name, indices] : groups) {
        if (indices.size() <= 1) continue;
        SlidDef& dst = program.slids[indices[0]];
        // public_field_count: the field count of the first reopen that lacks a
        // leading `...` (the public prefix). if dst already qualifies, count is
        // its current field size; otherwise scan for one in the rest of the group.
        bool first_no_leading = !dst.has_leading_ellipsis;
        if (first_no_leading) dst.public_field_count = (int)dst.fields.size();
        for (int i = 1; i < (int)indices.size(); i++) {
            SlidDef& src = program.slids[indices[i]];
            if (!first_no_leading && !src.has_leading_ellipsis) {
                dst.public_field_count = (int)dst.fields.size() + (int)src.fields.size();
                first_no_leading = true;
            }
            // append fields in source order
            for (auto& f : src.fields) dst.fields.push_back(std::move(f));
            // OR — "any reopen contributed this"
            dst.has_explicit_ctor_decl = dst.has_explicit_ctor_decl || src.has_explicit_ctor_decl;
            dst.has_explicit_dtor_decl = dst.has_explicit_dtor_decl || src.has_explicit_dtor_decl;
            dst.is_transport_impl      = dst.is_transport_impl      || src.is_transport_impl;
            dst.has_leading_ellipsis   = dst.has_leading_ellipsis   || src.has_leading_ellipsis;
            // AND — "still open" only if every reopen left it open
            dst.has_trailing_ellipsis  = dst.has_trailing_ellipsis  && src.has_trailing_ellipsis;
            // pick the non-empty / non-null contribution
            if (dst.base_name.empty()) dst.base_name = src.base_name;
            if (!dst.ctor_body)          dst.ctor_body          = std::move(src.ctor_body);
            if (!dst.explicit_ctor_body) dst.explicit_ctor_body = std::move(src.explicit_ctor_body);
            if (!dst.dtor_body)          dst.dtor_body          = std::move(src.dtor_body);
            if (dst.impl_module.empty()) dst.impl_module = src.impl_module;
            // accumulate methods (declarations + bodies; codegen dedupes overload signatures)
            for (auto& m : src.methods) dst.methods.push_back(std::move(m));
            // accumulate any nested slids that haven't been hoisted yet
            for (auto& n : src.nested_slids) dst.nested_slids.push_back(std::move(n));
            to_remove.insert(indices[i]);
        }
    }
    if (to_remove.empty()) return;
    std::vector<SlidDef> kept;
    kept.reserve(program.slids.size() - to_remove.size());
    for (int i = 0; i < (int)program.slids.size(); i++) {
        if (!to_remove.count(i))
            kept.push_back(std::move(program.slids[i]));
    }
    program.slids = std::move(kept);
}

std::unique_ptr<SwitchStmt> Parser::parseSwitchStmt() {
    expect(TokenType::kSwitch, "expected 'switch'");
    expect(TokenType::kLParen, "expected '('");
    auto stmt = std::make_unique<SwitchStmt>();
    stmt->expr = parseExpr();
    expect(TokenType::kRParen, "expected ')'");
    expect(TokenType::kLBrace, "expected '{'");

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        SwitchCase sc;
        if (peek().type == TokenType::kCase) {
            advance();
            colon_terminates_expr_++;
            sc.value = parseExpr();
            colon_terminates_expr_--;
            expect(TokenType::kColon, "expected ':'");
        } else if (peek().type == TokenType::kDefault) {
            advance();
            expect(TokenType::kColon, "expected ':'");
            sc.value = nullptr; // default
        } else {
            throw std::runtime_error("Line " + std::to_string(peek().line)
                + ": expected 'case' or 'default' in switch");
        }
        // parse statements until next case/default/}
        while (peek().type != TokenType::kCase
            && peek().type != TokenType::kDefault
            && peek().type != TokenType::kRBrace
            && peek().type != TokenType::kEof) {
            sc.stmts.push_back(parseStmt());
        }
        stmt->cases.push_back(std::move(sc));
    }
    expect(TokenType::kRBrace, "expected '}'");
    if (peek().type == TokenType::kColon) {
        advance();
        stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
        expect(TokenType::kSemicolon, "expected ';'");
    } else {
        stmt->block_label = "switch";
    }
    return stmt;
}
