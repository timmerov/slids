#include "codegen.h"
#include <sstream>
#include <functional>
#include <stdexcept>
#include <set>
#include <climits>
#include <algorithm>
#include "codegen_helpers.h"

Codegen::Codegen(const Program& program, std::ostream& out, std::string source_file)
    : program_(program), out_(out), str_counter_(0), tmp_counter_(0), label_counter_(0),
      source_file_(std::move(source_file)) {}

std::string Codegen::newTmp() { return "%t" + std::to_string(tmp_counter_++); }

std::string Codegen::registerStringConstant(const std::string& value) {
    std::string label = "@.str" + std::to_string(str_counter_++);
    string_constants_.emplace_back(label, value);
    return label;
}
std::string Codegen::newLabel(const std::string& p) { return p + std::to_string(label_counter_++); }
std::string Codegen::uniqueAllocaReg(const std::string& var_name) {
    std::string base = "%var_" + var_name;
    if (!emitted_alloca_regs_.count(base)) {
        emitted_alloca_regs_.insert(base);
        return base;
    }
    std::string unique = base + "_" + std::to_string(tmp_counter_++);
    emitted_alloca_regs_.insert(unique);
    return unique;
}


std::string Codegen::llvmType(const std::string& t) {
    if (!t.empty() && t[0] == '{') return t; // pass through LLVM struct types (tuple returns)
    // anonymous tuple type: "(t1,t2,...)" → "{ llvm(t1), llvm(t2), ... }"
    if (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        std::string s = "{ ";
        std::string inner = t.substr(1, t.size() - 2);
        int depth = 0;
        std::string cur;
        bool first = true;
        auto flush = [&]() {
            if (!first) s += ", ";
            s += llvmType(cur);
            first = false;
            cur.clear();
        };
        for (char c : inner) {
            if (c == '(') { depth++; cur += c; }
            else if (c == ')') { depth--; cur += c; }
            else if (c == ',' && depth == 0) { flush(); }
            else cur += c;
        }
        if (!cur.empty()) flush();
        return s + " }";
    }
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
    if (t == "anyptr") return "ptr";
    if (t == "$vptr")  return "ptr"; // synthetic vtable pointer slot for virtual classes
    if (t == "float32") return "float";
    if (t == "float64") return "double";
    if (t == "void")  return "void";
    // slid type name → named struct
    if (slid_info_.count(t)) return "%struct." + t;
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

    // template overloads: prefer the bodied FunctionDef as the authoritative entry
    // for each (name, param-signature) pair. A `.slh` forward-decl + a `.sl` body
    // can both appear in `program_.functions` for the same overload — pick the body.
    auto sameTemplateSig = [](const FunctionDef& a, const FunctionDef& b) {
        if (a.params.size() != b.params.size()) return false;
        for (int i = 0; i < (int)a.params.size(); i++)
            if (a.params[i].first != b.params[i].first) return false;
        return true;
    };
    std::set<const FunctionDef*> bodied_templates;
    for (auto& fn : program_.functions)
        if (!fn.type_params.empty() && fn.body) bodied_templates.insert(&fn);

    for (auto& fn : program_.functions) {
        if (!fn.type_params.empty()) {
            // suppress a bodyless decl when a bodied overload with matching sig exists
            if (!fn.body) {
                bool shadowed = false;
                for (auto* b : bodied_templates) {
                    if (b->name == fn.name && sameTemplateSig(*b, fn)) { shadowed = true; break; }
                }
                if (shadowed) continue;
            }
            template_funcs_[fn.name].push_back({&fn, fn.impl_module, fn.is_local});
            continue;
        }
        std::vector<std::string> ptypes;
        for (auto& [t, n] : fn.params) ptypes.push_back(t);

        std::string mangled = mangleFreeFunction(fn.name, ptypes);
        free_func_overloads_[fn.name].push_back({mangled, ptypes});

        if (!fn.tuple_return_fields.empty()) {
            func_return_types_[mangled] = buildTupleType(fn.tuple_return_fields);
            func_tuple_fields_[mangled] = fn.tuple_return_fields;
        } else {
            func_return_types_[mangled] = fn.return_type;
        }
        func_param_types_[mangled] = ptypes;

        if (!fn.body) { exported_symbols_.insert(mangled); continue; } // forward declaration = exported

        // recurse into all blocks to find nested function defs
        std::function<void(const BlockStmt&)> findNested = [&](const BlockStmt& block) {
            for (auto& stmt : block.stmts) {
                if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(stmt.get())) {
                    std::string mangled = fn.name + "__" + nfs->def.name;
                    std::string ret;
                    if (!nfs->def.tuple_return_fields.empty()) {
                        ret = buildTupleType(nfs->def.tuple_return_fields);
                        func_tuple_fields_[mangled] = nfs->def.tuple_return_fields;
                    } else {
                        ret = nfs->def.return_type;
                    }
                    func_return_types_[mangled] = ret;
                    std::vector<std::string> nptypes;
                    for (auto& [t, n] : nfs->def.params) nptypes.push_back(t);
                    func_param_types_[mangled] = nptypes;
                } else if (auto* f = dynamic_cast<const ForRangeStmt*>(stmt.get())) {
                    findNested(*f->body);
                } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(stmt.get())) {
                    findNested(*ft->body);
                } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(stmt.get())) {
                    findNested(*fa->body);
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
        // external forward decls: register signature for methods not defined in this TU
        for (auto& em : program_.external_methods) {
            if (em.slid_name != slid.name || em.body) continue;
            if (em.method_name == "_" || em.method_name == "~") continue;
            if (!by_name.count(em.method_name))
                by_name[em.method_name].push_back({em.return_type, em.params});
        }
        // op<-> may only take a single SameType^ parameter — reject anything else.
        if (auto it = by_name.find("op<->"); it != by_name.end()) {
            std::string want = slid.name + "^";
            for (auto& e : it->second) {
                if (e.params.size() != 1 || e.params[0].first != want)
                    throw std::runtime_error("op<-> on slid '" + slid.name
                        + "' must take exactly one parameter of type '" + want + "'");
            }
        }
        for (auto& [method_name, entries] : by_name) {
            std::string base = slid.name + "__" + method_name;
            for (auto& e : entries) {
                std::vector<std::string> ptypes;
                for (auto& [t, n] : e.params) ptypes.push_back(t);
                std::string mangled = mangleMethod(slid.name, method_name, ptypes);
                func_return_types_[mangled] = e.ret;
                func_param_types_[mangled] = ptypes;
                // avoid duplicate overload entries (transport slid + impl slid both contribute)
                auto& overloads = method_overloads_[base];
                if (std::none_of(overloads.begin(), overloads.end(),
                        [&](const auto& p){ return p.second == ptypes; }))
                    overloads.push_back({mangled, ptypes});
            }
        }

        // mark exported: methods with a bodyless declaration (from header)
        for (auto& m : slid.methods) {
            if (m.body || m.name == "_" || m.name == "~") continue;
            std::string base = slid.name + "__" + m.name;
            std::vector<std::string> mptypes;
            for (auto& [t, n] : m.params) mptypes.push_back(t);
            auto it = method_overloads_.find(base);
            if (it != method_overloads_.end()) {
                for (auto& [mangled, ptypes] : it->second)
                    if (ptypes == mptypes) exported_symbols_.insert(mangled);
            }
        }
        if (slid.has_explicit_ctor_decl) exported_symbols_.insert(slid.name + "__$ctor");
        if (slid.has_explicit_dtor_decl) exported_symbols_.insert(slid.name + "__$dtor");
        if (slid.is_transport_impl) {
            exported_symbols_.insert(slid.name + "__$pinit");
            exported_symbols_.insert(slid.name + "__$sizeof");
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
        } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(&stmt)) {
            for (auto& e : ft->elements) scanExpr(*e);
            for (auto& s : ft->body->stmts) scanStmt(*s);
        } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(&stmt)) {
            scanExpr(*fa->array_expr);
            for (auto& s : fa->body->stmts) scanStmt(*s);
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
            } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(stmt.get())) {
                parent_locals.insert(ft->var_name);
                collectLocals(*ft->body);
            } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(stmt.get())) {
                parent_locals.insert(fa->var_name);
                collectLocals(*fa->body);
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
                        } else if (auto* ft2 = dynamic_cast<const ForTupleStmt*>(s.get())) {
                            own_params.insert(ft2->var_name);
                            collectNested(*ft2->body);
                        } else if (auto* fa2 = dynamic_cast<const ForArrayStmt*>(s.get())) {
                            own_params.insert(fa2->var_name);
                            collectNested(*fa2->body);
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
            } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(stmt.get())) {
                findNested(*ft->body);
            } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(stmt.get())) {
                findNested(*fa->body);
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
        if (!slid.type_params.empty()) {
            template_slids_[slid.name] = &slid;
            if (slid.is_local)
                local_slid_template_names_.insert(slid.name);
            else if (!slid.impl_module.empty())
                slid_template_modules_[slid.name] = slid.impl_module;
            continue;
        }
        SlidInfo info;
        info.name = slid.name;
        info.base_name = slid.base_name;
        for (int i = 0; i < (int)slid.fields.size(); i++) {
            info.field_index[slid.fields[i].name] = i;
            info.field_types.push_back(slid.fields[i].type);
        }
        info.own_field_count = (int)slid.fields.size();
        // declaration of incomplete class: consumer calls __$pinit and __$sizeof
        if (slid.has_trailing_ellipsis)
            info.has_pinit = true;
        // impl side: complete locally; emits __$pinit and __$sizeof for consumer
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
        // empty class: has () but no fields, not incomplete. methods/ctor/dtor take no self.
        info.is_empty = info.field_types.empty() && !info.has_pinit && !info.is_transport_impl;
        // namespace: declared as `Name { ... }` only — non-instantiable, called as Name:fn()
        info.is_namespace = slid.is_namespace;
        // _() and ~() must be declared together or both auto-generated. Declaring
        // only one is a compile error. (Namespaces have no instances — skip.)
        if (!info.is_namespace && info.has_explicit_ctor != info.has_dtor) {
            const char* present = info.has_explicit_ctor ? "_" : "~";
            const char* missing = info.has_explicit_ctor ? "~" : "_";
            throw std::runtime_error("class '" + slid.name
                + "' declares '" + present + "()' but not '" + missing
                + "()': constructor and destructor must be declared together"
                + " or both auto-generated");
        }
        slid_info_[slid.name] = std::move(info);
    }
}

void Codegen::resolveSlidInheritanceFor(SlidInfo& info) {
    if (info.inheritance_resolved) return;
    info.inheritance_resolved = true;
    // Helper: prepend a synthetic $vptr slot at index 0 of the given own-field
    // list. Used at the root virtual class (this class is virtual, base is not
    // or absent). Derived virtual classes inherit the vptr via base.field_types
    // concat below — no need to re-prepend.
    auto prependVptr = [](std::vector<std::string>& ftypes,
                          std::vector<std::string>& fnames) {
        ftypes.insert(ftypes.begin(), std::string("$vptr"));
        fnames.insert(fnames.begin(), std::string("$vptr"));
    };
    if (info.base_name.empty()) {
        info.base_field_count = 0;
        if (info.is_virtual_class) {
            // root virtual class with no base — prepend $vptr to own field_types.
            std::vector<std::string> own_types(info.field_types);
            std::vector<std::string> own_names(info.own_field_count);
            for (auto& [name, idx] : info.field_index) own_names[idx] = name;
            prependVptr(own_types, own_names);
            info.field_types = std::move(own_types);
            info.field_index.clear();
            for (int i = 0; i < (int)own_names.size(); i++)
                info.field_index[own_names[i]] = i;
            info.own_field_count = (int)info.field_types.size();
        }
        return;
    }
    auto bit = slid_info_.find(info.base_name);
    if (bit == slid_info_.end())
        throw std::runtime_error("base class '" + info.base_name
            + "' of '" + info.name + "' is not defined");
    SlidInfo& base = bit->second;
    if (base.is_namespace)
        throw std::runtime_error("cannot inherit from namespace '" + info.base_name + "'");
    resolveSlidInheritanceFor(base); // ensure base's flat layout is built first
    info.base_info = &base;
    // virtual-base validation: a virtual class must have a virtual base.
    if (info.is_virtual_class && !base.is_virtual_class)
        throw std::runtime_error("class '" + info.name
            + "' is virtual but its base '" + base.name
            + "' is not — all ancestors of a virtual class must have a virtual destructor");

    // Save own fields (currently sole occupants of field_types/field_index).
    int own_count = info.own_field_count;
    std::vector<std::string> own_types(info.field_types.begin(),
                                       info.field_types.begin() + own_count);
    std::vector<std::string> own_names(own_count);
    for (auto& [name, idx] : info.field_index) own_names[idx] = name;
    // If we're locally virtual but base isn't (caught by validation above),
    // we'd need to prepend $vptr here. That branch is unreachable.

    // Rebuild as base prefix + own suffix.
    info.field_types = base.field_types;
    info.field_index.clear();
    for (auto& [name, idx] : base.field_index) info.field_index[name] = idx;
    int base_count = (int)base.field_types.size();
    info.base_field_count = base_count;
    for (int i = 0; i < own_count; i++) {
        if (info.field_index.count(own_names[i]))
            throw std::runtime_error("field '" + own_names[i]
                + "' in '" + info.name + "' collides with a field inherited from '"
                + info.base_name + "'");
        info.field_index[own_names[i]] = base_count + i;
        info.field_types.push_back(own_types[i]);
    }
}

void Codegen::resolveSlidInheritance() {
    for (auto& [name, info] : slid_info_) resolveSlidInheritanceFor(info);
}

