#include "codegen.h"
#include "source_map.h"
#include <sstream>
#include <functional>
#include <stdexcept>
#include <set>
#include <climits>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "codegen_helpers.h"

Codegen::Codegen(const Program& program, std::ostream& out, SourceMap& sm, std::string source_file)
    : program_(program), out_(out), sm_(sm),
      str_counter_(0), tmp_counter_(0), label_counter_(0),
      source_file_(std::move(source_file)) {}

// Ensure every diagnostic ends with terminal punctuation. Messages built
// with concatenations forget periods constantly; folding the rule into the
// error helpers keeps every site sentence-shaped without per-call upkeep.
static std::string finalizeErrorMsg(std::string msg) {
    if (msg.empty()) return msg;
    char last = msg.back();
    if (last != '.' && last != '!' && last != '?') msg.push_back('.');
    return msg;
}

void Codegen::error(const std::string& msg) {
    if (!emit_stack_.empty()) {
        auto [f, t] = emit_stack_.back();
        throw CompileError{f, t, finalizeErrorMsg(msg)};
    }
    throw CompileError{-1, 0, finalizeErrorMsg(msg)};
}

void Codegen::errorAtNode(const Stmt& s, const std::string& msg) {
    throw CompileError{s.file_id, s.tok, finalizeErrorMsg(msg)};
}

void Codegen::errorAtNode(const Expr& e, const std::string& msg) {
    throw CompileError{e.file_id, e.tok, finalizeErrorMsg(msg)};
}

void Codegen::errorWithNote(const std::string& msg,
                            int note_file, int note_tok,
                            const std::string& note_msg) {
    int f = -1, t = 0;
    if (!emit_stack_.empty()) std::tie(f, t) = emit_stack_.back();
    throw CompileError{f, t, finalizeErrorMsg(msg)}
        .addNote(note_file, note_tok, finalizeErrorMsg(note_msg));
}

void Codegen::errorAtNodeWithNote(const Stmt& s, const std::string& msg,
                                  int note_file, int note_tok,
                                  const std::string& note_msg) {
    throw CompileError{s.file_id, s.tok, finalizeErrorMsg(msg)}
        .addNote(note_file, note_tok, finalizeErrorMsg(note_msg));
}

void Codegen::errorAtNodeWithNote(const Expr& e, const std::string& msg,
                                  int note_file, int note_tok,
                                  const std::string& note_msg) {
    throw CompileError{e.file_id, e.tok, finalizeErrorMsg(msg)}
        .addNote(note_file, note_tok, finalizeErrorMsg(note_msg));
}

void Codegen::rejectConstToMutable(
    const std::string& display_name,
    const std::vector<std::unique_ptr<Expr>>& args,
    const std::vector<bool>& param_mutable,
    const std::vector<int>& param_mut_toks,
    int param_file_id)
{
    size_t n = std::min(args.size(), param_mutable.size());
    for (size_t i = 0; i < n; i++) {
        if (!param_mutable[i]) continue;
        std::string at = exprType(*args[i]);
        if (!typeHasConst(at)) continue;
        int note_tok = (i < param_mut_toks.size()) ? param_mut_toks[i] : 0;
        errorWithNote(
            "Cannot pass const argument to mutable parameter of '"
                + display_name + "'.",
            param_file_id, note_tok,
            "Parameter declared 'mutable' here.");
    }
}

void Codegen::rejectConstToMutable(
    const std::string& display_name,
    const std::vector<std::unique_ptr<Expr>>& args,
    const OverloadEntry& entry)
{
    rejectConstToMutable(display_name, args,
        std::get<2>(entry), std::get<3>(entry), std::get<4>(entry));
}

void Codegen::checkResolvedFreeFunction(
    const std::string& callee,
    const std::string& mangled,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    auto foit = free_func_overloads_.find(callee);
    if (foit == free_func_overloads_.end()) return;
    for (auto& entry : foit->second) {
        if (std::get<0>(entry) != mangled) continue;
        rejectConstToMutable(callee, args, entry);
        // An anon-tuple argument must structurally match the parameter's
        // tuple type (deref'd and const-stripped). Mismatched element types,
        // or a tuple passed where a non-tuple is expected, are rejected.
        auto& ptypes = std::get<1>(entry);
        for (size_t i = 0; i < args.size() && i < ptypes.size(); i++) {
            std::string at = inferSlidType(*args[i]);
            if (!isAnonTupleType(at)) continue;
            std::string pbase = (isRefType(ptypes[i]) || isPtrType(ptypes[i]))
                ? pointeeInfo(ptypes[i]).name
                : canonicalType(ptypes[i]);
            if (canonicalType(at) != pbase)
                errorAtNode(*args[i], "Argument of type '" + at
                    + "' does not match parameter type '" + ptypes[i] + "'.");
        }
        return;
    }
}

void Codegen::checkConstReceiver(
    const Expr& object,
    const std::string& method_name,
    const std::string& mangled)
{
    // Destructor is the destruction window — always callable on const objects.
    if (method_name == "~") return;
    // Method is explicitly const-marked: callable on const receivers.
    if (const_methods_.count(mangled)) return;
    // Receiver's effective slids type carries `const`? If not, no enforcement.
    if (!typeHasConst(exprType(object))) return;
    errorAtNode(object,
        "Cannot call non-const method '" + method_name + "' on a const target.");
}

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


std::string Codegen::canonType(std::string t) const {
    while (true) {
        if (t.rfind("const ", 0) == 0) t = t.substr(6);
        else if (t.rfind("mutable ", 0) == 0) t = t.substr(8);
        else break;
    }
    if (t.size() >= 2 && t.front() == '(') {
        int depth = 1, i = 1;
        while (i < (int)t.size() && depth > 0) {
            if (t[i] == '(') depth++;
            else if (t[i] == ')') depth--;
            if (depth == 0) break;
            i++;
        }
        if (i < (int)t.size() && t[i] == ')') {
            std::string inner = t.substr(1, i - 1);
            std::string suffix = t.substr(i + 1);
            if (inner.rfind("const ", 0) == 0
                || inner.rfind("mutable ", 0) == 0) {
                return canonType(canonType(inner) + suffix);
            }
        }
    }
    return t;
}

