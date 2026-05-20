#include "codegen.h"
#include "source_map.h"
#include <sstream>
#include <functional>
#include <stdexcept>
#include <set>
#include <climits>
#include <cstring>
#include <algorithm>
#include "codegen_helpers.h"

// True if a call argument of slids type `arg` can bind to a parameter of
// slids type `param`: an exact canonical-type match, or class-value → `T^`
// reference auto-promotion (a `T` lvalue binds a `T^` / `(const T)^` param —
// the call site takes its address). Shared by every arg/param overload match.
static bool argBindsToParam(const std::string& arg, const std::string& param) {
    std::string a = canonicalType(arg);
    std::string p = canonicalType(param);
    if (a == p) return true;
    // value → reference promotion for slid types.
    if (!p.empty() && p.back() == '^' && p.substr(0, p.size() - 1) == a) return true;
    return false;
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
        rejectConstToMutable(entry.def->user_name.empty() ? name : entry.def->user_name,
            args, entry.def->param_mutable, entry.def->param_mut_toks, entry.def->file_id);
        return {&entry, std::move(targs)};
    }

    // Multiple overloads: filter by arity, then by post-substitution param-type match.
    const TemplateFuncEntry* best = nullptr;
    std::vector<std::string> best_targs;
    int matches = 0;
    for (auto& entry : it->second) {
        const FunctionDef& tmpl = *entry.def;
        size_t treq = tmpl.params.size();
        for (auto& d : tmpl.param_defaults) if (d) treq--;
        if (args.size() < treq || args.size() > tmpl.params.size()) continue;
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
            if (!argBindsToParam(actual, substituted)
                && !argUpcastsToParam(actual, substituted)) { match = false; break; }
        }
        if (!match) continue;
        matches++;
        best = &entry;
        best_targs = std::move(targs);
    }
    if (matches == 0) return {nullptr, {}};
    if (matches > 1)
        error(std::string("Ambiguous template overload for '" + name + "'."));
    rejectConstToMutable(best->def->user_name.empty() ? name : best->def->user_name,
        args, best->def->param_mutable, best->def->param_mut_toks, best->def->file_id);
    return {best, std::move(best_targs)};
}

// Number of required (non-defaulted) parameters of a registered function.
size_t Codegen::requiredArity(const std::string& mangled) const {
    auto pit = func_param_types_.find(mangled);
    size_t total = (pit != func_param_types_.end()) ? pit->second.size() : 0;
    auto dit = func_param_defaults_.find(mangled);
    size_t ndef = (dit != func_param_defaults_.end()) ? dit->second.size() : 0;
    return total > ndef ? total - ndef : 0;
}

// Append default-value args for the trailing slots a call left unfilled, so
// emit/mangle proceed at full arity. Idempotent via `padded`.
void Codegen::padCallArgs(const std::vector<std::unique_ptr<Expr>>& cargs,
                          const bool& cpadded, const std::string& mangled) {
    bool& padded = const_cast<bool&>(cpadded);
    if (padded) return;
    padded = true;
    auto& args = const_cast<std::vector<std::unique_ptr<Expr>>&>(cargs);
    auto dit = func_param_defaults_.find(mangled);
    auto pit = func_param_types_.find(mangled);
    if (dit == func_param_defaults_.end() || dit->second.empty()
        || pit == func_param_types_.end()) return;
    int total = (int)pit->second.size();
    int first_def = total - (int)dit->second.size();
    for (int i = (int)args.size(); i < total; i++) {
        std::set<std::string> cyc;
        ConstEntry c = foldConstExpr(*dit->second[i - first_def], "", cyc);
        if (c.is_float)
            args.push_back(std::make_unique<FloatLiteralExpr>(c.float_value));
        else
            args.push_back(std::make_unique<IntLiteralExpr>(c.int_value));
    }
}

std::string Codegen::resolveFreeFunctionMangledName(
    const std::string& name,
    size_t arg_count,
    bool force_global) const
{
    // inside a namespace function body, an unqualified call resolves to a
    // sibling namespace member before falling back to the global scope.
    if (!current_namespace_.empty() && !force_global) {
        auto nsit = free_func_overloads_.find(current_namespace_ + ":" + name);
        if (nsit != free_func_overloads_.end())
            for (auto& [mn, ptypes, _pm, _pmt, _fid] : nsit->second)
                if (arg_count >= requiredArity(mn) && arg_count <= ptypes.size())
                    return mn;
    }
    // The bare-name fast path (mangled == base). Gated on the name being a
    // genuine free-function slids-name: func_return_types_ is keyed by mangled
    // names, and a namespace foreign function's mangled name is its bare C
    // symbol — without this gate a bare call would wrongly match `Ns:fn`.
    if (func_return_types_.count(name) && free_func_overloads_.count(name)) {
        // Bare name resolves only if the call's arity is in the function's
        // [required, total] range (defaults make the upper end reachable).
        auto pit = func_param_types_.find(name);
        if (pit == func_param_types_.end()
            || (arg_count >= requiredArity(name) && arg_count <= pit->second.size()))
            return name;
    }
    // nested function: match an overload by arity range. Full arg-type
    // resolution is done by nestedCallMangled at sites that have the args.
    auto nit = nested_func_overloads_.find(name);
    if (nit != nested_func_overloads_.end())
        for (auto& [mn, ptypes, _pm, _pmt, _fid] : nit->second)
            if (arg_count >= requiredArity(mn) && arg_count <= ptypes.size()) return mn;
    auto foit = free_func_overloads_.find(name);
    if (foit == free_func_overloads_.end()) return "";
    for (auto& [mn, ptypes, _pm, _pmt, _fid] : foit->second)
        if (arg_count >= requiredArity(mn) && arg_count <= ptypes.size()) return mn;
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
    if (it->second.size() == 1) {
        if (std::get<1>(it->second[0]).size() != params.size()) return base;
        return std::get<0>(it->second[0]);
    }
    // multiple overloads: match by canonical param types (const-stripping)
    std::vector<std::string> ptypes;
    for (auto& [t, n] : params) ptypes.push_back(canonicalType(t));
    for (auto& [mangled, mp, _pm, _pmt, _fid] : it->second) {
        if (mp.size() != ptypes.size()) continue;
        bool match = true;
        for (size_t i = 0; i < mp.size(); i++) {
            if (canonicalType(mp[i]) != ptypes[i]) { match = false; break; }
        }
        if (match) return mangled;
    }
    return base;
}