void Codegen::classifyVirtualClasses() {
    // step 1: locally_virtual = any virtual member or virtual ~.
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        auto it = slid_info_.find(slid.name);
        if (it == slid_info_.end()) continue;
        SlidInfo& info = it->second;
        if (slid.dtor_is_virtual) {
            info.locally_virtual = true;
            info.dtor_is_virtual = true;
        }
        for (auto& m : slid.methods) {
            if (m.is_virtual) { info.locally_virtual = true; break; }
        }
    }
    for (auto& em : program_.external_methods) {
        auto it = slid_info_.find(em.slid_name);
        if (it == slid_info_.end()) continue;
        if (em.is_virtual) it->second.locally_virtual = true;
    }
    // step 1b: in a virtual class, the dtor is virtual unconditionally —
    // the `virtual` keyword on `~` is optional/advisory. If the user wrote no
    // dtor, auto-generate an empty one. Either way, mark has_dtor so scope-exit
    // chains include this class.
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        auto it = slid_info_.find(slid.name);
        if (it == slid_info_.end()) continue;
        SlidInfo& info = it->second;
        if (!info.locally_virtual) continue;
        info.dtor_is_virtual = true;
        info.has_dtor = true;
    }
    // step 2: propagate is_virtual_class via base_name strings (base_info pointers
    // aren't linked yet — resolveSlidInheritance runs after this). Fixed-point.
    for (auto& [n, info] : slid_info_)
        if (info.locally_virtual) info.is_virtual_class = true;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [n, info] : slid_info_) {
            if (info.is_virtual_class) continue;
            if (info.base_name.empty()) continue;
            auto bit = slid_info_.find(info.base_name);
            if (bit == slid_info_.end()) continue;
            if (bit->second.is_virtual_class) {
                info.is_virtual_class = true;
                changed = true;
            }
        }
    }
}

void Codegen::buildVtables() {
    // Topological order: base before derived. resolveSlidInheritance has linked
    // base_info pointers, so we can walk chains via chainOf().
    auto findSlid = [&](const std::string& name) -> const SlidDef* {
        for (auto& s : program_.slids) if (s.name == name) return &s;
        return nullptr;
    };
    // Build per-class in dependency order using chainOf.
    std::set<std::string> built;
    std::function<void(const std::string&)> build = [&](const std::string& name) {
        if (!built.insert(name).second) return;
        auto it = slid_info_.find(name);
        if (it == slid_info_.end()) return;
        SlidInfo& info = it->second;
        if (!info.is_virtual_class) return;
        if (info.base_info) build(info.base_info->name);

        // Inherit base's vtable.
        if (info.base_info) info.vtable = info.base_info->vtable;
        const SlidDef* def = findSlid(name);
        if (!def) return;

        auto findSlot = [&](const std::string& mname,
                            const std::vector<std::string>& pts) -> int {
            for (int i = 0; i < (int)info.vtable.size(); i++)
                if (info.vtable[i].method_name == mname && info.vtable[i].param_types == pts)
                    return i;
            return -1;
        };
        auto findSlotByName = [&](const std::string& mname) -> int {
            for (int i = 0; i < (int)info.vtable.size(); i++)
                if (info.vtable[i].method_name == mname) return i;
            return -1;
        };

        // Apply own methods (original declaration). New virtual methods may only
        // be added here, not in reopens.
        for (auto& m : def->methods) {
            std::vector<std::string> pts;
            for (auto& [t, n] : m.params) pts.push_back(t);
            int slot = findSlot(m.name, pts);
            if (m.is_virtual) {
                if (slot < 0) {
                    // shadowing-by-name check: same name with different sig
                    int by_name = findSlotByName(m.name);
                    if (by_name >= 0)
                        throw std::runtime_error("class '" + name + "': virtual method '"
                            + m.name + "' signature does not match the inherited slot");
                    // new slot
                    SlidInfo::VtableSlot s;
                    s.method_name = m.name;
                    s.param_types = pts;
                    s.return_type = m.return_type;
                    s.is_pure = m.is_pure;
                    if (!m.is_pure) {
                        s.defining_class = name;
                        s.mangled = mangleMethod(name, m.name, pts);
                    }
                    info.vtable.push_back(std::move(s));
                } else {
                    // override: must match return type exactly
                    if (info.vtable[slot].return_type != m.return_type)
                        throw std::runtime_error("class '" + name + "': virtual override '"
                            + m.name + "' return type does not match base");
                    if (m.is_pure) {
                        info.vtable[slot].is_pure = true;
                        info.vtable[slot].defining_class = "";
                        info.vtable[slot].mangled = "";
                    } else {
                        info.vtable[slot].is_pure = false;
                        info.vtable[slot].defining_class = name;
                        info.vtable[slot].mangled = mangleMethod(name, m.name, pts);
                    }
                }
            } else {
                // non-virtual: must not shadow a base virtual of the same name
                int by_name = findSlotByName(m.name);
                if (by_name >= 0)
                    throw std::runtime_error("class '" + name + "': method '" + m.name
                        + "' shadows inherited virtual method — declare it 'virtual' to override");
            }
        }
        // Apply external_methods (reopens). May NOT add new virtual slots; may
        // only define an existing virtual slot. A non-virtual reopen of a name
        // that matches a virtual slot is rejected.
        for (auto& em : program_.external_methods) {
            if (em.slid_name != name) continue;
            if (em.method_name == "_" || em.method_name == "~") continue;
            std::vector<std::string> pts;
            for (auto& [t, n] : em.params) pts.push_back(t);
            int slot = findSlot(em.method_name, pts);
            if (em.is_virtual) {
                if (slot < 0) {
                    // either signature mismatch with a same-name inherited slot,
                    // or attempt to add a new virtual via reopen.
                    int by_name = findSlotByName(em.method_name);
                    if (by_name >= 0)
                        throw std::runtime_error("class '" + name + "': virtual method '"
                            + em.method_name + "' signature does not match the inherited slot");
                    throw std::runtime_error("class '" + name
                        + "': new virtual methods may not be added in a reopen — '"
                        + em.method_name + "' must be declared in the original class");
                }
                if (info.vtable[slot].return_type != em.return_type)
                    throw std::runtime_error("class '" + name + "': virtual override '"
                        + em.method_name + "' return type does not match base");
                if (em.is_pure) {
                    info.vtable[slot].is_pure = true;
                    info.vtable[slot].defining_class = "";
                    info.vtable[slot].mangled = "";
                } else if (em.body) {
                    info.vtable[slot].is_pure = false;
                    info.vtable[slot].defining_class = name;
                    info.vtable[slot].mangled = mangleMethod(name, em.method_name, pts);
                }
            } else {
                int by_name = findSlotByName(em.method_name);
                if (by_name >= 0)
                    throw std::runtime_error("class '" + name + "': method '" + em.method_name
                        + "' shadows inherited virtual method — declare it 'virtual' to override");
            }
        }
    };
    for (auto& [name, info] : slid_info_) build(name);
}

void Codegen::synthesizeFieldDtors() {
    // A class whose own fields include a slid-typed field needs a dtor to
    // destroy those fields. If the user didn't write one, mark has_dtor anyway
    // so emitDtors schedules the call and emitSlidCtorDtor emits a synthetic
    // body containing only the field-destruction loop.
    for (auto& [name, info] : slid_info_) {
        if (info.is_namespace) continue;
        if (info.has_dtor) continue;
        bool owns_vptr = info.is_virtual_class && info.base_info == nullptr;
        int vptr_local = owns_vptr ? 1 : 0;
        int n_own = info.own_field_count - vptr_local;
        for (int j = 0; j < n_own; j++) {
            int flat_idx = info.base_field_count + vptr_local + j;
            const std::string& ftype = info.field_types[flat_idx];
            if (slid_info_.count(ftype)) {
                info.has_dtor = true;
                break;
            }
        }
    }
}

void Codegen::markImportableClasses() {
    // A class is importable iff its name appears in program_.slid_modules — that
    // map is populated during .slh import. Local-only classes are not importable.
    for (auto& [name, info] : slid_info_) {
        if (program_.slid_modules.count(name)) info.is_importable = true;
    }
}

void Codegen::validatePureSlots() {
    // F2 (C): every non-importable concrete virtual class must have all vtable
    // slots filled. Importable classes defer to the linker.
    for (auto& [name, info] : slid_info_) {
        if (!info.is_virtual_class) continue;
        if (info.is_importable) continue;
        // a class is "concrete" here if any slot has a body. A class with all-pure
        // slots is the abstract base — it's an error to instantiate but not to declare.
        bool any_concrete = false;
        for (auto& slot : info.vtable) if (!slot.is_pure) { any_concrete = true; break; }
        if (!any_concrete) continue;
        for (auto& slot : info.vtable) {
            if (!slot.is_pure) continue;
            throw std::runtime_error("class '" + name + "': virtual method '"
                + slot.method_name + "' is declared pure (= delete) but is never defined");
        }
    }
}

bool Codegen::isAncestor(const std::string& base, const std::string& derived) {
    auto it = slid_info_.find(derived);
    if (it == slid_info_.end()) return false;
    SlidInfo* cur = it->second.base_info;
    while (cur) {
        if (cur->name == base) return true;
        cur = cur->base_info;
    }
    return false;
}

std::vector<SlidInfo*> Codegen::chainOf(const std::string& slid_name) {
    std::vector<SlidInfo*> chain;
    auto it = slid_info_.find(slid_name);
    if (it == slid_info_.end()) return chain;
    SlidInfo* cur = &it->second;
    while (cur) { chain.push_back(cur); cur = cur->base_info; }
    std::reverse(chain.begin(), chain.end()); // base→derived
    return chain;
}

bool Codegen::hasDtorInChain(const std::string& slid_name) {
    auto it = slid_info_.find(slid_name);
    if (it == slid_info_.end()) return false;
    SlidInfo* cur = &it->second;
    while (cur) { if (cur->has_dtor) return true; cur = cur->base_info; }
    return false;
}

void Codegen::ensureSlidInstantiated(const std::string& type) {
    if (slid_info_.count(type) || emitted_slid_templates_.count(type)) return;
    for (auto& [base, def] : template_slids_) {
        std::string prefix = base + "__";
        if (type.size() <= prefix.size() || type.substr(0, prefix.size()) != prefix) continue;
        std::string rest = type.substr(prefix.size());
        std::vector<std::string> type_args;
        std::string cur;
        for (size_t i = 0; i <= rest.size(); i++) {
            if (i == rest.size()
                || (rest[i] == '_' && i + 1 < rest.size() && rest[i + 1] == '_')) {
                if (!cur.empty()) type_args.push_back(cur);
                cur.clear();
                i++; // skip second '_'
            } else {
                cur += rest[i];
            }
        }
        if (type_args.size() == def->type_params.size()) {
            instantiateSlidTemplate(base, type_args);
            return;
        }
    }
}

void Codegen::scanForSlidTemplateUses() {
    if (template_slids_.empty()) return;

    std::function<void(const Stmt&)> scanStmt;
    std::function<void(const BlockStmt&)> scanBlock = [&](const BlockStmt& b) {
        for (auto& s : b.stmts) scanStmt(*s);
    };
    scanStmt = [&](const Stmt& stmt) {
        if (auto* d = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            ensureSlidInstantiated(d->type);
        } else if (auto* b = dynamic_cast<const BlockStmt*>(&stmt)) {
            scanBlock(*b);
        } else if (auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
            scanBlock(*i->then_block);
            if (i->else_block) scanBlock(*i->else_block);
        } else if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
            scanBlock(*w->body);
        } else if (auto* f = dynamic_cast<const ForRangeStmt*>(&stmt)) {
            scanBlock(*f->body);
        } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(&stmt)) {
            scanBlock(*ft->body);
        } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(&stmt)) {
            scanBlock(*fa->body);
        } else if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
            for (auto& sc : sw->cases)
                for (auto& s : sc.stmts) scanStmt(*s);
        }
    };
    for (auto& fn : program_.functions)
        if (fn.body && fn.type_params.empty()) scanBlock(*fn.body);
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        if (slid.ctor_body)          scanBlock(*slid.ctor_body);
        if (slid.explicit_ctor_body) scanBlock(*slid.explicit_ctor_body);
        if (slid.dtor_body)          scanBlock(*slid.dtor_body);
        for (auto& m : slid.methods)
            if (m.body) scanBlock(*m.body);
    }
}

