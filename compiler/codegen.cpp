#include "codegen.h"
#include <sstream>
#include <functional>
#include <stdexcept>
#include <set>
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
    if (t == "uint")   return "i32";
    if (t == "uint16") return "i16";
    if (t == "uint32") return "i32";
    if (t == "uint64") return "i64";
    if (t == "intptr") return "i64";
    if (t == "float32") return "float";
    if (t == "float64") return "double";
    if (t == "void")  return "void";
    // reference (^) and pointer ([]) both lower to ptr in LLVM IR
    if (!t.empty() && t.back() == '^') return "ptr";
    if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return "ptr";
    // inline fixed-size array: T[N] → [N x llvmType(T)]
    auto lb = t.rfind('[');
    if (lb != std::string::npos && lb > 0 && t.back() == ']') {
        std::string sz = t.substr(lb + 1, t.size() - lb - 2);
        if (!sz.empty() && std::all_of(sz.begin(), sz.end(), ::isdigit))
            return "[" + sz + " x " + llvmType(t.substr(0, lb)) + "]";
    }
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

    // detect which free function names have multiple definitions (overloads)
    // forward declarations imported from headers don't count toward the overload
    // total when a bodied definition exists in the same file — otherwise the
    // implementation file's own functions would get spurious mangled names.
    std::set<std::string> names_with_body;
    for (auto& fn : program_.functions)
        if (fn.body) names_with_body.insert(fn.name);
    std::map<std::string, int> func_name_count;
    for (auto& fn : program_.functions)
        if (fn.body || !names_with_body.count(fn.name)) func_name_count[fn.name]++;

    // helper: mangling suffix for a single type (duplicated here for use before typeToken lambda)
    auto typeToken2 = [](const std::string& t) -> std::string {
        if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
            return t.substr(0, t.size()-2) + "s";
        if (!t.empty() && t.back() == '^')
            return t.substr(0, t.size()-1) + "r";
        return t;
    };

    for (auto& fn : program_.functions) {
        if (!fn.type_params.empty()) {
            // template function — store for lazy instantiation, skip normal registration
            template_funcs_[fn.name] = &fn;
            continue;
        }
        bool overloaded = (func_name_count[fn.name] > 1);
        std::vector<std::string> ptypes;
        for (auto& [t, n] : fn.params) ptypes.push_back(t);

        // compute mangled name (append param type tokens when overloaded)
        std::string mangled = fn.name;
        if (overloaded) {
            for (auto& t : ptypes) mangled += "__" + typeToken2(t);
        }
        free_func_overloads_[fn.name].push_back({mangled, ptypes});

        if (!fn.tuple_return_fields.empty()) {
            std::string st = buildTupleType(fn.tuple_return_fields);
            func_return_types_[mangled] = st;
            func_tuple_fields_[mangled] = fn.tuple_return_fields;
            if (!overloaded) { func_return_types_[fn.name] = st; func_tuple_fields_[fn.name] = fn.tuple_return_fields; }
        } else {
            func_return_types_[mangled] = fn.return_type;
            if (!overloaded) func_return_types_[fn.name] = fn.return_type;
        }
        func_param_types_[mangled] = ptypes;
        if (!overloaded) func_param_types_[fn.name] = ptypes;

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
        // build set of method names covered by external implementations
        // so forward decls from an imported header don't get double-counted as overloads
        std::set<std::string> covered_by_external;
        for (auto& em : program_.external_methods) {
            if (em.slid_name != slid.name || !em.body) continue;
            if (em.method_name == "_" || em.method_name == "~") continue;
            covered_by_external.insert(em.method_name);
        }

        std::map<std::string, std::vector<Entry>> by_name;
        for (auto& m : slid.methods) {
            // skip bodyless forward decls when the external method already provides the impl
            if (!m.body && covered_by_external.count(m.name)) continue;
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


// Compute byte size of a Slids type for struct layout (LLVM natural alignment rules).
// Pointers/iterators (types ending in ^ or []) are always 8 bytes on 64-bit.
static int64_t slidsTypeSize(const std::string& t) {
    if (t == "bool" || t == "int8" || t == "uint8" || t == "char") return 1;
    if (t == "int16" || t == "uint16") return 2;
    if (t == "int" || t == "int32" || t == "uint" || t == "uint32" || t == "float32") return 4;
    if (t == "int64" || t == "uint64" || t == "intptr" || t == "float64") return 8;
    // pointer / iterator types — and any unknown type (user struct, etc.)
    return 8;
}

static int64_t computeFieldsSize(const std::vector<FieldDef>& fields) {
    int64_t offset = 0;
    int64_t max_align = 1;
    for (auto& f : fields) {
        int64_t sz = slidsTypeSize(f.type);
        int64_t al = sz; // natural alignment = size for all primitives/pointers
        if (al > max_align) max_align = al;
        offset = (offset + al - 1) & ~(al - 1); // align up
        offset += sz;
    }
    // trailing pad to multiple of max_align
    return (offset + max_align - 1) & ~(max_align - 1);
}

void Codegen::collectSlids() {
    for (auto& slid : program_.slids) {
        SlidInfo info;
        info.name = slid.name;
        for (int i = 0; i < (int)slid.fields.size(); i++) {
            info.field_index[slid.fields[i].name] = i;
            info.field_types.push_back(slid.fields[i].type);
        }
        // annotated incomplete type seen by consumer: use __pinit for all initialization
        if (slid.sizeof_value > 0 && slid.has_ellipsis_suffix) {
            int64_t public_size = computeFieldsSize(slid.fields);
            info.sizeof_override = slid.sizeof_value;
            info.padding_bytes = slid.sizeof_value - public_size;
            if (info.padding_bytes < 0)
                throw std::runtime_error("sizeof annotation for '" + slid.name
                    + "' (" + std::to_string(slid.sizeof_value)
                    + ") is smaller than its public fields (" + std::to_string(public_size) + ")");
            info.has_pinit = true; // consumer always calls __pinit; __pinit chains to __ctor if needed
        }
        // transport impl: complete locally; emits __pinit for consumer (does NOT set has_pinit)
        if (slid.is_transport_impl) {
            info.is_transport_impl = true;
            info.public_field_count = slid.public_field_count;
        }
        // has_explicit_ctor_decl covers forward declarations too
        info.has_explicit_ctor = slid.has_explicit_ctor_decl || (slid.explicit_ctor_body != nullptr);
        info.has_dtor = (slid.dtor_body != nullptr) || slid.has_explicit_dtor_decl;
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
            if (call->callee == "__println" || call->callee == "__print") {
                bool newline = (call->callee == "__println");
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
        } else if (auto* as = dynamic_cast<const AssignStmt*>(&stmt)) {
            collectExpr(as->value.get(), false);
        } else if (auto* decl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            // collect string literals in initializer (direct literal, binary expr, etc.)
            if (decl->init)
                collectExpr(decl->init.get(), false);
            // collect string literals in constructor args (e.g. String s("hello"))
            for (auto& arg : decl->ctor_args)
                collectExpr(arg.get(), false);
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
        } else if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (ret->value) collectExpr(ret->value.get(), false);
        } else if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
            collectExpr(es->expr.get(), false);
        }
    };

    // collection order must match emission order exactly:
    // 1. all ctor/dtor bodies (emitSlidCtorDtor loop)
    // 2. all method bodies   (emitSlidMethods loop)
    // 3. all free functions  (emitFunction loop)
    for (auto& slid : program_.slids) {
        if (slid.ctor_body)
            for (auto& stmt : slid.ctor_body->stmts) collect(*stmt);
        if (slid.explicit_ctor_body)
            for (auto& stmt : slid.explicit_ctor_body->stmts) collect(*stmt);
        if (slid.dtor_body)
            for (auto& stmt : slid.dtor_body->stmts) collect(*stmt);
    }
    for (auto& slid : program_.slids) {
        for (auto& m : slid.methods)
            if (m.body)
                for (auto& stmt : m.body->stmts) collect(*stmt);
    }
    for (auto& em : program_.external_methods) {
        if (em.body)
            for (auto& stmt : em.body->stmts) collect(*stmt);
    }
    for (auto& fn : program_.functions)
        if (fn.body && fn.type_params.empty())
            for (auto& stmt : fn.body->stmts) collect(*stmt);
}

