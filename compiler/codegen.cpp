#include "codegen.h"
#include <sstream>
#include <functional>
#include <stdexcept>
#include "codegen_helpers.h"

Codegen::Codegen(const Program& program, std::ostream& out)
    : program_(program), out_(out), str_counter_(0), tmp_counter_(0), label_counter_(0) {}

std::string Codegen::newTmp() { return "%t" + std::to_string(tmp_counter_++); }
std::string Codegen::newLabel(const std::string& p) { return p + std::to_string(label_counter_++); }


std::string Codegen::llvmType(const std::string& t) {
    if (!t.empty() && t[0] == '{') return t; // pass through LLVM struct types (tuple returns)
    if (t == "int32" || t == "int" || t == "bool") return "i32";
    if (t == "int64") return "i64";
    if (t == "int16") return "i16";
    if (t == "int8")  return "i8";
    if (t == "uint8" || t == "char") return "i8";
    if (t == "void")  return "void";
    // reference (^) and pointer ([]) both lower to ptr in LLVM IR
    if (!t.empty() && t.back() == '^') return "ptr";
    if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return "ptr";
    return "i32";
}

void Codegen::collectFunctionSignatures() {
    auto buildTupleType = [&](const std::vector<std::pair<std::string,std::string>>& fields) {
        std::string s = "{ ";
        for (int i = 0; i < (int)fields.size(); i++) {
            if (i > 0) s += ", ";
            s += llvmType(fields[i].first);
        }
        return s + " }";
    };

    for (auto& fn : program_.functions) {
        if (!fn.tuple_return_fields.empty()) {
            std::string st = buildTupleType(fn.tuple_return_fields);
            func_return_types_[fn.name] = st;
            func_tuple_fields_[fn.name] = fn.tuple_return_fields;
        } else {
            func_return_types_[fn.name] = fn.return_type;
        }
        std::vector<std::string> ptypes;
        for (auto& [t, n] : fn.params) ptypes.push_back(t);
        func_param_types_[fn.name] = ptypes;

        if (!fn.body) continue; // forward declaration

        // recurse into all blocks to find nested function defs
        std::function<void(const BlockStmt&)> findNested = [&](const BlockStmt& block) {
            for (auto& stmt : block.stmts) {
                if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(stmt.get())) {
                    std::string mangled = fn.name + "__" + nfs->def.name;
                    std::string ret;
                    if (!nfs->def.tuple_return_fields.empty()) {
                        ret = buildTupleType(nfs->def.tuple_return_fields);
                        func_tuple_fields_[mangled] = nfs->def.tuple_return_fields;
                        func_tuple_fields_[nfs->def.name] = nfs->def.tuple_return_fields;
                    } else {
                        ret = nfs->def.return_type;
                    }
                    func_return_types_[mangled] = ret;
                    func_return_types_[nfs->def.name] = ret;
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
    // helper: mangling suffix for a single type (used when overloads exist)
    auto typeToken = [](const std::string& t) -> std::string {
        if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
            return t.substr(0, t.size()-2) + "s"; // char[] -> chars, int[] -> ints
        if (!t.empty() && t.back() == '^')
            return t.substr(0, t.size()-1) + "r";  // char^ -> charr
        return t; // int, char, bool, etc.
    };

    for (auto& slid : program_.slids) {
        // collect all method implementations (inline bodies + external defs), grouped by name
        struct Entry { std::string ret; std::vector<std::pair<std::string,std::string>> params; };
        std::map<std::string, std::vector<Entry>> by_name;
        for (auto& m : slid.methods) {
            if (!m.body) continue; // forward decl
            by_name[m.name].push_back({m.return_type, m.params});
        }
        for (auto& em : program_.external_methods) {
            if (em.slid_name != slid.name || !em.body) continue;
            if (em.method_name == "_" || em.method_name == "~") continue;
            by_name[em.method_name].push_back({em.return_type, em.params});
        }
        for (auto& [method_name, entries] : by_name) {
            std::string base = slid.name + "__" + method_name;
            bool overloaded = (entries.size() > 1);
            for (auto& e : entries) {
                std::string mangled = base;
                if (overloaded) {
                    for (auto& [t, n] : e.params) mangled += "__" + typeToken(t);
                }
                func_return_types_[mangled] = e.ret;
                std::vector<std::string> ptypes;
                for (auto& [t, n] : e.params) ptypes.push_back(t);
                func_param_types_[mangled] = ptypes;
                method_overloads_[base].push_back({mangled, ptypes});
            }
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
        } else if (auto* te = dynamic_cast<const TupleExpr*>(&expr)) {
            for (auto& v : te->values) scanExpr(*v);
        } else if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&expr)) {
            // find root var of the array index chain
            const Expr* cur = &expr;
            while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
                scanExpr(*a->index);
                cur = a->base.get();
            }
            if (auto* ve = dynamic_cast<const VarExpr*>(cur)) {
                if (parent_locals.count(ve->name) && !own_params.count(ve->name))
                    captures.insert(ve->name);
            }
            (void)ai;
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
        } else if (auto* td = dynamic_cast<const TupleDestructureStmt*>(&stmt)) {
            scanExpr(*td->init);
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
            else if (auto* a = dynamic_cast<const ArrayDeclStmt*>(stmt.get()))
                parent_locals.insert(a->name);
            else if (auto* td = dynamic_cast<const TupleDestructureStmt*>(stmt.get())) {
                for (auto& [type, name] : td->fields) parent_locals.insert(name);
            } else if (auto* f = dynamic_cast<const ForRangeStmt*>(stmt.get())) {
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
                // also exclude variables declared locally inside the nested function body
                std::function<void(const BlockStmt&)> collectNested = [&](const BlockStmt& b) {
                    for (auto& s : b.stmts) {
                        if (auto* d = dynamic_cast<const VarDeclStmt*>(s.get()))
                            own_params.insert(d->name);
                        else if (auto* a = dynamic_cast<const ArrayDeclStmt*>(s.get()))
                            own_params.insert(a->name);
                        else if (auto* td2 = dynamic_cast<const TupleDestructureStmt*>(s.get()))
                            for (auto& [t, n] : td2->fields) own_params.insert(n);
                        else if (auto* f2 = dynamic_cast<const ForRangeStmt*>(s.get())) {
                            own_params.insert(f2->var_name);
                            collectNested(*f2->body);
                        } else if (auto* w2 = dynamic_cast<const WhileStmt*>(s.get()))
                            collectNested(*w2->body);
                        else if (auto* i2 = dynamic_cast<const IfStmt*>(s.get())) {
                            collectNested(*i2->then_block);
                            if (i2->else_block) collectNested(*i2->else_block);
                        }
                    }
                };
                collectNested(*nfs->def.body);

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
        // also check external method defs for ctor/dtor
        for (auto& em : program_.external_methods) {
            if (em.slid_name != slid.name || !em.body) continue;
            if (em.method_name == "_") info.has_explicit_ctor = true;
            if (em.method_name == "~") info.has_dtor = true;
        }
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
            } else {
                // other function calls may pass string literals as args
                for (auto& arg : call->args)
                    collectExpr(arg.get(), false);
            }
        } else if (auto* mcs = dynamic_cast<const MethodCallStmt*>(&stmt)) {
            for (auto& arg : mcs->args)
                collectExpr(arg.get(), false);
        } else if (auto* decl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            // collect string literals used as initializers (e.g. char[] s = "hello")
            if (decl->init && dynamic_cast<const StringLiteralExpr*>(decl->init.get()))
                collectExpr(decl->init.get(), false);
        } else if (auto* b = dynamic_cast<const BlockStmt*>(&stmt)) {
            for (auto& s : b->stmts) collect(*s);
        } else if (auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
            for (auto& s : i->then_block->stmts) collect(*s);
            if (i->else_block) for (auto& s : i->else_block->stmts) collect(*s);
        } else if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
            for (auto& s : w->body->stmts) collect(*s);
        } else if (auto* f = dynamic_cast<const ForRangeStmt*>(&stmt)) {
            for (auto& s : f->body->stmts) collect(*s);
        } else if (auto* f = dynamic_cast<const ForEnumStmt*>(&stmt)) {
            for (auto& s : f->body->stmts) collect(*s);
        } else if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
            for (auto& sc : sw->cases)
                for (auto& s : sc.stmts) collect(*s);
        } else if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(&stmt)) {
            for (auto& s : nfs->def.body->stmts) collect(*s);
        }
    };

    for (auto& fn : program_.functions)
        if (fn.body)
            for (auto& stmt : fn.body->stmts) collect(*stmt);
    for (auto& slid : program_.slids) {
        if (slid.ctor_body)
            for (auto& stmt : slid.ctor_body->stmts) collect(*stmt);
        if (slid.explicit_ctor_body)
            for (auto& stmt : slid.explicit_ctor_body->stmts) collect(*stmt);
        if (slid.dtor_body)
            for (auto& stmt : slid.dtor_body->stmts) collect(*stmt);
        for (auto& m : slid.methods)
            if (m.body)
                for (auto& stmt : m.body->stmts) collect(*stmt);
    }
    for (auto& em : program_.external_methods) {
        if (em.body)
            for (auto& stmt : em.body->stmts) collect(*stmt);
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
        enum_sizes_[e.name] = (int)e.values.size();
    }

    // analyze nested functions for all top-level functions
    for (auto& fn : program_.functions)
        if (fn.body) analyzeNestedFunctions(fn);

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
        if (fn.body) emitFrameStruct(fn);

    if (!program_.slids.empty() || !program_.functions.empty()) out_ << "\n";

    // emit string constants
    for (auto& [label, value] : string_constants_) {
        int len;
        std::string escaped = llvmEscape(value, len);
        out_ << label << " = private constant [" << len << " x i8] c\"" << escaped << "\"\n";
    }

    out_ << "\n";
    out_ << "declare i32 @printf(ptr noundef, ...)\n";
    out_ << "declare ptr @malloc(i64)\n";
    out_ << "declare void @free(ptr)\n\n";
    out_ << "@.fmt_int    = private constant [4 x i8] c\"%d\\0A\\00\"\n";
    out_ << "@.fmt_int_nonl = private constant [3 x i8] c\"%d\\00\"\n";
    out_ << "@.fmt_str    = private constant [4 x i8] c\"%s\\0A\\00\"\n";
    out_ << "@.fmt_str_nonl = private constant [3 x i8] c\"%s\\00\"\n";
    out_ << "@.fmt_slice    = private constant [6 x i8] c\"%.*s\\0A\\00\"\n";
    out_ << "@.fmt_slice_nonl = private constant [5 x i8] c\"%.*s\\00\"\n";
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

    std::string mangled = parent_name + "__" + fn.name;
    std::string ret_type = llvmType(func_return_types_[mangled]);
    current_func_return_type_ = ret_type;

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

    // helper: after restoring a capture ptr, also restore array_info if it was a parent array
    auto restoreCapture = [&](const std::string& var, const std::string& ptr) {
        locals_[var] = ptr;
        if (parent_array_info_.count(var)) {
            ArrayInfo ainfo = parent_array_info_[var];
            ainfo.alloca_reg = ptr;
            array_info_[var] = ainfo;
        }
    };

    // set up access to captured variables
    if (info.captures.size() == 1) {
        // single capture — the ptr is passed directly
        restoreCapture(single_cap_var, "%cap_" + single_cap_var);
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
            restoreCapture(ordered_caps[i], ptr);
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
    // find ctor/dtor bodies — either inline or external
    const BlockStmt* ctor_body = slid.explicit_ctor_body.get();
    const BlockStmt* dtor_body = slid.dtor_body.get();
    for (auto& em : program_.external_methods) {
        if (em.slid_name != slid.name || !em.body) continue;
        if (em.method_name == "_")  ctor_body = em.body.get();
        if (em.method_name == "~")  dtor_body = em.body.get();
    }

    // emit explicit constructor: @ClassName__ctor(ptr %self)
    if (ctor_body) {
        locals_.clear();
        local_types_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;

        out_ << "define void @" << slid.name << "__ctor(ptr %self) {\n";
        out_ << "entry:\n";
        emitBlock(*ctor_body);
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    // emit destructor: @ClassName__dtor(ptr %self)
    if (dtor_body) {
        locals_.clear();
        local_types_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;

        out_ << "define void @" << slid.name << "__dtor(ptr %self) {\n";
        out_ << "entry:\n";
        emitBlock(*dtor_body);
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    current_slid_ = "";
}

std::string Codegen::resolveMethodMangledName(
    const std::string& slid_name,
    const std::string& method_name,
    const std::vector<std::pair<std::string,std::string>>& params)
{
    std::string base = slid_name + "__" + method_name;
    auto it = method_overloads_.find(base);
    if (it == method_overloads_.end() || it->second.empty()) return base;
    if (it->second.size() == 1) return it->second[0].first;
    // multiple overloads: match by exact param types
    std::vector<std::string> ptypes;
    for (auto& [t, n] : params) ptypes.push_back(t);
    for (auto& [mangled, mp] : it->second) {
        if (mp == ptypes) return mangled;
    }
    return base;
}

std::string Codegen::resolveOverloadForCall(
    const std::string& base_mangled,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    auto it = method_overloads_.find(base_mangled);
    if (it == method_overloads_.end() || it->second.empty()) return base_mangled;
    if (it->second.size() == 1) return it->second[0].first;
    // multiple overloads: match by pointer-vs-scalar for each arg position
    for (auto& [mangled, ptypes] : it->second) {
        if (ptypes.size() != args.size()) continue;
        bool match = true;
        for (int i = 0; i < (int)args.size(); i++) {
            bool param_ptr = isIndirectType(ptypes[i]);
            bool arg_ptr   = isPointerExpr(*args[i]);
            if (param_ptr != arg_ptr) { match = false; break; }
        }
        if (match) return mangled;
    }
    return base_mangled;
}

bool Codegen::isPointerExpr(const Expr& expr) {
    if (dynamic_cast<const StringLiteralExpr*>(&expr)) return true;
    if (dynamic_cast<const NullptrExpr*>(&expr))       return true;
    if (dynamic_cast<const AddrOfExpr*>(&expr))        return true;
    if (dynamic_cast<const NewExpr*>(&expr))           return true;
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                return isIndirectType(info.field_types[fit->second]);
        }
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end()) return isIndirectType(tit->second);
    }
    // field access through ptr dereference: sa^.storage_
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        std::string slid_name;
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = local_types_.find(ve2->name);
                if (tit != local_types_.end()) {
                    slid_name = tit->second;
                    if (isRefType(slid_name)) slid_name.pop_back();
                    else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                }
            }
        } else if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
            auto tit = local_types_.find(ve2->name);
            if (tit != local_types_.end()) slid_name = tit->second;
        }
        if (!slid_name.empty() && slid_info_.count(slid_name)) {
            auto& info = slid_info_[slid_name];
            auto fit = info.field_index.find(fa->field);
            if (fit != info.field_index.end())
                return isIndirectType(info.field_types[fit->second]);
        }
    }
    return false;
}