void Codegen::collectStringConstants() {
    // Flatten a "+" concat chain and collect all StringLiteralExpr leaves
    std::function<void(const Expr*, bool)> collectExpr = [&](const Expr* e, bool newline_on_last) {
        if (!e) return;
        if (auto* s = dynamic_cast<const StringLiteralExpr*>(e)) {
            std::string full = newline_on_last ? s->value + "\n" : s->value;
            string_constants_.emplace_back("@.str" + std::to_string(str_counter_++), full);
            return;
        }
        if (auto* b = dynamic_cast<const BinaryExpr*>(e)) {
            if (b->op == "+") {
                collectExpr(b->left.get(), false);
                collectExpr(b->right.get(), newline_on_last);
                return;
            }
            collectExpr(b->left.get(), false);
            collectExpr(b->right.get(), false);
            return;
        }
        if (auto* t = dynamic_cast<const TupleExpr*>(e)) {
            for (auto& v : t->values) collectExpr(v.get(), false);
            return;
        }
        if (auto* ce = dynamic_cast<const CallExpr*>(e)) {
            for (auto& arg : ce->args) collectExpr(arg.get(), false);
            return;
        }
        if (auto* mc = dynamic_cast<const MethodCallExpr*>(e)) {
            collectExpr(mc->object.get(), false);
            for (auto& arg : mc->args) collectExpr(arg.get(), false);
            return;
        }
        if (auto* ne = dynamic_cast<const NewScalarExpr*>(e)) {
            for (auto& arg : ne->args) collectExpr(arg.get(), false);
            return;
        }
        if (auto* ne = dynamic_cast<const NewExpr*>(e)) {
            collectExpr(ne->count.get(), false);
            return;
        }
        if (auto* pn = dynamic_cast<const PlacementNewExpr*>(e)) {
            collectExpr(pn->addr.get(), false);
            for (auto& arg : pn->args) collectExpr(arg.get(), false);
            return;
        }
        if (auto* u = dynamic_cast<const UnaryExpr*>(e))       { collectExpr(u->operand.get(), false); return; }
        if (auto* d = dynamic_cast<const DerefExpr*>(e))       { collectExpr(d->operand.get(), false); return; }
        if (auto* p = dynamic_cast<const PostIncDerefExpr*>(e)){ collectExpr(p->operand.get(), false); return; }
        if (auto* a = dynamic_cast<const AddrOfExpr*>(e))      { collectExpr(a->operand.get(), false); return; }
        if (auto* tc = dynamic_cast<const TypeConvExpr*>(e))   { collectExpr(tc->operand.get(), false); return; }
        if (auto* pc = dynamic_cast<const PtrCastExpr*>(e))    { collectExpr(pc->operand.get(), false); return; }
        if (auto* fa = dynamic_cast<const FieldAccessExpr*>(e)){ collectExpr(fa->object.get(), false); return; }
        if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(e)) {
            collectExpr(ai->base.get(), false);
            collectExpr(ai->index.get(), false);
            return;
        }
        // leaves (IntLiteralExpr, FloatLiteralExpr, VarExpr, NullptrExpr, etc.) produce no strings
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
        } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(&stmt)) {
            for (auto& s : ft->body->stmts) collect(*s);
        } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(&stmt)) {
            collectExpr(fa->array_expr.get(), false);
            for (auto& s : fa->body->stmts) collect(*s);
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
        } else if (auto* td = dynamic_cast<const TupleDestructureStmt*>(&stmt)) {
            if (td->init) collectExpr(td->init.get(), false);
        }
    };

    // collection order must match emission order exactly:
    // 1. all ctor/dtor bodies (emitSlidCtorDtor loop, then template instantiations)
    // 2. all method bodies   (emitSlidMethods loop, then template instantiations)
    // 3. all free functions  (emitFunction loop)
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue; // template slids emitted on demand
        if (slid.ctor_body)
            for (auto& stmt : slid.ctor_body->stmts) collect(*stmt);
        if (slid.explicit_ctor_body)
            for (auto& stmt : slid.explicit_ctor_body->stmts) collect(*stmt);
        if (slid.dtor_body)
            for (auto& stmt : slid.dtor_body->stmts) collect(*stmt);
    }
    for (auto* slid : pending_slid_instantiations_) {
        if (slid->ctor_body)
            for (auto& stmt : slid->ctor_body->stmts) collect(*stmt);
        if (slid->explicit_ctor_body)
            for (auto& stmt : slid->explicit_ctor_body->stmts) collect(*stmt);
        if (slid->dtor_body)
            for (auto& stmt : slid->dtor_body->stmts) collect(*stmt);
    }
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue; // template slids emitted on demand
        for (auto& m : slid.methods)
            if (m.body)
                for (auto& stmt : m.body->stmts) collect(*stmt);
    }
    for (auto* slid : pending_slid_instantiations_) {
        for (auto& m : slid->methods)
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

    collectSlids();
    classifyVirtualClasses();
    resolveSlidInheritance();
    synthesizeFieldDtors();
    buildVtables();
    markImportableClasses();
    validatePureSlots();
    collectFunctionSignatures();
    scanForSlidTemplateUses();

    // process explicit instantiate statements before string-constant collection so
    // their bodies are included in the pre-scan and in the ctor/dtor/method emit loops
    for (auto& req : program_.instantiations) {
        if (template_slids_.count(req.func_name)) {
            instantiateSlidTemplate(req.func_name, req.type_args, true);
            continue;
        }
        // pick the function-template overload that matches req.param_types
        auto it = template_funcs_.find(req.func_name);
        if (it == template_funcs_.end() || it->second.empty())
            throw std::runtime_error("unknown template: " + req.func_name);
        // build subst for matching against req.param_types
        const TemplateFuncEntry* chosen = nullptr;
        for (auto& entry : it->second) {
            const FunctionDef& tmpl = *entry.def;
            if (tmpl.params.size() != req.param_types.size()) continue;
            if (tmpl.type_params.size() != req.type_args.size()) continue;
            std::map<std::string, std::string> subst;
            for (int i = 0; i < (int)tmpl.type_params.size(); i++)
                subst[tmpl.type_params[i]] = req.type_args[i];
            std::function<std::string(const std::string&)> sub =
                [&](const std::string& t) -> std::string {
                if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return sub(t.substr(0, t.size()-2)) + "[]";
                if (!t.empty() && t.back() == '^') return sub(t.substr(0, t.size()-1)) + "^";
                if (isAnonTupleType(t)) {
                    std::string s = "(";
                    bool first = true;
                    for (auto& e : anonTupleElems(t)) { if (!first) s += ","; first = false; s += sub(e); }
                    return s + ")";
                }
                auto sit = subst.find(t);
                return sit != subst.end() ? sit->second : t;
            };
            bool match = true;
            for (int i = 0; i < (int)tmpl.params.size(); i++) {
                std::string substituted = sub(tmpl.params[i].first);
                if (slid_info_.count(substituted)) substituted += "^";
                if (substituted != req.param_types[i]) { match = false; break; }
            }
            if (match) { chosen = &entry; break; }
        }
        if (!chosen)
            throw std::runtime_error("instantiate: no overload of '" + req.func_name
                + "' matches the requested parameter signature");
        instantiateTemplate(*chosen, req.type_args, true);
    }

    // explicit instantiations are linkable entry points — export all their symbols
    for (auto* slid : pending_slid_instantiations_) {
        exported_symbols_.insert(slid->name + "__$ctor");
        exported_symbols_.insert(slid->name + "__$dtor");
        exported_symbols_.insert(slid->name + "__$sizeof");
        std::string prefix = slid->name + "__";
        for (auto& [base, overloads] : method_overloads_) {
            if (base.substr(0, prefix.size()) != prefix) continue;
            for (auto& [mangled, ptypes] : overloads)
                exported_symbols_.insert(mangled);
        }
    }

    // validate: class objects and anon-tuples cannot be passed by value
    auto checkParams = [&](const std::string& ctx,
                           const std::vector<std::pair<std::string,std::string>>& params) {
        for (auto& [type, name] : params) {
            if (slid_info_.count(type) > 0)
                throw std::runtime_error(ctx + ": parameter '" + name +
                    "' has class type '" + type + "' — cannot pass by value; use '" + type + "^'");
            if (isAnonTupleType(type))
                throw std::runtime_error(ctx + ": parameter '" + name +
                    "' has tuple type '" + type + "' — cannot pass by value; use '" + type + "^'");
        }
    };
    for (auto& fn : program_.functions)
        checkParams(fn.name, fn.params);
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue; // skip template slids
        for (auto& m : slid.methods)
            checkParams(slid.name + "." + m.name, m.params);
    }
    for (auto& em : program_.external_methods)
        checkParams(em.slid_name + "." + em.method_name, em.params);

    // String constants are now registered on-demand from emit-time via
    // registerStringConstant(); the global declarations are flushed at the end
    // of the module (see end of emit()).

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
    // collect which names have an implementation slid in this TU
    std::set<std::string> has_impl_slid;
    for (auto& slid : program_.slids)
        if (slid.is_transport_impl) has_impl_slid.insert(slid.name);

    auto emitStructType = [&](const SlidDef& slid) {
        auto& info = slid_info_[slid.name];
        out_ << "%struct." << slid.name << " = type { ";
        bool first = true;
        for (int i = 0; i < (int)info.field_types.size(); i++) {
            if (!first) out_ << ", ";
            first = false;
            out_ << llvmType(info.field_types[i]);
        }
        if (info.padding_bytes > 0) {
            if (!first) out_ << ", ";
            out_ << "[" << info.padding_bytes << " x i8]";
        }
        out_ << " }\n";
    };

    std::set<std::string> emitted_structs;
    // first pass: emit implementation slids (complete field layout)
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        if (slid.is_namespace) continue;
        if (!slid.is_transport_impl) continue;
        if (!emitted_structs.insert(slid.name).second) continue;
        emitStructType(slid);
    }
    // second pass: emit remaining slids (skip names already covered by impl)
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        if (slid.is_namespace) continue;
        if (slid.is_transport_impl) continue;
        if (!emitted_structs.insert(slid.name).second) continue;
        emitStructType(slid);
    }
    // emit struct types for template class instantiations
    for (auto* slid : pending_slid_instantiations_) {
        auto& info = slid_info_[slid->name];
        out_ << "%struct." << slid->name << " = type { ";
        bool first = true;
        for (auto& ft : info.field_types) {
            if (!first) out_ << ", ";
            out_ << llvmType(ft);
            first = false;
        }
        out_ << " }\n";
    }
    // emit struct types for imported template class instantiations (deferred)
    for (auto* slid : pending_slid_declares_) {
        auto& info = slid_info_[slid->name];
        out_ << "%struct." << slid->name << " = type { ";
        bool first = true;
        for (auto& ft : info.field_types) {
            if (!first) out_ << ", ";
            out_ << llvmType(ft);
            first = false;
        }
        out_ << " }\n";
    }

    // emit frame struct types for functions with nested functions (skip templates)
    for (auto& fn : program_.functions)
        if (fn.body && fn.type_params.empty()) emitFrameStruct(fn);

    if (!program_.slids.empty() || !program_.functions.empty()) out_ << "\n";

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
        if (!slid.type_params.empty()) continue; // skip template slids
        // inline ctor/dtor bodies count as locally defined
        if (slid.explicit_ctor_body) local_methods.insert(slid.name + "__$ctor");
        if (slid.dtor_body)          local_methods.insert(slid.name + "__$dtor");
        // F1 (A): virtual classes with no explicit dtor get a synthetic empty
        // virtual dtor emitted in this TU.
        {
            auto sit = slid_info_.find(slid.name);
            if (sit != slid_info_.end() && sit->second.is_virtual_class
                && !slid.has_explicit_dtor_decl)
                local_methods.insert(slid.name + "__$dtor");
        }
        // impl slids locally define __$pinit and __$sizeof
        if (slid.is_transport_impl) {
            local_methods.insert(slid.name + "__$pinit");
            local_methods.insert(slid.name + "__$sizeof");
        }
        // all non-declaration slids define __$sizeof locally
        if (!slid.has_trailing_ellipsis) local_methods.insert(slid.name + "__$sizeof");
        for (auto& m : slid.methods) {
            if (!m.body) continue;
            std::string base = slid.name + "__" + m.name;
            auto oit = method_overloads_.find(base);
            if (oit != method_overloads_.end())
                for (auto& [mn, _] : oit->second) local_methods.insert(mn);
        }
    }
    // template instantiations are always locally defined
    for (auto* slid : pending_slid_instantiations_) {
        local_methods.insert(slid->name + "__$ctor");
        local_methods.insert(slid->name + "__$dtor");
        for (auto& m : slid->methods) {
            std::string base = slid->name + "__" + m.name;
            auto oit = method_overloads_.find(base);
            if (oit != method_overloads_.end())
                for (auto& [mn, _] : oit->second) local_methods.insert(mn);
        }
    }
    // imported template instantiations: emit declares for ctor/dtor/sizeof/methods
    for (auto* slid : pending_slid_declares_) {
        auto& info = slid_info_[slid->name];
        const char* self_arg = info.is_empty ? "" : "ptr";
        if (info.has_explicit_ctor)
            out_ << "declare void @" << slid->name << "__$ctor(" << self_arg << ")\n";
        if (info.has_dtor)
            out_ << "declare void @" << slid->name << "__$dtor(" << self_arg << ")\n";
        out_ << "declare i64 @" << slid->name << "__$sizeof()\n";
        for (auto& [base, overloads] : method_overloads_) {
            std::string prefix = slid->name + "__";
            if (base.substr(0, prefix.size()) != prefix) continue;
            for (auto& [mangled, ptypes] : overloads) {
                if (!declared_fns.insert(mangled).second) continue;
                std::string ret_type_str = func_return_types_[mangled];
                std::string ret = ret_type_str.empty() ? "void" : llvmType(ret_type_str);
                out_ << "declare " << ret << " @" << llvmGlobalName(mangled) << "(";
                bool first = info.is_empty;
                if (!info.is_empty) out_ << "ptr";
                for (auto& pt : ptypes) { if (!first) out_ << ", "; first = false; out_ << llvmType(pt); }
                out_ << ")\n";
            }
        }
    }
    for (auto& em : program_.external_methods) {
        if (!em.body) continue;
        std::string base = em.slid_name + "__" + em.method_name;
        auto oit = method_overloads_.find(base);
        if (oit != method_overloads_.end())
            for (auto& [mn, _] : oit->second) local_methods.insert(mn);
        // also include ctor/dtor
        if (em.method_name == "_") local_methods.insert(em.slid_name + "__$ctor");
        if (em.method_name == "~") local_methods.insert(em.slid_name + "__$dtor");
    }
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue; // skip template slids
        // ctor/dtor — declare if not locally defined
        auto& info = slid_info_[slid.name];
        const char* self_arg = info.is_empty ? "" : "ptr";
        if (info.has_explicit_ctor && !local_methods.count(slid.name + "__$ctor"))
            out_ << "declare void @" << slid.name << "__$ctor(" << self_arg << ")\n";
        if (info.has_dtor && !local_methods.count(slid.name + "__$dtor"))
            out_ << "declare void @" << slid.name << "__$dtor(" << self_arg << ")\n";
        if (info.has_pinit && !local_methods.count(slid.name + "__$pinit"))
            out_ << "declare void @" << slid.name << "__$pinit(ptr)\n";
        if (info.has_pinit && !local_methods.count(slid.name + "__$sizeof"))
            out_ << "declare i64 @" << slid.name << "__$sizeof()\n";
        // regular methods: first arg is ptr (self) unless slid is empty
        for (auto& [base, overloads] : method_overloads_) {
            if (base.substr(0, slid.name.size() + 2) != slid.name + "__") continue;
            for (auto& [mangled, ptypes] : overloads) {
                if (local_methods.count(mangled)) continue;
                if (!declared_fns.insert(mangled).second) continue;
                std::string ret_type_str = func_return_types_[mangled];
                std::string ret = ret_type_str.empty() ? "void" : llvmType(ret_type_str);
                out_ << "declare " << ret << " @" << llvmGlobalName(mangled) << "(";
                bool first = info.is_empty;
                if (!info.is_empty) out_ << "ptr";
                for (auto& pt : ptypes) { if (!first) out_ << ", "; first = false; out_ << llvmType(pt); }
                out_ << ")\n";
            }
        }
    }

    // emit declares for all virtual-method slot impls (both in this TU and
    // imported), and for __cxa_pure_virtual stub used in pure slots.
    {
        bool any_virtual = false;
        bool any_pure = false;
        for (auto& [name, info] : slid_info_) {
            if (!info.is_virtual_class) continue;
            any_virtual = true;
            for (auto& slot : info.vtable) if (slot.is_pure) { any_pure = true; break; }
        }
        if (any_pure) {
            out_ << "declare void @__cxa_pure_virtual()\n";
        }
        if (any_virtual) {
            // emit one vtable global per virtual class, internal linkage. Slot
            // entries reference functions by their already-declared mangled
            // names (declared above as part of method declares).
            for (auto& [name, info] : slid_info_) {
                if (!info.is_virtual_class) continue;
                int n = (int)info.vtable.size();
                out_ << "@_ZTV" << name << " = internal constant ["
                     << n << " x ptr] [";
                for (int i = 0; i < n; i++) {
                    if (i > 0) out_ << ", ";
                    if (info.vtable[i].is_pure) {
                        out_ << "ptr @__cxa_pure_virtual";
                    } else {
                        out_ << "ptr @" << llvmGlobalName(info.vtable[i].mangled);
                    }
                }
                out_ << "]\n";
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

    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        emitSlidCtorDtor(slid);
    }
    for (auto* slid : pending_slid_instantiations_)
        emitSlidCtorDtor(*slid);

    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        emitSlidMethods(slid);
    }
    for (auto* slid : pending_slid_instantiations_)
        emitSlidMethods(*slid);

    for (auto& fn : program_.functions)
        if (fn.type_params.empty()) emitFunction(fn);

    // emit all pending template instantiations (inline calls + explicit instantiate)
    for (auto& fn : pending_instantiations_)
        emitFunction(fn);

    // emit declares for imported template instantiations (deferred to module scope)
    for (auto& fn : pending_declares_)
        emitTemplateDeclare(fn);

    // flush string constants (registered on-demand during emit)
    if (!string_constants_.empty()) out_ << "\n";
    for (auto& [label, value] : string_constants_) {
        int len;
        std::string escaped = llvmEscape(value, len);
        out_ << label << " = private constant [" << len << " x i8] c\"" << escaped << "\"\n";
    }
}