void Codegen::emit() {
    out_ << "target triple = \"x86_64-pc-linux-gnu\"\n\n";

    collectFunctionSignatures();
    collectSlids();

    // validate: class objects cannot be passed by value
    auto checkParams = [&](const std::string& ctx,
                           const std::vector<std::pair<std::string,std::string>>& params) {
        for (auto& [type, name] : params) {
            if (slid_info_.count(type) > 0)
                throw std::runtime_error(ctx + ": parameter '" + name +
                    "' has class type '" + type + "' — cannot pass by value; use '" + type + "^'");
        }
    };
    for (auto& fn : program_.functions)
        checkParams(fn.name, fn.params);
    for (auto& slid : program_.slids) {
        for (auto& m : slid.methods)
            checkParams(slid.name + "." + m.name, m.params);
    }
    for (auto& em : program_.external_methods)
        checkParams(em.slid_name + "." + em.method_name, em.params);

    collectStringConstants();

    // collect enum values
    for (auto& e : program_.enums) {
        for (int i = 0; i < (int)e.values.size(); i++)
            enum_values_[e.values[i]] = i;
        enum_sizes_[e.name] = (int)e.values.size();
    }

    // analyze nested functions for all top-level functions (skip templates)
    for (auto& fn : program_.functions)
        if (fn.body && fn.type_params.empty()) analyzeNestedFunctions(fn);

    // emit struct types for classes
    for (auto& slid : program_.slids) {
        auto& info = slid_info_[slid.name];
        out_ << "%struct." << slid.name << " = type { ";
        bool first = true;
        for (int i = 0; i < (int)info.field_types.size(); i++) {
            if (!first) out_ << ", ";
            first = false;
            out_ << llvmType(info.field_types[i]);
        }
        // for annotated incomplete types: append opaque padding bytes
        if (info.padding_bytes > 0) {
            if (!first) out_ << ", ";
            out_ << "[" << info.padding_bytes << " x i8]";
        }
        out_ << " }\n";
    }

    // emit frame struct types for functions with nested functions (skip templates)
    for (auto& fn : program_.functions)
        if (fn.body && fn.type_params.empty()) emitFrameStruct(fn);

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
    out_ << "declare void @free(ptr)\n";

    // emit declares for imported functions (bodyless forward declarations)
    // skip if a full definition exists in this translation unit
    std::set<std::string> has_body;
    for (auto& fn : program_.functions)
        if (fn.body) has_body.insert(fn.name);

    std::set<std::string> declared_fns;
    for (auto& fn : program_.functions) {
        if (fn.body) continue;
        if (has_body.count(fn.name)) continue; // defined locally — no declare needed

        // use the mangled name from free_func_overloads_ for correct quoting and overload suffix
        auto foit = free_func_overloads_.find(fn.name);
        if (foit == free_func_overloads_.end()) continue;
        for (auto& [mangled, ptypes] : foit->second) {
            if (!declared_fns.insert(mangled).second) continue; // already emitted
            std::string ret_type_str = func_return_types_[mangled];
            bool is_sret = !ret_type_str.empty() && slid_info_.count(ret_type_str);
            std::string ret = (is_sret || ret_type_str.empty()) ? "void" : llvmType(ret_type_str);
            out_ << "declare " << ret << " @" << llvmGlobalName(mangled) << "(";
            bool first = true;
            if (is_sret) {
                out_ << "ptr sret(%struct." << ret_type_str << ")";
                first = false;
            }
            for (auto& pt : ptypes) {
                if (!first) out_ << ", ";
                out_ << llvmType(pt);
                first = false;
            }
            out_ << ")\n";
        }
    }
    // emit declares for slid methods defined in other translation units
    // build set of locally-implemented method mangled names
    std::set<std::string> local_methods;
    for (auto& slid : program_.slids) {
        // inline ctor/dtor bodies count as locally defined
        if (slid.explicit_ctor_body) local_methods.insert(slid.name + "__ctor");
        if (slid.dtor_body)          local_methods.insert(slid.name + "__dtor");
        // transport impl slids locally define __pinit
        if (slid.is_transport_impl) local_methods.insert(slid.name + "__pinit");
        for (auto& m : slid.methods) {
            if (!m.body) continue;
            std::string base = slid.name + "__" + m.name;
            auto oit = method_overloads_.find(base);
            if (oit != method_overloads_.end())
                for (auto& [mn, _] : oit->second) local_methods.insert(mn);
        }
    }
    for (auto& em : program_.external_methods) {
        if (!em.body) continue;
        std::string base = em.slid_name + "__" + em.method_name;
        auto oit = method_overloads_.find(base);
        if (oit != method_overloads_.end())
            for (auto& [mn, _] : oit->second) local_methods.insert(mn);
        // also include ctor/dtor
        if (em.method_name == "_") local_methods.insert(em.slid_name + "__ctor");
        if (em.method_name == "~") local_methods.insert(em.slid_name + "__dtor");
    }
    for (auto& slid : program_.slids) {
        // ctor/dtor — declare if not locally defined
        auto& info = slid_info_[slid.name];
        if (info.has_explicit_ctor && !local_methods.count(slid.name + "__ctor"))
            out_ << "declare void @" << slid.name << "__ctor(ptr)\n";
        if (info.has_dtor && !local_methods.count(slid.name + "__dtor"))
            out_ << "declare void @" << slid.name << "__dtor(ptr)\n";
        if (info.has_pinit && !local_methods.count(slid.name + "__pinit"))
            out_ << "declare void @" << slid.name << "__pinit(ptr)\n";
        // regular methods: first arg is always ptr (self)
        for (auto& [base, overloads] : method_overloads_) {
            if (base.substr(0, slid.name.size() + 2) != slid.name + "__") continue;
            for (auto& [mangled, ptypes] : overloads) {
                if (local_methods.count(mangled)) continue;
                if (!declared_fns.insert(mangled).second) continue;
                std::string ret_type_str = func_return_types_[mangled];
                std::string ret = ret_type_str.empty() ? "void" : llvmType(ret_type_str);
                out_ << "declare " << ret << " @" << llvmGlobalName(mangled) << "(ptr";
                for (auto& pt : ptypes) out_ << ", " << llvmType(pt);
                out_ << ")\n";
            }
        }
    }

    out_ << "\n";
    out_ << "@.fmt_int    = private constant [4 x i8] c\"%d\\0A\\00\"\n";
    out_ << "@.fmt_int_nonl = private constant [3 x i8] c\"%d\\00\"\n";
    out_ << "@.fmt_uint    = private constant [4 x i8] c\"%u\\0A\\00\"\n";
    out_ << "@.fmt_uint_nonl = private constant [3 x i8] c\"%u\\00\"\n";
    out_ << "@.fmt_long    = private constant [5 x i8] c\"%ld\\0A\\00\"\n";
    out_ << "@.fmt_long_nonl = private constant [4 x i8] c\"%ld\\00\"\n";
    out_ << "@.fmt_str    = private constant [4 x i8] c\"%s\\0A\\00\"\n";
    out_ << "@.fmt_str_nonl = private constant [3 x i8] c\"%s\\00\"\n";
    out_ << "@.fmt_slice    = private constant [6 x i8] c\"%.*s\\0A\\00\"\n";
    out_ << "@.fmt_slice_nonl = private constant [5 x i8] c\"%.*s\\00\"\n";
    out_ << "@.fmt_double    = private constant [5 x i8] c\"%lf\\0A\\00\"\n";
    out_ << "@.fmt_double_nonl = private constant [4 x i8] c\"%lf\\00\"\n";
    out_ << "@.str_newline = private constant [2 x i8] c\"\\0A\\00\"\n\n";

    str_counter_ = 0;

    for (auto& slid : program_.slids)
        emitSlidCtorDtor(slid);

    for (auto& slid : program_.slids)
        emitSlidMethods(slid);

    for (auto& fn : program_.functions)
        if (fn.type_params.empty()) emitFunction(fn);

    // emit pending template instantiations (collected during function emission)
    for (auto& fn : pending_instantiations_)
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

    // emit __pinit for transport slids: initializes private fields, optionally chains to __ctor
    if (slid.is_transport_impl) {
        auto& info = slid_info_[slid.name];
        locals_.clear();
        local_types_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;
        self_ptr_ = "";  // field access uses %self fallback

        out_ << "define void @" << slid.name << "__pinit(ptr %self) {\n";
        out_ << "entry:\n";

        // store each private field at its struct index
        for (int i = slid.public_field_count; i < (int)slid.fields.size(); i++) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct." << slid.name
                 << ", ptr %self, i32 0, i32 " << i << "\n";
            std::string val;
            if (slid.fields[i].default_val) {
                val = emitExpr(*slid.fields[i].default_val);
            } else {
                val = "0";
            }
            out_ << "    store " << llvmType(info.field_types[i]) << " " << val << ", ptr " << gep << "\n";
        }

        if (info.has_explicit_ctor) {
            // chain to explicit ctor with musttail for zero overhead
            out_ << "    musttail call void @" << slid.name << "__ctor(ptr %self)\n";
        }
        out_ << "    ret void\n";
        out_ << "}\n\n";

        current_slid_ = "";
        self_ptr_ = "";
    }
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