// Resolve which free function implements 'left op right'.
// Returns the function name (e.g. "op+") if found, empty string otherwise.
// Only matches if the operator's parameters are slid pointer types and the args
// are slid locals (i.e. it's actually a user-defined type operator, not int arithmetic).
std::string Codegen::resolveOperatorOverload(const std::string& op,
                                              const Expr& left, const Expr&) {
    std::string fname = "op" + op;
    auto pit = func_param_types_.find(fname);
    if (pit == func_param_types_.end() || pit->second.empty()) return "";
    // check if first param is a slid pointer type (e.g. "String^")
    const std::string& p0 = pit->second[0];
    std::string slid_name = p0;
    if (!slid_name.empty() && slid_name.back() == '^') slid_name.pop_back();
    else if (slid_name.size() >= 2 && slid_name.substr(slid_name.size()-2) == "[]") slid_name.resize(slid_name.size()-2);
    else return ""; // param is not a pointer — not a slid operator
    if (!slid_info_.count(slid_name)) return ""; // param type is not a known slid
    // check that left arg is actually a local of that slid type
    if (auto* ve = dynamic_cast<const VarExpr*>(&left)) {
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end() && tit->second == slid_name) return fname;
    }
    return "";
}

// Emit an argument expression, taking pointer-vs-value into account.
// If param_type ends with '^' and the arg is a slid local, pass its alloca ptr directly.
std::string Codegen::emitArgForParam(const Expr& arg, const std::string& param_type) {
    bool want_ptr = !param_type.empty() && param_type.back() == '^';
    if (want_ptr) {
        if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
            auto lit = local_types_.find(ve->name);
            if (lit != local_types_.end() && slid_info_.count(lit->second)) {
                return locals_.at(ve->name); // pass the alloca ptr
            }
        }
    }
    return emitExpr(arg);
}