bool Codegen::isExported(const std::string& mangled) const {
    if (mangled == "main") return true;
    return exported_symbols_.count(mangled) > 0;
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
            } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(stmt.get())) {
                scan(*ft->body);
            } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(stmt.get())) {
                scan(*fa->body);
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
    emitted_alloca_regs_.clear();
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
    current_func_slids_return_type_ = func_return_types_[mangled];
    current_func_tuple_fields_ = fn.tuple_return_fields;

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

    out_ << "define internal " << ret_type << " @" << mangled << "(" << param_str << ") {\n";
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
        std::string reg = uniqueAllocaReg(name);
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


void Codegen::emitExplicitDtor(const std::string& static_type, const std::string& obj_ptr) {
    auto sit = slid_info_.find(static_type);
    if (sit == slid_info_.end()) return; // primitive / pointer / anon-tuple — no-op
    if (sit->second.has_dtor || sit->second.has_pinit)
        emitDtorChainCall(static_type, obj_ptr);
}

void Codegen::emitDtorChainCall(const std::string& slid_type, const std::string& target) {
    auto chain = chainOf(slid_type);
    // walk derived→base: chain is base→derived, so iterate in reverse.
    for (int ci = (int)chain.size() - 1; ci >= 0; ci--) {
        SlidInfo* c = chain[ci];
        if (!c->has_dtor) continue;
        out_ << "    call void @" << c->name << "__$dtor("
             << (c->is_empty ? "" : "ptr " + target) << ")\n";
    }
}

void Codegen::emitDtors() {
    // flush expression-level temporaries first (before named locals)
    for (int i = (int)pending_temp_dtors_.size() - 1; i >= 0; i--) {
        auto& td = pending_temp_dtors_[i];
        emitDtorChainCall(td.second, td.first);
    }
    pending_temp_dtors_.clear();
    // call named-local dtors in reverse declaration order
    for (int i = (int)dtor_vars_.size() - 1; i >= 0; i--) {
        auto& e = dtor_vars_[i];
        std::string target;
        if (e.tuple_index >= 0) {
            std::string tuple_type = local_types_[e.var_name];
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr " << llvmType(tuple_type)
                 << ", ptr " << locals_[e.var_name] << ", i32 0, i32 " << e.tuple_index << "\n";
            target = gep;
        } else {
            target = locals_[e.var_name];
        }
        emitDtorChainCall(e.slid_type, target);
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

    // emit explicit constructor: @ClassName__$ctor(ptr %self)
    if (ctor_body) {
        locals_.clear();
        local_types_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;

        bool empty = slid_info_[slid.name].is_empty;
        out_ << "define " << (isExported(slid.name + "__$ctor") ? "" : "internal ") << "void @" << slid.name << "__$ctor("
             << (empty ? "" : "ptr %self") << ") {\n";
        out_ << "entry:\n";
        emitBlock(*ctor_body);
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    // emit destructor: @ClassName__$dtor(ptr %self)
    // Emitted whenever the class has a dtor for any reason: explicit body,
    // virtual auto-gen (F1 A), or slid-typed own fields needing destruction.
    bool need_dtor_fn = dtor_body
        || (slid_info_.count(slid.name) && slid_info_.at(slid.name).has_dtor);
    if (need_dtor_fn) {
        locals_.clear();
        local_types_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;

        auto& info = slid_info_[slid.name];
        bool empty = info.is_empty;
        out_ << "define " << (isExported(slid.name + "__$dtor") ? "" : "internal ") << "void @" << slid.name << "__$dtor("
             << (empty ? "" : "ptr %self") << ") {\n";
        out_ << "entry:\n";
        if (dtor_body) emitBlock(*dtor_body);
        // Auto-destroy own slid-typed fields in reverse declaration order.
        // Inherited fields are destroyed by the base class's __$dtor (the
        // chain walker calls each class's dtor in turn). Skip $vptr at slot 0
        // of the root virtual class.
        if (!block_terminated_ && !empty) {
            bool owns_vptr = info.is_virtual_class && info.base_info == nullptr;
            int vptr_local = owns_vptr ? 1 : 0;
            int n_own = info.own_field_count - vptr_local;
            for (int j = n_own - 1; j >= 0; j--) {
                int flat_idx = info.base_field_count + vptr_local + j;
                const std::string& ftype = info.field_types[flat_idx];
                if (slid_info_.count(ftype) && slid_info_.at(ftype).has_dtor) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %struct."
                         << slid.name << ", ptr %self, i32 0, i32 "
                         << flat_idx << "\n";
                    emitDtorChainCall(ftype, gep);
                }
            }
        }
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    current_slid_ = "";

    // emit __$pinit for transport slids: initializes private fields, optionally chains to __$ctor
    if (slid.is_transport_impl) {
        auto& info = slid_info_[slid.name];
        locals_.clear();
        local_types_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;
        self_ptr_ = "";  // field access uses %self fallback

        out_ << "define " << (isExported(slid.name + "__$pinit") ? "" : "internal ") << "void @" << slid.name << "__$pinit(ptr %self) {\n";
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
                val = (info.field_types[i] == "float32" || info.field_types[i] == "float64") ? "0.0" : "0";
            }
            out_ << "    store " << llvmType(info.field_types[i]) << " " << val << ", ptr " << gep << "\n";
        }

        if (info.has_explicit_ctor) {
            // chain to explicit ctor with musttail for zero overhead
            out_ << "    musttail call void @" << slid.name << "__$ctor(ptr %self)\n";
        }
        out_ << "    ret void\n";
        out_ << "}\n\n";

        current_slid_ = "";
        self_ptr_ = "";
    }

    // emit __$sizeof for every locally-complete slid (not for consumer-side declarations)
    if (!slid.has_trailing_ellipsis && !slid.is_namespace) {
        std::string linkage = isExported(slid.name + "__$sizeof") ? "" : "internal ";
        out_ << "define " << linkage << "i64 @" << slid.name << "__$sizeof() {\n";
        out_ << "entry:\n";
        out_ << "    %gep0 = getelementptr %struct." << slid.name << ", ptr null, i32 1\n";
        out_ << "    %sz0 = ptrtoint ptr %gep0 to i64\n";
        out_ << "    ret i64 %sz0\n";
        out_ << "}\n\n";
    }
}

Codegen::TemplateResolution Codegen::resolveTemplateOverload(
    const std::string& name,
    const std::vector<std::string>& explicit_type_args,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    auto it = template_funcs_.find(name);
    if (it == template_funcs_.end() || it->second.empty()) return {nullptr, {}};

    // Single overload fast path: skip arity check at this layer (template type-arg
    // inference will surface a clear error if shapes don't match).
    if (it->second.size() == 1) {
        const TemplateFuncEntry& entry = it->second[0];
        std::vector<std::string> targs = explicit_type_args.empty()
            ? inferTypeArgs(*entry.def, args) : explicit_type_args;
        return {&entry, std::move(targs)};
    }

    // Multiple overloads: filter by arity, then by post-substitution param-type match.
    const TemplateFuncEntry* best = nullptr;
    std::vector<std::string> best_targs;
    int matches = 0;
    for (auto& entry : it->second) {
        const FunctionDef& tmpl = *entry.def;
        if (tmpl.params.size() != args.size()) continue;
        std::vector<std::string> targs;
        try {
            targs = explicit_type_args.empty() ? inferTypeArgs(tmpl, args) : explicit_type_args;
        } catch (...) { continue; }
        if (targs.size() != tmpl.type_params.size()) continue;
        // build substitution and check each substituted param against actual arg type
        std::map<std::string, std::string> subst;
        for (int i = 0; i < (int)tmpl.type_params.size(); i++)
            subst[tmpl.type_params[i]] = targs[i];
        bool match = true;
        for (int i = 0; i < (int)tmpl.params.size(); i++) {
            std::string expected = tmpl.params[i].first;
            // apply substitution recursively (handles T, T^, T[], (T,T)^ etc.)
            std::function<std::string(const std::string&)> sub =
                [&](const std::string& t) -> std::string {
                if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return sub(t.substr(0, t.size()-2)) + "[]";
                if (!t.empty() && t.back() == '^') return sub(t.substr(0, t.size()-1)) + "^";
                if (isAnonTupleType(t)) {
                    std::string s = "(";
                    bool first = true;
                    for (auto& e : anonTupleElems(t)) { if (!first) s += ","; first = false; s += sub(e); }
                    return s + ")";
                }
                auto sit = subst.find(t);
                return sit != subst.end() ? sit->second : t;
            };
            std::string substituted = sub(expected);
            // class-typed params auto-promote to ref in instantiateTemplate; mirror that here
            if (slid_info_.count(substituted)) substituted += "^";
            std::string actual = exprType(*args[i]);
            if (actual.empty()) continue; // can't resolve actual; skip strict check
            if (actual != substituted) {
                // tolerate auto-promote (value to ref) for slid types
                std::string s = substituted;
                if (!s.empty() && s.back() == '^' && s.substr(0, s.size()-1) == actual) continue;
                match = false; break;
            }
        }
        if (!match) continue;
        matches++;
        best = &entry;
        best_targs = std::move(targs);
    }
    if (matches == 0) return {nullptr, {}};
    if (matches > 1)
        throw std::runtime_error("ambiguous template overload for '" + name + "'");
    return {best, std::move(best_targs)};
}

std::string Codegen::resolveFreeFunctionMangledName(
    const std::string& name,
    size_t arg_count) const
{
    if (func_return_types_.count(name)) return name;
    // nested function: signatures are registered under <parent>__<nested>.
    // Nested-fn names are unique within their parent so no arity match is needed.
    auto nit = nested_info_.find(name);
    if (nit != nested_info_.end()) {
        std::string mangled = nit->second.parent_name + "__" + name;
        if (func_return_types_.count(mangled)) return mangled;
    }
    auto foit = free_func_overloads_.find(name);
    if (foit == free_func_overloads_.end()) return "";
    for (auto& [mn, ptypes] : foit->second) {
        if (ptypes.size() == arg_count) return mn;
    }
    return "";
}

std::string Codegen::mangleFreeFunction(
    const std::string& name,
    const std::vector<std::string>& ptypes) const
{
    if (name == "main") return name; // C runtime calls main() by exact name
    std::string m = name;
    for (auto& t : ptypes) m += "__" + paramTokenForType(t);
    return m;
}

std::string Codegen::mangleMethod(
    const std::string& slid_name,
    const std::string& method_name,
    const std::vector<std::string>& ptypes) const
{
    std::string m = slid_name + "__" + method_name;
    for (auto& t : ptypes) m += "__" + paramTokenForType(t);
    return m;
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
    // Pass 1: exact type match
    for (auto& [mangled, ptypes] : it->second) {
        if (ptypes.size() != args.size()) continue;
        bool match = true;
        for (int i = 0; i < (int)args.size(); i++) {
            if (exprType(*args[i]) != ptypes[i]) { match = false; break; }
        }
        if (match) return mangled;
    }
    // Pass 2: indirect-type match (pointer vs scalar)
    for (auto& [mangled, ptypes] : it->second) {
        if (ptypes.size() != args.size()) continue;
        bool match = true;
        for (int i = 0; i < (int)args.size(); i++) {
            bool param_ptr = isIndirectType(ptypes[i]);
            std::string at = exprType(*args[i]);
            bool arg_ptr = (at == "nullptr") ? true
                         : (!at.empty()      ? isIndirectType(at)
                                             : isPointerExpr(*args[i]));
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
    if (dynamic_cast<const NewScalarExpr*>(&expr))    return true;
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
    // function or method call returning a slid type produces a fresh temp alloca
    if (dynamic_cast<const CallExpr*>(&expr))
        return !exprSlidType(expr).empty();
    if (dynamic_cast<const MethodCallExpr*>(&expr))
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
        std::string s = derefSlidName(*de);
        if (!s.empty()) return s;
    }
    // ArrayIndexExpr: tup[i] where tup is an anon-tuple with a slid-typed slot
    if (dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        std::string t = inferSlidType(expr);
        if (slid_info_.count(t)) return t;
    }
    // function call returning a slid type
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        // slid ctor call: SlidName(args) → fresh SlidName temp
        if (slid_info_.count(ce->callee)) return ce->callee;
        auto resolved = resolveTemplateOverload(ce->callee, ce->type_args, ce->args);
        if (resolved.entry) {
            const FunctionDef& tmpl = *resolved.entry->def;
            std::map<std::string,std::string> subst;
            for (int i = 0; i < (int)tmpl.type_params.size(); i++)
                subst[tmpl.type_params[i]] = resolved.type_args[i];
            auto it2 = subst.find(tmpl.return_type);
            std::string rt = (it2 != subst.end()) ? it2->second : tmpl.return_type;
            if (slid_info_.count(rt)) return rt;
        }
        std::string mangled = resolveFreeFunctionMangledName(ce->callee, ce->args.size());
        if (!mangled.empty()) {
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end() && slid_info_.count(rit->second))
                return rit->second;
        }
    }
    if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
        std::string obj_slid;
        if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
            if (ve->name == "self" && !current_slid_.empty())
                obj_slid = current_slid_;
            else {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) obj_slid = tit->second;
            }
        }
        if (!obj_slid.empty()) {
            std::string mangled = resolveOverloadForCall(obj_slid + "__" + mc->method, mc->args);
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end() && slid_info_.count(rit->second))
                return rit->second;
        }
        return "";
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
            // return type is a known primitive — this expression is not slid-typed
            if (rit != func_return_types_.end() && !rit->second.empty() && rit->second != "void")
                return "";
            // fallback: infer from first param type (for overloads with unknown return type)
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