bool Codegen::isUnsignedExpr(const Expr& expr) {
    static const std::set<std::string> utypes = {"uint","uint8","uint16","uint32","uint64","char"};

    // local variable or field via self
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                return utypes.count(info.field_types[fit->second]) > 0;
        }
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end()) return utypes.count(tit->second) > 0;
    }

    // field access: obj^.field_ or obj.field_
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        std::string slid_name;
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = local_types_.find(ve2->name);
                if (tit != local_types_.end()) {
                    slid_name = tit->second;
                    if (isRefType(slid_name))      slid_name.pop_back();
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
                return utypes.count(info.field_types[fit->second]) > 0;
        }
    }

    // type conversion to an unsigned type
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr)) {
        static const std::set<std::string> utypes_nc = {"uint","uint8","uint16","uint32","uint64","char"};
        return utypes_nc.count(nc->target_type) > 0;
    }

    // binary arithmetic result: unsigned if either operand is unsigned
    if (auto* be = dynamic_cast<const BinaryExpr*>(&expr)) {
        // comparisons and logical ops produce a signed i32 0/1
        if (be->op == "==" || be->op == "!=" || be->op == "<"  ||
            be->op == ">"  || be->op == "<=" || be->op == ">=" ||
            be->op == "&&" || be->op == "||" || be->op == "^^")
            return false;
        return isUnsignedExpr(*be->left) || isUnsignedExpr(*be->right);
    }

    return false;
}

