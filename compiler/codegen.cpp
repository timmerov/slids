#include "codegen.h"
#include <sstream>
#include <functional>
#include <stdexcept>

Codegen::Codegen(const Program& program, std::ostream& out)
    : program_(program), out_(out), str_counter_(0), tmp_counter_(0), label_counter_(0) {}

std::string Codegen::newTmp() { return "%t" + std::to_string(tmp_counter_++); }
std::string Codegen::newLabel(const std::string& p) { return p + std::to_string(label_counter_++); }

std::string Codegen::llvmType(const std::string& t) {
    if (t == "int32" || t == "int" || t == "bool") return "i32";
    if (t == "int64") return "i64";
    if (t == "int16") return "i16";
    if (t == "int8")  return "i8";
    if (t == "void")  return "void";
    // any reference type (ends in ^) is a pointer
    if (!t.empty() && t.back() == '^') return "ptr";
    return "i32";
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

void Codegen::collectFunctionSignatures() {
    for (auto& fn : program_.functions) {
        func_return_types_[fn.name] = fn.return_type;
        std::vector<std::string> ptypes;
        for (auto& [t, n] : fn.params) ptypes.push_back(t);
        func_param_types_[fn.name] = ptypes;

        // recurse into all blocks to find nested function defs
        std::function<void(const BlockStmt&)> findNested = [&](const BlockStmt& block) {
            for (auto& stmt : block.stmts) {
                if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(stmt.get())) {
                    std::string mangled = fn.name + "__" + nfs->def.name;
                    func_return_types_[mangled] = nfs->def.return_type;
                    func_return_types_[nfs->def.name] = nfs->def.return_type;
                    std::vector<std::string> nptypes;
                    for (auto& [t, n] : nfs->def.params) nptypes.push_back(t);
                    func_param_types_[mangled] = nptypes;
                    func_param_types_[nfs->def.name] = nptypes;
                } else if (auto* f = dynamic_cast<const ForRangeStmt*>(stmt.get())) {
                    findNested(*f->body);
                } else if (auto* w = dynamic_cast<const WhileStmt*>(stmt.get())) {
                    findNested(*w->body);
                } else if (auto* i = dynamic_cast<const IfStmt*>(stmt.get())) {
                    findNested(*i->then_block);
                    if (i->else_block) findNested(*i->else_block);
                }
            }
        };
        findNested(*fn.body);
    }
    for (auto& slid : program_.slids) {
        for (auto& m : slid.methods) {
            std::string mangled = slid.name + "__" + m.name;
            func_return_types_[mangled] = m.return_type;
            std::vector<std::string> ptypes;
            for (auto& [t, n] : m.params) ptypes.push_back(t);
            func_param_types_[mangled] = ptypes;
        }
    }
}

std::set<std::string> Codegen::collectCaptures(
    const BlockStmt& body,
    const std::set<std::string>& parent_locals,
    const std::set<std::string>& own_params)
{
    std::set<std::string> captures;
    std::function<void(const Stmt&)> scanStmt;
    std::function<void(const Expr&)> scanExpr;

    scanExpr = [&](const Expr& expr) {
        if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
            if (parent_locals.count(v->name) && !own_params.count(v->name))
                captures.insert(v->name);
        } else if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
            scanExpr(*b->left); scanExpr(*b->right);
        } else if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
            scanExpr(*u->operand);
        } else if (auto* c = dynamic_cast<const CallExpr*>(&expr)) {
            for (auto& a : c->args) scanExpr(*a);
        } else if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
            scanExpr(*fa->object);
        } else if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
            scanExpr(*mc->object);
            for (auto& a : mc->args) scanExpr(*a);
        }
    };

    scanStmt = [&](const Stmt& stmt) {
        if (auto* a = dynamic_cast<const AssignStmt*>(&stmt)) {
            if (parent_locals.count(a->name) && !own_params.count(a->name))
                captures.insert(a->name);
            scanExpr(*a->value);
        } else if (auto* d = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            if (d->init) scanExpr(*d->init);
            for (auto& a : d->ctor_args) scanExpr(*a);
        } else if (auto* r = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (r->value) scanExpr(*r->value);
        } else if (auto* cs = dynamic_cast<const CallStmt*>(&stmt)) {
            for (auto& a : cs->args) scanExpr(*a);
        } else if (auto* ms = dynamic_cast<const MethodCallStmt*>(&stmt)) {
            scanExpr(*ms->object);
            for (auto& a : ms->args) scanExpr(*a);
        } else if (auto* fa = dynamic_cast<const FieldAssignStmt*>(&stmt)) {
            scanExpr(*fa->object); scanExpr(*fa->value);
        } else if (auto* b = dynamic_cast<const BlockStmt*>(&stmt)) {
            for (auto& s : b->stmts) scanStmt(*s);
        } else if (auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
            scanExpr(*i->cond);
            for (auto& s : i->then_block->stmts) scanStmt(*s);
            if (i->else_block) for (auto& s : i->else_block->stmts) scanStmt(*s);
        } else if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
            scanExpr(*w->cond);
            for (auto& s : w->body->stmts) scanStmt(*s);
        } else if (auto* f = dynamic_cast<const ForRangeStmt*>(&stmt)) {
            scanExpr(*f->range_start); scanExpr(*f->range_end);
            for (auto& s : f->body->stmts) scanStmt(*s);
        } else if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(&stmt)) {
            // don't recurse into nested function defs — they have their own scope
            (void)nfs;
        }
    };

    for (auto& stmt : body.stmts) scanStmt(*stmt);
    return captures;
}

void Codegen::analyzeNestedFunctions(const FunctionDef& fn) {
    // collect all locals declared anywhere in parent (including inside loops)
    std::set<std::string> parent_locals;
    for (auto& [type, name] : fn.params) parent_locals.insert(name);

    std::function<void(const BlockStmt&)> collectLocals = [&](const BlockStmt& block) {
        for (auto& stmt : block.stmts) {
            if (auto* d = dynamic_cast<const VarDeclStmt*>(stmt.get()))
                parent_locals.insert(d->name);
            else if (auto* f = dynamic_cast<const ForRangeStmt*>(stmt.get())) {
                parent_locals.insert(f->var_name);
                collectLocals(*f->body);
            } else if (auto* w = dynamic_cast<const WhileStmt*>(stmt.get()))
                collectLocals(*w->body);
            else if (auto* i = dynamic_cast<const IfStmt*>(stmt.get())) {
                collectLocals(*i->then_block);
                if (i->else_block) collectLocals(*i->else_block);
            }
        }
    };
    collectLocals(*fn.body);

    // find all nested function defs anywhere in body
    std::function<void(const BlockStmt&)> findNested = [&](const BlockStmt& block) {
        for (auto& stmt : block.stmts) {
            if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(stmt.get())) {
                std::set<std::string> own_params;
                for (auto& [type, name] : nfs->def.params) own_params.insert(name);

                auto captures = collectCaptures(*nfs->def.body, parent_locals, own_params);

                std::string mangled = fn.name + "__" + nfs->def.name;
                NestedFuncInfo info;
                info.mangled_name = mangled;
                info.captures = captures;
                info.parent_name = fn.name;
                nested_info_[nfs->def.name] = info;
                nested_info_[mangled] = info;
            } else if (auto* f = dynamic_cast<const ForRangeStmt*>(stmt.get())) {
                findNested(*f->body);
            } else if (auto* w = dynamic_cast<const WhileStmt*>(stmt.get())) {
                findNested(*w->body);
            } else if (auto* i = dynamic_cast<const IfStmt*>(stmt.get())) {
                findNested(*i->then_block);
                if (i->else_block) findNested(*i->else_block);
            }
        }
    };
    findNested(*fn.body);
}