// Return the Slids type string of expr (e.g. "char[]", "int", "String").
// Returns "" when the type cannot be statically determined.
std::string Codegen::exprType(const Expr& expr) {
    if (dynamic_cast<const StringLiteralExpr*>(&expr)) return "char[]";
    if (auto* il = dynamic_cast<const IntLiteralExpr*>(&expr)) {
        if (il->is_char_literal) return "char";
        if (il->is_nondecimal)   return "uint64";
        return "int";
    }
    if (dynamic_cast<const FloatLiteralExpr*>(&expr)) return "float64";
    if (dynamic_cast<const NullptrExpr*>(&expr))      return "nullptr";
    if (dynamic_cast<const SizeofExpr*>(&expr))       return "intptr";
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr))  return nc->target_type;
    if (auto* pc = dynamic_cast<const PtrCastExpr*>(&expr))   return pc->target_type;
    if (auto* ne = dynamic_cast<const NewScalarExpr*>(&expr)) return ne->elem_type + "^";
    if (auto* ne = dynamic_cast<const NewExpr*>(&expr))       return ne->elem_type + "[]";
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                return info.field_types[fit->second];
        }
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end()) return tit->second;
        if (slid_info_.count(ve->name)) return ve->name;
        return "";
    }
    if (auto* ao = dynamic_cast<const AddrOfExpr*>(&expr)) {
        std::string t = exprType(*ao->operand);
        return t.empty() ? "" : t + "^";
    }
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        std::string t = exprType(*de->operand);
        if (isRefType(t)) return t.substr(0, t.size() - 1);
        if (isPtrType(t)) return t.substr(0, t.size() - 2);
        return "";
    }
    if (auto* pi = dynamic_cast<const PostIncDerefExpr*>(&expr)) {
        std::string t = exprType(*pi->operand);
        if (isPtrType(t)) return t.substr(0, t.size() - 2);
        return "";
    }
    if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        std::string t = exprType(*ai->base);
        if (isPtrType(t)) return t.substr(0, t.size() - 2);
        auto lb = t.rfind('[');
        if (lb != std::string::npos && lb > 0) return t.substr(0, lb);
        return "";
    }
    if (auto* se = dynamic_cast<const SliceExpr*>(&expr)) return exprType(*se->base);
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        std::string obj_slid;
        if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
            if (!current_slid_.empty()) {
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve2->name);
                if (fit != info.field_index.end()) {
                    obj_slid = info.field_types[fit->second];
                    if (isRefType(obj_slid)) obj_slid.pop_back();
                    else if (isPtrType(obj_slid)) obj_slid.resize(obj_slid.size()-2);
                    if (!slid_info_.count(obj_slid)) obj_slid.clear();
                }
            }
            if (obj_slid.empty()) {
                auto tit = local_types_.find(ve2->name);
                if (tit != local_types_.end()) obj_slid = tit->second;
            }
        } else if (auto* de2 = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de2->operand.get())) {
                auto tit = local_types_.find(ve2->name);
                if (tit != local_types_.end()) {
                    obj_slid = tit->second;
                    if (isRefType(obj_slid)) obj_slid.pop_back();
                    else if (isPtrType(obj_slid)) obj_slid.resize(obj_slid.size()-2);
                }
            }
        } else {
            obj_slid = exprSlidType(*fa->object);
        }
        if (!obj_slid.empty() && slid_info_.count(obj_slid)) {
            auto& info = slid_info_[obj_slid];
            auto fit = info.field_index.find(fa->field);
            if (fit != info.field_index.end())
                return info.field_types[fit->second];
        }
        return "";
    }
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        auto nit = nested_info_.find(ce->callee);
        if (nit != nested_info_.end()) {
            std::string mangled = nit->second.parent_name + "__" + ce->callee;
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end()) return rit->second;
        }
        std::string mangled = resolveFreeFunctionMangledName(ce->callee, ce->args.size());
        if (!mangled.empty()) {
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end()) return rit->second;
        }
        return "";
    }
    if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
        std::string obj_slid;
        if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
            if (ve->name == "self" && !current_slid_.empty())
                obj_slid = current_slid_;
            else {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) obj_slid = tit->second;
            }
        } else {
            obj_slid = exprSlidType(*mc->object);
        }
        if (!obj_slid.empty()) {
            std::string mangled = resolveOverloadForCall(obj_slid + "__" + mc->method, mc->args);
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end()) return rit->second;
        }
        return "";
    }
    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
        if (u->op == "!") return "bool";
        return exprType(*u->operand);
    }
    if (auto* be = dynamic_cast<const BinaryExpr*>(&expr)) {
        if (be->op == "==" || be->op == "!=" || be->op == "<"  ||
            be->op == ">"  || be->op == "<=" || be->op == ">=" ||
            be->op == "&&" || be->op == "||" || be->op == "^^")
            return "bool";
        std::string mangled = resolveOperatorOverload(be->op, *be->left, *be->right);
        if (!mangled.empty()) {
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end() && !rit->second.empty() && rit->second != "void")
                return rit->second;
        }
        // pointer arithmetic: +/- with a pointer operand preserves the pointer type
        if (be->op == "+" || be->op == "-") {
            std::string lt = exprType(*be->left);
            if (isIndirectType(lt)) return lt;
            if (be->op == "+") {
                std::string rt = exprType(*be->right);
                if (isIndirectType(rt)) return rt;
            }
        }
        return exprType(*be->left);
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
            std::string s = derefSlidName(*de);
            if (!s.empty()) return s;
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
            if (derefSlidName(*de) == slid_name) return true;
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
            if (!derefSlidName(*de).empty() && p1_is_slid_ref) return true;
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
// Priority: slid ref > exact type name match > best-fit (smallest rank >= arg rank) > ptr/char[] fallback.
std::string Codegen::derefSlidName(const DerefExpr& de) {
    if (auto* ve = dynamic_cast<const VarExpr*>(de.operand.get())) {
        auto tit = local_types_.find(ve->name);
        if (tit == local_types_.end()) return "";
        std::string t = tit->second;
        if (isPtrType(t)) t.resize(t.size() - 2);
        else if (!t.empty() && t.back() == '^') t.pop_back();
        else return "";
        if (slid_info_.count(t)) return t;
        return "";
    }
    std::string ot = inferSlidType(*de.operand);
    if (isPtrType(ot) || isIndirectType(ot)) {
        std::string pointee = isPtrType(ot)
            ? ot.substr(0, ot.size() - 2)
            : ot.substr(0, ot.size() - 1);
        if (slid_info_.count(pointee)) return pointee;
    }
    return "";
}

std::string Codegen::resolveOpEq(const std::string& base, const Expr& arg) {
    auto oit = method_overloads_.find(base);
    if (oit == method_overloads_.end()) return "";

    static const std::set<std::string> unsigned_types = {"uint","uint8","uint16","uint32","uint64","char"};
    // Integer rank for best-fit selection: bool=0, i8=1, i16=2, i32=3, i64=4
    static const std::map<std::string,int> int_rank = {
        {"bool",0},
        {"int8",1},{"uint8",1},{"char",1},
        {"int16",2},{"uint16",2},
        {"int32",3},{"uint32",3},{"int",3},{"uint",3},
        {"int64",4},{"uint64",4},{"intptr",4}
    };

    // determine argument category and specific type
    bool arg_is_slid = false;
    bool arg_is_char = false;
    bool arg_is_scalar_int = false; // signed integer (including bool)
    bool arg_is_unsigned = false;
    std::string arg_int_type;       // specific type name when known (enables exact + best-fit matching)

    auto classify_int = [&](const std::string& t) {
        if (t == "char") { arg_is_char = true; arg_int_type = "char"; }
        else if (unsigned_types.count(t)) { arg_is_unsigned = true; arg_int_type = t; }
        else { arg_is_scalar_int = true; arg_int_type = t; } // bool, int8, int16, int32, int64, int, ...
    };

    if (auto* ile = dynamic_cast<const IntLiteralExpr*>(&arg)) {
        arg_is_char = ile->is_char_literal;
        arg_is_scalar_int = !ile->is_char_literal;
        if (ile->is_char_literal) arg_int_type = "char";
        // untyped integer literal: arg_int_type left empty — any signed int overload is acceptable
    } else if (auto* nc = dynamic_cast<const TypeConvExpr*>(&arg)) {
        classify_int(nc->target_type);
    } else if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end()) {
            std::string t = tit->second;
            if (!t.empty() && t.back() == '^') t.pop_back();
            else if (t.size() >= 2 && t.substr(t.size()-2) == "[]") t = t.substr(0, t.size()-2);
            arg_is_slid = slid_info_.count(t) > 0;
            if (!arg_is_slid && !isIndirectType(tit->second))
                classify_int(tit->second);
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
        // use inferred LLVM type: map back to canonical type name for rank-based matching
        std::string lt = exprLlvmType(arg);
        if      (lt == "i1")  classify_int("bool");
        else if (lt == "i8")  classify_int("char");
        else if (lt == "i16") classify_int("int16");
        else if (lt == "i32") {
            // bool maps to i32 in LLVM; use Slids type to distinguish bool from int32
            std::string slids_t = inferSlidType(arg);
            classify_int(slids_t == "bool" ? "bool" : "int32");
        }
        else if (lt == "i64") classify_int("int64");
    }

    auto is_signed_int_param = [&](const std::string& pt) {
        return !isIndirectType(pt) && pt != "char" && !unsigned_types.count(pt);
    };

    // pass 1: slid ref exact match
    for (auto& [m, ptypes] : oit->second) {
        if (ptypes.size() != 1) continue;
        if (arg_is_slid && isRefType(ptypes[0])) return m;
    }

    // pass 2: exact type name match for char/int/uint
    if (!arg_int_type.empty()) {
        for (auto& [m, ptypes] : oit->second) {
            if (ptypes.size() == 1 && ptypes[0] == arg_int_type) return m;
        }
    }

    // pass 3: best-fit — smallest overload rank >= arg rank (avoids picking int64 when int32 exists)
    if ((arg_is_scalar_int || arg_is_unsigned) && !arg_int_type.empty()) {
        auto rit = int_rank.find(arg_int_type);
        if (rit != int_rank.end()) {
            int arg_r = rit->second;
            std::string best_m;
            int best_r = INT_MAX;
            for (auto& [m, ptypes] : oit->second) {
                if (ptypes.size() != 1) continue;
                bool sign_ok = arg_is_unsigned ? (ptypes[0] != "char" && unsigned_types.count(ptypes[0]) > 0)
                                               : is_signed_int_param(ptypes[0]);
                if (!sign_ok) continue;
                auto pit = int_rank.find(ptypes[0]);
                if (pit == int_rank.end()) continue;
                if (pit->second >= arg_r && pit->second < best_r) {
                    best_r = pit->second;
                    best_m = m;
                }
            }
            if (!best_m.empty()) return best_m;
        }
    }

    // pass 4: untyped scalar int literal — any signed int overload, or any unsigned overload
    if (arg_is_scalar_int && arg_int_type.empty()) {
        for (auto& [m, ptypes] : oit->second) {
            if (ptypes.size() == 1 && is_signed_int_param(ptypes[0])) return m;
        }
    }
    if (arg_is_unsigned && arg_int_type.empty()) {
        for (auto& [m, ptypes] : oit->second) {
            if (ptypes.size() == 1 && ptypes[0] != "char" && unsigned_types.count(ptypes[0])) return m;
        }
    }

    // pass 5: non-slid, non-int arg — ptr/indirect param (e.g. string literal / char[])
    if (!arg_is_slid && !arg_is_char && !arg_is_scalar_int && !arg_is_unsigned) {
        for (auto& [m, ptypes] : oit->second) {
            if (ptypes.size() == 1 && isPtrType(ptypes[0])) return m;
        }
        for (auto& [m, ptypes] : oit->second) {
            if (ptypes.size() == 1 && isIndirectType(ptypes[0])) return m;
        }
    }

    // slid arg with no matching slid-ref overload: return "" so the caller can synthesize a copy
    if (arg_is_slid) return "";
    // scalar int/unsigned with no matching overload: return "" so caller can coerce via temp
    if (arg_is_scalar_int || arg_is_unsigned) return "";
    // non-slid arg: fall back to first available overload
    return oit->second[0].first;
}