// True when emitExpr on this expression returns a ptr to a freshly constructed
// temp alloca that we own and can mutate (e.g. call op+= on directly).
// Named variables are NOT fresh temps — they belong to the caller.
bool Codegen::isFreshSlidTemp(const Expr& expr) {
    // type name used as anonymous temporary (not bound in locals_)
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr))
        return locals_.find(ve->name) == locals_.end() && slid_info_.count(ve->name) > 0;
    // (SlidType=expr) always allocates a fresh temp
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr))
        return slid_info_.count(nc->target_type) > 0;
    // function call returning a slid type produces a fresh temp alloca
    if (dynamic_cast<const CallExpr*>(&expr))
        return !exprSlidType(expr).empty();
    // any binary expression that produces a slid result owns a fresh temp alloca
    if (dynamic_cast<const BinaryExpr*>(&expr))
        return !exprSlidType(expr).empty();
    return false;
}

// Return the slid type name if expr produces a slid-typed value, else "".
std::string Codegen::exprSlidType(const Expr& expr) {
    // (SlidType=expr) — the result is a ptr to a SlidType temp
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr))
        if (slid_info_.count(nc->target_type)) return nc->target_type;
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        if (ve->name == "self" && !current_slid_.empty()) return current_slid_;
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end() && slid_info_.count(tit->second))
            return tit->second;
        // type name used directly as an anonymous temp (not in locals_)
        if (tit == local_types_.end() && slid_info_.count(ve->name))
            return ve->name;
    }
    // DerefExpr: sa^ where sa: SlidType^ — produces SlidType
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                std::string t = tit->second;
                if (!t.empty() && t.back() == '^') { t.pop_back(); if (slid_info_.count(t)) return t; }
            }
        }
    }
    // function call returning a slid type
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        auto tit = template_funcs_.find(ce->callee);
        if (tit != template_funcs_.end()) {
            const FunctionDef& tmpl = *tit->second;
            std::vector<std::string> targs = ce->type_args.empty()
                ? inferTypeArgs(tmpl, ce->args) : ce->type_args;
            std::map<std::string,std::string> subst;
            for (int i = 0; i < (int)tmpl.type_params.size() && i < (int)targs.size(); i++)
                subst[tmpl.type_params[i]] = targs[i];
            auto it2 = subst.find(tmpl.return_type);
            std::string rt = (it2 != subst.end()) ? it2->second : tmpl.return_type;
            if (slid_info_.count(rt)) return rt;
        }
        auto rit = func_return_types_.find(ce->callee);
        if (rit != func_return_types_.end() && slid_info_.count(rit->second))
            return rit->second;
    }
    if (auto* be = dynamic_cast<const BinaryExpr*>(&expr)) {
        std::string mangled = resolveOperatorOverload(be->op, *be->left, *be->right);
        if (!mangled.empty()) {
            // method overload: return type is void, result slid is encoded in mangled name
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end() && rit->second == "void") {
                auto pos = mangled.find("__op");
                if (pos != std::string::npos) {
                    std::string slid = mangled.substr(0, pos);
                    if (slid_info_.count(slid)) return slid;
                }
            }
            // free function overload: return type is the slid name
            if (rit != func_return_types_.end() && slid_info_.count(rit->second))
                return rit->second;
            // fallback: infer from first param type
            auto pit = func_param_types_.find(mangled);
            if (pit != func_param_types_.end() && !pit->second.empty()) {
                std::string t = pit->second[0];
                if (!t.empty() && t.back() == '^') t.pop_back();
                else if (t.size() >= 2 && t.substr(t.size()-2) == "[]") t = t.substr(0, t.size()-2);
                if (slid_info_.count(t)) return t;
            }
        }
        // Phase 2: exact overload not found, but left is a slid and right can be coerced via op=
        std::string left_slid = exprSlidType(*be->left);
        if (!left_slid.empty()) {
            std::string coerce = resolveOpEq(left_slid + "__op=", *be->right);
            if (!coerce.empty() && method_overloads_.count(left_slid + "__op" + be->op))
                return left_slid;
        }
        // Phase 3: no op+, but op+= exists — temp = copy(left); temp op= right
        if (!left_slid.empty() && method_overloads_.count(left_slid + "__op" + be->op + "="))
            return left_slid;
    }
    return "";
}