// Register one method overload: writes func_return_types_/func_param_types_
// under the per-overload mangled name, records const-ness, and appends a
// deduped method_overloads_ entry under the un-suffixed `<slid>__<method>`
// bucket. Single source of truth so the normal-class and template-
// instantiation registration paths cannot drift in their mangling.
void Codegen::storeParamDefaults(
    const std::string& mangled,
    const std::vector<std::unique_ptr<Expr>>& param_defaults)
{
    std::vector<const Expr*> defs;
    for (auto& d : param_defaults)
        if (d) {
            if (!isFoldableConstShape(*d, ""))
                error("Default parameter value must be a constant expression.");
            defs.push_back(d.get());
        }
    if (!defs.empty() && !func_param_defaults_.count(mangled))
        func_param_defaults_[mangled] = defs;
}

void Codegen::registerMethodOverload(
    const std::string& slid_name,
    const std::string& method_name,
    const std::vector<std::pair<std::string, std::string>>& params,
    const std::vector<bool>& param_mutable,
    const std::vector<int>& param_mut_toks,
    const std::string& return_type,
    bool is_const, int file_id,
    const std::vector<std::unique_ptr<Expr>>& param_defaults)
{
    std::vector<std::string> ptypes = buildParamTypes(params, param_mutable);
    std::string mangled = mangleMethod(slid_name, method_name, ptypes);
    func_return_types_[mangled] = return_type;
    func_param_types_[mangled]  = ptypes;
    if (is_const) const_methods_.insert(mangled);
    storeParamDefaults(mangled, param_defaults);
    auto& overloads = method_overloads_[slid_name + "__" + method_name];
    for (auto& e : overloads)
        if (std::get<1>(e) == ptypes) return;   // already registered
    overloads.push_back({mangled, ptypes, param_mutable, param_mut_toks, file_id});
}

// Register one nested-function overload: writes the signature maps under the
// per-overload mangled name and appends a deduped nested_func_overloads_
// bucket entry (keyed by the bare nested name).
void Codegen::registerNestedOverload(const std::string& parent_name,
                                     const NestedFunctionDef& def)
{
    std::vector<std::string> ptypes = buildParamTypes(def.params, def.param_mutable);
    std::string mangled = mangleMethod(parent_name, def.name, ptypes);
    std::string ret;
    if (!def.tuple_return_fields.empty()) {
        ret = "{ ";
        for (int i = 0; i < (int)def.tuple_return_fields.size(); i++) {
            if (i > 0) ret += ", ";
            ret += llvmType(def.tuple_return_fields[i].first);
        }
        ret += " }";
        func_tuple_fields_[mangled] = def.tuple_return_fields;
    } else {
        ret = def.return_type;
    }
    func_return_types_[mangled] = ret;
    func_param_types_[mangled]  = ptypes;
    storeParamDefaults(mangled, def.param_defaults);
    auto& bucket = nested_func_overloads_[def.name];
    for (auto& e : bucket)
        if (std::get<1>(e) == ptypes) return;   // already registered
    bucket.push_back({mangled, ptypes, def.param_mutable, def.param_mut_toks, 0});
}

// Resolve a nested-function call to its overload-mangled name. "" when callee
// names no nested function (or resolution produced no concrete overload).
std::string Codegen::nestedCallMangled(
    const std::string& callee,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    auto it = nested_func_overloads_.find(callee);
    if (it == nested_func_overloads_.end()) return "";
    std::string m = resolveOverloadIn(callee, args, it->second);
    return nested_info_.count(m) ? m : "";
}

std::string Codegen::resolveOverloadForCall(
    const std::string& base_mangled,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    auto it = method_overloads_.find(base_mangled);
    if (it != method_overloads_.end())
        return resolveOverloadIn(base_mangled, args, it->second);
    // Not a method of this class — walk the base chain so a derived class
    // inherits its base's methods. The `<Class>` part of base_mangled is
    // everything before the final `__`; `self` upcasts to the base sub-object
    // at offset 0, so the resolved `<Base>__<method>` is called with it as-is.
    size_t sep = base_mangled.rfind("__");
    if (sep != std::string::npos) {
        std::string method = base_mangled.substr(sep + 2);
        auto ci = slid_info_.find(base_mangled.substr(0, sep));
        for (const SlidInfo* b = (ci != slid_info_.end() ? ci->second.base_info : nullptr);
             b; b = b->base_info) {
            auto bit = method_overloads_.find(b->name + "__" + method);
            if (bit != method_overloads_.end())
                return resolveOverloadIn(b->name + "__" + method, args, bit->second);
        }
    }
    return base_mangled;
}

