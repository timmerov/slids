#include "codegen.h"
#include <sstream>
#include <functional>
#include <stdexcept>

Codegen::Codegen(const Program& program, std::ostream& out)
    : program_(program), out_(out), str_counter_(0), tmp_counter_(0), label_counter_(0) {}

std::string Codegen::newTmp() {
    return "%t" + std::to_string(tmp_counter_++);
}

std::string Codegen::newLabel(const std::string& prefix) {
    return prefix + std::to_string(label_counter_++);
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
    // recursively collect string literals from all statements
    std::function<void(const Stmt&)> collect = [&](const Stmt& stmt) {
        if (auto* call = dynamic_cast<const CallStmt*>(&stmt)) {
            bool newline = (call->callee == "println");
            for (auto& arg : call->args)
                if (auto* s = dynamic_cast<const StringLiteralExpr*>(arg.get())) {
                    std::string full = newline ? s->value + "\n" : s->value;
                    string_constants_.emplace_back(
                        "@.str" + std::to_string(str_counter_++), full);
                }
        } else if (auto* b = dynamic_cast<const BlockStmt*>(&stmt)) {
            for (auto& s : b->stmts) collect(*s);
        } else if (auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
            for (auto& s : i->then_block->stmts) collect(*s);
            if (i->else_block)
                for (auto& s : i->else_block->stmts) collect(*s);
        } else if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
            for (auto& s : w->body->stmts) collect(*s);
        } else if (auto* f = dynamic_cast<const ForRangeStmt*>(&stmt)) {
            for (auto& s : f->body->stmts) collect(*s);
        }
    };

    for (auto& fn : program_.functions)
        for (auto& stmt : fn.body->stmts)
            collect(*stmt);
}

void Codegen::emit() {
    out_ << "target triple = \"x86_64-pc-linux-gnu\"\n\n";

    collectStringConstants();

    for (auto& [label, value] : string_constants_) {
        int len;
        std::string escaped = llvmEscape(value, len);
        out_ << label << " = private constant [" << len << " x i8] c\"" << escaped << "\"\n";
    }

    out_ << "\n";
    out_ << "declare i32 @printf(ptr noundef, ...)\n\n";
    out_ << "@.fmt_int = private constant [4 x i8] c\"%d\\0A\\00\"\n";
    out_ << "@.fmt_int_nonl = private constant [3 x i8] c\"%d\\00\"\n";
    out_ << "@.str_newline = private constant [2 x i8] c\"\\0A\\00\"\n\n";

    str_counter_ = 0;

    for (auto& fn : program_.functions)
        emitFunction(fn);
}

void Codegen::emitFunction(const FunctionDef& fn) {
    locals_.clear();
    tmp_counter_ = 0;
    label_counter_ = 0;
    break_label_ = "";
    continue_label_ = "";

    std::string ret_type = (fn.return_type == "int32") ? "i32" : "void";
    out_ << "define " << ret_type << " @" << fn.name << "() {\n";
    out_ << "entry:\n";

    emitBlock(*fn.body);

    out_ << "}\n\n";
}

void Codegen::emitBlock(const BlockStmt& block) {
    for (auto& stmt : block.stmts)
        emitStmt(*stmt);
}