// Copy all fields of slid_name from src_ptr into dst_ptr (synthesized default copy).
std::vector<std::string> Codegen::fieldTypesOf(const std::string& struct_type) {
    auto it = slid_info_.find(struct_type);
    if (it != slid_info_.end()) return it->second.field_types;
    if (isAnonTupleType(struct_type)) return anonTupleElems(struct_type);
    return {};
}

std::string Codegen::emitFieldGep(const std::string& struct_type,
                                  const std::string& ptr, int i) {
    std::string gep = newTmp();
    std::string leading = slid_info_.count(struct_type)
        ? ("%struct." + struct_type)
        : llvmType(struct_type);
    out_ << "    " << gep << " = getelementptr " << leading
         << ", ptr " << ptr << ", i32 0, i32 " << i << "\n";
    return gep;
}

std::pair<std::string, std::string>
Codegen::emitInlineArrayElemPtr(const Expr& base, const Expr& index) {
    auto* ve = dynamic_cast<const VarExpr*>(&base);
    if (!ve) return {"", ""};
    auto ait = array_info_.find(ve->name);
    if (ait == array_info_.end()) return {"", ""};
    const std::string& elem_type = ait->second.elem_type;
    int total = 1;
    for (int d : ait->second.dims) total *= d;
    std::string elt = llvmType(elem_type);
    std::string idx_llvm = exprLlvmType(index);
    std::string idx_val = emitExpr(index);
    std::string gep = newTmp();
    out_ << "    " << gep << " = getelementptr [" << total << " x " << elt
         << "], ptr " << ait->second.alloca_reg << ", i32 0, "
         << idx_llvm << " " << idx_val << "\n";
    return {gep, elem_type};
}

void Codegen::emitSlidSlotAssign(const std::string& elem_type,
                                  const std::string& dst_ptr, const std::string& src_ptr,
                                  bool is_move, bool is_init) {
    // Init sites: default-construct the slot first so the eventual dtor pairs
    // with a ctor. No-op for anon-tuple and non-slid element types.
    if (is_init && slid_info_.count(elem_type)) {
        emitConstructAtPtrs(elem_type, dst_ptr, {}, {});
    }
    // For a slid slot: prefer user op<- (move) or op= (copy) taking Type^ if defined.
    if (slid_info_.count(elem_type)) {
        std::string op_base = elem_type + "__" + (is_move ? "op<-" : "op=");
        auto oit = method_overloads_.find(op_base);
        if (oit != method_overloads_.end()) {
            std::string want = elem_type + "^";
            bool empty = slid_info_[elem_type].is_empty;
            for (auto& [m, pt] : oit->second) {
                if (pt.size() == 1 && pt[0] == want) {
                    out_ << "    call void @" << llvmGlobalName(m)
                         << "(" << (empty ? "" : "ptr " + dst_ptr + ", ") << "ptr " << src_ptr << ")\n";
                    return;
                }
            }
        }
    }
    // No user op (or anon-tuple / other): default field-by-field walk.
    emitSlidAssign(elem_type, dst_ptr, src_ptr, is_move, is_init);
}

void Codegen::emitSlidAssign(const std::string& struct_type,
                             const std::string& dst_ptr, const std::string& src_ptr,
                             bool is_move, bool is_init) {
    auto fields = fieldTypesOf(struct_type);
    for (int i = 0; i < (int)fields.size(); i++) {
        const std::string& fslids = fields[i];
        std::string src_gep = emitFieldGep(struct_type, src_ptr, i);
        std::string dst_gep = emitFieldGep(struct_type, dst_ptr, i);
        // embedded slid / anon-tuple: dispatch via slot helper (user op else default walk)
        if (slid_info_.count(fslids) || isAnonTupleType(fslids)) {
            emitSlidSlotAssign(fslids, dst_gep, src_gep, is_move, is_init);
            continue;
        }
        // inline array of slids or pointers: walk each element
        if (isInlineArrayType(fslids)) {
            auto lb = fslids.rfind('[');
            std::string elem_slids = fslids.substr(0, lb);
            int n = std::stoi(fslids.substr(lb + 1, fslids.size() - lb - 2));
            std::string arr_llvm = llvmType(fslids);
            if (slid_info_.count(elem_slids)) {
                for (int k = 0; k < n; k++) {
                    std::string s_ep = newTmp();
                    out_ << "    " << s_ep << " = getelementptr " << arr_llvm
                         << ", ptr " << src_gep << ", i32 0, i32 " << k << "\n";
                    std::string d_ep = newTmp();
                    out_ << "    " << d_ep << " = getelementptr " << arr_llvm
                         << ", ptr " << dst_gep << ", i32 0, i32 " << k << "\n";
                    emitSlidSlotAssign(elem_slids, d_ep, s_ep, is_move, is_init);
                }
                continue;
            }
            if (isIndirectType(elem_slids)) {
                std::string elem_llvm = llvmType(elem_slids);
                for (int k = 0; k < n; k++) {
                    std::string s_ep = newTmp();
                    out_ << "    " << s_ep << " = getelementptr " << arr_llvm
                         << ", ptr " << src_gep << ", i32 0, i32 " << k << "\n";
                    std::string d_ep = newTmp();
                    out_ << "    " << d_ep << " = getelementptr " << arr_llvm
                         << ", ptr " << dst_gep << ", i32 0, i32 " << k << "\n";
                    std::string v = newTmp();
                    out_ << "    " << v << " = load " << elem_llvm << ", ptr " << s_ep << "\n";
                    out_ << "    store " << elem_llvm << " " << v << ", ptr " << d_ep << "\n";
                    if (is_move)
                        out_ << "    store ptr null, ptr " << s_ep << "\n";
                }
                continue;
            }
            // inline array of primitives — blit the whole array
            std::string val = newTmp();
            out_ << "    " << val << " = load " << arr_llvm << ", ptr " << src_gep << "\n";
            out_ << "    store " << arr_llvm << " " << val << ", ptr " << dst_gep << "\n";
            continue;
        }
        // pointer/iterator field: transfer (and null source on move)
        std::string ft = llvmType(fslids);
        std::string val = newTmp();
        out_ << "    " << val << " = load " << ft << ", ptr " << src_gep << "\n";
        out_ << "    store " << ft << " " << val << ", ptr " << dst_gep << "\n";
        if (is_move && isIndirectType(fslids))
            out_ << "    store ptr null, ptr " << src_gep << "\n";
    }
}

void Codegen::emitSlidSlotSwap(const std::string& elem_type,
                                const std::string& a_ptr, const std::string& b_ptr) {
    // For a slid slot: prefer user op<->(Type^) if defined.
    if (slid_info_.count(elem_type)) {
        std::string op_base = elem_type + "__op<->";
        auto oit = method_overloads_.find(op_base);
        if (oit != method_overloads_.end()) {
            std::string want = elem_type + "^";
            bool empty = slid_info_[elem_type].is_empty;
            for (auto& [m, pt] : oit->second) {
                if (pt.size() == 1 && pt[0] == want) {
                    out_ << "    call void @" << llvmGlobalName(m)
                         << "(" << (empty ? "" : "ptr " + a_ptr + ", ") << "ptr " << b_ptr << ")\n";
                    return;
                }
            }
        }
    }
    // No user op<-> (or anon-tuple): default field-by-field swap.
    emitSlidSwap(elem_type, a_ptr, b_ptr);
}

void Codegen::emitSlidSwap(const std::string& struct_type,
                            const std::string& a_ptr, const std::string& b_ptr) {
    auto fields = fieldTypesOf(struct_type);
    for (int i = 0; i < (int)fields.size(); i++) {
        const std::string& fslids = fields[i];
        std::string a_gep = emitFieldGep(struct_type, a_ptr, i);
        std::string b_gep = emitFieldGep(struct_type, b_ptr, i);
        // embedded slid / anon-tuple: dispatch via slot helper
        if (slid_info_.count(fslids) || isAnonTupleType(fslids)) {
            emitSlidSlotSwap(fslids, a_gep, b_gep);
            continue;
        }
        // inline arrays: deferred — array handling will be revisited
        if (isInlineArrayType(fslids)) {
            throw std::runtime_error("swap of inline-array fields not yet supported (field type '"
                + fslids + "')");
        }
        // pointer/iterator or primitive field: 4-load/store exchange (no nullification)
        std::string ft = llvmType(fslids);
        std::string av = newTmp();
        std::string bv = newTmp();
        out_ << "    " << av << " = load " << ft << ", ptr " << a_gep << "\n";
        out_ << "    " << bv << " = load " << ft << ", ptr " << b_gep << "\n";
        out_ << "    store " << ft << " " << bv << ", ptr " << a_gep << "\n";
        out_ << "    store " << ft << " " << av << ", ptr " << b_gep << "\n";
    }
}

void Codegen::emitElementwiseAtPtr(const std::string& ttype,
                                    const std::string& l_ptr, const std::string& r_ptr,
                                    const std::string& res_ptr, const std::string& op) {
    auto fields = fieldTypesOf(ttype);
    for (int i = 0; i < (int)fields.size(); i++) {
        const std::string& ft = fields[i];
        std::string l_gep = emitFieldGep(ttype, l_ptr, i);
        std::string r_gep = emitFieldGep(ttype, r_ptr, i);
        std::string res_gep = emitFieldGep(ttype, res_ptr, i);
        // nested anon-tuple slot: recurse
        if (isAnonTupleType(ft)) {
            emitElementwiseAtPtr(ft, l_gep, r_gep, res_gep, op);
            continue;
        }
        // slid slot: dispatch user op<op>(Elem^, Elem^)
        if (slid_info_.count(ft)) {
            std::string op_base = ft + "__op" + op;
            auto oit = method_overloads_.find(op_base);
            std::string mangled;
            std::string want = ft + "^";
            if (oit != method_overloads_.end()) {
                for (auto& [m, pt] : oit->second) {
                    if (pt.size() == 2 && pt[0] == want && pt[1] == want) {
                        mangled = m; break;
                    }
                }
            }
            if (mangled.empty())
                throw std::runtime_error("slid element type '" + ft
                    + "' has no op" + op + "(" + ft + "^, " + ft + "^)");
            std::string ret_t = func_return_types_.count(mangled)
                ? func_return_types_[mangled] : "";
            bool is_method = (ret_t == "void");
            std::string args = is_method
                ? ("ptr " + res_gep)
                : ("ptr sret(%struct." + ft + ") " + res_gep);
            args += ", ptr " + l_gep + ", ptr " + r_gep;
            out_ << "    call void @" << llvmGlobalName(mangled)
                 << "(" << args << ")\n";
            continue;
        }
        // scalar slot: load both operands, emit scalar op, store
        std::string elem_llvm = llvmType(ft);
        bool unsig = (ft == "uint" || ft == "uint8" || ft == "uint16"
                   || ft == "uint32" || ft == "uint64"
                   || ft == "char" || ft == "bool");
        std::string instr;
        if      (op == "+")  instr = "add";
        else if (op == "-")  instr = "sub";
        else if (op == "*")  instr = "mul";
        else if (op == "/")  instr = unsig ? "udiv" : "sdiv";
        else if (op == "%")  instr = unsig ? "urem" : "srem";
        else if (op == "&")  instr = "and";
        else if (op == "|")  instr = "or";
        else if (op == "^")  instr = "xor";
        else if (op == "<<") instr = "shl";
        else if (op == ">>") instr = unsig ? "lshr" : "ashr";
        else throw std::runtime_error("elementwise: unsupported op '" + op + "'");
        std::string lv = newTmp();
        out_ << "    " << lv << " = load " << elem_llvm << ", ptr " << l_gep << "\n";
        std::string rv = newTmp();
        out_ << "    " << rv << " = load " << elem_llvm << ", ptr " << r_gep << "\n";
        std::string rtmp = newTmp();
        out_ << "    " << rtmp << " = " << instr << " " << elem_llvm
             << " " << lv << ", " << rv << "\n";
        out_ << "    store " << elem_llvm << " " << rtmp
             << ", ptr " << res_gep << "\n";
    }
}