// Derived→base upcast check — see header. `arg` (a class type, possibly `^`)
// binds `param` (`B^`) when arg's class derives from B.
bool Codegen::argUpcastsToParam(const std::string& arg, const std::string& param) {
    std::string p = canonicalType(param);
    if (p.empty() || p.back() != '^') return false;       // param must be a reference
    std::string base = p.substr(0, p.size() - 1);
    std::string a = canonicalType(arg);
    if (!a.empty() && a.back() == '^') a.pop_back();        // accept D or D^
    return !a.empty() && a != base && slid_info_.count(a) > 0
        && isAncestor(base, a);
}

// Pure type-match core: canonical-type pass then indirect-type pass over an
// explicit bucket. Returns the matching mangled name, or "" if none — no
// arity error, no throw. resolveOverloadIn layers fast paths and diagnostics
// on top; type-query callers use it directly without risking a throw.
std::string Codegen::matchOverload(
    const std::vector<OverloadEntry>& overloads,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    // Pass 1: canonical type match (const-stripping)
    for (auto& entry : overloads) {
        auto& ptypes = std::get<1>(entry);
        if (args.size() < requiredArity(std::get<0>(entry))
            || args.size() > ptypes.size()) continue;
        bool match = true;
        for (int i = 0; i < (int)args.size(); i++) {
            if (!argBindsToParam(exprType(*args[i]), ptypes[i])) { match = false; break; }
        }
        if (match) return std::get<0>(entry);
    }
    // Pass 2: indirect-type match (pointer vs scalar)
    for (auto& entry : overloads) {
        auto& ptypes = std::get<1>(entry);
        if (args.size() < requiredArity(std::get<0>(entry))
            || args.size() > ptypes.size()) continue;
        bool match = true;
        for (int i = 0; i < (int)args.size(); i++) {
            bool param_ptr = isIndirectType(ptypes[i]);
            std::string at = exprType(*args[i]);
            bool arg_ptr = (at == "nullptr") ? true
                         : (!at.empty()      ? isIndirectType(at)
                                             : isPointerExpr(*args[i]));
            if (param_ptr != arg_ptr) { match = false; break; }
        }
        if (match) return std::get<0>(entry);
    }
    // Pass 3: derived→base upcast — lowest priority, tried only after exact
    // and indirect matching fail, so an exact overload always wins.
    for (auto& entry : overloads) {
        auto& ptypes = std::get<1>(entry);
        if (args.size() < requiredArity(std::get<0>(entry))
            || args.size() > ptypes.size()) continue;
        bool match = true;
        for (int i = 0; i < (int)args.size(); i++) {
            std::string at = exprType(*args[i]);
            if (argBindsToParam(at, ptypes[i])) continue;
            if (!argUpcastsToParam(at, ptypes[i])) { match = false; break; }
        }
        if (match) return std::get<0>(entry);
    }
    return "";
}

// Non-throwing type-aware resolution of a namespace-qualified free-function
// call. `qname` is the already-qualified `Ns:fn` key. "" when nothing
// resolves. The emit path uses resolveOverloadIn instead, for rich errors.
std::string Codegen::resolveQualifiedFreeCall(
    const std::string& qname,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    auto it = free_func_overloads_.find(qname);
    if (it == free_func_overloads_.end() || it->second.empty()) return "";
    auto& bucket = it->second;
    if (bucket.size() == 1) {
        if (args.size() >= requiredArity(std::get<0>(bucket[0]))
            && args.size() <= std::get<1>(bucket[0]).size())
            return std::get<0>(bucket[0]);
        return "";
    }
    return matchOverload(bucket, args);
}

// Core overload picker over an explicit bucket — shared by method calls
// (method_overloads_) and nested-function calls (nested_func_overloads_).
std::string Codegen::resolveOverloadIn(
    const std::string& base_mangled,
    const std::vector<std::unique_ptr<Expr>>& args,
    const std::vector<OverloadEntry>& overloads)
{
    if (overloads.empty()) return base_mangled;
    // No overload's arity matches the call: explicit error so we don't fall
    // through to the single-overload fast path or pass 1/2 (which all skip
    // arity for size-1 or silently fail for size-N).
    bool any_arity = false;
    for (auto& [mangled, ptypes, _pm, _pmt, _fid] : overloads)
        if (args.size() >= requiredArity(mangled) && args.size() <= ptypes.size())
            { any_arity = true; break; }
    if (!any_arity) {
        std::string arities;
        for (size_t i = 0; i < overloads.size(); i++) {
            if (i > 0) arities += "/";
            arities += std::to_string(std::get<1>(overloads[i]).size());
        }
        size_t sep = base_mangled.rfind("__");
        std::string method = (sep != std::string::npos) ? base_mangled.substr(sep+2) : base_mangled;
        error("Wrong number of arguments to '" + method + "': passed "
              + std::to_string(args.size()) + ", expected " + arities + ".");
    }
    auto display = [&] {
        size_t sep = base_mangled.rfind("__");
        return (sep != std::string::npos) ? base_mangled.substr(sep+2) : base_mangled;
    };
    if (overloads.size() == 1) {
        rejectConstToMutable(display(), args, overloads[0]);
        return std::get<0>(overloads[0]);
    }
    // Type-aware pick: canonical-type then indirect-type passes.
    if (std::string m = matchOverload(overloads, args); !m.empty()) {
        for (auto& entry : overloads)
            if (std::get<0>(entry) == m) {
                rejectConstToMutable(display(), args, entry);
                break;
            }
        return m;
    }
    // Arity matched but no overload's parameter types accept the arguments.
    // Fail loudly rather than returning the bare base (which can alias a
    // zero-parameter overload's mangled name).
    std::string got;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) got += ", ";
        std::string t = exprType(*args[i]);
        got += t.empty() ? "?" : t;
    }
    std::string cands;
    for (auto& entry : overloads) {
        auto& ptypes = std::get<1>(entry);
        if (args.size() < requiredArity(std::get<0>(entry))
            || args.size() > ptypes.size()) continue;
        std::string sig;
        for (size_t i = 0; i < ptypes.size(); i++) {
            if (i > 0) sig += ", ";
            sig += ptypes[i];
        }
        if (!cands.empty()) cands += " / ";
        cands += "(" + sig + ")";
    }
    error("No overload of '" + display() + "' matches arguments ("
          + got + "); candidates: " + cands + ".");
    return base_mangled; // unreachable — error() throws
}

