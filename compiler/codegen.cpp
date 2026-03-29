#include "codegen.h"
#include <sstream>
#include <stdexcept>

Codegen::Codegen(const Program& program, std::ostream& out)
    : program_(program), out_(out), str_counter_(0), tmp_counter_(0) {}

std::string Codegen::newTmp() {
    return "%t" + std::to_string(tmp_counter_++);
}

static std::string llvmEscape(const std::string& s, int& len) {
    std::string result;
    len = 0;
    for (char c : s) {
        if (c == '\n')      { result += "\\0A"; }
        else if (c == '\t') { result += "\\09"; }
        else if (c == '"')  { result += "\\22"; }
        else if (c == '\\') { result += "\\5C"; }
        else                { result += c; }
        len++;
    }
    result += "\\00";
    len++;
    return result;
}

void Codegen::collectStringConstants() {
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
}

void Codegen::emit() {
    out_ << "target triple = \"x86_64-pc-linux-gnu\"\n\n";

    collectStringConstants();

    for (auto& [label, value] : string_constants_) {
        std::string full = value + "\n";
        int len;
        std::string escaped = llvmEscape(full, len);
        out_ << label << " = private constant [" << len << " x i8] c\"" << escaped << "\"\n";
    }

    out_ << "\n";
    out_ << "declare i32 @printf(ptr noundef, ...)\n\n";

    // format strings for integer and string println
    out_ << "@.fmt_int = private constant [4 x i8] c\"%d\\0A\\00\"\n";
    out_ << "@.fmt_str = private constant [4 x i8] c\"%s\\0A\\00\"\n\n";

    str_counter_ = 0;

    for (auto& fn : program_.functions)
        emitFunction(fn);
}

void Codegen::emitFunction(const FunctionDef& fn) {
    locals_.clear();
    tmp_counter_ = 0;

    std::string ret_type = (fn.return_type == "int32") ? "i32" : "void";
    out_ << "define " << ret_type << " @" << fn.name << "() {\n";
    out_ << "entry:\n";

    for (auto& stmt : fn.body)
        emitStmt(*stmt);

    out_ << "}\n\n";
}

std::string Codegen::emitExpr(const Expr& expr) {
    if (auto* i = dynamic_cast<const IntLiteralExpr*>(&expr)) {
        return std::to_string(i->value);
    }

    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
        auto it = locals_.find(v->name);
        if (it == locals_.end())
            throw std::runtime_error("undefined variable: " + v->name);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = load i32, ptr " << it->second << "\n";
        return tmp;
    }

    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        std::string left  = emitExpr(*b->left);
        std::string right = emitExpr(*b->right);
        std::string tmp   = newTmp();
        std::string instr;
        if      (b->op == "+") instr = "add";
        else if (b->op == "-") instr = "sub";
        else if (b->op == "*") instr = "mul";
        else if (b->op == "/") instr = "sdiv";
        else if (b->op == "%") instr = "srem";
        else throw std::runtime_error("unknown operator: " + b->op);
        out_ << "    " << tmp << " = " << instr << " i32 " << left << ", " << right << "\n";
        return tmp;
    }

    if (auto* s = dynamic_cast<const StringLiteralExpr*>(&expr)) {
        std::string label = "@.str" + std::to_string(str_counter_++);
        std::string full = s->value + "\n";
        int len;
        llvmEscape(full, len);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr " << label << ", i32 0, i32 0\n";
        return tmp;
    }

    throw std::runtime_error("unsupported expression type");
}

void Codegen::emitStmt(const Stmt& stmt) {
    if (auto* decl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        // alloca for the variable
        std::string alloca_reg = "%var_" + decl->name;
        out_ << "    " << alloca_reg << " = alloca i32\n";
        locals_[decl->name] = alloca_reg;
        // evaluate init and store
        std::string val = emitExpr(*decl->init);
        out_ << "    store i32 " << val << ", ptr " << alloca_reg << "\n";
        return;
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
        auto it = locals_.find(assign->name);
        if (it == locals_.end())
            throw std::runtime_error("undefined variable: " + assign->name);
        std::string val = emitExpr(*assign->value);
        out_ << "    store i32 " << val << ", ptr " << it->second << "\n";
        return;
    }

    if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        std::string val = emitExpr(*ret->value);
        out_ << "    ret i32 " << val << "\n";
        return;
    }

    if (auto* call = dynamic_cast<const CallStmt*>(&stmt)) {
        if (call->callee == "println") {
            if (call->args.size() != 1)
                throw std::runtime_error("println expects exactly 1 argument");

            // string literal
            if (auto* s = dynamic_cast<const StringLiteralExpr*>(call->args[0].get())) {
                std::string label = "@.str" + std::to_string(str_counter_++);
                std::string full = s->value + "\n";
                int len;
                llvmEscape(full, len);
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
                     << label << ", i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                return;
            }

            // integer expression — use %d format
            std::string val = emitExpr(*call->args[0]);
            std::string fmt = newTmp();
            out_ << "    " << fmt << " = getelementptr [4 x i8], ptr @.fmt_int, i32 0, i32 0\n";
            out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", i32 " << val << ")\n";
            return;
        }
        throw std::runtime_error("unknown function: " + call->callee);
    }

    throw std::runtime_error("unsupported statement type");
}