void Codegen::emitTupleScalarBroadcastAtPtr(const std::string& ttype,
                                             const std::string& tup_ptr,
                                             const std::string& scalar_val,
                                             const std::string& scalar_slids,
                                             const std::string& res_ptr,
                                             const std::string& op,
                                             bool scalar_on_left) {
    auto fields = fieldTypesOf(ttype);
    for (int i = 0; i < (int)fields.size(); i++) {
        const std::string& ft = fields[i];
        std::string t_gep = emitFieldGep(ttype, tup_ptr, i);
        std::string r_gep = emitFieldGep(ttype, res_ptr, i);
        if (isAnonTupleType(ft)) {
            emitTupleScalarBroadcastAtPtr(ft, t_gep, scalar_val, scalar_slids,
                                          r_gep, op, scalar_on_left);
            continue;
        }
        if (slid_info_.count(ft)) {
            std::string op_base = ft + "__op" + op;
            auto oit = method_overloads_.find(op_base);
            std::string mangled;
            std::string want_left = ft + "^";
            if (oit != method_overloads_.end()) {
                for (auto& [m, pt] : oit->second) {
                    if (pt.size() == 2 && pt[0] == want_left && pt[1] == scalar_slids) {
                        mangled = m; break;
                    }
                }
            }
            if (mangled.empty())
                throw std::runtime_error("broadcast: slid slot '" + ft + "' has no op"
                    + op + "(" + ft + "^, " + scalar_slids + ")");
            std::string ret_t = func_return_types_.count(mangled)
                ? func_return_types_[mangled] : "";
            bool is_method = (ret_t == "void");
            std::string args = is_method
                ? ("ptr " + r_gep)
                : ("ptr sret(%struct." + ft + ") " + r_gep);
            args += ", ptr " + t_gep + ", " + llvmType(scalar_slids) + " " + scalar_val;
            out_ << "    call void @" << llvmGlobalName(mangled) << "(" << args << ")\n";
            continue;
        }
        // scalar slot — strict type-match with scalar
        if (ft != scalar_slids)
            throw std::runtime_error("broadcast: tuple slot type '" + ft
                + "' does not match scalar type '" + scalar_slids + "'");
        std::string elem_llvm = llvmType(ft);
        bool unsig = (ft == "uint" || ft == "uint8" || ft == "uint16"
                   || ft == "uint32" || ft == "uint64"
                   || ft == "char" || ft == "bool");
        std::string instr;
        if      (op == "+")  instr = "add";
        else if (op == "-")  instr = "sub";
        else if (op == "*")  instr = "mul";
        else if (op == "/")  instr = unsig ? "udiv" : "sdiv";
        else if (op == "%")  instr = unsig ? "urem" : "srem";
        else if (op == "&")  instr = "and";
        else if (op == "|")  instr = "or";
        else if (op == "^")  instr = "xor";
        else if (op == "<<") instr = "shl";
        else if (op == ">>") instr = unsig ? "lshr" : "ashr";
        else throw std::runtime_error("broadcast: unsupported op '" + op + "'");
        std::string slot_val = newTmp();
        out_ << "    " << slot_val << " = load " << elem_llvm << ", ptr " << t_gep << "\n";
        std::string lhs = scalar_on_left ? scalar_val : slot_val;
        std::string rhs = scalar_on_left ? slot_val : scalar_val;
        std::string rtmp = newTmp();
        out_ << "    " << rtmp << " = " << instr << " " << elem_llvm
             << " " << lhs << ", " << rhs << "\n";
        out_ << "    store " << elem_llvm << " " << rtmp << ", ptr " << r_gep << "\n";
    }
}

// Alloca a fresh instance of slid_name, default-init all fields, run ctor body, call __$ctor.
// Returns the alloca register. Does NOT register for dtor (caller's responsibility if needed).
std::string Codegen::emitSlidAlloca(const std::string& slid_name) {
    auto& info = slid_info_[slid_name];
    if (info.is_virtual_class) {
        for (auto& slot : info.vtable) {
            if (slot.is_pure)
                throw std::runtime_error("cannot instantiate pure virtual class '" + slid_name
                    + "': method '" + slot.method_name + "' is not defined");
        }
    }
    std::string reg = newTmp();
    if (info.has_pinit) {
        std::string sz = newTmp();
        out_ << "    " << sz << " = call i64 @" << slid_name << "__$sizeof()\n";
        out_ << "    " << reg << " = alloca i8, i64 " << sz << "\n";
    } else {
        out_ << "    " << reg << " = alloca %struct." << slid_name << "\n";
    }
    // Walk the inheritance chain base→derived. For each class C: init C's own
    // fields (using flat indices into the leaf's struct), emit C's implicit
    // ctor body if any, then conditionally call C's __$pinit / __$ctor.
    auto chain = chainOf(slid_name);
    for (SlidInfo* c_info : chain) {
        const SlidDef* c_def = nullptr;
        for (auto& s : program_.slids) if (s.name == c_info->name) { c_def = &s; break; }
        bool owns_vptr = c_info->is_virtual_class && c_info->base_info == nullptr;
        if (c_info->is_virtual_class) {
            std::string vptr_gep = newTmp();
            out_ << "    " << vptr_gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << reg << ", i32 0, i32 0\n";
            out_ << "    store ptr @_ZTV" << c_info->name
                 << ", ptr " << vptr_gep << "\n";
        }
        if (!c_info->has_pinit) {
            int vptr_local = owns_vptr ? 1 : 0;
            int n_own = c_info->own_field_count - vptr_local;
            for (int j = 0; j < n_own; j++) {
                int flat_idx = c_info->base_field_count + vptr_local + j;
                const std::string& ftype = info.field_types[flat_idx];
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << slid_name
                     << ", ptr " << reg << ", i32 0, i32 " << flat_idx << "\n";
                std::string val;
                if (c_def && j < (int)c_def->fields.size() && c_def->fields[j].default_val)
                    val = emitExpr(*c_def->fields[j].default_val);
                else
                    val = isInlineArrayType(ftype) ? "zeroinitializer"
                        : isIndirectType(ftype) ? "null"
                        : (ftype == "float32" || ftype == "float64") ? "0.0"
                        : "0";
                out_ << "    store " << llvmType(ftype) << " " << val << ", ptr " << gep << "\n";
            }
        }
        if (c_def && c_def->ctor_body) {
            std::string saved_slid = current_slid_;
            std::string saved_self = self_ptr_;
            current_slid_ = c_info->name;
            self_ptr_ = reg;
            emitBlock(*c_def->ctor_body);
            current_slid_ = saved_slid;
            self_ptr_ = saved_self;
        }
        if (c_info->has_pinit)
            out_ << "    call void @" << c_info->name << "__$pinit(ptr " << reg << ")\n";
        else if (c_info->has_explicit_ctor)
            out_ << "    call void @" << c_info->name << "__$ctor("
                 << (c_info->is_empty ? "" : "ptr " + reg) << ")\n";
    }
    if (hasDtorInChain(slid_name))
        pending_temp_dtors_.push_back({reg, slid_name});
    return reg;
}

std::string Codegen::emitRawSlidAlloca(const std::string& slid_name) {
    auto& info = slid_info_[slid_name];
    std::string reg = newTmp();
    if (info.has_pinit) {
        std::string sz = newTmp();
        out_ << "    " << sz << " = call i64 @" << slid_name << "__$sizeof()\n";
        out_ << "    " << reg << " = alloca i8, i64 " << sz << "\n";
    } else {
        out_ << "    " << reg << " = alloca %struct." << slid_name << "\n";
    }
    return reg;
}

void Codegen::emitConstructAt(const std::string& stype, const std::string& ptr,
                              const std::vector<std::unique_ptr<Expr>>& args,
                              const std::vector<std::unique_ptr<Expr>>& overrides) {
    std::vector<const Expr*> a; a.reserve(args.size());
    for (auto& p : args) a.push_back(p.get());
    std::vector<const Expr*> o; o.reserve(overrides.size());
    for (auto& p : overrides) o.push_back(p.get());
    emitConstructAtPtrs(stype, ptr, a, o);
}

void Codegen::emitConstructAtPtrs(const std::string& stype, const std::string& ptr,
                                  const std::vector<const Expr*>& args,
                                  const std::vector<const Expr*>& overrides) {
    bool is_slid = slid_info_.count(stype) != 0;
    bool is_tuple = isAnonTupleType(stype);
    if (!is_slid && !is_tuple) return;

    auto fields = fieldTypesOf(stype);
    int nfields = (int)fields.size();
    if ((int)args.size() > nfields)
        throw std::runtime_error("too many initializers: '" + stype + "' has "
            + std::to_string(nfields) + " fields, got " + std::to_string(args.size()));
    if ((int)overrides.size() > nfields)
        throw std::runtime_error("too many tuple values: '" + stype + "' has "
            + std::to_string(nfields) + " fields, got " + std::to_string(overrides.size()));

    // transport-only fast path (no inheritance): caller writes nothing, __$pinit handles init
    if (is_slid && slid_info_[stype].has_pinit && slid_info_[stype].base_info == nullptr) {
        out_ << "    call void @" << stype << "__$pinit(ptr " << ptr << ")\n";
        return;
    }

    auto slidDefFor = [&](const std::string& name) -> const SlidDef* {
        for (auto& s : program_.slids) if (s.name == name) return &s;
        auto it = concrete_slid_template_defs_.find(name);
        if (it != concrete_slid_template_defs_.end()) return &it->second;
        return nullptr;
    };

    auto initFieldFromExpr = [&](const std::string& ftype, const std::string& gep,
                                 const Expr* arg_expr) {
        if (slid_info_.count(ftype)) {
            if (!arg_expr) {
                emitConstructAtPtrs(ftype, gep, {}, {});
            } else if (inferSlidType(*arg_expr) == ftype) {
                std::string src = emitExpr(*arg_expr);
                emitSlidSlotAssign(ftype, gep, src, /*is_move=*/false, /*is_init=*/true);
            } else {
                std::vector<const Expr*> one{ arg_expr };
                emitConstructAtPtrs(ftype, gep, one, {});
            }
            return;
        }
        std::string val;
        if (arg_expr) {
            val = emitExpr(*arg_expr);
        } else {
            val = isInlineArrayType(ftype) ? "zeroinitializer"
                : isIndirectType(ftype) ? "null"
                : (ftype == "float32" || ftype == "float64") ? "0.0"
                : "0";
        }
        out_ << "    store " << llvmType(ftype) << " " << val << ", ptr " << gep << "\n";
    };

    if (is_tuple) {
        // tuple types: no chain; flat init
        const SlidDef* slid_def = nullptr;
        for (int i = 0; i < nfields; i++) {
            const Expr* arg_expr = nullptr;
            if (i < (int)overrides.size())      arg_expr = overrides[i];
            else if (i < (int)args.size())      arg_expr = args[i];
            else if (slid_def && i < (int)slid_def->fields.size() && slid_def->fields[i].default_val)
                arg_expr = slid_def->fields[i].default_val.get();
            std::string gep = emitFieldGep(stype, ptr, i);
            initFieldFromExpr(fields[i], gep, arg_expr);
        }
        return;
    }

    // slid: walk inheritance chain base→derived using flat indices into the leaf's struct.
    // Virtual classes have a $vptr at flat index 0 (synthetic, not in the user
    // arg list); each class's ctor sets the vptr to its own vtable as the chain
    // walks down (Itanium ABI), so the most-derived ctor leaves the correct
    // vtable in place. user-arg index = flat_idx - args_vptr_skew.
    auto chain = chainOf(stype);
    bool root_virtual = !slid_info_.at(stype).is_virtual_class ? false : true;
    int args_vptr_skew = root_virtual ? 1 : 0;
    for (SlidInfo* c_info : chain) {
        const SlidDef* c_def = slidDefFor(c_info->name);
        bool owns_vptr = c_info->is_virtual_class && c_info->base_info == nullptr;
        if (c_info->is_virtual_class) {
            std::string vptr_gep = emitFieldGep(stype, ptr, 0);
            out_ << "    store ptr @_ZTV" << c_info->name
                 << ", ptr " << vptr_gep << "\n";
        }
        if (!c_info->has_pinit) {
            int vptr_local = owns_vptr ? 1 : 0;
            int n_own = c_info->own_field_count - vptr_local;
            for (int j = 0; j < n_own; j++) {
                int flat_idx = c_info->base_field_count + vptr_local + j;
                const std::string& ftype = fields[flat_idx];
                int args_idx = flat_idx - args_vptr_skew;
                const Expr* arg_expr = nullptr;
                if (args_idx >= 0 && args_idx < (int)overrides.size())
                    arg_expr = overrides[args_idx];
                else if (args_idx >= 0 && args_idx < (int)args.size())
                    arg_expr = args[args_idx];
                else if (c_def && j < (int)c_def->fields.size() && c_def->fields[j].default_val)
                    arg_expr = c_def->fields[j].default_val.get();
                std::string gep = emitFieldGep(stype, ptr, flat_idx);
                initFieldFromExpr(ftype, gep, arg_expr);
            }
        }
        if (c_def && c_def->ctor_body) {
            std::string saved_slid = current_slid_;
            std::string saved_self = self_ptr_;
            current_slid_ = c_info->name;
            self_ptr_ = ptr;
            emitBlock(*c_def->ctor_body);
            current_slid_ = saved_slid;
            self_ptr_ = saved_self;
        } else if (c_info->has_pinit) {
            out_ << "    call void @" << c_info->name << "__$pinit(ptr " << ptr << ")\n";
        } else if (c_info->has_explicit_ctor) {
            out_ << "    call void @" << c_info->name << "__$ctor("
                 << (c_info->is_empty ? "" : "ptr " + ptr) << ")\n";
        }
    }
}