// Resolve which free function implements 'left op right'.
// Returns the mangled function name (e.g. "op+__Stringr__char") if found, empty string otherwise.
// Matches when left is a slid local, a chained BinaryExpr, a DerefExpr of a slid pointer,
// or a string literal that can be implicitly converted to the slid type via op=.
std::string Codegen::resolveOperatorOverload(const std::string& op,
                                              const Expr& left, const Expr& right) {
    std::string fname = "op" + op;

    // helper: extract the slid type name from the left operand expression
    auto leftSlidName = [&]() -> std::string {
        if (auto* ve = dynamic_cast<const VarExpr*>(&left)) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                std::string t = tit->second;
                if (slid_info_.count(t)) return t;
                if (!t.empty() && t.back() == '^') { t.pop_back(); if (slid_info_.count(t)) return t; }
            }
            // type name used as anonymous temp
            else if (slid_info_.count(ve->name)) return ve->name;
        }
        if (auto* de = dynamic_cast<const DerefExpr*>(&left)) {
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) {
                    std::string t = tit->second;
                    if (!t.empty() && t.back() == '^') { t.pop_back(); if (slid_info_.count(t)) return t; }
                }
            }
        }
        if (dynamic_cast<const BinaryExpr*>(&left)) return exprSlidType(left);
        // StringLiteralExpr: find a slid type that has op=(char[]) — it can act as that type
        if (dynamic_cast<const StringLiteralExpr*>(&left)) {
            for (auto& [slid, info] : slid_info_) {
                auto oit = method_overloads_.find(slid + "__op=");
                if (oit == method_overloads_.end()) continue;
                for (auto& [m, ptypes] : oit->second)
                    if (!ptypes.empty() && isPtrType(ptypes[0])) return slid;
            }
        }
        return "";
    };

    // helper: does this expression match a slid-typed parameter (SlidType^)?
    auto leftMatchesSlid = [&](const std::string& slid_name) -> bool {
        // VarExpr: local variable of exact slid type, or type name used as anonymous temp
        if (auto* ve = dynamic_cast<const VarExpr*>(&left)) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && tit->second == slid_name) return true;
            if (tit == local_types_.end() && ve->name == slid_name) return true;
        }
        // DerefExpr: sa^ where sa: SlidType^
        if (auto* de = dynamic_cast<const DerefExpr*>(&left)) {
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) {
                    std::string t = tit->second;
                    if (!t.empty() && t.back() == '^') t.pop_back();
                    if (t == slid_name) return true;
                }
            }
        }
        // BinaryExpr: chained op result (e.g. (a+b)+c)
        if (dynamic_cast<const BinaryExpr*>(&left)) {
            if (exprSlidType(left) == slid_name) return true;
        }
        // StringLiteralExpr: allowed if slid type has op=(char[])
        if (dynamic_cast<const StringLiteralExpr*>(&left)) {
            auto oit = method_overloads_.find(slid_name + "__op=");
            if (oit != method_overloads_.end()) {
                for (auto& [m, ptypes] : oit->second)
                    if (!ptypes.empty() && isPtrType(ptypes[0])) return true;
            }
        }
        return false;
    };

    // helper: does the right operand match param type p1?
    auto rightMatchesParam = [&](const std::string& p1) -> bool {
        bool p1_is_slid_ref = isRefType(p1) && slid_info_.count(p1.substr(0, p1.size()-1));
        bool p1_is_ptr = isIndirectType(p1); // ^ or [] — any pointer/reference type
        // right is a string literal → matches slid ref (implicit temp) or ptr param
        if (dynamic_cast<const StringLiteralExpr*>(&right)) return p1_is_slid_ref || p1_is_ptr;
        // right is an integer/char literal → matches non-pointer param
        if (dynamic_cast<const IntLiteralExpr*>(&right)) return !p1_is_ptr;
        // right is a variable
        if (auto* ve = dynamic_cast<const VarExpr*>(&right)) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                std::string t = tit->second;
                bool is_slid = slid_info_.count(t) > 0;
                bool is_slid_ref = (!t.empty() && t.back() == '^' && slid_info_.count(t.substr(0, t.size()-1)));
                if (p1_is_slid_ref) return is_slid || is_slid_ref;
                return !is_slid && !is_slid_ref;
            }
        }
        // right is a DerefExpr → produces slid value
        if (auto* de = dynamic_cast<const DerefExpr*>(&right)) {
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) {
                    std::string t = tit->second;
                    if (!t.empty() && t.back() == '^') t.pop_back();
                    if (slid_info_.count(t) && p1_is_slid_ref) return true;
                }
            }
        }
        // default: if we can't tell, accept any single-overload or slid-ref param
        return p1_is_slid_ref;
    };

    // prefer in-class method overloads (new-style) over naked free functions (old-style)
    {
        std::string slid_name = leftSlidName();
        if (!slid_name.empty()) {
            std::string method_base = slid_name + "__" + fname;
            auto moit = method_overloads_.find(method_base);
            if (moit != method_overloads_.end() && !moit->second.empty()) {
                std::string best;
                for (auto& [mangled, ptypes] : moit->second) {
                    // method params are (sa, sb) — check right param (index 0 for methods vs 1 for free funcs)
                    if (!ptypes.empty() && !rightMatchesParam(ptypes[ptypes.size() > 1 ? 1 : 0])) continue;
                    if (best.empty() || ptypes.size() > func_param_types_[best].size())
                        best = mangled;
                }
                if (!best.empty()) return best;
            }
        }
    }

    // fall back to naked free-function overloads (old-style)
    auto foit = free_func_overloads_.find(fname);
    if (foit == free_func_overloads_.end() || foit->second.empty()) return "";

    std::string best;
    for (auto& [mangled, ptypes] : foit->second) {
        if (ptypes.empty()) continue;
        // check first param is a slid pointer and left matches
        std::string p0 = ptypes[0];
        std::string slid_name = p0;
        if (!slid_name.empty() && slid_name.back() == '^') slid_name.pop_back();
        else if (slid_name.size() >= 2 && slid_name.substr(slid_name.size()-2) == "[]") slid_name.resize(slid_name.size()-2);
        else continue;
        if (!slid_info_.count(slid_name)) continue;
        if (!leftMatchesSlid(slid_name)) continue;
        // check right param if present
        if (ptypes.size() > 1 && !rightMatchesParam(ptypes[1])) continue;
        // matched: prefer more specific (multi-param) overload
        if (best.empty() || ptypes.size() > func_param_types_[best].size())
            best = mangled;
    }
    return best;
}