std::string Codegen::llvmType(const std::string& raw_t) {
    std::string t = canonType(raw_t);
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
    if (t == "float" || t == "float32") return "float";
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
        // Caller view of the signature matches the body's view: default-const
        // applied per slot, so overload identity is preserved (canonicalType
        // collapses both forms to the same).
        std::vector<std::string> ptypes = buildParamTypes(fn.params, fn.param_mutable);

        std::string mangled = mangleFreeFunction(fn.name, ptypes);
        free_func_overloads_[fn.name].push_back({mangled, ptypes, fn.param_mutable, fn.param_mut_toks, fn.file_id});

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
                    func_param_types_[mangled] =
                        buildParamTypes(nfs->def.params, nfs->def.param_mutable);
                } else if (auto* fl = dynamic_cast<const ForLongStmt*>(stmt.get())) {
                    findNested(*fl->update_block);
                    findNested(*fl->body);
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
        struct Entry {
            std::string ret;
            std::vector<std::pair<std::string,std::string>> params;
            std::vector<bool> param_mutable;
            std::vector<int> param_mut_toks;
            int file_id = 0;
            bool is_const = false;
        };
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
            // = default / = delete have no own body; the impl lives in an
            // ancestor (default) or nowhere (delete / pure-virtual). Registering
            // would emit a `Derived__m` reference that no body satisfies.
            if (m.is_default || m.is_delete) continue;
            by_name[m.name].push_back({m.return_type, m.params, m.param_mutable, m.param_mut_toks, m.file_id, m.is_const_method});
        }
        for (auto& em : program_.external_methods) {
            if (em.slid_name != slid.name || !em.body) continue;
            if (em.method_name == "_" || em.method_name == "~") continue;
            by_name[em.method_name].push_back({em.return_type, em.params, em.param_mutable, em.param_mut_toks, em.file_id, em.is_const_method});
        }
        // external forward decls: register signature for methods not defined in this TU
        for (auto& em : program_.external_methods) {
            if (em.slid_name != slid.name || em.body) continue;
            if (em.method_name == "_" || em.method_name == "~") continue;
            if (em.is_default || em.is_delete) continue;
            if (!by_name.count(em.method_name))
                by_name[em.method_name].push_back({em.return_type, em.params, em.param_mutable, em.param_mut_toks, em.file_id, em.is_const_method});
        }
        // op<-> may only take a single SameType^ parameter — reject anything else.
        if (auto it = by_name.find("op<->"); it != by_name.end()) {
            std::string want = slid.name + "^";
            for (auto& e : it->second) {
                if (e.params.size() != 1 || e.params[0].first != want)
                    error(std::string("Operator op<-> on class '" + slid.name
                        + "' must take exactly one parameter of type '" + want + "'."));
            }
        }
        // Arity-0 unary (op+/-/~/!) and comparison ops (op==/!=/</></><=/>=)
        // must return a built-in type. Empty ret means "returns self" — also a class.
        static const std::set<std::string> cmp_ops = {
            "op==", "op!=", "op<", "op>", "op<=", "op>="
        };
        static const std::set<std::string> unary_ops = {"op+", "op-", "op~", "op!"};
        for (auto& [method_name, entries] : by_name) {
            for (auto& e : entries) {
                bool needs = false;
                if (e.params.size() == 1 && cmp_ops.count(method_name)) needs = true;
                if (e.params.empty() && unary_ops.count(method_name)) needs = true;
                if (!needs) continue;
                // "void" is the parser's default when no explicit return is given on
                // an op-method, which means "returns self" — i.e. a class.
                if (e.ret.empty() || e.ret == "void" || slid_info_.count(e.ret))
                    error(std::string("Operator '" + method_name + "' on class '"
                        + slid.name + "' must return a built-in type (bool, int, pointer), not a class."));
            }
        }
        for (auto& [method_name, entries] : by_name) {
            std::string base = slid.name + "__" + method_name;
            for (auto& e : entries) {
                std::vector<std::string> ptypes = buildParamTypes(e.params, e.param_mutable);
                std::string mangled = mangleMethod(slid.name, method_name, ptypes);
                func_return_types_[mangled] = e.ret;
                func_param_types_[mangled] = ptypes;
                if (e.is_const) const_methods_.insert(mangled);
                // avoid duplicate overload entries (transport slid + impl slid both contribute)
                auto& overloads = method_overloads_[base];
                if (std::none_of(overloads.begin(), overloads.end(),
                        [&](const auto& p){ return std::get<1>(p) == ptypes; }))
                    overloads.push_back({mangled, ptypes, e.param_mutable, e.param_mut_toks, e.file_id});
            }
        }

        // mark exported: methods with a bodyless declaration (from header)
        for (auto& m : slid.methods) {
            if (m.body || m.name == "_" || m.name == "~") continue;
            std::string base = slid.name + "__" + m.name;
            std::vector<std::string> mptypes = buildParamTypes(m.params, m.param_mutable);
            auto it = method_overloads_.find(base);
            if (it != method_overloads_.end()) {
                for (auto& [mangled, ptypes, _pm, _pmt, _fid] : it->second)
                    if (ptypes == mptypes) exported_symbols_.insert(mangled);
            }
        }
        if (slid.has_explicit_ctor_decl) exported_symbols_.insert(slid.name + "__$ctor");
        if (slid.has_explicit_dtor_decl) exported_symbols_.insert(slid.name + "__$dtor");
        if (slid.is_transport_impl) {
            exported_symbols_.insert(slid.name + "__$ctor");
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
        } else if (auto* fl = dynamic_cast<const ForLongStmt*>(&stmt)) {
            for (auto& s : fl->init_stmts) scanStmt(*s);
            if (fl->cond) scanExpr(*fl->cond);
            for (auto& s : fl->update_block->stmts) scanStmt(*s);
            for (auto& s : fl->body->stmts) scanStmt(*s);
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
            } else if (auto* fl = dynamic_cast<const ForLongStmt*>(stmt.get())) {
                for (auto& s : fl->init_stmts) {
                    if (auto* d = dynamic_cast<const VarDeclStmt*>(s.get()))
                        parent_locals.insert(d->name);
                    else if (auto* a = dynamic_cast<const ArrayDeclStmt*>(s.get()))
                        parent_locals.insert(a->name);
                }
                collectLocals(*fl->update_block);
                collectLocals(*fl->body);
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
                        else if (auto* fl2 = dynamic_cast<const ForLongStmt*>(s.get())) {
                            for (auto& is : fl2->init_stmts) {
                                if (auto* d = dynamic_cast<const VarDeclStmt*>(is.get()))
                                    own_params.insert(d->name);
                                else if (auto* a = dynamic_cast<const ArrayDeclStmt*>(is.get()))
                                    own_params.insert(a->name);
                            }
                            collectNested(*fl2->update_block);
                            collectNested(*fl2->body);
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
            } else if (auto* fl = dynamic_cast<const ForLongStmt*>(stmt.get())) {
                findNested(*fl->update_block);
                findNested(*fl->body);
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

// Build the registry of global slid fields. Folds inferred-type field
// initializers, infers slids types where missing, and assigns each field a
// stable LLVM symbol name. Co-named global slids (stacked under the same
// namespace) each contribute their own fields to the same key prefix; the
// collision check in the parser already guarantees no key collisions here.
std::string Codegen::lazyMangleSuffix(const GlobalDef& g) const {
    std::string out;
    for (char c : g.namespace_name) out += (c == ':') ? '.' : c;
    if (!g.fields.empty()) {
        if (!out.empty()) out += '.';
        out += g.fields[0].name;
    }
    return out;
}

void Codegen::collectGlobals() {
    std::set<std::string> cycle;
    for (auto& g_const : program_.globals) {
        auto& g = const_cast<GlobalDef&>(g_const);
        for (auto& f : g.fields) {
            // Infer type from the foldable default when not annotated.
            if (f.type.empty()) {
                if (!f.default_val)
                    error(std::string("Global field '" + f.name
                        + "' has no type and no initializer."));
                cycle.clear();
                ConstEntry folded;
                try {
                    folded = foldConstExpr(*f.default_val, "", cycle);
                } catch (CompileError&) {
                    throw CompileError{f.file_id, f.tok,
                        finalizeErrorMsg("Global field '" + f.name
                            + "' initializer is not a foldable constant")};
                }
                if (folded.is_float) {
                    double v = std::fabs(folded.float_value);
                    f.type = (v <= (double)FLT_MAX) ? "float32" : "float64";
                } else {
                    f.type = folded.slid_type;
                }
            }
            GlobalEntry entry;
            entry.namespace_name = g.namespace_name;
            entry.field_name = f.name;
            entry.slids_type = f.type;
            entry.file_id = f.file_id;
            entry.tok = f.tok;
            entry.is_lazy = g.is_lazy();
            entry.visible_in_function = g.visible_in_function;
            entry.def = &g_const;
            // Canonical key: "<ns>:<field>" with empty ns dropping its prefix.
            std::string key = g.namespace_name.empty()
                ? f.name
                : g.namespace_name + ":" + f.name;
            // Mangled LLVM symbol: replace ':' separators with '.'.
            std::string mangled = "@__$g.";
            for (char c : key) mangled += (c == ':') ? '.' : c;
            entry.llvm_symbol = mangled;
            globals_[key] = std::move(entry);
        }
    }
}

// Emit module-level LLVM globals for static-allocated entries (those with
// no ctor and no dtor on their owning GlobalDef). Lazy entries get module-
// level storage too (the sentinel + storage), but their initializer is the
// type's zero value and the user-supplied ctor body runs at first access —
// that arm lands in phase 3; for now lazy emits storage only and skips the
// init computation.
void Codegen::emitStaticGlobals() {
    if (globals_.empty()) return;
    out_ << "\n";
    std::set<std::string> emitted;
    std::set<std::string> cycle;
    for (auto& g : program_.globals) {
        for (auto& f : g.fields) {
            std::string key = g.namespace_name.empty()
                ? f.name
                : g.namespace_name + ":" + f.name;
            auto it = globals_.find(key);
            if (it == globals_.end()) continue;
            if (!emitted.insert(it->second.llvm_symbol).second) continue;

            std::string llvm_ty = llvmType(f.type);
            // Imported globals (declared in this TU's `.slh` import, defined
            // elsewhere) emit as `external global` — no storage, no init.
            // The linker resolves to the defining TU's `<sym> = global …`.
            if (!g.impl_module.empty()) {
                out_ << it->second.llvm_symbol << " = external global "
                     << llvm_ty << "\n";
                continue;
            }
            std::string init_value;
            if (f.default_val) {
                // Every global field — static or lazy — has its declared
                // value at module-load. A lazy ctor may overwrite fields
                // when it runs at first access; until then, reads see this
                // static initializer.
                cycle.clear();
                ConstEntry folded;
                try {
                    folded = foldConstExpr(*f.default_val, "", cycle);
                } catch (CompileError&) {
                    throw CompileError{f.file_id, f.tok,
                        finalizeErrorMsg("Global field '" + f.name
                            + "' initializer is not a foldable constant")};
                }
                init_value = emitConstValue(folded);
            } else {
                if (llvm_ty == "ptr") init_value = "null";
                else if (llvm_ty == "float" || llvm_ty == "double") init_value = "0.0";
                // Slid-typed field storage: zero the aggregate at module
                // load. The wrapping global's lazy ctor populates fields
                // through emitConstructAt on first access. Inline-array slid
                // fields (`[N x %struct.X]`) ride the same path.
                else if ((llvm_ty.size() >= 8
                        && llvm_ty.compare(0, 8, "%struct.") == 0)
                        || (!llvm_ty.empty() && llvm_ty[0] == '['))
                    init_value = "zeroinitializer";
                else init_value = "0";
            }
            // Function-internal globals are scoped to their owning function;
            // mark them `internal` so the LLVM symbol doesn't cross TUs.
            // File/class-scope globals stay default-linkage so the pre-link
            // collision check is the load-bearing diagnostic.
            const char* linkage = !it->second.visible_in_function.empty()
                                ? "internal " : "";
            out_ << it->second.llvm_symbol << " = " << linkage << "global "
                 << llvm_ty << " " << init_value << "\n";
        }
    }
}

// Emit one ctor function, one dtor function, the per-slid sentinel, and the
// ensure widget for every lazy GlobalDef in this TU. Bodies are emitted with
// `current_global_namespace_` set to the slid's namespace so bare field
// references resolve through lookupGlobal's namespace arm.
void Codegen::emitLazyGlobalHelpers() {
    // Two passes over program.globals:
    //   - imported lazy entries: emit external declares for sentinel + ensure
    //     widget so the consumer's gate links to the defining TU's symbols.
    //   - local lazy entries: emit the full set (sentinel, node, ctor, dtor,
    //     ensure) keyed by `lazyMangleSuffix` so the names are stable across
    //     TUs and the consumer-side declares resolve.
    // Function-internal lazies (visible_in_function non-empty) stay strictly
    // local — keep `internal` linkage on every symbol. Namespace-scope
    // lazies expose the sentinel and ensure widget for cross-TU access.
    bool wrote_any = false;
    auto ensure_blank = [&](){ if (!wrote_any) { out_ << "\n"; wrote_any = true; } };

    // imported declares
    for (auto& g : program_.globals) {
        if (!g.is_lazy() || g.impl_module.empty()) continue;
        ensure_blank();
        std::string suffix = lazyMangleSuffix(g);
        out_ << "@__$g_sentinel." << suffix << " = external global i1\n";
        out_ << "declare void @__$g_ensure." << suffix << "()\n";
    }
    if (wrote_any) out_ << "\n";

    // local definitions
    for (auto& g : program_.globals) {
        if (!g.is_lazy() || !g.impl_module.empty()) continue;
        std::string suffix = lazyMangleSuffix(g);
        bool fn_internal = !g.visible_in_function.empty();
        // Sentinel and ensure widget go default-linkage so consumer TUs can
        // reach them; function-internal lazies stay private to this TU.
        const char* gate_link = fn_internal ? "internal " : "";
        out_ << "@__$g_sentinel." << suffix << " = " << gate_link << "global i1 0\n";
        // dtor_node is only ever referenced from this TU's ensure widget;
        // internal is always safe.
        out_ << "@__$g_dtor_node." << suffix
             << " = internal global { ptr, ptr } "
             << "{ ptr null, ptr @__$g_dtor." << suffix << " }\n";
    }

    // ctor / dtor / ensure per local lazy GlobalDef.
    for (auto& g : program_.globals) {
        if (!g.is_lazy() || !g.impl_module.empty()) continue;
        std::string suffix = lazyMangleSuffix(g);
        const GlobalDef* def = &g;
        bool fn_internal = !g.visible_in_function.empty();
        const char* gate_link = fn_internal ? "internal " : "";

        // === ctor ===
        out_ << "define internal void @__$g_ctor." << suffix << "() {\n";
        out_ << "entry:\n";
        locals_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_.clear();
        self_ptr_.clear();
        current_global_namespace_ = def->namespace_name;
        pushScope();
        // Field-ctor pass: each slid-typed field gets default-construction
        // on its static-allocated storage before the user body runs.
        // Inline-array slid fields (T[N]) are walked element-wise — the site
        // peels the array shape and calls the canonical helper per slot,
        // matching emitInlineCtorWalk's deferred-to-site rule for inline arrays.
        for (auto& f : def->fields) {
            std::string field_sym = "@__$g.";
            for (char c : def->namespace_name) field_sym += (c == ':') ? '.' : c;
            if (!def->namespace_name.empty()) field_sym += '.';
            field_sym += f.name;
            if (isInlineArrayType(f.type)) {
                auto lb = f.type.rfind('[');
                std::string elem_type = f.type.substr(0, lb);
                if (slid_info_.count(elem_type) == 0) continue;
                std::string sz_str = f.type.substr(lb + 1, f.type.size() - lb - 2);
                int n = std::stoi(sz_str);
                for (int k = 0; k < n; k++) {
                    std::string slot = newTmp();
                    out_ << "    " << slot << " = getelementptr [" << n
                         << " x %struct." << elem_type << "], ptr " << field_sym
                         << ", i32 0, i32 " << k << "\n";
                    emitConstructAt(elem_type, slot, {}, {});
                }
                continue;
            }
            if (slid_info_.count(f.type) == 0) continue;
            emitConstructAt(f.type, field_sym, {}, {});
        }
        if (def->ctor_body) emitBlock(*def->ctor_body);
        popScope();
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";

        // === dtor ===
        out_ << "define internal void @__$g_dtor." << suffix << "() {\n";
        out_ << "entry:\n";
        locals_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_.clear();
        self_ptr_.clear();
        current_global_namespace_ = def->namespace_name;
        pushScope();
        if (def->dtor_body) emitBlock(*def->dtor_body);
        // Field-dtor pass: each slid-typed field's dtor fires in reverse
        // declaration order after the user body. Inline-array slid fields
        // reverse-walk elements (last index first).
        for (int i = (int)def->fields.size() - 1; i >= 0; i--) {
            auto& f = def->fields[i];
            std::string field_sym = "@__$g.";
            for (char c : def->namespace_name) field_sym += (c == ':') ? '.' : c;
            if (!def->namespace_name.empty()) field_sym += '.';
            field_sym += f.name;
            if (isInlineArrayType(f.type)) {
                auto lb = f.type.rfind('[');
                std::string elem_type = f.type.substr(0, lb);
                if (slid_info_.count(elem_type) == 0) continue;
                std::string sz_str = f.type.substr(lb + 1, f.type.size() - lb - 2);
                int n = std::stoi(sz_str);
                for (int k = n - 1; k >= 0; k--) {
                    std::string slot = newTmp();
                    out_ << "    " << slot << " = getelementptr [" << n
                         << " x %struct." << elem_type << "], ptr " << field_sym
                         << ", i32 0, i32 " << k << "\n";
                    emitDtorChainCall(elem_type, slot);
                }
                continue;
            }
            if (slid_info_.count(f.type) == 0) continue;
            emitDtorChainCall(f.type, field_sym);
        }
        popScope();
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
        current_global_namespace_.clear();

        // === ensure widget ===
        // Fast-path gate is inlined at every access site; this widget is the
        // cold path. Sets the sentinel, prepends this slid's dtor node onto
        // the shared list (so reverse-construction-order falls out naturally),
        // then tail-calls the user ctor.
        out_ << "define " << gate_link << "void @__$g_ensure." << suffix << "() {\n";
        out_ << "entry:\n";
        out_ << "    store i1 1, ptr @__$g_sentinel." << suffix << "\n";
        out_ << "    %old_head = load ptr, ptr @__$global_dtor_head\n";
        out_ << "    %next_slot = getelementptr { ptr, ptr }, "
             << "ptr @__$g_dtor_node." << suffix << ", i32 0, i32 0\n";
        out_ << "    store ptr %old_head, ptr %next_slot\n";
        out_ << "    store ptr @__$g_dtor_node." << suffix
             << ", ptr @__$global_dtor_head\n";
        out_ << "    tail call void @__$g_ctor." << suffix << "()\n";
        out_ << "    ret void\n";
        out_ << "}\n\n";
    }
}

// Emit the shared runtime: one head pointer for a singly-linked list of dtor
// nodes, and the walker `__$global_dtor_all`. Both have `linkonce_odr`
// linkage so multiple TUs can each emit the symbol and the linker dedups.
// Per-lazy nodes are statically allocated in each defining TU (see
// emitLazyGlobalHelpers) and prepended to the shared list at first-access.
// Walking-then-clearing the head means reverse construction order falls out
// naturally and `global;` is re-entrant (a second `global;` block sees an
// empty list — matching the spec's lazy-instantiation invariant).
void Codegen::emitGlobalDtorRuntime() {
    out_ << "\n";
    out_ << "@__$global_dtor_head = linkonce_odr global ptr null\n\n";

    out_ << "define linkonce_odr void @__$global_dtor_all() {\n";
    out_ << "entry:\n";
    out_ << "    %head = load ptr, ptr @__$global_dtor_head\n";
    out_ << "    store ptr null, ptr @__$global_dtor_head\n";
    out_ << "    br label %loop\n";
    out_ << "loop:\n";
    out_ << "    %n = phi ptr [ %head, %entry ], [ %next, %body ]\n";
    out_ << "    %done = icmp eq ptr %n, null\n";
    out_ << "    br i1 %done, label %end, label %body\n";
    out_ << "body:\n";
    out_ << "    %fn_slot = getelementptr { ptr, ptr }, ptr %n, i32 0, i32 1\n";
    out_ << "    %fn = load ptr, ptr %fn_slot\n";
    out_ << "    call void %fn()\n";
    out_ << "    %next_slot = getelementptr { ptr, ptr }, ptr %n, i32 0, i32 0\n";
    out_ << "    %next = load ptr, ptr %next_slot\n";
    out_ << "    br label %loop\n";
    out_ << "end:\n";
    out_ << "    ret void\n";
    out_ << "}\n\n";
}

void Codegen::emitLazySentinelGate(const GlobalEntry& ge) {
    if (!ge.is_lazy || !ge.def) return;
    std::string suffix = lazyMangleSuffix(*ge.def);
    std::string s_tmp = newTmp();
    std::string ensure_lbl = newLabel("g_ens");
    std::string access_lbl = newLabel("g_acc");
    out_ << "    " << s_tmp << " = load i1, ptr @__$g_sentinel." << suffix << "\n";
    out_ << "    br i1 " << s_tmp << ", label %" << access_lbl
         << ", label %" << ensure_lbl << "\n";
    out_ << ensure_lbl << ":\n";
    out_ << "    call void @__$g_ensure." << suffix << "()\n";
    out_ << "    br label %" << access_lbl << "\n";
    out_ << access_lbl << ":\n";
}

const Codegen::GlobalEntry* Codegen::lookupGlobal(const std::string& name) const {
    // The function-internal visibility scope. For a free function this is the
    // function name; for a method it's the qualified "Class:method" — which
    // matches the namespace_name the parser registers when seeing `global`
    // inside the method body.
    std::string fn_scope = current_func_name_;
    if (!current_slid_.empty() && !current_func_name_.empty())
        fn_scope = current_slid_ + ":" + current_func_name_;

    // `::name` is an explicit unnamed-namespace lookup. Strip the prefix.
    if (name.size() >= 2 && name[0] == ':' && name[1] == ':') {
        auto it = globals_.find(name.substr(2));
        if (it == globals_.end()) return nullptr;
        // `::` form only reaches unnamed-namespace entries (no function-internal).
        if (!it->second.namespace_name.empty()
            || !it->second.visible_in_function.empty()) return nullptr;
        return &it->second;
    }
    // Namespace-qualified: direct lookup. Function-internal entries are only
    // reachable from inside their owning function, so reject explicit
    // qualified-from-outside.
    if (name.find(':') != std::string::npos) {
        auto it = globals_.find(name);
        if (it == globals_.end()) return nullptr;
        if (!it->second.visible_in_function.empty()
            && it->second.visible_in_function != fn_scope) return nullptr;
        return &it->second;
    }
    // Bare name. Try in order:
    //   1. current lazy ctor/dtor body's owning namespace
    //   2. function-internal (current function's namespace — qualified for
    //      methods, bare for free functions)
    //   3. unnamed-namespace
    if (!current_global_namespace_.empty()) {
        auto it = globals_.find(current_global_namespace_ + ":" + name);
        if (it != globals_.end()
            && it->second.namespace_name == current_global_namespace_)
            return &it->second;
    }
    if (!fn_scope.empty()) {
        auto it = globals_.find(fn_scope + ":" + name);
        if (it != globals_.end()
            && it->second.visible_in_function == fn_scope)
            return &it->second;
    }
    auto it = globals_.find(name);
    if (it == globals_.end()) return nullptr;
    if (!it->second.namespace_name.empty()) return nullptr;
    if (!it->second.visible_in_function.empty()) return nullptr;
    return &it->second;
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
        } else if (auto* fl = dynamic_cast<const ForLongStmt*>(&stmt)) {
            for (auto& s : fl->init_stmts) collect(*s);
            for (auto& s : fl->update_block->stmts) collect(*s);
            for (auto& s : fl->body->stmts) collect(*s);
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

    // Establish the bottom frame of block_const_stack_ — the file/global scope.
    // File-scope const decls land here via collectAndFoldConsts. Each function
    // body emit pushes its own frame on top (via pushScope) and pops it on exit,
    // so block-scope consts never leak across function boundaries.
    block_const_stack_.push_back({});

    collectAndFoldConsts();
    inferFieldTypes();
    collectGlobals();
    collectSlids();
    classifyVirtualClasses();
    resolveSlidInheritance();
    synthesizeFieldDtors();
    synthesizeCtorNeeds();
    // Auto-lazy promotion: a global with no user `_()`/`~()` whose fields
    // include at least one slid-typed entry needs the lazy machinery so the
    // field's own ctor/dtor can run. Synthesize empty user bodies (leave
    // ctor_body / dtor_body null; the field-ctor/dtor pass in
    // emitLazyGlobalHelpers does the work). Imported globals are skipped —
    // their defining TU owns the construction.
    for (auto& g_const : program_.globals) {
        auto& g = const_cast<GlobalDef&>(g_const);
        if (!g.impl_module.empty()) continue;
        if (g.has_ctor_decl || g.has_dtor_decl) continue;
        bool has_slid_field = false;
        for (auto& f : g.fields) {
            std::string elem = isInlineArrayType(f.type)
                ? f.type.substr(0, f.type.rfind('[')) : f.type;
            if (slid_info_.count(elem)) { has_slid_field = true; break; }
        }
        if (has_slid_field) {
            g.has_ctor_decl = true;
            g.has_dtor_decl = true;
            // Refresh the corresponding GlobalEntry cache so the gate fires
            // at access sites (collectGlobals stamped is_lazy from the
            // pre-promotion view).
            for (auto& f : g.fields) {
                std::string key = g.namespace_name.empty()
                    ? f.name
                    : g.namespace_name + ":" + f.name;
                auto it = globals_.find(key);
                if (it != globals_.end()) it->second.is_lazy = true;
            }
        }
    }
    validateDefaultDelete();
    buildVtables();
    markImportableClasses();
    validatePureSlots();
    collectFunctionSignatures();
    scanForSlidTemplateUses();
    scanForTemplateFunctionUses();

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
            error(std::string("Unknown template '" + req.func_name + "'."));
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
                bool is_mut = (i < (int)tmpl.param_mutable.size()) && tmpl.param_mutable[i];
                substituted = applyParamConstDefault(substituted, is_mut);
                if (substituted != req.param_types[i]) { match = false; break; }
            }
            if (match) { chosen = &entry; break; }
        }
        if (!chosen)
            error(std::string("No overload of '" + req.func_name
                + "' matches the requested parameter signature for instantiation."));
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
            for (auto& [mangled, ptypes, _pm, _pmt, _fid] : overloads)
                exported_symbols_.insert(mangled);
        }
    }

    // validate: class objects and anon-tuples cannot be passed by value
    auto checkParams = [&](const std::string& ctx,
                           const std::vector<std::pair<std::string,std::string>>& params) {
        for (auto& [type, name] : params) {
            if (slid_info_.count(type) > 0)
                error(std::string("In " + ctx + ", parameter '" + name +
                    "' has class type '" + type + "' and cannot be passed by value; use '" + type + "^'."));
            if (isAnonTupleType(type))
                error(std::string("In " + ctx + ", parameter '" + name +
                    "' has tuple type '" + type + "' and cannot be passed by value; use '" + type + "^'."));
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
    // emit struct types for per-instantiation local classes
    for (auto* slid : local_class_instances_)
        emitStructType(*slid);
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

    // Module-level globals: static-allocated storage + zero-initialized
    // storage for lazy entries (phase 3 fills in the sentinel + ctor glue).
    emitStaticGlobals();

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
        for (auto& [mangled, ptypes, _pm, _pmt, _fid] : foit->second) {
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
        // emitSlidCtorDtor emits __$ctor / __$dtor only for has_explicit_ctor
        // classes (paired with user _()/~()). Implicit classes inline at the
        // site and emit no symbol. Track local emissions so the declare loop
        // below skips them.
        {
            auto sit = slid_info_.find(slid.name);
            if (sit != slid_info_.end() && sit->second.has_explicit_ctor) {
                // body presence in this TU = this TU is the definer
                bool has_body_here = slid.explicit_ctor_body || slid.dtor_body
                                     || slid.ctor_body;
                for (auto& em : program_.external_methods) {
                    if (em.slid_name == slid.name && em.body
                        && (em.method_name == "_" || em.method_name == "~")) {
                        has_body_here = true; break;
                    }
                }
                if (has_body_here) {
                    local_methods.insert(slid.name + "__$ctor");
                    if (sit->second.has_dtor)
                        local_methods.insert(slid.name + "__$dtor");
                }
            }
        }
        // impl slids locally define __$sizeof
        if (slid.is_transport_impl) {
            local_methods.insert(slid.name + "__$sizeof");
        }
        // all non-declaration slids define __$sizeof locally
        if (!slid.has_trailing_ellipsis) local_methods.insert(slid.name + "__$sizeof");
        for (auto& m : slid.methods) {
            if (!m.body) continue;
            std::string base = slid.name + "__" + m.name;
            auto oit = method_overloads_.find(base);
            if (oit != method_overloads_.end())
                for (auto& [mn, _pt, _pm, _pmt, _fid] : oit->second) local_methods.insert(mn);
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
                for (auto& [mn, _pt, _pm, _pmt, _fid] : oit->second) local_methods.insert(mn);
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
            for (auto& [mangled, ptypes, _pm, _pmt, _fid] : overloads) {
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
            for (auto& [mn, _pt, _pm, _pmt, _fid] : oit->second) local_methods.insert(mn);
        // also include ctor/dtor
        if (em.method_name == "_") local_methods.insert(em.slid_name + "__$ctor");
        if (em.method_name == "~") local_methods.insert(em.slid_name + "__$dtor");
    }
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue; // skip template slids
        // ctor/dtor — declare if not locally defined.
        // Only explicit (user-paired _()/~()) classes have symbols; implicit
        // classes are inlined at site, so no declare is ever needed.
        auto& info = slid_info_[slid.name];
        const char* self_arg = info.is_empty ? "" : "ptr";
        if (info.has_explicit_ctor && !local_methods.count(slid.name + "__$ctor"))
            out_ << "declare void @" << slid.name << "__$ctor(" << self_arg << ")\n";
        if (info.has_explicit_ctor && info.has_dtor
                && !local_methods.count(slid.name + "__$dtor"))
            out_ << "declare void @" << slid.name << "__$dtor(" << self_arg << ")\n";
        if (info.has_private_suffix && !local_methods.count(slid.name + "__$sizeof"))
            out_ << "declare i64 @" << slid.name << "__$sizeof()\n";
        // regular methods: first arg is ptr (self) unless slid is empty
        for (auto& [base, overloads] : method_overloads_) {
            if (base.substr(0, slid.name.size() + 2) != slid.name + "__") continue;
            for (auto& [mangled, ptypes, _pm, _pmt, _fid] : overloads) {
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

    // Lazy globals: runtime dtor list, register helper, dtor_all, per-slid
    // sentinel / ctor / dtor / ensure widget. The dtor runtime is always
    // emitted so `global;` has a target even when there are no lazies.
    emitGlobalDtorRuntime();
    emitLazyGlobalHelpers();

    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        emitSlidCtorDtor(slid);
    }
    for (auto* slid : pending_slid_instantiations_)
        emitSlidCtorDtor(*slid);
    for (auto* slid : local_class_instances_)
        emitSlidCtorDtor(*slid);

    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        emitSlidMethods(slid);
    }
    for (auto* slid : pending_slid_instantiations_)
        emitSlidMethods(*slid);
    for (auto* slid : local_class_instances_)
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
            } else if (auto* fl = dynamic_cast<const ForLongStmt*>(stmt.get())) {
                scan(*fl->update_block);
                scan(*fl->body);
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
    emitted_alloca_regs_.clear();
    array_info_.clear();
    dtor_vars_.clear();
    scope_stack_.clear();
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
        locals_[var].reg = ptr;
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
    for (int i = 0; i < (int)fn.params.size(); i++) {
        const auto& [type, name] = fn.params[i];
        bool is_mut = (i < (int)fn.param_mutable.size()) && fn.param_mutable[i];
        std::string reg = uniqueAllocaReg(name);
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        locals_[name] = {reg, applyParamConstDefault(type, is_mut)};
    }

    pushScope();
    emitBlock(*fn.body);

    if (fn.return_type == "void" && !block_terminated_) {
        emitDtors();
        out_ << "    ret void\n";
    }
    popScope();

    out_ << "}\n\n";
}

void Codegen::pushScope() {
    scope_stack_.push_back({dtor_vars_.size(), locals_, {}});
    block_const_stack_.push_back({});
}

void Codegen::popScope() {
    auto& frame = scope_stack_.back();
    if (frame.opens_global_lifetime && global_lifetime_depth_ > 0)
        global_lifetime_depth_--;
    if (!block_terminated_) {
        for (int i = (int)dtor_vars_.size() - 1; i >= (int)frame.dtor_mark; i--) {
            auto& e = dtor_vars_[i];
            std::string target;
            if (e.tuple_index >= 0) {
                std::string tuple_type = locals_[e.var_name].type;
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr " << llvmType(tuple_type)
                     << ", ptr " << locals_[e.var_name].reg << ", i32 0, i32 " << e.tuple_index << "\n";
                target = gep;
            } else {
                target = locals_[e.var_name].reg;
            }
            emitDtorChainCall(e.slid_type, target);
        }
        // Drain scope-exit hooks in reverse registration order. Used for the
        // `global;` lifetime statement to fire `__$global_dtor_all()` at the
        // close of the enclosing block.
        for (int i = (int)frame.exit_emits.size() - 1; i >= 0; i--)
            out_ << frame.exit_emits[i];
    }
    dtor_vars_.resize(frame.dtor_mark);
    locals_ = std::move(frame.saved_locals);
    scope_stack_.pop_back();
    block_const_stack_.pop_back();
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
std::string Codegen::emitPhraseArg(const Expr& arg, const std::string& param_type) {
    pushPostIncQueue();
    emitPrePass(arg);
    std::string r = emitArgForParam(arg, param_type);
    flushPostIncQueue();
    return r;
}

std::string Codegen::emitArgForParam(const Expr& arg, const std::string& param_type) {
    bool want_ptr = !param_type.empty() && param_type.back() == '^';
    if (want_ptr) {
        std::string slid_name = param_type.substr(0, param_type.size() - 1);
        // implicit temporary: string literal passed to SlidType^ param
        if (slid_info_.count(slid_name) && dynamic_cast<const StringLiteralExpr*>(&arg)) {
            std::string tmp_reg = emitRawSlidAlloca(slid_name);
            // default-init fields + call ctor
            emitConstructAtPtrs(slid_name, tmp_reg, {}, {});
            // call op=(char[]) to initialize from the literal
            auto oit = method_overloads_.find(slid_name + "__op=");
            if (oit != method_overloads_.end()) {
                for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
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
        // self as a function argument — pass self_ptr_ directly so the
        // receiver inherits any inline-ctor-body remap (cannot be replaced
        // by locals_["self"].reg, which is fixed to "%self").
        if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
            if (ve->name == "self" && !current_slid_.empty())
                return self_ptr_.empty() ? "%self" : self_ptr_;
        }
        // ^s — explicit address-of a slid local: pass its alloca ptr directly
        if (auto* ao = dynamic_cast<const AddrOfExpr*>(&arg)) {
            if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
                auto lit = locals_.find(ve->name);
                if (lit != locals_.end() && slid_info_.count(lit->second.type))
                    return locals_.at(ve->name).reg;
            }
            return emitExpr(arg);
        }
        // s — implicit address-of (auto-promote slid local to ref)
        if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
            auto lit = locals_.find(ve->name);
            if (lit != locals_.end() && slid_info_.count(lit->second.type)) {
                return locals_.at(ve->name).reg;
            }
        }
        // DerefExpr of a slid pointer (sa^): pass the pointer value directly without re-deref.
        if (auto* de = dynamic_cast<const DerefExpr*>(&arg)) {
            if (!derefSlidName(*de).empty()) {
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load ptr, ptr " << locals_.at(ve->name).reg << "\n";
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
                    auto lit = locals_.find(ve->name);
                    if (lit != locals_.end() && isAnonTupleType(lit->second.type))
                        return locals_.at(ve->name).reg;
                }
            }
            // bare my_tuple → auto-promote (analogous to implicit slid-ref promotion)
            if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
                auto lit = locals_.find(ve->name);
                if (lit != locals_.end() && isAnonTupleType(lit->second.type))
                    return locals_.at(ve->name).reg;
            }
            // deref of a tuple-ref local: load the ptr
            if (auto* de = dynamic_cast<const DerefExpr*>(&arg)) {
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto lit = locals_.find(ve->name);
                    if (lit != locals_.end() && lit->second.type == param_type) {
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load ptr, ptr " << locals_.at(ve->name).reg << "\n";
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
    // generic lvalue auto-promote for slid / anon-tuple ref params: chained
    // obj.f_, arr[i], p++^.f_, ptr^.f_, etc. The per-shape arms above cover
    // the common cases; this catches the rest where resolveLvalue returns
    // an address whose type matches the callee's expected pointee.
    if (want_ptr) {
        // Canonicalize the pointee for slid_info_ lookup and lv.type compare
        // — param_type may carry default-const wrap "(const T)^" → pointee is
        // "(const T)" which slid_info_ doesn't know.
        std::string inner = baseSlidType(param_type.substr(0, param_type.size() - 1));
        if (slid_info_.count(inner) || isAnonTupleType(inner)) {
            bool addressable = dynamic_cast<const FieldAccessExpr*>(&arg)
                || dynamic_cast<const ArrayIndexExpr*>(&arg)
                || dynamic_cast<const DerefExpr*>(&arg);
            if (addressable) {
                auto lv = resolveLvalue(arg);
                if (baseSlidType(lv.type) == inner) return lv.addr;
            }
        }
    }

    // implicit construction: non-slid value passed to SlidType^ param — find matching op=
    if (want_ptr) {
        std::string slid_name = baseSlidType(param_type.substr(0, param_type.size() - 1));
        if (slid_info_.count(slid_name)) {
            std::string arg_llvm = exprLlvmType(arg);
            static const std::map<std::string,int> irank = {{"i8",1},{"i16",2},{"i32",3},{"i64",4}};
            auto ait = irank.find(arg_llvm);
            auto oit = method_overloads_.find(slid_name + "__op=");
            if (oit != method_overloads_.end()) {
                for (auto& [m, ptypes2, _pm, _pmt, _fid] : oit->second) {
                    if (ptypes2.empty()) continue;
                    std::string p_llvm = llvmType(ptypes2[0]);
                    auto pit = irank.find(p_llvm);
                    // get raw Slids type of arg (works for non-slid types like char[] too)
                    std::string arg_slids;
                    if (auto* ve2 = dynamic_cast<const VarExpr*>(&arg)) {
                        auto tit2 = locals_.find(ve2->name);
                        if (tit2 != locals_.end()) arg_slids = tit2->second.type;
                    }
                    if (arg_slids.empty()) arg_slids = exprSlidType(arg);
                    if (arg_slids.empty() && dynamic_cast<const StringLiteralExpr*>(&arg)) arg_slids = "char[]";
                    if (arg_slids.empty()) arg_slids = inferSlidType(arg);
                    // scalars: exact llvm type; pointers: exact Slids type (char[] != String^).
                    // Canonical compare on the pointer side handles the default-const wrap
                    // ((const char)[] ≡ char[] for matching).
                    bool exact  = (p_llvm == arg_llvm && p_llvm != "ptr")
                               || (arg_llvm == "ptr"
                                   && canonicalType(ptypes2[0]) == canonicalType(arg_slids));
                    bool widen  = (ait != irank.end() && pit != irank.end()
                                   && pit->second >= ait->second)
                               && (arg_slids != "bool" || ptypes2[0] == "bool");
                    if (!exact && !widen) continue;
                    std::string tmp = emitRawSlidAlloca(slid_name);
                    emitConstructAtPtrs(slid_name, tmp, {}, {});
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
        std::string slid_name = baseSlidType(param_type.substr(0, param_type.size() - 1));
        if (slid_info_.count(slid_name) && baseSlidType(inferSlidType(arg)) == slid_name)
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
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end()) src_unsigned = unsigned_types.count(tit->second.type) > 0;
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
                              const std::vector<bool>& param_mutable,
                              const BlockStmt& body,
                              bool is_const_method) {
    locals_.clear();
    emitted_alloca_regs_.clear();
    dtor_vars_.clear();
    scope_stack_.clear();
    tmp_counter_ = 0;
    label_counter_ = 0;
    break_label_ = "";
    continue_label_ = "";
    current_slid_ = slid.name;
    block_terminated_ = false;
    current_func_name_ = method_user_name;
    // Phase 3: a `const` method sees `self` as const. Writes through self
    // (rebind, field writes, deref-writes) are rejected by the existing
    // body-level enforcement once self's type carries the qualifier.
    locals_["self"].type = is_const_method ? ("const " + slid.name) : slid.name;
    locals_["self"].reg = "%self";

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

    for (int i = 0; i < (int)params.size(); i++) {
        const auto& [type, name] = params[i];
        bool is_mut = (i < (int)param_mutable.size()) && param_mutable[i];
        std::string reg = uniqueAllocaReg(name);
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        locals_[name] = {reg, applyParamConstDefault(type, is_mut)};
    }

    pushScope();
    emitBlock(body);

    if (!block_terminated_) {
        if (return_type == "void" || uses_sret) {
            emitDtors();
            out_ << "    ret void\n";
        } else {
            error(std::string("Method '" + full_mangled + "' is missing a return statement."));
        }
    }
    popScope();

    out_ << "}\n\n";
}

void Codegen::emitSlidMethods(const SlidDef& slid) {
    // emit inline methods that have a body (not forward decls)
    for (auto& m : slid.methods) {
        if (!m.body) continue;
        std::string mangled = resolveMethodMangledName(slid.name, m.name, m.params);
        current_func_tuple_fields_.clear();
        emitSlidMethod(slid, m.name, mangled, m.return_type, m.params, m.param_mutable, *m.body, m.is_const_method);
    }
    // emit external method definitions for this slid
    for (auto& em : program_.external_methods) {
        if (em.slid_name != slid.name || !em.body) continue;
        if (em.method_name == "_" || em.method_name == "~") continue;
        std::string mangled = resolveMethodMangledName(slid.name, em.method_name, em.params);
        current_func_tuple_fields_.clear();
        emitSlidMethod(slid, em.method_name, mangled, em.return_type, em.params, em.param_mutable, *em.body, em.is_const_method);
    }
    current_slid_ = "";
}

void Codegen::emitFunction(const FunctionDef& fn) {
    if (!fn.body) return; // forward declaration

    locals_.clear();
    array_info_.clear();
    parent_array_info_.clear();
    dtor_vars_.clear();
    scope_stack_.clear();
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
        // Match by param types in the registry's canonical form (default-const
        // applied per slot). Raw fn.params types must run through the same
        // transform before comparison.
        std::vector<std::string> ptypes = buildParamTypes(fn.params, fn.param_mutable);
        for (auto& [mangled, mptypes, _pm, _pmt, _fid] : foit->second) {
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

    for (int i = 0; i < (int)fn.params.size(); i++) {
        const auto& [type, name] = fn.params[i];
        bool is_mut = (i < (int)fn.param_mutable.size()) && fn.param_mutable[i];
        std::string reg = uniqueAllocaReg(name);
        out_ << "    " << reg << " = alloca " << llvmType(type) << "\n";
        out_ << "    store " << llvmType(type) << " %arg_" << name << ", ptr " << reg << "\n";
        // Const-by-default: unmarked indirect params are reference-to-const
        // inside the body. From the caller's view the canonical type is
        // unchanged; the const only affects write-rejection here.
        locals_[name] = {reg, applyParamConstDefault(type, is_mut), fn.file_id, fn.tok};
    }

    pushScope();
    emitBlock(*fn.body);

    if (!block_terminated_) {
        if (fn.return_type == "void" || uses_sret) {
            emitDtors();
            out_ << "    ret void\n";
        } else {
            error(std::string("Function '" + fn.name + "' is missing a return statement."));
        }
    }
    popScope();

    out_ << "}\n\n";

    // emit nested functions after parent
    std::function<void(const BlockStmt&)> emitNested = [&](const BlockStmt& block) {
        for (auto& stmt : block.stmts) {
            if (auto* nfs = dynamic_cast<const NestedFunctionDefStmt*>(stmt.get())) {
                auto it = nested_info_.find(nfs->def.name);
                if (it != nested_info_.end())
                    emitNestedFunction(nfs->def, fn.name, it->second);
            } else if (auto* fl = dynamic_cast<const ForLongStmt*>(stmt.get())) {
                emitNested(*fl->update_block);
                emitNested(*fl->body);
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
        // PPID: each statement is its own phrase. Pre advances fire at entry;
        // post advances queued during emitStmt drain at the end (`;`).
        pushPostIncQueue();
        emitPrePass(*stmt);
        emitStmt(*stmt);
        flushPostIncQueue();
        // destroy implicit temporaries created during this statement.
        // Single dispatcher call per temp — the dtor function (or inline walk)
        // chains to its base internally.
        for (int i = (int)pending_temp_dtors_.size() - 1; i >= (int)temp_mark; i--) {
            auto& td = pending_temp_dtors_[i];
            emitDtorChainCall(td.second, td.first);
        }
        pending_temp_dtors_.resize(temp_mark);
    }
}

// returns the ptr to a field in a struct instance
std::string Codegen::emitFieldPtr(const std::string& obj_name, const std::string& field) {
    std::string slid_name;
    std::string obj_ptr;

    auto type_it = locals_.find(obj_name);
    if (type_it != locals_.end()) {
        slid_name = type_it->second.type;
        obj_ptr = type_it->second.reg;
    } else if (!current_slid_.empty()
               && slid_info_[current_slid_].field_index.count(obj_name)) {
        // obj_name may be a field of the current slid, accessed via %self
        auto& parent_info = slid_info_[current_slid_];
        int parent_idx = parent_info.field_index[obj_name];
        slid_name = parent_info.field_types[parent_idx];
        std::string self_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
        std::string parent_gep = newTmp();
        out_ << "    " << parent_gep << " = getelementptr %struct." << current_slid_
             << ", ptr " << self_ptr << ", i32 0, i32 " << parent_idx << "\n";
        obj_ptr = parent_gep;
    } else if (auto* ge = lookupGlobal(obj_name);
               ge && slid_info_.count(ge->slids_type)) {
        // Global slid: fire the lazy gate (if any) and use the field's
        // statically-allocated storage as the struct base. Subsequent GEPs
        // climb into the slid's own fields from there.
        emitLazySentinelGate(*ge);
        slid_name = ge->slids_type;
        obj_ptr = ge->llvm_symbol;
    } else {
        error(std::string("Unknown type for variable: " + obj_name));
    }

    auto& info = slid_info_[slid_name];
    auto field_it = info.field_index.find(field);
    if (field_it == info.field_index.end())
        error(std::string("Unknown field '" + field + "' on type '" + slid_name + "'."));

    int idx = field_it->second;
    std::string gep = newTmp();
    out_ << "    " << gep << " = getelementptr %struct." << slid_name
         << ", ptr " << obj_ptr << ", i32 0, i32 " << idx << "\n";
    return gep;
}