void Codegen::emitStackRestore(int to_frame) {
    for (int i = (int)loop_stack_.size() - 1; i >= to_frame; i--) {
        if (!loop_stack_[i].stack_ptr_reg.empty())
            out_ << "    call void @llvm.stackrestore(ptr " << loop_stack_[i].stack_ptr_reg << ")\n";
    }
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
            std::string tmp_reg = emitRawSlidAlloca(slid_name);
            auto& info = slid_info_[slid_name];
            if (info.has_pinit) {
                out_ << "    call void @" << slid_name << "__$pinit(ptr " << tmp_reg << ")\n";
            } else {
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
                    out_ << "    call void @" << slid_name << "__$ctor("
                         << (info.is_empty ? "" : "ptr " + tmp_reg) << ")\n";
            }
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
            // register for dtor at end of enclosing statement (chain-aware)
            if (hasDtorInChain(slid_name))
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
        // DerefExpr of a slid pointer (sa^): pass the pointer value directly without re-deref.
        if (auto* de = dynamic_cast<const DerefExpr*>(&arg)) {
            if (!derefSlidName(*de).empty()) {
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load ptr, ptr " << locals_.at(ve->name) << "\n";
                    return loaded;
                }
                return emitExpr(*de->operand);
            }
        }
        // anon-tuple-ref param: `(t1,...)^`
        std::string tup_inner = param_type.substr(0, param_type.size() - 1);
        if (isAnonTupleType(tup_inner)) {
            // ^my_tuple → pass alloca ptr
            if (auto* ao = dynamic_cast<const AddrOfExpr*>(&arg)) {
                if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
                    auto lit = local_types_.find(ve->name);
                    if (lit != local_types_.end() && isAnonTupleType(lit->second))
                        return locals_.at(ve->name);
                }
            }
            // bare my_tuple → auto-promote (analogous to implicit slid-ref promotion)
            if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
                auto lit = local_types_.find(ve->name);
                if (lit != local_types_.end() && isAnonTupleType(lit->second))
                    return locals_.at(ve->name);
            }
            // deref of a tuple-ref local: load the ptr
            if (auto* de = dynamic_cast<const DerefExpr*>(&arg)) {
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto lit = local_types_.find(ve->name);
                    if (lit != local_types_.end() && lit->second == param_type) {
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load ptr, ptr " << locals_.at(ve->name) << "\n";
                        return loaded;
                    }
                }
            }
            // tuple-literal arg: materialize into a temp alloca, pass its ptr
            std::string arg_slids = inferSlidType(arg);
            if (isAnonTupleType(arg_slids)) {
                std::string v = emitExpr(arg);
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = alloca " << llvmType(tup_inner) << "\n";
                out_ << "    store " << llvmType(tup_inner) << " " << v << ", ptr " << tmp << "\n";
                return tmp;
            }
        }
    }
    // implicit construction: non-slid value passed to SlidType^ param — find matching op=
    if (want_ptr) {
        std::string slid_name = param_type.substr(0, param_type.size() - 1);
        if (slid_info_.count(slid_name)) {
            auto& info = slid_info_[slid_name];
            std::string arg_llvm = exprLlvmType(arg);
            static const std::map<std::string,int> irank = {{"i8",1},{"i16",2},{"i32",3},{"i64",4}};
            auto ait = irank.find(arg_llvm);
            auto oit = method_overloads_.find(slid_name + "__op=");
            if (oit != method_overloads_.end()) {
                for (auto& [m, ptypes2] : oit->second) {
                    if (ptypes2.empty()) continue;
                    std::string p_llvm = llvmType(ptypes2[0]);
                    auto pit = irank.find(p_llvm);
                    // get raw Slids type of arg (works for non-slid types like char[] too)
                    std::string arg_slids;
                    if (auto* ve2 = dynamic_cast<const VarExpr*>(&arg)) {
                        auto tit2 = local_types_.find(ve2->name);
                        if (tit2 != local_types_.end()) arg_slids = tit2->second;
                    }
                    if (arg_slids.empty()) arg_slids = exprSlidType(arg);
                    if (arg_slids.empty() && dynamic_cast<const StringLiteralExpr*>(&arg)) arg_slids = "char[]";
                    if (arg_slids.empty()) arg_slids = inferSlidType(arg);
                    // scalars: exact llvm type; pointers: exact Slids type (char[] != String^)
                    bool exact  = (p_llvm == arg_llvm && p_llvm != "ptr")
                               || (arg_llvm == "ptr" && ptypes2[0] == arg_slids);
                    bool widen  = (ait != irank.end() && pit != irank.end()
                                   && pit->second >= ait->second)
                               && (arg_slids != "bool" || ptypes2[0] == "bool");
                    if (!exact && !widen) continue;
                    std::string tmp = emitRawSlidAlloca(slid_name);
                    if (info.has_pinit) {
                        // transport type: __$pinit zeros private fields and chains to __$ctor
                        out_ << "    call void @" << slid_name << "__$pinit(ptr " << tmp << ")\n";
                    } else {
                        for (int i2 = 0; i2 < (int)info.field_types.size(); i2++) {
                            std::string gep = newTmp();
                            out_ << "    " << gep << " = getelementptr %struct." << slid_name
                                 << ", ptr " << tmp << ", i32 0, i32 " << i2 << "\n";
                            if (isIndirectType(info.field_types[i2]))
                                out_ << "    store ptr null, ptr " << gep << "\n";
                            else
                                out_ << "    store " << llvmType(info.field_types[i2]) << " 0, ptr " << gep << "\n";
                        }
                        if (info.has_explicit_ctor)
                            out_ << "    call void @" << slid_name << "__$ctor("
                                 << (info.is_empty ? "" : "ptr " + tmp) << ")\n";
                    }
                    std::string av = emitExpr(arg);
                    if (widen && !exact) {
                        std::string ext = newTmp();
                        out_ << "    " << ext << " = sext " << arg_llvm << " " << av << " to " << p_llvm << "\n";
                        av = ext;
                    }
                    out_ << "    call void @" << llvmGlobalName(m)
                         << "(ptr " << tmp << ", " << p_llvm << " " << av << ")\n";
                    if (hasDtorInChain(slid_name))
                        pending_temp_dtors_.push_back({tmp, slid_name});
                    return tmp;
                }
            }
        }
    }

    // auto-promote slid-producing expressions (CallExpr, BinaryExpr with op
    // overload, MethodCallExpr returning a slid) to their alloca ptr.
    // The earlier VarExpr-only auto-promote misses these forms; emitExpr already
    // produces a ptr for them, so return it directly before requirePtrInit runs.
    if (!param_type.empty() && param_type.back() == '^') {
        std::string slid_name = param_type.substr(0, param_type.size() - 1);
        if (slid_info_.count(slid_name) && inferSlidType(arg) == slid_name)
            return emitExpr(arg);
    }
    requirePtrInit(param_type, arg);
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

void Codegen::emitSlidMethod(const SlidDef& slid,
                              const std::string& method_user_name,
                              const std::string& full_mangled,
                              const std::string& return_type,
                              const std::vector<std::pair<std::string,std::string>>& params,
                              const BlockStmt& body) {
    locals_.clear();
    local_types_.clear();
    emitted_alloca_regs_.clear();
    dtor_vars_.clear();
    tmp_counter_ = 0;
    label_counter_ = 0;
    break_label_ = "";
    continue_label_ = "";
    current_slid_ = slid.name;
    block_terminated_ = false;
    current_func_name_ = method_user_name;

    bool uses_sret = !return_type.empty() && slid_info_.count(return_type) > 0;
    current_func_uses_sret_ = uses_sret;
    std::string ret_type = uses_sret ? "void" : llvmType(return_type);
    current_func_return_type_ = ret_type;
    current_func_slids_return_type_ = return_type;

    bool empty = slid_info_[slid.name].is_empty;
    std::string param_str;
    if (!empty) param_str = "ptr %self";
    if (uses_sret) {
        if (!param_str.empty()) param_str += ", ";
        param_str += "ptr sret(%struct." + return_type + ") %retval";
    }
    for (auto& [type, name] : params) {
        if (!param_str.empty()) param_str += ", ";
        param_str += llvmType(type) + " %arg_" + name;
    }

    out_ << "define " << (isExported(full_mangled) ? "" : "internal ") << ret_type << " @" << llvmGlobalName(full_mangled)
         << "(" << param_str << ") {\n";
    out_ << "entry:\n";

    for (auto& [type, name] : params) {
        std::string reg = uniqueAllocaReg(name);
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        locals_[name] = reg;
        local_types_[name] = type;
    }

    emitBlock(body);

    if (!block_terminated_) {
        if (return_type == "void" || uses_sret) {
            emitDtors();
            out_ << "    ret void\n";
        } else {
            throw std::runtime_error("method '" + full_mangled + "' is missing a return statement");
        }
    }

    out_ << "}\n\n";
}

void Codegen::emitSlidMethods(const SlidDef& slid) {
    // emit inline methods that have a body (not forward decls)
    for (auto& m : slid.methods) {
        if (!m.body) continue;
        std::string mangled = resolveMethodMangledName(slid.name, m.name, m.params);
        current_func_tuple_fields_.clear();
        emitSlidMethod(slid, m.name, mangled, m.return_type, m.params, *m.body);
    }
    // emit external method definitions for this slid
    for (auto& em : program_.external_methods) {
        if (em.slid_name != slid.name || !em.body) continue;
        if (em.method_name == "_" || em.method_name == "~") continue;
        std::string mangled = resolveMethodMangledName(slid.name, em.method_name, em.params);
        current_func_tuple_fields_.clear();
        emitSlidMethod(slid, em.method_name, mangled, em.return_type, em.params, *em.body);
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
    current_func_name_ = !fn.user_name.empty() ? fn.user_name : fn.name;
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
    current_func_slids_return_type_ = func_return_types_[emit_name];
    current_func_tuple_fields_ = fn.tuple_return_fields;

    std::string param_str;
    if (uses_sret) {
        param_str = "ptr sret(%struct." + fn.return_type + ") %retval";
    }
    for (int i = 0; i < (int)fn.params.size(); i++) {
        if (!param_str.empty()) param_str += ", ";
        param_str += llvmType(fn.params[i].first) + " %arg_" + fn.params[i].second;
    }

    out_ << "define " << (isExported(emit_name) ? "" : "internal ") << ret_type << " @" << llvmGlobalName(emit_name) << "(" << param_str << ") {\n";
    out_ << "entry:\n";

    for (auto& [type, name] : fn.params) {
        std::string reg = uniqueAllocaReg(name);
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
            } else if (auto* ft = dynamic_cast<const ForTupleStmt*>(stmt.get())) {
                emitNested(*ft->body);
            } else if (auto* fa = dynamic_cast<const ForArrayStmt*>(stmt.get())) {
                emitNested(*fa->body);
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
        // destroy implicit temporaries created during this statement (chain derived→base)
        for (int i = (int)pending_temp_dtors_.size() - 1; i >= (int)temp_mark; i--) {
            auto& td = pending_temp_dtors_[i];
            auto chain = chainOf(td.second);
            for (int ci = (int)chain.size() - 1; ci >= 0; ci--) {
                SlidInfo* c = chain[ci];
                if (!c->has_dtor) continue;
                out_ << "    call void @" << c->name << "__$dtor("
                     << (c->is_empty ? "" : "ptr " + td.first) << ")\n";
            }
        }
        pending_temp_dtors_.resize(temp_mark);
    }
}

// returns the ptr to a field in a struct instance
std::string Codegen::emitFieldPtr(const std::string& obj_name, const std::string& field) {
    std::string slid_name;
    std::string obj_ptr;

    auto type_it = local_types_.find(obj_name);
    if (type_it != local_types_.end()) {
        slid_name = type_it->second;
        auto loc_it = locals_.find(obj_name);
        if (loc_it == locals_.end())
            throw std::runtime_error("undefined variable: " + obj_name);
        obj_ptr = loc_it->second;
    } else if (!current_slid_.empty()) {
        // obj_name may be a field of the current slid, accessed via %self
        auto& parent_info = slid_info_[current_slid_];
        auto parent_it = parent_info.field_index.find(obj_name);
        if (parent_it == parent_info.field_index.end())
            throw std::runtime_error("unknown type for variable: " + obj_name);
        int parent_idx = parent_it->second;
        slid_name = parent_info.field_types[parent_idx];
        std::string self_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
        std::string parent_gep = newTmp();
        out_ << "    " << parent_gep << " = getelementptr %struct." << current_slid_
             << ", ptr " << self_ptr << ", i32 0, i32 " << parent_idx << "\n";
        obj_ptr = parent_gep;
    } else {
        throw std::runtime_error("unknown type for variable: " + obj_name);
    }

    auto& info = slid_info_[slid_name];
    auto field_it = info.field_index.find(field);
    if (field_it == info.field_index.end())
        throw std::runtime_error("unknown field: " + field + " in " + slid_name);

    int idx = field_it->second;
    std::string gep = newTmp();
    out_ << "    " << gep << " = getelementptr %struct." << slid_name
         << ", ptr " << obj_ptr << ", i32 0, i32 " << idx << "\n";
    return gep;
}