std::string Codegen::emitExpr(const Expr& expr) {
    if (auto* i = dynamic_cast<const IntLiteralExpr*>(&expr))
        return std::to_string(i->value);

    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
        auto it = locals_.find(v->name);
        if (it == locals_.end())
            throw std::runtime_error("undefined variable: " + v->name);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = load i32, ptr " << it->second << "\n";
        return tmp;
    }

    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
        std::string val = emitExpr(*u->operand);
        std::string tmp = newTmp();
        if (u->op == "!") {
            // !x == (x == 0)
            out_ << "    " << tmp << " = icmp eq i32 " << val << ", 0\n";
            std::string tmp2 = newTmp();
            out_ << "    " << tmp2 << " = zext i1 " << tmp << " to i32\n";
            return tmp2;
        }
        throw std::runtime_error("unknown unary op: " + u->op);
    }

    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        // short-circuit && and ||
        if (b->op == "&&" || b->op == "||") {
            std::string result_ptr = newTmp() + "_sc";
            out_ << "    " << result_ptr << " = alloca i32\n";

            std::string left_val = emitExpr(*b->left);
            std::string left_bool = newTmp();
            out_ << "    " << left_bool << " = icmp ne i32 " << left_val << ", 0\n";

            std::string eval_right = newLabel("sc_right");
            std::string done       = newLabel("sc_done");

            if (b->op == "&&") {
                // if left is false, skip right
                out_ << "    store i32 0, ptr " << result_ptr << "\n";
                out_ << "    br i1 " << left_bool << ", label %" << eval_right
                     << ", label %" << done << "\n";
            } else {
                // if left is true, skip right
                out_ << "    store i32 1, ptr " << result_ptr << "\n";
                out_ << "    br i1 " << left_bool << ", label %" << done
                     << ", label %" << eval_right << "\n";
            }

            out_ << eval_right << ":\n";
            std::string right_val = emitExpr(*b->right);
            std::string right_bool = newTmp();
            out_ << "    " << right_bool << " = icmp ne i32 " << right_val << ", 0\n";
            std::string right_int = newTmp();
            out_ << "    " << right_int << " = zext i1 " << right_bool << " to i32\n";
            out_ << "    store i32 " << right_int << ", ptr " << result_ptr << "\n";
            out_ << "    br label %" << done << "\n";

            out_ << done << ":\n";
            std::string result = newTmp();
            out_ << "    " << result << " = load i32, ptr " << result_ptr << "\n";
            return result;
        }

        std::string left  = emitExpr(*b->left);
        std::string right = emitExpr(*b->right);
        std::string tmp   = newTmp();

        if      (b->op == "+")  { out_ << "    " << tmp << " = add i32 "  << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "-")  { out_ << "    " << tmp << " = sub i32 "  << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "*")  { out_ << "    " << tmp << " = mul i32 "  << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "/")  { out_ << "    " << tmp << " = sdiv i32 " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "%")  { out_ << "    " << tmp << " = srem i32 " << left << ", " << right << "\n"; return tmp; }

        // comparison — returns i1, extend to i32
        std::string cmp = newTmp();
        std::string pred;
        if      (b->op == "==") pred = "eq";
        else if (b->op == "!=") pred = "ne";
        else if (b->op == "<")  pred = "slt";
        else if (b->op == ">")  pred = "sgt";
        else if (b->op == "<=") pred = "sle";
        else if (b->op == ">=") pred = "sge";
        else throw std::runtime_error("unknown operator: " + b->op);

        out_ << "    " << cmp << " = icmp " << pred << " i32 " << left << ", " << right << "\n";
        out_ << "    " << tmp << " = zext i1 " << cmp << " to i32\n";
        return tmp;
    }

    if (auto* s = dynamic_cast<const StringLiteralExpr*>(&expr)) {
        std::string label = "@.str" + std::to_string(str_counter_++);
        std::string full = s->value + "\n";
        int len; llvmEscape(full, len);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
             << label << ", i32 0, i32 0\n";
        return tmp;
    }

    throw std::runtime_error("unsupported expression type");
}