// Resolve the best op= (or op<-) overload for the given argument expression.
// Priority: slid ref > scalar exact match > ptr/char[] fallback.
std::string Codegen::resolveOpEq(const std::string& base, const Expr& arg) {
    auto oit = method_overloads_.find(base);
    if (oit == method_overloads_.end()) return "";

    static const std::set<std::string> unsigned_types = {"uint","uint8","uint16","uint32","uint64","char"};

    // determine argument category
    bool arg_is_slid = false;
    bool arg_is_char = false;       // char literal ('x') — prefers char overload
    bool arg_is_scalar_int = false; // signed integer
    bool arg_is_unsigned = false;   // unsigned integer — prefers uint64 overload
    if (auto* ile = dynamic_cast<const IntLiteralExpr*>(&arg)) {
        arg_is_char = ile->is_char_literal;
        arg_is_scalar_int = !ile->is_char_literal;
    } else if (auto* nc = dynamic_cast<const TypeConvExpr*>(&arg)) {
        arg_is_char = nc->target_type == "char";
        arg_is_unsigned = !arg_is_char && unsigned_types.count(nc->target_type) > 0;
        arg_is_scalar_int = !arg_is_char && !arg_is_unsigned;
    } else if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end()) {
            std::string t = tit->second;
            if (!t.empty() && t.back() == '^') t.pop_back();
            else if (t.size() >= 2 && t.substr(t.size()-2) == "[]") t = t.substr(0, t.size()-2);
            arg_is_slid = slid_info_.count(t) > 0;
            arg_is_char = !arg_is_slid && (tit->second == "char");
            arg_is_unsigned = !arg_is_slid && !arg_is_char && unsigned_types.count(tit->second) > 0;
            if (!arg_is_slid && !arg_is_char && !arg_is_unsigned && !isIndirectType(tit->second))
                arg_is_scalar_int = true;
        }
    } else if (auto* ao = dynamic_cast<const AddrOfExpr*>(&arg)) {
        // ^x: addr-of x
        // ^x where x: SlidType (non-indirect) → SlidType^ — valid slid ref arg
        // ^x where x: SlidType^ or char[] → double-indirect — no match for any overload
        if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                if (isIndirectType(tit->second))
                    return "";  // ^ref is double-indirect: type error
                if (slid_info_.count(tit->second))
                    arg_is_slid = true;  // ^slid_var is a valid slid ref
            }
        }
    } else if (!exprSlidType(arg).empty()) {
        arg_is_slid = true; // e.g. result of op+ expression
    } else {
        // use inferred LLVM type: integer types are scalar, ptr is string/reference
        std::string lt = exprLlvmType(arg);
        if (lt == "i8")
            arg_is_char = true;
        else if (lt == "i16" || lt == "i32" || lt == "i64")
            arg_is_scalar_int = true;
    }

    // pass 1: exact category match
    for (auto& [m, ptypes] : oit->second) {
        if (ptypes.size() != 1) continue;
        if (arg_is_slid       && isRefType(ptypes[0])) return m;
        if (arg_is_char       && ptypes[0] == "char") return m;
        if (arg_is_unsigned   && ptypes[0] != "char" && unsigned_types.count(ptypes[0])) return m;
        if (arg_is_scalar_int && !isIndirectType(ptypes[0]) && ptypes[0] != "char" && !unsigned_types.count(ptypes[0])) return m;
        if (!arg_is_slid && !arg_is_char && !arg_is_scalar_int && !arg_is_unsigned && isPtrType(ptypes[0])) return m;
    }
    // pass 2: fallback — any ptr param for non-slid, non-scalar arg (e.g. string literal / char[])
    if (!arg_is_slid && !arg_is_scalar_int && !arg_is_unsigned) {
        for (auto& [m, ptypes] : oit->second) {
            if (ptypes.size() == 1 && isIndirectType(ptypes[0])) return m;
        }
    }
    // slid arg with no matching slid-ref overload: return "" so the caller can synthesize a copy
    if (arg_is_slid) return "";
    // scalar int/unsigned with no matching scalar overload: return "" so caller can coerce via temp
    if (arg_is_scalar_int || arg_is_unsigned) return "";
    // non-slid arg: fall back to first available overload
    return oit->second[0].first;
}