void Codegen::emitSlidMethod(const SlidDef& slid, const std::string& full_mangled,
                              const std::string& return_type,
                              const std::vector<std::pair<std::string,std::string>>& params,
                              const BlockStmt& body) {
    locals_.clear();
    local_types_.clear();
    tmp_counter_ = 0;
    label_counter_ = 0;
    break_label_ = "";
    continue_label_ = "";
    current_slid_ = slid.name;
    block_terminated_ = false;

    std::string ret_type = llvmType(return_type);
    current_func_return_type_ = ret_type;

    std::string param_str = "ptr %self";
    for (auto& [type, name] : params)
        param_str += ", " + llvmType(type) + " %arg_" + name;

    out_ << "define " << ret_type << " @" << full_mangled
         << "(" << param_str << ") {\n";
    out_ << "entry:\n";

    for (auto& [type, name] : params) {
        std::string reg = "%var_" + name;
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        locals_[name] = reg;
        local_types_[name] = type;
    }

    emitBlock(body);

    if (return_type == "void" && !block_terminated_)
        out_ << "    ret void\n";

    out_ << "}\n\n";
}

void Codegen::emitSlidMethods(const SlidDef& slid) {
    // emit inline methods that have a body (not forward decls)
    for (auto& m : slid.methods) {
        if (!m.body) continue;
        std::string mangled = resolveMethodMangledName(slid.name, m.name, m.params);
        emitSlidMethod(slid, mangled, m.return_type, m.params, *m.body);
    }
    // emit external method definitions for this slid
    for (auto& em : program_.external_methods) {
        if (em.slid_name != slid.name || !em.body) continue;
        if (em.method_name == "_" || em.method_name == "~") continue;
        std::string mangled = resolveMethodMangledName(slid.name, em.method_name, em.params);
        emitSlidMethod(slid, mangled, em.return_type, em.params, *em.body);
    }
    current_slid_ = "";
}