void Codegen::emitStmt(const Stmt& stmt) {
    if (auto* decl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        std::string reg = "%var_" + decl->name;
        out_ << "    " << reg << " = alloca i32\n";
        locals_[decl->name] = reg;
        std::string val = emitExpr(*decl->init);
        out_ << "    store i32 " << val << ", ptr " << reg << "\n";
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

    if (dynamic_cast<const BreakStmt*>(&stmt)) {
        if (break_label_.empty())
            throw std::runtime_error("break outside of loop");
        out_ << "    br label %" << break_label_ << "\n";
        return;
    }

    if (dynamic_cast<const ContinueStmt*>(&stmt)) {
        if (continue_label_.empty())
            throw std::runtime_error("continue outside of loop");
        out_ << "    br label %" << continue_label_ << "\n";
        return;
    }

    if (auto* if_stmt = dynamic_cast<const IfStmt*>(&stmt)) {
        std::string then_lbl = newLabel("if_then");
        std::string else_lbl = newLabel("if_else");
        std::string end_lbl  = newLabel("if_end");

        std::string cond = emitExpr(*if_stmt->cond);
        std::string cond_bool = newTmp();
        out_ << "    " << cond_bool << " = icmp ne i32 " << cond << ", 0\n";
        out_ << "    br i1 " << cond_bool << ", label %" << then_lbl
             << ", label %" << (if_stmt->else_block ? else_lbl : end_lbl) << "\n";

        out_ << then_lbl << ":\n";
        emitBlock(*if_stmt->then_block);
        out_ << "    br label %" << end_lbl << "\n";

        if (if_stmt->else_block) {
            out_ << else_lbl << ":\n";
            emitBlock(*if_stmt->else_block);
            out_ << "    br label %" << end_lbl << "\n";
        }

        out_ << end_lbl << ":\n";
        return;
    }

    if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
        std::string cond_lbl = newLabel("while_cond");
        std::string body_lbl = newLabel("while_body");
        std::string end_lbl  = newLabel("while_end");

        std::string saved_break    = break_label_;
        std::string saved_continue = continue_label_;
        break_label_    = end_lbl;
        continue_label_ = cond_lbl;

        out_ << "    br label %" << cond_lbl << "\n";
        out_ << cond_lbl << ":\n";
        std::string cond = emitExpr(*w->cond);
        std::string cond_bool = newTmp();
        out_ << "    " << cond_bool << " = icmp ne i32 " << cond << ", 0\n";
        out_ << "    br i1 " << cond_bool << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

        out_ << body_lbl << ":\n";
        emitBlock(*w->body);
        out_ << "    br label %" << cond_lbl << "\n";

        out_ << end_lbl << ":\n";

        break_label_    = saved_break;
        continue_label_ = saved_continue;
        return;
    }

    if (auto* f = dynamic_cast<const ForRangeStmt*>(&stmt)) {
        std::string init_lbl = newLabel("for_init");
        std::string cond_lbl = newLabel("for_cond");
        std::string body_lbl = newLabel("for_body");
        std::string incr_lbl = newLabel("for_incr");
        std::string end_lbl  = newLabel("for_end");

        std::string saved_break    = break_label_;
        std::string saved_continue = continue_label_;
        break_label_    = end_lbl;
        continue_label_ = incr_lbl;

        // alloca for loop variable
        std::string var_reg = "%var_" + f->var_name;
        out_ << "    " << var_reg << " = alloca i32\n";
        locals_[f->var_name] = var_reg;

        // alloca for end value
        std::string end_reg = newTmp() + "_end";
        out_ << "    " << end_reg << " = alloca i32\n";

        // init
        out_ << "    br label %" << init_lbl << "\n";
        out_ << init_lbl << ":\n";
        std::string start_val = emitExpr(*f->range_start);
        out_ << "    store i32 " << start_val << ", ptr " << var_reg << "\n";
        std::string end_val = emitExpr(*f->range_end);
        out_ << "    store i32 " << end_val << ", ptr " << end_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        // cond: i < end
        out_ << cond_lbl << ":\n";
        std::string cur = newTmp();
        out_ << "    " << cur << " = load i32, ptr " << var_reg << "\n";
        std::string lim = newTmp();
        out_ << "    " << lim << " = load i32, ptr " << end_reg << "\n";
        std::string cmp = newTmp();
        out_ << "    " << cmp << " = icmp slt i32 " << cur << ", " << lim << "\n";
        out_ << "    br i1 " << cmp << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

        // body
        out_ << body_lbl << ":\n";
        emitBlock(*f->body);
        out_ << "    br label %" << incr_lbl << "\n";

        // increment
        out_ << incr_lbl << ":\n";
        std::string old_val = newTmp();
        out_ << "    " << old_val << " = load i32, ptr " << var_reg << "\n";
        std::string new_val = newTmp();
        out_ << "    " << new_val << " = add i32 " << old_val << ", 1\n";
        out_ << "    store i32 " << new_val << ", ptr " << var_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        out_ << end_lbl << ":\n";

        locals_.erase(f->var_name);
        break_label_    = saved_break;
        continue_label_ = saved_continue;
        return;
    }

    if (auto* call = dynamic_cast<const CallStmt*>(&stmt)) {
        if (call->callee == "println" || call->callee == "print") {
            bool newline = (call->callee == "println");
            if (call->args.size() > 1)
                throw std::runtime_error(call->callee + " expects 0 or 1 arguments");

            // println() with no args — just a newline
            if (call->args.size() == 0) {
                if (!newline)
                    throw std::runtime_error("print() requires an argument");
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = getelementptr [2 x i8], ptr @.str_newline, i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                return;
            }

            if (auto* s = dynamic_cast<const StringLiteralExpr*>(call->args[0].get())) {
                std::string label = "@.str" + std::to_string(str_counter_++);
                std::string full = newline ? s->value + "\n" : s->value;
                int len; llvmEscape(full, len);
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
                     << label << ", i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                return;
            }

            // integer expression
            std::string val = emitExpr(*call->args[0]);
            std::string fmt = newTmp();
            std::string fmt_name = newline ? "@.fmt_int" : "@.fmt_int_nonl";
            out_ << "    " << fmt << " = getelementptr [4 x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
            out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", i32 " << val << ")\n";
            return;
        }
        throw std::runtime_error("unknown function: " + call->callee);
    }

    throw std::runtime_error("unsupported statement type");
}