// Copy all fields of slid_name from src_ptr into dst_ptr (synthesized default copy).
void Codegen::emitSlidCopy(const std::string& slid_name,
                            const std::string& dst_ptr, const std::string& src_ptr) {
    auto& info = slid_info_[slid_name];
    for (int i = 0; i < (int)info.field_types.size(); i++) {
        std::string ft = llvmType(info.field_types[i]);
        std::string src_gep = newTmp();
        out_ << "    " << src_gep << " = getelementptr %struct." << slid_name
             << ", ptr " << src_ptr << ", i32 0, i32 " << i << "\n";
        std::string val = newTmp();
        out_ << "    " << val << " = load " << ft << ", ptr " << src_gep << "\n";
        std::string dst_gep = newTmp();
        out_ << "    " << dst_gep << " = getelementptr %struct." << slid_name
             << ", ptr " << dst_ptr << ", i32 0, i32 " << i << "\n";
        out_ << "    store " << ft << " " << val << ", ptr " << dst_gep << "\n";
    }
}

// Alloca a fresh instance of slid_name, default-init all fields, run ctor body, call __ctor.
// Returns the alloca register. Does NOT register for dtor (caller's responsibility if needed).
std::string Codegen::emitSlidAlloca(const std::string& slid_name) {
    auto& info = slid_info_[slid_name];
    std::string reg = newTmp();
    out_ << "    " << reg << " = alloca %struct." << slid_name << "\n";
    const SlidDef* slid_def = nullptr;
    for (auto& s : program_.slids) if (s.name == slid_name) { slid_def = &s; break; }
    for (int i = 0; i < (int)info.field_types.size(); i++) {
        std::string gep = newTmp();
        out_ << "    " << gep << " = getelementptr %struct." << slid_name
             << ", ptr " << reg << ", i32 0, i32 " << i << "\n";
        std::string val;
        if (slid_def && i < (int)slid_def->fields.size() && slid_def->fields[i].default_val)
            val = emitExpr(*slid_def->fields[i].default_val);
        else
            val = isInlineArrayType(info.field_types[i]) ? "zeroinitializer" : "0";
        out_ << "    store " << llvmType(info.field_types[i]) << " " << val << ", ptr " << gep << "\n";
    }
    if (slid_def && slid_def->ctor_body) {
        std::string saved_slid = current_slid_;
        std::string saved_self = self_ptr_;
        current_slid_ = slid_name;
        self_ptr_ = reg;
        emitBlock(*slid_def->ctor_body);
        current_slid_ = saved_slid;
        self_ptr_ = saved_self;
    }
    if (info.has_pinit && !info.is_transport_impl)
        out_ << "    call void @" << slid_name << "__pinit(ptr " << reg << ")\n";
    else if (info.has_explicit_ctor)
        out_ << "    call void @" << slid_name << "__ctor(ptr " << reg << ")\n";
    return reg;
}