void Codegen::emitFunction(const FunctionDef& fn) {
    if (!fn.body) return; // forward declaration

    locals_.clear();
    local_types_.clear();
    array_info_.clear();
    parent_array_info_.clear();
    dtor_vars_.clear();
    tmp_counter_ = 0;
    label_counter_ = 0;
    break_label_ = "";
    continue_label_ = "";
    current_slid_ = "";
    current_parent_ = fn.name;
    frame_ptr_reg_ = "";
    block_terminated_ = false;

    // detect sret: return type is a slid type
    bool uses_sret = !fn.return_type.empty() && slid_info_.count(fn.return_type) > 0;
    current_func_uses_sret_ = uses_sret;

    std::string ret_type = uses_sret ? "void" : llvmType(func_return_types_[fn.name]);
    current_func_return_type_ = ret_type;

    std::string param_str;
    if (uses_sret) {
        param_str = "ptr sret(%struct." + fn.return_type + ") %retval";
    }
    for (int i = 0; i < (int)fn.params.size(); i++) {
        if (!param_str.empty()) param_str += ", ";
        param_str += llvmType(fn.params[i].first) + " %arg_" + fn.params[i].second;
    }

    out_ << "define " << ret_type << " @" << llvmGlobalName(fn.name) << "(" << param_str << ") {\n";
    out_ << "entry:\n";

    for (auto& [type, name] : fn.params) {
        std::string reg = "%var_" + name;
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        locals_[name] = reg;
        local_types_[name] = type;
    }

    emitBlock(*fn.body);

    if (!block_terminated_) {
        if (fn.return_type == "void" || uses_sret) {
            emitDtors();
            out_ << "    ret void\n";
        } else {
            throw std::runtime_error("function '" + fn.name + "' is missing a return statement");
        }
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