void Codegen::collectSlids() {
    for (auto& slid : program_.slids) {
        SlidInfo info;
        info.name = slid.name;
        for (int i = 0; i < (int)slid.fields.size(); i++) {
            info.field_index[slid.fields[i].name] = i;
            info.field_types.push_back(slid.fields[i].type);
        }
        info.has_explicit_ctor = (slid.explicit_ctor_body != nullptr);
        info.has_dtor = (slid.dtor_body != nullptr);
        slid_info_[slid.name] = std::move(info);
    }
}

void Codegen::collectStringConstants() {
    // Flatten a "+" concat chain and collect all StringLiteralExpr leaves
    std::function<void(const Expr*, bool)> collectExpr = [&](const Expr* e, bool newline_on_last) {
        if (auto* b = dynamic_cast<const BinaryExpr*>(e)) {
            if (b->op == "+") {
                collectExpr(b->left.get(), false);
                collectExpr(b->right.get(), newline_on_last);
                return;
            }
        }
        if (auto* s = dynamic_cast<const StringLiteralExpr*>(e)) {
            std::string full = newline_on_last ? s->value + "\n" : s->value;
            string_constants_.emplace_back("@.str" + std::to_string(str_counter_++), full);
        }
        // integer exprs produce no string constants
    };

    std::function<void(const Stmt&)> collect = [&](const Stmt& stmt) {
        if (auto* call = dynamic_cast<const CallStmt*>(&stmt)) {
            if (call->callee == "println" || call->callee == "print") {
                bool newline = (call->callee == "println");
                for (auto& arg : call->args)
                    collectExpr(arg.get(), newline);
            }
        } else if (auto* b = dynamic_cast<const BlockStmt*>(&stmt)) {
            for (auto& s : b->stmts) collect(*s);
        } else if (auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
            for (auto& s : i->then_block->stmts) collect(*s);
            if (i->else_block) for (auto& s : i->else_block->stmts) collect(*s);
        } else if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
            for (auto& s : w->body->stmts) collect(*s);
        } else if (auto* f = dynamic_cast<const ForRangeStmt*>(&stmt)) {
            for (auto& s : f->body->stmts) collect(*s);
        } else if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(&stmt)) {
            for (auto& s : nfs->def.body->stmts) collect(*s);
        }
    };

    for (auto& fn : program_.functions)
        for (auto& stmt : fn.body->stmts) collect(*stmt);
    for (auto& slid : program_.slids) {
        if (slid.ctor_body)
            for (auto& stmt : slid.ctor_body->stmts) collect(*stmt);
        if (slid.explicit_ctor_body)
            for (auto& stmt : slid.explicit_ctor_body->stmts) collect(*stmt);
        if (slid.dtor_body)
            for (auto& stmt : slid.dtor_body->stmts) collect(*stmt);
        for (auto& m : slid.methods)
            for (auto& stmt : m.body->stmts) collect(*stmt);
    }
}

void Codegen::emit() {
    out_ << "target triple = \"x86_64-pc-linux-gnu\"\n\n";

    collectFunctionSignatures();
    collectSlids();
    collectStringConstants();

    // collect enum values
    for (auto& e : program_.enums) {
        for (int i = 0; i < (int)e.values.size(); i++)
            enum_values_[e.values[i]] = i;
    }

    // analyze nested functions for all top-level functions
    for (auto& fn : program_.functions)
        analyzeNestedFunctions(fn);

    // emit struct types for classes
    for (auto& slid : program_.slids) {
        auto& info = slid_info_[slid.name];
        out_ << "%struct." << slid.name << " = type { ";
        for (int i = 0; i < (int)info.field_types.size(); i++) {
            if (i > 0) out_ << ", ";
            out_ << llvmType(info.field_types[i]);
        }
        out_ << " }\n";
    }

    // emit frame struct types for functions with nested functions
    for (auto& fn : program_.functions)
        emitFrameStruct(fn);

    if (!program_.slids.empty() || !program_.functions.empty()) out_ << "\n";

    // emit string constants
    for (auto& [label, value] : string_constants_) {
        int len;
        std::string escaped = llvmEscape(value, len);
        out_ << label << " = private constant [" << len << " x i8] c\"" << escaped << "\"\n";
    }

    out_ << "\n";
    out_ << "declare i32 @printf(ptr noundef, ...)\n\n";
    out_ << "@.fmt_int    = private constant [4 x i8] c\"%d\\0A\\00\"\n";
    out_ << "@.fmt_int_nonl = private constant [3 x i8] c\"%d\\00\"\n";
    out_ << "@.str_newline = private constant [2 x i8] c\"\\0A\\00\"\n\n";

    str_counter_ = 0;

    for (auto& slid : program_.slids)
        emitSlidCtorDtor(slid);

    for (auto& slid : program_.slids)
        emitSlidMethods(slid);

    for (auto& fn : program_.functions)
        emitFunction(fn);
}

void Codegen::emitFrameStruct(const FunctionDef& fn) {
    bool has_frame = false;
    std::set<std::string> all_captures;

    std::function<void(const BlockStmt&)> scan = [&](const BlockStmt& block) {
        for (auto& stmt : block.stmts) {
            if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(stmt.get())) {
                auto it = nested_info_.find(nfs->def.name);
                if (it != nested_info_.end() && it->second.captures.size() >= 2) {
                    has_frame = true;
                    for (auto& c : it->second.captures)
                        all_captures.insert(c);
                }
            } else if (auto* f = dynamic_cast<const ForRangeStmt*>(stmt.get())) {
                scan(*f->body);
            } else if (auto* w = dynamic_cast<const WhileStmt*>(stmt.get())) {
                scan(*w->body);
            } else if (auto* i = dynamic_cast<const IfStmt*>(stmt.get())) {
                scan(*i->then_block);
                if (i->else_block) scan(*i->else_block);
            }
        }
    };
    scan(*fn.body);

    if (!has_frame) return;

    out_ << "%frame." << fn.name << " = type { ";
    bool first = true;
    for (size_t i = 0; i < all_captures.size(); i++) {
        if (!first) out_ << ", ";
        out_ << "ptr";
        first = false;
    }
    out_ << " }\n";
}

