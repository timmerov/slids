#include "codegen.h"
#include <sstream>
#include <stdexcept>

Codegen::Codegen(const Program& program, std::ostream& out)
    : program_(program), out_(out), str_counter_(0) {}

// escape a string for LLVM IR and return its length including null terminator
static std::string llvmEscape(const std::string& s, int& len) {
    std::string result;
    len = 0;
    for (char c : s) {
        if (c == '\n') { result += "\\0A"; }
        else if (c == '\t') { result += "\\09"; }
        else if (c == '"') { result += "\\22"; }
        else if (c == '\\') { result += "\\5C"; }
        else { result += c; }
        len++;
    }
    result += "\\00"; // null terminator
    len++;
    return result;
}

void Codegen::emit() {
    // first pass — collect all string constants from all functions
    // so we can emit them at the top of the file
    for (auto& fn : program_.functions) {
        for (auto& stmt : fn.body) {
            if (auto* call = dynamic_cast<const CallStmt*>(stmt.get())) {
                for (auto& arg : call->args) {
                    if (auto* s = dynamic_cast<const StringLiteralExpr*>(arg.get())) {
                        std::string label = "@.str" + std::to_string(str_counter_++);
                        string_constants_.emplace_back(label, s->value);
                    }
                }
            }
        }
    }

    // emit string constants
    for (auto& [label, value] : string_constants_) {
        // println appends \n
        std::string full = value + "\n";
        int len;
        std::string escaped = llvmEscape(full, len);
        out_ << label << " = private constant [" << len << " x i8] c\"" << escaped << "\"\n";
    }

    out_ << "\n";
    out_ << "declare i32 @printf(i8* noundef, ...)\n\n";

    // reset counter for codegen pass
    str_counter_ = 0;

    for (auto& fn : program_.functions)
        emitFunction(fn);
}

void Codegen::emitFunction(const FunctionDef& fn) {
    std::string ret_type = (fn.return_type == "int32") ? "i32" : "void";
    out_ << "define " << ret_type << " @" << fn.name << "() {\n";
    out_ << "entry:\n";

    for (auto& stmt : fn.body)
        emitStmt(*stmt);

    out_ << "}\n\n";
}

void Codegen::emitStmt(const Stmt& stmt) {
    if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (auto* i = dynamic_cast<const IntLiteralExpr*>(ret->value.get())) {
            out_ << "    ret i32 " << i->value << "\n";
        } else {
            throw std::runtime_error("unsupported return expression type");
        }
        return;
    }

    if (auto* call = dynamic_cast<const CallStmt*>(&stmt)) {
        if (call->callee == "println") {
            if (call->args.size() != 1)
                throw std::runtime_error("println expects exactly 1 argument");

            auto* s = dynamic_cast<const StringLiteralExpr*>(call->args[0].get());
            if (!s) throw std::runtime_error("println argument must be a string literal in Phase 1");

            std::string label = "@.str" + std::to_string(str_counter_++);
            std::string full = s->value + "\n";
            int len;
            llvmEscape(full, len); // just to get len

            out_ << "    %p" << str_counter_ << " = getelementptr [" << len << " x i8], ["
                 << len << " x i8]* " << label << ", i32 0, i32 0\n";
            out_ << "    call i32 (i8*, ...) @printf(i8* %p" << str_counter_ << ")\n";
            return;
        }
        throw std::runtime_error("unknown function: " + call->callee);
    }

    throw std::runtime_error("unsupported statement type");
}