// Emit an argument expression, taking pointer-vs-value into account.
// If param_type ends with '^' and the arg is a slid local, pass its alloca ptr directly.
// If param_type is 'SlidType^' and arg is a string literal, construct an implicit temporary.
std::string Codegen::emitArgForParam(const Expr& arg, const std::string& param_type) {
    bool want_ptr = !param_type.empty() && param_type.back() == '^';
    if (want_ptr) {
        std::string slid_name = param_type.substr(0, param_type.size() - 1);
        // implicit temporary: string literal passed to SlidType^ param
        if (slid_info_.count(slid_name) && dynamic_cast<const StringLiteralExpr*>(&arg)) {
            std::string tmp_reg = "%tmp_" + std::to_string(tmp_counter_++);
            out_ << "    " << tmp_reg << " = alloca %struct." << slid_name << "\n";
            auto& info = slid_info_[slid_name];
            // zero-init fields
            for (int i = 0; i < (int)info.field_types.size(); i++) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << slid_name
                     << ", ptr " << tmp_reg << ", i32 0, i32 " << i << "\n";
                if (isIndirectType(info.field_types[i]))
                    out_ << "    store ptr null, ptr " << gep << "\n";
                else
                    out_ << "    store " << llvmType(info.field_types[i]) << " 0, ptr " << gep << "\n";
            }
            // call explicit constructor if any
            if (info.has_explicit_ctor)
                out_ << "    call void @" << slid_name << "__ctor(ptr " << tmp_reg << ")\n";
            // call op=(char[]) to initialize from the literal
            auto oit = method_overloads_.find(slid_name + "__op=");
            if (oit != method_overloads_.end()) {
                for (auto& [m, ptypes] : oit->second) {
                    if (!ptypes.empty() && isPtrType(ptypes[0])) {
                        std::string str_val = emitExpr(arg);
                        out_ << "    call void @" << llvmGlobalName(m)
                             << "(ptr " << tmp_reg << ", ptr " << str_val << ")\n";
                        break;
                    }
                }
            }
            // register for dtor at end of enclosing statement
            if (info.has_dtor)
                pending_temp_dtors_.push_back({tmp_reg, slid_name});
            return tmp_reg;
        }
        // self — implicit object pointer, pass as current_slid_^ reference
        if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
            if (ve->name == "self" && !current_slid_.empty())
                return self_ptr_.empty() ? "%self" : self_ptr_;
        }
        // ^s — explicit address-of a slid local: pass its alloca ptr directly
        if (auto* ao = dynamic_cast<const AddrOfExpr*>(&arg)) {
            if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
                auto lit = local_types_.find(ve->name);
                if (lit != local_types_.end() && slid_info_.count(lit->second))
                    return locals_.at(ve->name);
            }
            return emitExpr(arg);
        }
        // s — implicit address-of (auto-promote slid local to ref)
        if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
            auto lit = local_types_.find(ve->name);
            if (lit != local_types_.end() && slid_info_.count(lit->second)) {
                return locals_.at(ve->name);
            }
        }
        // DerefExpr of a slid pointer (sa^): load the pointer value and pass it directly
        if (auto* de = dynamic_cast<const DerefExpr*>(&arg)) {
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto lit = local_types_.find(ve->name);
                if (lit != local_types_.end()) {
                    std::string t = lit->second;
                    if (!t.empty() && t.back() == '^') { t.pop_back(); }
                    if (slid_info_.count(t)) {
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load ptr, ptr " << locals_.at(ve->name) << "\n";
                        return loaded;
                    }
                }
            }
        }
    }
    std::string val = emitExpr(arg);
    // widen or truncate integer to match param type
    if (!param_type.empty() && !isIndirectType(param_type)) {
        std::string want = llvmType(param_type);
        std::string src  = exprLlvmType(arg);
        static const std::map<std::string,int> rank = {{"i1",0},{"i8",1},{"i16",2},{"i32",3},{"i64",4}};
        auto wit = rank.find(want), sit = rank.find(src);
        if (wit != rank.end() && sit != rank.end() && wit->first != sit->first) {
            if (wit->second < sit->second) {
                // truncate: e.g. i32 → i8 for char param
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = trunc " << src << " " << val << " to " << want << "\n";
                return tmp;
            } else {
                // widen: zext for unsigned source, sext for signed
                static const std::set<std::string> unsigned_types = {"uint","uint8","uint16","uint32","uint64","char"};
                bool src_unsigned = false;
                if (auto* nc = dynamic_cast<const TypeConvExpr*>(&arg))
                    src_unsigned = unsigned_types.count(nc->target_type) > 0;
                else if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
                    auto tit = local_types_.find(ve->name);
                    if (tit != local_types_.end()) src_unsigned = unsigned_types.count(tit->second) > 0;
                }
                std::string op = src_unsigned ? "zext" : "sext";
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = " << op << " " << src << " " << val << " to " << want << "\n";
                return tmp;
            }
        }
    }
    return val;
}

void Codegen::emitSlidMethod(const SlidDef& slid, const std::string& full_mangled,
                              const std::string& return_type,
                              const std::vector<std::pair<std::string,std::string>>& params,
                              const BlockStmt& body) {
    locals_.clear();
    local_types_.clear();
    dtor_vars_.clear();
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

    out_ << "define " << ret_type << " @" << llvmGlobalName(full_mangled)
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

    // find mangled name (may differ from fn.name when multiple overloads exist)
    std::string emit_name = fn.name;
    auto foit = free_func_overloads_.find(fn.name);
    if (foit != free_func_overloads_.end()) {
        // match by param types
        std::vector<std::string> ptypes;
        for (auto& [t, n] : fn.params) ptypes.push_back(t);
        for (auto& [mangled, mptypes] : foit->second) {
            if (mptypes == ptypes) { emit_name = mangled; break; }
        }
    }

    std::string ret_type = uses_sret ? "void" : llvmType(func_return_types_[emit_name]);
    current_func_return_type_ = ret_type;

    std::string param_str;
    if (uses_sret) {
        param_str = "ptr sret(%struct." + fn.return_type + ") %retval";
    }
    for (int i = 0; i < (int)fn.params.size(); i++) {
        if (!param_str.empty()) param_str += ", ";
        param_str += llvmType(fn.params[i].first) + " %arg_" + fn.params[i].second;
    }

    out_ << "define " << ret_type << " @" << llvmGlobalName(emit_name) << "(" << param_str << ") {\n";
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
        size_t temp_mark = pending_temp_dtors_.size();
        emitStmt(*stmt);
        // destroy implicit temporaries created during this statement
        for (int i = (int)pending_temp_dtors_.size() - 1; i >= (int)temp_mark; i--) {
            auto& td = pending_temp_dtors_[i];
            out_ << "    call void @" << td.second << "__dtor(ptr " << td.first << ")\n";
        }
        pending_temp_dtors_.resize(temp_mark);
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