void Codegen::emitNestedFunction(
    const NestedFunctionDef& fn,
    const std::string& parent_name,
    const NestedFuncInfo& info)
{
    locals_.clear();
    local_types_.clear();
    array_info_.clear();
    dtor_vars_.clear();
    tmp_counter_ = 0;
    label_counter_ = 0;
    break_label_ = "";
    continue_label_ = "";
    current_slid_ = "";
    frame_ptr_reg_ = "";
    block_terminated_ = false;

    std::string ret_type = llvmType(fn.return_type);
    std::string mangled = parent_name + "__" + fn.name;

    // build parameter list
    std::string param_str;
    std::string single_cap_var;

    if (info.captures.size() == 0) {
        // no parent param needed
    } else if (info.captures.size() == 1) {
        // pass single var ptr directly
        single_cap_var = *info.captures.begin();
        param_str = "ptr %cap_" + single_cap_var;
    } else {
        // pass frame struct ptr
        param_str = "ptr %frame";
        frame_ptr_reg_ = "%frame";
    }

    // add explicit params
    for (auto& [type, name] : fn.params) {
        if (!param_str.empty()) param_str += ", ";
        param_str += llvmType(type) + " %arg_" + name;
    }

    out_ << "define " << ret_type << " @" << mangled << "(" << param_str << ") {\n";
    out_ << "entry:\n";

    // set up access to captured variables
    if (info.captures.size() == 1) {
        // single capture — the ptr is passed directly
        locals_[single_cap_var] = "%cap_" + single_cap_var;
    } else if (info.captures.size() >= 2) {
        // multi-capture — extract ptrs from frame struct
        // build ordered list of captures (must match frame struct order)
        std::vector<std::string> ordered_caps(info.captures.begin(), info.captures.end());
        for (int i = 0; i < (int)ordered_caps.size(); i++) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %frame." << parent_name
                 << ", ptr %frame, i32 0, i32 " << i << "\n";
            std::string ptr = newTmp();
            out_ << "    " << ptr << " = load ptr, ptr " << gep << "\n";
            locals_[ordered_caps[i]] = ptr;
        }
    }

    // alloca/store explicit params
    for (auto& [type, name] : fn.params) {
        std::string reg = "%var_" + name;
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        locals_[name] = reg;
        local_types_[name] = type;
    }

    emitBlock(*fn.body);

    if (fn.return_type == "void" && !block_terminated_) {
        emitDtors();
        out_ << "    ret void\n";
    }

    out_ << "}\n\n";
}


void Codegen::emitDtors() {
    // call dtors in reverse declaration order
    for (int i = (int)dtor_vars_.size() - 1; i >= 0; i--) {
        auto& [var_name, slid_type] = dtor_vars_[i];
        out_ << "    call void @" << slid_type << "__dtor(ptr " << locals_[var_name] << ")\n";
    }
}