// Resolve a bare callee as an implicit-self method of current_slid_. Returns
// the overload-resolved mangled name, or "" when current_slid_ is unset or
// the callee names no method of it. Mirrors emitExpr's self-call arm so type
// queries and emission resolve self-method calls identically.
std::string Codegen::selfMethodMangled(
    const std::string& callee,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    if (current_slid_.empty()) return "";
    std::string mangled = resolveOverloadForCall(current_slid_ + "__" + callee, args);
    return func_return_types_.count(mangled) ? mangled : "";
}

// Overload-resolved `<obj_slid>__<method>` mangled name for an explicit
// method call. Single source of truth for the method-call base so every
// type query resolves overloads the same way emission does.
std::string Codegen::methodMangled(
    const std::string& obj_slid,
    const std::string& method,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    return resolveOverloadForCall(obj_slid + "__" + method, args);
}

bool Codegen::isPointerExpr(const Expr& expr) {
    if (dynamic_cast<const StringLiteralExpr*>(&expr)) return true;
    if (dynamic_cast<const NullptrExpr*>(&expr))       return true;
    if (dynamic_cast<const AddrOfExpr*>(&expr))        return true;
    if (!newExprResultType(expr).empty())              return true;
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                return isIndirectType(info.field_types[fit->second]);
        }
        auto tit = locals_.find(ve->name);
        if (tit != locals_.end()) return isIndirectType(tit->second.type);
    }
    // field access through ptr dereference: sa^.storage_
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        std::string slid_name;
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = locals_.find(ve2->name);
                if (tit != locals_.end()) {
                    slid_name = tit->second.type;
                    if (isRefType(slid_name)) slid_name.pop_back();
                    else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                }
            }
        } else if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
            auto tit = locals_.find(ve2->name);
            if (tit != locals_.end()) slid_name = tit->second.type;
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
        auto tit = locals_.find(ve->name);
        if (tit != locals_.end()) return utypes.count(tit->second.type) > 0;
    }

    // field access: obj^.field_ or obj.field_
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        std::string slid_name;
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = locals_.find(ve2->name);
                if (tit != locals_.end()) {
                    slid_name = tit->second.type;
                    if (isRefType(slid_name))      slid_name.pop_back();
                    else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                }
            }
        } else if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
            auto tit = locals_.find(ve2->name);
            if (tit != locals_.end()) slid_name = tit->second.type;
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
        // comparisons and logical ops yield a 0/1 boolean value; treat as
        // unsigned (bool-rank) so it widens cleanly into both signed and
        // unsigned int targets — the value always fits.
        if (be->op == "==" || be->op == "!=" || be->op == "<"  ||
            be->op == ">"  || be->op == "<=" || be->op == ">=" ||
            be->op == "&&" || be->op == "||" || be->op == "^^")
            return true;
        return isUnsignedExpr(*be->left) || isUnsignedExpr(*be->right);
    }

    // dereference: signedness comes from the pointer's pointee type
    // (`(const char)[]` iter → `^elem` is unsigned `char`; `int^` → signed).
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        std::string pt = inferSlidType(*de->operand);
        // Peel one level of ^ / [] to get the pointee type.
        if (pt.size() >= 2 && pt.substr(pt.size() - 2) == "[]")
            pt = pt.substr(0, pt.size() - 2);
        else if (!pt.empty() && pt.back() == '^')
            pt = pt.substr(0, pt.size() - 1);
        static const std::set<std::string> utypes_pe =
            {"bool","char","uint","uint8","uint16","uint32","uint64"};
        std::string canon = canonicalType(pt);
        return utypes_pe.count(canon) > 0;
    }

    // post-inc/dec, pre-inc/dec, unary +/- — result is the operand's type.
    if (auto* ue = dynamic_cast<const UnaryExpr*>(&expr)) {
        if (ue->op == "post++" || ue->op == "post--"
            || ue->op == "pre++" || ue->op == "pre--"
            || ue->op == "+" || ue->op == "-")
            return isUnsignedExpr(*ue->operand);
    }

    // array index: signedness from the base's element type. Walk the chain
    // to the deepest base, then peel one [] level for the element.
    if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        std::string pt = inferSlidType(*ai->base);
        if (pt.size() >= 2 && pt.substr(pt.size() - 2) == "[]")
            pt = pt.substr(0, pt.size() - 2);
        static const std::set<std::string> utypes_ai =
            {"bool","char","uint","uint8","uint16","uint32","uint64"};
        return utypes_ai.count(canonicalType(pt)) > 0;
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
        auto tit = locals_.find(ve->name);
        if (tit != locals_.end() && slid_info_.count(tit->second.type))
            return tit->second.type;
        // Self-field reachable via the implicit-self path inside a method —
        // mirror `inferSlidType`'s VarExpr arm so a receiver like `rings_`
        // in `rings_.size()` resolves to its slid type. Without this the
        // gated MethodCallExpr arm in `inferSlidType` loses the receiver's
        // type, falls through to the default `int`, and an inferred-decl
        // (`nrings = rings_.size();`) gets the wrong width.
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end()) {
                const std::string& t = info.field_types[fit->second];
                if (slid_info_.count(t)) return t;
            }
        }
        // Global slid field — same access as self-fields, distinguished by
        // namespace path / function-internal visibility.
        if (auto* ge = lookupGlobal(ve->name)) {
            if (slid_info_.count(ge->slids_type)) return ge->slids_type;
        }
        // type name used directly as an anonymous temp (not in locals_)
        if (tit == locals_.end() && slid_info_.count(ve->name))
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
        // namespace-qualified free-function call: ns:fn(args)
        if (!ce->qualifier.empty() && ce->qualifier != "::"
            && !slid_info_.count(ce->qualifier)) {
            std::string m = resolveQualifiedFreeCall(
                qualifiedName(ce->qualifier, ce->callee), ce->args);
            if (!m.empty()) {
                auto rit = func_return_types_.find(m);
                if (rit != func_return_types_.end() && slid_info_.count(rit->second))
                    return rit->second;
            }
        }
        if (std::string nm = nestedCallMangled(ce->callee, ce->args); !nm.empty()) {
            const std::string& rt = func_return_types_[nm];
            if (slid_info_.count(rt)) return rt;
        }
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
        // implicit-self method call — resolved the same way emitExpr does.
        if (std::string sm = selfMethodMangled(ce->callee, ce->args); !sm.empty()) {
            const std::string& rt = func_return_types_[sm];
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
            // self short-circuits to current_slid_; equivalent to looking up
            // locals_["self"].type (set at method entry to the same value).
            if (ve->name == "self" && !current_slid_.empty())
                obj_slid = current_slid_;
            else {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) obj_slid = tit->second.type;
            }
        }
        if (!obj_slid.empty()) {
            std::string mangled = methodMangled(obj_slid, mc->method, mc->args);
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
                std::string t = pointeeForLookup(pit->second[0]);
                if (slid_info_.count(t)) return t;
            }
        }
        // Phase 2: exact overload not found, but left is a slid and right can be coerced via op=
        std::string left_slid = exprSlidType(*be->left);
        if (!left_slid.empty()) {
            std::string coerce = resolveSingleArgOverload(left_slid + "__op=", *be->right);
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
        if (il->is_bool)         return "bool";
        if (il->is_char_literal) return "char";
        if (il->is_nondecimal)   return "uint64";
        return "int";
    }
    if (dynamic_cast<const FloatLiteralExpr*>(&expr)) return "float64";
    if (dynamic_cast<const NullptrExpr*>(&expr))      return "nullptr";
    if (dynamic_cast<const SizeofExpr*>(&expr))       return "intptr";
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr))  return nc->target_type;
    if (auto* pc = dynamic_cast<const PtrCastExpr*>(&expr))   return pc->target_type;
    if (auto t = newExprResultType(expr); !t.empty()) return t;
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        // `Base:self` — typed as the named base sub-object.
        if (ve->name.size() > 5
            && ve->name.compare(ve->name.size() - 5, 5, ":self") == 0)
            return ve->name.substr(0, ve->name.size() - 5);
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                return info.field_types[fit->second];
        }
        auto tit = locals_.find(ve->name);
        if (tit != locals_.end()) return tit->second.type;
        if (slid_info_.count(ve->name)) return ve->name;
        return "";
    }
    if (auto* ao = dynamic_cast<const AddrOfExpr*>(&expr)) {
        std::string t = exprType(*ao->operand);
        if (t.empty()) return "";
        // ^x where x: const T  →  (const T)^ (reference-to-const). The leaf
        // const must be paren-wrapped so future readers see "mutable handle,
        // const pointee" — not full-const "const T^".
        if (typeStartsWithConst(t)) return "(" + t + ")^";
        return t + "^";
    }
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        std::string t = exprType(*de->operand);
        if (isRefType(t)) return stripRedundantConstParens(t.substr(0, t.size() - 1));
        if (isPtrType(t)) return stripRedundantConstParens(t.substr(0, t.size() - 2));
        // Class instance with op^() defined — desugar `x^` to `x.op^()^` and
        // report the pointee of op^()'s return type as the result.
        std::string base = canonType(t);
        if (slid_info_.count(base)) {
            std::string mangled = resolveDerefOverload(base);
            if (!mangled.empty()) {
                auto rit = func_return_types_.find(mangled);
                if (rit != func_return_types_.end()) {
                    const std::string& rt = rit->second;
                    if (isRefType(rt)) return stripRedundantConstParens(rt.substr(0, rt.size() - 1));
                    if (isPtrType(rt)) return stripRedundantConstParens(rt.substr(0, rt.size() - 2));
                }
            }
        }
        return "";
    }
    if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        std::string t = exprType(*ai->base);
        if (isPtrType(t)) return stripRedundantConstParens(t.substr(0, t.size() - 2));
        auto lb = t.rfind('[');
        if (lb != std::string::npos && lb > 0) return t.substr(0, lb);
        return "";
    }
    if (auto* se = dynamic_cast<const SliceExpr*>(&expr)) return exprType(*se->base);
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        std::string obj_slid;
        bool obj_is_const = false;
        if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
            if (!current_slid_.empty()) {
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve2->name);
                if (fit != info.field_index.end()) {
                    obj_slid = info.field_types[fit->second];
                    if (isRefType(obj_slid) || isPtrType(obj_slid)) {
                        auto pi = pointeeInfo(obj_slid);
                        obj_slid = pi.name;
                        obj_is_const = pi.is_const;
                    }
                    if (!slid_info_.count(obj_slid)) obj_slid.clear();
                }
            }
            if (obj_slid.empty()) {
                auto tit = locals_.find(ve2->name);
                if (tit != locals_.end()) obj_slid = tit->second.type;
            }
        } else if (auto* de2 = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de2->operand.get())) {
                auto tit = locals_.find(ve2->name);
                if (tit != locals_.end()) {
                    auto pi = pointeeInfo(tit->second.type);
                    obj_slid = pi.name;
                    obj_is_const = pi.is_const;
                }
            }
        } else {
            obj_slid = exprSlidType(*fa->object);
        }
        if (!obj_slid.empty()) {
            // The non-indirect paths above may leave a leading-const obj_slid
            // (e.g. a const local). Strip + remember the const for propagation.
            if (typeStartsWithConst(obj_slid)) {
                obj_is_const = true;
                obj_slid = baseSlidType(obj_slid);
            }
            if (slid_info_.count(obj_slid)) {
                auto& info = slid_info_[obj_slid];
                auto fit = info.field_index.find(fa->field);
                if (fit != info.field_index.end()) {
                    std::string ftype = info.field_types[fit->second];
                    if (obj_is_const) ftype = propagateConst(ftype);
                    return ftype;
                }
            }
        }
        return "";
    }
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        // namespace-qualified free-function call: ns:fn(args)
        if (!ce->qualifier.empty() && ce->qualifier != "::"
            && !slid_info_.count(ce->qualifier)) {
            std::string m = resolveQualifiedFreeCall(
                qualifiedName(ce->qualifier, ce->callee), ce->args);
            if (!m.empty()) {
                auto rit = func_return_types_.find(m);
                if (rit != func_return_types_.end()) return rit->second;
            }
        }
        if (std::string nm = nestedCallMangled(ce->callee, ce->args); !nm.empty())
            return func_return_types_[nm];
        // implicit-self method call — resolved the same way emitExpr does.
        if (std::string sm = selfMethodMangled(ce->callee, ce->args); !sm.empty())
            return func_return_types_[sm];
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
            // self short-circuit; same value as locals_["self"].type.
            if (ve->name == "self" && !current_slid_.empty())
                obj_slid = current_slid_;
            else {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) obj_slid = tit->second.type;
            }
        } else {
            obj_slid = exprSlidType(*mc->object);
        }
        if (!obj_slid.empty()) {
            std::string mangled = methodMangled(obj_slid, mc->method, mc->args);
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

    // helper: extract the slid type name from the left operand expression.
    // Pointer-typed operands (T^ / T[]) intentionally do not match — slids has
    // no auto-deref for op-method dispatch; the user writes ptr^ explicitly.
    auto leftSlidName = [&]() -> std::string {
        if (auto* ve = dynamic_cast<const VarExpr*>(&left)) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) {
                const std::string& t = tit->second.type;
                if (slid_info_.count(t)) return t;
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
                for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second)
                    if (!ptypes.empty() && isPtrType(ptypes[0])) return slid;
            }
        }
        return "";
    };

    // helper: does this expression match a slid-typed parameter (SlidType^)?
    auto leftMatchesSlid = [&](const std::string& slid_name) -> bool {
        // VarExpr: local variable of exact slid type, or type name used as anonymous temp
        if (auto* ve = dynamic_cast<const VarExpr*>(&left)) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()
                && (tit->second.type == slid_name
                    || isAncestor(slid_name, tit->second.type))) return true;
            if (tit == locals_.end() && ve->name == slid_name) return true;
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
                for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second)
                    if (!ptypes.empty() && isPtrType(ptypes[0])) return true;
            }
        }
        return false;
    };

    // helper: does the right operand match param type p1?
    auto argMatchesParam = [&](const Expr& arg, const std::string& p1_raw) -> bool {
        std::string p1 = canonicalType(p1_raw);
        bool p1_is_slid_ref = isRefType(p1) && slid_info_.count(p1.substr(0, p1.size()-1));
        bool p1_is_ptr = isIndirectType(p1); // ^ or [] — any pointer/reference type
        std::string p1_slid = p1_is_slid_ref ? p1.substr(0, p1.size()-1) : "";
        // arg is a string literal → matches slid ref (implicit temp) or ptr param
        if (dynamic_cast<const StringLiteralExpr*>(&arg)) return p1_is_slid_ref || p1_is_ptr;
        // an integer/char literal matches any non-pointer param (it flexes,
        // including to a float param).
        if (dynamic_cast<const IntLiteralExpr*>(&arg)) return !p1_is_ptr;
        // a float literal binds only to a float param — forcing it into an
        // integer param would emit a float constant for an i-typed operand.
        if (dynamic_cast<const FloatLiteralExpr*>(&arg)) {
            static const std::set<std::string> float_types =
                {"float","float32","float64"};
            return float_types.count(p1) > 0;
        }
        // arg is a variable
        if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) {
                std::string t = canonicalType(tit->second.type);
                std::string r_slid;
                if (slid_info_.count(t)) r_slid = t;
                else if (!t.empty() && t.back() == '^' && slid_info_.count(t.substr(0, t.size()-1)))
                    r_slid = t.substr(0, t.size()-1);
                if (p1_is_slid_ref)
                    return !r_slid.empty()
                        && (r_slid == p1_slid || isAncestor(p1_slid, r_slid));
                return r_slid.empty();
            }
        }
        // arg is a DerefExpr → produces slid value
        if (auto* de = dynamic_cast<const DerefExpr*>(&arg)) {
            if (p1_is_slid_ref) {
                std::string ds = derefSlidName(*de);
                if (!ds.empty()) return ds == p1_slid || isAncestor(p1_slid, ds);
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end()) {
                        std::string t = canonicalType(tit->second.type);
                        if (!t.empty() && t.back() == '^') t.pop_back();
                        if (slid_info_.count(t))
                            return t == p1_slid || isAncestor(p1_slid, t);
                    }
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
                static const std::set<std::string> cmp_ops = {"==", "!=", "<", ">", "<=", ">="};
                bool is_cmp = cmp_ops.count(op) > 0;
                std::string best;
                for (auto& [mangled, ptypes, _pm, _pmt, _fid] : moit->second) {
                    if (ptypes.empty()) continue;
                    // 2-arg method (binary op produces self): both ptypes are operand types.
                    // 1-arg method: comparison style only (ptypes[0] is rhs; self is lhs).
                    // Unary arity-1 overloads (op+/op-/op~/op!) live in the same bucket
                    // but are dispatched separately — never match here.
                    if (ptypes.size() > 1) {
                        if (!argMatchesParam(left,  ptypes[0])) continue;
                        if (!argMatchesParam(right, ptypes[1])) continue;
                    } else {
                        if (!is_cmp) continue;
                        if (!argMatchesParam(right, ptypes[0])) continue;
                    }
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
    for (auto& [mangled, ptypes, _pm, _pmt, _fid] : foit->second) {
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
        if (ptypes.size() > 1 && !argMatchesParam(right, ptypes[1])) continue;
        // matched: prefer more specific (multi-param) overload
        if (best.empty() || ptypes.size() > func_param_types_[best].size())
            best = mangled;
    }
    return best;
}

std::string Codegen::resolveCompoundFuse(const std::string& op,
                                          const Expr& left, const Expr& right) {
    if (!isCompoundableOp(op)) return "";
    std::string left_slid = exprSlidType(left);
    if (left_slid.empty()) return "";
    std::string base = left_slid + "__op" + op + "=";
    if (!method_overloads_.count(base)) return "";
    return resolveSingleArgOverload(base, right);
}

bool Codegen::isMethodMangled(const std::string& mangled) const {
    for (auto& [base, overloads] : method_overloads_)
        for (auto& [m, ptypes, _pm, _pmt, _fid] : overloads)
            if (m == mangled) return true;
    return false;
}

std::string Codegen::resolveSlidIndex(const std::string& slid_name, const Expr& index) {
    std::string base = slid_name + "__op[]";
    auto oit = method_overloads_.find(base);
    if (oit == method_overloads_.end() || oit->second.empty()) return "";
    std::string mangled = resolveSingleArgOverload(base, index);
    if (mangled.empty()) mangled = std::get<0>(oit->second[0]);
    return mangled;
}

std::string Codegen::resolveDerefOverload(const std::string& slid_name) {
    auto it = method_overloads_.find(slid_name + "__op^");
    if (it == method_overloads_.end()) return "";
    for (auto& [m, ptypes, _pm, _pmt, _fid] : it->second) {
        if (ptypes.empty()) return m;
    }
    return "";
}

std::string Codegen::derefSlidName(const DerefExpr& de) {
    if (auto* ve = dynamic_cast<const VarExpr*>(de.operand.get())) {
        auto tit = locals_.find(ve->name);
        if (tit == locals_.end()) return "";
        // Pointer/iterator local: strip the indirection, check if pointee is a slid.
        if (isIndirectType(tit->second.type)) {
            std::string t = pointeeForLookup(tit->second.type);
            if (!t.empty() && slid_info_.count(t)) return t;
            return "";
        }
        // Class local with op^() defined: the deref result type is the pointee
        // of op^()'s return type. Only report a slid name if that pointee is a slid.
        std::string base = canonType(tit->second.type);
        if (slid_info_.count(base)) {
            std::string mangled = resolveDerefOverload(base);
            if (!mangled.empty()) {
                auto rit = func_return_types_.find(mangled);
                if (rit != func_return_types_.end()) {
                    std::string pointee = pointeeForLookup(rit->second);
                    if (!pointee.empty() && slid_info_.count(pointee)) return pointee;
                }
            }
        }
        return "";
    }
    std::string ot = inferSlidType(*de.operand);
    if (isPtrType(ot) || isIndirectType(ot)) {
        std::string pointee = pointeeForLookup(ot);
        if (slid_info_.count(pointee)) return pointee;
    }
    return "";
}

// Pick the best-fit single-argument overload for the given argument expression.
// Used by op=, op<-, compound op<sym>=, and op[] dispatch.
// Priority: slid ref > exact type name match > best-fit (smallest rank >= arg rank) > ptr/char[] fallback.
std::string Codegen::resolveSingleArgOverload(const std::string& base, const Expr& arg) {
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
    bool arg_is_float = false;
    std::string arg_int_type;       // specific type name when known (enables exact + best-fit matching)
    std::string arg_float_type;     // float32 / float64 when arg_is_float
    std::string arg_slid_name;      // specific slid type name when arg_is_slid

    auto classify_int = [&](const std::string& t) {
        if (t == "float" || t == "float32" || t == "float64") {
            arg_is_float = true; arg_float_type = t; return;
        }
        if (t == "char") { arg_is_char = true; arg_int_type = "char"; }
        else if (unsigned_types.count(t)) { arg_is_unsigned = true; arg_int_type = t; }
        else { arg_is_scalar_int = true; arg_int_type = t; } // bool, int8, int16, int32, int64, int, ...
    };

    if (auto* ile = dynamic_cast<const IntLiteralExpr*>(&arg)) {
        arg_is_char = ile->is_char_literal;
        arg_is_scalar_int = !ile->is_char_literal;
        if (ile->is_char_literal) arg_int_type = "char";
        else if (ile->is_bool)    arg_int_type = "bool";
        // untyped integer literal: arg_int_type left empty — any signed int overload is acceptable
    } else if (dynamic_cast<const FloatLiteralExpr*>(&arg)) {
        // a float literal has no fixed width — treat as float64 for matching.
        arg_is_float = true; arg_float_type = "float64";
    } else if (auto* nc = dynamic_cast<const TypeConvExpr*>(&arg)) {
        classify_int(nc->target_type);
    } else if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
        // Resolve the bare name from locals_, falling back to a field of the
        // enclosing class — an unqualified field name is not in locals_.
        std::string raw_t;
        auto tit = locals_.find(ve->name);
        if (tit != locals_.end()) {
            raw_t = tit->second.type;
        } else if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                raw_t = info.field_types[fit->second];
        }
        if (!raw_t.empty()) {
            std::string t = canonicalType(raw_t);
            if (!t.empty() && t.back() == '^') t.pop_back();
            else if (t.size() >= 2 && t.substr(t.size()-2) == "[]") t = t.substr(0, t.size()-2);
            arg_is_slid = slid_info_.count(t) > 0;
            if (arg_is_slid) arg_slid_name = t;
            if (!arg_is_slid && !isIndirectType(canonicalType(raw_t)))
                classify_int(canonicalType(raw_t));
        }
    } else if (auto* ao = dynamic_cast<const AddrOfExpr*>(&arg)) {
        // ^x: addr-of x
        // ^x where x: SlidType (non-indirect) → SlidType^ — valid slid ref arg
        // ^x where x: SlidType^ or char[] → double-indirect — no match for any overload
        if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
            // locals_ first, then a field of the enclosing class.
            std::string raw_t;
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) {
                raw_t = tit->second.type;
            } else if (!current_slid_.empty()) {
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve->name);
                if (fit != info.field_index.end())
                    raw_t = info.field_types[fit->second];
            }
            if (!raw_t.empty()) {
                if (isIndirectType(raw_t))
                    return "";  // ^ref is double-indirect: type error
                if (slid_info_.count(raw_t)) {
                    arg_is_slid = true;  // ^slid_var is a valid slid ref
                    arg_slid_name = raw_t;
                }
            }
        }
    } else if (!exprSlidType(arg).empty()) {
        arg_is_slid = true; // e.g. result of op+ expression
        arg_slid_name = exprSlidType(arg);
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
        else if (lt == "float" || lt == "double")
            classify_int(lt == "float" ? "float32" : "float64");
    }

    auto is_signed_int_param = [&](const std::string& pt) {
        return !isIndirectType(pt) && pt != "char" && !unsigned_types.count(pt);
    };

    // pass 1: slid ref exact match — same slid type. Compare canonical
    // (strip const, unwrap "(const T)" parens) so the default-const-on-param
    // form "(const T)^" matches an arg of slid type "T".
    for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
        if (ptypes.size() != 1 || !isRefType(ptypes[0])) continue;
        std::string param_slid = baseSlidType(ptypes[0].substr(0, ptypes[0].size()-1));
        if (arg_is_slid && param_slid == baseSlidType(arg_slid_name)) return m;
    }

    // pass 2: exact type name match for char/int/uint
    if (!arg_int_type.empty()) {
        for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
            if (ptypes.size() == 1 && ptypes[0] == arg_int_type) return m;
        }
    }

    // float arg → float param: exact width first, then any float overload.
    if (arg_is_float) {
        static const std::set<std::string> float_types = {"float","float32","float64"};
        for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second)
            if (ptypes.size() == 1 && canonicalType(ptypes[0]) == arg_float_type) return m;
        for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second)
            if (ptypes.size() == 1 && float_types.count(canonicalType(ptypes[0]))) return m;
        return "";  // no float overload — caller coerces via temp / errors
    }

    // pass 3: best-fit — smallest overload rank >= arg rank (avoids picking int64 when int32 exists)
    if ((arg_is_scalar_int || arg_is_unsigned) && !arg_int_type.empty()) {
        auto rit = int_rank.find(arg_int_type);
        if (rit != int_rank.end()) {
            int arg_r = rit->second;
            std::string best_m;
            int best_r = INT_MAX;
            for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
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
        for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
            if (ptypes.size() == 1 && is_signed_int_param(ptypes[0])) return m;
        }
    }
    if (arg_is_unsigned && arg_int_type.empty()) {
        for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
            if (ptypes.size() == 1 && ptypes[0] != "char" && unsigned_types.count(ptypes[0])) return m;
        }
    }

    // pass 5: non-slid, non-int arg — ptr/indirect param (e.g. string literal / char[])
    if (!arg_is_slid && !arg_is_char && !arg_is_scalar_int && !arg_is_unsigned
            && !arg_is_float) {
        for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
            if (ptypes.size() == 1 && isPtrType(ptypes[0])) return m;
        }
        for (auto& [m, ptypes, _pm, _pmt, _fid] : oit->second) {
            if (ptypes.size() == 1 && isIndirectType(ptypes[0])) return m;
        }
    }

    // slid arg with no matching slid-ref overload: return "" so the caller can synthesize a copy
    if (arg_is_slid) return "";
    // scalar int/unsigned with no matching overload: return "" so caller can coerce via temp
    if (arg_is_scalar_int || arg_is_unsigned) return "";
    // non-slid arg: fall back to first available overload
    return std::get<0>(oit->second[0]);
}