void Codegen::emitSlidCtorDtor(const SlidDef& slid) {
    // emit explicit constructor: @ClassName__ctor(ptr %self)
    if (slid.explicit_ctor_body) {
        locals_.clear();
        local_types_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;

        out_ << "define void @" << slid.name << "__ctor(ptr %self) {\n";
        out_ << "entry:\n";
        emitBlock(*slid.explicit_ctor_body);
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    // emit destructor: @ClassName__dtor(ptr %self)
    if (slid.dtor_body) {
        locals_.clear();
        local_types_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;

        out_ << "define void @" << slid.name << "__dtor(ptr %self) {\n";
        out_ << "entry:\n";
        emitBlock(*slid.dtor_body);
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    current_slid_ = "";
}

void Codegen::emitSlidMethods(const SlidDef& slid) {
    for (auto& m : slid.methods) {
        locals_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        break_label_ = "";
        continue_label_ = "";
        current_slid_ = slid.name;
        block_terminated_ = false;

        std::string ret_type = llvmType(m.return_type);

        // build param list — self is first
        std::string param_str = "ptr %self";
        for (auto& [type, name] : m.params)
            param_str += ", " + llvmType(type) + " %arg_" + name;

        out_ << "define " << ret_type << " @" << slid.name << "__" << m.name
             << "(" << param_str << ") {\n";
        out_ << "entry:\n";

        // alloca/store params
        for (auto& [type, name] : m.params) {
            std::string reg = "%var_" + name;
            out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
            out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
            locals_[name] = reg;
            local_types_[name] = type;
        }

        emitBlock(*m.body);

        // add implicit ret void if needed
        if (m.return_type == "void" && !block_terminated_)
            out_ << "    ret void\n";

        out_ << "}\n\n";
    }
    current_slid_ = "";
}

void Codegen::emitFunction(const FunctionDef& fn) {
    locals_.clear();
    local_types_.clear();
    array_info_.clear();
    tmp_counter_ = 0;
    label_counter_ = 0;
    break_label_ = "";
    continue_label_ = "";
    current_slid_ = "";
    current_parent_ = fn.name;
    frame_ptr_reg_ = "";
    block_terminated_ = false;

    std::string ret_type = llvmType(fn.return_type);
    std::string param_str;
    for (int i = 0; i < (int)fn.params.size(); i++) {
        if (i > 0) param_str += ", ";
        param_str += llvmType(fn.params[i].first) + " %arg_" + fn.params[i].second;
    }

    out_ << "define " << ret_type << " @" << fn.name << "(" << param_str << ") {\n";
    out_ << "entry:\n";

    for (auto& [type, name] : fn.params) {
        std::string reg = "%var_" + name;
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        locals_[name] = reg;
        local_types_[name] = type;
    }

    emitBlock(*fn.body);

    if (fn.return_type == "void" && !block_terminated_) {
        emitDtors();
        out_ << "    ret void\n";
    }

    out_ << "}\n\n";

    // emit nested functions after parent
    std::function<void(const BlockStmt&)> emitNested = [&](const BlockStmt& block) {
        for (auto& stmt : block.stmts) {
            if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(stmt.get())) {
                auto it = nested_info_.find(nfs->def.name);
                if (it != nested_info_.end())
                    emitNestedFunction(nfs->def, fn.name, it->second);
            } else if (auto* f = dynamic_cast<const ForRangeStmt*>(stmt.get())) {
                emitNested(*f->body);
            } else if (auto* w = dynamic_cast<const WhileStmt*>(stmt.get())) {
                emitNested(*w->body);
            } else if (auto* i = dynamic_cast<const IfStmt*>(stmt.get())) {
                emitNested(*i->then_block);
                if (i->else_block) emitNested(*i->else_block);
            }
        }
    };
    emitNested(*fn.body);

    current_parent_ = "";
}


void Codegen::emitBlock(const BlockStmt& block) {
    for (auto& stmt : block.stmts) {
        if (block_terminated_) break; // dead code after terminator — skip
        emitStmt(*stmt);
    }
}

// returns the ptr to a field in a struct instance
std::string Codegen::emitFieldPtr(const std::string& obj_name, const std::string& field) {
    // find the slid type for this object
    auto type_it = local_types_.find(obj_name);
    if (type_it == local_types_.end())
        throw std::runtime_error("unknown type for variable: " + obj_name);
    std::string slid_name = type_it->second;

    auto& info = slid_info_[slid_name];
    auto field_it = info.field_index.find(field);
    if (field_it == info.field_index.end())
        throw std::runtime_error("unknown field: " + field + " in " + slid_name);

    int idx = field_it->second;
    std::string field_type = llvmType(info.field_types[idx]);

    std::string obj_ptr;
    auto loc_it = locals_.find(obj_name);
    if (loc_it != locals_.end()) {
        obj_ptr = loc_it->second;
    } else {
        throw std::runtime_error("undefined variable: " + obj_name);
    }

    std::string gep = newTmp();
    out_ << "    " << gep << " = getelementptr %struct." << slid_name
         << ", ptr " << obj_ptr << ", i32 0, i32 " << idx << "\n";
    return gep;
}

std::string Codegen::emitExpr(const Expr& expr) {
    if (auto* i = dynamic_cast<const IntLiteralExpr*>(&expr))
        return std::to_string(i->value);

    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
        // check if it's a field access via self in a method
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.field_index.count(v->name)) {
                int idx = info.field_index[v->name];
                std::string field_type = llvmType(info.field_types[idx]);
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                     << ", ptr %self, i32 0, i32 " << idx << "\n";
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = load " << field_type << ", ptr " << gep << "\n";
                return tmp;
            }
        }
        auto it = locals_.find(v->name);
        if (it == locals_.end()) {
            // check if it's an enum value
            auto eit = enum_values_.find(v->name);
            if (eit != enum_values_.end())
                return std::to_string(eit->second);
            throw std::runtime_error("undefined variable: " + v->name);
        }
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = load i32, ptr " << it->second << "\n";
        return tmp;
    }

    if (dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        // support chained indexing: base[i][j] — base may be another ArrayIndexExpr
        // compute flat linear index and GEP into the alloca
        // collect index chain and base name
        std::vector<const Expr*> indices;
        const Expr* cur = &expr;
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
            indices.insert(indices.begin(), a->index.get());
            cur = a->base.get();
        }
        // cur is now the root VarExpr
        auto* ve = dynamic_cast<const VarExpr*>(cur);
        if (!ve) throw std::runtime_error("complex array base not supported");
        auto ait = array_info_.find(ve->name);
        if (ait == array_info_.end())
            throw std::runtime_error("undefined array: " + ve->name);
        auto& ainfo = ait->second;
        // compute flat index: i*dim1 + j  (for 2D: row*cols + col)
        std::string flat = emitExpr(*indices[0]);
        for (int k = 1; k < (int)indices.size(); k++) {
            int stride = ainfo.dims[k];
            std::string stride_val = std::to_string(stride);
            std::string mul = newTmp();
            out_ << "    " << mul << " = mul i32 " << flat << ", " << stride_val << "\n";
            std::string idx_val = emitExpr(*indices[k]);
            std::string add = newTmp();
            out_ << "    " << add << " = add i32 " << mul << ", " << idx_val << "\n";
            flat = add;
        }
        std::string gep = newTmp();
        int total = 1;
        for (int d : ainfo.dims) total *= d;
        out_ << "    " << gep << " = getelementptr [" << total << " x i32], ptr "
             << ainfo.alloca_reg << ", i32 0, i32 " << flat << "\n";
        std::string val = newTmp();
        out_ << "    " << val << " = load i32, ptr " << gep << "\n";
        return val;
    }

    if (auto* ao = dynamic_cast<const AddrOfExpr*>(&expr)) {
        // ^x — return the alloca register for x (its address)
        if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                throw std::runtime_error("AddrOf: undefined variable '" + ve->name + "'");
            return it->second; // the alloca ptr IS the address
        }
        throw std::runtime_error("AddrOf: unsupported operand");
    }

    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        // ptr^ — first load the pointer from its alloca, then load through it
        std::string pointee_llvm = "i32"; // default pointee type
        std::string ptr_reg;

        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                throw std::runtime_error("DerefExpr: undefined variable '" + ve->name + "'");
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && !tit->second.empty() && tit->second.back() == '^') {
                // variable holds a pointer — load the ptr, then load through it
                std::string loaded_ptr = newTmp();
                out_ << "    " << loaded_ptr << " = load ptr, ptr " << it->second << "\n";
                ptr_reg = loaded_ptr;
                std::string pointee_type = tit->second.substr(0, tit->second.size() - 1);
                pointee_llvm = llvmType(pointee_type);
            } else {
                // variable is a plain value used as ptr (e.g. parameter already holds ptr)
                ptr_reg = it->second;
            }
        } else {
            // general case: evaluate operand as expression to get a ptr
            ptr_reg = emitExpr(*de->operand);
        }

        std::string tmp = newTmp();
        out_ << "    " << tmp << " = load " << pointee_llvm << ", ptr " << ptr_reg << "\n";
        return tmp;
    }

    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        // handle ptr^.field — object is a DerefExpr
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            std::string ptr_val;
            std::string slid_name;
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto it = locals_.find(ve->name);
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end() && !tit->second.empty() && tit->second.back() == '^') {
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load ptr, ptr " << it->second << "\n";
                    ptr_val = loaded;
                    slid_name = tit->second.substr(0, tit->second.size() - 1);
                } else {
                    ptr_val = it->second;
                    if (tit != local_types_.end()) slid_name = tit->second;
                }
            } else {
                ptr_val = emitExpr(*de->operand);
            }
            if (slid_name.empty() || !slid_info_.count(slid_name))
                throw std::runtime_error("DerefFieldAccess: unknown slid type for field '" + fa->field + "'");
            auto& info = slid_info_[slid_name];
            auto fit = info.field_index.find(fa->field);
            if (fit == info.field_index.end())
                throw std::runtime_error("unknown field: " + fa->field);
            int idx = fit->second;
            std::string field_type = llvmType(info.field_types[idx]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << ptr_val << ", i32 0, i32 " << idx << "\n";
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = load " << field_type << ", ptr " << gep << "\n";
            return tmp;
        }
        if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            std::string gep = emitFieldPtr(ve->name, fa->field);
            auto type_it = local_types_.find(ve->name);
            std::string slid_name = type_it->second;
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            std::string field_type = llvmType(info.field_types[idx]);
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = load " << field_type << ", ptr " << gep << "\n";
            return tmp;
        }
        throw std::runtime_error("complex field access not yet supported");
    }

    if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
        // helper to get slid_name and obj_ptr from any object expression
        std::string slid_name, obj_ptr;
        if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
            auto type_it = local_types_.find(ve->name);
            if (type_it == local_types_.end())
                throw std::runtime_error("unknown type for: " + ve->name);
            slid_name = type_it->second;
            obj_ptr = locals_[ve->name];
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mc->object.get())) {
            // ptr^.method() — load the pointer, use as self
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto type_it = local_types_.find(ve2->name);
                if (type_it == local_types_.end())
                    throw std::runtime_error("unknown type for: " + ve2->name);
                slid_name = type_it->second;
                if (!slid_name.empty() && slid_name.back() == '^') slid_name.pop_back();
                // load the pointer value from the alloca
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << locals_[ve2->name] << "\n";
                obj_ptr = loaded;
            }
        }
        if (!slid_name.empty()) {
            std::string mangled = slid_name + "__" + mc->method;
            auto ret_it = func_return_types_.find(mangled);
            if (ret_it == func_return_types_.end())
                throw std::runtime_error("unknown method: " + mc->method);
            std::string ret_type = llvmType(ret_it->second);
            std::string arg_str = "ptr " + obj_ptr;
            auto& mptypes = func_param_types_[mangled];
            for (int i = 0; i < (int)mc->args.size(); i++) {
                std::string ptype = (i < (int)mptypes.size()) ? llvmType(mptypes[i]) : "i32";
                arg_str += ", " + ptype + " " + emitExpr(*mc->args[i]);
            }
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = call " << ret_type << " @" << mangled
                 << "(" << arg_str << ")\n";
            return tmp;
        }
        throw std::runtime_error("complex method call not yet supported");
    }

    if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
        // check nested function first
        auto nit = nested_info_.find(call->callee);
        if (nit != nested_info_.end()) {
            auto& info = nit->second;
            std::string mangled = info.parent_name + "__" + call->callee;
            std::string ret_type = llvmType(func_return_types_[mangled]);

            std::string arg_str;
            if (info.captures.size() == 1) {
                std::string cap = *info.captures.begin();
                arg_str = "ptr " + locals_[cap];
            } else if (info.captures.size() >= 2) {
                std::string frame = newTmp() + "_frame";
                out_ << "    " << frame << " = alloca %frame." << info.parent_name << "\n";
                std::vector<std::string> ordered_caps(info.captures.begin(), info.captures.end());
                for (int i = 0; i < (int)ordered_caps.size(); i++) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %frame." << info.parent_name
                         << ", ptr " << frame << ", i32 0, i32 " << i << "\n";
                    out_ << "    store ptr " << locals_[ordered_caps[i]] << ", ptr " << gep << "\n";
                }
                arg_str = "ptr " + frame;
            }

            auto& nptypes_ce = func_param_types_[mangled];
            int ni_ce = 0;
            for (auto& arg : call->args) {
                if (!arg_str.empty()) arg_str += ", ";
                std::string ptype = (ni_ce < (int)nptypes_ce.size()) ? llvmType(nptypes_ce[ni_ce]) : "i32";
                arg_str += ptype + " " + emitExpr(*arg);
                ni_ce++;
            }

            std::string tmp = newTmp();
            out_ << "    " << tmp << " = call " << ret_type << " @" << mangled
                 << "(" << arg_str << ")\n";
            return tmp;
        }

        // regular function call
        auto it = func_return_types_.find(call->callee);
        if (it == func_return_types_.end())
            throw std::runtime_error("undefined function: " + call->callee);
        std::string ret_type = llvmType(it->second);
        std::string arg_str;
        auto& ptypes = func_param_types_[call->callee];
        for (int i = 0; i < (int)call->args.size(); i++) {
            if (i > 0) arg_str += ", ";
            std::string ptype = (i < (int)ptypes.size()) ? llvmType(ptypes[i]) : "i32";
            arg_str += ptype + " " + emitExpr(*call->args[i]);
        }
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = call " << ret_type << " @" << call->callee
             << "(" << arg_str << ")\n";
        return tmp;
    }

    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
        // pre/post increment/decrement — handle before evaluating operand
        if (u->op == "pre++" || u->op == "pre--" ||
            u->op == "post++" || u->op == "post--") {

            bool is_pre = (u->op == "pre++" || u->op == "pre--");
            std::string instr = (u->op == "pre++" || u->op == "post++") ? "add" : "sub";
            std::string ptr;

            // field access via self in a method
            if (!current_slid_.empty()) {
                auto* ve = dynamic_cast<const VarExpr*>(u->operand.get());
                if (ve) {
                    auto& info = slid_info_[current_slid_];
                    if (info.field_index.count(ve->name)) {
                        int idx = info.field_index[ve->name];
                        std::string gep = newTmp();
                        std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                        out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                             << ", ptr " << self << ", i32 0, i32 " << idx << "\n";
                        ptr = gep;
                    }
                }
            }

            // plain local variable
            if (ptr.empty()) {
                auto* ve = dynamic_cast<const VarExpr*>(u->operand.get());
                if (!ve) throw std::runtime_error("++/-- requires a variable");
                auto it = locals_.find(ve->name);
                if (it == locals_.end()) throw std::runtime_error("undefined variable: " + ve->name);
                ptr = it->second;
            }

            std::string old = newTmp();
            out_ << "    " << old << " = load i32, ptr " << ptr << "\n";
            std::string new_val = newTmp();
            out_ << "    " << new_val << " = " << instr << " i32 " << old << ", 1\n";
            out_ << "    store i32 " << new_val << ", ptr " << ptr << "\n";
            return is_pre ? new_val : old;
        }
        // other unary ops — evaluate operand first
        std::string val = emitExpr(*u->operand);
        std::string tmp = newTmp();
        if (u->op == "!") {
            out_ << "    " << tmp << " = icmp eq i32 " << val << ", 0\n";
            std::string tmp2 = newTmp();
            out_ << "    " << tmp2 << " = zext i1 " << tmp << " to i32\n";
            return tmp2;
        }
        if (u->op == "~") {
            out_ << "    " << tmp << " = xor i32 " << val << ", -1\n";
            return tmp;
        }
        throw std::runtime_error("unknown unary op: " + u->op);
    }

    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        if (b->op == "&&" || b->op == "||" || b->op == "^^") {
            std::string result_ptr = newTmp() + "_sc";
            out_ << "    " << result_ptr << " = alloca i32\n";
            std::string left_val = emitExpr(*b->left);
            std::string left_bool = newTmp();
            out_ << "    " << left_bool << " = icmp ne i32 " << left_val << ", 0\n";
            std::string eval_right = newLabel("sc_right");
            std::string done       = newLabel("sc_done");

            if (b->op == "&&") {
                out_ << "    store i32 0, ptr " << result_ptr << "\n";
                out_ << "    br i1 " << left_bool << ", label %" << eval_right
                     << ", label %" << done << "\n";
            } else if (b->op == "||") {
                out_ << "    store i32 1, ptr " << result_ptr << "\n";
                out_ << "    br i1 " << left_bool << ", label %" << done
                     << ", label %" << eval_right << "\n";
            } else {
                out_ << "    store i32 0, ptr " << result_ptr << "\n";
                out_ << "    br label %" << eval_right << "\n";
            }

            out_ << eval_right << ":\n";
            std::string right_val = emitExpr(*b->right);
            std::string right_bool = newTmp();
            out_ << "    " << right_bool << " = icmp ne i32 " << right_val << ", 0\n";
            std::string right_int = newTmp();
            if (b->op == "^^") {
                std::string xor_result = newTmp();
                out_ << "    " << xor_result << " = xor i1 " << left_bool << ", " << right_bool << "\n";
                out_ << "    " << right_int << " = zext i1 " << xor_result << " to i32\n";
            } else {
                out_ << "    " << right_int << " = zext i1 " << right_bool << " to i32\n";
            }
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
        else if (b->op == "&")  { out_ << "    " << tmp << " = and i32 "  << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "|")  { out_ << "    " << tmp << " = or i32 "   << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "^")  { out_ << "    " << tmp << " = xor i32 "  << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "<<") { out_ << "    " << tmp << " = shl i32 "  << left << ", " << right << "\n"; return tmp; }
        else if (b->op == ">>") { out_ << "    " << tmp << " = ashr i32 " << left << ", " << right << "\n"; return tmp; }

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
        int len; llvmEscape(s->value, len);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
             << label << ", i32 0, i32 0\n";
        return tmp;
    }

    throw std::runtime_error("unsupported expression type");
}

void Codegen::emitStmt(const Stmt& stmt) {
    if (auto* arr = dynamic_cast<const ArrayDeclStmt*>(&stmt)) {
        int total = 1;
        for (int d : arr->dims) total *= d;
        std::string reg = "%arr_" + arr->name;
        out_ << "    " << reg << " = alloca [" << total << " x i32]\n";
        ArrayInfo ainfo;
        ainfo.elem_type = arr->elem_type;
        ainfo.dims = arr->dims;
        ainfo.alloca_reg = reg;
        array_info_[arr->name] = ainfo;
        // store initializer values
        for (int i = 0; i < (int)arr->init_values.size(); i++) {
            std::string val = emitExpr(*arr->init_values[i]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << total << " x i32], ptr "
                 << reg << ", i32 0, i32 " << i << "\n";
            out_ << "    store i32 " << val << ", ptr " << gep << "\n";
        }
        return;
    }

    if (auto* decl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        // class instantiation
        if (slid_info_.count(decl->type)) {
            auto& info = slid_info_[decl->type];
            std::string reg = "%var_" + decl->name;
            out_ << "    " << reg << " = alloca %struct." << decl->type << "\n";
            locals_[decl->name] = reg;
            local_types_[decl->name] = decl->type;

            // find the SlidDef
            const SlidDef* slid_def = nullptr;
            for (auto& s : program_.slids)
                if (s.name == decl->type) { slid_def = &s; break; }

            // initialize fields with defaults or ctor args
            for (int i = 0; i < (int)info.field_types.size(); i++) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << decl->type
                     << ", ptr " << reg << ", i32 0, i32 " << i << "\n";

                std::string val;
                if (i < (int)decl->ctor_args.size()) {
                    val = emitExpr(*decl->ctor_args[i]);
                } else if (slid_def && slid_def->fields[i].default_val) {
                    val = emitExpr(*slid_def->fields[i].default_val);
                } else {
                    val = "0";
                }
                out_ << "    store " << llvmType(info.field_types[i])
                     << " " << val << ", ptr " << gep << "\n";
            }

            // run implicit constructor body if any (loose code in slid body)
            if (slid_def && slid_def->ctor_body) {
                std::string saved_slid = current_slid_;
                std::string saved_self = self_ptr_;
                current_slid_ = decl->type;
                self_ptr_ = reg;
                emitBlock(*slid_def->ctor_body);
                current_slid_ = saved_slid;
                self_ptr_ = saved_self;
            }

            // call explicit constructor __ctor if defined
            if (info.has_explicit_ctor) {
                out_ << "    call void @" << decl->type << "__ctor(ptr " << reg << ")\n";
            }

            // register for dtor call on scope exit
            if (info.has_dtor) {
                dtor_vars_.push_back({decl->name, decl->type});
            }
            return;
        }

        // primitive or reference variable declaration
        std::string reg = "%var_" + decl->name;
        std::string llvm_t = llvmType(decl->type);
        out_ << "    " << reg << " = alloca " << llvm_t << "\n";
        locals_[decl->name] = reg;
        local_types_[decl->name] = decl->type;
        std::string val = emitExpr(*decl->init);
        out_ << "    store " << llvm_t << " " << val << ", ptr " << reg << "\n";
        return;
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
        // check if it's a field via self in a method
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.field_index.count(assign->name)) {
                int idx = info.field_index[assign->name];
                std::string field_type = llvmType(info.field_types[idx]);
                std::string gep = newTmp();
                std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                     << ", ptr " << self << ", i32 0, i32 " << idx << "\n";
                std::string val = emitExpr(*assign->value);
                out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
                return;
            }
        }
        auto it = locals_.find(assign->name);
        if (it == locals_.end())
            throw std::runtime_error("undefined variable: " + assign->name);
        std::string val = emitExpr(*assign->value);
        out_ << "    store i32 " << val << ", ptr " << it->second << "\n";
        return;
    }

    if (auto* da = dynamic_cast<const DerefAssignStmt*>(&stmt)) {
        // ptr^ = val — load the pointer from its alloca, then store through it
        std::string ptr_reg;
        std::string pointee_llvm = "i32";
        if (auto* ve = dynamic_cast<const VarExpr*>(da->ptr.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                throw std::runtime_error("DerefAssign: undefined variable '" + ve->name + "'");
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && !tit->second.empty() && tit->second.back() == '^') {
                std::string loaded_ptr = newTmp();
                out_ << "    " << loaded_ptr << " = load ptr, ptr " << it->second << "\n";
                ptr_reg = loaded_ptr;
                std::string pointee_type = tit->second.substr(0, tit->second.size() - 1);
                pointee_llvm = llvmType(pointee_type);
            } else {
                ptr_reg = it->second;
            }
        } else {
            ptr_reg = emitExpr(*da->ptr);
        }
        std::string val = emitExpr(*da->value);
        out_ << "    store " << pointee_llvm << " " << val << ", ptr " << ptr_reg << "\n";
        return;
    }

    if (auto* fa = dynamic_cast<const FieldAssignStmt*>(&stmt)) {
        // handle ptr^.field = val — object is a DerefExpr
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            std::string ptr_val;
            std::string slid_name;
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto it = locals_.find(ve->name);
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end() && !tit->second.empty() && tit->second.back() == '^') {
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load ptr, ptr " << it->second << "\n";
                    ptr_val = loaded;
                    slid_name = tit->second.substr(0, tit->second.size() - 1);
                } else {
                    ptr_val = it->second;
                    if (tit != local_types_.end()) slid_name = tit->second;
                }
            } else {
                ptr_val = emitExpr(*de->operand);
            }
            if (slid_name.empty() || !slid_info_.count(slid_name))
                throw std::runtime_error("DerefFieldAssign: unknown slid type for field '" + fa->field + "'");
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            std::string field_type = llvmType(info.field_types[idx]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << ptr_val << ", i32 0, i32 " << idx << "\n";
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
            return;
        }
        if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            std::string gep = emitFieldPtr(ve->name, fa->field);
            auto type_it = local_types_.find(ve->name);
            std::string slid_name = type_it->second;
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            std::string field_type = llvmType(info.field_types[idx]);
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
            return;
        }
        throw std::runtime_error("complex field assignment not yet supported");
    }

    if (auto* mcs = dynamic_cast<const MethodCallStmt*>(&stmt)) {
        std::string slid_name, obj_ptr;
        if (auto* ve = dynamic_cast<const VarExpr*>(mcs->object.get())) {
            auto type_it = local_types_.find(ve->name);
            if (type_it == local_types_.end())
                throw std::runtime_error("unknown type for: " + ve->name);
            slid_name = type_it->second;
            obj_ptr = locals_[ve->name];
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mcs->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto type_it = local_types_.find(ve2->name);
                if (type_it == local_types_.end())
                    throw std::runtime_error("unknown type for: " + ve2->name);
                slid_name = type_it->second;
                if (!slid_name.empty() && slid_name.back() == '^') slid_name.pop_back();
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << locals_[ve2->name] << "\n";
                obj_ptr = loaded;
            }
        }
        if (!slid_name.empty()) {
            std::string mangled = slid_name + "__" + mcs->method;
            auto ret_it = func_return_types_.find(mangled);
            if (ret_it == func_return_types_.end())
                throw std::runtime_error("unknown method: " + mcs->method);
            std::string ret_type = llvmType(ret_it->second);
            std::string arg_str = "ptr " + obj_ptr;
            for (auto& arg : mcs->args)
                arg_str += ", i32 " + emitExpr(*arg);
            if (ret_type == "void") {
                out_ << "    call void @" << mangled << "(" << arg_str << ")\n";
            } else {
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << mangled
                     << "(" << arg_str << ")\n";
            }
            return;
        }
        throw std::runtime_error("complex method call statement not yet supported");
    }

    if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        emitDtors();
        if (ret->value) {
            std::string val = emitExpr(*ret->value);
            out_ << "    ret i32 " << val << "\n";
        } else {
            out_ << "    ret void\n";
        }
        block_terminated_ = true;
        return;
    }

    // nested function definition — already emitted after parent, skip here
    if (dynamic_cast<const NestedFunctionDefStmt*>(&stmt)) return;

    if (auto* brk = dynamic_cast<const BreakStmt*>(&stmt)) {
        std::string target;
        if (!brk->label.empty()) {
            // named break: find the frame with this label
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].block_label == brk->label) {
                    target = loop_stack_[i].break_target;
                    break;
                }
            }
            if (target.empty())
                throw std::runtime_error("break: unknown label '" + brk->label + "'");
        } else if (brk->number > 0) {
            // numbered break: count outward N loop frames
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                count++;
                if (count == brk->number) {
                    target = loop_stack_[i].break_target;
                    break;
                }
            }
            if (target.empty())
                throw std::runtime_error("break " + std::to_string(brk->number) + ": not enough enclosing loops");
        } else {
            if (break_label_.empty()) throw std::runtime_error("break outside of loop");
            target = break_label_;
        }
        out_ << "    br label %" << target << "\n";
        block_terminated_ = true;
        return;
    }

    if (auto* cont = dynamic_cast<const ContinueStmt*>(&stmt)) {
        std::string target;
        if (!cont->label.empty()) {
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].block_label == cont->label) {
                    target = loop_stack_[i].continue_target;
                    break;
                }
            }
            if (target.empty())
                throw std::runtime_error("continue: unknown label '" + cont->label + "'");
        } else if (cont->number > 0) {
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (!loop_stack_[i].continue_target.empty()) {
                    count++;
                    if (count == cont->number) {
                        target = loop_stack_[i].continue_target;
                        break;
                    }
                }
            }
            if (target.empty())
                throw std::runtime_error("continue " + std::to_string(cont->number) + ": not enough enclosing loops");
        } else {
            if (continue_label_.empty()) throw std::runtime_error("continue outside of loop");
            target = continue_label_;
        }
        out_ << "    br label %" << target << "\n";
        block_terminated_ = true;
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
        block_terminated_ = false;
        out_ << then_lbl << ":\n";
        emitBlock(*if_stmt->then_block);
        if (!block_terminated_) out_ << "    br label %" << end_lbl << "\n";
        if (if_stmt->else_block) {
            block_terminated_ = false;
            out_ << else_lbl << ":\n";
            emitBlock(*if_stmt->else_block);
            if (!block_terminated_) out_ << "    br label %" << end_lbl << "\n";
        }
        block_terminated_ = false;
        out_ << end_lbl << ":\n";
        return;
    }

    if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
        std::string cond_lbl = newLabel("while_cond");
        std::string body_lbl = newLabel("while_body");
        std::string end_lbl  = newLabel("while_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl; continue_label_ = cond_lbl;
        loop_stack_.push_back({w->block_label, end_lbl, cond_lbl});
        out_ << "    br label %" << cond_lbl << "\n";
        block_terminated_ = false;
        out_ << cond_lbl << ":\n";
        std::string cond = emitExpr(*w->cond);
        std::string cond_bool = newTmp();
        out_ << "    " << cond_bool << " = icmp ne i32 " << cond << ", 0\n";
        out_ << "    br i1 " << cond_bool << ", label %" << body_lbl << ", label %" << end_lbl << "\n";
        block_terminated_ = false;
        out_ << body_lbl << ":\n";
        emitBlock(*w->body);
        if (!block_terminated_) out_ << "    br label %" << cond_lbl << "\n";
        block_terminated_ = false;
        out_ << end_lbl << ":\n";
        loop_stack_.pop_back();
        break_label_ = saved_break; continue_label_ = saved_continue;
        return;
    }

    if (auto* f = dynamic_cast<const ForRangeStmt*>(&stmt)) {
        std::string init_lbl = newLabel("for_init");
        std::string cond_lbl = newLabel("for_cond");
        std::string body_lbl = newLabel("for_body");
        std::string incr_lbl = newLabel("for_incr");
        std::string end_lbl  = newLabel("for_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl; continue_label_ = incr_lbl;
        loop_stack_.push_back({f->block_label, end_lbl, incr_lbl});

        std::string var_reg;
        bool new_var = !f->var_type.empty();
        if (new_var) {
            // declare new loop variable
            var_reg = "%var_" + f->var_name;
            out_ << "    " << var_reg << " = alloca i32\n";
            locals_[f->var_name] = var_reg;
        } else {
            // reuse existing variable
            auto it = locals_.find(f->var_name);
            if (it == locals_.end())
                throw std::runtime_error("for loop: undefined variable '" + f->var_name + "'");
            var_reg = it->second;
        }
        std::string end_reg = newTmp() + "_end";
        out_ << "    " << end_reg << " = alloca i32\n";

        out_ << "    br label %" << init_lbl << "\n";
        block_terminated_ = false;
        out_ << init_lbl << ":\n";
        std::string start_val = emitExpr(*f->range_start);
        out_ << "    store i32 " << start_val << ", ptr " << var_reg << "\n";
        std::string end_val = emitExpr(*f->range_end);
        out_ << "    store i32 " << end_val << ", ptr " << end_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << cond_lbl << ":\n";
        std::string cur = newTmp();
        out_ << "    " << cur << " = load i32, ptr " << var_reg << "\n";
        std::string lim = newTmp();
        out_ << "    " << lim << " = load i32, ptr " << end_reg << "\n";
        std::string cmp = newTmp();
        out_ << "    " << cmp << " = icmp slt i32 " << cur << ", " << lim << "\n";
        out_ << "    br i1 " << cmp << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

        block_terminated_ = false;
        out_ << body_lbl << ":\n";
        emitBlock(*f->body);
        if (!block_terminated_) out_ << "    br label %" << incr_lbl << "\n";

        block_terminated_ = false;
        out_ << incr_lbl << ":\n";
        std::string old_val = newTmp();
        out_ << "    " << old_val << " = load i32, ptr " << var_reg << "\n";
        std::string new_val = newTmp();
        out_ << "    " << new_val << " = add i32 " << old_val << ", 1\n";
        out_ << "    store i32 " << new_val << ", ptr " << var_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << end_lbl << ":\n";
        if (new_var) locals_.erase(f->var_name);
        loop_stack_.pop_back();
        break_label_ = saved_break; continue_label_ = saved_continue;
        return;
    }

    if (auto* call = dynamic_cast<const CallStmt*>(&stmt)) {
        if (call->callee == "println" || call->callee == "print") {
            bool newline = (call->callee == "println");
            if (call->args.size() > 1)
                throw std::runtime_error(call->callee + " expects 0 or 1 arguments");

            if (call->args.size() == 0) {
                if (!newline) throw std::runtime_error("print() requires an argument");
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = getelementptr [2 x i8], ptr @.str_newline, i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                return;
            }

            // Flatten a left-leaning "+" chain into segments.
            // Each segment is either a StringLiteralExpr or an integer expr.
            std::vector<const Expr*> segments;
            std::function<void(const Expr*)> flatten = [&](const Expr* e) {
                if (auto* b = dynamic_cast<const BinaryExpr*>(e)) {
                    if (b->op == "+") {
                        flatten(b->left.get());
                        flatten(b->right.get());
                        return;
                    }
                }
                segments.push_back(e);
            };
            flatten(call->args[0].get());

            bool is_concat = segments.size() > 1 ||
                (segments.size() == 1 && !dynamic_cast<const StringLiteralExpr*>(segments[0]));

            if (!is_concat && segments.size() == 1) {
                // single string literal
                if (auto* s = dynamic_cast<const StringLiteralExpr*>(segments[0])) {
                    std::string label = "@.str" + std::to_string(str_counter_++);
                    std::string full = newline ? s->value + "\n" : s->value;
                    int len; llvmEscape(full, len);
                    std::string tmp = newTmp();
                    out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
                         << label << ", i32 0, i32 0\n";
                    out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                    return;
                }
                // single integer expr
                std::string val = emitExpr(*segments[0]);
                std::string fmt = newTmp();
                std::string fmt_name = newline ? "@.fmt_int" : "@.fmt_int_nonl";
                out_ << "    " << fmt << " = getelementptr [4 x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", i32 " << val << ")\n";
                return;
            }

            // multi-segment: emit one printf per segment, newline only on last
            for (int si = 0; si < (int)segments.size(); si++) {
                bool last = (si == (int)segments.size() - 1);
                if (auto* s = dynamic_cast<const StringLiteralExpr*>(segments[si])) {
                    std::string full = (last && newline) ? s->value + "\n" : s->value;
                    std::string label = "@.str" + std::to_string(str_counter_++);
                    int len; llvmEscape(full, len);
                    std::string tmp = newTmp();
                    out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
                         << label << ", i32 0, i32 0\n";
                    out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                } else {
                    std::string val = emitExpr(*segments[si]);
                    std::string fmt = newTmp();
                    std::string fmt_name = (last && newline) ? "@.fmt_int" : "@.fmt_int_nonl";
                    out_ << "    " << fmt << " = getelementptr [4 x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                    out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", i32 " << val << ")\n";
                }
            }
            return;
        }

        // check if it's a nested function call
        auto nit = nested_info_.find(call->callee);
        if (nit != nested_info_.end()) {
            auto& info = nit->second;
            std::string mangled = info.parent_name + "__" + call->callee;
            std::string ret_type = llvmType(func_return_types_[mangled]);

            std::string arg_str;
            if (info.captures.size() == 1) {
                std::string cap = *info.captures.begin();
                arg_str = "ptr " + locals_[cap];
            } else if (info.captures.size() >= 2) {
                // build frame struct on stack and fill in ptrs
                std::string frame = newTmp() + "_frame";
                out_ << "    " << frame << " = alloca %frame." << info.parent_name << "\n";
                std::vector<std::string> ordered_caps(info.captures.begin(), info.captures.end());
                for (int i = 0; i < (int)ordered_caps.size(); i++) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %frame." << info.parent_name
                         << ", ptr " << frame << ", i32 0, i32 " << i << "\n";
                    out_ << "    store ptr " << locals_[ordered_caps[i]] << ", ptr " << gep << "\n";
                }
                arg_str = "ptr " + frame;
            }

            auto& nptypes_cs = func_param_types_[mangled];
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (!arg_str.empty()) arg_str += ", ";
                std::string ptype = (i < (int)nptypes_cs.size()) ? llvmType(nptypes_cs[i]) : "i32";
                arg_str += ptype + " " + emitExpr(*call->args[i]);
            }

            if (ret_type == "void") {
                out_ << "    call void @" << mangled << "(" << arg_str << ")\n";
            } else {
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << mangled
                     << "(" << arg_str << ")\n";
            }
            return;
        }

        // regular top-level function call as statement
        auto fit = func_return_types_.find(call->callee);
        if (fit != func_return_types_.end()) {
            std::string ret_type = llvmType(fit->second);
            auto& rptypes = func_param_types_[call->callee];
            std::string arg_str;
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (i > 0) arg_str += ", ";
                std::string ptype = (i < (int)rptypes.size()) ? llvmType(rptypes[i]) : "i32";
                arg_str += ptype + " " + emitExpr(*call->args[i]);
            }
            if (ret_type == "void") {
                out_ << "    call void @" << call->callee << "(" << arg_str << ")\n";
            } else {
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << call->callee
                     << "(" << arg_str << ")\n";
            }
            return;
        }

        throw std::runtime_error("unknown function: " + call->callee);
    }

    throw std::runtime_error("unsupported statement type");
}
