#include "resolve.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic.h"
#include "parse.h"
#include "widen.h"

namespace resolve {

// Relocate the external qualified defs (`int C:m(){}`, `C:Ns{}`, `C:R(){}`) among a
// scope's `children` into their target class/namespace, BEFORE that scope registers
// its members. Runs per-scope (file / namespace / class / function body) so the
// external re-open form works in any scope the class is declared in. Defined at
// resolve:: scope (with collectScopeOpenings) but forward-declared here so the
// anon-namespace scope resolvers can call it.
void relocateOutOfLineMembers(parse::Tree& tree,
                              std::vector<std::unique_ptr<parse::Node>>& children,
                              diagnostic::Sink& diag);

// Synthesize a class's default copy/move/swap operators (op=/op<--/op<-->(Self^)) as real
// methods. Defined at resolve:: scope, forward-declared here so the anon-namespace
// resolveScopeBodies — the single per-class body choke point — can call it.
void synthesizeClassTransferOps(parse::Tree& tree, parse::Node& cnode,
                                diagnostic::Sink& diag);
void synthesizeOpaqueCtor(parse::Tree& tree, parse::Node& cnode,
                          diagnostic::Sink& diag);

// Instantiate the CLASS template `tmpl_entry_id` with the canonicalized `args`.
// Defined at resolve:: scope (with the template machinery, bottom of this file),
// forward-declared here so resolveTypeRef's kTmplUse arm and the construction
// arm can call it. Returns the instance's kClass entry id, or -1 (reported).
int instantiateClassTemplate(parse::Tree& tree, int tmpl_entry_id,
                             std::vector<widen::TypeRef> const& args,
                             int file_id, int tok, diagnostic::Sink& diag);

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

// A fixed-size array type spelling (`int[5]`, `int[3][5]`), distinct from
// `int[]` (iterator) and `int^` (reference).
bool isArrayType(widen::TypeRef t) {
    return widen::form(t) == widen::Type::Form::kArray;
}

// An in-place indexable aggregate: a fixed-size array or a tuple. Both store
// their slots directly in the local (a subscript GEPs into them), so a slot
// write ASSIGNS the aggregate (monotonic may-set) rather than reading through a
// pointer — the same definite-assignment treatment for either. (A class is an
// in-place aggregate too, but it is default-constructed at its decl, so it is
// already initialized and never needs the may-set carve-out.)
bool isInPlaceAggregate(widen::TypeRef t) {
    widen::Type::Form f = widen::form(widen::strip(t));
    return f == widen::Type::Form::kArray || f == widen::Type::Form::kTuple;
}

bool isReferenceType(widen::TypeRef t) {
    return widen::form(t) == widen::Type::Form::kPointer;
}

// A `const`-marked decl whose declared type is a FOLDABLE SCALAR (a primitive —
// numeric / bool / char, or an enum facet, which strip peels to its underlying
// primitive) is a SUBSTITUTED named constant. Any other KNOWN type (pointer /
// iterator / array / tuple / class) is a not-mutable VARIABLE: allocated +
// initialized, its type deep-const-qualified. A typeless const (kNoType — type
// inferred later) stays on the substitution path. True iff the const is a variable
// (needs real storage) rather than a substituted scalar.
bool constNeedsStorage(widen::TypeRef t) {
    return t != widen::kNoType
        && widen::form(widen::strip(t)) != widen::Type::Form::kPrimitive;
}

int resolveName(parse::Tree const& tree, std::string const& name);

// Is `name` a namespace/class member that is a TYPE (a hoisted class, a member
// alias, or an enum facet) — so qualifying it would actually yield a type? Used to
// hint "needs a qualifier" ONLY when the qualifier helps (not for a member const /
// function, where the qualified form still isn't a type).
bool namespaceMemberTypeExists(parse::Tree const& tree, std::string const& name) {
    using K = parse::EntryKind;
    for (parse::Entry const& e : tree.entries) {
        if (e.owner_ns_frame < 0 || e.name != name) continue;
        if (e.kind == K::kClass || e.kind == K::kAlias) return true;
        if (e.kind == K::kNamespace && e.slids_type != widen::kNoType) return true;
    }
    return false;
}

// Report a name collision, careting the source-LATER declaration as the duplicate
// and the earlier as "first declared here". Registration order need not match
// source order (e.g. classes register before file-scope consts/functions), so
// compare positions. `(ef,et)` = the existing entry, `(nf,nt)` = the new one.
void reportNameCollision(diagnostic::Sink& diag, std::string const& msg,
                         int ef, int et, int nf, int nt) {
    if (ef != nf || et <= nt)
        diagnostic::report(diag, {nf, nt, msg, {{ef, et, "first declared here"}}});
    else
        diagnostic::report(diag, {ef, et, msg, {{nf, nt, "first declared here"}}});
}

// A type whose leaf (through alias / pointer / iterator / array wrappers) is a
// kSlid naming a registered class — a known type even though widen::isKnownType
// (which knows only built-ins) says otherwise.
bool leafIsKnownClass(parse::Tree const& tree, widen::TypeRef t) {
    using F = widen::Type::Form;
    for (;;) {
        widen::Type const& ty = widen::get(t);
        if (ty.form == F::kAlias || ty.form == F::kConst) { t = ty.underlying; continue; }
        if (ty.form == F::kPointer || ty.form == F::kIterator) { t = ty.pointee; continue; }
        if (ty.form == F::kArray) { t = ty.elem; continue; }
        if (ty.form == F::kTuple) {
            // A tuple is a known type iff every slot is — a built-in (isKnownType)
            // or itself a class-leaf. Recurse per slot (slots are heterogeneous, so
            // there is no single leaf to walk to). Copy the slots first: the
            // recursive call reads the arena, but keep the loop independent of the
            // `ty` reference for safety.
            std::vector<widen::TypeRef> slots = ty.slots;
            for (widen::TypeRef s : slots)
                if (!widen::isKnownType(s) && !leafIsKnownClass(tree, s)) return false;
            return true;
        }
        if (ty.form == F::kSlid) {
            // A template TYPE-PARAMETER marker is "known" in a template's pattern
            // signature — the instance resolves the real type.
            if (ty.def_id == widen::kTmplParamDefId) return true;
            return tree.classes.count(t) > 0;
        }
        return false;
    }
}

// Reject the declared / return / parameter type if it's not a known spelling.
// Caret points at the construct's own tok (the type-name token's position),
// which together with surrounding source context tells the user where the
// unknown name appears.
void requireKnownType(parse::Tree const& tree, widen::TypeRef t,
                      int file_id, int tok, diagnostic::Sink& diag) {
    // `void` has no stride: it may only be a reference (`void^`), never an
    // iterator (`void[]`) or array (`void[N]`) — i.e. an iterator/array wrapping
    // a void element directly. strip() sees through an alias to the same end.
    {
        widen::Type const& ty = widen::get(widen::strip(t));
        bool void_iter = ty.form == widen::Type::Form::kIterator
                      && widen::form(ty.pointee) == widen::Type::Form::kVoid;
        bool void_arr  = ty.form == widen::Type::Form::kArray
                      && widen::form(ty.elem) == widen::Type::Form::kVoid;
        if (void_iter || void_arr) {
            diagnostic::report(diag, {file_id, tok,
                "A void pointer must be a reference 'void^'; void has no stride and "
                "cannot be an iterator or array.", {}});
            return;
        }
    }
    if (widen::isKnownType(t) || leafIsKnownClass(tree, t)) return;
    // If the leaf name resolves to a non-type entry (a namespace, function,
    // constant, variable), name that precisely instead of "Unknown type".
    {
        using F = widen::Type::Form;
        widen::TypeRef leaf = t;
        for (;;) {
            widen::Type const& ty = widen::get(leaf);
            if (ty.form == F::kAlias || ty.form == F::kConst) { leaf = ty.underlying; continue; }
            if (ty.form == F::kPointer || ty.form == F::kIterator) { leaf = ty.pointee; continue; }
            if (ty.form == F::kArray) { leaf = ty.elem; continue; }
            break;
        }
        widen::Type const& lt = widen::get(leaf);
        if (lt.form == F::kSlid) {
            int id = resolveName(tree, lt.name);
            char const* what = id < 0 ? nullptr
                : tree.entries[id].kind == parse::EntryKind::kNamespace ? "namespace"
                : tree.entries[id].kind == parse::EntryKind::kFunction  ? "function"
                : tree.entries[id].kind == parse::EntryKind::kConst     ? "constant"
                : tree.entries[id].kind == parse::EntryKind::kLocalVar  ? "variable"
                : nullptr;
            if (what) {
                diagnostic::report(diag, {file_id, tok,
                    "'" + lt.name + "' is a " + what + ", not a type.", {}});
                return;
            }
            // Out of scope here, but it IS a member TYPE somewhere (a hoisted
            // class, a member alias/enum) — it just needs its `Host:` qualifier.
            // (A member const/function of that name wouldn't be a type even
            // qualified, so it doesn't earn the hint.)
            if (id < 0 && namespaceMemberTypeExists(tree, lt.name)) {
                diagnostic::report(diag, {file_id, tok,
                    "'" + lt.name + "' needs a namespace qualifier.", {}});
                return;
            }
        }
    }
    diagnostic::report(diag, {file_id, tok,
        "Unknown type '" + widen::spell(t) + "'.", {}});
}

// Resolve a namespace-qualified type spelling (`Space:Dir`) to its underlying.
std::string resolveQualifiedType(parse::Tree& tree, std::string const& base,
                                 int file_id, int tok, bool& reported,
                                 diagnostic::Sink& diag,
                                 std::vector<int> const* seg_toks = nullptr,
                                 widen::TypeRef* out_handle = nullptr);

// Display name for a class/namespace member: "Owner:member", walking owner
// frames outward (a member alias `Float` of class `Space` -> "Space:Float"). A
// non-member entry (owner_ns_frame < 0) keeps its bare name. Used so a member
// type names itself qualified wherever it surfaces (e.g. ##type).
std::string memberQualifiedName(parse::Tree const& tree, int entry_id) {
    std::string name = tree.entries[entry_id].name;
    int owner = tree.entries[entry_id].owner_ns_frame;
    while (owner >= 0) {
        int oid = -1;
        for (std::size_t i = 0; i < tree.entries.size(); ++i) {
            if (tree.entries[i].ns_frame_id == owner) { oid = (int)i; break; }
        }
        // Every owner frame is owned by a kClass/kNamespace entry; a member with
        // an owner_ns_frame that names no entry is a builder invariant violation,
        // not a quietly-shortened name.
        assert(oid >= 0 && "memberQualifiedName: owner frame has no entry");
        name = tree.entries[oid].name + ":" + name;
        owner = tree.entries[oid].owner_ns_frame;
    }
    return name;
}

// Structurally resolve a DECLARED type: walk the handle, and for each named leaf
// (kSlid) that is an alias / enum-facet / qualified type, replace it with a
// transparent kAlias(name, resolved-underlying) — preserving the as-written name
// for ##type while seeing through to the underlying everywhere else. Wrappers
// (pointer/iterator/array/tuple) are rebuilt around resolved children via the
// structural constructors, so an alias leaf survives inside a composite (the
// spelling path can't do this). Child handles are copied BEFORE recursing (a
// recursive intern may realloc the arena).
// Resolve an alias template's TARGET into its PATTERN (marker leaves for the type
// parameters) once, writing it to the entry. Defined below resolveTypeRef (mutual
// recursion: an expansion may need a pattern not yet built).
widen::TypeRef ensureAliasTemplatePattern(parse::Tree& tree, int entry_id,
                                          std::set<std::string>& visiting,
                                          bool& reported, diagnostic::Sink& diag);
// The entry a template-alias use names — bare (scope-aware) or qualified
// (`Deep:RT<int>`). -1 when absent (the caller reports).
int lookupAliasTemplateEntry(parse::Tree& tree, std::string const& name,
                             int file_id, int tok, diagnostic::Sink& diag,
                             bool& reported);
void registerAliasTemplate(parse::Tree& tree, parse::Node& node);
void validateAliasTemplate(parse::Tree& tree, parse::Node& node,
                           diagnostic::Sink& diag);

widen::TypeRef resolveTypeRef(parse::Tree& tree, widen::TypeRef t,
                              std::set<std::string>& visiting, bool& reported,
                              int file_id, int tok, diagnostic::Sink& diag,
                              std::vector<int> const* seg_toks = nullptr) {
    using F = widen::Type::Form;
    switch (widen::get(t).form) {
        case F::kPointer: {
            widen::TypeRef p = widen::get(t).pointee;
            return widen::internPointer(
                resolveTypeRef(tree, p, visiting, reported, file_id, tok, diag));
        }
        case F::kIterator: {
            widen::TypeRef p = widen::get(t).pointee;
            return widen::internIterator(
                resolveTypeRef(tree, p, visiting, reported, file_id, tok, diag));
        }
        case F::kArray: {
            widen::TypeRef e = widen::get(t).elem;
            std::vector<int> dims = widen::get(t).dims;
            return widen::internArray(
                resolveTypeRef(tree, e, visiting, reported, file_id, tok, diag), dims);
        }
        case F::kTuple: {
            std::vector<widen::TypeRef> slots = widen::get(t).slots;
            for (auto& s : slots)
                s = resolveTypeRef(tree, s, visiting, reported, file_id, tok, diag);
            return widen::internTuple(slots);
        }
        case F::kConst: {
            widen::TypeRef u = widen::get(t).underlying;
            return widen::internConst(
                resolveTypeRef(tree, u, visiting, reported, file_id, tok, diag));
        }
        case F::kTmplUse: {
            // A TEMPLATE-ALIAS USE `Name<args>`: resolve the arguments in THIS
            // (the use's) scope — which is where an enclosing function template's
            // T resolves — then substitute them into the alias's pattern. The
            // expansion wraps in a transparent kAlias labeled with the use AS
            // WRITTEN, so ##type reports `Ref<int>`.
            std::string name = widen::get(t).name;
            std::string label = widen::spell(t);
            int id = lookupAliasTemplateEntry(tree, name, file_id, tok, diag,
                                              reported);
            if (id < 0) {
                if (!reported) {
                    reported = true;
                    diagnostic::report(diag, {file_id, tok,
                        "Unknown type '" + name + "'.", {}});
                }
                return t;
            }
            // A CLASS-template use: resolve the args in THIS scope, canonicalize
            // (alias/const layers dropped, so `Vec<Integer>` and `Vec<int>` share
            // one instance), instantiate (memoized), and label the result with the
            // use AS WRITTEN so ##type reports `Vec<Integer>`.
            if (tree.entries[id].kind == parse::EntryKind::kClass
                && tree.entries[id].is_template) {
                auto cti = tree.templates.find(id);
                if (cti == tree.templates.end()) return t;   // registration errored
                std::size_t nparams = cti->second.def->type_params.size();
                std::vector<widen::TypeRef> args = widen::get(t).slots;
                if (args.size() != nparams) {
                    reported = true;
                    diagnostic::report(diag, {file_id, tok,
                        "Wrong number of template arguments for '" + name + "': "
                        + std::to_string(nparams) + " expected, got "
                        + std::to_string(args.size()) + ".", {}});
                    return t;
                }
                for (auto& a : args) {
                    a = resolveTypeRef(tree, a, visiting, reported, file_id, tok,
                                       diag);
                    if (reported) return t;
                    if (!widen::isKnownType(a) && !leafIsKnownClass(tree, a)) {
                        reported = true;
                        requireKnownType(tree, a, file_id, tok, diag);
                        return t;
                    }
                    a = widen::removeConst(widen::deepStrip(a));
                }
                int iid = instantiateClassTemplate(tree, id, args, file_id, tok,
                                                   diag);
                if (iid < 0) { reported = true; return t; }
                return widen::internAlias(label, tree.entries[iid].slids_type);
            }
            if (tree.entries[id].kind == parse::EntryKind::kClass) {
                reported = true;
                diagnostic::report(diag, {file_id, tok,
                    "'" + name + "' is not a template class.", {}});
                return t;
            }
            parse::Entry const& e = tree.entries[id];
            if (e.kind != parse::EntryKind::kAlias || !e.is_template) {
                reported = true;
                diagnostic::report(diag, {file_id, tok,
                    "'" + name + "' is not a template alias.", {}});
                return t;
            }
            auto ti = tree.templates.find(id);
            if (ti == tree.templates.end()) return t;   // registration errored
            std::vector<std::string> const& params = ti->second.def->type_params;
            std::vector<widen::TypeRef> args = widen::get(t).slots;
            if (args.size() != params.size()) {
                reported = true;
                diagnostic::report(diag, {file_id, tok,
                    "Wrong number of template arguments for '" + name + "': "
                    + std::to_string(params.size()) + " expected, got "
                    + std::to_string(args.size()) + ".", {}});
                return t;
            }
            if (visiting.count(name)) {
                reported = true;
                diagnostic::report(diag, {file_id, tok,
                    "Type alias '" + name + "' is part of a cycle.", {}});
                return t;
            }
            visiting.insert(name);
            for (auto& a : args)
                a = resolveTypeRef(tree, a, visiting, reported, file_id, tok, diag);
            widen::TypeRef pat =
                ensureAliasTemplatePattern(tree, id, visiting, reported, diag);
            visiting.erase(name);
            if (pat == widen::kNoType) return t;
            return widen::internAlias(label,
                widen::substituteTypeParams(pat, params, args));
        }
        case F::kSlid: {
            // A template TYPE-PARAMETER marker is terminal — it is not a scope name
            // (the alias registered for `T` targets it; resolving it by name again
            // would read as an alias cycle).
            if (widen::get(t).def_id == widen::kTmplParamDefId) return t;
            std::string name = widen::get(t).name;
            if (name.find(':') != std::string::npos) {   // qualified (Space:Dir)
                widen::TypeRef handle = widen::kNoType;
                resolveQualifiedType(tree, name, file_id, tok, reported, diag,
                                     seg_toks, &handle);
                if (handle == widen::kNoType) return t;   // error already reported
                // Use the HANDLE directly — never re-intern the spelling (a class's
                // def_id can't survive a spelling round-trip).
                return widen::internAlias(name, handle);
            }
            // SCOPE-AWARE: resolveName (open-ns chain + lexical-with-owner<0), not a
            // frame-blind any-live-entry lookup. A namespaced member type (a host
            // class's alias/enum, a hoisted class) resolves bare ONLY where its
            // frame is open — inside the host (member types + bodies open it), so
            // `Inner`/`Innerger` at file scope are out of scope and fail.
            int id = resolveName(tree, name);
            // A class name resolves to its registered kSlid handle. The handle
            // carries the def_id (scope distinction) and the layout; the bare
            // written-name kSlid `t` is just a placeholder. Always redirect so
            // every reference — file-scope or local — shares the one real handle.
            if (id >= 0 && tree.entries[id].kind == parse::EntryKind::kClass) {
                // A class TEMPLATE used BARE. Inside its own instantiation the
                // bare name means THE INSTANCE (the receiver `Vec^`, a
                // self-typed member); anywhere else the type-list is required.
                if (tree.entries[id].is_template) {
                    for (auto it = tree.tmpl_self_stack.rbegin();
                         it != tree.tmpl_self_stack.rend(); ++it) {
                        if (it->tmpl_entry == id)
                            return tree.entries[it->instance_entry].slids_type;
                    }
                    reported = true;
                    diagnostic::report(diag, {file_id, tok,
                        "Class template '" + name
                        + "' requires a type-argument list.", {}});
                    return t;
                }
                return tree.entries[id].slids_type;
            }
            // A SUB-PATTERN's own bare name inside its instantiation (the
            // receiver `Inner^`, a self-typed member): its entry is registered
            // under the QUALIFIED spelling, so resolveName misses it — match
            // the redirect by the pattern's spelled name instead.
            if (id < 0) {
                for (auto it = tree.tmpl_self_stack.rbegin();
                     it != tree.tmpl_self_stack.rend(); ++it) {
                    auto sti = tree.templates.find(it->tmpl_entry);
                    if (sti != tree.templates.end() && sti->second.def
                        && sti->second.def->name == name)
                        return tree.entries[it->instance_entry].slids_type;
                }
            }
            bool is_alias = id >= 0 && tree.entries[id].kind == parse::EntryKind::kAlias;
            bool is_enum  = id >= 0
                && tree.entries[id].kind == parse::EntryKind::kNamespace
                && tree.entries[id].slids_type != widen::kNoType;
            if (!is_alias && !is_enum) return t;   // unknown name / real slid — leave
            // A TEMPLATE alias used BARE — its entry would expand to the raw
            // pattern (marker leaves). The use must supply arguments.
            if (is_alias && tree.entries[id].is_template) {
                reported = true;
                diagnostic::report(diag, {file_id, tok,
                    "Template alias '" + name + "' needs a type-argument list.", {}});
                return t;
            }
            if (visiting.count(name)) {
                reported = true;
                diagnostic::report(diag, {file_id, tok,
                    "Type alias '" + name + "' is part of a cycle.", {}});
                return t;
            }
            visiting.insert(name);
            widen::TypeRef target = tree.entries[id].slids_type;
            widen::TypeRef u =
                resolveTypeRef(tree, target, visiting, reported, file_id, tok, diag);
            visiting.erase(name);
            // A class/namespace member alias names itself qualified ("Space:Float")
            // wherever it surfaces, even when written bare inside its own scope.
            std::string disp = tree.entries[id].owner_ns_frame >= 0
                ? memberQualifiedName(tree, id) : name;
            return widen::internAlias(disp, u);
        }
        case F::kPrimitive:
        case F::kVoid:
        case F::kAnyptr:
        case F::kAlias:
        case F::kNone:
            return t;   // already resolved / nothing to resolve
    }
    return t;
}

// True if `file_id` names an imported `.slh` header (grammar filled file_imported
// from the token list). A function DECLARED in such a file is external: its
// definition lives in another translation unit, so it is not an orphan here.
bool fileIsImported(parse::Tree const& tree, int file_id) {
    return file_id >= 0 && file_id < (int)tree.file_imported.size()
        && tree.file_imported[file_id];
}

// True if `file_id` is a TEMPLATE SOURCE loaded beside its imported header —
// only its template content participates in this TU (run() strips the rest);
// the template decl/def merge accepts a definition from it.
bool fileIsTemplateSource(parse::Tree const& tree, int file_id) {
    return file_id >= 0 && file_id < (int)tree.file_template_source.size()
        && tree.file_template_source[file_id];
}

// True if `file_id` names THIS TU's sibling header — an imported `.slh` whose base name
// matches the primary source's (`library.slh` <-> `library.sl`; grammar filled
// file_sibling). This TU is that header's implementation, so it is the ONE TU that emits
// its classes' SYNTHESIZED symbols; every other importer declares them.
bool fileIsSibling(parse::Tree const& tree, int file_id) {
    return file_id >= 0 && file_id < (int)tree.file_sibling.size()
        && tree.file_sibling[file_id];
}

// Resolve a declared type IN PLACE to its structured form (alias leaves become
// transparent kAlias), then require the result to be a known type. A cycle was
// already reported, so skip the redundant "Unknown type" the broken chain emits.
void resolveDeclType(parse::Tree& tree, widen::TypeRef& type_ref,
                     int file_id, int tok, diagnostic::Sink& diag,
                     std::vector<int> const* seg_toks = nullptr) {
    std::set<std::string> visiting;
    bool reported = false;
    type_ref = resolveTypeRef(tree, type_ref, visiting, reported,
                              file_id, tok, diag, seg_toks);
    if (!reported) requireKnownType(tree, type_ref, file_id, tok, diag);
}

// Resolve a call node's explicit template type-list in the current scope. Shared
// by the free-function call path and both method-call arms (a method's callee —
// and its arity — bind later, at classify, off the receiver's class).
void resolveTmplArgs(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    for (std::size_t i = 0; i < s.tmpl_args.size(); i++) {
        int t_tok = i < s.tmpl_arg_toks.size() ? s.tmpl_arg_toks[i] : s.tok;
        resolveDeclType(tree, s.tmpl_args[i], s.file_id, t_tok, diag);
    }
}

// Any same-scope TEMPLATE function entry of this name — a member scope when
// `owner` >= 0, else file scope. The collision rule's search, shared by the
// file-scope function pass and the scope-member registration.
int findSameScopeTemplate(parse::Tree const& tree, std::string const& name,
                          int owner) {
    for (std::size_t idx = 0; idx < tree.entries.size(); idx++) {
        parse::Entry const& pe = tree.entries[idx];
        if (pe.kind != parse::EntryKind::kFunction || !pe.is_template
            || pe.name != name) continue;
        if (owner >= 0 ? pe.owner_ns_frame == owner
                       : (pe.owner_ns_frame < 0 && pe.parent_frame_id == 0))
            return (int)idx;
    }
    return -1;
}

// The ONE place an entry's KIND is named for a diagnostic ("Cannot assign to <noun>
// 'x'"). A named enum is a kNamespace whose slids_type carries the underlying type; a
// plain namespace's is kNoType — so an enum reads "enum", not "namespace". Shared by
// registerDeclarator (reuse-reject), resolveAssignTarget, and resolveCallTarget.
void resolveTemplatePatterns(parse::Tree& tree, parse::Node& node, int entry_id,
                             diagnostic::Sink& diag);
void snapshotTemplate(parse::Tree& tree, parse::Node& node);

char const* entryKindNoun(parse::Entry const& e) {
    switch (e.kind) {
        case parse::EntryKind::kAlias:     return "type";
        case parse::EntryKind::kConst:     return "constant";
        case parse::EntryKind::kFunction:  return "function";
        case parse::EntryKind::kClass:     return "class";
        case parse::EntryKind::kNamespace:
            return e.slids_type != widen::kNoType ? "enum" : "namespace";
        case parse::EntryKind::kLocalVar:
        case parse::EntryKind::kGlobalVar: return "variable";
        case parse::EntryKind::kField:     return "field";
    }
    return "variable";
}

// The entry a declarator registers: name + resolved type + kind + flags.
struct DeclInfo {
    std::string name;
    int file_id = 0;
    int name_tok = 0;
    widen::TypeRef type = widen::kNoType;   // kNoType -> classify/constfold infers later
    parse::EntryKind kind = parse::EntryKind::kLocalVar;
    std::string alias_label;                // ##type label when a named type erased
    bool track_body_local = true;           // add to the unused-local sweep list
};
enum class BindMode { Declare, DeclareOrReuse };

// THE funnel every in-body declarator routes through — one place owns the declare-vs-reuse
// decision, the dup-check + diagnostic, the addEntry, and the body-local tracking, so the
// logic cannot drift between sites.
//   mode DeclareOrReuse : a TYPELESS binding. If the name already resolves in scope it is a
//        REUSE (assignment semantics): an assignable variable is reused (sets *reused); a
//        non-assignable target (const / fn / class / enum / namespace / type) is REJECTED
//        exactly as an assignment to it would be — one rule for `x = e`, `for (x ...)`, and
//        destructure slots. Unresolved -> a fresh declaration.
//        Declare : always a fresh declaration (typed slots, plain / typed decls).
// Conflict is by NORMAL lexical scoping: only a SAME-FRAME entry collides ("Duplicate
// declaration"); an enclosing entry is a legal shadow. A TYPED decl may shadow an enclosing
// const / fn / class; a TYPELESS binding reuses/rejects it per the mode above.
// Returns the entry id (reused or fresh) or -1 on a reuse-reject / conflict error.
int registerDeclarator(parse::Tree& tree, DeclInfo const& d, BindMode mode,
                       bool& reused, diagnostic::Sink& diag) {
    reused = false;
    if (mode == BindMode::DeclareOrReuse) {
        int existing = resolveName(tree, d.name);
        if (existing >= 0) {
            parse::Entry const& prev = tree.entries[existing];
            if (prev.kind == parse::EntryKind::kLocalVar
                || prev.kind == parse::EntryKind::kGlobalVar) {
                reused = true;   // reuse an assignable variable (a store)
                return existing;
            }
            // A typeless binding to a non-assignable name is the same error an
            // assignment to it would raise (uses the shared entryKindNoun wording).
            std::string what = entryKindNoun(prev);
            diagnostic::report(diag, {d.file_id, d.name_tok,
                "Cannot assign to " + what + " '" + d.name + "'.",
                {{prev.file_id, prev.tok, what + " declared here"}}});
            return -1;
        }
    }
    int conflict = parse::findInFrame(tree, parse::currentFrameId(tree), d.name);
    if (conflict >= 0) {
        parse::Entry const& prev = tree.entries[conflict];
        diagnostic::report(diag, {d.file_id, d.name_tok,
            "Duplicate declaration of '" + d.name + "'.",
            {{prev.file_id, prev.tok, "first declared here"}}});
        return -1;
    }
    parse::Entry e;
    e.kind = d.kind;
    e.name = d.name;
    e.slids_type = d.type;
    if (!d.alias_label.empty()) e.alias_label = d.alias_label;
    e.file_id = d.file_id;
    e.tok = d.name_tok;
    int id = parse::addEntry(tree, std::move(e));
    if (d.track_body_local) tree.body_locals.push_back(id);
    return id;
}

// Register `alias Name = Type;` as a kAlias entry in the current frame. An alias
// TEMPLATE (`alias Ref<T> = T^;`) additionally records its def in tree.templates;
// its target stays raw until the pattern builds.
void registerAlias(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    bool reused;
    DeclInfo d;
    d.name = s.name;
    d.file_id = s.file_id;
    d.name_tok = s.name_tok;
    d.type = s.return_type;              // target spelling; resolved at use
    d.kind = parse::EntryKind::kAlias;
    d.track_body_local = false;
    s.resolved_entry_id = registerDeclarator(tree, d, BindMode::Declare, reused, diag);
    if (!s.type_params.empty()) registerAliasTemplate(tree, s);
}

// Collect every name declared as a function anywhere in the tree (scope-blind).
void collectFunctionNames(parse::Node const& n, std::set<std::string>& out) {
    if (n.kind == parse::Kind::kFunctionDef || n.kind == parse::Kind::kFunctionDecl)
        out.insert(n.name);
    for (auto const& ch : n.children)
        if (ch) collectFunctionNames(*ch, out);
}

// A kAliasDecl whose target names a FUNCTION is a FUNCTION alias (`alias sin = sinf`),
// not a type alias — it never mints a (colliding) kAlias entry; instead the target's
// overloads are duplicated under the alias name after types resolve (processFuncAliases).
bool isFuncAlias(parse::Tree const& tree, parse::Node const& s) {
    return s.kind == parse::Kind::kAliasDecl
        && s.return_type != widen::kNoType
        && tree.all_function_names.count(widen::spellOrEmpty(s.return_type)) > 0;
}

void recordFuncAlias(parse::Tree& tree, parse::Node const& s, int frame) {
    tree.func_alias_reqs.push_back(
        {s.name, widen::spellOrEmpty(s.return_type), frame, s.file_id, s.name_tok});
}

// LATE (post-type-resolution) — duplicate each function alias's target overloads under
// the alias name as kFunction entries carrying alias_of (so they emit the TARGET's
// symbol) with the target's now-resolved signature. Additive; deduped by signature.
void processFuncAliases(parse::Tree& tree, diagnostic::Sink& diag) {
    for (auto const& req : tree.func_alias_reqs) {
        // A namespace/class member is owned by its frame; a FILE-scope function is
        // owner_ns_frame < 0 with parent_frame_id == the global frame.
        auto inScope = [&](parse::Entry const& e) {
            if (req.frame == parse::kGlobalFrame)
                return e.owner_ns_frame < 0 && e.parent_frame_id == parse::kGlobalFrame;
            return e.owner_ns_frame == req.frame;
        };
        std::vector<int> targets;
        for (std::size_t id = 0; id < tree.entries.size(); id++) {
            parse::Entry const& e = tree.entries[id];
            if (e.kind == parse::EntryKind::kFunction && inScope(e)
                && e.name == req.target && e.alias_of < 0
                && !e.is_template && e.tmpl_args.empty())
                targets.push_back((int)id);
        }
        if (targets.empty()) {
            diagnostic::report(diag, {req.file_id, req.tok,
                "Alias target '" + req.target + "' is not a function in this scope.", {}});
            continue;
        }
        for (int tid : targets) {
            bool dup = false;
            for (std::size_t id = 0; id < tree.entries.size(); id++) {
                parse::Entry const& q = tree.entries[id];
                if (q.kind == parse::EntryKind::kFunction && inScope(q)
                    && q.name == req.name
                    && q.param_types == tree.entries[tid].param_types) { dup = true; break; }
            }
            if (dup) {
                diagnostic::report(diag, {req.file_id, req.tok,
                    "Alias '" + req.name + "' has the same signature as an existing "
                    "overload of '" + req.name + "'.", {}});
                continue;
            }
            parse::Entry e = tree.entries[tid];   // copy the target's resolved signature
            e.name = req.name;
            e.alias_of = tid;
            e.file_id = req.file_id;
            e.tok = req.tok;
            e.def_file_id = -1;
            e.def_tok = -1;
            parse::addEntry(tree, std::move(e));
        }
    }
}

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag,
                 bool unevaluated = false);
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);
void resolveStoreTarget(parse::Tree& tree, parse::Node& lv,
                        diagnostic::Sink& diag);
void markLvalueBaseRead(parse::Tree& tree, parse::Node& lv);

// ---- Namespace lookup --------------------------------------------------------
// The global root is namespace frame 0 (the program frame). Its members are the
// file-scope entries (parent_frame_id == 0, not themselves namespace members).
using parse::kGlobalFrame;
// Member-by-frame lookup lives in parse (shared with classify); use it unqualified.
using parse::findMemberDeclared;

int findMemberLive(parse::Tree const& tree, int ns_frame,
                   std::string const& name);

// Bare-name resolution. Qualifiers are always optional, so a bare name resolves
// against (1) the open-namespace chain, innermost first — this is the current
// member's siblings and its enclosing namespaces, plus any `alias Ns;` imports;
// then (2) the ordinary lexical scope (locals, params, file-scope entries),
// innermost first. (1) before (2) means a sibling member shadows an outer name;
// reaching the shadowed outer needs `::`. Returns entry id or -1.
// Like resolveName, but reports an open-namespace-import collision: if the name
// matches distinct members in two different open namespaces (e.g. `alias Bonk1;
// alias Bonk2;` where both define `kOops`), `other` is set to the second match
// so the caller can emit an ambiguity diagnostic. Returns the first match (or
// the lexical match if no namespace match). `other` is -1 when unambiguous.
int resolveNameDetail(parse::Tree const& tree, std::string const& name,
                      int& other) {
    other = -1;
    int first = -1;
    for (auto it = tree.open_ns_frames.rbegin();
         it != tree.open_ns_frames.rend(); ++it) {
        int id = findMemberLive(tree, *it, name);
        if (id < 0) continue;
        if (first < 0) {
            first = id;
        } else if (id != first) {
            other = id;
            return first;
        }
    }
    // A class MEMBER (method / member const) is shadowed by a body LOCAL of the
    // same name: a method body is lexically inside the class, so its params,
    // locals, and nested functions take precedence (standard scoping). A bare
    // NAMESPACE member still beats file scope, and only a true body-local (not a
    // file-scope function) shadows a class member.
    bool member_is_class = first >= 0
        && tree.entries[first].owner_ns_frame >= 0
        && parse::classEntryForFrame(tree, tree.entries[first].owner_ns_frame) >= 0;
    if (first >= 0 && !member_is_class) return first;
    for (auto it = tree.live_entry_ids.rbegin();
         it != tree.live_entry_ids.rend(); ++it) {
        parse::Entry const& e = tree.entries[*it];
        if (e.name != name || e.owner_ns_frame >= 0) continue;
        if (member_is_class && e.parent_frame_id == kGlobalFrame) continue;
        return *it;
    }
    return first;
}

int resolveName(parse::Tree const& tree, std::string const& name) {
    int other;
    return resolveNameDetail(tree, name, other);
}

// An unknown name hit while resolving an INLINE-LOCAL template instance's
// body, where the name is a TEMPLATE SOURCE's private (stripped at load):
// upgrade the generic unknown-name error to the full cross-TU chain — the
// use site, the local class, the template, the reference, the private
// definition, and the remedy. True = handled; the caller emits nothing.
// `verb` reads as "its body <verb> 'name' here" — "calls" / "references".
bool reportPrivateNameInInline(parse::Tree& tree, std::string const& name,
                               int ref_file, int ref_tok, char const* verb,
                               diagnostic::Sink& diag) {
    if (tree.inline_inst_ctx.empty()) return false;
    auto sp = tree.stripped_privates.find(name);
    if (sp == tree.stripped_privates.end()) return false;
    parse::Tree::InlineInstCtx const& ctx = tree.inline_inst_ctx.back();
    if (ctx.tmpl_entry < 0) return false;
    std::string tname = tree.entries[ctx.tmpl_entry].name;
    std::string lname = "?";
    int lfile = -1, ltok = -1;
    auto ci = tree.classes.find(ctx.local_arg);
    if (ci != tree.classes.end()) {
        lname = ci->second.name;
        lfile = ci->second.def_file_id;
        ltok = ci->second.def_tok;
    }
    diagnostic::report(diag, {ctx.use_file, ctx.use_tok,
        "Cannot instantiate '" + tname + "' with the local type '" + lname
        + "': the template uses a name private to its source; "
        "this would fail at link.",
        {{lfile, ltok, "local class '" + lname + "' declared here"},
         {tree.entries[ctx.tmpl_entry].file_id, tree.entries[ctx.tmpl_entry].tok,
          "'" + tname + "' declared here"},
         {ref_file, ref_tok,
          "its body " + std::string(verb) + " '" + name + "' here"},
         {sp->second.file_id, sp->second.name_tok,
          "'" + name + "' is defined here, private to the template's source. "
          "note: declare '" + name + "' or class '" + lname
          + "' in a header file to make them public."}}});
    return true;
}

// Does any entry exist as a namespace member of the given name? Used to choose
// "needs a qualifier" over "unresolved" when a bare lookup fails.
bool namespaceMemberExists(parse::Tree const& tree, std::string const& name) {
    for (parse::Entry const& e : tree.entries) {
        if (e.owner_ns_frame >= 0 && e.name == name) return true;
    }
    return false;
}

// Find a member `name` in namespace frame `ns_frame` among LIVE entries (so a
// member that has dropped out of scope is not found). Returns entry id or -1.
int findMemberLive(parse::Tree const& tree, int ns_frame,
                   std::string const& name) {
    for (int id : tree.live_entry_ids) {
        parse::Entry const& e = tree.entries[id];
        if (e.name != name) continue;
        if (ns_frame == kGlobalFrame) {
            if (e.parent_frame_id == kGlobalFrame && e.owner_ns_frame < 0) return id;
        } else if (e.owner_ns_frame == ns_frame) {
            return id;
        }
    }
    return -1;
}


// Human-readable form of a qualifier chain (e.g. "Space:Nested", or "::" for a
// bare global qualifier) for the leaf "has no member" diagnostic.
std::string qualPrefixText(std::vector<std::string> const& segments, bool global,
                           std::size_t count) {
    std::string s;
    for (std::size_t i = 0; i < count; ++i) {
        if (!s.empty()) s += ":";
        s += segments[i];
    }
    if (s.empty()) return global ? "::" : "";
    return s;
}

// The namespace/class frame a segment entry names, or -1 if the entry is not a
// frame root. A namespace and a class both expose their member set via
// ns_frame_id; a type-alias to a class (or namespace/enum) sees through to that
// frame (so `alias Time = Space; Time:Count` resolves).
int entryNamespaceFrame(parse::Tree const& tree, int id) {
    if (id < 0) return -1;
    parse::Entry const& e = tree.entries[id];
    if (e.kind == parse::EntryKind::kNamespace
        || e.kind == parse::EntryKind::kClass) {
        return e.ns_frame_id;
    }
    if (e.kind == parse::EntryKind::kAlias) {
        widen::TypeRef leaf = widen::deepStrip(e.slids_type);
        if (widen::form(leaf) == widen::Type::Form::kSlid) {
            return entryNamespaceFrame(tree, resolveName(tree, widen::get(leaf).name));
        }
    }
    return -1;
}

// Walk a chain of namespace segments to the frame it names. `segments`/`toks`
// are parallel; each segment must resolve to a namespace or class. The caret
// lands on the offending segment's token, and the message names the chain
// resolved so far — the SOLE chain walker, shared by qualified refs, inline
// member decls, and bare aliases, so all three word identically. Returns -1 on a
// diagnosed error.
int resolveNamespaceSegments(parse::Tree const& tree,
                             std::vector<std::string> const& segments,
                             std::vector<int> const& toks, bool global,
                             int file_id, diagnostic::Sink& diag) {
    int cur;
    std::size_t i;
    if (global) {
        cur = kGlobalFrame;
        i = 0;
    } else {
        int frame = entryNamespaceFrame(tree, resolveName(tree, segments[0]));
        if (frame < 0) {
            diagnostic::report(diag, {file_id, toks[0],
                "'" + segments[0] + "' is not a namespace.", {}});
            return -1;
        }
        cur = frame;
        i = 1;
    }
    for (; i < segments.size(); ++i) {
        int frame = entryNamespaceFrame(tree, findMemberLive(tree, cur, segments[i]));
        if (frame < 0) {
            diagnostic::report(diag, {file_id, toks[i],
                "'" + segments[i] + "' is not a namespace member of '"
                + qualPrefixText(segments, global, i) + "'.", {}});
            return -1;
        }
        cur = frame;
    }
    return cur;
}

// Resolve a qualified reference (ident or call) to its target entry id, or -1
// on a diagnosed error. The qualifier names the namespace; node.name is the
// member looked up within it.
int resolveQualifiedRef(parse::Tree& tree, parse::Node& node,
                        diagnostic::Sink& diag) {
    int frame = resolveNamespaceSegments(tree, node.qualifier, node.qualifier_toks,
                                         node.global_qualified, node.file_id, diag);
    if (frame < 0) return -1;
    int id = findMemberLive(tree, frame, node.name);
    if (id >= 0) return id;
    if (findMemberDeclared(tree, frame, node.name) >= 0) {
        diagnostic::report(diag, {node.file_id, node.name_tok,
            "'" + node.name + "' is not visible from this scope.", {}});
        return -1;
    }
    std::string prefix = qualPrefixText(node.qualifier, node.global_qualified,
                                        node.qualifier.size());
    diagnostic::report(diag, {node.file_id, node.name_tok,
        "'" + prefix + "' has no member '" + node.name + "'.", {}});
    return -1;
}

bool isQualified(parse::Node const& n) {
    return n.global_qualified || !n.qualifier.empty();
}

// Resolve a namespace-qualified type spelling (`Space:Dir`, `::A:B:Type`) to its
// underlying type. The leading segments name a namespace path; the final segment
// must be a type living there — today an enum (a kNamespace carrying a non-empty
// slids_type underlying). Returns the underlying, or "" with `reported` set after
// a diagnostic. When `seg_toks` is supplied (parse captured per-segment tokens)
// each segment's caret lands on its own token; otherwise every segment falls back
// to `tok` (the decl's type position), since a type spelling carries no tokens.
std::string resolveQualifiedType(parse::Tree& tree, std::string const& base,
                                 int file_id, int tok, bool& reported,
                                 diagnostic::Sink& diag,
                                 std::vector<int> const* seg_toks,
                                 widen::TypeRef* out_handle) {
    std::vector<std::string> segs;
    bool global = false;
    std::size_t i = 0;
    if (base.size() >= 2 && base[0] == ':' && base[1] == ':') {
        global = true;
        i = 2;
    }
    std::string cur;
    for (; i < base.size(); ++i) {
        if (base[i] == ':') { segs.push_back(cur); cur.clear(); }
        else cur.push_back(base[i]);
    }
    segs.push_back(cur);
    // Use the real per-segment tokens when parse captured them (and they align
    // with the segments); otherwise point every segment at the flat decl token.
    std::vector<int> toks =
        (seg_toks && seg_toks->size() == segs.size())
            ? *seg_toks
            : std::vector<int>(segs.size(), tok);
    // Walk all but the last segment as a namespace path; look the last up there.
    std::vector<std::string> path(segs.begin(), segs.end() - 1);
    std::vector<int> ptoks(toks.begin(), toks.end() - 1);
    int frame = resolveNamespaceSegments(tree, path, ptoks, global, file_id, diag);
    if (frame < 0) { reported = true; return ""; }
    int id = findMemberLive(tree, frame, segs.back());
    // A member type is an enum facet (a kNamespace carrying a non-empty underlying),
    // a member type-alias (`Space:Float`), or a hoisted CLASS (`Outer:Inner`). The
    // caller re-wraps with the written qualified name as label. CRITICAL: a class's
    // identity rides its def_id, which a spelling cannot carry — so out_handle
    // returns the real underlying HANDLE; the caller MUST use it (never re-intern
    // the returned spelling, which would lose the def_id).
    bool is_enum_facet = id >= 0
        && tree.entries[id].kind == parse::EntryKind::kNamespace
        && tree.entries[id].slids_type != widen::kNoType;
    bool is_member_alias = id >= 0
        && tree.entries[id].kind == parse::EntryKind::kAlias;
    // A member TEMPLATE alias used BARE — its slids_type is the marker PATTERN,
    // which must never leak through the plain-alias path. The use needs arguments
    // (the argumented form routes through resolveTypeRef's kTmplUse arm instead).
    if (is_member_alias && tree.entries[id].is_template) {
        diagnostic::report(diag, {file_id, toks.back(),
            "Template alias '" + base + "' needs a type-argument list.", {}});
        reported = true;
        return "";
    }
    bool is_member_class = id >= 0
        && tree.entries[id].kind == parse::EntryKind::kClass;
    if (!is_enum_facet && !is_member_alias && !is_member_class) {
        diagnostic::report(diag, {file_id, toks.back(),
            "'" + segs.back() + "' is not a type in '"
            + qualPrefixText(segs, global, segs.size() - 1) + "'.", {}});
        reported = true;
        return "";
    }
    widen::TypeRef under = widen::deepStrip(tree.entries[id].slids_type);
    if (out_handle) *out_handle = under;
    return widen::spell(under);
}

int lookupAliasTemplateEntry(parse::Tree& tree, std::string const& name,
                             int file_id, int tok, diagnostic::Sink& diag,
                             bool& reported) {
    if (name.find(':') == std::string::npos) return resolveName(tree, name);
    // A nested class template's SUB-PATTERN is registered under the qualified
    // spelling itself ("Outer:Inner") — the whole name is the entry name, so
    // try it before the namespace walk (which would reject the outer template:
    // a pattern owns no frame).
    if (int sub = resolveName(tree, name);
        sub >= 0 && tree.entries[sub].kind == parse::EntryKind::kClass
        && tree.entries[sub].is_template) {
        return sub;
    }
    std::vector<std::string> segs;
    bool global = false;
    std::size_t i = 0;
    if (name.size() >= 2 && name[0] == ':' && name[1] == ':') { global = true; i = 2; }
    std::string cur;
    for (; i < name.size(); ++i) {
        if (name[i] == ':') { segs.push_back(cur); cur.clear(); }
        else cur.push_back(name[i]);
    }
    segs.push_back(cur);
    std::vector<std::string> path(segs.begin(), segs.end() - 1);
    std::vector<int> ptoks(path.size(), tok);
    int frame = resolveNamespaceSegments(tree, path, ptoks, global, file_id, diag);
    if (frame < 0) { reported = true; return -1; }
    return findMemberLive(tree, frame, segs.back());
}

// Bind each template type parameter as an alias to its kTmplParamDefId marker
// leaf in the CURRENT (caller-pushed) frame. Shared by function-template and
// alias-template pattern building.
void bindTypeParamMarkers(parse::Tree& tree, parse::Node const& node) {
    for (std::size_t i = 0; i < node.type_params.size(); i++) {
        parse::Entry a;
        a.kind = parse::EntryKind::kAlias;
        a.name = node.type_params[i];
        a.slids_type = widen::internSlid(node.type_params[i], {},
                                         widen::kTmplParamDefId);
        a.file_id = node.file_id;
        a.tok = i < node.type_param_toks.size() ? node.type_param_toks[i]
                                                : node.name_tok;
        parse::addEntry(tree, std::move(a));
    }
}

widen::TypeRef ensureAliasTemplatePattern(parse::Tree& tree, int entry_id,
                                          std::set<std::string>& visiting,
                                          bool& reported, diagnostic::Sink& diag) {
    auto it = tree.templates.find(entry_id);
    if (it == tree.templates.end()) return widen::kNoType;
    parse::TemplateInfo& ti = it->second;
    if (ti.pattern_built) return tree.entries[entry_id].slids_type;
    ti.pattern_built = true;   // set BEFORE resolving: a self-reference re-enters
                               // and must not rebuild (the cycle check reports it)
    parse::Node& node = *ti.def;
    // NEVER hold an Entry& across this build — bindTypeParamMarkers and the
    // recursive resolution addEntry, and tree.entries may REALLOCATE (the entry
    // is re-indexed at each touch instead; this was a dangling-write bug).
    std::string alias_name = tree.entries[entry_id].name;
    // The alias's own name joins the cycle set for the build, so a self-
    // referential target (`alias Cyc<T> = Cyc<T>^;`) reports the CYCLE, not an
    // unknown-type fallout. (Erased only if inserted here — a use-site arm may
    // already hold it.)
    bool inserted = visiting.insert(alias_name).second;
    parse::pushFrame(tree);
    bindTypeParamMarkers(tree, node);
    widen::TypeRef pat = resolveTypeRef(tree, node.return_type, visiting, reported,
                                        node.file_id, node.name_tok, diag);
    parse::popFrame(tree);
    if (inserted) visiting.erase(alias_name);
    if (!reported) requireKnownType(tree, pat, node.file_id, node.name_tok, diag);
    tree.entries[entry_id].slids_type = pat;
    return pat;
}

// Mark an alias decl carrying a template-list as an ALIAS TEMPLATE: flag its
// entry, record the def node in tree.templates. The target stays raw on the
// entry until ensureAliasTemplatePattern builds the pattern (lazily at the first
// use, or at the scope's validate point — whichever comes first).
void registerAliasTemplate(parse::Tree& tree, parse::Node& node) {
    if (node.resolved_entry_id < 0) return;
    tree.entries[node.resolved_entry_id].is_template = true;
    parse::TemplateInfo ti;
    ti.def = &node;
    tree.templates[node.resolved_entry_id] = std::move(ti);
}

// The validate-point wrapper: build (and thereby check) the pattern now.
void validateAliasTemplate(parse::Tree& tree, parse::Node& node,
                           diagnostic::Sink& diag) {
    if (node.resolved_entry_id < 0) return;
    std::set<std::string> visiting;
    bool reported = false;
    ensureAliasTemplatePattern(tree, node.resolved_entry_id, visiting, reported,
                               diag);
}

// ---- Namespace registration -------------------------------------------------
void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag, bool nested = false);
void registerLocalClasses(parse::Tree& tree,
                          std::vector<std::unique_ptr<parse::Node>>& stmts,
                          diagnostic::Sink& diag);
void registerScopeNames(parse::Tree& tree, parse::Node& node, int frame,
                        std::vector<parse::Node*>& classes, diagnostic::Sink& diag);
void registerClassBody(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag);
void registerClassTemplate(parse::Tree& tree, parse::Node& node,
                           std::vector<std::unique_ptr<parse::Node>>* host_list,
                           int owner, diagnostic::Sink& diag);
void snapshotTemplate(parse::Tree& tree, parse::Node& node);
void checkClassCyclesAndNeeds(parse::Tree& tree, std::vector<parse::Node*> const& classes,
                              diagnostic::Sink& diag);
void resolveScopeBodies(parse::Tree& tree, parse::Node& node, bool isClass,
                        diagnostic::Sink& diag);
void resolveScopeTypes(parse::Tree& tree, parse::Node& node, bool isClass,
                       diagnostic::Sink& diag);
void registerEnum(parse::Tree& tree, parse::Node& node, int parent_ns,
                  diagnostic::Sink& diag);
void resolveEnumMemberInits(parse::Tree& tree, parse::Node& node,
                            diagnostic::Sink& diag);

// Report any local in `body_locals` never read: "Unused local variable 'x'." if
// it was never written (a1 — declared and ignored), else "Local variable 'x'
// set but never used." (a2 — a value was computed and discarded). Caret at the
// decl. Gated by the caller on hasErrors so a value-before-init / dup diagnostic
// isn't trailed by spurious unused reports. Shared by the per-function and
// per-block scope exits.
// A type whose ctor/dtor hooks run on construction/destruction — a hook-bearing
// class, OR an array / tuple that CONTAINS one (the elements' hooks fire). Such a
// value is USED by its mere existence, so the unused-local sweep must skip it.
bool typeHasHooks(widen::TypeRef t) {
    using F = widen::Type::Form;
    widen::TypeRef s = widen::strip(t);
    F f = widen::form(s);
    if (f == F::kSlid)  return widen::get(s).needs_ctor || widen::get(s).needs_dtor;
    if (f == F::kArray) return typeHasHooks(widen::get(s).elem);
    if (f == F::kTuple) {
        for (widen::TypeRef slot : widen::get(s).slots)
            if (typeHasHooks(slot)) return true;
    }
    return false;
}

void sweepUnusedLocals(parse::Tree& tree, diagnostic::Sink& diag) {
    if (diagnostic::hasErrors(diag)) return;
    for (int id : tree.body_locals) {
        if (tree.read_locals.count(id) > 0) continue;
        parse::Entry const& e = tree.entries[id];
        // A class instance with a constructor / destructor is USED by its mere
        // existence — the hooks run at construction / scope exit (the demo
        // `{ CtorDtor cd1(1); ... }` is exactly this); likewise an array / tuple
        // of such a class. Don't flag it as unused.
        if (typeHasHooks(e.slids_type)) continue;
        bool was_set = tree.initialized_locals.count(id) > 0
                    || tree.assigned_arrays.count(id) > 0;
        diagnostic::report(diag, {e.file_id, e.tok,
            was_set ? "Local variable '" + e.name + "' set but never used."
                    : "Unused local variable '" + e.name + "'.", {}});
    }
}

// Find an existing namespace named `name` to reopen. For a top-level namespace
// (parent_ns == global) this is a lexical lookup so a locally-opened namespace
// reopens correctly; for a nested namespace it is a member lookup in the parent.
// Returns its ns_frame_id, or -1 if none exists (caller creates one). Sets
// `collision` if a non-namespace of that name blocks the open.
int findExistingNamespace(parse::Tree const& tree, std::string const& name,
                          int parent_ns, bool& collision) {
    collision = false;
    int id;
    if (parent_ns == kGlobalFrame) {
        id = -1;
        for (auto it = tree.live_entry_ids.rbegin();
             it != tree.live_entry_ids.rend(); ++it) {
            parse::Entry const& e = tree.entries[*it];
            if (e.name == name && e.owner_ns_frame < 0) { id = *it; break; }
        }
    } else {
        id = findMemberDeclared(tree, parent_ns, name);
    }
    if (id < 0) return -1;
    if (tree.entries[id].kind != parse::EntryKind::kNamespace) {
        collision = true;
        return -1;
    }
    return tree.entries[id].ns_frame_id;
}

// Open (or reopen) the namespace `name` whose parent namespace is `parent_ns`.
// Returns its ns_frame_id. A fresh open allocates an id and adds a kNamespace
// name entry at the current lexical frame (so a locally-opened namespace pops
// with its scope). Members added under the returned frame inherit the current
// lexical lifetime.
int openNamespace(parse::Tree& tree, std::string const& name, int name_tok,
                  int file_id, int parent_ns, diagnostic::Sink& diag,
                  std::string const& enum_underlying = "") {
    bool collision = false;
    int existing = findExistingNamespace(tree, name, parent_ns, collision);
    if (existing >= 0) return existing;
    if (collision) {
        diagnostic::report(diag, {file_id, name_tok,
            "'" + name + "' is already declared and is not a namespace.", {}});
        return -1;
    }
    int fid = parse::allocFrameId(tree);
    parse::Entry e;
    e.kind = parse::EntryKind::kNamespace;
    e.name = name;
    e.ns_frame_id = fid;
    e.owner_ns_frame = (parent_ns == kGlobalFrame) ? -1 : parent_ns;
    e.slids_type = widen::internOrNone(enum_underlying);   // set → enum (doubles as a type)
    e.file_id = file_id;
    e.tok = name_tok;
    parse::addEntry(tree, std::move(e));
    return fid;
}

// Every member kind a namespace/class body may hold (parse emits only these). The
// scope phases assert this at each member (NO silent defaults — an unhandled kind is
// a parse/compiler bug, not a quiet skip), then handle the kinds relevant to that
// phase and legitimately no-op the rest (e.g. an alias has no BODY). A non-const
// var-decl member would be a GLOBAL — parse rejects it today; when globals land
// (Phase 8) they get explicit handling, so the assert flags the gap loudly.
bool isScopeMember(parse::Node const& m) {
    return m.kind == parse::Kind::kClassDef
        || m.kind == parse::Kind::kNamespaceDecl
        || m.kind == parse::Kind::kEnumDecl
        || m.kind == parse::Kind::kAliasDecl
        || m.kind == parse::Kind::kFunctionDef
        || m.kind == parse::Kind::kFunctionDecl
        || (m.kind == parse::Kind::kVarDeclStmt && (m.is_const || m.is_global));
}

// The BODY phase for ONE scope (a namespace, a class, or — recursing — either
// nested in the other to any depth), with its frame OPEN. Resolves, in member
// order: a class's field-default exprs and its const/enum-member inits; every
// member FUNCTION body (method/ctor/dtor for a class, or a free function for a
// namespace — signature types already resolved in resolveScopeTypes); recurses into
// nested namespace AND class through THIS routine. Names/types/cycle already ran
// in the registration phases, so a member body forward-references freely. One
// routine for all three contexts replaces resolveNamespaceBodies +
// resolveClassMemberBodies + the bundled resolveClassMemberInits/FieldDefaults +
// the kClassDef/kNamespaceDecl cross-arms.
// A DERIVED class (node.text = base name) opens its base's member frame so base
// members (alias/const/enum/method) resolve BARE in the derived — the base is the
// unnamed first field. Returns the base's ns_frame, or -1. Opened BELOW the derived's
// own frame so derived members shadow base members; the WHOLE base chain (immediate
// base, its base, ...) is opened, deepest-ancestor-first so a nearer base shadows a
// farther one. Returns the number of frames pushed (to pop).
int pushBaseChain(parse::Tree& tree, parse::Node const& node) {
    if (node.kind != parse::Kind::kClassDef || node.text.empty()) return 0;
    widen::TypeRef base_type = widen::kNoType;
    int id = resolveName(tree, node.text);
    if (id >= 0 && tree.entries[id].kind == parse::EntryKind::kClass
        && !tree.entries[id].is_template) {
        base_type = tree.entries[id].slids_type;
    } else {
        // A template-INSTANCE base (`Vec<int> : Der`): the spelling is no entry
        // name — read the `_$base` field's RESOLVED type instead (kNoType until
        // the TYPES phase resolves it; the BODY phase, which needs the chain
        // open, runs after).
        for (auto const& p : node.params) {
            if (p && p->name == "_$base") {
                base_type = widen::strip(p->return_type);
                break;
            }
        }
        if (widen::form(base_type) != widen::Type::Form::kSlid) return 0;
    }
    // derived frame first, then each base frame (most-derived first)
    std::vector<int> frames = parse::classAndBaseFrames(tree, base_type);
    for (auto it = frames.rbegin(); it != frames.rend(); ++it)  // deepest pushed first
        tree.open_ns_frames.push_back(*it);
    return (int)frames.size();
}

void resolveScopeBodies(parse::Tree& tree, parse::Node& node, bool isClass,
                        diagnostic::Sink& diag) {
    int frame = isClass
        ? (node.resolved_entry_id >= 0
               ? tree.entries[node.resolved_entry_id].ns_frame_id : -1)
        : node.resolved_entry_id;
    if (frame < 0) return;   // a duplicate / unregistered scope — no members to resolve
    int base_pushed = pushBaseChain(tree, node);
    tree.open_ns_frames.push_back(frame);
    std::string saved_base = tree.current_base_name;
    std::string saved_class = tree.current_class_name;
    if (isClass) {
        auto it = tree.classes.find(widen::strip(node.return_type));
        // Bare field names in a method/ctor/dtor body resolve via a kField frame pushed in
        // resolveFunctionBody (see collectMethodFields); current_base_name/class_name below
        // still drive the `Base:` reframe and base-class depth.
        tree.current_base_name = node.text;   // base name for the `Base:` reframe (or "")
        tree.current_class_name = (it != tree.classes.end()) ? it->second.name : "";
        // Synthesize the default copy/move/swap ops NOW — the SINGLE choke point every
        // class-body pass funnels through (file-scope via run(), local via
        // registerLocalClasses, namespace-local + class-nested by recursion). Fields are
        // final (the TYPES phase ran) and user ops are registered, so the appended methods
        // resolve in place in the member loop below. Covers EVERY class kind uniformly.
        synthesizeClassTransferOps(tree, node, diag);
        // And, for a completed-incomplete header class, the complete ctor (@C__$ctor) an
        // opaque importer cannot construct itself. Same choke point, same reason.
        synthesizeOpaqueCtor(tree, node, diag);
        // Field-default exprs, with the class frame open (a default may name a
        // sibling member bare).
        for (auto& p : node.params) {
            if (p && !p->children.empty() && p->children[0]) {
                resolveExpr(tree, *p->children[0], diag);
            }
        }
    } else {
        tree.current_base_name.clear();
        tree.current_class_name.clear();
    }
    for (auto& m : node.children) {
        if (!m) continue;
        assert(isScopeMember(*m) && "unexpected scope-member kind in BODY phase");
        if (m->kind == parse::Kind::kFunctionDef) {
            // Signature types (incl. the `_$recv` receiver) were resolved in the
            // TYPES phase (resolveScopeTypes); the body is resolved here. A
            // kFunctionDecl (forward decl) has no body — legitimately no-op'd below.
            // A member TEMPLATE's body stays pristine — snapshot the scope instead.
            if (!m->type_params.empty()) { snapshotTemplate(tree, *m); continue; }
            resolveFunctionBody(tree, *m, diag, /*nested=*/false);
        } else if (m->kind == parse::Kind::kFunctionDecl
                   && !m->type_params.empty()) {
            // A header class's bodyless member-template DECLARATION: snapshot,
            // so a consumer can mint declaration-only instances from it.
            snapshotTemplate(tree, *m);
        } else if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            if (isQualified(*m)) continue;   // remote-namespace member, done by relocation
            for (auto& init : m->children) {
                if (init) resolveExpr(tree, *init, diag);
            }
        } else if (m->kind == parse::Kind::kEnumDecl) {
            if (isQualified(*m)) continue;   // remote-namespace member, done by relocation
            resolveEnumMemberInits(tree, *m, diag);
        } else if (m->kind == parse::Kind::kNamespaceDecl) {
            resolveScopeBodies(tree, *m, /*isClass=*/false, diag);
        } else if (m->kind == parse::Kind::kClassDef) {
            // A member CLASS TEMPLATE's body stays pristine; its file-level scope
            // snapshot (taken with this scope's frames open) serves instantiation.
            if (!m->type_params.empty()) { snapshotTemplate(tree, *m); continue; }
            resolveScopeBodies(tree, *m, /*isClass=*/true, diag);
        }
    }
    tree.current_base_name = saved_base;
    tree.current_class_name = saved_class;
    tree.open_ns_frames.pop_back();
    for (int i = 0; i < base_pushed; i++) tree.open_ns_frames.pop_back();
}

// The TYPES phase for ONE scope: now that EVERY name across every scope exists (the
// NAME phase ran), resolve EVERY declared type with the scope frame OPEN — member
// alias targets, then a class's FIELD types (via registerClassBody, attaching slots),
// then const types and function/method param + return types — writing the resolved
// types BACK to the member entries. Recurses into nested namespaces AND classes, so
// the enclosing frame chain is naturally on the stack (a field / signature may name a
// hoisted member or an enclosing-scope sibling bare — NO frame-chain reopening). This
// is what lets a member type name ANY class regardless of order (the forward-ref
// bugs): types resolve AFTER all names, not during registration. (Enums are
// self-contained, resolved at NAME.)
void resolveScopeTypes(parse::Tree& tree, parse::Node& node, bool isClass,
                       diagnostic::Sink& diag) {
    int frame = isClass
        ? (node.resolved_entry_id >= 0
               ? tree.entries[node.resolved_entry_id].ns_frame_id : -1)
        : node.resolved_entry_id;
    if (frame < 0) return;
    int base_pushed = pushBaseChain(tree, node);
    tree.open_ns_frames.push_back(frame);
    // Member alias TARGETS first, so a later const / field / signature type in this
    // scope may name a member alias.
    for (auto& m : node.children) {
        if (!m || m->kind != parse::Kind::kAliasDecl) continue;
        if (isQualified(*m) && m->return_type != widen::kNoType) continue;  // remote member
        if (m->resolved_entry_id < 0) continue;   // a duplicate — skipped at NAMES
        // A member alias TEMPLATE builds its PATTERN (node target stays pristine).
        if (!m->type_params.empty()) {
            validateAliasTemplate(tree, *m, diag);
            continue;
        }
        resolveDeclType(tree, m->return_type, m->file_id, m->tok, diag);
        tree.entries[m->resolved_entry_id].slids_type = m->return_type;
    }
    // A class's FIELD types (resolve + attach slots). The frame is open, so a field
    // may name a hoisted member / enclosing sibling bare with no chain reopening.
    if (isClass) registerClassBody(tree, node, diag);
    for (auto& m : node.children) {
        if (!m) continue;
        assert(isScopeMember(*m) && "unexpected scope-member kind in TYPES phase");
        if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            if (isQualified(*m)) continue;   // remote-namespace member, done by relocation
            if (m->resolved_entry_id < 0) continue;   // a duplicate — skipped at NAMES
            resolveDeclType(tree, m->return_type, m->file_id, m->tok, diag);
            if (constNeedsStorage(m->return_type)) {
                diagnostic::report(diag, {m->file_id, m->name_tok,
                    "A const variable of a non-scalar type (array, tuple, class, or "
                    "pointer) requires global storage, which is not yet supported.", {}});
            }
            tree.entries[m->resolved_entry_id].slids_type = m->return_type;
        } else if (m->kind == parse::Kind::kFunctionDef
                || m->kind == parse::Kind::kFunctionDecl) {
            // ctor/dtor hooks have no entry but still need their `_$recv` param typed
            // for the body; a DUPLICATE member (entry skipped at NAME) is done — skip
            // it so a bad type doesn't pile a second diagnostic on the dup error.
            bool isHook = m->name == "_$ctor" || m->name == "_$dtor";
            if (m->resolved_entry_id < 0 && !isHook) continue;
            // A member TEMPLATE resolves its PATTERN signature onto the entry; the
            // node's own types stay pristine for the instance clone.
            if (!m->type_params.empty()) {
                if (m->resolved_entry_id >= 0)
                    resolveTemplatePatterns(tree, *m, m->resolved_entry_id, diag);
                continue;
            }
            // Resolve param + return types on the NODE (incl. a method's `_$recv` —
            // the class kSlid is a known type by now); write the signature back to a
            // non-hook member's ENTRY, index-aligned with the node's params.
            resolveDeclType(tree, m->return_type, m->file_id, m->tok, diag);
            std::vector<widen::TypeRef> ptypes;
            for (auto& p : m->params) {
                if (!p) continue;
                if (p->return_type != widen::kNoType)
                    resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
                ptypes.push_back(p->return_type);
            }
            if (m->resolved_entry_id >= 0) {   // ctor/dtor hooks have no entry
                parse::Entry& e = tree.entries[m->resolved_entry_id];
                e.slids_type = m->return_type;
                e.param_types = std::move(ptypes);
            }
        } else if (m->kind == parse::Kind::kNamespaceDecl) {
            resolveScopeTypes(tree, *m, /*isClass=*/false, diag);
        } else if (m->kind == parse::Kind::kClassDef) {
            // A member CLASS TEMPLATE is a pattern — nothing here resolves.
            if (!m->type_params.empty()) continue;
            resolveScopeTypes(tree, *m, /*isClass=*/true, diag);
        }
    }
    tree.open_ns_frames.pop_back();
    for (int i = 0; i < base_pushed; i++) tree.open_ns_frames.pop_back();
}


// `alias Ns;` / `alias A:B;` — import a namespace's members into the current
// scope (push its frame onto the open chain for the rest of this lexical scope).
void resolveBareAlias(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    // The whole path (qualifier + name) names a namespace here.
    std::vector<std::string> segs = s.qualifier;
    segs.push_back(s.name);
    std::vector<int> toks = s.qualifier_toks;
    toks.push_back(s.name_tok);
    int frame = resolveNamespaceSegments(tree, segs, toks, s.global_qualified,
                                         s.file_id, diag);
    if (frame < 0) return;
    // A class is a type, not an importable namespace: `alias Space;` is rejected
    // even though the class exposes a member set (the path resolved to a class).
    for (parse::Entry const& e : tree.entries) {
        if (e.ns_frame_id == frame && e.kind == parse::EntryKind::kClass) {
            diagnostic::report(diag, {s.file_id, s.name_tok,
                "A class is a type, not an importable namespace.", {}});
            return;
        }
    }
    tree.open_ns_frames.push_back(frame);
}

// ---- Enums ------------------------------------------------------------------
// An enum lowers (at resolve, not desugar — members must be kConst by constfold
// so `Enum:member` folds) to: a namespace of the members (named enum), which
// doubles as a transparent type alias to the underlying; or bare consts in the
// enclosing scope (anonymous enum). Member values auto-increment by 1 (int) or
// 1.0 (float) from 0, with an explicit literal resetting the running counter
// (C rules).

// Structural — sees through an alias underlying (`alias F = float; enum F ...`).
bool isFloatUnderlying(widen::TypeRef t) {
    widen::TypeKind k;
    return widen::classify(widen::deepStrip(t), k) && k.cat == widen::Category::kFloat;
}

std::string enumFloatText(double d) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    return buf;
}

// Deep-copy an expression subtree (member init exprs: literals + unary/binary).
// Needed because an implicit enum member's value is synthesized as
// `clone(last-explicit-init) + offset`, and the explicit init may be an
// arbitrary constant expression not yet folded at resolve time.
std::unique_ptr<parse::Node> cloneExpr(parse::Node const& src) {
    auto n = std::make_unique<parse::Node>();
    n->kind = src.kind;
    n->name = src.name;
    n->text = src.text;
    n->return_type = src.return_type;
    n->return_type_seg_toks = src.return_type_seg_toks;
    n->file_id = src.file_id;
    n->tok = src.tok;
    n->name_tok = src.name_tok;
    // A qualified reference (`Other:kX`, `::g`) carries its qualifier on these
    // fields, not in children — drop them and the clone re-resolves as a bare
    // name. (See feedback_cloner_pos_propagation: cloners must copy every field
    // resolution depends on.)
    n->qualifier = src.qualifier;
    n->qualifier_toks = src.qualifier_toks;
    n->global_qualified = src.global_qualified;
    for (auto const& c : src.children) {
        if (c) n->children.push_back(cloneExpr(*c));
    }
    return n;
}

// Synthesize auto-increment literal inits for members lacking one, then create a
// kConst entry per member. ns_frame >= 0: named enum (members owned by that
// namespace). ns_frame < 0: anonymous (members land in the current lexical
// frame, bare-visible).
void registerEnumMembers(parse::Tree& tree, parse::Node& node, int ns_frame,
                         diagnostic::Sink& diag) {
    bool is_float = isFloatUnderlying(node.return_type);
    // Auto-increment, C rules. A member with no init takes the previous value
    // plus one; an explicit init resets the run. Because an explicit init may
    // be a not-yet-folded expression (`kB = 1 + 2`), we synthesize an implicit
    // member's value as `clone(anchor) + steps` where `anchor` is the last
    // explicit init and `steps` counts members since it (constfold then folds
    // the whole thing). Before any explicit init, the value is just the member
    // ordinal. The offset literal matches the underlying family so constfold's
    // no-int/float-mix rule is satisfied.
    auto makeOffsetLiteral = [&](long long v, int file_id, int tok) {
        auto lit = std::make_unique<parse::Node>();
        if (is_float) {
            lit->kind = parse::Kind::kFloatLiteral;
            lit->text = enumFloatText(static_cast<double>(v));
        } else {
            lit->kind = parse::Kind::kIntLiteral;
            lit->text = std::to_string(v);
        }
        lit->file_id = file_id;
        lit->tok = tok;
        return lit;
    };
    parse::Node const* anchor = nullptr;  // last explicit init expr
    int steps = 0;                        // members since the anchor
    long long ordinal = 0;                // member index (used before any anchor)
    for (auto& m : node.children) {
        if (!m) continue;
        if (!m->children.empty()) {
            anchor = m->children[0].get();
            steps = 0;
        } else if (is_float) {
            // A float enum cannot auto-increment — every member needs an explicit
            // value (+1.0 per step is meaningless for floats).
            diagnostic::report(diag, {m->file_id, m->name_tok,
                "Enum member '" + m->name + "' of a float type requires an explicit "
                "value.", {}});
            steps++;
            ordinal++;
            continue;
        } else if (anchor == nullptr) {
            m->children.push_back(makeOffsetLiteral(ordinal, m->file_id, m->tok));
        } else {
            auto plus = std::make_unique<parse::Node>();
            plus->kind = parse::Kind::kBinaryExpr;
            plus->text = "+";
            plus->file_id = m->file_id;
            plus->tok = m->tok;
            plus->children.push_back(cloneExpr(*anchor));
            plus->children.push_back(makeOffsetLiteral(steps, m->file_id, m->tok));
            m->children.push_back(std::move(plus));
        }
        steps++;
        ordinal++;

        int prev = (ns_frame >= 0)
            ? findMemberDeclared(tree, ns_frame, m->name)
            : parse::findInFrame(tree, parse::currentFrameId(tree), m->name);
        if (prev >= 0) {
            parse::Entry const& pe = tree.entries[prev];
            diagnostic::report(diag, {m->file_id, m->name_tok,
                "Duplicate declaration of '" + m->name + "'.",
                {{pe.file_id, pe.tok, "first declared here"}}});
            continue;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = m->name;
        e.slids_type = node.return_type;
        // A named enum's members carry the enum name as their type label (the
        // member's type is `Enum`, not the bare underlying) so ##type reports
        // `const Enum`. Anonymous enum -> no name -> no label (bare `const int`).
        e.alias_label = node.name;
        e.file_id = m->file_id;
        e.tok = m->name_tok;
        e.owner_ns_frame = (ns_frame >= 0) ? ns_frame : -1;
        m->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
}

// Resolve enum member init expressions. Split from registerEnumMembers so a
// file-scope enum's inits resolve in a later pass — after every file-scope
// entry is collected — letting a member init reference a file-scope const
// (`const int kG = 6; enum E ( e = kG );`). At block scope all enclosing-scope
// entries already exist, so the body pass resolves inits right after register.
// For a named enum, open its own frame first so a member init can reference a
// sibling member bare (`enum E ( a, b = a )`); members already exist (registered
// above). The frame chain still falls back to the enclosing scope, so file-scope
// refs keep resolving. Anonymous enums need no push — their members are already
// bare in the enclosing frame.
void resolveEnumMemberInits(parse::Tree& tree, parse::Node& node,
                            diagnostic::Sink& diag) {
    bool named = !node.name.empty() && node.resolved_entry_id >= 0;
    if (named) tree.open_ns_frames.push_back(node.resolved_entry_id);
    for (auto& m : node.children) {
        if (!m) continue;
        for (auto& init : m->children) {
            if (init) resolveExpr(tree, *init, diag);
        }
    }
    if (named) tree.open_ns_frames.pop_back();
}

// `parent_ns` is the enclosing namespace frame, or kGlobalFrame for an enum at
// file or function (lexical) scope. A named enum becomes a namespace member of
// parent_ns (or a lexical namespace entry at global scope); an anonymous enum's
// members land as namespace members of parent_ns, or — at lexical scope — as
// bare consts in the current frame.
void registerEnum(parse::Tree& tree, parse::Node& node, int parent_ns,
                  diagnostic::Sink& diag) {
    // Validate the underlying type spelling once (members inherit it).
    resolveDeclType(tree, node.return_type, node.file_id, node.tok, diag);
    if (node.name.empty()) {
        int owner = (parent_ns == kGlobalFrame) ? -1 : parent_ns;
        registerEnumMembers(tree, node, owner, diag);
        return;
    }
    int ns = openNamespace(tree, node.name, node.name_tok, node.file_id,
                           parent_ns, diag, widen::spellOrEmpty(node.return_type));
    if (ns < 0) return;
    node.resolved_entry_id = ns;
    registerEnumMembers(tree, node, ns, diag);
}

// While resolving a nested function body, a kLocalVar resolved in an enclosing
// (host) frame is a capture — record it on the nested function node.
bool isCaptureLocal(parse::Tree const& tree, int id) {
    if (tree.capture_floor < 0 || id < 0) return false;
    parse::Entry const& e = tree.entries[id];
    return e.kind == parse::EntryKind::kLocalVar
        && e.parent_frame_id > kGlobalFrame
        && e.parent_frame_id < tree.capture_floor;
}

void noteCapture(parse::Tree& tree, int id) {
    if (!tree.capture_node || !isCaptureLocal(tree, id)) return;
    for (int c : tree.capture_node->captures) if (c == id) return;
    tree.capture_node->captures.push_back(id);
}

// Build `_$recv^` — the deref of the implicit receiver param `_$recv` (a
// `Class^` holding the object's address). This IS the object the author calls
// `self` (transmogrification: author `self` = compiler `_$recv^`; see the
// `self`-keyword registration in resolveFunctionBody). The one place this shape
// is minted, shared by every bare-member rewrite: a bare field READ/WRITE
// (author `self.field` -> `_$recv^.field`) and a bare sibling-method call
// (author `self.m()` -> `_$recv^.m()`). Routing all of them through `_$recv`
// gives a bare method call the SAME reach as a bare field access.
std::unique_ptr<parse::Node> buildRecvDeref(int file_id, int tok) {
    auto recv_id = std::make_unique<parse::Node>();
    recv_id->kind = parse::Kind::kIdentExpr;
    recv_id->name = "_$recv";
    recv_id->file_id = file_id;
    recv_id->tok = tok;
    auto deref = std::make_unique<parse::Node>();
    deref->kind = parse::Kind::kDerefExpr;
    deref->file_id = file_id;
    deref->tok = tok;
    deref->children.push_back(std::move(recv_id));
    return deref;
}

// `Base:member` inside a DERIVED class member (Base = the immediate base) reframes
// `self` to the base sub-object (slot 0): `Base:self` -> `self._$base`, `Base:field`
// -> `self._$base.field`, `Base:method(args)` -> a method call on `self._$base`. The
// base is at offset 0, so the receiver is `self` reinterpreted as Base^. Returns true
// (and resolves the rewritten node) when it applied. Single base segment for now.
// If `name` is a FIELD of the current class's BASE chain, the number of `_$base`
// hops to reach the ancestor that declares it (1 = immediate base), else 0. A bare
// `self._$base...(_$base)` — the ancestor sub-object `depth` levels up (depth>=1).
std::unique_ptr<parse::Node> buildBaseReceiver(int depth, int file_id, int tok) {
    std::unique_ptr<parse::Node> recv = buildRecvDeref(file_id, tok);   // self
    for (int h = 0; h < depth; h++) {
        auto hop = std::make_unique<parse::Node>();
        hop->kind = parse::Kind::kFieldExpr;
        hop->name = "_$base";
        hop->file_id = file_id;
        hop->tok = tok;
        hop->children.push_back(std::move(recv));
        recv = std::move(hop);
    }
    return recv;
}

// Lower a bare reference that resolved to a class FIELD (a kField entry) to the receiver
// access `_$recv^.field` (own field, depth 0) or `_$recv^._$base…field` (a base field).
// Mutates `node` IN PLACE from a kIdentExpr to a kFieldExpr and resolves the synthesized
// receiver. Returns true when it fired. THE single field-access lowering — every
// bare-name resolution site calls it right after resolveName, so no context can "forget"
// to reach `self` the way the old per-site method_fields rewrites could.
bool lowerFieldRef(parse::Tree& tree, parse::Node& node, int id,
                   diagnostic::Sink& diag) {
    if (id < 0 || tree.entries[id].kind != parse::EntryKind::kField) return false;
    int depth = tree.entries[id].field_depth;
    std::unique_ptr<parse::Node> recv = (depth == 0)
        ? buildRecvDeref(node.file_id, node.tok)
        : buildBaseReceiver(depth, node.file_id, node.tok);
    node.kind = parse::Kind::kFieldExpr;   // node.name stays the field name
    node.resolved_entry_id = -1;           // no longer an entry ref; classify types the field
    node.children.clear();
    node.children.push_back(std::move(recv));
    resolveExpr(tree, *node.children[0], diag);   // resolve the receiver `_$recv^`
    return true;
}

// A resolved `_$recv^.field` lvalue node for a bare field NAME (id = its kField entry).
// The whole-name store / aug-assign rewrites start from the STATEMENT name (not an ident
// node), so they mint the ident here and lower it — the same field access lowerFieldRef
// produces for an in-place node.
std::unique_ptr<parse::Node> buildFieldLvalue(parse::Tree& tree, std::string const& name,
                                              int id, int file, int tok,
                                              diagnostic::Sink& diag) {
    auto n = std::make_unique<parse::Node>();
    n->kind = parse::Kind::kIdentExpr;
    n->name = name;
    n->file_id = file;
    n->tok = tok;
    lowerFieldRef(tree, *n, id, diag);
    return n;
}

// The `_$base` hop count to reach a transitive BASE CLASS named `className` from the
// current class (1 = immediate base), else 0. Used by the `Base:` qualifier.
int baseClassDepth(parse::Tree& tree, std::string const& className) {
    if (tree.current_base_name.empty()) return 0;
    int id = resolveName(tree, tree.current_base_name);
    if (id < 0 || tree.entries[id].kind != parse::EntryKind::kClass) return 0;
    widen::TypeRef cls = widen::strip(tree.entries[id].slids_type);
    // Backstop only: a cyclic base chain is diagnosed by checkClassByValueAcyclic
    // (a base is a by-value `_$base` field); this guard just bounds the walk.
    int guard = (int)tree.classes.size() + 2;
    for (int depth = 1; cls != widen::kNoType && guard-- > 0; depth++) {
        auto it = tree.classes.find(cls);
        if (it == tree.classes.end()) return 0;
        if (it->second.name == className) return depth;
        cls = parse::baseTypeOf(it->second);
    }
    return 0;
}

bool frameHasFunction(parse::Tree& tree, int frame, std::string const& name) {
    if (frame < 0) return false;
    // The first member by name in the frame; same-name members are a redeclaration
    // error, so a function's presence is exactly "first match is a function".
    int id = findMemberDeclared(tree, frame, name);
    return id >= 0 && tree.entries[id].kind == parse::EntryKind::kFunction;
}

// Base hops from `self` to the subobject named by `className`: 0 = the CURRENT class
// itself (`Self:` — no reframe, `self` as-is), d>=1 = a transitive base at depth d
// (`Base:` — self._$base...(d)), -1 = neither (not in the self chain). Unifies the
// own-class and ancestor qualifier spellings behind one hop count for buildBaseReceiver.
int selfOrBaseDepth(parse::Tree& tree, std::string const& className) {
    if (!tree.current_class_name.empty() && className == tree.current_class_name)
        return 0;
    int d = baseClassDepth(tree, className);
    return d > 0 ? d : -1;
}

bool tryResolveBaseQualifier(parse::Tree& tree, parse::Node& e,
                             diagnostic::Sink& diag, bool unevaluated) {
    // Spelling `X:self.method(args)` — a method call whose receiver (children[0]) is the
    // qualified `X:self`. This is the explicit-self twin of `X:method(args)`: reframe the
    // receiver to X's subobject (0 hops for the own class, d for a base) and BYPASS
    // dispatch — a static call to X's method, not the runtime-most-derived override.
    if (e.kind == parse::Kind::kMethodCallStmt
        && !e.children.empty() && e.children[0]
        && e.children[0]->kind == parse::Kind::kIdentExpr
        && e.children[0]->qualifier.size() == 1
        && e.children[0]->name == "self") {
        int depth = selfOrBaseDepth(tree, e.children[0]->qualifier[0]);
        if (depth < 0) return false;   // an unrelated qualifier — let normal lookup diagnose
        e.children[0] = buildBaseReceiver(depth, e.file_id, e.tok);   // self._$base...(depth)
        e.bypass_virtual = true;
        for (auto& ch : e.children)
            if (ch) resolveExpr(tree, *ch, diag, unevaluated);
        return true;
    }
    if (e.qualifier.size() != 1) return false;
    // 0 = the OWN class (`Self:` — no reframe), d>=1 = a transitive base (`Base:` at
    // depth d), -1 = neither (not in the self chain -> defer to normal lookup).
    int depth = selfOrBaseDepth(tree, e.qualifier[0]);
    if (depth < 0) return false;
    bool is_call  = (e.kind == parse::Kind::kCallExpr || e.kind == parse::Kind::kCallStmt);
    bool is_ident = (e.kind == parse::Kind::kIdentExpr);
    if (!is_call && !is_ident) return false;
    // `X:member` reframes self ONLY for an INSTANCE member (a field, a method, or `self`).
    // A static (const / alias / enum / nested type) is left for the normal qualified-name
    // lookup — it is not reached through `self` / `self._$base`.
    int aid = resolveName(tree, e.qualifier[0]);
    if (aid < 0) return false;
    widen::TypeRef anc = widen::strip(tree.entries[aid].slids_type);
    auto ci = tree.classes.find(anc);
    bool is_field = (ci != tree.classes.end())
        && parse::classHasField(ci->second, e.name);
    bool is_self   = (is_ident && e.name == "self");
    // A METHOD reached via the qualifier — the qualified class's own frame OR one it
    // INHERITS from a base (so `Mid:m()` reaches `Top`'s `m`). This mirrors the
    // `X:self.method()` form, which binds against the full chain in classify; without
    // the base-chain walk the two spellings would diverge (one bypasses, one errors).
    bool is_method = false;
    if (is_call) {
        for (int fr : parse::classAndBaseFrames(tree, anc))
            if (frameHasFunction(tree, fr, e.name)) { is_method = true; break; }
    }
    if (!is_self && !(is_ident && is_field) && !is_method) return false;  // static -> defer
    auto base_recv = buildBaseReceiver(depth, e.file_id, e.tok);   // depth 0 = self as-is
    e.qualifier.clear();
    e.qualifier_toks.clear();
    if (is_ident && e.name == "self") {
        // `X:self` as a bare value: the own class (depth 0) is plain `self`; a base
        // (depth>=1) is `self._$base...(depth)` — take the reframed receiver verbatim.
        e.kind = base_recv->kind;
        e.name = base_recv->name;
        e.children = std::move(base_recv->children);
    } else if (is_ident) {
        e.kind = parse::Kind::kFieldExpr;            // X:field -> self[._$base...].field
        e.children.clear();
        e.children.push_back(std::move(base_recv));  // [0] = the reframed receiver
    } else {                                         // X:method(args) -> method call
        std::vector<std::unique_ptr<parse::Node>> args = std::move(e.children);
        e.kind = parse::Kind::kMethodCallStmt;       // e.name = method
        e.children.clear();
        e.children.push_back(std::move(base_recv));  // [0] = the reframed receiver
        for (auto& a : args) e.children.push_back(std::move(a));
        // A qualified call BYPASSES virtual dispatch: it statically targets the named
        // class's method (own or ancestor), not the runtime-most-derived override.
        e.bypass_virtual = true;
    }
    for (auto& ch : e.children)
        if (ch) resolveExpr(tree, *ch, diag, unevaluated);
    return true;
}

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag,
                 bool unevaluated) {
    // Const-expression array dims in a TYPE operand (`sizeof(int[N])`, `<int[N]^>`,
    // `(int[N]=e)`): resolve each so its const refs / sizeof resolve; constfold
    // folds + bakes the size (provisional `[1]` until then).
    for (auto& d : e.dim_exprs)
        if (d) resolveExpr(tree, *d, diag, /*unevaluated=*/false);
    if (tryResolveBaseQualifier(tree, e, diag, unevaluated)) return;
    switch (e.kind) {
        case parse::Kind::kIdentExpr: {
            int id;
            if (isQualified(e)) {
                id = resolveQualifiedRef(tree, e, diag);
                if (id < 0) return;
            } else {
                int other = -1;
                id = resolveNameDetail(tree, e.name, other);
                if (other >= 0) {
                    parse::Entry const& a = tree.entries[id];
                    parse::Entry const& b = tree.entries[other];
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Reference to '" + e.name + "' is ambiguous.",
                        {{a.file_id, a.tok, "could be this one"},
                         {b.file_id, b.tok, "or this one"}}});
                    return;
                }
                // A bare name resolving to a class FIELD (own or base) lowers to
                // `_$recv^.field` — the single field-access rewrite, so a body local of
                // the same name (which resolveNameDetail found first) shadows the field.
                if (lowerFieldRef(tree, e, id, diag)) return;
                if (id < 0) {
                    if (reportPrivateNameInInline(tree, e.name, e.file_id,
                                                  e.tok, "references", diag)) {
                        return;
                    }
                    if (namespaceMemberExists(tree, e.name)) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "'" + e.name + "' needs a namespace qualifier.", {}});
                    } else {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "Unresolved identifier '" + e.name + "'.", {}});
                    }
                    return;
                }
            }
            if (tree.entries[id].kind == parse::EntryKind::kAlias) {
                parse::Entry const& a = tree.entries[id];
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a type, not a value.",
                    {{a.file_id, a.tok, "alias declared here"}}});
                return;
            }
            if (tree.entries[id].kind == parse::EntryKind::kNamespace) {
                parse::Entry const& ns = tree.entries[id];
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a namespace, not a value.",
                    {{ns.file_id, ns.tok, "namespace declared here"}}});
                return;
            }
            if (tree.entries[id].kind == parse::EntryKind::kClass) {
                // A class TEMPLATE's bare name is no type and no value — every
                // use requires the type-argument list. Inside its own
                // instantiation, though, the bare name means THE INSTANCE.
                if (tree.entries[id].is_template) {
                    int self = -1;
                    for (auto it = tree.tmpl_self_stack.rbegin();
                         it != tree.tmpl_self_stack.rend(); ++it) {
                        if (it->tmpl_entry == id) { self = it->instance_entry; break; }
                    }
                    if (self < 0) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "Class template '" + e.name
                            + "' requires a type-argument list.", {}});
                        return;
                    }
                    id = self;
                }
                // A bare class name in an EVALUATED value position is a default
                // construction (`Class` == `Class()`): rewrite the ident into a
                // zero-arg construction kCallExpr that flows through the normal
                // class-construction machinery. In an UNEVALUATED context
                // (sizeof / ##type) the name is the TYPE, not a value — keep the
                // error so a type isn't silently constructed there.
                if (!unevaluated) {
                    e.kind = parse::Kind::kCallExpr;
                    e.resolved_entry_id = id;
                    e.is_construction = true;
                    return;
                }
                parse::Entry const& c = tree.entries[id];
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a type, not a value.",
                    {{c.file_id, c.tok, "class declared here"}}});
                return;
            }
            // Definite-assignment: reading a local before it is written is a
            // compile error (a value-before-init footgun). Only kLocalVar is
            // tracked — consts are substituted away, params are pre-seeded.
            if (tree.entries[id].kind == parse::EntryKind::kLocalVar
                && !isCaptureLocal(tree, id)) {
                // In-place aggregates (arrays, tuples) read under the monotonic
                // may-set: a slot write marks assigned_arrays, while a whole-value
                // init marks initialized_locals (arrays route their init to the
                // former, tuples to the latter) — either counts as set. Scalars
                // use the strict must-set.
                bool aggregate = isInPlaceAggregate(tree.entries[id].slids_type);
                bool ok = aggregate
                    ? (tree.assigned_arrays.count(id) > 0
                       || tree.initialized_locals.count(id) > 0)
                    : tree.initialized_locals.count(id) > 0;
                // In an unevaluated context (sizeof / ##type operand) only the
                // TYPE is read, never the value — so definite assignment is not
                // required. The read-mark below still fires (the var counts as
                // used), and the var is NOT marked initialized (a later real read
                // still errors).
                if (!ok && !unevaluated) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Use of uninitialized variable '" + e.name + "'.", {}});
                    return;
                }
                // This is a value-position read; record it for the unused sweep.
                tree.read_locals.insert(id);
            }
            noteCapture(tree, id);
            e.resolved_entry_id = id;
            return;
        }
        case parse::Kind::kCallExpr: {
            if (isPrintIntrinsic(e.name)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' cannot be used as an expression.", {}});
                for (auto& ch : e.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
                return;
            }
            resolveUserCall(tree, e, diag);
            return;
        }
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr: {
            // Operand must be an assignable lvalue. A complex lvalue (field /
            // index / deref) is a read-modify-write: resolve it as a store target
            // (the same DA an aug-assign does) and mark its base read.
            parse::Node& operand = *e.children[0];
            if (operand.kind == parse::Kind::kIndexExpr
                || operand.kind == parse::Kind::kFieldExpr
                || operand.kind == parse::Kind::kDerefExpr) {
                resolveStoreTarget(tree, operand, diag);
                markLvalueBaseRead(tree, operand);   // RMW reads the target
                return;
            }
            if (operand.kind != parse::Kind::kIdentExpr) {
                // Caret the offending operand (consistent with the function / const
                // cases below), not the '++' operator.
                diagnostic::report(diag, {operand.file_id, operand.tok,
                    "The operand of '" + e.text + "' must be a variable.", {}});
                return;
            }
            resolveExpr(tree, operand, diag, unevaluated);
            if (operand.resolved_entry_id >= 0) {
                parse::Entry const& entry = tree.entries[operand.resolved_entry_id];
                if (entry.kind == parse::EntryKind::kFunction) {
                    diagnostic::report(diag, {operand.file_id, operand.tok,
                        "'" + operand.name + "' is a function, not a variable.",
                        {{entry.file_id, entry.tok, "function declared here"}}});
                } else if (entry.kind == parse::EntryKind::kConst) {
                    diagnostic::report(diag, {operand.file_id, operand.tok,
                        "Constant '" + operand.name + "' cannot be incremented.",
                        {{entry.file_id, entry.tok, "constant declared here"}}});
                }
            }
            return;
        }
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kTupleExpr:   // resolve each slot expr
            for (auto& ch : e.children) {
                if (ch) resolveExpr(tree, *ch, diag, unevaluated);
            }
            return;
        case parse::Kind::kAddrOfExpr: {
            // `^lvalue` — address-of. A bare variable yields a reference (`T^`);
            // an indexed element yields an iterator (`T[]`); a class field yields
            // a reference. Walk any subscript / field chain (resolving each index
            // as a read) down to the base variable.
            // `^field` / `^field[i]` / `^field.sub` (a bare field, OR a subscripted /
            // sub-field of a field, in a method body) -> rewrite the BASE ident of the
            // chain to `self.<field>`: a self.field access (the `self` keyword, an
            // address-aliased local) so the walk below descends to a resolvable ident —
            // like the explicit `^self.field[i]` form. Peek down the subscript/field
            // chain to its base ident first (a bare `^field` has no chain). A local
            // shadowing the field resolves normally, so this fires only when the base
            // name did NOT resolve AND it is a method field. (Without descending to the
            // base, `^field[i]` missed the rewrite and failed "Unresolved identifier".)
            if (e.children[0]) {
                parse::Node* peek = e.children[0].get();
                while (peek->kind == parse::Kind::kIndexExpr
                    || peek->kind == parse::Kind::kFieldExpr) {
                    peek = peek->children[0].get();
                }
                if (int pid = (peek->kind == parse::Kind::kIdentExpr
                               && !isQualified(*peek))
                                  ? resolveName(tree, peek->name) : -1;
                    pid >= 0 && tree.entries[pid].kind == parse::EntryKind::kField) {
                    // Rewrite the base field to `self._$base…(depth).field` over the `self`
                    // LOCAL — the address-of walk below descends to the base IDENT, so the
                    // receiver must bottom out at `self` (an ident), not `_$recv^` (a deref
                    // — what lowerFieldRef uses for a value read).
                    int depth = tree.entries[pid].field_depth;
                    auto recv = std::make_unique<parse::Node>();
                    recv->kind = parse::Kind::kIdentExpr;
                    recv->name = "self";
                    recv->file_id = peek->file_id;
                    recv->tok = peek->tok;
                    for (int h = 0; h < depth; h++) {
                        auto hop = std::make_unique<parse::Node>();
                        hop->kind = parse::Kind::kFieldExpr;
                        hop->name = "_$base";
                        hop->file_id = peek->file_id;
                        hop->tok = peek->tok;
                        hop->children.push_back(std::move(recv));
                        recv = std::move(hop);
                    }
                    peek->kind = parse::Kind::kFieldExpr;   // peek->name stays the field
                    peek->children.clear();
                    peek->children.push_back(std::move(recv));
                }
            }
            parse::Node* base = e.children[0].get();
            while (base->kind == parse::Kind::kIndexExpr
                || base->kind == parse::Kind::kFieldExpr) {
                if (base->kind == parse::Kind::kIndexExpr && base->children[1])
                    resolveExpr(tree, *base->children[1], diag, unevaluated);
                base = base->children[0].get();
            }
            if (base->kind != parse::Kind::kIdentExpr) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "The operand of '^' must be a variable or array element.",
                    {}});
                return;
            }
            int id = isQualified(*base)
                ? resolveQualifiedRef(tree, *base, diag)
                : resolveName(tree, base->name);
            if (id < 0) {
                if (!isQualified(*base)) {
                    diagnostic::report(diag, {base->file_id, base->tok,
                        "Unresolved identifier '" + base->name + "'.", {}});
                }
                return;
            }
            parse::Entry const& entry = tree.entries[id];
            // Addressable = a storage-backed variable: a local (stack alloca; params
            // and the address-aliased receiver are kLocalVar too) or a global (static
            // `@`-storage). The rest of EntryKind has no runtime slot to point at —
            // kConst is substituted, kAlias/kNamespace/kClass are type/scope names,
            // kFunction is code. codegen's emitLvalueAddr addresses exactly these two.
            if (entry.kind != parse::EntryKind::kLocalVar
                && entry.kind != parse::EntryKind::kGlobalVar) {
                diagnostic::report(diag, {base->file_id, base->tok,
                    "Cannot take the address of '" + base->name + "'.",
                    {{entry.file_id, entry.tok, "declared here"}}});
                return;
            }
            base->resolved_entry_id = id;
            // Taking an address aliases the variable — it may be read or written
            // through the pointer. Mark it assigned (so the alias is not a
            // use-before-init) and read (so it is not swept as unused). Arrays
            // use the monotonic may-set, scalars the strict must-set. In an
            // unevaluated context (`sizeof(^x)`) no address is actually taken, so
            // the var is NOT marked assigned (a later real read still errors);
            // the read-mark below still fires.
            if (!unevaluated) {
                if (isArrayType(entry.slids_type)) tree.assigned_arrays.insert(id);
                else tree.initialized_locals.insert(id);
            }
            tree.read_locals.insert(id);
            noteCapture(tree, id);
            return;
        }
        case parse::Kind::kDerefExpr:
            // `value^` — dereference. The operand is a reference/iterator value;
            // resolve it normally (read-mark + definite-assignment). classify
            // verifies it is a pointer type and supplies the pointee type.
            if (e.children[0]) resolveExpr(tree, *e.children[0], diag, unevaluated);
            return;
        case parse::Kind::kIndexExpr:
            // `base[index]` rvalue read: resolve the base (an array read -> its
            // definite-assignment is required) and the index expression.
            for (auto& ch : e.children) {
                if (ch) resolveExpr(tree, *ch, diag, unevaluated);
            }
            return;
        case parse::Kind::kFieldExpr:
            // `base.field` — resolve the base lvalue. The field NAME is a member
            // of the base's class, resolved by classify against the ClassInfo
            // (which knows the field set); resolve only walks the base.
            if (e.children[0]) resolveExpr(tree, *e.children[0], diag, unevaluated);
            return;
        case parse::Kind::kMethodCallStmt:
            // A method-call EXPRESSION (`x = obj.method(args)`). Resolve the
            // receiver (children[0]) and args (children[1..]); classify binds the
            // method against the receiver's class and stamps the return type. An
            // explicit template type-list resolves here, in the call's scope.
            resolveTmplArgs(tree, e, diag);
            for (auto& ch : e.children)
                if (ch) resolveExpr(tree, *ch, diag, unevaluated);
            return;
        case parse::Kind::kCastExpr: {
            // `<Type^> operand` — resolve the operand as a read, then substitute
            // and validate the target type spelling (alias chains, void[] reject,
            // unknown-type). classify enforces the cast legality rules.
            if (e.children[0]) resolveExpr(tree, *e.children[0], diag, unevaluated);
            // `<const>` / `<mutable>` qualifier casts carry no target type (the
            // result is derived from the operand in classify) — nothing to resolve.
            if (e.text == "const" || e.text == "mutable") return;
            resolveDeclType(tree, e.return_type, e.file_id, e.tok, diag,
                            &e.return_type_seg_toks);
            return;
        }
        case parse::Kind::kConvertExpr: {
            // `(Type=operand)` — resolve the operand as a read, then substitute
            // and validate the target value type. classify enforces the grid
            // legality (value/pointer source, non-pointer target).
            resolveExpr(tree, *e.children[0], diag, unevaluated);
            resolveDeclType(tree, e.return_type, e.file_id, e.tok, diag,
                            &e.return_type_seg_toks);
            return;
        }
        case parse::Kind::kNewExpr: {
            // new T / new T[n] / new(addr) T[n]. Validate + alias-resolve the
            // element type, then resolve the array-size and placement-address
            // sub-expressions as value reads. classify computes the result type
            // (T^ / T[]) and checks the size is integer / the addr is buffer-class.
            resolveDeclType(tree, e.return_type, e.file_id, e.tok, diag);
            if (e.children[0]) resolveExpr(tree, *e.children[0], diag, unevaluated);  // size
            if (e.children[1]) resolveExpr(tree, *e.children[1], diag, unevaluated);  // addr
            if (e.children.size() > 2 && e.children[2])
                resolveExpr(tree, *e.children[2], diag, unevaluated);                 // ctor-args
            return;
        }
        case parse::Kind::kSizeofExpr: {
            // sizeof(T): grammar parsed a type spelling onto return_type — alias-
            // resolve + validate it. sizeof(expr): the operand is an expression;
            // an ident NAMING a type (alias / enum facet, bare or qualified) is
            // measured as that type (stamp the underlying on return_type, like
            // ##type), and any other operand is a value whose type classify
            // measures. sizeof never evaluates the operand.
            if (e.return_type != widen::kNoType) {
                resolveDeclType(tree, e.return_type, e.file_id, e.tok, diag);
                return;
            }
            parse::Node& operand = *e.children[0];
            if (operand.kind != parse::Kind::kIdentExpr) {
                resolveExpr(tree, operand, diag, /*unevaluated=*/true);
                return;
            }
            bool qualified = isQualified(operand);
            int id = qualified ? resolveQualifiedRef(tree, operand, diag)
                               : resolveName(tree, operand.name);
            if (id >= 0) {
                parse::EntryKind k = tree.entries[id].kind;
                if (k == parse::EntryKind::kClass) {
                    // A class TEMPLATE has no size — the type-list is required.
                    if (tree.entries[id].is_template) {
                        diagnostic::report(diag, {operand.file_id, operand.tok,
                            "Class template '" + operand.name
                            + "' requires a type-argument list.", {}});
                        return;
                    }
                    // A class name is a type; measure it via its slotful kSlid
                    // (classify lowers to the runtime __$sizeof). Drop the operand
                    // so it reads as a type sizeof.
                    e.return_type = tree.entries[id].slids_type;
                    e.children.clear();
                    return;
                }
                bool is_type = (k == parse::EntryKind::kAlias)
                    || (k == parse::EntryKind::kNamespace
                        && tree.entries[id].slids_type != widen::kNoType);   // enum facet
                if (k == parse::EntryKind::kAlias) {
                    // Measure the alias as a type, PRESERVING the alias wrapper
                    // (don't flatten to the target spelling): sizeof sees through it
                    // for the size, and keeping the kAlias lets constfold refresh a
                    // const-expression dim in the alias target (`alias V = int[N]`).
                    e.return_type =
                        widen::internAlias(tree.entries[id].name, tree.entries[id].slids_type);
                    return;
                }
                if (is_type) {
                    // Enum facet only here (kClass / kAlias returned above): its
                    // underlying primitive is the handle on the entry, and `id` is
                    // already scope-resolved (line above) — use it directly.
                    e.return_type = tree.entries[id].slids_type;
                    return;
                }
                if (k == parse::EntryKind::kLocalVar
                    || k == parse::EntryKind::kConst) {
                    if (qualified) operand.resolved_entry_id = id;
                    else resolveExpr(tree, operand, diag, /*unevaluated=*/true);
                    return;
                }
                char const* what = (k == parse::EntryKind::kFunction)
                    ? "function" : "namespace";
                diagnostic::report(diag, {operand.file_id, operand.tok,
                    "'" + operand.name + "' is a " + what
                        + ", not a value or a type.", {}});
                return;
            }
            // id < 0: every class has a kClass entry (handled above), so a bare
            // unresolved name here is genuinely unknown.
            // a qualified ref already reported its own resolution failure
            // (resolveQualifiedRef); only a bare unresolved name needs reporting.
            if (!qualified) {
                diagnostic::report(diag, {operand.file_id, operand.tok,
                    "'" + operand.name + "' is not a value or a type.", {}});
            }
            return;
        }
        case parse::Kind::kStringifyType: {
            // ##type's operand is either a VALUE (resolve it; classify reports its
            // labeled type) or a TYPE NAME — an alias or an enum's transparent type
            // facet, bare OR namespace-qualified — in which case ##type reports the
            // UNDERLYING (stamped on return_type for classify). A name that is
            // neither (a namespace, a function, undefined) is rejected here. A
            // non-ident operand is a value expression.
            parse::Node& operand = *e.children[0];
            if (operand.kind != parse::Kind::kIdentExpr) {
                for (auto& ch : e.children) {
                    if (ch) resolveExpr(tree, *ch, diag, /*unevaluated=*/true);
                }
                return;
            }
            bool qualified = isQualified(operand);
            int id = qualified ? resolveQualifiedRef(tree, operand, diag)
                               : resolveName(tree, operand.name);
            if (id >= 0) {
                // A bare FIELD -> `_$recv^.field`, so ##type reports the FIELD's type.
                if (tree.entries[id].kind == parse::EntryKind::kField) {
                    lowerFieldRef(tree, operand, id, diag);
                    return;
                }
                parse::EntryKind k = tree.entries[id].kind;
                if (k == parse::EntryKind::kClass) {
                    // A class name is a type; ##type reports the class itself.
                    e.return_type = tree.entries[id].slids_type;
                    return;
                }
                bool is_type = (k == parse::EntryKind::kAlias)
                    || (k == parse::EntryKind::kNamespace
                        && tree.entries[id].slids_type != widen::kNoType);   // enum facet
                if (is_type) {
                    // ##type of a TYPE NAME reports its UNDERLYING (intentional: a
                    // VALUE keeps its alias/enum label via classify's alias_label
                    // channel; a type name flattens). Resolve the entry's type through
                    // the canonical handle resolver — an alias target chases to its
                    // underlying, an enum facet already holds the primitive — then
                    // deep-strip the transparent layers; classify spells the result.
                    std::set<std::string> visiting;
                    bool reported = false;
                    widen::TypeRef u = resolveTypeRef(tree, tree.entries[id].slids_type,
                        visiting, reported, operand.file_id, operand.tok, diag);
                    e.return_type = widen::deepStrip(u);
                    return;
                }
                if (k == parse::EntryKind::kLocalVar
                    || k == parse::EntryKind::kConst) {
                    if (qualified) operand.resolved_entry_id = id;  // already resolved
                    else resolveExpr(tree, operand, diag, /*unevaluated=*/true);  // read-mark, no DA
                    return;
                }
                if (!e.quiet_diag) {
                    char const* what = (k == parse::EntryKind::kFunction)
                        ? "function" : "namespace";
                    diagnostic::report(diag, {operand.file_id, operand.tok,
                        "'" + operand.name + "' is a " + what
                            + ", not a value or an alias.", {}});
                }
                return;
            }
            // qualified: resolveQualifiedRef already reported the resolution error.
            // quiet_diag (`#x`): the sibling `^x` reports the unresolved operand.
            if (!qualified && !e.quiet_diag) {
                diagnostic::report(diag, {operand.file_id, operand.tok,
                    "'" + operand.name + "' is not a value or an alias.", {}});
            }
            return;
        }
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral:
        case parse::Kind::kNullptrLiteral:
            return;
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kStoreStmt:
        case parse::Kind::kMoveStmt:
        case parse::Kind::kSwapStmt:
        case parse::Kind::kDestructureStmt:
        case parse::Kind::kDeleteStmt:
        case parse::Kind::kDtorCallStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kExprStmt:
        case parse::Kind::kAliasDecl:
        case parse::Kind::kNamespaceDecl:
        case parse::Kind::kClassDef:
        case parse::Kind::kEnumDecl:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kForArrayStmt:
        case parse::Kind::kForTupleStmt:
        case parse::Kind::kForRangedStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kGlobalScopeStmt:
        case parse::Kind::kSwitchStmt:
        case parse::Kind::kCaseClause:
        case parse::Kind::kParam:
            assert(false && "resolveExpr: not an expression kind");
            return;
    }
}

// Write target (assign / aug-assign lhs) — must point at a LocalVar entry.
bool resolveAssignTarget(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int id = isQualified(s) ? resolveQualifiedRef(tree, s, diag)
                            : resolveName(tree, s.name);
    if (id < 0) {
        if (!isQualified(s)) {
            diagnostic::report(diag, {s.file_id, s.tok,
                "Cannot assign to undeclared variable '" + s.name + "'.", {}});
        }
        return false;
    }
    parse::Entry const& entry = tree.entries[id];
    // Allowlist: a local variable (incl. a param) or a GLOBAL variable is an
    // assignable lvalue. Every other kind rejects — the kind is named by the shared
    // entryKindNoun (class -> "class", enum -> "enum", ...), matching the funnel's
    // reuse-reject and resolveCallTarget word-for-word.
    if (entry.kind != parse::EntryKind::kLocalVar
        && entry.kind != parse::EntryKind::kGlobalVar) {
        std::string what = entryKindNoun(entry);
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Cannot assign to " + what + " '" + s.name + "'.",
            {{entry.file_id, entry.tok, what + " declared here"}}});
        return false;
    }
    if (entry.kind == parse::EntryKind::kLocalVar)
        noteCapture(tree, id);   // an assigned host local is a (by-ref) capture
    s.resolved_entry_id = id;
    return true;
}

// Call target — must point at a Function entry.
bool resolveCallTarget(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int id;
    if (isQualified(s)) {
        id = resolveQualifiedRef(tree, s, diag);
        if (id < 0) return false;
    } else {
        id = resolveName(tree, s.name);
        if (id < 0) {
            if (reportPrivateNameInInline(tree, s.name, s.file_id, s.tok,
                                          "calls", diag)) {
                return false;
            }
            if (namespaceMemberExists(tree, s.name)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "'" + s.name + "' needs a namespace qualifier.", {}});
            } else {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Unknown function '" + s.name + "'.", {}});
            }
            return false;
        }
    }
    parse::Entry const& entry = tree.entries[id];
    // A name resolving to a CLASS is a `Class(args)` construction, not a function
    // call — accept it (resolveUserCall branches to construction on this). The
    // arity is checked against the class fields in classify, not here.
    if (entry.kind == parse::EntryKind::kClass) {
        // Inside a class template's own instantiation, its bare name constructs
        // THE INSTANCE (self-construction in a method body).
        if (entry.is_template) {
            for (auto it = tree.tmpl_self_stack.rbegin();
                 it != tree.tmpl_self_stack.rend(); ++it) {
                if (it->tmpl_entry == id) { id = it->instance_entry; break; }
            }
        }
        s.resolved_entry_id = id;
        s.is_construction = true;
        return true;
    }
    // A bare `Name;` statement. A class constructs (above); a VALUE is a discarded read
    // (rewritten in resolveUserCall before reaching here). What remains: a FUNCTION name
    // is a call missing its `()` (never silently invoked); a namespace / type is not a
    // statement.
    if (s.parenless) {
        if (entry.kind == parse::EntryKind::kFunction) {
            diagnostic::report(diag, {s.file_id, s.name_tok,
                "Function call is missing parameter list '()'.", {}});
        } else {
            diagnostic::report(diag, {s.file_id, s.name_tok,
                "'" + s.name + "' is not a statement.", {}});
        }
        return false;
    }
    if (entry.kind != parse::EntryKind::kFunction) {
        std::string what = entryKindNoun(entry);
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "'" + s.name + "' is a " + what + ", not a function.",
            {{entry.file_id, entry.tok, what + " declared here"}}});
        return false;
    }
    s.resolved_entry_id = id;
    return true;
}

// User-function call (statement or expression form): resolve the target,
// check arity against the entry's param list, and cache return + param types
// for downstream stages. Then recurse into the argument expressions.
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    // A bare `Name;` STATEMENT (parenless, unqualified) that resolves to a VALUE
    // (variable / param / const) is a discarded READ — it "uses" the name (so the
    // unused-local sweep stays quiet) and evaluates to nothing. Rewrite it to a
    // kExprStmt holding the read; the pipeline evaluates-and-discards, exactly like a
    // postfix `arr[0];`. (A class name constructs, a function is a call missing its
    // `()`, a namespace/type is not a statement — all fall through to resolveCallTarget.)
    if (s.parenless && !isQualified(s)) {
        int id = resolveName(tree, s.name);
        if (id >= 0) {
            parse::EntryKind k = tree.entries[id].kind;
            if (k == parse::EntryKind::kLocalVar || k == parse::EntryKind::kConst) {
                auto rd = std::make_unique<parse::Node>();
                rd->kind = parse::Kind::kIdentExpr;
                rd->name = s.name;
                rd->name_tok = s.name_tok;
                rd->file_id = s.file_id;
                rd->tok = s.tok;
                s.kind = parse::Kind::kExprStmt;
                s.name.clear();
                s.children.clear();
                s.children.push_back(std::move(rd));
                resolveExpr(tree, *s.children[0], diag);
                return;
            }
        }
    }
    // A base-qualified call STATEMENT (`Base:method();`) is the static bypass: reframe it
    // to a method call on the base subobject and mark it (bypass_virtual) so desugar skips
    // vtable dispatch. Must run BEFORE resolveCallTarget's bare-method rewrite, which would
    // otherwise drop the `Base:` and rebind it as a dispatched `self.method()`. (The
    // expression form is handled by tryResolveBaseQualifier inside resolveExpr.)
    if (tryResolveBaseQualifier(tree, s, diag, /*unevaluated=*/false)) return;
    if (resolveCallTarget(tree, s, diag)) {
        // `Class(args)` construction: resolveCallTarget marked it (is_construction)
        // and stamped the class entry. There is no function arity / method / nested
        // logic to apply — classify validates the args against the class fields and
        // builds the per-field construction tuple. Just resolve the arg expressions
        // so names used in the args bind.
        if (s.is_construction) {
            // A class-TEMPLATE construction `Vec<int>(args)`: the type-list is
            // required; resolve it in the call's scope, canonicalize, instantiate
            // (memoized), and retarget the node at the instance — an ordinary
            // class construction from here on.
            if (tree.entries[s.resolved_entry_id].is_template) {
                int tid = s.resolved_entry_id;
                if (s.tmpl_args.empty()) {
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "Class template '" + s.name
                        + "' requires a type-argument list.", {}});
                    return;
                }
                auto ti = tree.templates.find(tid);
                if (ti == tree.templates.end()) return;   // registration errored
                if (s.tmpl_args.size() != ti->second.def->type_params.size()) {
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "Wrong number of template arguments for '" + s.name + "': "
                        + std::to_string(ti->second.def->type_params.size())
                        + " expected, got " + std::to_string(s.tmpl_args.size())
                        + ".", {}});
                    return;
                }
                resolveTmplArgs(tree, s, diag);
                std::vector<widen::TypeRef> args;
                for (widen::TypeRef a : s.tmpl_args)
                    args.push_back(widen::removeConst(widen::deepStrip(a)));
                int iid = instantiateClassTemplate(tree, tid, args,
                                                   s.file_id, s.name_tok, diag);
                if (iid < 0) return;
                s.resolved_entry_id = iid;
                s.tmpl_args.clear();
                s.tmpl_arg_toks.clear();
            } else if (!s.tmpl_args.empty()) {
                diagnostic::report(diag, {s.file_id, s.name_tok,
                    "'" + s.name + "' is not a template class.", {}});
                return;
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        }
        parse::Entry const& callee = tree.entries[s.resolved_entry_id];
        // Explicit template arguments: only a template callee takes them; resolve
        // each in the CALL's scope (aliases, local classes) and check the count
        // against the template-list here, where the definition is at hand.
        if (!s.tmpl_args.empty() && !callee.is_template) {
            diagnostic::report(diag, {s.file_id, s.name_tok,
                "'" + s.name + "' is not a template function.", {}});
            return;
        }
        if (callee.is_template && !s.tmpl_args.empty()) {
            auto ti = tree.templates.find(s.resolved_entry_id);
            if (ti != tree.templates.end()
                && s.tmpl_args.size() != ti->second.def->type_params.size()) {
                diagnostic::report(diag, {s.file_id, s.name_tok,
                    "Wrong number of template arguments for '" + s.name + "': "
                    + std::to_string(ti->second.def->type_params.size())
                    + " expected, got " + std::to_string(s.tmpl_args.size()) + ".", {}});
                return;
            }
            resolveTmplArgs(tree, s, diag);
        }
        // A bare call that resolved to a sibling class METHOD (its owner frame is
        // a class). Author-speak: an implicit `self.method(args)`. Compiler-speak:
        // a method call on the receiver object `_$recv^`. Rewrite it to exactly
        // that — the receiver built from `_$recv` directly (buildRecvDeref),
        // mirroring the bare-FIELD rewrite, so a bare method call has the SAME
        // reach as a bare field access. classify binds the method against the
        // receiver's class + arity-checks; lowering prepends `_$recv` as the
        // implicit receiver. (The name resolved only because we are lexically
        // inside the class body, where `_$recv` is in scope; a member shadowed by
        // a local never reaches here and needs the explicit `self.method()` form.)
        if (callee.kind == parse::EntryKind::kFunction
            && callee.owner_ns_frame >= 0
            && parse::classEntryForFrame(tree, callee.owner_ns_frame) >= 0
            && !callee.is_foreign) {   // a class-scoped FOREIGN import is a namespace-style
                                       // member (no `self`), not a method — a plain call
            s.children.insert(s.children.begin(), buildRecvDeref(s.file_id, s.tok));
            s.kind = parse::Kind::kMethodCallStmt;
            s.resolved_entry_id = -1;   // classify re-binds via class-member lookup
            for (auto& ch : s.children)
                if (ch) resolveExpr(tree, *ch, diag);
            return;
        }
        bool callee_nested = callee.kind == parse::EntryKind::kFunction
                          && callee.parent_frame_id != kGlobalFrame;
        if (tree.capture_floor >= 0 && tree.capture_node) {
            // Inside a nested function: calling another nested function (a
            // sibling) is unsupported; self-recursion is fine.
            if (callee_nested
                && s.resolved_entry_id != tree.capture_node->resolved_entry_id) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Calling a sibling nested function is not supported.", {}});
            }
        } else if (callee_nested) {
            // Host-level call to a nested function: every captured host variable
            // must be definitely-assigned here. Deferred — the callee's captures
            // are known only after its body resolves (it may be defined later).
            tree.nested_call_checks.push_back(
                {s.resolved_entry_id, tree.initialized_locals, s.file_id, s.tok});
        }
        // Count same-name free-function overloads (unqualified calls only). With
        // more than one, the pick needs argument TYPES, which only exist after
        // inference — defer the arity check + signature cache to classify. A
        // single candidate keeps the precise arity error here.
        int overloads = 0;
        if (!isQualified(s)) {
            for (parse::Entry const& e : tree.entries) {
                if (e.kind == parse::EntryKind::kFunction && e.owner_ns_frame < 0
                    && e.name == s.name) {
                    overloads++;
                }
            }
        }
        if (overloads <= 1) {
            parse::Entry const& entry = tree.entries[s.resolved_entry_id];
            std::size_t total = entry.param_types.size();
            std::size_t req = (std::size_t)entry.num_required;
            if (s.children.size() < req || s.children.size() > total) {
                std::string want = (req == total)
                    ? std::to_string(total)
                    : std::to_string(req) + " to " + std::to_string(total);
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Function '" + s.name + "' expects " + want
                    + " arguments, got "
                    + std::to_string(s.children.size()) + ".", {}});
            } else {
                s.return_type = entry.slids_type;
                s.param_types = entry.param_types;
            }
        }
    }
    for (auto& ch : s.children) {
        if (ch) resolveExpr(tree, *ch, diag);
    }
}

// Whether a statement (or statement list) can fall through to its successor.
// Abrupt = it always transfers control elsewhere (return / break / continue).
// Drives the if/else join (an abrupt arm doesn't constrain the post-merge
// init-set) and 2A unreachable-statement detection.
enum class Completion { Normal, Abrupt };

// Intersection of two definite-assignment init-sets (a "must" join).
std::set<int> intersectInit(std::set<int> const& a, std::set<int> const& b) {
    std::set<int> out;
    for (int id : a) {
        if (b.count(id) > 0) out.insert(id);
    }
    return out;
}

// Fold one break/continue point's init-set into a loop-frame accumulator. The
// first fold seeds it (top ∩ X = X); later folds intersect. `seen` carries the
// top sentinel so "no break/continue yet" stays distinct from "intersected to {}".
void foldLoopExit(std::set<int>& accum, bool& seen, std::set<int> const& cur) {
    if (!seen) { accum = cur; seen = true; }
    else        accum = intersectInit(accum, cur);
}

// Resolve a statement list, threading Completion: once a statement is Abrupt,
// the next reachable statement is flagged "Unreachable statement." (2A) and the
// rest of the list is skipped (dead code declares no locals). Returns Abrupt if
// any statement in the list completes abruptly.
Completion resolveStmtList(parse::Tree& tree,
                           std::vector<std::unique_ptr<parse::Node>>& stmts,
                           diagnostic::Sink& diag);
Completion resolveStmt(parse::Tree& tree, parse::Node& s,
                       diagnostic::Sink& diag);

// Understand `for (var : arr) {body}` over a fixed-size 1-D array WITHOUT
// lowering: register the loop var bound to the element, resolve the body with it
// in scope, run definite-assignment, and validate (one-dimensional; a by-ref
// var's base type must equal the element type). desugar builds the `_$idx`
// counter + the per-iteration element binding (by ref `v = ^arr[i]`, by value
// `v = arr[i]`, or typeless by value reusing/declaring v). The node is re-tagged
// kForArrayStmt; s.children stay [loop-var decl, array ident, body].

// Bind a for-loop variable through registerDeclarator: normal lexical scoping (a for-var
// may shadow an enclosing const / fn / class), a typeless one REUSES a visible local
// (DeclareOrReuse). The loop frame is already pushed, so the for-var lives in its own
// scope and an enclosing same-name entry is a legal shadow. `infer_type` types a freshly-
// declared typeless var (the element type, or kNoType when classify infers it). Sets the
// loop-var node and flags a reuse as a store.
void bindForVar(parse::Tree& tree, parse::Node& var_decl, widen::TypeRef infer_type,
                diagnostic::Sink& diag) {
    bool typeless = (var_decl.return_type == widen::kNoType);
    DeclInfo d;
    d.name = var_decl.name;
    d.file_id = var_decl.file_id;
    d.name_tok = var_decl.name_tok;
    d.type = typeless ? infer_type : var_decl.return_type;
    bool reused = false;
    var_decl.resolved_entry_id = registerDeclarator(
        tree, d, typeless ? BindMode::DeclareOrReuse : BindMode::Declare,
        reused, diag);
    if (reused) var_decl.kind = parse::Kind::kAssignStmt;
}

Completion understandForArray(parse::Tree& tree, parse::Node& s, int arr_id,
                              diagnostic::Sink& diag) {
    parse::Node& arr_ref = *s.children[1];
    parse::Node& var_decl = *s.children[0];
    // Element type + length come off the array handle STRUCTURALLY (one subscript
    // strips the first dim). strip() sees through an alias-typed array.
    widen::TypeRef arrS = widen::strip(tree.entries[arr_id].slids_type);
    widen::TypeRef aelem = widen::get(arrS).elem;
    std::vector<int> adims = widen::get(arrS).dims;
    widen::TypeRef elemRef = (adims.size() <= 1)
        ? aelem
        : widen::internArray(aelem, std::vector<int>(adims.begin() + 1, adims.end()));
    int afile = arr_ref.file_id, atok = arr_ref.tok;
    // A multi-dim array iterates its OUTER dim; each element is the (N-1)-D
    // sub-array `elemRef` — a non-primitive element, forced by-ref below.
    int vfile = var_decl.file_id, vtok = var_decl.name_tok;
    // Resolve the loop var's declared type (alias leaf) so it matches the
    // already-resolved element type and the desugared binding carries the alias.
    if (var_decl.return_type != widen::kNoType) {
        std::set<std::string> visiting;
        bool reported = false;
        var_decl.return_type = resolveTypeRef(tree, var_decl.return_type, visiting,
                                              reported, vfile, vtok, diag);
    }
    // A const-expression dim in the loop var's TYPE (a tuple slot) — resolve in the
    // enclosing scope (before the loop frame) so constfold folds + bakes it.
    for (auto& d : var_decl.dim_exprs)
        if (d) resolveExpr(tree, *d, diag);
    bool by_ref = isReferenceType(var_decl.return_type);
    if (by_ref) {
        // A by-ref loop var `T^` references the element type T (pointee == elem).
        widen::TypeRef pointeeRef = widen::get(widen::strip(var_decl.return_type)).pointee;
        if (widen::strip(pointeeRef) != widen::strip(elemRef)) {
            // The loop var is a reference whose pointee ISN'T the element. If the
            // loop var type IS the element type, this is a by-VALUE copy of a
            // reference-typed element (`int^ v : int^[]` — a by-ref binding would
            // need `int^^ v`); otherwise it's a genuine mismatch.
            if (widen::deepStrip(var_decl.return_type) == widen::deepStrip(elemRef)) {
                by_ref = false;
            } else {
                diagnostic::report(diag, {vfile, vtok,
                    "Loop variable type '" + widen::spellOrEmpty(var_decl.return_type)
                        + "' does not match the array element type '"
                        + widen::spell(elemRef) + "'.", {}});
                return Completion::Normal;
            }
        }
    }
    // A NON-PRIMITIVE element (a sub-array of a multi-dim array, a tuple, or a
    // slid) can't be bound by value — it must be iterated by REFERENCE. A typeless
    // loop var is FORCED to a reference (`^arr[i]`); a declared by-value one errors.
    widen::Type::Form ef = widen::form(widen::strip(elemRef));
    bool elem_aggregate = (ef == widen::Type::Form::kArray
                        || ef == widen::Type::Form::kTuple
                        || ef == widen::Type::Form::kSlid);
    if (elem_aggregate && !by_ref) {
        if (var_decl.return_type != widen::kNoType) {
            diagnostic::report(diag, {vfile, vtok,
                "A for-loop over an array with non-primitive elements must use a "
                "reference loop variable.", {}});
            return Completion::Normal;
        }
        var_decl.return_type = widen::internPointer(elemRef);   // -> `^arr[i]`
        by_ref = true;
    }
    // Stamp the ident (classify asserts resolve did) and apply the definite-
    // assignment side effects the desugared element binding used to carry:
    //  - by REFERENCE (`^arr[_$idx]`): the array is FILLED through element
    //    references — it need not be initialized first, and is marked assigned.
    //  - by VALUE (`arr[_$idx]`): each element is READ, so the array must already
    //    be initialized (mirrors the read-before-write check). A GLOBAL has static /
    //    lazy storage — always initialized (its first touch constructs it) — so the
    //    definite-assignment check does not apply to it.
    // Either way the array is read every iteration, so it is never unused.
    arr_ref.resolved_entry_id = arr_id;
    if (by_ref) {
        tree.assigned_arrays.insert(arr_id);
    } else if (tree.entries[arr_id].kind != parse::EntryKind::kGlobalVar
            && tree.assigned_arrays.count(arr_id) == 0) {
        diagnostic::report(diag, {afile, atok,
            "Use of uninitialized variable '" + tree.entries[arr_id].name + "'.",
            {}});
    }
    tree.read_locals.insert(arr_id);
    // Scope the loop var to the loop, resolve the body with it bound to an element.
    parse::pushFrame(tree);
    std::vector<int> saved_body_locals = std::move(tree.body_locals);
    tree.body_locals.clear();
    std::set<int> entry_set = tree.initialized_locals;   // S (possibly-zero body)
    // Bind the loop var through the shared registrar (typeless declares as the element).
    bindForVar(tree, var_decl, elemRef, diag);
    // The loop var is (re)bound at the top of every iteration, so the body sees
    // it initialized; whether it is READ is left to the body (an unused loop var
    // is swept like any other).
    if (var_decl.resolved_entry_id >= 0) {
        tree.initialized_locals.insert(var_decl.resolved_entry_id);
    }
    tree.loop_stack.push_back({});
    tree.loop_stack.back().name = s.label.empty() ? "for" : s.label;
    resolveStmt(tree, *s.children[2], diag);   // body
    tree.loop_stack.pop_back();
    sweepUnusedLocals(tree, diag);
    tree.body_locals = std::move(saved_body_locals);
    parse::popFrame(tree);
    tree.initialized_locals = std::move(entry_set);   // possibly-zero: after = S
    s.kind = parse::Kind::kForArrayStmt;   // understood; desugar lowers it
    return Completion::Normal;
}

// Best-effort tuple/array type of a for-iterable, derived at RESOLVE time (before
// classify) for the forms whose type is structural: a variable, a deref of a
// typed pointer (`ref^`), an array element / tuple slot index, a function call's
// return. Returns kNoType when the type isn't resolvable here (e.g. a computed
// tuple rvalue — that needs classify). Only LOOKS NAMES UP (resolveName /
// resolveCallTarget), so the iterable's real resolution still happens when the
// rebuilt kForLongStmt is re-resolved.
widen::TypeRef peekIterableType(parse::Tree& tree, parse::Node& e,
                                diagnostic::Sink& diag) {
    // if/else (not a switch — -Wswitch-enum would demand every parse::Kind).
    if (e.kind == parse::Kind::kIdentExpr) {
        int id = isQualified(e) ? resolveQualifiedRef(tree, e, diag)
                                : resolveName(tree, e.name);
        if (id < 0 || tree.entries[id].kind != parse::EntryKind::kLocalVar)
            return widen::kNoType;
        return tree.entries[id].slids_type;
    }
    if (e.kind == parse::Kind::kDerefExpr) {
        widen::TypeRef b = widen::strip(peekIterableType(tree, *e.children[0], diag));
        if (widen::form(b) == widen::Type::Form::kPointer
            || widen::form(b) == widen::Type::Form::kIterator) {
            return widen::get(b).pointee;
        }
        return widen::kNoType;
    }
    if (e.kind == parse::Kind::kIndexExpr) {
        widen::TypeRef b = widen::strip(peekIterableType(tree, *e.children[0], diag));
        if (widen::form(b) == widen::Type::Form::kArray) {
            std::vector<int> dims = widen::get(b).dims;
            if (dims.size() <= 1) return widen::get(b).elem;
            return widen::internArray(widen::get(b).elem,
                std::vector<int>(dims.begin() + 1, dims.end()));
        }
        if (widen::form(b) == widen::Type::Form::kTuple
            && e.children[1]->kind == parse::Kind::kIntLiteral) {
            long k = std::strtol(e.children[1]->text.c_str(), nullptr, 10);
            std::vector<widen::TypeRef> const& slots = widen::get(b).slots;
            if (k >= 0 && k < static_cast<long>(slots.size())) return slots[k];
        }
        return widen::kNoType;
    }
    if (e.kind == parse::Kind::kCallExpr) {
        // A function entry's slids_type IS its return type (parse::Entry).
        if (!resolveCallTarget(tree, e, diag)) return widen::kNoType;
        return tree.entries[e.resolved_entry_id].slids_type;
    }
    return widen::kNoType;
}

// Understand `for (v : <iterable>) {body}` over a homogeneous tuple WITHOUT
// lowering: validate (non-literal homogeneity), register the loop var bound to a
// slot, resolve the iterable + body, run definite-assignment, and re-tag the node
// kForTupleStmt. The iterable is a tuple LITERAL (is_literal), an LVALUE (a var /
// `ref^` deref / index — iterated IN PLACE), or an rvalue call (SPILLED to a temp
// in desugar). desugar builds the `_$idx` counter, the `_$iter` walking iterator
// (as `<T[]><void^>` of the storage address, so a `ref^` iterable dodges
// addr-of-through-deref) and the per-iteration binding. A literal's slot
// homogeneity is checked in classify (slot types aren't known here); `tuple_type`
// is kNoType for a literal.
Completion understandForTuple(parse::Tree& tree, parse::Node& s,
                              widen::TypeRef tuple_type, bool is_literal,
                              bool is_lvalue, diagnostic::Sink& diag) {
    parse::Node& it_ref = *s.children[1];     // the iterable expression
    parse::Node& var_decl = *s.children[0];
    int ifile = it_ref.file_id, itok = it_ref.tok;
    (void)is_lvalue;   // desugar re-derives spill-vs-in-place from it_ref's kind
    // A non-literal with a KNOWN type has its slot types here, so check homogeneity
    // (the iterator strides by one slot type). A literal — or a TYPELESS local,
    // kNoType until classify — defers the check (and the is-it-a-tuple guard).
    if (!is_literal && tuple_type != widen::kNoType) {
        std::vector<widen::TypeRef> slots = widen::get(widen::strip(tuple_type)).slots;
        for (std::size_t i = 1; i < slots.size(); i++) {
            if (widen::deepStrip(slots[i]) != widen::deepStrip(slots[0])) {
                diagnostic::report(diag, {ifile, itok,
                    "A for-loop over a tuple requires a homogeneous tuple.", {}});
                return Completion::Normal;
            }
        }
    }
    int vfile = var_decl.file_id, vtok = var_decl.name_tok;
    if (var_decl.return_type != widen::kNoType) {   // resolve alias leaves in T^
        std::set<std::string> visiting;
        bool reported = false;
        var_decl.return_type = resolveTypeRef(tree, var_decl.return_type, visiting,
                                              reported, vfile, vtok, diag);
    }
    // A const-expression dim in the loop var's TYPE (a tuple slot) — resolve in the
    // enclosing scope (before the loop frame) so constfold folds + bakes it.
    for (auto& d : var_decl.dim_exprs)
        if (d) resolveExpr(tree, *d, diag);
    // Resolve the iterable: an lvalue (var / `ref^` / index) is read + init-checked
    // + use-marked; a literal's / call's sub-expressions resolve.
    resolveExpr(tree, it_ref, diag);
    // Scope the loop var to the loop; resolve the body with it bound to a slot.
    parse::pushFrame(tree);
    std::vector<int> saved_body_locals = std::move(tree.body_locals);
    tree.body_locals.clear();
    std::set<int> entry_set = tree.initialized_locals;   // S (possibly-zero body)
    // Bind the loop var through the shared registrar (typeless tuple slot type is
    // classify-inferred from slot 0, so infer_type is kNoType).
    bindForVar(tree, var_decl, widen::kNoType, diag);
    if (var_decl.resolved_entry_id >= 0) {
        tree.initialized_locals.insert(var_decl.resolved_entry_id);
    }
    tree.loop_stack.push_back({});
    tree.loop_stack.back().name = s.label.empty() ? "for" : s.label;
    resolveStmt(tree, *s.children[2], diag);   // body
    tree.loop_stack.pop_back();
    sweepUnusedLocals(tree, diag);
    tree.body_locals = std::move(saved_body_locals);
    parse::popFrame(tree);
    tree.initialized_locals = std::move(entry_set);   // possibly-zero: after = S
    s.kind = parse::Kind::kForTupleStmt;   // understood; desugar lowers it
    return Completion::Normal;
}

// ---- for-class: iterate a user class by protocol -------------------------------
// A class is iterable if it defines begin/end/next (arity 0/0/1, all returning the
// SAME type, which next also takes) OR size/op[] (arity 0/1, op[] returning a
// reference). Unlike array/tuple (understood here, lowered in desugar), for-class
// LOWERS AT RESOLVE — it rebuilds the node as a kForLongStmt over METHOD CALLS and
// re-resolves it, so classify's ordinary pass infers the calls (a call synthesized
// in desugar would never be classified). Same model as enum-for.

// Find a class METHOD by name across the class's own frame and its bases (most-
// derived first); -1 if none. Name-based (not overload-aware): the protocol methods
// are identified by name, and a malformed one is caught by the arity/return checks.
int findClassMethodByName(parse::Tree const& tree, widen::TypeRef cls,
                          std::string const& name) {
    for (int fr : parse::classAndBaseFrames(tree, cls)) {
        int id = parse::findMemberDeclared(tree, fr, name);
        if (id >= 0 && tree.entries[id].kind == parse::EntryKind::kFunction)
            return id;
    }
    return -1;
}

// The classification of one for-class protocol family. A method's param_types
// carries the `_$recv` receiver at [0], so user-arity == param_types.size() - 1.
struct ForClassProto {
    enum Status { Absent, Bad, Good } status = Absent;
    std::string reason;
    widen::TypeRef ret = widen::kNoType;   // begin's iterator / op[]'s element ref
};

ForClassProto classifyBeginEndNext(parse::Tree const& tree, widen::TypeRef cls,
                                   std::string const& cname) {
    int b = findClassMethodByName(tree, cls, "begin");
    int e = findClassMethodByName(tree, cls, "end");
    int n = findClassMethodByName(tree, cls, "next");
    ForClassProto d;
    int present = (b >= 0) + (e >= 0) + (n >= 0);
    if (present == 0) return d;                       // Absent
    if (present < 3) {
        std::string missing;
        auto add = [&](char const* nm) {
            if (!missing.empty()) missing += ", ";
            missing += nm;
        };
        if (b < 0) add("begin");
        if (e < 0) add("end");
        if (n < 0) add("next");
        d.status = ForClassProto::Bad;
        d.reason = "Type '" + cname + "' defines some of begin/end/next but not all; "
                   "missing " + missing + ".";
        return d;
    }
    if (tree.entries[b].param_types.size() != 1
        || tree.entries[e].param_types.size() != 1
        || tree.entries[n].param_types.size() != 2) {
        d.status = ForClassProto::Bad;
        d.reason = "Methods begin/end/next on type '" + cname
                 + "' must have arity 0/0/1.";
        return d;
    }
    widen::TypeRef rb = tree.entries[b].slids_type;
    if (widen::strip(rb) != widen::strip(tree.entries[e].slids_type)
        || widen::strip(rb) != widen::strip(tree.entries[n].slids_type)
        || widen::strip(rb) != widen::strip(tree.entries[n].param_types[1])) {
        d.status = ForClassProto::Bad;
        d.reason = "Methods begin/end/next on type '" + cname
                 + "' must all return, and next must take, the same type.";
        return d;
    }
    d.status = ForClassProto::Good;
    d.ret = rb;
    return d;
}

ForClassProto classifySizeOpIndex(parse::Tree const& tree, widen::TypeRef cls,
                                  std::string const& cname) {
    int sz = findClassMethodByName(tree, cls, "size");
    int op = findClassMethodByName(tree, cls, "op[]");
    ForClassProto d;
    if (sz < 0 && op < 0) return d;                   // Absent
    if (sz < 0 || op < 0) {
        d.status = ForClassProto::Bad;
        d.reason = "Type '" + cname + "' defines "
                 + std::string(op >= 0 ? "op[] but not size." : "size but not op[].");
        return d;
    }
    if (tree.entries[sz].param_types.size() != 1
        || tree.entries[op].param_types.size() != 2) {
        d.status = ForClassProto::Bad;
        d.reason = "Methods size/op[] on type '" + cname + "' must have arity 0/1.";
        return d;
    }
    if (!isReferenceType(tree.entries[op].slids_type)) {
        d.status = ForClassProto::Bad;
        d.reason = "Method op[] on type '" + cname + "' must return a reference.";
        return d;
    }
    d.status = ForClassProto::Good;
    d.ret = tree.entries[op].slids_type;             // element reference T^
    return d;
}

Completion understandForClass(parse::Tree& tree, parse::Node& s,
                              widen::TypeRef cls, diagnostic::Sink& diag) {
    parse::Node& var_decl = *s.children[0];
    parse::Node& cont = *s.children[1];
    int cfile = cont.file_id, ctok = cont.tok;
    int vfile = var_decl.file_id, vtok = var_decl.name_tok;
    std::string cname = widen::spellOrEmpty(cls);

    if (var_decl.return_type != widen::kNoType) {     // resolve alias leaf
        std::set<std::string> visiting; bool reported = false;
        var_decl.return_type = resolveTypeRef(tree, var_decl.return_type, visiting,
                                              reported, vfile, vtok, diag);
    }
    bool has_type = (var_decl.return_type != widen::kNoType);
    bool lv_is_ref = has_type && isReferenceType(var_decl.return_type);

    ForClassProto bnn = classifyBeginEndNext(tree, cls, cname);
    ForClassProto soi = classifySizeOpIndex(tree, cls, cname);
    bool bnnGood = bnn.status == ForClassProto::Good;
    bool soiGood = soi.status == ForClassProto::Good;

    if (!bnnGood && !soiGood) {
        if (bnn.status == ForClassProto::Bad || soi.status == ForClassProto::Bad) {
            std::string msg;
            if (bnn.status == ForClassProto::Bad) msg += bnn.reason;
            if (soi.status == ForClassProto::Bad) {
                if (!msg.empty()) msg += " ";
                msg += soi.reason;
            }
            diagnostic::report(diag, {cfile, ctok, msg, {}});
        } else {
            diagnostic::report(diag, {cfile, ctok, "Type '" + cname
                + "' is not iterable: it defines neither begin/end/next nor "
                  "size/op[].", {}});
        }
        return Completion::Normal;
    }

    // Select the protocol (option D): when both are defined the loop var must be
    // explicit, and its shape picks — a value picks size/op[], a reference picks
    // begin/end/next.
    bool use_bnn;
    if (bnnGood && soiGood) {
        if (!has_type) {
            diagnostic::report(diag, {vfile, vtok, "Type '" + cname
                + "' defines both size/op[] and begin/end/next; the for-loop "
                  "variable type must be written explicitly to select a protocol.",
                {}});
            return Completion::Normal;
        }
        use_bnn = lv_is_ref;
    } else {
        use_bnn = bnnGood;
    }

    // Decide the desugar shape and finalize the loop var's type.
    //   1: begin/end/next return a VALUE  — loop var IS the iterator value
    //   2: begin/end/next return a REF, loop var by-VALUE (hidden ref + deref)
    //   3: begin/end/next return a REF, loop var by-REF (loop var IS the iterator)
    //   4: size/op[], loop var by-VALUE (var = c[i])
    //   5: size/op[], loop var by-REF  (ref = ^c[i])
    int shape;
    widen::TypeRef size_ret = widen::kNoType, count_ty = widen::kNoType;
    auto isPrim = [](widen::TypeRef t) {
        return widen::form(widen::strip(t)) == widen::Type::Form::kPrimitive; };
    if (use_bnn) {
        widen::TypeRef iter = bnn.ret;
        if (isReferenceType(iter)) {
            widen::TypeRef elem = widen::get(widen::strip(iter)).pointee;
            if (!has_type) {
                shape = isPrim(elem) ? 2 : 3;
                var_decl.return_type = isPrim(elem) ? elem : iter;
            } else {
                shape = lv_is_ref ? 3 : 2;
            }
        } else {
            if (lv_is_ref) {
                diagnostic::report(diag, {vfile, vtok, "begin/end/next on type '"
                    + cname + "' return a value; the for-loop variable cannot be a "
                      "reference.", {}});
                return Completion::Normal;
            }
            shape = 1;
            if (!has_type) var_decl.return_type = iter;
        }
    } else {
        int op_id = findClassMethodByName(tree, cls, "op[]");
        int sz_id = findClassMethodByName(tree, cls, "size");
        size_ret = tree.entries[sz_id].slids_type;
        count_ty = tree.entries[op_id].param_types[1];
        widen::TypeRef elem = widen::get(widen::strip(soi.ret)).pointee;
        if (!has_type) {
            shape = isPrim(elem) ? 4 : 5;
            var_decl.return_type = isPrim(elem) ? elem : widen::internPointer(elem);
        } else {
            shape = lv_is_ref ? 5 : 4;
        }
    }

    // ---- lower to a kForLongStmt over the protocol's method calls --------------
    int fid = s.file_id, tk = s.tok;
    std::unique_ptr<parse::Node> loopVar = std::move(s.children[0]);
    std::unique_ptr<parse::Node> contNode = std::move(s.children[1]);
    std::unique_ptr<parse::Node> body = std::move(s.children[2]);
    std::string lv = loopVar->name;
    // Synthesized locals are suffixed with the loop's token so NESTED for-class
    // loops never share a name (each is unique across the whole function).
    std::string sfx = "_" + std::to_string(tk);
    std::string sEnd = "_$fc_end" + sfx, sRef = "_$fc_ref" + sfx,
                sSize = "_$fc_size" + sfx, sCount = "_$fc_count" + sfx,
                sRecv = "_$fc_recv" + sfx;
    // A re-readable lvalue container (a var, `ptr^`, an index) is cloned per method
    // call; an rvalue (a construction / call) is SPILLED to a for-scope local built
    // ONCE, so every begin/size/next/op[] hits the same object (and it is destructed
    // when the loop scope exits). Lvalue kinds re-read with no side effect.
    bool spill_recv = !(contNode->kind == parse::Kind::kIdentExpr
                     || contNode->kind == parse::Kind::kDerefExpr
                     || contNode->kind == parse::Kind::kIndexExpr);

    auto ident = [&](std::string nm) {
        auto n = std::make_unique<parse::Node>();
        n->kind = parse::Kind::kIdentExpr; n->name = std::move(nm);
        n->file_id = fid; n->tok = tk; n->name_tok = tk;
        return n;
    };
    auto recv = [&]() -> std::unique_ptr<parse::Node> {
        return spill_recv ? ident(sRecv) : cloneExpr(*contNode);
    };
    auto mcall = [&](std::string method, std::unique_ptr<parse::Node> arg) {
        auto c = std::make_unique<parse::Node>();
        c->kind = parse::Kind::kMethodCallStmt;
        c->name = std::move(method); c->file_id = fid; c->tok = tk; c->name_tok = tk;
        c->children.push_back(recv());
        if (arg) c->children.push_back(std::move(arg));
        return c;
    };
    auto bin = [&](std::string op, std::unique_ptr<parse::Node> a,
                   std::unique_ptr<parse::Node> b) {
        auto n = std::make_unique<parse::Node>();
        n->kind = parse::Kind::kBinaryExpr; n->text = std::move(op);
        n->file_id = fid; n->tok = tk;
        n->children.push_back(std::move(a)); n->children.push_back(std::move(b));
        return n;
    };
    auto intlit = [&](char const* v) {
        auto n = std::make_unique<parse::Node>();
        n->kind = parse::Kind::kIntLiteral; n->text = v;
        n->file_id = fid; n->tok = tk;
        return n;
    };
    auto un = [&](parse::Kind k, std::unique_ptr<parse::Node> a) {
        auto n = std::make_unique<parse::Node>();
        n->kind = k; n->file_id = fid; n->tok = tk;
        n->children.push_back(std::move(a));
        return n;
    };
    auto vdecl = [&](std::string nm, widen::TypeRef ty,
                     std::unique_ptr<parse::Node> init) {
        auto n = std::make_unique<parse::Node>();
        n->kind = parse::Kind::kVarDeclStmt; n->name = std::move(nm); n->name_tok = tk;
        n->return_type = ty; n->file_id = fid; n->tok = tk;
        if (init) n->children.push_back(std::move(init));
        return n;
    };
    auto assign = [&](std::string nm, std::unique_ptr<parse::Node> rhs) {
        auto n = std::make_unique<parse::Node>();
        n->kind = parse::Kind::kAssignStmt; n->name = std::move(nm); n->name_tok = tk;
        n->file_id = fid; n->tok = tk;
        n->children.push_back(std::move(rhs));
        return n;
    };
    auto blk = [&](std::vector<std::unique_ptr<parse::Node>> stmts) {
        auto n = std::make_unique<parse::Node>();
        n->kind = parse::Kind::kBlockStmt; n->file_id = fid; n->tok = tk;
        for (auto& st : stmts) n->children.push_back(std::move(st));
        return n;
    };
    auto prepend = [&](std::unique_ptr<parse::Node> st) {
        body->children.insert(body->children.begin(), std::move(st)); };

    std::vector<std::unique_ptr<parse::Node>> varlist;
    // The rvalue container is spilled to a class local that WRAPS the loop in a
    // block scope — a for-long varlist local is not destructed at loop scope, a
    // block local is — so the temp is built once before the loop and destructed
    // when the block exits.
    std::unique_ptr<parse::Node> spillDecl;
    if (spill_recv) spillDecl = vdecl(sRecv, cls, std::move(contNode));
    std::unique_ptr<parse::Node> cond, update;

    if (shape == 1 || shape == 3) {
        // loop var IS the iterator: init = begin(), advance = next(loopvar).
        loopVar->children.clear();
        loopVar->children.push_back(mcall("begin", nullptr));
        varlist.push_back(std::move(loopVar));
        varlist.push_back(vdecl(sEnd, bnn.ret, mcall("end", nullptr)));
        cond = bin("!=", ident(lv), ident(sEnd));
        std::vector<std::unique_ptr<parse::Node>> up;
        up.push_back(assign(lv, mcall("next", ident(lv))));
        update = blk(std::move(up));
    } else if (shape == 2) {
        // hidden reference iterator; loop var = element by value.
        varlist.push_back(vdecl(sRef, bnn.ret, mcall("begin", nullptr)));
        varlist.push_back(vdecl(sEnd, bnn.ret, mcall("end", nullptr)));
        varlist.push_back(std::move(loopVar));
        cond = bin("!=", ident(sRef), ident(sEnd));
        std::vector<std::unique_ptr<parse::Node>> up;
        up.push_back(assign(sRef, mcall("next", ident(sRef))));
        update = blk(std::move(up));
        prepend(assign(lv, un(parse::Kind::kDerefExpr, ident(sRef))));
    } else {
        // size/op[]: index 0.._$fc_size-1; by-value var = c[i], by-ref ref = ^c[i].
        varlist.push_back(vdecl(sSize, size_ret, mcall("size", nullptr)));
        varlist.push_back(vdecl(sCount, count_ty, intlit("0")));
        varlist.push_back(std::move(loopVar));
        cond = bin("<", ident(sCount), ident(sSize));
        std::vector<std::unique_ptr<parse::Node>> up;
        up.push_back(assign(sCount, bin("+", ident(sCount), intlit("1"))));
        update = blk(std::move(up));
        // op[] returns the element REFERENCE. By-value derefs it (`c.op[](i)^`);
        // by-ref binds it directly (`c.op[](i)`). Built as the method call, not the
        // `c[i]` subscript sugar (which always derefs, and whose `^c[i]` address is
        // not a plain variable codegen's addr-of accepts).
        std::unique_ptr<parse::Node> elem = mcall("op[]", ident(sCount));
        if (shape == 4) elem = un(parse::Kind::kDerefExpr, std::move(elem));
        prepend(assign(lv, std::move(elem)));
    }

    auto buildForLong = [&](parse::Node& n) {
        n.kind = parse::Kind::kForLongStmt;
        n.children.clear();
        n.children.push_back(std::move(cond));     // [0]
        n.children.push_back(std::move(update));   // [1]
        n.children.push_back(std::move(body));     // [2]
        for (auto& v : varlist) n.children.push_back(std::move(v));   // [3..]
    };
    if (spill_recv) {
        // { _$fc_recv = <container>; for (...) {...} } — the block owns the temp's
        // lifetime; the loop keeps the label so break/continue still target it.
        auto forNode = std::make_unique<parse::Node>();
        forNode->file_id = fid; forNode->tok = tk; forNode->label = s.label;
        buildForLong(*forNode);
        s.kind = parse::Kind::kBlockStmt;
        s.label.clear(); s.text.clear(); s.name.clear();
        s.children.clear();
        s.children.push_back(std::move(spillDecl));
        s.children.push_back(std::move(forNode));
        return resolveStmt(tree, s, diag);
    }
    s.text.clear(); s.name.clear();
    buildForLong(s);
    return resolveStmt(tree, s, diag);
}

// Resolve a store lvalue (the target of a kStoreStmt). An index store WRITES
// its base array — mark it initialized (whole-array definite-assignment; a
// subscript write assigns the array) but don't require it and don't read-mark
// it, so a write-only array is still swept as unused — and READS its index
// expressions. A deref store READS its pointer operand (it must be initialized
// to store through it).
void resolveStoreTarget(parse::Tree& tree, parse::Node& lv,
                        diagnostic::Sink& diag) {
    if (lv.kind == parse::Kind::kIndexExpr) {
        if (lv.children[1]) resolveExpr(tree, *lv.children[1], diag);
        resolveStoreTarget(tree, *lv.children[0], diag);
        return;
    }
    if (lv.kind == parse::Kind::kIdentExpr) {
        int id = isQualified(lv) ? resolveQualifiedRef(tree, lv, diag)
                                 : resolveName(tree, lv.name);
        // A bare index/deref base that is a class FIELD lowers to `_$recv^.field` — the
        // SAME rewrite the read path gets (this was the missing site: `field[i] = v`
        // never reached `self` and errored "undeclared"). lowerFieldRef leaves a resolved
        // kFieldExpr; the enclosing index/deref store then targets the field's element.
        if (lowerFieldRef(tree, lv, id, diag)) return;
        if (id < 0) {
            if (!isQualified(lv)) {
                diagnostic::report(diag, {lv.file_id, lv.tok,
                    "Cannot assign to undeclared variable '" + lv.name + "'.",
                    {}});
            }
            return;
        }
        parse::Entry const& entry = tree.entries[id];
        if (entry.kind != parse::EntryKind::kLocalVar
            && entry.kind != parse::EntryKind::kGlobalVar) {
            diagnostic::report(diag, {lv.file_id, lv.tok,
                "Cannot assign to '" + lv.name + "'.",
                {{entry.file_id, entry.tok, "declared here"}}});
            return;
        }
        lv.resolved_entry_id = id;
        // A global has static storage — always initialized, never swept — so it needs
        // no definite-assignment / unused tracking; an element/field store just stands.
        if (entry.kind == parse::EntryKind::kGlobalVar) return;
        if (isInPlaceAggregate(entry.slids_type)) {
            // An array/tuple slot store assigns the aggregate (monotonic may-set).
            tree.assigned_arrays.insert(id);
        } else {
            // An iterator base (`it[i] = v`): the pointer value is READ to store
            // through it, so it must already be initialized — and counts as a
            // use, not a write (a store-only iterator is still unused).
            if (!isCaptureLocal(tree, id)
                && tree.initialized_locals.count(id) == 0) {
                diagnostic::report(diag, {lv.file_id, lv.tok,
                    "Use of uninitialized variable '" + lv.name + "'.", {}});
            } else {
                tree.read_locals.insert(id);
            }
        }
        return;
    }
    // A deref store (`ref^ = v`): the pointer operand is read (must be init).
    resolveExpr(tree, lv, diag);
}

// Mark the base local of an lvalue chain as READ — for an augmented assign,
// which is a read-modify-write (`arr[i] += v` reads arr[i]). resolveStoreTarget
// does only the write/may-set side, so the base would otherwise look store-only
// and trip the unused-local sweep, unlike a bare `x += v` (read-marked) or a
// deref `p^ += v` (resolveExpr marks the pointer). The walk to the innermost
// ident is idempotent for the deref case.
void markLvalueBaseRead(parse::Tree& tree, parse::Node& lv) {
    parse::Node* n = &lv;
    while (n->kind == parse::Kind::kIndexExpr
           || n->kind == parse::Kind::kFieldExpr
           || n->kind == parse::Kind::kDerefExpr) {
        if (n->children.empty() || !n->children[0]) return;
        n = n->children[0].get();
    }
    if (n->kind == parse::Kind::kIdentExpr && n->resolved_entry_id >= 0
        && tree.entries[n->resolved_entry_id].kind == parse::EntryKind::kLocalVar) {
        tree.read_locals.insert(n->resolved_entry_id);
    }
}

// Resolve an lvalue operand of a move / swap. An index/deref target reuses
// resolveStoreTarget (a store-through — its DA reads the base). A bare-name
// target is checked assignable here: `read` (swap, and the moved-from leaf) means
// it must already be initialized and counts as a use; the target always ends up
// initialized (a move/swap writes it). Only an lvalue can be addressed this way:
// BOTH swap operands and a move's LHS must be lvalues (a move's RHS may be an
// rvalue — it is read, not written, so it is resolved by resolveExpr, not here).
// A swap rhs is parsed as a general expression, so a literal / arithmetic / call
// rvalue can reach here and must be rejected (`what` names the offender — "A swap
// operand" / "A move target") rather than crash codegen with no address.
void resolveMoveSwapLvalue(parse::Tree& tree, parse::Node& lv,
                           bool read, char const* what, diagnostic::Sink& diag) {
    if (lv.kind == parse::Kind::kIndexExpr || lv.kind == parse::Kind::kDerefExpr
        || lv.kind == parse::Kind::kFieldExpr) {
        resolveStoreTarget(tree, lv, diag);
        return;
    }
    // A bare field NAME target: lower `field` -> `self.field` and store through it,
    // the SAME funnel the read / index / deref paths use (lowerFieldRef). Mirrors the
    // kAssignStmt / kAugAssignStmt bare-field rewrite; without it a field reaches
    // resolveAssignTarget's local/global-only allowlist and is wrongly rejected.
    if (lv.kind == parse::Kind::kIdentExpr && !isQualified(lv)) {
        int fid = resolveName(tree, lv.name);
        if (lowerFieldRef(tree, lv, fid, diag)) {   // fires only for a kField
            resolveStoreTarget(tree, lv, diag);
            return;
        }
    }
    if (lv.kind != parse::Kind::kIdentExpr) {
        resolveExpr(tree, lv, diag);   // resolve sub-exprs so inner errors surface
        diagnostic::report(diag, {lv.file_id, lv.tok,
            std::string(what) + " must be an lvalue.", {}});
        return;
    }
    if (!resolveAssignTarget(tree, lv, diag)) return;
    // resolveAssignTarget returns true ONLY for a kLocalVar (id >= 0); this guard
    // is defensive (unreachable in practice) so the DA below is unconditionally
    // safe. user notified, accepts state.
    int id = lv.resolved_entry_id;
    if (id < 0 || tree.entries[id].kind != parse::EntryKind::kLocalVar) return;
    if (read) {
        if (!isCaptureLocal(tree, id) && tree.initialized_locals.count(id) == 0) {
            diagnostic::report(diag, {lv.file_id, lv.tok,
                "Use of uninitialized variable '" + lv.name + "'.", {}});
        } else {
            tree.read_locals.insert(id);
        }
    }
    tree.initialized_locals.insert(id);
    if (isArrayType(tree.entries[id].slids_type)) tree.assigned_arrays.insert(id);
}

// A loop condition that is a constant-true literal: bool `true`, or a non-zero
// int/uint/char. The grammar synthesizes an empty `()` condition as bool true,
// so an empty-condition loop lands here too. Detected SYNTACTICALLY (resolve
// runs before constfold), so a named-const-true condition is not caught — that
// path keeps the possibly-completing behavior. Float conditions (exotic) are
// not treated as constant-true. Mirrors the literal side of classify's constTruth.
bool condIsConstTrue(parse::Node const& c) {
    if (c.kind == parse::Kind::kBoolLiteral
        || c.kind == parse::Kind::kIntLiteral
        || c.kind == parse::Kind::kUintLiteral
        || c.kind == parse::Kind::kCharLiteral) {
        return !c.text.empty() && c.text != "0";
    }
    return false;
}

// Resolve a destructure's slots (node.children[1..]; [0] is the rhs, handled by the
// caller). A TYPED slot declares a fresh local (dup-checked); a TYPELESS slot reuses
// an enclosing local (flipped to kAssignStmt) or declares a fresh inferred one; a
// NESTED `(...)` slot (a kDestructureStmt with no rhs) recurses; null is a discard.
// `seen` carries the names already targeted by THIS destructure (across nested
// sub-patterns); a name may appear at most once, whether it declares or reuses.
void resolveDestructureSlots(parse::Tree& tree, parse::Node& node,
                             std::map<std::string, std::pair<int, int>>& seen,
                             diagnostic::Sink& diag) {
    for (std::size_t i = 1; i < node.children.size(); i++) {
        parse::Node* slot = node.children[i].get();
        if (!slot) continue;   // discard
        if (slot->kind == parse::Kind::kDestructureStmt) {
            resolveDestructureSlots(tree, *slot, seen, diag);   // nested sub-pattern
            continue;
        }
        // One name per destructure: a repeat is a duplicate target whether it
        // would declare (`(y, y)`) or reuse an existing variable (`(a, a)`).
        if (auto it = seen.find(slot->name); it != seen.end()) {
            diagnostic::report(diag, {slot->file_id, slot->name_tok,
                "Duplicate destructure target '" + slot->name + "'.",
                {{it->second.first, it->second.second, "first targeted here"}}});
            continue;
        }
        seen.emplace(slot->name, std::make_pair(slot->file_id, slot->name_tok));
        // A typed slot DECLARES (dup-errors same-frame); a typeless slot REUSES a visible
        // local, dup-errors a same-frame non-local, else declare-inferred — both through
        // the shared registrar (SameFrame: an enclosing name is a legal shadow).
        DeclInfo d;
        d.name = slot->name;
        d.file_id = slot->file_id;
        d.name_tok = slot->name_tok;
        bool typed = (slot->return_type != widen::kNoType);
        if (typed) {
            resolveDeclType(tree, slot->return_type, slot->file_id,
                            slot->tok, diag);   // caret the TYPE, not the name
            d.type = slot->return_type;
        }   // else kNoType — classify stamps it from the slot
        bool reused = false;
        slot->resolved_entry_id = registerDeclarator(
            tree, d, typed ? BindMode::Declare : BindMode::DeclareOrReuse,
            reused, diag);
        if (reused) slot->kind = parse::Kind::kAssignStmt;   // reuse -> a store
        if (slot->resolved_entry_id >= 0)
            tree.initialized_locals.insert(slot->resolved_entry_id);
    }
}

// A bare-name UNDECLARED target of `<--` / `<-->` is an inferred move/swap-INIT,
// symmetric to `cls = e` (copy): declare a fresh inferred local through the SAME funnel
// (registerDeclarator DeclareOrReuse), flip the stmt to a kVarDeclStmt with
// default_move_init / default_swap_init, and resolve its source — so classify's inferred
// decl-init dispatch runs op<-- (or default-constructs-then-swaps), exactly like the typed
// `Class cls <-- e` form. Returns true iff it handled the stmt. A bare FIELD or an existing
// variable returns false -> the normal move/swap path (a store / a reuse). Mirrors the
// kAssignStmt inferred-init promotion so all four init shapes declare-if-fresh alike.
bool tryInferredMoveSwapInit(parse::Tree& tree, parse::Node& s, bool swap,
                             diagnostic::Sink& diag) {
    parse::Node* lhs = s.children.empty() ? nullptr : s.children[0].get();
    if (!lhs || lhs->kind != parse::Kind::kIdentExpr) return false;
    if (resolveName(tree, lhs->name) >= 0)
        return false;   // an existing variable / a bare field -> the normal path
    DeclInfo d;
    d.name = lhs->name;
    d.file_id = lhs->file_id;
    d.name_tok = lhs->name_tok;
    d.type = widen::kNoType;   // classify stamps it from the source
    bool reused = false;
    int id = registerDeclarator(tree, d, BindMode::DeclareOrReuse, reused, diag);
    if (id < 0) return true;   // non-assignable reuse rejected (diagnostic reported)
    // resolveName<0 above guarantees a fresh declare (never a reuse); the source is
    // children[1] for both forms (move [target, rhs] / swap [target, other]).
    std::string nm = lhs->name;
    int nt = lhs->name_tok;
    auto src = s.children.size() > 1 ? std::move(s.children[1]) : nullptr;
    s.kind = parse::Kind::kVarDeclStmt;
    s.name = nm;
    s.name_tok = nt;
    s.resolved_entry_id = id;
    if (swap) s.default_swap_init = true;
    else      s.default_move_init = true;
    s.children.clear();
    s.children.push_back(std::move(src));
    if (s.children[0]) resolveExpr(tree, *s.children[0], diag);
    tree.initialized_locals.insert(id);
    return true;
}

Completion resolveStmt(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            // A qualified const targeting a LOCAL class was physically moved into it by
            // relocateOutOfLineMembers; one targeting a remote NAMESPACE was registered
            // in place (registerQualifiedLeaf) and left here so constfold folds its init.
            // Either way it is already a member, not a local — skip it.
            if (isQualified(s)) return Completion::Normal;
            // Const-expression array dims (`arr[N]`, `arr[sizeof(int)]`): resolve
            // each so its const refs / sizeof resolve; constfold folds + bakes the
            // size into the type spelling (provisional `[1]` until then).
            for (auto& d : s.dim_exprs) {
                if (d) resolveExpr(tree, *d, diag);
            }
            // Consts in function bodies are pre-created in the forward-decl
            // pre-pass (resolveFunctionBody). If resolved_entry_id is set,
            // entry already exists; skip creation and dup-check.
            if (s.resolved_entry_id < 0) {
                std::string declared = widen::spellOrEmpty(s.return_type);   // pre-erasure spelling
                // A typeless const (block scope) defers type inference to constfold.
                if (s.return_type != widen::kNoType) {
                    resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag,
                                    &s.return_type_seg_toks);
                }
                // A const whose type isn't a foldable scalar is a not-mutable VARIABLE,
                // not a substituted constant (constNeedsStorage) — a local with a deep-
                // const type. A block-scope `global` is a scoped STATIC (kGlobalVar): one
                // persistent cell, static-initialized, exempt from the unused/DA tracking.
                // const is not yet enforced (Phase 6). The dup-check + addEntry funnel
                // through registerDeclarator; the kind / alias-label / body-tracking are
                // this site's specifics, passed in.
                bool needs_storage = constNeedsStorage(s.return_type);
                bool as_const = s.is_const && !needs_storage;
                DeclInfo d;
                d.name = s.name;
                d.file_id = s.file_id;
                d.name_tok = s.name_tok;   // caret at the ident, not at 'const'/type
                d.kind = s.is_global ? parse::EntryKind::kGlobalVar
                       : as_const    ? parse::EntryKind::kConst
                                     : parse::EntryKind::kLocalVar;
                // A named type (alias / enum / qualified) erased to a different underlying
                // — keep the as-declared spelling as the ##type label. Compare BEFORE the
                // const-wrap so the wrap isn't read as erasure.
                if (declared != widen::spellOrEmpty(s.return_type)) d.alias_label = declared;
                if (s.is_const && needs_storage) s.return_type = widen::deepConst(s.return_type);
                d.type = s.return_type;
                // A substituted const folds away (never swept); a scoped-global static is
                // tracked separately — only a real local goes on the body-sweep list.
                d.track_body_local = !as_const && !s.is_global;
                bool reused = false;
                s.resolved_entry_id = registerDeclarator(tree, d, BindMode::Declare,
                                                         reused, diag);
            }
            // Resolve the initializer (if any) BEFORE marking the local
            // initialized, so a self-reference reads as uninitialized
            // (`int x = x;` fires on the rhs). The kLocalVar guard intentionally
            // skips a kConst target (no definite-assignment tracking — consts
            // are required-init by grammar and substituted away) and a qualified
            // namespace-member decl (handled above, never reaches here).
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            if (s.resolved_entry_id >= 0
                && tree.entries[s.resolved_entry_id].kind
                       == parse::EntryKind::kLocalVar) {
                // A class — or an array/tuple whose leaves are classes — is
                // always constructed on declaration (its fields take default /
                // zero values), so such a decl is definitely initialized even
                // with no explicit initializer.
                bool constructed = widen::hasInPlaceClass(
                    tree.entries[s.resolved_entry_id].slids_type);
                if (!s.children.empty() || constructed) {
                    tree.initialized_locals.insert(s.resolved_entry_id);
                    // A whole-array initializer (`int a[3] = (1,2,3)`) assigns the
                    // entire array — mark it in the array may-set too.
                    if (isArrayType(tree.entries[s.resolved_entry_id].slids_type))
                        tree.assigned_arrays.insert(s.resolved_entry_id);
                }
            }
            return Completion::Normal;
        }
        case parse::Kind::kAssignStmt: {
            // A bare assignment target matching a class FIELD (no local shadows it)
            // is a field STORE through `self.field` — the write-side mirror of the
            // read rewrite. Without this it would fall into the inferred-init path
            // below and invent a phantom local that shadows the field. Becomes a
            // kStoreStmt [self.field, rhs]; the rhs's own field reads rewrite too.
            if (int fid = isQualified(s) ? -1 : resolveName(tree, s.name);
                fid >= 0 && tree.entries[fid].kind == parse::EntryKind::kField) {
                s.children.insert(s.children.begin(),
                    buildFieldLvalue(tree, s.name, fid, s.file_id, s.name_tok, diag));
                s.kind = parse::Kind::kStoreStmt;
                if (s.children.size() > 1 && s.children[1])
                    resolveExpr(tree, *s.children[1], diag);
                return Completion::Normal;
            }
            // Inferred-init / reassign: a typeless bare-name assign. The declare-vs-reuse
            // decision goes through the ONE funnel: an UNDECLARED name declares a fresh
            // local (type inferred from the rhs); an existing assignable variable reuses it
            // (falls through to the assignment path below for capture / DA); a non-assignable
            // target (const / class / enum / ...) is rejected there — the same wording an
            // explicit assignment to it raises.
            if (!isQualified(s)) {
                DeclInfo d;
                d.name = s.name;
                d.file_id = s.file_id;
                d.name_tok = s.name_tok;
                d.type = widen::kNoType;   // classify stamps it from the rhs
                bool reused = false;
                int id = registerDeclarator(tree, d, BindMode::DeclareOrReuse,
                                            reused, diag);
                if (id < 0) return Completion::Normal;   // non-assignable reuse rejected
                if (!reused) {
                    s.resolved_entry_id = id;
                    s.kind = parse::Kind::kVarDeclStmt;   // fresh: alloca + classify infer
                    // rhs BEFORE marking initialized, so `x = x` reads x uninitialized.
                    for (auto& ch : s.children) {
                        if (ch) resolveExpr(tree, *ch, diag);
                    }
                    tree.initialized_locals.insert(s.resolved_entry_id);
                    return Completion::Normal;
                }
                // an existing assignable variable -> the assignment path below.
            }
            resolveAssignTarget(tree, s, diag);
            // rhs BEFORE marking, so `x = x;` with x uninitialized still fires.
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            if (s.resolved_entry_id >= 0
                && tree.entries[s.resolved_entry_id].kind
                       == parse::EntryKind::kLocalVar) {
                tree.initialized_locals.insert(s.resolved_entry_id);
                // A whole-array assign (`a = (4,5,6)`) assigns the entire array.
                if (isArrayType(tree.entries[s.resolved_entry_id].slids_type))
                    tree.assigned_arrays.insert(s.resolved_entry_id);
            }
            return Completion::Normal;
        }
        case parse::Kind::kAugAssignStmt: {
            // A BARE aug-assign target matching a class FIELD is `self.field op= rhs`
            // — rewrite to the complex-lvalue form [self.field, rhs] (mirror of the
            // kAssignStmt field rewrite). Without this the bare name falls into
            // resolveAssignTarget and errors "undeclared variable".
            if (int fid = (s.children.size() == 1 && !isQualified(s))
                              ? resolveName(tree, s.name) : -1;
                fid >= 0 && tree.entries[fid].kind == parse::EntryKind::kField) {
                s.children.insert(s.children.begin(),
                    buildFieldLvalue(tree, s.name, fid, s.file_id, s.name_tok, diag));
                s.name.clear();
            }
            // Complex lvalue form (`arr[i] += v`): [0]=lvalue chain, [1]=rhs.
            // resolveStoreTarget does the same read-and-write DA a store does
            // (it reads the base, marks an aggregate assigned); the rhs reads.
            // The bare-name form has the target name on the node + [0]=rhs.
            if (s.children.size() == 2) {
                resolveStoreTarget(tree, *s.children[0], diag);
                markLvalueBaseRead(tree, *s.children[0]);   // RMW reads the target
                if (s.children[1]) resolveExpr(tree, *s.children[1], diag);
                return Completion::Normal;
            }
            if (resolveAssignTarget(tree, s, diag)) {
                // Cache lvalue type on the stmt so desugar's synthesized
                // IdentExpr inherits it without re-walking entries.
                s.return_type = parse::entryType(tree, s.resolved_entry_id);
                // An aug-assign READS the target before writing it, so it must
                // already be initialized (`x += 1` on an uninitialized x is an
                // error). resolveAssignTarget doesn't route through resolveExpr,
                // so check here, then mark written.
                int id = s.resolved_entry_id;
                if (tree.entries[id].kind == parse::EntryKind::kLocalVar
                    && tree.initialized_locals.count(id) == 0) {
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "Use of uninitialized variable '" + s.name + "'.", {}});
                } else if (tree.entries[id].kind
                               == parse::EntryKind::kLocalVar) {
                    // An aug-assign reads the target's value (like `x++`), so it
                    // counts as a use for the unused sweep, then re-writes it.
                    tree.read_locals.insert(id);
                    tree.initialized_locals.insert(id);
                }
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return Completion::Normal;
        }
        case parse::Kind::kStoreStmt:
            // Store through an lvalue expression: `ref^ = rhs` or `arr[i] = rhs`.
            // children[0] = lvalue (kDerefExpr / kIndexExpr), [1] = rhs.
            resolveStoreTarget(tree, *s.children[0], diag);
            if (s.children.size() > 1 && s.children[1]) {
                resolveExpr(tree, *s.children[1], diag);
            }
            return Completion::Normal;
        case parse::Kind::kMoveStmt:
            // `a <-- b;` — a is a WRITE destination (need not be pre-initialized);
            // b is a READ (the copy reads it; pointer leaves are then nulled but b
            // stays initialized and valid). rhs FIRST so `x <-- x` reads x before
            // the write. classify checks copy compatibility. A bare UNDECLARED target
            // is an inferred move-INIT (declare fresh), symmetric to `cls = e`.
            if (tryInferredMoveSwapInit(tree, s, /*swap=*/false, diag))
                return Completion::Normal;
            if (s.children[1]) resolveExpr(tree, *s.children[1], diag);
            resolveMoveSwapLvalue(tree, *s.children[0], /*read=*/false,
                                  "A move target", diag);
            return Completion::Normal;
        case parse::Kind::kSwapStmt:
            // `a <--> b;` — both operands are READ and WRITTEN (exchanged), so
            // both must already be initialized lvalues. classify checks same-type.
            // A bare UNDECLARED target is an inferred swap-INIT (declare fresh +
            // default_swap_init), symmetric to `cls = e`.
            if (tryInferredMoveSwapInit(tree, s, /*swap=*/true, diag))
                return Completion::Normal;
            resolveMoveSwapLvalue(tree, *s.children[0], /*read=*/true,
                                  "A swap operand", diag);
            resolveMoveSwapLvalue(tree, *s.children[1], /*read=*/true,
                                  "A swap operand", diag);
            return Completion::Normal;
        case parse::Kind::kDestructureStmt:
            // children[0] = rhs (read FIRST), [1..] = declarator / nested slots.
            if (s.children[0]) resolveExpr(tree, *s.children[0], diag);
            {
                std::map<std::string, std::pair<int, int>> seen;
                resolveDestructureSlots(tree, s, seen, diag);
            }
            return Completion::Normal;
        case parse::Kind::kDeleteStmt: {
            // delete <ptr>; — frees the pointer and, if it is an LVALUE, nulls it
            // back. The operand is ANY pointer EXPRESSION: a variable / field / array
            // element / tuple slot / deref (an lvalue, nulled back), or a call return
            // / op result (an rvalue, freed only). resolveExpr resolves it as a read
            // (you can't delete an uninitialized pointer; the free reads it); classify
            // checks it is a pointer type. A bad operand (non-pointer, wrong-kind
            // name) is reported by resolveExpr / classify.
            parse::Node& operand = *s.children[0];
            resolveExpr(tree, operand, diag);
            return Completion::Normal;
        }
        case parse::Kind::kDtorCallStmt: {
            // lvalue.~(); — explicit destructor call (placement cleanup, no free).
            // Resolve the receiver as a read; classify checks it is a class lvalue.
            resolveExpr(tree, *s.children[0], diag);
            return Completion::Normal;
        }
        case parse::Kind::kCallStmt: {
            if (isPrintIntrinsic(s.name)) {
                if (s.children.size() != 1) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "'" + s.name + "' takes exactly one argument.", {}});
                }
                for (auto& ch : s.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
            } else {
                resolveUserCall(tree, s, diag);
            }
            return Completion::Normal;
        }
        case parse::Kind::kMethodCallStmt: {
            // A `X:self.method()` statement is the static bypass: reframe + mark it
            // (bypass_virtual) BEFORE the children resolve — else resolving the `X:self`
            // receiver rewrites it in place and the call stays dispatched. (The
            // expression form runs through tryResolveBaseQualifier at resolveExpr's top.)
            if (tryResolveBaseQualifier(tree, s, diag, /*unevaluated=*/false))
                return Completion::Normal;
            // Resolve the receiver (children[0]) and the args (children[1..]); the
            // method name binds in classify against the receiver's class type. An
            // explicit template type-list resolves here, in the call's scope.
            resolveTmplArgs(tree, s, diag);
            for (auto& ch : s.children)
                if (ch) resolveExpr(tree, *ch, diag);
            return Completion::Normal;
        }
        case parse::Kind::kAliasDecl: {
            // Bare `alias Ns;` / `alias Ns:Sub;` (NO target) imports a namespace's
            // members into this scope — checked FIRST, so a qualified import isn't
            // mistaken for a member decl.
            if (s.return_type == widen::kNoType) {
                resolveBareAlias(tree, s, diag);
                return Completion::Normal;
            }
            // A qualified alias WITH a target (`alias C:Num = int;`) was relocated into a
            // LOCAL class, or registered into a remote NAMESPACE's frame in place — either
            // way it is already a member, so skip it here.
            if (isQualified(s)) return Completion::Normal;
            // Function-scope value alias: register in the body frame, then
            // validate the target (forward refs within a body aren't pre-scanned).
            registerAlias(tree, s, diag);
            // A body-scope alias TEMPLATE builds its PATTERN here (this is its
            // natural scope point); the node's own target stays pristine.
            if (!s.type_params.empty()) {
                validateAliasTemplate(tree, s, diag);
                return Completion::Normal;
            }
            // A const-expression dim in the alias TARGET (`alias V = int[N]`) —
            // resolve in the body frame so constfold folds + bakes + refreshes it.
            for (auto& d : s.dim_exprs) {
                if (d) resolveExpr(tree, *d, diag);
            }
            resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
            return Completion::Normal;
        }
        case parse::Kind::kNamespaceDecl: {
            // A namespace opened in a function body: NAME phase (members + any nested
            // classes, collected) -> class BODY phase -> TYPES -> bodies. The SAME
            // routines as file scope, applied to this local subtree; lifetime = this
            // body frame. Parent is the global namespace (a function body isn't one).
            int ns = openNamespace(tree, s.name, s.name_tok, s.file_id,
                                   kGlobalFrame, diag);
            if (ns >= 0) {
                s.resolved_entry_id = ns;
                std::vector<parse::Node*> classes;
                registerScopeNames(tree, s, ns, classes, diag);        // NAME
                resolveScopeTypes(tree, s, /*isClass=*/false, diag);   // TYPES (+ fields)
                checkClassCyclesAndNeeds(tree, classes, diag);         // cycle + needs
                resolveScopeBodies(tree, s, /*isClass=*/false, diag);  // BODY
            }
            return Completion::Normal;
        }
        case parse::Kind::kEnumDecl: {
            // A qualified enum (`enum int C:E(…)` / `enum int Space:E(…)`) was moved into
            // a local class, or registered into a remote namespace's frame in place —
            // already a member, so skip it.
            if (isQualified(s)) return Completion::Normal;
            // An enum opened in a function body: registers its alias+namespace+
            // members (named) or bare consts (anonymous) in the current frame.
            // All enclosing-scope entries already exist here, so resolve the
            // member inits right away.
            registerEnum(tree, s, kGlobalFrame, diag);
            resolveEnumMemberInits(tree, s, diag);
            return Completion::Normal;
        }
        case parse::Kind::kExprStmt: {
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return Completion::Normal;
        }
        case parse::Kind::kReturnStmt: {
            // A long-for's update clause may not return (the spec; it runs at the
            // tail of each iteration). Banned transitively through the update.
            if (tree.in_for_update) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A 'return' statement is not allowed in a for-loop update "
                    "clause.", {}});
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            // A return transfers control out of the function — it never falls
            // through to its successor.
            return Completion::Abrupt;
        }
        case parse::Kind::kBlockStmt: {
            // A nested lexical scope. Push a frame so block-local decls live and
            // die with the block (and may shadow an outer name via the
            // innermost-first lookup). Definite-assignment state FLOWS THROUGH:
            // initialized_locals / read_locals are scoped, not isolated — an
            // assignment or read inside the block affects the enclosing local.
            // Only body_locals (declaration tracking for the unused sweep) is
            // block-scoped: save/clear on entry, sweep at block exit, restore.
            parse::pushFrame(tree);
            std::vector<int> saved_body_locals = std::move(tree.body_locals);
            tree.body_locals.clear();
            Completion c = resolveStmtList(tree, s.children, diag);
            sweepUnusedLocals(tree, diag);   // this block's declarations
            tree.body_locals = std::move(saved_body_locals);
            parse::popFrame(tree);
            // A block is Abrupt iff its statement list is.
            return c;
        }
        case parse::Kind::kIfStmt: {
            // children[0] = condition, [1] = then-block, [2] = optional else.
            // THE JOIN: snapshot the init-set S, resolve the then-arm from S,
            // restore S, resolve the else-arm from S; the post-merge set is the
            // INTERSECTION of the arms' contributions. An abrupt arm (one that
            // returns) never reaches the merge, so it contributes top (the
            // universal set) and is skipped from the ∩. A missing else arm
            // contributes S unchanged (T ∩ S = S — an else-less if never adds an
            // initialization). read_locals is NOT snapshotted: a read on any path
            // is a use (monotonic union).
            assert(s.children.size() >= 2 && "kIfStmt needs condition + then");
            resolveExpr(tree, *s.children[0], diag);
            std::set<int> entry = tree.initialized_locals;

            Completion then_c = resolveStmt(tree, *s.children[1], diag);
            std::set<int> then_out = std::move(tree.initialized_locals);

            tree.initialized_locals = entry;   // restore S for the else arm
            bool has_else = s.children.size() > 2 && s.children[2];
            Completion else_c = Completion::Normal;
            std::set<int> else_out;
            if (has_else) {
                else_c = resolveStmt(tree, *s.children[2], diag);
                else_out = std::move(tree.initialized_locals);
            } else {
                else_out = entry;   // no-else: the fall-through path keeps S
            }

            // Join: ∩ of normally-completing arms; abrupt arms skipped (top).
            std::set<int> joined;
            bool then_in = (then_c == Completion::Normal);
            bool else_in = (else_c == Completion::Normal);
            if (then_in && else_in) {
                for (int id : then_out) {
                    if (else_out.count(id) > 0) joined.insert(id);
                }
            } else if (then_in) {
                joined = std::move(then_out);
            } else if (else_in) {
                joined = std::move(else_out);
            } else {
                // Both arms abrupt: the merge is unreachable. The init-set is
                // moot; keep S so an erroneous downstream read still attributes.
                joined = std::move(entry);
            }
            tree.initialized_locals = std::move(joined);

            // The if is Abrupt iff every path through it is — both arms abrupt,
            // and an else exists (an else-less if always has a fall-through).
            if (has_else && then_c == Completion::Abrupt
                && else_c == Completion::Abrupt) {
                return Completion::Abrupt;
            }
            return Completion::Normal;
        }
        case parse::Kind::kWhileStmt: {
            // children[0] = condition, [1] = body block. Per 3B every pre-
            // condition loop is possibly-zero: the condition is tested before the
            // body, so its reads must be satisfied by the entry set S; the body
            // resolves from S but its inits do NOT escape (the loop may run zero
            // times), so the post-loop set is S again — regardless of any break
            // (the zero-iteration exit always contributes S, which dominates the
            // ∩). A while always completes Normally — it can fall through to its
            // exit (even an apparent while(true) is treated as possibly-zero; no
            // constant-true special case). The loop frame is pushed only so a
            // break/continue has a target + legality; its accumulators are unused.
            assert(s.children.size() == 2 && "kWhileStmt needs condition + body");
            resolveExpr(tree, *s.children[0], diag);
            std::set<int> entry = tree.initialized_locals;
            tree.loop_stack.push_back({});
            tree.loop_stack.back().name = s.label.empty() ? "while" : s.label;
            resolveStmt(tree, *s.children[1], diag);   // body block, from S
            parse::Tree::LoopFrame lf = std::move(tree.loop_stack.back());
            tree.loop_stack.pop_back();
            tree.initialized_locals = std::move(entry);   // after = S
            // A constant-true condition with no break targeting THIS loop never
            // exits (3B exception): the loop is non-completing, so its after-point
            // is unreachable (Abrupt drives 2A) and it is a return-terminator.
            if (condIsConstTrue(*s.children[0]) && !lf.break_seen) {
                s.non_completing = true;
                return Completion::Abrupt;
            }
            return Completion::Normal;
        }
        case parse::Kind::kDoWhileStmt: {
            // children[0] = condition, [1] = body block. The body runs AT LEAST
            // once, so its inits escape — but a break can cut the after-set short
            // and a continue can reach the condition with fewer inits. Resolve the
            // body from S (the first iteration's entry, which is also the
            // must-analysis fixpoint for the back-edge); the body's normal-
            // completion out-set is B. The condition is tested after a normal
            // fall-through OR a continue, so its reads must hold on B ∩
            // continue_accum. The loop exits via the condition (init-set =
            // cond_in, since the test adds nothing) or via a break, so
            // after = cond_in ∩ break_accum.
            assert(s.children.size() == 2 && "kDoWhileStmt needs condition + body");
            // No S snapshot: the body resolves from the current init-set (= S)
            // and a do-while never restores S (its inits escape), unlike if/while.
            tree.loop_stack.push_back({});
            tree.loop_stack.back().name = s.label.empty() ? "while" : s.label;
            resolveStmt(tree, *s.children[1], diag);   // body block, from S
            parse::Tree::LoopFrame lf = std::move(tree.loop_stack.back());
            tree.loop_stack.pop_back();

            std::set<int> body_out = tree.initialized_locals;       // B
            std::set<int> cond_in = lf.continue_seen
                ? intersectInit(body_out, lf.continue_init)         // B ∩ continues
                : body_out;
            tree.initialized_locals = cond_in;
            resolveExpr(tree, *s.children[0], diag);   // condition reads from cond_in

            std::set<int> after = lf.break_seen
                ? intersectInit(cond_in, lf.break_init)             // ∩ break exits
                : cond_in;
            tree.initialized_locals = std::move(after);
            // A constant-true post-condition with no break never exits (the body
            // loops forever): the loop is non-completing — Abrupt (unreachable
            // after) and a return-terminator. (Otherwise Normal-completing: even a
            // body that always returns is assumed able to reach its exit.)
            if (condIsConstTrue(*s.children[0]) && !lf.break_seen) {
                s.non_completing = true;
                return Completion::Abrupt;
            }
            return Completion::Normal;
        }
        case parse::Kind::kForLongStmt: {
            // children[0]=cond, [1]=update block, [2]=body block, [3..]=varlist.
            // Three scope frames: a for-scope holds the varlist, with the update
            // and body as sibling nested blocks (each pushes its own frame, so the
            // body may shadow a for-var). Pre-condition / possibly-zero, so the
            // post-loop init-set is the entry set S — body/update inits don't
            // escape, and the for-vars leave scope (like a pre-condition while).
            assert(s.children.size() >= 3 && "kForLongStmt needs cond+update+body");
            parse::pushFrame(tree);                            // for-scope
            std::vector<int> saved_body_locals = std::move(tree.body_locals);
            tree.body_locals.clear();
            // varlist decls live in the for-scope (explicit type) OR reuse an
            // enclosing local (typeless). The varlist initializers run once
            // unconditionally before the first test, so their inits escape the
            // loop (after = S', captured below) — distinct from the possibly-zero
            // body/update, whose inits don't escape. A fresh for-var leaves scope
            // but its id is unique, so its presence in S' is inert. S -> S'.
            for (std::size_t i = 3; i < s.children.size(); i++) {
                if (!s.children[i]) continue;
                parse::Node& d = *s.children[i];
                // A typeless varlist decl (no explicit type). WITH an initializer it
                // routes through the kAssignStmt path — an in-scope assignable name is
                // reused (observable after), an unknown name becomes a fresh inferred-init
                // local (classify types it from the rhs), and a non-assignable target is
                // rejected — all by the ONE funnel. WITHOUT an initializer there is no type
                // to infer: a bare name is not a valid declaration.
                if (d.kind == parse::Kind::kVarDeclStmt && !d.is_const
                    && d.return_type == widen::kNoType && !isQualified(d)) {
                    if (!d.children.empty()) {
                        d.kind = parse::Kind::kAssignStmt;
                        resolveStmt(tree, d, diag);
                        continue;
                    }
                    // Typeless AND no initializer — a structural error, independent of what
                    // the name resolves to (`int x; for (x)` is an error even though x is in
                    // scope: a varlist slot may not be a bare "touch"). Canon: flow/forlong.sl.
                    diagnostic::report(diag, {d.file_id, d.name_tok,
                        "A variable declaration needs an explicit type or an initializer.",
                        {}});
                    // Resolve an existing name / register a placeholder through the ONE
                    // funnel so cond/update/body reads don't cascade; main bails after
                    // resolve, so this is inert (resolveName<0 => not in-frame either, so
                    // the funnel's Declare path always fresh-declares, never conflicts).
                    int existing = resolveName(tree, d.name);
                    if (existing >= 0) {
                        d.resolved_entry_id = existing;
                    } else {
                        DeclInfo ph;
                        ph.name = d.name;
                        ph.file_id = d.file_id;
                        ph.name_tok = d.name_tok;
                        ph.track_body_local = false;   // error-recovery dummy, not swept
                        bool reused = false;
                        d.resolved_entry_id =
                            registerDeclarator(tree, ph, BindMode::Declare, reused, diag);
                        if (d.resolved_entry_id >= 0)
                            tree.initialized_locals.insert(d.resolved_entry_id);
                    }
                    d.kind = parse::Kind::kBlockStmt;   // neutralize the slot
                    d.children.clear();
                    continue;
                }
                resolveStmt(tree, d, diag);
            }
            std::set<int> after_varlist = tree.initialized_locals;   // S'
            // The condition is tested before the body, so its reads must hold on
            // the post-varlist set S' (the first iteration's entry, also the
            // back-edge fixpoint).
            resolveExpr(tree, *s.children[0], diag);
            // Body FIRST (execution is body-then-update): break/continue target
            // the for; continue folds into the loop frame's continue accumulator.
            tree.loop_stack.push_back({});
            tree.loop_stack.back().name = s.label.empty() ? "for" : s.label;
            resolveStmt(tree, *s.children[2], diag);   // body block, from S'
            parse::Tree::LoopFrame lf = std::move(tree.loop_stack.back());
            tree.loop_stack.pop_back();
            // The update runs after the body completes normally OR after a
            // continue, so its reads must hold on body-out ∩ continue_accum (this
            // lets the update see body-assigned vars, but not ones a continue
            // skipped). break_accum is moot — possibly-zero, so after = S anyway.
            std::set<int> body_out = tree.initialized_locals;
            if (lf.continue_seen) {
                tree.initialized_locals = intersectInit(body_out, lf.continue_init);
            }
            // Update clause: restricted (no break/continue/return) and NOT a loop
            // target — resolved with the for's loop frame already popped.
            bool saved_in_update = tree.in_for_update;
            int saved_floor = tree.for_update_floor;
            tree.in_for_update = true;
            tree.for_update_floor = (int)tree.loop_stack.size();
            resolveStmt(tree, *s.children[1], diag);   // update block
            tree.in_for_update = saved_in_update;
            tree.for_update_floor = saved_floor;
            // Sweep the for-scope-declared locals (the varlist) for unused.
            sweepUnusedLocals(tree, diag);
            tree.body_locals = std::move(saved_body_locals);
            parse::popFrame(tree);
            tree.initialized_locals = std::move(after_varlist);   // after = S'
            // A constant-true condition with no break targeting THIS for never
            // exits: non-completing — Abrupt (unreachable after) + a terminator.
            if (condIsConstTrue(*s.children[0]) && !lf.break_seen) {
                s.non_completing = true;
                return Completion::Abrupt;
            }
            return Completion::Normal;
        }
        case parse::Kind::kForRangedStmt: {
            // [0]=loop-var decl (init=start), [1]=end, [2]=step|null, [3]=body.
            // text=cmp, name=op. UNDERSTOOD here in scope (loop var registered,
            // bounds + body resolved, definite-assignment run) but NOT lowered —
            // desugar builds the kForLongStmt with fresh `_$end`/`_$step` ids.
            // Mirrors the kForLongStmt DA: the bounds run once (inits escape to S'),
            // the body is possibly-zero (after = S'), break/continue target here.
            assert(s.children.size() == 4
                && "kForRangedStmt needs var+end+step+body");
            parse::pushFrame(tree);                            // for-scope
            std::vector<int> saved_body_locals = std::move(tree.body_locals);
            tree.body_locals.clear();
            // Loop var: a typeless decl WITH an initializer reuses an enclosing
            // local or declares a fresh inferred one (the kAssignStmt path, exactly
            // as the kForLongStmt varlist does — resolveStmt flips an undeclared
            // name back to a fresh kVarDeclStmt, a reuse stays a kAssignStmt); an
            // explicitly-typed decl declares fresh. The loop var always has `start`.
            parse::Node& lv = *s.children[0];
            if (lv.kind == parse::Kind::kVarDeclStmt && !lv.is_const
                && lv.return_type == widen::kNoType && !isQualified(lv)
                && !lv.children.empty()) {
                lv.kind = parse::Kind::kAssignStmt;
            }
            resolveStmt(tree, lv, diag);
            // The cond/update read the loop var on every pass, so it is never an
            // unused local — mark it read (the body alone may not touch it).
            if (lv.resolved_entry_id >= 0) {
                tree.read_locals.insert(lv.resolved_entry_id);
            }
            // end + step run once before the loop (reads on the entry set); their
            // values become the desugar-minted `_$end`/`_$step` locals.
            resolveExpr(tree, *s.children[1], diag);
            if (s.children[2]) resolveExpr(tree, *s.children[2], diag);
            std::set<int> after = tree.initialized_locals;     // S'
            // Body from S', break/continue target this loop.
            tree.loop_stack.push_back({});
            tree.loop_stack.back().name = s.label.empty() ? "for" : s.label;
            resolveStmt(tree, *s.children[3], diag);
            tree.loop_stack.pop_back();
            // Possibly-zero body: after = S' (body inits don't escape). The update
            // `var = var op step` reads only already-initialized locals, so the
            // continue-accum intersection a long-for does for its update is moot.
            sweepUnusedLocals(tree, diag);
            tree.body_locals = std::move(saved_body_locals);
            parse::popFrame(tree);
            tree.initialized_locals = std::move(after);
            // A ranged condition is `var cmp end` (idents, never a constant), so —
            // unlike a long-for — it is never constant-true / non-completing.
            return Completion::Normal;
        }
        case parse::Kind::kForArrayStmt:
            // Produced by understandForArray's retag inside the kForEnumStmt case
            // (which already resolved it); it is never dispatched here directly.
            assert(false && "resolveStmt: kForArrayStmt resolved via kForEnumStmt");
            return Completion::Normal;
        case parse::Kind::kForTupleStmt:
            // Produced by understandForTuple's retag inside the kForEnumStmt case;
            // already resolved there, never dispatched here directly.
            assert(false && "resolveStmt: kForTupleStmt resolved via kForEnumStmt");
            return Completion::Normal;
        case parse::Kind::kForEnumStmt: {
            // children[0]=loop-var decl, [1]=enum-ref, [2]=body. Rewrite IN PLACE
            // into a kForLongStmt over the enum's first..last defined members,
            // then resolve that (so the whole for-long pipeline is reused). The
            // first/last kConst members are referenced by qualified name, so the
            // normal resolve+constfold path fills their values; an empty/descending
            // enum (first > last) then trips the empty-range check ("Invalid
            // range." on the enum name, via range_dotdot_tok).
            assert(s.children.size() == 3 && "kForEnumStmt needs var+enum+body");
            parse::Node& enum_ref = *s.children[1];
            // A tuple LITERAL iterable — `for (x : (7,4,2))`. Spill + iterate.
            if (enum_ref.kind == parse::Kind::kTupleExpr) {
                return understandForTuple(tree, s, widen::kNoType,
                                       /*is_literal=*/true, /*is_lvalue=*/false, diag);
            }
            // Any non-name, non-literal EXPRESSION — `for (x : ref^)`, `f()`,
            // `arr[i]`. Peek its type: a homogeneous tuple iterates (in place for
            // an lvalue deref/index, spilled for an rvalue call). A tuple is the
            // ONLY iterable expression form, so an UNKNOWN-typed expression (an
            // inferred ref/index — peek yields kNoType) is routed as a tuple with a
            // deferred type; classify guards it (errors if it isn't one). A KNOWN
            // non-tuple type errors here.
            if (enum_ref.kind != parse::Kind::kIdentExpr) {
                widen::TypeRef ity = peekIterableType(tree, enum_ref, diag);
                bool lval = (enum_ref.kind == parse::Kind::kDerefExpr
                          || enum_ref.kind == parse::Kind::kIndexExpr);
                // A tuple OR array EXPRESSION iterable (`sub^`, `t[i]`) routes
                // through the iterator-based for-tuple path — an array is a
                // homogeneous tuple (the loop var binds each element in place).
                if (widen::form(widen::strip(ity)) == widen::Type::Form::kTuple
                    || widen::form(widen::strip(ity)) == widen::Type::Form::kArray) {
                    return understandForTuple(tree, s, ity, /*is_literal=*/false,
                                           lval, diag);
                }
                // A CLASS EXPRESSION iterable — `ptr^`, a construction `C(..)`, a
                // call `fn()`. understandForClass spills a non-lvalue (rvalue) to a
                // for-scope temp so its protocol methods hit one object.
                if (widen::form(widen::strip(ity)) == widen::Type::Form::kSlid) {
                    return understandForClass(tree, s, widen::strip(ity), diag);
                }
                if (ity == widen::kNoType) {
                    return understandForTuple(tree, s, widen::kNoType,
                                           /*is_literal=*/false, lval, diag);
                }
                diagnostic::report(diag, {enum_ref.file_id, enum_ref.tok,
                    "A for-loop operand must be an enum, an array, or a tuple.", {}});
                return Completion::Normal;
            }
            int enum_id = isQualified(enum_ref)
                ? resolveQualifiedRef(tree, enum_ref, diag)
                : resolveName(tree, enum_ref.name);
            if (enum_id < 0) {
                if (!isQualified(enum_ref)) {
                    diagnostic::report(diag, {enum_ref.file_id, enum_ref.tok,
                        "Unknown enum '" + enum_ref.name + "'.", {}});
                }
                return Completion::Normal;
            }
            // The colon-form also iterates a fixed-size ARRAY local — `for (v :
            // arr)` — or a TUPLE local — `for (v : tuple)`. Dispatch on what the
            // name resolved to. A for-iterable is a storage-backed VARIABLE — a local
            // OR a global (a global array/tuple iterates exactly like a local; only
            // the storage class differs, and codegen touches the lazy global via the
            // element access / iterator-base `^` inside the loop).
            bool iter_var = tree.entries[enum_id].kind == parse::EntryKind::kLocalVar
                         || tree.entries[enum_id].kind == parse::EntryKind::kGlobalVar;
            if (iter_var && isArrayType(tree.entries[enum_id].slids_type)) {
                return understandForArray(tree, s, enum_id, diag);
            }
            // A CLASS local/global — iterate by its begin/end/next or size/op[]
            // protocol (understood + lowered to a kForLongStmt at resolve).
            if (iter_var
                && widen::form(widen::strip(tree.entries[enum_id].slids_type))
                       == widen::Type::Form::kSlid) {
                return understandForClass(tree, s,
                    widen::strip(tree.entries[enum_id].slids_type), diag);
            }
            if (iter_var
                && widen::form(widen::strip(tree.entries[enum_id].slids_type))
                       == widen::Type::Form::kTuple) {
                return understandForTuple(tree, s, tree.entries[enum_id].slids_type,
                                       /*is_literal=*/false, /*is_lvalue=*/true, diag);
            }
            // A TYPELESS local (`t = (1,2,4)`) is still kNoType at resolve (classify
            // infers it). Arrays are always explicitly typed, so a typeless local
            // used as an iterable can only be a tuple — route it as one with a
            // deferred type (homogeneity + arity resolve in classify); classify
            // errors if it doesn't actually infer to a tuple.
            if (iter_var && tree.entries[enum_id].slids_type == widen::kNoType) {
                return understandForTuple(tree, s, widen::kNoType,
                                       /*is_literal=*/false, /*is_lvalue=*/true, diag);
            }
            // Otherwise it must be an enum: a kNamespace carrying an underlying
            // type (transparent).
            if (tree.entries[enum_id].kind != parse::EntryKind::kNamespace
                || tree.entries[enum_id].slids_type == widen::kNoType) {
                parse::Entry const& bad = tree.entries[enum_id];
                diagnostic::report(diag, {enum_ref.file_id, enum_ref.tok,
                    "'" + enum_ref.name + "' is not an enum, array, or tuple.",
                    {{bad.file_id, bad.tok, "declared here"}}});
                return Completion::Normal;
            }
            int member_frame = tree.entries[enum_id].ns_frame_id;
            int first = -1, last = -1;
            for (std::size_t i = 0; i < tree.entries.size(); i++) {
                parse::Entry const& m = tree.entries[i];
                if (m.kind == parse::EntryKind::kConst
                    && m.owner_ns_frame == member_frame) {
                    if (first < 0) first = (int)i;
                    last = (int)i;
                }
            }
            if (first < 0) {
                diagnostic::report(diag, {enum_ref.file_id, enum_ref.tok,
                    "Enum '" + enum_ref.name + "' has no members to iterate.", {}});
                return Completion::Normal;
            }
            // Capture enum-ref shape before we rebuild s.children.
            std::vector<std::string> en_qual = enum_ref.qualifier;
            std::vector<int> en_qtoks = enum_ref.qualifier_toks;
            std::string en_name = enum_ref.name;
            bool en_global = enum_ref.global_qualified;
            int en_file = enum_ref.file_id;
            int en_tok = enum_ref.tok;
            int en_nametok = enum_ref.name_tok >= 0 ? enum_ref.name_tok : enum_ref.tok;

            auto ident = [&](std::string nm, int tok) {
                auto n = std::make_unique<parse::Node>();
                n->kind = parse::Kind::kIdentExpr;
                n->name = std::move(nm);
                n->file_id = en_file; n->tok = tok; n->name_tok = tok;
                return n;
            };
            // A qualified ref `<enum-path>:member` to a first/last member.
            auto memberRef = [&](int mid) {
                auto r = ident(tree.entries[mid].name, en_tok);
                r->qualifier = en_qual;
                r->qualifier.push_back(en_name);
                r->qualifier_toks = en_qtoks;
                r->qualifier_toks.push_back(en_nametok);
                r->global_qualified = en_global;
                return r;
            };

            // An enum-for IS a ranged-for over the member VALUES first..last
            // (inclusive, `<=`, step +1): build a kForRangedStmt and resolve it
            // through that path. desugar lowers it, minting `_$end` with a fresh id
            // — so the old nested-typeless-`_$end` clobber can't arise.
            std::unique_ptr<parse::Node> var_decl = std::move(s.children[0]);
            var_decl->children.push_back(memberRef(first));   // init = first member
            std::unique_ptr<parse::Node> end = memberRef(last);
            std::unique_ptr<parse::Node> body = std::move(s.children[2]);

            s.kind = parse::Kind::kForRangedStmt;
            s.text = "<=";                 // var <= last
            s.name = "+";                  // step +1
            s.range_dotdot_tok = en_tok;   // empty-range caret on the enum name
            s.children.clear();
            s.children.push_back(std::move(var_decl));   // [0] loop-var (init=first)
            s.children.push_back(std::move(end));        // [1] end = last
            s.children.push_back(nullptr);               // [2] step (null => +1)
            s.children.push_back(std::move(body));       // [3] body
            return resolveStmt(tree, s, diag);
        }
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt: {
            // A loop/switch exit / loop restart: transfers control (Abrupt). The
            // target depends on the argument — named (s.name), numbered (s.text),
            // or naked. The current init-set folds into the TARGET frame's
            // accumulator (so a do-while / for can intersect it), and the node is
            // stamped with the hops to the target for codegen.
            bool is_break = (s.kind == parse::Kind::kBreakStmt);
            char const* what = is_break ? "break" : "continue";
            // No break/continue of ANY flavor directly in a long-for update clause
            // (a loop/switch nested in the update absorbs its own and raises the
            // stack above the floor, so this only fires at the update's own level).
            if (tree.in_for_update
                && (int)tree.loop_stack.size() == tree.for_update_floor) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    std::string("A '") + what
                        + "' statement is not allowed in a for-loop update "
                          "clause.", {}});
                return Completion::Abrupt;
            }
            int n = (int)tree.loop_stack.size();
            int target = -1;
            if (!s.name.empty()) {
                // NAMED: the nearest enclosing LOOP whose name matches (switches
                // carry no name); innermost match wins.
                for (int t = n - 1; t >= 0; --t) {
                    if (!tree.loop_stack[t].is_switch
                        && tree.loop_stack[t].name == s.name) {
                        target = t;
                        break;
                    }
                }
                if (target < 0) {
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "No enclosing loop labeled '" + s.name + "'.", {}});
                    return Completion::Abrupt;
                }
            } else if (!s.text.empty()) {
                // NUMBERED: the Nth enclosing LOOP outward, skipping switches.
                long count = std::strtol(s.text.c_str(), nullptr, 10);
                if (count < 1) {
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        std::string(is_break ? "Break" : "Continue")
                            + " count must be at least 1.", {}});
                    return Completion::Abrupt;
                }
                long seen = 0;
                for (int t = n - 1; t >= 0; --t) {
                    if (tree.loop_stack[t].is_switch) continue;
                    if (++seen == count) { target = t; break; }
                }
                if (target < 0) {
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        std::string(is_break ? "Break" : "Continue")
                            + " count exceeds the enclosing loop nesting.", {}});
                    return Completion::Abrupt;
                }
            } else if (is_break) {
                // NAKED break: the nearest enclosing loop (switch is transparent —
                // it is no longer a break target).
                for (int t = n - 1; t >= 0; --t) {
                    if (!tree.loop_stack[t].is_switch) { target = t; break; }
                }
                if (target < 0) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "A 'break' statement must be inside a loop.", {}});
                    return Completion::Abrupt;
                }
            } else {
                // NAKED continue: the nearest enclosing LOOP (switch transparent).
                for (int t = n - 1; t >= 0; --t) {
                    if (!tree.loop_stack[t].is_switch) { target = t; break; }
                }
                if (target < 0) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "A 'continue' statement must be inside a loop.", {}});
                    return Completion::Abrupt;
                }
            }
            parse::Tree::LoopFrame& lf = tree.loop_stack[target];
            if (is_break) {
                foldLoopExit(lf.break_init, lf.break_seen, tree.initialized_locals);
            } else {
                foldLoopExit(lf.continue_init, lf.continue_seen,
                             tree.initialized_locals);
            }
            s.loop_levels = (n - 1) - target;   // hops outward for codegen
            return Completion::Abrupt;
        }
        case parse::Kind::kSwitchStmt: {
            // children[0] = scrutinee, [1..] = kCaseClause. A clause is a label-list
            // (children[0..n-2], null = default) + a body block (children.back());
            // clause.text == "continue" marks a trailing fall-through into the next
            // clause. There is NO implicit fall-through: a clause exits the switch at
            // its body's `}` unless it carries the trailing continue (the last
            // clause's continue falls off the bottom). switch is transparent to
            // break/continue — both bind to the enclosing loop — so no switch frame
            // is pushed. Each clause body ENTERS from S (any case is a direct
            // dispatch target, so the direct-entry init-set dominates the join).
            assert(!s.children.empty() && "kSwitchStmt needs a scrutinee");
            resolveExpr(tree, *s.children[0], diag);
            std::set<int> entry = tree.initialized_locals;   // S
            bool has_default = false;
            std::set<int> after;
            bool have = false;
            bool normal_exit = false;
            auto fold = [&](std::set<int> const& set) {
                after = have ? intersectInit(after, set) : set;
                have = true;
                normal_exit = true;
            };
            for (std::size_t i = 1; i < s.children.size(); i++) {
                parse::Node& clause = *s.children[i];   // kCaseClause
                std::size_t nlabel = clause.children.size() - 1;   // body = back()
                for (std::size_t j = 0; j < nlabel; j++) {
                    if (clause.children[j]) resolveExpr(tree, *clause.children[j], diag);
                    else has_default = true;
                }
                tree.initialized_locals = entry;                    // enter from S
                Completion c = resolveStmt(tree, *clause.children.back(), diag);
                bool is_last = (i + 1 == s.children.size());
                bool has_cont = (clause.text == "continue");
                // A clause that completes Normally exits the switch — at its body's
                // end (no continue) or off the bottom (last clause's continue). A
                // continue on a non-last clause falls into the next clause instead,
                // contributing no exit path.
                if (c == Completion::Normal && (!has_cont || is_last)) {
                    fold(tree.initialized_locals);
                }
            }
            bool empty_body = (s.children.size() == 1);
            if (!has_default || empty_body) fold(entry);   // no-match path = S
            tree.initialized_locals = have ? std::move(after) : std::move(entry);
            return normal_exit ? Completion::Normal : Completion::Abrupt;
        }
        case parse::Kind::kFunctionDef:
            // A nested function definition: signature registered by the host's
            // nested pre-pass; its BODY is resolved AFTER the whole host body
            // (resolveFunctionBody's deferred pass), so it can reference any of the
            // host's top-level locals regardless of textual order (forward
            // capture). Nothing to do at the statement position.
            return Completion::Normal;
        case parse::Kind::kFunctionDecl:
            // A nested forward declaration: signature-only, registered by the
            // pre-pass; no body to resolve. (Never defined -> the pass-3 orphan
            // check reports it, like a file-scope declaration.)
            return Completion::Normal;
        case parse::Kind::kClassDef:
            // A local class: registered and member-resolved by resolveStmtList's
            // pre-pass before this statement is reached. The no-op here RELIES on
            // that, so assert it — a kClassDef arriving unregistered means a body
            // path bypassed the pre-pass (the Finding-1 silent-drop), not a case
            // to swallow quietly. EXCEPTION: the pre-pass ran but registration
            // itself rejected the class (every error return leaves the node
            // unregistered by design) — the diagnostic is already recorded, so
            // return and let it render instead of aborting on the assert.
            if (s.resolved_entry_id < 0 && diagnostic::hasErrors(diag))
                return Completion::Normal;
            assert(s.resolved_entry_id >= 0
                   && "kClassDef reached resolveStmt unregistered — a body path "
                      "skipped resolveStmtList's local-class pre-pass");
            return Completion::Normal;
        case parse::Kind::kGlobalScopeStmt:
            // `global;` opens the global lifetime for its scope. Nothing to resolve;
            // placement rules (main-only, at most one) are checked by a dedicated
            // pass, and codegen registers the dtor-registry call at scope exit.
            return Completion::Normal;
        case parse::Kind::kProgram:
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral:
        case parse::Kind::kNullptrLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kAddrOfExpr:
        case parse::Kind::kDerefExpr:
        case parse::Kind::kIndexExpr:
        case parse::Kind::kTupleExpr:
        case parse::Kind::kFieldExpr:
        case parse::Kind::kCastExpr:
        case parse::Kind::kConvertExpr:
        case parse::Kind::kNewExpr:
        case parse::Kind::kSizeofExpr:
        case parse::Kind::kStringifyType:
        case parse::Kind::kCallExpr:
        case parse::Kind::kCaseClause:
        case parse::Kind::kParam:
            assert(false && "resolveStmt: not a statement kind");
            return Completion::Normal;
    }
    assert(false && "resolveStmt: unhandled parse::Kind");
    return Completion::Normal;
}

// ---- Function templates ------------------------------------------------------
// A template registers like a function but resolves NOTHING of its body — the body
// stays in pristine parse state until classify demands an instance. Registration
// builds the entry's PATTERN signature (each `T` a kTmplParamDefId marker leaf) so
// classify can unify call arguments against it, records the definition node + host
// list, and — at the point the body WOULD have resolved — snapshots resolve's
// transient scope state so instantiation can re-enter resolution there later.

// Any same-scope same-name function entry (template or not). `frame` < 0 means a
// namespace/class member scope identified by `owner`; else a lexical frame.
int findSameScopeFunction(parse::Tree const& tree, std::string const& name,
                          int owner, int parent_frame) {
    for (std::size_t id = 0; id < tree.entries.size(); id++) {
        parse::Entry const& e = tree.entries[id];
        if (e.kind != parse::EntryKind::kFunction || e.name != name) continue;
        if (!e.tmpl_args.empty()) continue;   // an instance shares its template's name
        if (owner >= 0 ? e.owner_ns_frame == owner
                       : (e.owner_ns_frame < 0 && e.parent_frame_id == parent_frame))
            return (int)id;
    }
    return -1;
}

void reportTemplateNameClash(diagnostic::Sink& diag, parse::Node const& node,
                             parse::Entry const& prev) {
    diagnostic::report(diag, {node.file_id, node.name_tok,
        "A template function may not share its name with another function; '"
        + node.name + "' owns its name (overloading a template is not supported).",
        {{prev.file_id, prev.tok, "first declared here"}}});
}

// Register a function TEMPLATE definition in the current scope. `owner` >= 0 puts it
// in a namespace member frame; `nested` marks a body (block) scope, where instances
// become nested functions. The body is NOT resolved.
void registerTemplateFunction(parse::Tree& tree, parse::Node& node,
                              std::vector<std::unique_ptr<parse::Node>>* host_list,
                              bool nested, int owner, diagnostic::Sink& diag) {
    // A BODYLESS template declaration (`T add<T>(T a, T b);`) is the
    // header-side spelling of a cross-TU template — its definition lives in
    // the header's same-named source. In a NON-imported file it declares
    // nothing anyone can define.
    if (node.kind == parse::Kind::kFunctionDecl
        && !fileIsImported(tree, node.file_id)) {
        diagnostic::report(diag, {node.file_id, node.name_tok,
            "A template function must have a body.", {}});
        return;
    }
    // Collision, both directions: a template owns its name in its scope —
    // EXCEPT the cross-TU decl/def pair: a header's bodyless template
    // declaration MERGES with the same-signature definition in this file
    // (the header's sibling), exactly as a plain function's decl + def merge.
    int prev = findSameScopeFunction(tree, node.name, owner,
                                     parse::currentFrameId(tree));
    if (prev >= 0) {
        auto pit = tree.templates.find(prev);
        // A HEADER-owned template (its entry registered from the header's
        // declaration — true whether or not a loaded source already merged a
        // body onto the pattern).
        bool prev_header_tmpl = pit != tree.templates.end() && pit->second.def
            && fileIsImported(tree, tree.entries[prev].file_id);
        bool prev_is_header_decl = prev_header_tmpl
            && pit->second.def->kind == parse::Kind::kFunctionDecl;
        // Only the header's OWN MODULE supplies the definition: a loaded
        // template source, or the sibling TU compiling itself. A CONSUMER's
        // same-name definition must not merge — it would be silently ignored
        // (the consumer's flavors are declare-only) while shadowing the real
        // one — nor collide as an overload: reject it by name.
        bool def_here = node.kind == parse::Kind::kFunctionDef
            && (fileIsTemplateSource(tree, node.file_id)
                || (!fileIsImported(tree, node.file_id)
                    && fileIsSibling(tree, tree.entries[prev].file_id)));
        if (prev_is_header_decl && def_here) {
            if (node.type_params != pit->second.def->type_params
                || node.params.size() != pit->second.def->params.size()) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "The template definition does not match the header's "
                    "declaration of '" + node.name + "'.",
                    {{pit->second.def->file_id, pit->second.def->name_tok,
                      "declared here"}}});
                return;
            }
            // Adopt the DEFINITION as the pattern (it has the body to clone);
            // the entry — and its resolved pattern signature — stays.
            pit->second.def = &node;
            pit->second.host_list = host_list;
            node.resolved_entry_id = prev;
            tree.entries[prev].def_file_id = node.file_id;
            tree.entries[prev].def_tok = node.name_tok;
            return;
        }
        if (prev_header_tmpl && node.kind == parse::Kind::kFunctionDef
            && !fileIsImported(tree, node.file_id)
            && !fileIsTemplateSource(tree, node.file_id)) {
            diagnostic::report(diag, {node.file_id, node.name_tok,
                "A template declared in a header is defined by its module's "
                "source file; '" + node.name + "' cannot be defined here.",
                {{tree.entries[prev].file_id, tree.entries[prev].tok,
                  "declared here"}}});
            return;
        }
        reportTemplateNameClash(diag, node, tree.entries[prev]);
        return;
    }
    int other = owner >= 0 ? findMemberDeclared(tree, owner, node.name)
                           : parse::findInFrame(tree, parse::currentFrameId(tree),
                                                node.name);
    if (other >= 0) {
        parse::Entry const& pe = tree.entries[other];
        reportNameCollision(diag, "Duplicate declaration of '" + node.name + "'.",
                            pe.file_id, pe.tok, node.file_id, node.name_tok);
        return;
    }
    int nreq = 0;
    bool seen_default = false;
    std::vector<widen::TypeRef> raw;
    for (auto& p : node.params) {
        if (!p) continue;
        bool has_default = !p->children.empty();
        if (p->return_type == widen::kNoType && !has_default) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "Parameter '" + p->name
                    + "' needs an explicit type or a default value.", {}});
        }
        if (has_default) {
            seen_default = true;
        } else {
            if (seen_default) {
                diagnostic::report(diag, {p->file_id, p->name_tok,
                    "A required parameter cannot follow an optional parameter.", {}});
            }
            nreq++;
        }
        raw.push_back(p->return_type);
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kFunction;
    e.name = node.name;
    e.slids_type = node.return_type;    // provisional — resolveTemplatePatterns
    e.param_types = std::move(raw);     //   rewrites both to PATTERN types
    e.num_required = nreq;
    e.file_id = node.file_id;
    e.tok = node.name_tok;
    e.is_template = true;
    e.defined = true;   // the template IS its definition (never an orphan; a call
                        // never emits its symbol — instances carry the code)
    e.def_file_id = node.file_id;
    e.def_tok = node.name_tok;
    if (owner >= 0) e.owner_ns_frame = owner;
    node.resolved_entry_id = parse::addEntry(tree, std::move(e));

    parse::TemplateInfo ti;
    ti.def = &node;
    ti.host_list = host_list;
    ti.nested = nested;
    tree.templates[node.resolved_entry_id] = std::move(ti);
}

// PATTERN signature: resolve COPIES of the template's declared types in a frame
// where each type parameter aliases a kTmplParamDefId marker leaf, writing the
// results to the ENTRY. The node's own types stay pristine — the instance clone
// re-resolves them against the real bound types. Called where the surrounding
// scope's signatures resolve (file/nested: at registration, every name is known
// there; namespace member: the TYPES phase, for forward refs).
void resolveTemplatePatterns(parse::Tree& tree, parse::Node& node, int entry_id,
                             diagnostic::Sink& diag) {
    parse::pushFrame(tree);
    bindTypeParamMarkers(tree, node);
    std::vector<widen::TypeRef> ptypes;
    for (auto& p : node.params) {
        if (!p) continue;
        widen::TypeRef t = p->return_type;   // a COPY — the param node stays pristine
        if (t != widen::kNoType) resolveDeclType(tree, t, p->file_id, p->tok, diag);
        ptypes.push_back(t);
    }
    widen::TypeRef ret = node.return_type;
    if (ret != widen::kNoType)
        resolveDeclType(tree, ret, node.file_id, node.tok, diag);
    parse::popFrame(tree);
    parse::Entry& e = tree.entries[entry_id];
    e.slids_type = ret;
    e.param_types = std::move(ptypes);
}

// Every name mentioned anywhere in a subtree (idents, callees, assign targets).
void collectBodyNames(parse::Node const& n, std::set<std::string>& out) {
    if (!n.name.empty()) out.insert(n.name);
    for (auto const& c : n.children) if (c) collectBodyNames(*c, out);
    for (auto const& p : n.params) if (p) collectBodyNames(*p, out);
}

// Capture resolve's transient scope state at the template's body-resolution point —
// the same visibility an ordinary body would have had (all forward refs registered,
// the enclosing frames live). Instantiation re-installs this to re-enter resolution.
void snapshotTemplate(parse::Tree& tree, parse::Node& node) {
    auto it = tree.templates.find(node.resolved_entry_id);
    if (it == tree.templates.end()) return;   // registration failed (collision)
    parse::TemplateInfo& ti = it->second;
    ti.frame_id_stack = tree.frame_id_stack;
    ti.frame_entries_start_stack = tree.frame_entries_start_stack;
    ti.live_entry_ids = tree.live_entry_ids;
    ti.open_ns_frames = tree.open_ns_frames;
    ti.initialized_locals = tree.initialized_locals;
    ti.assigned_arrays = tree.assigned_arrays;
    ti.current_class_name = tree.current_class_name;   // a METHOD template's body
    ti.current_base_name = tree.current_base_name;     //   resolves as a member's
    ti.tmpl_self_stack = tree.tmpl_self_stack;   // a pattern inside a class-template
                                                 //   instantiation keeps its flavor's
                                                 //   bare-name redirect
    ti.snapshot_taken = true;
    // A kClassDef pattern's NESTED CLASS TEMPLATES (sub-patterns, registered
    // beside it under "Outer:Inner") share its definition-point visibility —
    // capture theirs with the same state. (The file-scope snapshot pass also
    // reaches them through tree.templates; snapshot_taken keeps it single.)
    if (node.kind == parse::Kind::kClassDef) {
        for (auto& m : node.children) {
            if (m && m->kind == parse::Kind::kClassDef && !m->type_params.empty()
                && m->resolved_entry_id >= 0) {
                auto sit = tree.templates.find(m->resolved_entry_id);
                if (sit != tree.templates.end() && !sit->second.snapshot_taken)
                    snapshotTemplate(tree, *m);
            }
        }
    }
    // A BLOCK template's body may CAPTURE host locals — but it resolves only at
    // instantiation, after the host's unused-local sweep. Mark every host local
    // the body names as READ now, so a local used only inside the template
    // doesn't trip "set but never used". (Name-based and so conservative: a
    // shadowed or unresolvable name marks nothing that isn't a live local.)
    if (ti.nested) {
        std::set<std::string> used;
        for (auto const& c : node.children) if (c) collectBodyNames(*c, used);
        for (std::string const& nm : used) {
            int id = resolveName(tree, nm);
            if (id >= 0 && tree.entries[id].kind == parse::EntryKind::kLocalVar)
                tree.read_locals.insert(id);
        }
    }
}

// NESTED-FUNCTION SIGNATURE PRE-PASS, per SCOPE. Register each nested function declared
// DIRECTLY in this scope so a call may precede the definition (the canon `call_before`
// shape) and so the function can recurse on itself. Runs for EVERY scoped statement list —
// a function body's top level, a bare block, an if/else arm, a loop body, a switch case —
// exactly like the local-class pre-pass right above it, and for the same reason: the name
// belongs to THIS frame, so it shadows and drops with this scope. (It used to run only over
// a function body's DIRECT children, so a function written inside a block was never
// registered at all: calling it said "Unknown function", and NOT calling it left its body
// unresolved, which crashed classify on an unstamped identifier.)
//
// A nested function inside a BLOCK captures that block's locals. That is sound for the same
// reason a top-level capture is: a capture is the host alloca's ADDRESS, all allocas live in
// the one function frame, and the function can only be CALLED from inside the block where
// its captures are live.
void registerNestedFunctions(parse::Tree& tree,
                             std::vector<std::unique_ptr<parse::Node>>& stmts,
                             diagnostic::Sink& diag) {
    for (auto& ch : stmts) {
        if (!ch || (ch->kind != parse::Kind::kFunctionDef
                    && ch->kind != parse::Kind::kFunctionDecl)) {
            continue;
        }
        // capture_floor >= 0 means we are resolving inside a NESTED function's body (at its
        // top level or in one of its blocks) — a further nesting level.
        if (tree.capture_floor >= 0) {
            diagnostic::report(diag, {ch->file_id, ch->name_tok,
                "Nested functions may not contain further nested functions.", {}});
            continue;
        }
        // A block-scope TEMPLATE: register (pattern signature + host list), no body
        // resolution — the deferred-body pass below takes its scope snapshot instead.
        if (!ch->type_params.empty()) {
            registerTemplateFunction(tree, *ch, &stmts, /*nested=*/true,
                                     /*owner=*/-1, diag);
            if (ch->resolved_entry_id >= 0)
                resolveTemplatePatterns(tree, *ch, ch->resolved_entry_id, diag);
            continue;
        }
        std::vector<widen::TypeRef> ptypes;
        int nreq = 0;
        bool seen_default = false;
        for (auto& p : ch->params) {
            if (!p) continue;
            bool has_default = !p->children.empty();
            if (p->return_type == widen::kNoType && !has_default) {
                diagnostic::report(diag, {p->file_id, p->name_tok,
                    "Parameter '" + p->name
                        + "' needs an explicit type or a default value.", {}});
            }
            if (has_default) {
                seen_default = true;
            } else {
                if (seen_default) {
                    diagnostic::report(diag, {p->file_id, p->name_tok,
                        "A required parameter cannot follow an optional "
                        "parameter.", {}});
                }
                nreq++;
            }
            if (p->return_type != widen::kNoType) {
                resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
            }
            ptypes.push_back(p->return_type);
        }
        resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        bool is_def = (ch->kind == parse::Kind::kFunctionDef);
        // A nested function may be forward-declared (signature only) and defined later in
        // the same scope — match an existing same-name entry: the signature must agree, and
        // a second definition is a duplicate.
        int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
        if (existing >= 0) {
            parse::Entry& prev = tree.entries[existing];
            if (prev.kind == parse::EntryKind::kFunction && prev.is_template) {
                reportTemplateNameClash(diag, *ch, prev);
                continue;
            }
            if (prev.kind != parse::EntryKind::kFunction
                || prev.slids_type != ch->return_type
                || prev.param_types != ptypes) {
                diagnostic::report(diag, {ch->file_id, ch->name_tok,
                    "Duplicate declaration of '" + ch->name + "'.",
                    {{prev.file_id, prev.tok, "first declared here"}}});
                continue;
            }
            if (is_def && prev.defined) {
                diagnostic::report(diag, {ch->file_id, ch->name_tok,
                    "Duplicate definition of '" + ch->name + "'.",
                    {{prev.def_file_id, prev.def_tok, "first defined here"}}});
                continue;
            }
            if (is_def) {
                prev.defined = true;
                prev.def_file_id = ch->file_id;
                prev.def_tok = ch->name_tok;
            }
            ch->resolved_entry_id = existing;
            continue;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kFunction;
        e.name = ch->name;
        e.slids_type = ch->return_type;
        e.param_types = std::move(ptypes);
        e.num_required = nreq;
        e.file_id = ch->file_id;
        e.tok = ch->name_tok;
        e.defined = is_def;
        e.is_external = !is_def && fileIsImported(tree, ch->file_id);
        e.is_foreign = ch->is_foreign;
        if (is_def) {
            e.def_file_id = ch->file_id;
            e.def_tok = ch->name_tok;
        }
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
}

// DEFERRED nested-function BODY pass, per SCOPE. Runs AFTER this scope's statements, with
// this scope's frame still OPEN — so a nested function may reference any local of the scope
// it lives in, declared anywhere in it (forward capture). Captures are published on the
// nested entry here, before the host's call-site definite-assignment check reads them.
void resolveNestedFunctionBodies(parse::Tree& tree,
                                 std::vector<std::unique_ptr<parse::Node>>& stmts,
                                 diagnostic::Sink& diag) {
    for (auto& ch : stmts) {
        if (ch && ch->kind == parse::Kind::kFunctionDef) {
            // A TEMPLATE's body stays pristine — capture the scope state its body
            // WOULD have resolved under (this scope's frame open, forward captures
            // included) for classify's on-demand instantiation.
            if (!ch->type_params.empty()) {
                snapshotTemplate(tree, *ch);
                continue;
            }
            resolveFunctionBody(tree, *ch, diag, /*nested=*/true);
        }
    }
}

Completion resolveStmtList(parse::Tree& tree,
                           std::vector<std::unique_ptr<parse::Node>>& stmts,
                           diagnostic::Sink& diag) {
    // Local-class pre-pass: register every class defined DIRECTLY in this scope
    // (its entry + members land in the current frame, so they shadow and drop
    // with the scope) before resolving statements — so a use may precede the
    // definition. Every scoped body (function body, bare block, if/else arm,
    // loop body, switch case) resolves its statements through here, so a local
    // class works in any of them. Idempotent: a function body's top-level classes
    // were already registered by resolveFunctionBody (ahead of the nested-fn
    // signature pre-pass), so registerLocalClasses skips an already-registered one.
    // First relocate any external qualified defs (`int C:m(){}`, `C:Ns{}`, `C:R(){}`)
    // into their target — the external re-open form in a function body / nested block
    // — so the pre-pass registers them as members of that class/namespace.
    relocateOutOfLineMembers(tree, stmts, diag);
    registerLocalClasses(tree, stmts, diag);
    // Nested-function SIGNATURES, after the classes (so a nested function may name a
    // scope-local class in its signature) and before any statement — so a call may precede
    // the definition.
    registerNestedFunctions(tree, stmts, diag);
    Completion result = Completion::Normal;
    for (std::size_t i = 0; i < stmts.size(); i++) {
        if (!stmts[i]) continue;
        Completion c = resolveStmt(tree, *stmts[i], diag);
        // Stop at the first error (the design's first-error policy): resolving
        // later statements after one failed only spawns cascading follow-on
        // diagnostics (e.g. a bad for-iterable leaves its body-var undeclared).
        // No nested-function bodies are resolved on this path — main short-circuits
        // before classify, so an unresolved body never reaches it.
        if (diagnostic::hasErrors(diag)) return c;
        if (c == Completion::Abrupt) {
            // 2A: every statement after an abrupt one is unreachable. Flag the
            // next real statement (if any) once, then stop — dead code declares
            // no locals (so it can't trip the unused sweep) and resolving it
            // would only spawn follow-on diagnostics.
            for (std::size_t j = i + 1; j < stmts.size(); j++) {
                if (!stmts[j]) continue;
                diagnostic::report(diag, {stmts[j]->file_id, stmts[j]->tok,
                    "Unreachable statement.", {}});
                break;
            }
            result = Completion::Abrupt;
            break;
        }
    }
    // Nested-function BODIES last, with this scope's frame still open. Runs on the ABRUPT
    // path too: a definition after a `return` is unreachable CODE, but its body must still
    // be resolved or classify meets an unstamped identifier.
    resolveNestedFunctionBodies(tree, stmts, diag);
    return result;
}

// Collect every field reachable BARE in a method of class `cls`: its own fields (depth
// 0) then transitive base fields (depth 1…), a derived field SHADOWING a same-named base
// one (first-seen wins). Skips the internal `_$base` slot. Populates the method's field
// frame with kField entries — replacing the old method_fields name-list + baseFieldDepth
// per-site walk.
void collectMethodFields(parse::Tree& tree, widen::TypeRef cls,
                         std::vector<std::tuple<std::string, widen::TypeRef, int>>& out) {
    std::set<std::string> seen;
    int guard = static_cast<int>(tree.classes.size()) + 2;
    for (int depth = 0; cls != widen::kNoType && guard-- > 0; depth++) {
        auto it = tree.classes.find(cls);
        if (it == tree.classes.end()) break;
        parse::ClassInfo const& info = it->second;
        for (std::size_t i = 0; i < info.field_names.size(); i++) {
            std::string const& n = info.field_names[i];
            if (n == "_$base") continue;                 // internal base subobject slot
            if (!seen.insert(n).second) continue;        // a derived field shadows a base's
            widen::TypeRef ty = i < info.field_types.size()
                                    ? info.field_types[i] : widen::kNoType;
            out.emplace_back(n, ty, depth);
        }
        cls = parse::baseTypeOf(info);
    }
}

void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag, bool nested) {
    if (!nested) tree.nested_call_checks.clear();   // per top-level function body
    // Parameter defaults are constant expressions resolved in the ENCLOSING
    // (file) scope — before the body frame is pushed, so a default cannot
    // reference a parameter or a body local. (constfold then folds them and
    // classify requires the result to be a literal constant.)
    for (auto& p : fn.params) {
        if (p && !p->children.empty() && p->children[0]) {
            resolveExpr(tree, *p->children[0], diag);
        }
        // A const-expression array dim on a param (`int a[N]`) is likewise a
        // constant expression in the enclosing scope — resolve so constfold can
        // fold + bake it (it re-syncs param_types after).
        if (p) {
            for (auto& d : p->dim_exprs) {
                if (d) resolveExpr(tree, *d, diag);
            }
        }
    }
    // A const-expression array dim in the RETURN type (`int[N] f()` / a tuple-slot
    // return) lives on the function node — a constant expression in the enclosing
    // scope, like a param dim. constfold bakes it into the return type (== entry
    // slids_type for a function).
    for (auto& d : fn.dim_exprs) {
        if (d) resolveExpr(tree, *d, diag);
    }
    int saved_floor = tree.capture_floor;
    parse::Node* saved_capture_node = tree.capture_node;
    // A METHOD body resolves inside a transient FIELD FRAME (pushed OUTSIDE the body
    // frame): the class's fields — own + base — are kField entries, so a bare `f`
    // resolves like a local and is SHADOWED by a same-named body local (pushed inside).
    // Detected by the receiver param `_$recv`; a free/nested function has none. This
    // replaces tree.method_fields entirely.
    int field_frame_pushed = 0;
    for (auto& p : fn.params) {
        if (!p || p->name != "_$recv") continue;
        // strip() the pointee too: a template INSTANCE's receiver resolves
        // through the instantiation frame's self-name alias, so it arrives
        // alias-wrapped (`kAlias("Vec", Vec<int>)`), and tree.classes keys on
        // the stripped kSlid.
        widen::TypeRef cls =
            widen::strip(widen::get(widen::strip(p->return_type)).pointee);
        if (cls == widen::kNoType) break;
        parse::pushFrame(tree);
        field_frame_pushed = 1;
        std::vector<std::tuple<std::string, widen::TypeRef, int>> fields;
        collectMethodFields(tree, cls, fields);
        for (auto& [n, ty, d] : fields) {
            parse::Entry fe;
            fe.kind = parse::EntryKind::kField;
            fe.name = n;
            fe.slids_type = ty;
            fe.field_depth = d;
            fe.file_id = fn.file_id;
            fe.tok = fn.name_tok;
            parse::addEntry(tree, std::move(fe));
        }
        break;
    }
    parse::pushFrame(tree);
    if (nested) {
        // A kLocalVar resolved below this frame (a host local/param) is a capture.
        tree.capture_floor = parse::currentFrameId(tree);
        tree.capture_node = &fn;
    }
    // Bare `alias Ns;` imports inside this body extend the open-namespace chain;
    // restore it on exit so imports don't leak to sibling functions.
    std::size_t open_ns_at_entry = tree.open_ns_frames.size();
    // Definite-assignment + unused-local state is per-body; clear on entry and
    // restore on exit so one function's locals don't leak into the next.
    std::set<int> saved_initialized = std::move(tree.initialized_locals);
    std::set<int> saved_read = std::move(tree.read_locals);
    std::vector<int> saved_body_locals = std::move(tree.body_locals);
    tree.initialized_locals.clear();
    tree.assigned_arrays.clear();
    tree.read_locals.clear();
    tree.body_locals.clear();
    // Params become LocalVar entries in the body frame. Type spellings were
    // already validated in pass 1. A param arrives initialized (the caller
    // supplied its value), so it is seeded into initialized_locals.
    for (auto& p : fn.params) {
        if (!p) continue;
        int existing = parse::findInFrame(tree, parse::currentFrameId(tree), p->name);
        if (existing >= 0) {
            parse::Entry const& prev = tree.entries[existing];
            diagnostic::report(diag, {p->file_id, p->tok,
                "Duplicate parameter name '" + p->name + "'.",
                {{prev.file_id, prev.tok, "first declared here"}}});
            continue;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kLocalVar;
        e.name = p->name;
        e.slids_type = p->return_type;
        e.alias_label = p->alias_label;   // alias/enum spelling captured in pass 1
        e.file_id = p->file_id;
        e.tok = p->name_tok;
        p->resolved_entry_id = parse::addEntry(tree, std::move(e));
        tree.initialized_locals.insert(p->resolved_entry_id);
        // An ARRAY param arrives initialized too — seed the monotonic may-set so a
        // read isn't a use-before-init (a direct `int a[3]` / `int a[N]`; the alias
        // form `A3 a` reads as non-array so it uses initialized_locals above).
        if (isArrayType(p->return_type))
            tree.assigned_arrays.insert(p->resolved_entry_id);
    }
    // `self` — the receiver OBJECT in a method / ctor / dtor body. CANONICAL
    // author<->compiler mapping (the transmogrification):
    //     author  self          ==  compiler  _$recv^   (the object)
    //     author  self.field    ==  compiler  _$recv^.field
    //     author  self.method() ==  compiler  _$recv^.method()
    //     author  ^self         ==  compiler  _$recv     (address-of-object = the pointer)
    // `self` is the AUTHOR's view (an object); `_$recv` is the COMPILER's view (a
    // `Class^` reference to that object). `self` is NEVER a pointer, so `self^` is
    // nonsense in both views — internal code names the receiver machinery `_$recv`
    // / `_$recv^`, never "self". Here `self` is registered as an address-aliased
    // LOCAL of the class type whose storage IS `_$recv^`, so the explicit author
    // forms `self`, `self.field`, `self.method()`, and `^self` all flow through the
    // ordinary local-variable machinery; codegen binds its address to `_$recv^` at
    // the prologue. (Implicit bare `field` / `method()` don't use this local — they
    // rewrite to `_$recv^.field` / `_$recv^.method()` directly; see buildRecvDeref.)
    // Registered like a param (NOT in body_locals, so the unused-local sweep
    // ignores it); `self` is a reserved word so it never collides with a user name.
    for (auto& p : fn.params) {
        if (!p || p->name != "_$recv") continue;
        widen::TypeRef recv_ty = tree.entries[p->resolved_entry_id].slids_type;
        parse::Entry se;
        se.kind = parse::EntryKind::kLocalVar;
        se.name = "self";
        se.slids_type = widen::get(widen::strip(recv_ty)).pointee;
        se.file_id = fn.file_id;
        se.tok = fn.name_tok;
        fn.self_entry_id = parse::addEntry(tree, std::move(se));
        tree.initialized_locals.insert(fn.self_entry_id);
        break;
    }
    // Local-class pre-pass for the body's TOP LEVEL. BEFORE the const pre-pass
    // (so a const decl whose type names a body-local class — `const Class c = ...`
    // — resolves) AND before the nested-function pre-pass (so a nested function may
    // name a body-local class in its signature, `void use(LocalClass^ p)`).
    // resolveStmtList's own pre-pass (covering nested blocks) is idempotent — it
    // skips a class already registered here — so these don't double-register.
    // Relocate external qualified defs first (so a `int C:m(){}` beside a body-local
    // `C` registers as C's method before this pre-pass runs).
    relocateOutOfLineMembers(tree, fn.children, diag);
    registerLocalClasses(tree, fn.children, diag);
    // Forward-decl pre-pass for kConst: pre-create entries so const init
    // expressions can reference later-declared consts in the same body.
    // Dup detection is deferred to the main pass below (which emits a single
    // "Duplicate declaration" diagnostic with a multi-source note).
    for (auto& ch : fn.children) {
        if (!ch || ch->kind != parse::Kind::kVarDeclStmt || !ch->is_const) continue;
        if (isQualified(*ch)) continue;  // qualified const is a namespace member
        if (parse::findInFrame(tree, parse::currentFrameId(tree), ch->name) >= 0) {
            continue;  // pre-pass yields to main-pass dup detection
        }
        // A typeless const has no spelling to validate; constfold infers its
        // type from the rhs and stamps slids_type later.
        if (ch->return_type != widen::kNoType) {
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        }
        // A const whose type isn't a foldable scalar (array/tuple/class/pointer) is
        // a not-mutable VARIABLE, not a substituted constant: route it to a local
        // (allocated + initialized) with a deep-const-qualified type. const is not
        // yet enforced (Phase 6), so it behaves as an ordinary local for now.
        bool needs_storage = constNeedsStorage(ch->return_type);
        if (needs_storage) ch->return_type = widen::deepConst(ch->return_type);
        parse::Entry e;
        e.kind = needs_storage ? parse::EntryKind::kLocalVar
                               : parse::EntryKind::kConst;
        e.name = ch->name;
        e.slids_type = ch->return_type;
        e.file_id = ch->file_id;
        e.tok = ch->name_tok;
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
        if (needs_storage) tree.body_locals.push_back(ch->resolved_entry_id);
    }
    // Local classes AND nested functions (signatures, then bodies) are registered by
    // resolveStmtList's per-SCOPE pre-passes — which run for every scope, including nested
    // blocks / if-arms / loop bodies / switch cases. So a function nested in a BLOCK is
    // registered in that block's frame and its body is resolved with that frame open,
    // exactly as a top-level nested function is with the body frame open.
    resolveStmtList(tree, fn.children, diag);
    if (!nested) {
        // Now every nested function's captures are known: a host-level call must
        // have each captured host variable definitely-assigned at the call.
        for (auto const& chk : tree.nested_call_checks) {
            for (int cap : tree.entries[chk.entry].captures) {
                if (chk.snapshot.count(cap) == 0) {
                    diagnostic::report(diag, {chk.file_id, chk.tok,
                        "Use of uninitialized variable '"
                            + tree.entries[cap].name + "'.", {}});
                }
            }
        }
        tree.nested_call_checks.clear();
    }
    sweepUnusedLocals(tree, diag);   // function-body declarations
    tree.body_locals = std::move(saved_body_locals);
    // A captured host local is used by the nested function — count it as read in
    // the HOST's unused-local sweep (its reads happened in the cleared nested
    // body state).
    for (int c : fn.captures) saved_read.insert(c);
    tree.read_locals = std::move(saved_read);
    tree.initialized_locals = std::move(saved_initialized);
    tree.open_ns_frames.resize(open_ns_at_entry);
    if (nested && fn.resolved_entry_id >= 0) {
        // Publish the captured-entry list on the function entry so call sites
        // (classify) and codegen can pass / receive the captures.
        tree.entries[fn.resolved_entry_id].captures = fn.captures;
    }
    tree.capture_floor = saved_floor;
    tree.capture_node = saved_capture_node;
    parse::popFrame(tree);
    if (field_frame_pushed) parse::popFrame(tree);   // drop the method's field frame
}

void registerClassName(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag,
                       int member_of = -1, bool file_scope_def_id = false);
void registerClassBody(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag);
void checkClassByValueAcyclic(parse::Tree& tree, parse::Node& node,
                              diagnostic::Sink& diag);

// The NAME phase for a scope (a namespace or a class — and via run() the program):
// register every member's NAME / entry into `frame`, recursing through nested
// namespaces AND classes. ONE routine replaces registerClassMembers +
// registerNamespaceTree + registerMemberSignature + the cross-registration arms.
// Order: nested TYPE names first (class names via name-only registerClassName +
// recurse, namespace frames via openNamespace + recurse, enums), then member ALIASES
// (target may now name a sibling class), then VALUE members (const, function/method)
// whose entries carry PROVISIONAL signature types — resolveScopeTypes resolves those
// after every name across every scope exists. Each nested class NODE is appended to
// `classes` so the caller can run the global BODY phase (registerClassBody) after ALL
// names, letting a field forward-reference any class regardless of scope.
// Build + register a simple variable entry (const / global) from a var-decl node,
// carrying its provisional type. `owner_ns_frame < 0` (the default) resolves bare;
// a namespace/class member passes its owning frame. Returns the new entry id.
int addVarEntry(parse::Tree& tree, parse::EntryKind kind, parse::Node const& d,
                int owner_ns_frame = -1) {
    parse::Entry e;
    e.kind = kind;
    e.name = d.name;
    e.slids_type = d.return_type;
    e.file_id = d.file_id;
    e.tok = d.name_tok;
    e.owner_ns_frame = owner_ns_frame;
    // A GLOBAL declared in an imported `.slh` is a cross-TU DECLARATION (external),
    // defined in exactly one linked `.sl`; a `.sl` global is a definition. (A const is
    // always local — its value is substituted, so there is nothing to link.)
    if (kind == parse::EntryKind::kGlobalVar) {
        e.is_external = fileIsImported(tree, d.file_id);
        e.defined = !e.is_external;
    }
    return parse::addEntry(tree, std::move(e));
}

// A header-declared global (is_external) MERGES with its definition in this TU — the
// same decl+def merge a namespace function gets. Returns the merged entry id, or -1 if
// `prev` is not a mergeable header-global declaration.
int tryMergeGlobalDecl(parse::Tree& tree, int prev, parse::Node const& d) {
    if (prev < 0 || !d.is_global) return -1;
    parse::Entry& pe = tree.entries[prev];
    if (pe.kind != parse::EntryKind::kGlobalVar || !pe.is_external) return -1;
    if (fileIsImported(tree, d.file_id)) return -1;   // another header decl, not a def
    pe.is_external = false;
    pe.defined = true;
    pe.def_file_id = d.file_id;
    pe.def_tok = d.name_tok;
    return prev;
}

// Register a CLASS TEMPLATE definition: the node is a PATTERN — no ClassInfo, no
// member registration, no type — and its body stays in pristine parse state until
// a use supplies a type-list (resolveTypeRef's kTmplUse arm instantiates on
// demand). The entry is a kClass with is_template set and NO slids_type/ns_frame,
// so nothing downstream can mistake it for a real class. A template owns its name
// in its scope. `owner` >= 0 registers a namespace/class member.
void registerClassTemplate(parse::Tree& tree, parse::Node& node,
                           std::vector<std::unique_ptr<parse::Node>>* host_list,
                           int owner, diagnostic::Sink& diag) {
    // The deferred forms, rejected up front (they would otherwise ride the
    // clone). Both scans run before the re-open divert below, so they cover
    // EVERY opening, and both fire only for a HEADER-owned template — a
    // TU-local template keeps its nested classes (tmpl_class.sl Kit:Sub) AND
    // its template methods (tmpl_nested.sl): the flavor clone re-registers
    // them per instance through the ordinary member diverts. A header's
    // flavors are declaration-only with bodies aggregated by --instantiate,
    // and an inner pattern's instances aren't knowable there — no delivery
    // channel, so both forms stay rejected cross-TU. fileIsImported covers
    // the header's openings AND a loaded template source's re-opens alike.
    for (auto& m : node.children) {
        if (!m) continue;
        if (!fileIsImported(tree, node.file_id)) break;
        if ((m->kind == parse::Kind::kFunctionDef
             || m->kind == parse::Kind::kFunctionDecl)
            && !m->type_params.empty()) {
            diagnostic::report(diag, {m->file_id, m->name_tok,
                "A class template declared in a header may not contain a "
                "template method (not supported yet).", {}});
            return;
        }
        if (m->kind == parse::Kind::kClassDef) {
            diagnostic::report(diag, {m->file_id, m->name_tok,
                "A class template declared in a header may not contain a "
                "nested class (not supported yet).", {}});
            return;
        }
    }
    int prev = owner >= 0
        ? findMemberDeclared(tree, owner, node.name)
        : parse::findInFrame(tree, parse::currentFrameId(tree), node.name);
    if (prev >= 0) {
        // A same-scope same-name CLASS TEMPLATE: this opening is a RE-OPEN.
        // Record it pristine; instantiation clones every opening and the
        // clones re-run the plain-class merge. The `...` state machine runs
        // HERE, at the pattern, so the field rules fire at the declaration
        // whether or not anyone instantiates.
        if (tree.entries[prev].kind == parse::EntryKind::kClass
            && tree.entries[prev].is_template) {
            auto it = tree.templates.find(prev);
            if (it == tree.templates.end()) return;   // registration errored
            parse::TemplateInfo& ti = it->second;
            // A HEADER template's openings belong to its module: further
            // header openings and the loaded/compiling source's. A CONSUMER
            // re-open would grow the flavors in THIS TU only — silently
            // diverging from every other TU's (and from the aggregated
            // definitions).
            if (fileIsImported(tree, ti.def->file_id)
                && !fileIsImported(tree, node.file_id)
                && !fileIsTemplateSource(tree, node.file_id)
                && !fileIsSibling(tree, ti.def->file_id)) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "A template declared in a header is defined by its "
                    "module's source file; '" + node.name
                    + "' cannot be re-opened here.",
                    {{ti.def->file_id, ti.def->name_tok, "declared here"}}});
                return;
            }
            if (node.type_params != ti.def->type_params) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "A re-open's template list must match class template '"
                    + node.name + "'s.",
                    {{ti.def->file_id, ti.def->name_tok, "first declared here"}}});
                return;
            }
            bool has_fields = false;
            for (auto& p : node.params)
                if (p && p->name != "_$vptr" && p->name != "_$base") {
                    has_fields = true;
                    break;
                }
            if (ti.cls_open) {
                ti.cls_open = node.is_incomplete;   // no trailing `...` -> closed
            } else if (has_fields) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "Duplicate definition of class template '" + node.name
                    + "'; a re-open cannot add fields (use '" + node.name
                    + "<...>()' to add members).",
                    {{ti.def->file_id, ti.def->name_tok, "first defined here"}}});
                return;
            } else if (node.is_incomplete) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "Class template '" + node.name + "' is already complete; it "
                    "cannot be re-opened as incomplete.",
                    {{ti.def->file_id, ti.def->name_tok, "first defined here"}}});
                return;
            }
            node.resolved_entry_id = prev;
            ti.reopens.push_back(&node);
            return;
        }
        parse::Entry const& pe = tree.entries[prev];
        reportNameCollision(diag, "Duplicate declaration of '" + node.name + "'.",
                            pe.file_id, pe.tok, node.file_id, node.name_tok);
        return;
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kClass;
    e.name = node.name;
    e.is_template = true;
    e.defined = true;
    e.file_id = node.file_id;
    e.tok = node.name_tok;
    if (owner >= 0) e.owner_ns_frame = owner;
    node.resolved_entry_id = parse::addEntry(tree, std::move(e));

    parse::TemplateInfo ti;
    ti.def = &node;
    ti.host_list = host_list;
    ti.cls_open = node.is_incomplete;   // trailing `...` — re-opens may append
    tree.templates[node.resolved_entry_id] = std::move(ti);

    // A NESTED CLASS TEMPLATE (tmpl_nested.sl): register a SUB-PATTERN under
    // the qualified spelling ("Outer:Inner"), the only spelling that reaches
    // it — lookupAliasTemplateEntry's full-name divert. Its template list is
    // SELF-CONTAINED: it names every parameter it uses (including any of the
    // outer's — `UClass<U, T>` re-lists T); the outer contributes only the
    // name qualifier, so no outer flavor is implied or required. The entry
    // name carries the ':', so the bare inner name resolves nowhere outside.
    // Instances splice into the OUTER's host list, ordinary classes there.
    // (A plain listless nested class stays as-is — it rides the flavor clone,
    // tmpl_class.sl Kit:Sub. A flavor clone's copy of this nested template
    // re-registers per instance through the member divert, for inside use.)
    for (auto& m : node.children) {
        if (!m || m->kind != parse::Kind::kClassDef || m->type_params.empty())
            continue;
        parse::Entry se;
        se.kind = parse::EntryKind::kClass;
        se.name = node.name + ":" + m->name;
        se.is_template = true;
        se.defined = true;
        se.file_id = m->file_id;
        se.tok = m->name_tok;
        m->resolved_entry_id = parse::addEntry(tree, std::move(se));
        parse::TemplateInfo sti;
        sti.def = m.get();
        sti.host_list = host_list;
        sti.cls_open = m->is_incomplete;
        tree.templates[m->resolved_entry_id] = std::move(sti);
    }
}

void registerScopeNames(parse::Tree& tree, parse::Node& node, int frame,
                        std::vector<parse::Node*>& classes,
                        diagnostic::Sink& diag) {
    // External qualified defs among this scope's members (`int Sib:m(){}`, `Sib:Ns{}`,
    // `Sib:R(){}` targeting a sibling class/namespace declared here) relocate into
    // their target BEFORE we register anything — the external re-open form in a
    // namespace / class body.
    relocateOutOfLineMembers(tree, node.children, diag);
    auto isDup = [&](parse::Node& m) {
        int prev = findMemberDeclared(tree, frame, m.name);
        if (prev >= 0) {
            parse::Entry const& pe = tree.entries[prev];
            diagnostic::report(diag, {m.file_id, m.name_tok,
                "Duplicate declaration of '" + m.name + "'.",
                {{pe.file_id, pe.tok, "first declared here"}}});
            return true;
        }
        return false;
    };
    // The frame is OPEN so a member alias target / nested-class field name resolves
    // bare via the scope frame stack.
    tree.open_ns_frames.push_back(frame);
    // Type names first: nested classes (name-only, collected for the body phase),
    // nested namespace frames, enums. (This loop visits every child, so the assert
    // guards the whole member set — NO silent default — for all three sub-passes.)
    for (auto& m : node.children) {
        if (!m) continue;
        assert(isScopeMember(*m) && "unexpected scope-member kind in NAME phase");
        if (m->kind == parse::Kind::kClassDef) {
            // A member CLASS TEMPLATE: register the pattern (no members, no
            // ClassInfo — the body stays pristine); uses instantiate it.
            if (!m->type_params.empty()) {
                registerClassTemplate(tree, *m, &node.children, frame, diag);
                continue;
            }
            registerClassName(tree, *m, diag, frame);
            if (!m->is_reopen) classes.push_back(m.get());   // a re-open owns no body
            if (m->resolved_entry_id >= 0)
                registerScopeNames(tree, *m,
                    tree.entries[m->resolved_entry_id].ns_frame_id, classes, diag);
        } else if (m->kind == parse::Kind::kNamespaceDecl) {
            int ns = openNamespace(tree, m->name, m->name_tok, m->file_id, frame, diag);
            if (ns < 0) continue;
            m->resolved_entry_id = ns;
            registerScopeNames(tree, *m, ns, classes, diag);
        } else if (m->kind == parse::Kind::kEnumDecl) {
            // A qualified enum was moved into a local class, or (remote namespace)
            // registered in place by relocation — already done; skip.
            if (isQualified(*m)) continue;
            registerEnum(tree, *m, frame, diag);
        }
    }
    // Member aliases — entry with a PROVISIONAL target (resolveScopeTypes resolves
    // the target after all names exist, so an alias may name any class). A qualified
    // external alias was moved into a local class, or (remote namespace) registered in
    // place by relocation — only local aliases (and bare imports) remain here.
    for (auto& m : node.children) {
        if (!m || m->kind != parse::Kind::kAliasDecl) continue;
        if (isQualified(*m) && m->return_type != widen::kNoType) continue;
        // A FUNCTION alias merges into the overload set later — no kAlias entry (which
        // would collide with a same-name function), just a deferred duplication request.
        if (isFuncAlias(tree, *m)) { recordFuncAlias(tree, *m, frame); continue; }
        if (isDup(*m)) continue;
        parse::Entry e;
        e.kind = parse::EntryKind::kAlias;
        e.name = m->name;
        e.slids_type = m->return_type;   // provisional
        e.file_id = m->file_id;
        e.tok = m->name_tok;
        e.owner_ns_frame = frame;
        m->resolved_entry_id = parse::addEntry(tree, std::move(e));
        if (!m->type_params.empty()) registerAliasTemplate(tree, *m);
    }
    // Value members — const + function/method. Entries carry PROVISIONAL signature
    // types (resolveScopeTypes resolves them); a method's param_types includes the
    // implicit `_$recv` at [0]. ctor/dtor (`_$ctor`/`_$dtor`) are hooks, not entries.
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kVarDeclStmt && (m->is_const || m->is_global)) {
            if (isQualified(*m)) continue;   // remote-namespace member, done by relocation
            int merged = tryMergeGlobalDecl(tree, findMemberDeclared(tree, frame, m->name), *m);
            if (merged >= 0) { m->resolved_entry_id = merged; continue; }
            if (isDup(*m)) continue;
            parse::EntryKind kind = m->is_global ? parse::EntryKind::kGlobalVar
                                                 : parse::EntryKind::kConst;
            m->resolved_entry_id = addVarEntry(tree, kind, *m, frame);
        } else if (m->kind == parse::Kind::kFunctionDef
                || m->kind == parse::Kind::kFunctionDecl) {
            if (m->name == "_$ctor" || m->name == "_$dtor") continue;
            // A MEMBER template — namespace function or class METHOD (the parse
            // spliced `_$recv` into a method's params like any method): register
            // with this frame as owner; patterns resolve in the TYPES phase, the
            // scope-bodies pass takes the snapshot.
            if (!m->type_params.empty()) {
                registerTemplateFunction(tree, *m, &node.children,
                                         /*nested=*/false, frame, diag);
                continue;
            }
            // A TEMPLATE member owns its name — a plain same-name method is a
            // collision, not an overload (the reverse direction is checked by
            // registerTemplateFunction itself).
            if (int tmpl = findSameScopeTemplate(tree, m->name, frame); tmpl >= 0) {
                reportTemplateNameClash(diag, *m, tree.entries[tmpl]);
                continue;
            }
            // A same-name CLASS METHOD is an OVERLOAD — register it as a separate
            // entry (classify picks among them by signature; an identical-signature
            // pair is caught as a dup DEFINITION there). A same-name NON-function
            // member (a const / alias / enum / nested class — a field is a slot, not
            // a frame member) is a true duplicate. In a NAMESPACE a same-name function
            // stays a duplicate too: only class methods + file-scope functions resolve
            // overloaded calls today.
            bool is_class_frame = (node.kind == parse::Kind::kClassDef);
            int prev = findMemberDeclared(tree, frame, m->name);
            // A NAMESPACE function's DECLARATION and DEFINITION merge into ONE entry —
            // the rule a file-scope function already has. A namespace does not overload,
            // so a same-name pair can only be a decl + its def, never two functions; and
            // the pair is now routine, since a header declares `void Space:goodbye_world();`
            // and its source defines it. Without the merge the two read as a duplicate.
            // (A CLASS frame keeps separate entries — same-name methods ARE overloads —
            // and desugar's methodSymbol counts distinct SIGNATURES so a decl+def pair
            // there does not read as one.)
            if (!is_class_frame && prev >= 0
                && tree.entries[prev].kind == parse::EntryKind::kFunction
                && m->kind == parse::Kind::kFunctionDef
                && !tree.entries[prev].defined) {
                parse::Entry& pe = tree.entries[prev];
                pe.defined = true;
                pe.is_external = false;      // declared in a header, but defined HERE
                pe.def_file_id = m->file_id;
                pe.def_tok = m->name_tok;
                m->resolved_entry_id = prev;
                continue;
            }
            if (prev >= 0
                && !(is_class_frame
                     && tree.entries[prev].kind == parse::EntryKind::kFunction)) {
                parse::Entry const& pe = tree.entries[prev];
                diagnostic::report(diag, {m->file_id, m->name_tok,
                    "Duplicate declaration of '" + m->name + "'.",
                    {{pe.file_id, pe.tok, "first declared here"}}});
                continue;
            }
            std::vector<widen::TypeRef> ptypes;
            for (auto& p : m->params) {
                if (!p) continue;
                ptypes.push_back(p->return_type);   // provisional
            }
            parse::Entry e;
            e.kind = parse::EntryKind::kFunction;
            e.name = m->name;
            e.slids_type = m->return_type;   // provisional
            e.param_types = std::move(ptypes);
            e.file_id = m->file_id;
            e.tok = m->name_tok;
            e.defined = (m->kind == parse::Kind::kFunctionDef);
            e.is_external = !e.defined && fileIsImported(tree, m->file_id);
            e.is_virtual = m->is_virtual;
            e.is_pure = m->is_pure;
            e.is_foreign = m->is_foreign;
            if (e.defined) { e.def_file_id = m->file_id; e.def_tok = m->name_tok; }
            e.owner_ns_frame = frame;
            m->resolved_entry_id = parse::addEntry(tree, std::move(e));
        }
    }
    tree.open_ns_frames.pop_back();
}

bool fieldContributesNeed(parse::Tree const& tree, widen::TypeRef ft, bool ctor);

// Class registration is TWO-PHASE so a class field may forward-reference a
// sibling class defined later: registerClassName makes every class NAME a known
// type (a kClass entry + a SLOTLESS interned handle + a placeholder ClassInfo),
// then registerClassBody resolves field types (now all sibling names validate)
// and attaches the slots. The kSlid HANDLE is stable across slotless->slotful —
// structKey is "S"+name(+"#def_id"), slots are not in the key — so a field that
// referenced the forward class shares the very handle that later gains its layout.

// Phase 1: register the class NAME. A LOCAL class disambiguates by its defining
// FRAME id (the kSlid's def_id), NOT a mangled name — the name stays bare
// everywhere. A duplicate is a same-named class already declared in THIS frame.
// `member_of >= 0`: a HOISTED class — a namespace-member of that host class frame
// (owner_ns_frame = member_of, def_id = member_of so `Host:Inner` is distinct,
// dup-checked among the host's members); reached only via `Host:Inner`, not bare.
void registerClassName(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag,
                       int member_of, bool file_scope_def_id) {
    int decl_frame = parse::currentFrameId(tree);
    // file_scope_def_id: a cross-TU template INSTANCE registers in a transient
    // frame but its symbol must be STABLE across every TU that names the
    // flavor — the canonical spelling alone identifies it, so it takes the
    // file-scope def_id (no `.frame` suffix) regardless of the current frame.
    int def_id = file_scope_def_id ? -1
               : member_of >= 0 ? member_of
               : (decl_frame == kGlobalFrame) ? -1 : decl_frame;
    int prev_id = member_of >= 0
        ? findMemberDeclared(tree, member_of, node.name)
        : parse::findInFrame(tree, decl_frame, node.name);
    if (prev_id >= 0) {
        parse::Entry const& prev = tree.entries[prev_id];
        // A same-name CLASS already declared in this frame is a RE-OPEN: the new
        // opening merges its members into the existing class frame. It may add NO
        // fields (the layout is the primary's) — a field-bearing re-open is an
        // error. Point this node at the primary's entry so the caller's member
        // recursion registers into the shared frame; the class BODY passes
        // (registerClassBody / cycle / needs) skip a reopen node.
        if (prev.kind == parse::EntryKind::kClass) {
            // A class TEMPLATE owns its name: a LISTLESS opening is no re-open
            // (every opening spells the template list — the divert above takes
            // those), and a plain class cannot share the name.
            if (prev.is_template) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "A class template owns its name; a re-open of '" + node.name
                    + "' must repeat its template list.",
                    {{prev.file_id, prev.tok, "template declared here"}}});
                return;
            }
            // The synthetic `_$vptr` / `_$base` are not user fields — a re-open whose
            // segment has a `virtual` member picks up a spurious `_$vptr` from the parser
            // (it does not know yet this is a re-open); ignore it.
            parse::ClassInfo& info = tree.classes.at(widen::strip(prev.slids_type));
            bool has_fields = false;
            for (auto& p : node.params)
                if (p && p->name != "_$vptr" && p->name != "_$base") { has_fields = true; break; }
            // The re-open rule is stateful on the class's OPEN (incomplete) flag:
            //   open   + fields  -> APPEND to the layout (still growing)
            //   open   + none    -> just merge body members
            //   closed + fields  -> error: a complete class adds no fields
            //   closed + `...`   -> error: a complete class cannot re-open incomplete
            // Either way the re-open closes (or keeps) the class per its own `...`.
            if (info.is_open) {
                // Point at the appended fields; leave them OWNED by this re-open node so
                // the body phase resolves their default exprs (resolveClassMemberBodies
                // walks node.params with the class frame open). registerClassBody interns
                // them onto the primary's layout.
                if (has_fields)
                    for (auto& p : node.params)
                        if (p && p->name != "_$vptr" && p->name != "_$base")
                            info.pending_fields.push_back(p.get());
                info.is_open = node.is_incomplete;   // no trailing `...` -> now closed
            } else if (has_fields) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "Duplicate definition of class '" + node.name + "'; a re-open "
                    "cannot add fields (use '" + node.name + "()' to add members).",
                    {{prev.file_id, prev.tok, "first defined here"}}});
                return;
            } else if (node.is_incomplete) {
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "Class '" + node.name + "' is already complete; it cannot be "
                    "re-opened as incomplete.",
                    {{prev.file_id, prev.tok, "first defined here"}}});
                return;
            }
            // A re-open's implicitly-invoked members are the PRIMARY's lifecycle: point at
            // them so registerClassBody's per-class scans see every opening (they stay
            // owned by this node, so the body phase resolves them here). Without this the
            // primary reads has_ctor=false and the hook is never called.
            for (auto& m : node.children)
                if (m && parse::isImplicitMember(m->name))
                    info.pending_hooks.push_back(m.get());
            // The layout is the primary's regardless (a re-open node skips the class
            // BODY passes); any appended fields flow to the primary via pending_fields.
            node.is_reopen = true;
            node.resolved_entry_id = prev_id;
            node.return_type = prev.slids_type;
            return;
        }
        // A same-name NON-class entry (alias/enum/namespace/const/function) collides.
        reportNameCollision(diag, "Duplicate declaration of '" + node.name + "'.",
                            prev.file_id, prev.tok, node.file_id, node.name_tok);
        return;
    }
    // Slotless handle now; registerClassBody re-interns with the field slots (same
    // handle). The name becomes a KNOWN type immediately (placeholder ClassInfo).
    widen::TypeRef type = widen::internSlid(node.name, {}, def_id);
    node.return_type = type;
    int cls_frame = parse::allocFrameId(tree);
    parse::Entry e;
    e.kind = parse::EntryKind::kClass;
    e.name = node.name;
    e.ns_frame_id = cls_frame;
    e.slids_type = type;
    e.owner_ns_frame = member_of;   // -1 lexical, else a member of the host frame
    e.file_id = node.file_id;
    e.tok = node.name_tok;
    node.resolved_entry_id = parse::addEntry(tree, std::move(e));
    // Emplace the placeholder ClassInfo BEFORE registering members, so a method
    // whose signature names its OWN class (`Self^ m(Self^)`) sees a KNOWN type
    // (requireKnownType -> leafIsKnownClass checks tree.classes). The slots are
    // still filled in Phase 2 (registerClassBody); only type-known-ness matters here.
    parse::ClassInfo info;
    info.name = node.name;           // bare; def_id carries the scope distinction
    info.def_file_id = node.file_id;
    info.def_tok = node.name_tok;
    info.type = type;
    info.is_open = node.is_incomplete;   // an INCOMPLETE primary accepts appended fields
    info.declared_incomplete = node.is_incomplete;   // persistent (see ClassInfo)
    tree.classes.emplace(type, std::move(info));   // placeholder; fields filled in Phase 2
    // NAME-only: the class's members are registered by the caller (registerScopeNames
    // recurses into this class's frame), so all class NAMES across all scopes register
    // before any field BODY resolves (the global two-phase, cross-scope forward refs).
    (void)cls_frame;
}

// Resolve the field types (every class name is known now, so a forward field
// reference validates) and attach the slots to the class's handle; set the direct
// ctor/dtor needs. The CALLER (resolveScopeTypes) has already pushed this class's
// frame AND its enclosing chain (the TYPES recursion), so a field may name a hoisted
// member / enclosing sibling bare — this routine touches no frames.
void registerClassBody(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag) {
    if (node.resolved_entry_id < 0) return;        // a duplicate (Phase 1 skipped it)
    if (node.is_reopen) return;                    // a re-open adds no fields; the
                                                   // primary owns the layout + lifecycle
    widen::TypeRef type = node.return_type;
    parse::ClassInfo& info = tree.classes.at(widen::strip(type));
    int def_id = widen::get(widen::strip(type)).def_id;
    std::vector<std::pair<int, int>> field_locs;   // first-seen loc per field name
    // One field-slot funnel for the primary's own fields AND the fields appended by
    // open re-opens of an INCOMPLETE class (info.pending_fields) — so the layout is
    // interned in exactly one place regardless of how many re-opens grew it.
    auto addField = [&](parse::Node* p) {
        if (!p) return;
        int dup = info.fieldIndex(p->name);
        if (dup >= 0) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "Duplicate field '" + p->name + "' in class '" + node.name + "'.",
                {{field_locs[dup].first, field_locs[dup].second,
                  "first declared here"}}});
            return;
        }
        if (p->return_type == widen::kNoType) {
            // A typeless field with a DEFAULT infers its type from that default — but
            // the field is a kSlid LAYOUT slot and the default isn't folded yet, so
            // DEFER: register a kNoType slot now; classify's classifyClassSignature
            // folds the default, infers the type, and re-interns the handle (same
            // name+def_id key). With NO default there is nothing to infer from.
            if (p->children.empty() || !p->children[0]) {
                diagnostic::report(diag, {p->file_id, p->name_tok,
                    "Field '" + p->name + "' needs an explicit type.", {}});
                return;
            }
        } else {
            resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
        }
        info.field_names.push_back(p->name);
        info.field_types.push_back(p->return_type);   // kNoType -> inferred in classify
        info.field_params.push_back(p);   // stable; default read live later
        field_locs.push_back({p->file_id, p->name_tok});
    };
    for (auto& p : node.params) addField(p.get());
    for (parse::Node* p : info.pending_fields) addField(p);
    // Constructor / destructor presence (parsed as `_$ctor`/`_$dtor` members), over
    // EVERY opening: the primary's own members plus the hooks re-opens contributed
    // (info.pending_hooks). The DECLARATION `_();` and the DEFINITION `_(){}` may sit
    // in different openings — the author reads them as one class definition — so both
    // the presence answer and the obligation a declaration creates are per CLASS, and
    // this is the only place that sees the whole class.
    // WHERE THE CLASS IS DECLARED decides how this TU emits its symbols. A `.sl` class is
    // private to this TU (kInternal — nothing outside can name it, so two unrelated
    // sources may each declare a class of the same name). A `.slh` class is shared: its
    // SIBLING `.sl` emits the synthesized symbols (kDefine) and every other importer only
    // declares them (kDeclare).
    bool header_class = fileIsImported(tree, node.file_id);
    widen::setSlidLinkage(widen::strip(type),
        !header_class                            ? widen::Type::Linkage::kInternal
        : fileIsSibling(tree, node.file_id)      ? widen::Type::Linkage::kDefine
                                                 : widen::Type::Linkage::kDeclare);
    // OPAQUE cross-TU: a HEADER class declared incomplete (a `...`) has private fields an
    // importer cannot see — its size is a runtime function, not a folded constant. True in
    // the completer too (it must export the size for importers). A `.sl`-local incomplete
    // class is complete within its own TU (no importer), so it is never opaque.
    widen::setSlidOpaque(widen::strip(type), header_class && info.declared_incomplete);
    // A source file cannot ADD an implicitly-invoked member to a class declared in a
    // HEADER. The five are called without the author naming them, so every importing TU
    // emits those calls off the header ALONE: one that exists only in some `.sl` would
    // make that TU disagree, silently, with every other about what constructing or
    // copying the class does — and its symbol would collide with the sibling's
    // synthesized default besides.
    //
    // The ban is on ADDING, not on defining. A member the header DECLARES may be defined
    // in any one source (that is the whole `_();` + `_(){}` pattern, and it holds for the
    // sibling and non-sibling alike), so the question this asks is whether the HEADER
    // declared it — never where the definition lives. Asked per CLASS, over every
    // opening, because a re-open in a `.sl` is one of the spellings that adds one.
    if (header_class) {
        for (char const* member : {"_$ctor", "_$dtor", "op=", "op<--", "op<-->"}) {
            parse::Node const* added = nullptr;
            bool in_header = false;
            auto scanAdd = [&](parse::Node const* m) {
                if (!m || m->name != member) return;
                if (m->kind != parse::Kind::kFunctionDef
                    && m->kind != parse::Kind::kFunctionDecl) return;
                if (fileIsImported(tree, m->file_id)) in_header = true;
                else if (!added) added = m;
            };
            for (auto& m : node.children) scanAdd(m.get());
            for (parse::Node* m : info.pending_hooks) scanAdd(m);
            if (added && !in_header) {
                diagnostic::report(diag, {added->file_id, added->name_tok,
                    "A source file cannot add "
                    + std::string(parse::implicitMemberNoun(member)) + " to class '"
                    + node.name + "', which is declared in a header.",
                    {{node.file_id, node.name_tok, "class declared here"}}});
            }
        }
    }
    // A DUPLICATE hook is diagnosed here too, and for the same reason: the two definitions
    // may sit in different openings. Keeping the first def node gives the "first defined
    // here" note a method's duplicate already gets.
    bool ctor_declared = false, dtor_declared = false;
    parse::Node const* ctor_def = nullptr;
    parse::Node const* dtor_def = nullptr;
    auto scanHook = [&](parse::Node const* m) {
        if (!m) return;
        bool is_ctor = (m->name == "_$ctor");
        if (!is_ctor && m->name != "_$dtor") return;
        bool& declared = is_ctor ? ctor_declared : dtor_declared;
        parse::Node const*& def = is_ctor ? ctor_def : dtor_def;
        if (m->kind != parse::Kind::kFunctionDef) { declared = true; return; }
        if (def) {
            diagnostic::report(diag, {m->file_id, m->name_tok,
                is_ctor ? "Duplicate constructor." : "Duplicate destructor.",
                {{def->file_id, def->name_tok, "first defined here"}}});
            return;
        }
        def = m;
    };
    for (auto& m : node.children) scanHook(m.get());
    for (parse::Node* m : info.pending_hooks) scanHook(m);
    bool ctor_defined = (ctor_def != nullptr);
    bool dtor_defined = (dtor_def != nullptr);
    bool has_ctor = ctor_declared || ctor_defined;
    bool has_dtor = dtor_declared || dtor_defined;
    // The ctor/dtor contract, over the whole class. Both halves are reported here and
    // not returned on: the layout below still interns, so the rest of resolve sees a
    // well-formed class and piles no cascade on top of the one real error.
    // A ctor and dtor are hooks for the same scope boundary; one without the other is a
    // contract error — and it gates the obligation check below, so a lone `_();` reports
    // the missing dtor once rather than that plus "must be defined".
    if (has_ctor != has_dtor) {
        diagnostic::report(diag, {node.file_id, node.name_tok,
            has_ctor ? "A constructor requires a matching destructor."
                     : "A destructor requires a matching constructor.", {}});
    } else {
        // A forward-declared hook must be defined in SOME opening of the class — but only
        // for a class this TU OWNS. A hook declared in an imported HEADER may be defined in
        // any other `.sl`, which this TU cannot see, so "is it defined?" is a LINK-time
        // question there (an undefined `@C__$ctor__impl`), not a compile-time one.
        if (!header_class) {
            if (ctor_declared && !ctor_defined)
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "A forward-declared constructor must be defined.", {}});
            if (dtor_declared && !dtor_defined)
                diagnostic::report(diag, {node.file_id, node.name_tok,
                    "A forward-declared destructor must be defined.", {}});
        }
    }
    info.needs_ctor = has_ctor;
    info.needs_dtor = has_dtor;
    widen::internSlid(node.name, info.field_types, def_id);  // attach slots to `type`
    widen::setSlidLifecycle(type, has_ctor, has_dtor);
    // WHO DEFINES THE HOOK BODY, as opposed to whether the class has one. A hook DEF node
    // exists in this tree only if some `.sl` compiled into THIS TU wrote it: a header may
    // not hold a body at all (grammar rejects one), so a `_$ctor` kFunctionDef is proof
    // the body is ours. `ctor_defined` is that answer over EVERY opening, which is the
    // only place it can be taken — the declaration and the definition may sit in
    // different ones. Codegen turns it into `call an impl I define` vs `declare the
    // impl somebody else defines`.
    widen::setSlidHookHere(type, ctor_defined, dtor_defined);
}

// True if `cls` (or an ancestor) is a WELL-FORMED virtual class — one that carries a
// vtable pointer. Only a ROOT virtual class holds `_$vptr`; derived classes inherit it,
// so walking to the root and checking hasVptr answers the whole chain.
bool classIsVirtual(parse::Tree const& tree, widen::TypeRef cls) {
    int guard = static_cast<int>(tree.classes.size()) + 2;
    for (widen::TypeRef c = widen::strip(cls); guard-- > 0; ) {
        auto it = tree.classes.find(c);
        if (it == tree.classes.end()) return false;
        if (parse::hasVptr(it->second)) return true;
        c = parse::baseTypeOf(it->second);
        if (c == widen::kNoType) return false;
    }
    return false;
}

// The method entry across `frames` matching (name, user-params), other than `exclude`;
// -1 if none. `frames` is most-derived first, so the first hit is the closest
// declaration. Per-frame lookup + signature equality are the shared parse:: primitives.
int findMethodInFrames(parse::Tree const& tree, std::vector<int> const& frames,
                       std::string const& name,
                       std::vector<widen::TypeRef> const& params, int exclude) {
    for (int fr : frames) {
        int id = parse::findMethodInFrame(tree, fr, name, params, exclude);
        if (id >= 0) return id;
    }
    return -1;
}

// Enforce the virtual-class rules on ONE class declaration node: the base of a virtual
// class must be virtual; an explicit destructor must be virtual; an override must agree
// with the inherited method on virtual-ness and return type; a re-open may not add a new
// virtual method.
void validateVirtualClass(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag) {
    if (node.resolved_entry_id < 0) return;
    widen::TypeRef cls = widen::strip(node.return_type);
    auto it = tree.classes.find(cls);
    if (it == tree.classes.end()) return;
    parse::ClassInfo const& info = it->second;
    widen::TypeRef base = parse::baseTypeOf(info);

    // Own declared virtual-ness (from this node's members — a re-open node carries only
    // its own segment's members).
    bool has_own_virtual = false;
    parse::Node* dtor = nullptr;
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind != parse::Kind::kFunctionDef && m->kind != parse::Kind::kFunctionDecl)
            continue;
        if (m->name == "_$dtor") { dtor = m.get(); if (m->is_virtual) has_own_virtual = true; }
        else if (m->name == "_$ctor") continue;
        else if (m->is_virtual) has_own_virtual = true;
    }
    bool base_virtual = (base != widen::kNoType) && classIsVirtual(tree, base);
    if (!has_own_virtual && !base_virtual) return;   // not a virtual class

    // A virtual class ALWAYS needs construction (to stamp its vtable pointer at offset 0)
    // and destruction (its vtable dtor slot + the field/base chain), even with no user
    // ctor/dtor and no hook fields — so the construct/destruct hooks are emitted for it.
    it->second.needs_ctor = true;
    it->second.needs_dtor = true;
    widen::setSlidNeeds(cls, true, true);

    // The base of a virtual class must itself be virtual. (Checked on the PRIMARY node —
    // a re-open carries the same base and would only double-report.)
    if (!node.is_reopen && has_own_virtual && base != widen::kNoType && !base_virtual) {
        diagnostic::report(diag, {node.file_id, node.name_tok,
            "The base class of a virtual class must itself be virtual.", {}});
    }
    // An explicitly declared destructor of a virtual class must be virtual.
    if (dtor && !dtor->is_virtual) {
        diagnostic::report(diag, {dtor->file_id, dtor->name_tok,
            "The destructor of a virtual class must be virtual.", {}});
    }

    std::vector<int> base_frames = (base != widen::kNoType)
        ? parse::classAndBaseFrames(tree, base) : std::vector<int>{};
    std::vector<int> self_and_base = parse::classAndBaseFrames(tree, cls);
    for (auto& m : node.children) {
        if (!m || m->resolved_entry_id < 0) continue;
        if (m->kind != parse::Kind::kFunctionDef && m->kind != parse::Kind::kFunctionDecl)
            continue;
        if (m->name == "_$ctor" || m->name == "_$dtor") continue;
        parse::Entry const& em = tree.entries[m->resolved_entry_id];
        int inh = findMethodInFrames(tree, base_frames, em.name, em.param_types, -1);
        if (inh >= 0) {
            parse::Entry const& ei = tree.entries[inh];
            if (ei.is_virtual && !m->is_virtual) {
                diagnostic::report(diag, {m->file_id, m->name_tok,
                    "'" + m->name + "' overrides a virtual method and must be declared "
                    "'virtual' (a non-virtual method cannot shadow a virtual one).", {}});
            } else if (!ei.is_virtual && m->is_virtual) {
                diagnostic::report(diag, {m->file_id, m->name_tok,
                    "'" + m->name + "' is 'virtual' but the inherited method it shadows "
                    "is not (a virtual method cannot shadow a non-virtual one).", {}});
            } else if (ei.is_virtual && m->is_virtual
                       && widen::deepStrip(em.slids_type) != widen::deepStrip(ei.slids_type)) {
                diagnostic::report(diag, {m->file_id, m->name_tok,
                    "The return type of override '" + m->name + "' must match the "
                    "inherited method.", {}});
            }
        } else if (m->is_virtual && node.is_reopen) {
            // A re-open may implement/override an existing slot (found above) but not
            // introduce a NEW virtual method — unless a matching slot was declared in the
            // class's PRIMARY body (same frame, a different entry).
            // The message must NOT call the class virtual: this ALSO fires when the class
            // is not virtual yet and the re-open is what would make it — the more damaging
            // case, since the first virtual method inserts `_$vptr` at slot 0 and shifts
            // every field an importer already folded from the class's declaration. Naming
            // the consequence is the point; "a re-opened virtual class" described a
            // situation the author may not even be in.
            if (findMethodInFrames(tree, self_and_base, em.name, em.param_types,
                                   m->resolved_entry_id) < 0) {
                diagnostic::report(diag, {m->file_id, m->name_tok,
                    "Class '" + node.name + "' may not add the new virtual method '"
                    + m->name + "' in a re-open; every virtual method must be in the "
                    "original declaration (the first one places a vtable pointer at "
                    "offset 0 and fixes the slot layout).", {}});
            }
        }
    }
}

// Reject by-value cycles and run the transitive ctor/dtor-needs fixpoint over a
// class set whose field types were already resolved by resolveScopeTypes (the TYPES
// phase). Used for a LOCAL set (a function-body class / local namespace), which the
// whole-program fixpoint in run() doesn't sweep.
void checkClassCyclesAndNeeds(parse::Tree& tree, std::vector<parse::Node*> const& classes,
                              diagnostic::Sink& diag) {
    for (parse::Node* c : classes) checkClassByValueAcyclic(tree, *c, diag);
    bool changed = true;
    while (changed) {
        changed = false;
        for (parse::Node* c : classes) {
            if (c->resolved_entry_id < 0) continue;
            parse::ClassInfo& info = tree.classes.at(widen::strip(c->return_type));
            for (widen::TypeRef ft : info.field_types) {
                if (!info.needs_ctor && fieldContributesNeed(tree, ft, true))  { info.needs_ctor = true; changed = true; }
                if (!info.needs_dtor && fieldContributesNeed(tree, ft, false)) { info.needs_dtor = true; changed = true; }
            }
        }
    }
    for (parse::Node* c : classes) {
        if (c->resolved_entry_id < 0) continue;
        parse::ClassInfo const& info = tree.classes.at(widen::strip(c->return_type));
        widen::setSlidNeeds(c->return_type, info.needs_ctor, info.needs_dtor);
        validateVirtualClass(tree, *c, diag);
    }
}

// The class types a field DEPENDS ON BY VALUE — a kSlid field, or an array / tuple
// of one (recursively). A pointer / iterator field does NOT (it breaks a size
// cycle). Mirrors fieldContributesNeed's shape.
void collectByValueClasses(widen::TypeRef ft, std::vector<widen::TypeRef>& out) {
    using F = widen::Type::Form;
    widen::TypeRef s = widen::strip(ft);
    switch (widen::form(s)) {
        case F::kSlid:  out.push_back(s); break;
        case F::kArray: collectByValueClasses(widen::get(s).elem, out); break;
        case F::kTuple:
            for (widen::TypeRef sl : widen::get(s).slots) collectByValueClasses(sl, out);
            break;
        // A pointer / iterator field breaks the size cycle; primitives carry none;
        // alias was stripped; none/void/anyptr can't be a by-value class. An
        // unresolved template-alias use never survives resolve to reach here.
        case F::kPointer: case F::kIterator: case F::kPrimitive:
        case F::kVoid: case F::kAnyptr: case F::kAlias: case F::kConst: case F::kNone:
        case F::kTmplUse:
            break;   // kConst can't reach here — strip() peeled it above
    }
}

// A class this TU only IMPORTS whose layout is hidden (declared incomplete in a header,
// completed by another module). Its size/field-offsets are unknown here, so it may only
// be a bare local (Slice 2 sizes it at runtime), a pointer, or a `new` — never embedded
// by value in an aggregate.
bool isImportOpaque(widen::TypeRef s) {
    return widen::form(s) == widen::Type::Form::kSlid && widen::slidOpaque(s)
        && widen::slidLinkage(s) == widen::Type::Linkage::kDeclare;
}

// A class DERIVING from an opaque class inherits its unknown layout. The base sub-object
// still occupies slot 0 — every upcast stays a no-op — but its SIZE is a runtime fact, so
// the derived class's OWN field offsets are not compile-time constants in an importer.
// They come from the completer's exported `__$offsets` table (codegen).
//
// runtime_layout IMPLIES opaque, and that is the whole trick: an opaque class's size is
// already a runtime call, its construction is already split into `__$ctor`/`__$pctor`
// across the seam, and its by-value embedding is already rejected in an importer. A
// derived class needs exactly those three things, so it joins that machinery rather than
// growing a parallel copy.
//
// The flag rides only PERSISTENT header facts (the base's `...`), so the completer and
// every importer compute it identically — the layout rule cannot disagree across the seam.
// Two forms are REJECTED rather than laid out:
//   - a `.sl`-LOCAL class over an opaque base: nothing outside the TU can name it, so no
//     sibling exports its offsets, and it cannot fold them itself.
//   - a VIRTUAL class over an opaque base: the vptr wants offset 0 and the base holds it.
void propagateRuntimeLayout(parse::Tree& tree, std::vector<parse::Node*> const& classes,
                            diagnostic::Sink& diag) {
    // Fixpoint, so a chain `Opaque <- A <- B` marks B as well as A regardless of the
    // order the classes were registered in.
    bool changed = true;
    while (changed) {
        changed = false;
        for (parse::Node* c : classes) {
            if (!c || c->resolved_entry_id < 0) continue;
            widen::TypeRef self = widen::strip(c->return_type);
            if (widen::form(self) != widen::Type::Form::kSlid) continue;
            if (widen::slidRuntimeLayout(self)) continue;
            auto it = tree.classes.find(self);
            if (it == tree.classes.end()) continue;
            widen::TypeRef base = parse::baseTypeOf(it->second);
            if (base == widen::kNoType || !widen::slidOpaque(widen::strip(base))) continue;
            widen::setSlidRuntimeLayout(self, true);
            widen::setSlidOpaque(self, true);
            changed = true;
        }
    }
    for (parse::Node* c : classes) {
        if (!c || c->resolved_entry_id < 0) continue;
        widen::TypeRef self = widen::strip(c->return_type);
        if (widen::form(self) != widen::Type::Form::kSlid) continue;
        if (!widen::slidRuntimeLayout(self)) continue;
        auto it = tree.classes.find(self);
        if (it == tree.classes.end()) continue;
        auto bit = tree.classes.find(widen::strip(parse::baseTypeOf(it->second)));
        std::string bname = bit != tree.classes.end() ? bit->second.name : "?";
        if (widen::slidLinkage(self) == widen::Type::Linkage::kInternal) {
            diagnostic::report(diag, {c->file_id, c->name_tok,
                "Class '" + c->name + "' derives from imported incomplete class '" + bname
                + "', so its field offsets are only known to the module that completes '"
                + bname + "'; declare '" + c->name + "' in a header so that module exports "
                "them.", {}});
            continue;
        }
        for (auto& m : c->children) {
            if (m && m->is_virtual) {
                diagnostic::report(diag, {m->file_id, m->name_tok,
                    "Class '" + c->name + "' cannot be virtual: it derives from imported "
                    "incomplete class '" + bname + "', whose sub-object occupies the slot "
                    "the vtable pointer needs.", {}});
                break;
            }
        }
    }
}

// A class whose by-value field graph cycles back to itself has INFINITE size —
// reject it (classify's recursive construction and codegen's struct lowering both
// recurse forever otherwise: a SIGSEGV, not a diagnostic). A `^` / `[]` field
// breaks the cycle. Now that the two-phase makes a class's own name known while
// its fields resolve, this is reachable (`Foo(Foo f_)`, mutual `A(B)`/`B(A)`).
// The SAME by-value walk also rejects embedding an imported OPAQUE class (its layout
// is unknown here, so the enclosing class's offsets are uncomputable — the silent
// mis-size an importer would otherwise emit).
void checkClassByValueAcyclic(parse::Tree& tree, parse::Node& node,
                              diagnostic::Sink& diag) {
    if (node.resolved_entry_id < 0) return;
    widen::TypeRef self = widen::strip(node.return_type);
    auto it = tree.classes.find(self);
    if (it == tree.classes.end()) return;
    std::set<widen::TypeRef> seen;
    std::vector<widen::TypeRef> stack;
    // The unnamed `_$base` slot of a RUNTIME-LAYOUT class is the one by-value embedding of
    // an opaque class the model allows (propagateRuntimeLayout marked it; the offsets past
    // it come from the completer's table). Skip that slot's own seeding so the rejection
    // below doesn't fire on it, and descend into the base's fields by hand so the CYCLE
    // check still covers everything reachable through the base. A named field of the same
    // opaque type is untouched by this and stays rejected.
    std::size_t first = 0;
    if (widen::slidRuntimeLayout(self)) {
        widen::TypeRef base = widen::strip(parse::baseTypeOf(it->second));
        if (base == self) {
            diagnostic::report(diag, {node.file_id, node.name_tok,
                "Class '" + node.name + "' contains itself by value (infinite "
                "size); use a reference '^' field.", {}});
            return;
        }
        first = 1;
        seen.insert(base);
        auto bit = tree.classes.find(base);
        if (bit != tree.classes.end())
            for (widen::TypeRef ft : bit->second.field_types)
                collectByValueClasses(ft, stack);
    }
    for (std::size_t i = first; i < it->second.field_types.size(); i++)
        collectByValueClasses(it->second.field_types[i], stack);
    while (!stack.empty()) {
        widen::TypeRef cur = stack.back();
        stack.pop_back();
        if (cur == self) {
            diagnostic::report(diag, {node.file_id, node.name_tok,
                "Class '" + node.name + "' contains itself by value (infinite "
                "size); use a reference '^' field.", {}});
            return;
        }
        if (isImportOpaque(cur)) {
            auto cit = tree.classes.find(cur);
            std::string oname = cit != tree.classes.end() ? cit->second.name : "?";
            diagnostic::report(diag, {node.file_id, node.name_tok,
                "Class '" + node.name + "' embeds imported incomplete class '" + oname
                + "' by value; its layout is private to the module that completes it — "
                "use a reference '" + oname + "^'.", {}});
            return;
        }
        if (!seen.insert(cur).second) continue;
        auto cit = tree.classes.find(cur);
        if (cit == tree.classes.end()) continue;
        for (widen::TypeRef ft : cit->second.field_types)
            collectByValueClasses(ft, stack);
    }
}

// The imported OPAQUE class that materializing `t` BY VALUE as an AGGREGATE MEMBER would
// need the (hidden) layout of, or kNoType. A bare opaque local/global is fine — only an
// array element, tuple slot, or by-value class field of one is illegal in an importer.
// So the top-level type is NOT itself flagged; its aggregate members are. A pointer /
// iterator breaks the walk. `seen` bounds recursion through class fields (cycles are
// rejected elsewhere, but a self-referential graph must not loop here).
widen::TypeRef aggregateEmbedsOpaque(widen::TypeRef t, std::set<widen::TypeRef>& seen) {
    widen::TypeRef s = widen::strip(t);
    using F = widen::Type::Form;
    F form = widen::form(s);
    if (form == F::kArray) {
        widen::TypeRef e = widen::strip(widen::get(s).elem);
        if (isImportOpaque(e)) return e;
        return aggregateEmbedsOpaque(e, seen);
    }
    // A tuple, or a NON-opaque class (whose real fields are visible here): scan slots. An
    // opaque class's own placeholder slots are not its fields, so don't descend into them.
    if (form == F::kTuple || (form == F::kSlid && !isImportOpaque(s))) {
        if (form == F::kSlid && !seen.insert(s).second) return widen::kNoType;
        for (widen::TypeRef sl : widen::get(s).slots) {
            widen::TypeRef ss = widen::strip(sl);
            if (isImportOpaque(ss)) return ss;
            widen::TypeRef r = aggregateEmbedsOpaque(ss, seen);
            if (r != widen::kNoType) return r;
        }
    }
    return widen::kNoType;
}

// Register the classes defined DIRECTLY in one scope (a statement list), TWO-PHASE
// like the file-scope passes, so a local class field may forward-reference a
// sibling. Only processes not-yet-registered classes (idempotent — a function
// body's top-level classes are registered ahead of the nested-fn pre-pass, and
// resolveStmtList(fn.children) then skips them). Members + ctor/dtor bodies and
// the TRANSITIVE-needs fixpoint run after both phases, all scoped to these
// siblings (their field classes are already published, or in this very set).
void registerLocalClasses(parse::Tree& tree,
                          std::vector<std::unique_ptr<parse::Node>>& stmts,
                          diagnostic::Sink& diag) {
    std::vector<parse::Node*> fresh;
    for (auto& s : stmts) {
        if (!s || s->kind != parse::Kind::kClassDef || s->resolved_entry_id >= 0)
            continue;
        // A block-scope CLASS TEMPLATE: register the pattern and snapshot right
        // here — the current state IS its definition-point visibility (the same
        // move a block-scope function template makes). A RE-OPEN opening also
        // lands here (registerClassTemplate records it); only the PRIMARY
        // snapshots — the template has ONE definition-point state.
        if (!s->type_params.empty()) {
            registerClassTemplate(tree, *s, &stmts, /*owner=*/-1, diag);
            if (s->resolved_entry_id >= 0) {
                auto it = tree.templates.find(s->resolved_entry_id);
                if (it != tree.templates.end() && it->second.def == s.get())
                    snapshotTemplate(tree, *s);
            }
            continue;
        }
        fresh.push_back(s.get());
    }
    if (fresh.empty()) return;
    // The four phases over this local set, scoped to a function body's lifetime:
    // NAME (each local class name + its members, recursing + collecting nested) ->
    // TYPES (resolveScopeTypes resolves aliases + FIELD types + signatures, recursing)
    // -> cycle + needs-fixpoint over the collected set (the whole-program fixpoint in
    // run() doesn't sweep a local set) -> BODY. Names register before any field type,
    // so a local sibling field forward-references a later sibling.
    std::vector<parse::Node*> classes;
    for (parse::Node* c : fresh) {
        registerClassName(tree, *c, diag);   // local scope: def_id via currentFrameId
        classes.push_back(c);
        if (c->resolved_entry_id >= 0)
            registerScopeNames(tree, *c,
                tree.entries[c->resolved_entry_id].ns_frame_id, classes, diag);
    }
    // A qualified external const / alias / enum declared beside a local class was
    // relocated into that class's children by relocateOutOfLineMembers (run over the
    // stmt list before this), so registerScopeNames above already registered it as an
    // ordinary member — no separate qualified-member pass is needed here.
    for (parse::Node* c : fresh) resolveScopeTypes(tree, *c, /*isClass=*/true, diag);
    checkClassCyclesAndNeeds(tree, classes, diag);
    for (parse::Node* c : fresh) resolveScopeBodies(tree, *c, /*isClass=*/true, diag);
}

// Does a field type carry a ctor (or dtor) need into its container — a hook class,
// OR an array / tuple of one (recursive)? Reads the ClassInfo needs being computed
// by the transitive fixpoint (NOT the widen flags, which are published after).
bool fieldContributesNeed(parse::Tree const& tree, widen::TypeRef ft, bool ctor) {
    using F = widen::Type::Form;
    widen::TypeRef s = widen::strip(ft);
    F f = widen::form(s);
    if (f == F::kSlid) {
        auto it = tree.classes.find(s);
        if (it == tree.classes.end()) return false;
        return ctor ? it->second.needs_ctor : it->second.needs_dtor;
    }
    if (f == F::kArray) return fieldContributesNeed(tree, widen::get(s).elem, ctor);
    if (f == F::kTuple) {
        for (widen::TypeRef slot : widen::get(s).slots)
            if (fieldContributesNeed(tree, slot, ctor)) return true;
    }
    return false;
}

// mungeParamTypes — the param-type rewrite hook (see plan-declarator.txt PASSING).
// The spec: primitives pass by value; everything else by pointer. So a parameter's
// resolved type must be primitive, a pointer (reference `^` / iterator `[]`), or an
// array (the `int a[3]` shorthand, rewritten here to a pointer to the array — the
// body indexes WITHOUT an explicit `^`); a TUPLE or CLASS value parameter is a
// COMPILE ERROR. Runs after param types are resolved (alias-substituted).
//
// THE DEFAULT CONTRACT (const munge): calling a function is not permission to
// modify the caller's data. So a reference / iterator parameter's POINTEE
// becomes const — `T^` -> `(const T)^`, `T[]` -> `(const T)[]` — and an array
// (passed by pointer) becomes a pointer to a const array — `T[N]` -> `const T[N]`
// then `^`. The `mutable` qualifier opts a pointer/iterator/array OUT (the
// function may write through it). A primitive passes by value (its own copy —
// no const). The synthesized method receiver `_$recv` is skipped: const methods
// (a const receiver) are a Phase-6 concern, so the receiver stays plain `Class^`.
void mungeParamType(parse::Tree& /*tree*/, parse::Node& p, diagnostic::Sink& diag) {
    if (p.return_type == widen::kNoType) return;   // typeless: inferred from a default
    using F = widen::Type::Form;
    widen::TypeRef st = widen::strip(p.return_type);   // see through alias for the form
    F f = widen::form(st);
    bool already_const = (widen::form(p.return_type) == F::kConst);
    bool is_recv = (p.name == "_$recv");   // synthesized method receiver — const-method = Phase 6

    // `mutable` is valid only on a pointer (reference / iterator) or array parameter.
    if (p.is_mutable && f != F::kPointer && f != F::kIterator && f != F::kArray) {
        diagnostic::report(diag, {p.file_id, p.tok,
            "The 'mutable' qualifier applies only to a pointer "
            "(reference / iterator) or array parameter.", {}});
        return;
    }

    if (f == F::kArray) {
        // Array-by-pointer arm: rewrite `int[3]` to a pointer to the array. The
        // array VALUE is const unless `mutable` (or already written const), so
        // the default form is `(const int[3])^`.
        widen::TypeRef arr = p.return_type;
        if (!p.is_mutable && !already_const) arr = widen::internConst(arr);
        p.return_type = widen::internPointer(arr);
        return;
    }
    if (f == F::kTuple || f == F::kSlid) {
        diagnostic::report(diag, {p.file_id, p.tok,   // caret the TYPE, not the name
            "A non-primitive parameter must be a pointer (reference / iterator) or "
            "an array; got '" + widen::spellOrEmpty(p.return_type) + "'.", {}});
        return;
    }
    if ((f == F::kPointer || f == F::kIterator)
        && !p.is_mutable && !already_const && !is_recv) {
        // Const the POINTEE (the caller's data), leaving the pointer itself
        // writable: `T^` -> `(const T)^`, `T[]` -> `(const T)[]`.
        widen::TypeRef cpointee = widen::internConst(widen::get(st).pointee);
        p.return_type = (f == F::kPointer) ? widen::internPointer(cpointee)
                                           : widen::internIterator(cpointee);
    }
}

// Walk every function (file-scope / nested / method / namespace member) and munge
// its parameter types. Recurses through children (nested fns, class methods, ns
// members all live there). After rewriting the param's parse-node type, re-syncs
// the function entry's param_types AND the body-frame param entry's slids_type,
// which were both captured pre-rewrite during body resolution.
void mungeParamTypes(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag) {
    // A TEMPLATE definition stays pristine (params AND body) — its instances are
    // munged individually after their types resolve (instantiateTemplate).
    if (!node.type_params.empty()) return;
    if (node.kind == parse::Kind::kFunctionDef
        || node.kind == parse::Kind::kFunctionDecl) {
        for (auto& p : node.params) {
            if (p) mungeParamType(tree, *p, diag);
        }
        // Re-sync the function entry's param_types so call-site stamping at
        // classify sees the rewritten (pointer) types.
        if (node.resolved_entry_id >= 0
            && node.resolved_entry_id < static_cast<int>(tree.entries.size())) {
            parse::Entry& e = tree.entries[node.resolved_entry_id];
            for (std::size_t i = 0;
                 i < node.params.size() && i < e.param_types.size(); i++) {
                if (node.params[i]) e.param_types[i] = node.params[i]->return_type;
            }
        }
        // Re-sync each body-frame param entry's slids_type so SymTab seeding at
        // codegen and identifier resolution at classify see the rewrite.
        for (auto& p : node.params) {
            if (p && p->resolved_entry_id >= 0
                && p->resolved_entry_id < static_cast<int>(tree.entries.size())) {
                tree.entries[p->resolved_entry_id].slids_type = p->return_type;
            }
        }
    }
    for (auto& c : node.children) {
        if (c) mungeParamTypes(tree, *c, diag);
    }
}

}  // namespace

// Find a class def node named `name` directly among `nodes` (the first opening).
// Collect every class OR namespace def node named `name` among `scope` (ALL
// openings of it) — a qualifier segment may be either a class or a namespace.
void collectScopeOpenings(std::vector<std::unique_ptr<parse::Node>>& scope,
                          std::string const& name, std::vector<parse::Node*>& out) {
    for (auto& n : scope)
        if (n && (n->kind == parse::Kind::kClassDef
                  || n->kind == parse::Kind::kNamespaceDecl) && n->name == name)
            out.push_back(n.get());
}

// Join a qualified def's path (`qualifier[0]:…:name`) for diagnostics.
std::string qualifiedPath(parse::Node const& n) {
    std::string p;
    for (auto const& seg : n.qualifier) p += seg + ":";
    return p + n.name;
}

// Register a qualified LEAF member (const / alias / enum) into an already-resolved target
// FRAME — the remote-namespace case of the external re-open form (a namespace opens in
// any scope, so a nested scope may add a member to it). The caller has verified the frame
// and consumes (nulls) the node afterward, so no later pass re-processes it.
void registerQualifiedLeaf(parse::Tree& tree, parse::Node& s, int frame,
                           diagnostic::Sink& diag) {
    if (s.kind == parse::Kind::kEnumDecl) {
        registerEnum(tree, s, frame, diag);
        resolveEnumMemberInits(tree, s, diag);
        return;
    }
    if (int prev = findMemberDeclared(tree, frame, s.name); prev >= 0) {
        parse::Entry const& pe = tree.entries[prev];
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Duplicate declaration of '" + s.name + "'.",
            {{pe.file_id, pe.tok, "first declared here"}}});
        return;
    }
    parse::Entry e;
    e.name = s.name;
    e.slids_type = s.return_type;
    e.file_id = s.file_id;
    e.tok = s.name_tok;
    e.owner_ns_frame = frame;
    if (s.kind == parse::Kind::kAliasDecl) {
        e.kind = parse::EntryKind::kAlias;
        s.resolved_entry_id = parse::addEntry(tree, std::move(e));
        for (auto& d : s.dim_exprs) if (d) resolveExpr(tree, *d, diag);
        resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
        return;
    }
    // const
    e.kind = parse::EntryKind::kConst;
    resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
    if (constNeedsStorage(s.return_type)) {
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "A const variable of a non-scalar type (array, tuple, class, or "
            "pointer) requires global storage, which is not yet supported.", {}});
    }
    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
    for (auto& init : s.children) if (init) resolveExpr(tree, *init, diag);
}

// OUT-OF-LINE MEMBER RELOCATION. A qualified definition — the external
// re-open form — desugars to a member of the scope named by its qualifier path:
//   `Ret Class:method(...)`  -> a method of Class      (`node->qualifier = [Class]`)
//   `Class:Namespace { }`    -> a namespace of Class   (`qualifier = [Class]`, ns node)
//   `Class:Reopen() { }`     -> a hoisted class of Class (`qualifier = [Class]`, cls node)
//   `const int Class:k = 7;` -> a const of Class       (`qualifier = [Class]`, var node)
//   `alias Class:A = int;`   -> an alias of Class       (`qualifier = [Class]`, alias node)
//   `enum int Class:E ( … );`-> an enum of Class        (`qualifier = [Class]`, enum node)
// Move the node into the target scope's children, so ALL the in-scope machinery
// (registration, signatures, self-binding body resolution, the `<Class>__method`
// lift, namespace/class re-open merge) handles it with no special-casing. A method
// whose immediate scope is a CLASS gets the implicit `_$recv` receiver spliced in;
// a namespace/class node — or a free function whose scope is a namespace — gets none;
// a const/alias/enum leaf gets none. Runs per-scope (over `children`), BEFORE that
// scope registers its members, so the target opening is a SIBLING in the same
// `children` (same-scope re-open). A multi-segment path (`A:B:m`, `Class1:Ns1:Class2:m`)
// walks scope-in-scope through classes AND namespaces, searching ALL openings at each
// level — a nested scope may be introduced in a re-open, so every opening of the
// enclosing scope is a candidate parent.
//
// A CLASS re-open is SAME-SCOPE only: the target class must be a sibling opening in THIS
// `children` — a class merely VISIBLE from an enclosing scope is NOT searched, so
// re-opening it from a non-declaring scope (refine) is rejected per-segment. A NAMESPACE,
// by contrast, may be opened in ANY scope: a qualified LEAF (const/alias/enum) whose first
// segment names an enclosing-scope namespace is registered into that namespace's frame in
// place (registerQualifiedLeaf) rather than physically moved.
void relocateOutOfLineMembers(parse::Tree& tree,
                              std::vector<std::unique_ptr<parse::Node>>& children,
                              diagnostic::Sink& diag) {
    bool moved = false;
    for (auto& ch : children) {
        if (!ch) continue;
        if (ch->qualifier.empty()) continue;   // not the external out-of-line form
        bool is_fn = (ch->kind == parse::Kind::kFunctionDef
                      || ch->kind == parse::Kind::kFunctionDecl);
        bool is_scope = (ch->kind == parse::Kind::kNamespaceDecl
                         || ch->kind == parse::Kind::kClassDef);
        bool is_enum = (ch->kind == parse::Kind::kEnumDecl);
        bool is_const = (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const);
        // A qualified alias with a TARGET is an external member; a bare qualified alias
        // (`alias Ns:Sub;`, kNoType) is a namespace IMPORT resolved at its use scope —
        // leave it in place.
        bool is_alias = (ch->kind == parse::Kind::kAliasDecl
                         && ch->return_type != widen::kNoType);
        // A qualified MUTABLE var (`int C:x = 5;`) is not a re-openable member — only
        // constants / aliases / enums (and whole method / namespace / class defs) are.
        // Report and drop it so no later pass mis-registers a stray qualified var.
        if (ch->kind == parse::Kind::kVarDeclStmt && !ch->is_const) {
            diagnostic::report(diag, {ch->file_id, ch->name_tok,
                "Only constants, aliases, and enums may be defined by qualified name.",
                {}});
            ch.reset();
            moved = true;
            continue;
        }
        if (!is_fn && !is_scope && !is_enum && !is_const && !is_alias) continue;
        // Walk the qualifier path to the target scope. At each level the candidates
        // are the class/namespace nodes named seg among the children of the previous
        // level's openings; the target is the first opening of the final segment
        // (appending to any opening is equivalent — they share one frame).
        std::vector<parse::Node*> level;
        collectScopeOpenings(children, ch->qualifier[0], level);
        std::size_t fail_i = 0;                 // segment index where the walk emptied
        for (std::size_t i = 1; i < ch->qualifier.size() && !level.empty(); i++) {
            std::vector<parse::Node*> next;
            for (parse::Node* p : level)
                collectScopeOpenings(p->children, ch->qualifier[i], next);
            level = std::move(next);
            fail_i = i;
        }
        if (level.empty()) {
            // A LEAF whose FIRST segment is not a local sibling may still target a
            // NAMESPACE opened in an enclosing scope (namespaces open in any scope):
            // register it into that namespace's frame in place, then drop the node. A
            // CLASS first segment is NOT accepted here — a class re-open is same-scope
            // only, so a non-local class (refine) falls through to the per-segment error.
            if (fail_i == 0 && (is_const || is_alias || is_enum)) {
                // Idempotent: relocation runs more than once over a function body
                // (resolveFunctionBody + resolveStmtList), so register the leaf once.
                if (ch->resolved_entry_id >= 0) continue;
                int seg0 = resolveName(tree, ch->qualifier[0]);
                if (seg0 >= 0
                    && tree.entries[seg0].kind == parse::EntryKind::kNamespace) {
                    int frame = resolveNamespaceSegments(tree, ch->qualifier,
                        ch->qualifier_toks, ch->global_qualified, ch->file_id, diag);
                    if (frame >= 0) registerQualifiedLeaf(tree, *ch, frame, diag);
                    // Leave the node in place (qualifier intact) so constfold folds its
                    // init; the intermediate resolve passes skip a qualified leaf.
                    continue;
                }
            }
            // Caret the SPECIFIC segment that could not be resolved (fail_i), named
            // against its parent scope — not the whole prefix as an undifferentiated
            // unit (which mis-carets and over-blames when an earlier segment is the
            // one missing).
            std::string const& seg = ch->qualifier[fail_i];
            std::string msg;
            if (fail_i == 0) {
                msg = "'" + seg + "' is not a class or namespace in scope.";
            } else {
                std::string parent = ch->qualifier[0];
                for (std::size_t i = 1; i < fail_i; i++)
                    parent += ":" + ch->qualifier[i];
                msg = "'" + parent + "' has no class or namespace member '"
                    + seg + "'.";
            }
            diagnostic::report(diag,
                {ch->file_id, ch->qualifier_toks[fail_i], msg, {}});
            continue;
        }
        // AMBIGUITY: `A:B() {}` (a qualified class re-open, empty parens) is
        // token-identical to an empty-field DERIVED class `A : B() {}`. Disambiguate
        // semantically: if `A` is a CLASS that does NOT already contain a class/namespace
        // `B`, read it as INHERITANCE (B derives from A) rather than creating a nested
        // A::B. (B already an opening of A -> re-open, relocated below; A a namespace ->
        // create nested, below.) Only the single-segment `A:B` form — a multi-segment
        // path is a genuine nested-scope target.
        if (ch->kind == parse::Kind::kClassDef && ch->qualifier.size() == 1
            && level.front()->kind == parse::Kind::kClassDef) {
            std::vector<parse::Node*> b_openings;
            for (parse::Node* a_open : level)
                collectScopeOpenings(a_open->children, ch->name, b_openings);
            if (b_openings.empty()) {
                // Reinterpret as a derived class of the CURRENT scope: mirror grammar's
                // inheritance setup (base name on ->text + the unnamed `_$base` slot-0
                // field of type A) and leave the node in place with the qualifier cleared,
                // so ordinary registration handles it as `A : B()`.
                auto bp = std::make_unique<parse::Node>();
                bp->kind = parse::Kind::kParam;
                bp->name = "_$base";
                bp->file_id = ch->file_id;
                bp->tok = ch->name_tok;
                bp->name_tok = ch->name_tok;
                bp->return_type = widen::internOrNone(ch->qualifier[0]);
                ch->text = ch->qualifier[0];
                ch->params.insert(ch->params.begin(), std::move(bp));
                ch->qualifier.clear();
                ch->qualifier_toks.clear();
                continue;
            }
        }
        parse::Node* target = level.front();
        // An external member of a CLASS TEMPLATE (`int Vec:late() { }`,
        // `const int Vec:kk = 40;`) relocates into the PATTERN's children —
        // before registration reads them — and so rides every instance clone.
        // (A member TEMPLATE targeting a class still rejects, below.)
        // An out-of-line TEMPLATE definition relocates like any external member
        // — registration's member-template divert takes it from there. The
        // NAMESPACE flavor (`T Space:f<T>(v) { }`) is an external namespace
        // function that happens to be a template; the CLASS flavor
        // (`T Gauge:scaled<T>(T v) { }`) is the sibling-side BODY of a header
        // class's member-template declaration — the decl/def merge in
        // registerTemplateFunction pairs them.
        // A ctor/dtor is CLASS-only. The bare form is rejected in the parser ("A
        // constructor or destructor may only appear in a class body"); the QUALIFIED
        // spelling must not be a way around that restriction — a namespace has no
        // lifecycle. Without this the hook relocates in as a receiver-less free
        // function and codegen emits a stray top-level `@_$ctor`.
        if (is_fn && target->kind != parse::Kind::kClassDef
            && (ch->name == "_$ctor" || ch->name == "_$dtor")) {
            diagnostic::report(diag, {ch->file_id, ch->name_tok,
                "A constructor or destructor may only appear in a class body.", {}});
            continue;
        }
        // A method (function whose immediate scope is a class) needs the implicit
        // receiver `_$recv` of type `Class^`. A namespace/class node, or a free
        // function in a namespace, has no receiver.
        if (is_fn && target->kind == parse::Kind::kClassDef) {
            ch->params.insert(ch->params.begin(),
                parse::makeReceiverParam(widen::internOrNone(target->name + "^"),
                                         ch->file_id, ch->name_tok));
        }
        ch->qualifier.clear();
        ch->qualifier_toks.clear();
        target->children.push_back(std::move(ch));   // leaves a null slot behind
        moved = true;
    }
    if (moved)
        children.erase(std::remove(children.begin(), children.end(), nullptr),
                       children.end());
}

// Does this subtree open the global scope itself (a `global;` at any depth, NOT
// crossing into a nested function)? If not, desugar auto-inserts one over all of main.
bool subtreeOpensGlobalScope(parse::Node const& n) {
    if (n.kind == parse::Kind::kGlobalScopeStmt) return true;
    if (n.kind == parse::Kind::kFunctionDef
        || n.kind == parse::Kind::kFunctionDecl) return false;   // its own scope
    for (auto const& c : n.children)
        if (c && subtreeOpensGlobalScope(*c)) return true;
    return false;
}

// The `global;` REGION check over main's body. `active` is inherited into children and
// advanced across SIBLINGS — a `global;` opens the rest of its block AND everything
// nested in that rest — but is passed BY VALUE, so it does not leak out of a block.
//   - a global USE (an ident / assign target resolving to a kGlobalVar) while inactive
//     is "outside the global scope"
//   - a second `global;` while already active is a double instantiation
// A nested function is a separate scope (skipped): its global uses run during the
// scope dynamically, like a lazy group's ctor.
void checkGlobalRegion(parse::Tree& tree, parse::Node& n, bool active,
                       diagnostic::Sink& diag) {
    if (n.resolved_entry_id >= 0
        && tree.entries[n.resolved_entry_id].kind == parse::EntryKind::kGlobalVar
        && !active) {
        diagnostic::report(diag, {n.file_id, n.tok,
            "A global variable is accessed outside the 'global;' scope.", {}});
    }
    bool local = active;
    for (auto& c : n.children) {
        if (!c) continue;
        if (c->kind == parse::Kind::kFunctionDef
            || c->kind == parse::Kind::kFunctionDecl) continue;
        if (c->kind == parse::Kind::kGlobalScopeStmt) {
            if (local)
                diagnostic::report(diag, {c->file_id, c->tok,
                    "A second 'global;' — the global scope is already open.", {}});
            local = true;
            continue;
        }
        checkGlobalRegion(tree, *c, local, diag);
    }
}

// Explode anonymous global groups (`global (a=…, b=…) {…}`, an EMPTY-name is_global
// namespace) BEFORE registration: splice each group's members into its enclosing scope,
// so they register through the ordinary bare-global path — reached bare / via `::` at
// file scope, `Enclosing:member` in a namespace. A named group keeps its namespace
// (`name:member`); only the name-less form is dissolved.
//
// A STATIC anon group (empty body) just splices its members. A LAZY anon group (with a
// ctor/dtor) has no namespace to anchor its shared lifetime, so it is DISSOLVED: its
// members splice into the enclosing scope as bare siblings, and its ctor/dtor move into
// a GENERATED plain namespace of the enclosing scope (`$glazy<id>`) — a receiver-less
// home whose function bodies resolve the members bare UP the frame chain (works at
// file / namespace / class scope alike). Members and ctor/dtor share a fresh group id;
// collectGlobals rebuilds the shared lazy GlobalGroup from it.
void explodeAnonGlobalGroups(parse::Node& scope, int& gid_counter) {
    for (auto& c : scope.children)
        if (c) explodeAnonGlobalGroups(*c, gid_counter);
    bool any = false;
    for (auto& c : scope.children)
        if (c && c->kind == parse::Kind::kNamespaceDecl && c->is_global
            && c->name.empty()) { any = true; break; }
    if (!any) return;
    std::vector<std::unique_ptr<parse::Node>> flat;
    for (auto& c : scope.children) {
        if (!(c && c->kind == parse::Kind::kNamespaceDecl && c->is_global
              && c->name.empty())) {
            flat.push_back(std::move(c));
            continue;
        }
        bool lazy = false;
        for (auto& m : c->children)
            if (m && m->kind == parse::Kind::kFunctionDef
                && (m->name == "_$gctor" || m->name == "_$gdtor")) { lazy = true; break; }
        if (!lazy) {
            // A hook-less anon group is still ONE group: its members share a gate so
            // touching any compound member constructs them all. Tag members with a fresh
            // gid (no ctor/dtor bodies, so no `$glazy` home is generated); desugar
            // decides which are gated vs static, and drops a purely-scalar group.
            int gid = gid_counter++;
            for (auto& m : c->children) {
                if (!m) continue;
                if (m->kind == parse::Kind::kVarDeclStmt) m->global_group_id = gid;
                flat.push_back(std::move(m));   // dissolve into enclosing scope
            }
            continue;
        }
        int gid = gid_counter++;
        auto genns = std::make_unique<parse::Node>();
        genns->kind = parse::Kind::kNamespaceDecl;
        genns->name = "$glazy" + std::to_string(gid);
        genns->file_id = c->file_id;
        genns->tok = c->tok;
        genns->name_tok = c->name_tok;
        for (auto& m : c->children) {
            if (!m) continue;
            if (m->kind == parse::Kind::kFunctionDef) {
                m->name = (m->name == "_$gctor" ? "_$glazyctor_" : "_$glazydtor_")
                          + std::to_string(gid);
                m->name_tok = c->name_tok;
                m->global_group_id = gid;
                genns->children.push_back(std::move(m));
            } else if (m->kind == parse::Kind::kVarDeclStmt) {
                m->global_group_id = gid;
                flat.push_back(std::move(m));   // dissolve into enclosing scope
            }
        }
        flat.push_back(std::move(genns));
    }
    scope.children = std::move(flat);
}

// `recv^.field` — the field of a receiver/argument pointer, as a source-level lvalue
// (a kFieldExpr over a kDerefExpr over the param ident). resolve/classify resolve the
// names and types like any hand-written access.
std::unique_ptr<parse::Node> makeRecvFieldAccess(std::string const& recv,
                                                 std::string const& field,
                                                 int file, int tok) {
    auto id = std::make_unique<parse::Node>();
    id->kind = parse::Kind::kIdentExpr;
    id->name = recv;
    id->file_id = file; id->tok = tok; id->name_tok = tok;
    auto deref = std::make_unique<parse::Node>();
    deref->kind = parse::Kind::kDerefExpr;
    deref->file_id = file; deref->tok = tok;
    deref->children.push_back(std::move(id));
    auto fld = std::make_unique<parse::Node>();
    fld->kind = parse::Kind::kFieldExpr;
    fld->name = field;
    fld->file_id = file; fld->tok = tok; fld->name_tok = tok;
    fld->children.push_back(std::move(deref));
    return fld;
}

// True iff the class frame already DECLARES a user same-type operator `opname`
// (signature `(Self^)`), which shadows the synthesized default.
bool classHasUserSelfOp(parse::Tree const& tree, int frame,
                        std::string const& opname, widen::TypeRef cstrip) {
    for (parse::Entry const& e : tree.entries) {
        if (e.kind != parse::EntryKind::kFunction || e.owner_ns_frame != frame
            || e.name != opname || e.param_types.size() != 2)
            continue;
        widen::TypeRef p = widen::strip(e.param_types[1]);
        if (widen::form(p) != widen::Type::Form::kPointer) continue;
        if (widen::deepStrip(widen::get(p).pointee) == widen::deepStrip(cstrip))
            return true;
    }
    return false;
}

// Synthesize the default same-type transfer operators — op=(Self^) copy, op<--(Self^)
// move, op<-->(Self^) swap — for every class that does not DECLARE one (a user op
// shadows). They are REAL methods (a kFunctionDef + a kFunction entry in the class
// frame), so findClassOperator finds them like any operator and every copy/move/swap
// site dispatches them uniformly — no bespoke default path anywhere. Each body is
// by-slot iterative and recursive: one statement per field, `_$recv^.fi OP _$src^.fi`,
// which classify dispatches to THAT field's op — a class field recurses into its own
// synthesized op, a primitive/pointer leaf bottoms out at a plain store/move/swap. The
// vtable ptr is not a field, so it is never transferred; a base subobject (`_$base`,
// slot 0) IS a field, so it transfers via the base's op. Runs after the TYPES phase
// (fields + all user ops — including re-open members — registered) and before body
// resolution, so resolveScopeBodies + classify + desugar + codegen process the
// synthesized bodies exactly like user methods.
void synthesizeClassTransferOps(parse::Tree& tree, parse::Node& cnode,
                                diagnostic::Sink& diag) {
    (void)diag;
    if (cnode.resolved_entry_id < 0) return;
    parse::Entry const& ce = tree.entries[cnode.resolved_entry_id];
    widen::TypeRef cstrip = widen::strip(ce.slids_type);
    int frame = ce.ns_frame_id;
    auto itc = tree.classes.find(cstrip);
    if (itc == tree.classes.end()) return;
    // src_mutable: move / swap MUTATE the source (null it / exchange), so their pointer
    // param must be `mutable` (opts out of the by-pointer-to-const munge); copy reads a
    // const source.
    struct OpSpec { char const* name; parse::Kind stmt; bool src_mutable; };
    OpSpec const ops[] = {
        {"op=",    parse::Kind::kStoreStmt, false},
        {"op<--",  parse::Kind::kMoveStmt,  true},
        {"op<-->", parse::Kind::kSwapStmt,  true},
    };
    widen::TypeRef voidTy = widen::internOrNone("void");
    int file = cnode.file_id, tok = cnode.name_tok;
    widen::TypeRef selfPtr = widen::internPointer(cstrip);   // `C^`
    // Capture the field-name list BEFORE minting (addEntry / intern can move; the
    // ClassInfo ref could dangle — copy the names out).
    std::vector<std::string> fields = itc->second.field_names;
    for (OpSpec const& op : ops) {
        // Idempotent: classHasUserSelfOp matches a user OR an already-synthesized op, so
        // re-running this on the same class (or a class reached by two drivers) is a no-op.
        if (classHasUserSelfOp(tree, frame, op.name, cstrip)) continue;
        auto fn = std::make_unique<parse::Node>();
        fn->kind = parse::Kind::kFunctionDef;
        fn->name = op.name;
        fn->file_id = file; fn->tok = tok; fn->name_tok = tok;
        fn->return_type = voidTy;
        fn->params.push_back(parse::makeReceiverParam(selfPtr, file, tok));
        auto src = std::make_unique<parse::Node>();
        src->kind = parse::Kind::kParam;
        src->name = "_$src";
        src->file_id = file; src->tok = tok; src->name_tok = tok;
        src->return_type = selfPtr;
        src->is_mutable = op.src_mutable;
        fn->params.push_back(std::move(src));
        for (std::string const& f : fields) {
            auto st = std::make_unique<parse::Node>();
            st->kind = op.stmt;
            st->file_id = file; st->tok = tok;
            st->children.push_back(makeRecvFieldAccess("_$recv", f, file, tok));
            st->children.push_back(makeRecvFieldAccess("_$src", f, file, tok));
            fn->children.push_back(std::move(st));
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kFunction;
        e.name = op.name;
        e.slids_type = voidTy;
        e.param_types = {selfPtr, selfPtr};
        e.defined = true;
        e.synthesized = true;
        e.def_file_id = file;
        e.def_tok = tok;
        e.owner_ns_frame = frame;
        fn->resolved_entry_id = parse::addEntry(tree, std::move(e));
        cnode.children.push_back(std::move(fn));
    }
}

// Synthesize @C__$ctor — the COMPLETE constructor of an OPAQUE class (one declared
// incomplete in a header, completed by its sibling `.sl`). An importer sees the class as
// opaque: it knows the class exists and can name its methods, but not its fields, size, or
// offsets, so it cannot construct one itself. The completer — the only TU with the layout —
// exports @C__$ctor, which fully default-constructs an object at a caller-provided address.
//
// The body is a PLACEMENT NEW into the receiver pointer: `new(_$recv) C`. That reuses THE
// construction funnel wholesale — classifyClassInit fills every field with its author
// default, else zero, recursing into class / array / tuple fields, and emitConstructHooks
// runs the ctor hooks (which, for an opaque class, dispatch to @C__$pctor). So the exact
// same rules a normal `C c;` site obeys apply here; nothing is re-derived by hand. Only the
// completer (kDefine) synthesizes it; a `.sl`-local incomplete class is never opaque.
//
// It is a METHOD whose receiver IS the object (`void @C__$ctor(ptr %o)` — one pointer, the
// storage to construct into), matching the call emitConstructHooks makes at an importer.
void synthesizeOpaqueCtor(parse::Tree& tree, parse::Node& cnode,
                          diagnostic::Sink& diag) {
    (void)diag;
    if (cnode.resolved_entry_id < 0) return;
    parse::Entry const& ce = tree.entries[cnode.resolved_entry_id];
    widen::TypeRef cstrip = widen::strip(ce.slids_type);
    if (!widen::slidOpaque(cstrip)) return;
    if (widen::slidLinkage(cstrip) != widen::Type::Linkage::kDefine) return;
    int frame = ce.ns_frame_id;
    // Idempotent: a class reached by two drivers (primary + re-open) must synth once.
    for (parse::Entry const& e : tree.entries)
        if (e.kind == parse::EntryKind::kFunction && e.owner_ns_frame == frame
            && e.name == "_$octor")
            return;
    widen::TypeRef voidTy = widen::internOrNone("void");
    widen::TypeRef voidPtr = widen::internOrNone("void^");   // buffer ptr for placement
    widen::TypeRef selfPtr = widen::internPointer(cstrip);   // `C^`
    int file = cnode.file_id, tok = cnode.name_tok;
    auto ident = [&](std::string nm) {
        auto n = std::make_unique<parse::Node>();
        n->kind = parse::Kind::kIdentExpr;
        n->name = std::move(nm);
        n->file_id = file; n->tok = tok; n->name_tok = tok;
        return n;
    };
    auto fn = std::make_unique<parse::Node>();
    fn->kind = parse::Kind::kFunctionDef;
    fn->name = "_$octor";
    fn->file_id = file; fn->tok = tok; fn->name_tok = tok;
    fn->return_type = voidTy;
    fn->params.push_back(parse::makeReceiverParam(selfPtr, file, tok));
    // void^ _p = _$recv;   (a placement address must be a buffer pointer; C^ converts)
    auto decl = std::make_unique<parse::Node>();
    decl->kind = parse::Kind::kVarDeclStmt;
    decl->name = "_p";
    decl->file_id = file; decl->tok = tok; decl->name_tok = tok;
    decl->return_type = voidPtr;
    decl->children.push_back(ident("_$recv"));
    fn->children.push_back(std::move(decl));
    // _p = new(_p) C;      (placement construct AT _p; result discarded back into _p, so _p
    // is read — no unused-variable halt — and the object is default-built + hooked in place)
    auto newx = std::make_unique<parse::Node>();
    newx->kind = parse::Kind::kNewExpr;
    newx->return_type = cstrip;
    newx->file_id = file; newx->tok = tok; newx->name_tok = tok;
    newx->children.push_back(nullptr);        // [0] array size — single object
    newx->children.push_back(ident("_p"));    // [1] placement address
    newx->children.push_back(nullptr);        // [2] ctor args — none => all defaults
    auto asn = std::make_unique<parse::Node>();
    asn->kind = parse::Kind::kAssignStmt;
    asn->name = "_p";
    asn->file_id = file; asn->tok = tok; asn->name_tok = tok;
    asn->children.push_back(std::move(newx));
    fn->children.push_back(std::move(asn));
    parse::Entry e;
    e.kind = parse::EntryKind::kFunction;
    e.name = "_$octor";
    e.slids_type = voidTy;
    e.param_types = {selfPtr};
    e.defined = true;
    e.synthesized = true;
    e.def_file_id = file;
    e.def_tok = tok;
    e.owner_ns_frame = frame;
    fn->resolved_entry_id = parse::addEntry(tree, std::move(e));
    cnode.children.push_back(std::move(fn));
}

// ---- Template instantiation (called from classify) ---------------------------

// Deep copy of a pristine (parse-stage) subtree — every field a parse node carries,
// the unique_ptr vectors recursing with null slots preserved (a switch default's
// absent label, an empty construction slot).
static std::unique_ptr<parse::Node> cloneDeep(parse::Node const& n) {
    auto c = std::make_unique<parse::Node>();
    c->kind = n.kind;
    c->name = n.name;
    c->text = n.text;
    c->return_type = n.return_type;
    c->return_type_seg_toks = n.return_type_seg_toks;
    c->nominal_type = n.nominal_type;
    c->inferred_type = n.inferred_type;
    c->op_type = n.op_type;
    c->alias_label = n.alias_label;
    c->strong_type = n.strong_type;
    c->file_id = n.file_id;
    c->tok = n.tok;
    c->name_tok = n.name_tok;
    c->resolved_entry_id = n.resolved_entry_id;
    c->range_dotdot_tok = n.range_dotdot_tok;
    c->label = n.label;
    c->loop_levels = n.loop_levels;
    c->is_const = n.is_const;
    c->const_method = n.const_method;
    c->is_global = n.is_global;
    c->global_group_id = n.global_group_id;
    c->is_reopen = n.is_reopen;
    c->is_incomplete = n.is_incomplete;
    c->is_construction = n.is_construction;
    c->is_temp_init = n.is_temp_init;
    c->ctor_no_args = n.ctor_no_args;
    c->class_op_chain = n.class_op_chain;
    c->op_collapse_head = n.op_collapse_head;
    c->op_bin_eid = n.op_bin_eid;
    c->op_un_eid = n.op_un_eid;
    c->op_aug_eid = n.op_aug_eid;
    c->op_eq_lhs_eid = n.op_eq_lhs_eid;
    c->op_eq_rhs_eid = n.op_eq_rhs_eid;
    c->op_move_eid = n.op_move_eid;
    c->parenless = n.parenless;
    c->class_conversion = n.class_conversion;
    c->agg_conv_spill = n.agg_conv_spill;
    c->is_mutable = n.is_mutable;
    c->is_virtual = n.is_virtual;
    c->is_pure = n.is_pure;
    c->is_foreign = n.is_foreign;
    c->bypass_virtual = n.bypass_virtual;
    c->default_move_init = n.default_move_init;
    c->default_swap_init = n.default_swap_init;
    c->construction_init = n.construction_init;
    c->quiet_diag = n.quiet_diag;
    c->require_homogeneous = n.require_homogeneous;
    c->non_completing = n.non_completing;
    c->qualifier = n.qualifier;
    c->qualifier_toks = n.qualifier_toks;
    c->global_qualified = n.global_qualified;
    c->type_params = n.type_params;
    c->type_param_toks = n.type_param_toks;
    c->tmpl_args = n.tmpl_args;
    c->tmpl_arg_toks = n.tmpl_arg_toks;
    c->param_types = n.param_types;
    c->captures = n.captures;
    c->capture_types = n.capture_types;
    c->self_entry_id = n.self_entry_id;
    for (auto const& d : n.dim_exprs)
        c->dim_exprs.push_back(d ? cloneDeep(*d) : nullptr);
    for (auto const& ch : n.children)
        c->children.push_back(ch ? cloneDeep(*ch) : nullptr);
    for (auto const& p : n.params)
        c->params.push_back(p ? cloneDeep(*p) : nullptr);
    return c;
}

static widen::TypeRef firstLocalArg(parse::Tree& tree,
                                    std::vector<widen::TypeRef> const& args);
static bool anyArgLocal(parse::Tree& tree,
                        std::vector<widen::TypeRef> const& args);

int instantiateTemplate(parse::Tree& tree, int tmpl_entry_id,
                        std::vector<widen::TypeRef> const& args,
                        int file_id, int tok, diagnostic::Sink& diag,
                        bool& created, parse::Node*& instance_node) {
    created = false;
    instance_node = nullptr;
    auto it = tree.templates.find(tmpl_entry_id);
    if (it == tree.templates.end()) return -1;
    parse::TemplateInfo& ti = it->second;
    auto memo = ti.instances.find(args);
    if (memo != ti.instances.end()) return memo->second;
    if (!ti.snapshot_taken) return -1;   // registration errored upstream

    // MODE (the class twin's rule): a HEADER-declared template's flavor is
    // AGGREGATED (all args header-visible: a declaration-only instance here,
    // defined by the source's --instantiate pass) or INLINE (a LOCAL arg: the
    // body clones from the loaded template source, emitted internal — nobody
    // else can). The sibling — and a TU-local template — defines as before.
    bool header_tmpl =
        fileIsImported(tree, tree.entries[tmpl_entry_id].file_id);
    bool here_sibling =
        fileIsSibling(tree, tree.entries[tmpl_entry_id].file_id);
    bool inline_local = header_tmpl && !here_sibling && anyArgLocal(tree, args);
    bool declare_only = header_tmpl && !here_sibling && !inline_local;
    if (inline_local && ti.def->kind != parse::Kind::kFunctionDef) {
        diagnostic::report(diag, {file_id, tok,
            "Instantiating '" + tree.entries[tmpl_entry_id].name
            + "' with a local type needs its template source (the .sl beside "
            "its header), which was not found.", {}});
        return -1;
    }

    // Save the caller's transient scope state (empty at classify time; mid-pipeline
    // values during a chained instantiation), install the definition-point snapshot.
    auto sv_frames = std::move(tree.frame_id_stack);
    auto sv_starts = std::move(tree.frame_entries_start_stack);
    auto sv_live   = std::move(tree.live_entry_ids);
    auto sv_ns     = std::move(tree.open_ns_frames);
    auto sv_init   = std::move(tree.initialized_locals);
    auto sv_arrays = std::move(tree.assigned_arrays);
    auto sv_checks = std::move(tree.nested_call_checks);
    auto sv_class  = std::move(tree.current_class_name);
    auto sv_base   = std::move(tree.current_base_name);
    auto sv_self   = std::move(tree.tmpl_self_stack);
    tree.frame_id_stack = ti.frame_id_stack;
    tree.frame_entries_start_stack = ti.frame_entries_start_stack;
    tree.live_entry_ids = ti.live_entry_ids;
    tree.open_ns_frames = ti.open_ns_frames;
    tree.initialized_locals = ti.initialized_locals;
    tree.assigned_arrays = ti.assigned_arrays;
    tree.current_class_name = ti.current_class_name;
    tree.current_base_name = ti.current_base_name;
    tree.tmpl_self_stack = ti.tmpl_self_stack;   // a class-template flavor's member
                                                 //   pattern re-enters with its bare-
                                                 //   name redirect live (recv, self)
    tree.nested_call_checks.clear();

    // A frame binding each type parameter as a transparent alias to its bound type —
    // the clone's `T`s then resolve exactly like any aliased type.
    parse::pushFrame(tree);
    for (std::size_t i = 0; i < ti.def->type_params.size() && i < args.size(); i++) {
        parse::Entry a;
        a.kind = parse::EntryKind::kAlias;
        a.name = ti.def->type_params[i];
        a.slids_type = args[i];
        a.file_id = ti.def->file_id;
        a.tok = ti.def->name_tok;
        parse::addEntry(tree, std::move(a));
    }

    // Clone the pristine definition; the clone is an ORDINARY function from here on.
    auto clone = cloneDeep(*ti.def);
    clone->type_params.clear();
    clone->type_param_toks.clear();
    // An AGGREGATED flavor keeps the signature, sheds the body (the loaded
    // template source supplied one): declaration-only here.
    if (declare_only && clone->kind == parse::Kind::kFunctionDef) {
        clone->kind = parse::Kind::kFunctionDecl;
        clone->children.clear();
    }

    // Register the instance entry — the clone's signature types resolve through the
    // type-parameter alias frame.
    if (clone->return_type != widen::kNoType)
        resolveDeclType(tree, clone->return_type, clone->file_id, clone->tok, diag);
    std::vector<widen::TypeRef> ptypes;
    int nreq = 0;
    bool seen_default = false;
    for (auto& p : clone->params) {
        if (!p) continue;
        if (!p->children.empty()) seen_default = true;
        else if (!seen_default) nreq++;
        if (p->return_type != widen::kNoType)
            resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
        ptypes.push_back(p->return_type);
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kFunction;
    e.name = clone->name;
    e.slids_type = clone->return_type;
    e.param_types = std::move(ptypes);
    e.num_required = nreq;
    e.file_id = tree.entries[tmpl_entry_id].file_id;
    e.tok = tree.entries[tmpl_entry_id].tok;
    e.defined = true;
    e.tmpl_args = args;
    e.def_file_id = ti.def->file_id;
    e.def_tok = ti.def->name_tok;
    int iid = parse::addEntry(tree, std::move(e));
    // The instance is reached ONLY through resolved_entry_id stamps, never by name
    // — drop it from the live set, or a recursive call in the body would resolve
    // to THIS instance instead of the template and pin T to this binding (wrong
    // whenever the recursion re-binds, e.g. `deeper(^p)` needing T^).
    tree.live_entry_ids.pop_back();
    // The instance LIVES where its template does — the overload-candidate filters
    // and symbolFor's nested-vs-file test read these scope fields.
    tree.entries[iid].parent_frame_id = tree.entries[tmpl_entry_id].parent_frame_id;
    tree.entries[iid].owner_ns_frame = tree.entries[tmpl_entry_id].owner_ns_frame;
    clone->resolved_entry_id = iid;
    // Memo BEFORE the body resolves, so a self-recursive call inside lands here.
    ti.instances[args] = iid;

    if (clone->kind == parse::Kind::kFunctionDef) {
        // An inline-local body resolves under the private-name diagnostic
        // context: a reference to a name the template-source strip dropped
        // reports the full cross-TU chain at THIS use site.
        if (inline_local)
            tree.inline_inst_ctx.push_back(
                {tmpl_entry_id, file_id, tok, firstLocalArg(tree, args)});
        resolveFunctionBody(tree, *clone, diag, /*nested=*/ti.nested);
        if (inline_local) tree.inline_inst_ctx.pop_back();
        mungeParamTypes(tree, *clone, diag);
        // An INLINE-local flavor is this TU's own function: re-home the entry
        // to the root file so the linkage decision (declared-in-header ->
        // external) reads it as file-local — emitted `define internal`. The
        // flag is the METHOD carve-out: a member instance's linkage follows
        // its OWNER class (declare-only here), and liftMember overrides that
        // for a flavor nobody else can spell.
        if (inline_local) {
            tree.entries[iid].file_id = 0;
            tree.entries[iid].tu_local_instance = true;
        }
    } else {
        // A DECLARATION-ONLY instance: the pattern is a header's bodyless
        // template declaration and this TU is a consumer — the body is
        // emitted by the template source's --instantiate pass. The entry
        // becomes an external declaration (munged, so the extern signature
        // matches the definition's); the node splices as a plain kFunctionDecl
        // and codegen `declare`s it on use.
        mungeParamTypes(tree, *clone, diag);
        tree.entries[iid].defined = false;
        tree.entries[iid].is_external = true;
    }

    parse::popFrame(tree);
    tree.frame_id_stack = std::move(sv_frames);
    tree.frame_entries_start_stack = std::move(sv_starts);
    tree.live_entry_ids = std::move(sv_live);
    tree.open_ns_frames = std::move(sv_ns);
    tree.initialized_locals = std::move(sv_init);
    tree.assigned_arrays = std::move(sv_arrays);
    tree.nested_call_checks = std::move(sv_checks);
    tree.current_class_name = std::move(sv_class);
    tree.current_base_name = std::move(sv_base);
    tree.tmpl_self_stack = std::move(sv_self);

    // Park the instance for the end-of-classify splice into its host list; hand the
    // caller the node for the remaining stages (constfold + classify).
    instance_node = clone.get();
    tree.pending_tmpl_instances.push_back({ti.host_list, ti.def, std::move(clone)});
    created = true;
    return iid;
}

// ---- CLASS-template instantiation (from resolveTypeRef / the construction arm) --
//
// The instance's NAME/TYPES/needs phases run HERE, at the triggering use — the
// caller needs a complete TYPE in hand (layout, sizeof, hooks). The BODY phase
// defers: during resolve, to the end-of-resolve drain (file-scope visibility is
// complete there); after resolve (a use inside a function template's body,
// instantiated at classify), it runs synchronously — everything is registered by
// then — and classify runs the late stages over the node.
//
// The instantiation frame binds each T as a transparent alias AND the template's
// own bare name as an alias to the instance type — the receiver (`Vec^`), a
// self-typed member, and a recursive `Vec<T>` in the body all resolve through
// them. The instance is NAMED by its canonical spelling (`Vec<int>`, resolved
// args); widen::classSymbol sanitizes that name for LLVM symbols.

// Bind the T aliases in the CURRENT frame. (The template's OWN name is NOT
// aliased — it resolves to the template entry and the tmpl_self_stack redirects
// it to the instance. An alias would shadow the template and break a recursive
// `Node<T>` use inside the body, which must reach the kTmplUse machinery.)
static void bindInstanceAliases(parse::Tree& tree, int tmpl_entry_id,
                                std::vector<widen::TypeRef> const& args) {
    parse::Node* def = tree.templates.at(tmpl_entry_id).def;
    for (std::size_t i = 0; i < def->type_params.size() && i < args.size(); i++) {
        parse::Entry a;
        a.kind = parse::EntryKind::kAlias;
        a.name = def->type_params[i];
        a.slids_type = args[i];
        a.file_id = def->file_id;
        a.tok = def->name_tok;
        parse::addEntry(tree, std::move(a));
    }
}

// The transient-scope-state save/install/restore of instantiateTemplate, shared
// by the class instantiation and the body drain. Default-constructed `Saved`
// holds the caller's state after save(); restore() puts it back.
struct SavedResolveState {
    std::vector<int> frames, live, ns;
    std::vector<std::size_t> starts;
    std::set<int> init, arrays;
    std::vector<parse::Tree::NestedCallCheck> checks;
    std::string cls, base;
};
static SavedResolveState saveResolveState(parse::Tree& tree) {
    SavedResolveState s;
    s.frames = std::move(tree.frame_id_stack);
    s.starts = std::move(tree.frame_entries_start_stack);
    s.live   = std::move(tree.live_entry_ids);
    s.ns     = std::move(tree.open_ns_frames);
    s.init   = std::move(tree.initialized_locals);
    s.arrays = std::move(tree.assigned_arrays);
    s.checks = std::move(tree.nested_call_checks);
    s.cls    = std::move(tree.current_class_name);
    s.base   = std::move(tree.current_base_name);
    return s;
}
static void installSnapshot(parse::Tree& tree, parse::TemplateInfo const& ti) {
    tree.frame_id_stack = ti.frame_id_stack;
    tree.frame_entries_start_stack = ti.frame_entries_start_stack;
    tree.live_entry_ids = ti.live_entry_ids;
    tree.open_ns_frames = ti.open_ns_frames;
    tree.initialized_locals = ti.initialized_locals;
    tree.assigned_arrays = ti.assigned_arrays;
    tree.current_class_name = ti.current_class_name;
    tree.current_base_name = ti.current_base_name;
    tree.nested_call_checks.clear();
}
static void restoreResolveState(parse::Tree& tree, SavedResolveState& s) {
    tree.frame_id_stack = std::move(s.frames);
    tree.frame_entries_start_stack = std::move(s.starts);
    tree.live_entry_ids = std::move(s.live);
    tree.open_ns_frames = std::move(s.ns);
    tree.initialized_locals = std::move(s.init);
    tree.assigned_arrays = std::move(s.arrays);
    tree.nested_call_checks = std::move(s.checks);
    tree.current_class_name = std::move(s.cls);
    tree.current_base_name = std::move(s.base);
}

// The first type argument whose class is declared in THIS TU's own source
// (not an imported header) — kNoType if none. Such a flavor cannot aggregate:
// its spelling means nothing to the template source's compile, so it
// instantiates INLINE, internal. The returned class also names the
// private-name diagnostic's "local class" note.
static widen::TypeRef firstLocalArg(parse::Tree& tree,
                                    std::vector<widen::TypeRef> const& args) {
    for (widen::TypeRef a : args) {
        widen::TypeRef s = widen::strip(a);
        while (widen::form(s) == widen::Type::Form::kPointer
               || widen::form(s) == widen::Type::Form::kIterator)
            s = widen::strip(widen::get(s).pointee);
        if (widen::form(s) != widen::Type::Form::kSlid) continue;
        auto ci = tree.classes.find(s);
        if (ci != tree.classes.end()
            && !fileIsImported(tree, ci->second.def_file_id)) return s;
    }
    return widen::kNoType;
}
static bool anyArgLocal(parse::Tree& tree,
                        std::vector<widen::TypeRef> const& args) {
    return firstLocalArg(tree, args) != widen::kNoType;
}

int instantiateClassTemplate(parse::Tree& tree, int tmpl_entry_id,
                             std::vector<widen::TypeRef> const& args,
                             int file_id, int tok, diagnostic::Sink& diag) {
    auto it = tree.templates.find(tmpl_entry_id);
    if (it == tree.templates.end()) return -1;
    {
        auto memo = it->second.instances.find(args);
        if (memo != it->second.instances.end()) return memo->second;
    }
    // A never-completed INCOMPLETE template has no layout to instantiate. A
    // plain class left open completes in another translation unit via its
    // header; cross-TU templates have not landed, so this is an error, not a
    // half-class. (The end-of-resolve sweep catches the never-USED case;
    // open_reported keeps the two sites to one diagnostic.)
    if (it->second.cls_open) {
        if (!it->second.open_reported) {
            it->second.open_reported = true;
            diagnostic::report(diag, {file_id, tok,
                "Class template '" + tree.entries[tmpl_entry_id].name
                + "' is declared incomplete ('...') and never completed; "
                "cross-TU completion of a template is not supported yet — "
                "close it with a re-open that omits the '...'.", {}});
        }
        return -1;
    }
    // MODE. A HEADER-declared template's flavor is either AGGREGATED — every
    // argument header-visible: declaration-only here (method bodies stripped
    // from the clones), stable file-scope symbols, kDeclare linkage, defined
    // once by the source's --instantiate pass — or INLINE: an argument is
    // LOCAL to this TU, so nobody else can emit the flavor; the bodies clone
    // from the loaded template source and the instance is internal, def_id-
    // suffixed. The header's SIBLING defines externally, as before.
    bool header_tmpl = fileIsImported(tree, tree.entries[tmpl_entry_id].file_id);
    bool here_sibling = fileIsSibling(tree, tree.entries[tmpl_entry_id].file_id);
    bool inline_local = header_tmpl && !here_sibling && anyArgLocal(tree, args);
    bool declare_only = header_tmpl && !here_sibling && !inline_local;
    if (inline_local) {
        // The bodies must exist HERE — the template source beside the header.
        bool has_bodies = false, has_decls = false;
        auto scan = [&](parse::Node* o) {
            for (auto& m : o->children) {
                if (!m) continue;
                if (m->kind == parse::Kind::kFunctionDef) has_bodies = true;
                if (m->kind == parse::Kind::kFunctionDecl) has_decls = true;
            }
        };
        scan(it->second.def);
        for (parse::Node* r : it->second.reopens) scan(r);
        if (has_decls && !has_bodies) {
            diagnostic::report(diag, {file_id, tok,
                "Instantiating '" + tree.entries[tmpl_entry_id].name
                + "' with a local type needs its template source (the .sl "
                "beside its header), which was not found.", {}});
            return -1;
        }
    }
    if (tree.tmpl_instantiation_depth >= 64
        || tree.class_instance_total >= 512) {
        diagnostic::report(diag, {file_id, tok,
            "Template instantiation depth limit exceeded (a runaway recursive "
            "instantiation?).", {}});
        return -1;
    }
    tree.tmpl_instantiation_depth++;
    tree.class_instance_total++;

    // Install the definition-point snapshot when it exists. A trigger BEFORE the
    // snapshot pass (another class's field type, Pass 1a-types) runs on the
    // current state — which at that point IS file scope, mid-registration, the
    // right visibility for the NAME/TYPES phases run here.
    bool installed = it->second.snapshot_taken;
    SavedResolveState saved;
    if (installed) {
        saved = saveResolveState(tree);
        installSnapshot(tree, it->second);
    }
    parse::pushFrame(tree);
    std::size_t live_mark = tree.live_entry_ids.size();
    bindInstanceAliases(tree, tmpl_entry_id, args);

    // Clone EVERY opening — the primary, then each re-open, in source order —
    // named by the canonical use spelling. The re-open clones hit the ordinary
    // plain-class merge at registration (is_reopen, pending_fields,
    // pending_hooks), so appended fields, contributed hooks, and member sets
    // land per instance exactly as a plain class's openings do.
    parse::Node* def = tree.templates.at(tmpl_entry_id).def;
    std::string iname = def->name + "<";
    for (std::size_t i = 0; i < args.size(); i++) {
        if (i) iname += ", ";
        iname += widen::spell(args[i]);
    }
    iname += ">";
    std::vector<std::unique_ptr<parse::Node>> clones;
    {
        std::vector<parse::Node*> openings;
        openings.push_back(def);
        for (parse::Node* r : tree.templates.at(tmpl_entry_id).reopens)
            openings.push_back(r);
        for (parse::Node* o : openings) {
            auto c = cloneDeep(*o);
            c->type_params.clear();
            c->type_param_toks.clear();
            c->name = iname;
            c->resolved_entry_id = -1;   // a re-open pattern points at the
                                         // template entry; the clone re-binds
            // A DECLARE-ONLY flavor is the HEADER's interface alone. The
            // primary's members are declarations already; a RE-OPEN clone
            // (the loaded template source's bodies) sheds its function
            // members entirely — a stripped twin would sit beside the
            // header's declaration as an ambiguous duplicate, and the bodies
            // are defined once, externally, by the --instantiate pass.
            if (declare_only) {
                if (o == def) {
                    for (auto& m : c->children) {
                        if (m && m->kind == parse::Kind::kFunctionDef) {
                            m->kind = parse::Kind::kFunctionDecl;
                            m->children.clear();
                        }
                    }
                } else {
                    for (auto& m : c->children) {
                        if (m && (m->kind == parse::Kind::kFunctionDef
                                  || m->kind == parse::Kind::kFunctionDecl))
                            m.reset();
                    }
                    c->children.erase(
                        std::remove(c->children.begin(), c->children.end(),
                                    nullptr),
                        c->children.end());
                }
            }
            clones.push_back(std::move(c));
        }
    }

    // The NAME phase: entry + frame + placeholder ClassInfo (unique: the name
    // carries the args, and the def_id carries this transient frame — except a
    // cross-TU AGGREGATED flavor, whose symbol must be the same in every TU:
    // file-scope def_id, the canonical spelling alone. An INLINE-local flavor
    // keeps the frame def_id — it is this TU's private class.)
    registerClassName(tree, *clones[0], diag, /*member_of=*/-1,
                      /*file_scope_def_id=*/header_tmpl && !inline_local);
    int iid = clones[0]->resolved_entry_id;
    if (iid < 0) {
        parse::popFrame(tree);
        if (installed) restoreResolveState(tree, saved);
        tree.tmpl_instantiation_depth--;
        return -1;
    }
    // From here the template's bare name means THE INSTANCE (the receiver
    // `Vec^`, a self-typed member, self-construction) via the stack redirect.
    tree.tmpl_self_stack.push_back({tmpl_entry_id, iid});
    // Memo BEFORE the member phases, so a recursive `Vec<T>^` field lands here.
    tree.templates.at(tmpl_entry_id).instances[args] = iid;
    tree.entries[iid].tmpl_args = args;

    // NAME (members, opening by opening — a re-open clone finds the primary
    // clone in this frame and merges) -> TYPES per opening (registerClassBody
    // runs once, on the primary; a re-open's members still type) -> cycle +
    // needs + the virtual rules (the re-open flavor runs on the re-open
    // clones, as the file-scope passes do on re-open nodes).
    std::vector<parse::Node*> classes;
    classes.push_back(clones[0].get());
    registerScopeNames(tree, *clones[0], tree.entries[iid].ns_frame_id, classes,
                       diag);
    for (std::size_t i = 1; i < clones.size(); i++) {
        registerClassName(tree, *clones[i], diag);
        if (clones[i]->resolved_entry_id >= 0)
            registerScopeNames(tree, *clones[i], tree.entries[iid].ns_frame_id,
                               classes, diag);
    }
    // The instance LIVES where its template does (the symbol's scope path).
    // Stamped only NOW: findInFrame skips owner-bearing entries (a namespace
    // member is not a lexical occupant), so setting this before the re-open
    // clones registered would hide the primary from their merge lookup — a
    // namespace-member template's re-open then re-registered as a SECOND class
    // and its synthesized transfer ops emitted twice (invalid IR at llc).
    tree.entries[iid].owner_ns_frame = tree.entries[tmpl_entry_id].owner_ns_frame;
    for (auto& c : clones) resolveScopeTypes(tree, *c, /*isClass=*/true, diag);
    // registerClassBody stamped linkage off the clone's HEADER file_id
    // (kDeclare here, a pure consumer) — an INLINE-local flavor is this TU's
    // own class: internal, bodies emitted here, nobody else names it.
    if (inline_local)
        widen::setSlidLinkage(widen::strip(clones[0]->return_type),
                              widen::Type::Linkage::kInternal);
    checkClassCyclesAndNeeds(tree, classes, diag);
    for (std::size_t i = 1; i < clones.size(); i++)
        validateVirtualClass(tree, *clones[i], diag);

    bool late = tree.resolve_done;
    if (late) {
        // Post-resolve (classify demanded this instance): everything is
        // registered, so the bodies resolve right here, under this frame's T
        // bindings; classify runs the late stages over the parked nodes.
        // Inline-local bodies carry the private-name diagnostic context.
        if (inline_local)
            tree.inline_inst_ctx.push_back(
                {tmpl_entry_id, file_id, tok, firstLocalArg(tree, args)});
        for (auto& c : clones) {
            resolveScopeBodies(tree, *c, /*isClass=*/true, diag);
            mungeParamTypes(tree, *c, diag);
        }
        if (inline_local) tree.inline_inst_ctx.pop_back();
    }

    tree.tmpl_self_stack.pop_back();
    // The instantiation frame dies here, but the instance's MEMBERS must stay
    // LIVE (findMemberLive filters on liveness — a base-member bare reference,
    // a member const, a synthesized operator). Collect every member entry the
    // phases registered (owner_ns_frame >= 0 — the T aliases are lexical and
    // rightly die) and re-publish them past the pop/restore. The list also
    // rides the pending entries: the drain installs the template's snapshot,
    // whose live set predates this instance.
    std::vector<int> keep;
    for (std::size_t k = live_mark; k < tree.live_entry_ids.size(); k++) {
        int eid = tree.live_entry_ids[k];
        if (tree.entries[eid].owner_ns_frame >= 0) keep.push_back(eid);
    }
    for (auto& c : clones) {
        parse::TemplateInfo& ti = tree.templates.at(tmpl_entry_id);
        tree.pending_class_instances.push_back(
            {ti.host_list, ti.def, std::move(c), tmpl_entry_id, iid, args,
             keep, /*body_resolved=*/late, file_id, tok});
    }
    parse::popFrame(tree);
    if (installed) restoreResolveState(tree, saved);
    tree.live_entry_ids.insert(tree.live_entry_ids.end(), keep.begin(), keep.end());
    tree.tmpl_instantiation_depth--;
    return iid;
}

// The end-of-resolve drain: resolve every queued instance BODY (a body may mint
// further instances — the index loop picks them up), then splice every instance
// node into its host list right after its template's definition node.
void drainClassTemplateBodies(parse::Tree& tree, diagnostic::Sink& diag) {
    for (std::size_t i = 0; i < tree.pending_class_instances.size(); i++) {
        // NEVER hold a reference across the body resolve — it may push new
        // entries and reallocate the vector. Copy what the re-entry needs.
        if (tree.pending_class_instances[i].body_resolved) continue;
        tree.pending_class_instances[i].body_resolved = true;
        int tid = tree.pending_class_instances[i].tmpl_entry_id;
        int iid = tree.pending_class_instances[i].instance_entry_id;
        parse::Node* node = tree.pending_class_instances[i].node.get();
        std::vector<widen::TypeRef> args = tree.pending_class_instances[i].args;
        std::vector<int> members = tree.pending_class_instances[i].member_entries;
        int use_file = tree.pending_class_instances[i].use_file;
        int use_tok = tree.pending_class_instances[i].use_tok;
        auto ti = tree.templates.find(tid);
        if (ti == tree.templates.end() || !node) continue;
        // Inline-local bodies drain under the private-name diagnostic context
        // (mode recomputed — the pending entry predates the drain).
        bool inl = fileIsImported(tree, tree.entries[tid].file_id)
            && !fileIsSibling(tree, tree.entries[tid].file_id)
            && anyArgLocal(tree, args);
        if (inl)
            tree.inline_inst_ctx.push_back(
                {tid, use_file, use_tok, firstLocalArg(tree, args)});

        SavedResolveState saved = saveResolveState(tree);
        installSnapshot(tree, ti->second);
        parse::pushFrame(tree);
        // The snapshot's live set predates this instance — re-publish its
        // members so a body's bare member reference (a const, an enum member)
        // resolves. Appended above the frame watermark, they drop with the pop;
        // the caller's own live set already carries them.
        tree.live_entry_ids.insert(tree.live_entry_ids.end(),
                                   members.begin(), members.end());
        std::size_t live_mark = tree.live_entry_ids.size();
        bindInstanceAliases(tree, tid, args);
        tree.tmpl_self_stack.push_back({tid, iid});
        resolveScopeBodies(tree, *node, /*isClass=*/true, diag);
        tree.tmpl_self_stack.pop_back();
        if (inl) tree.inline_inst_ctx.pop_back();
        // Keep the members the BODY phase registered (the synthesized transfer
        // operators) live past the pop/restore — same move as instantiation.
        std::vector<int> keep;
        for (std::size_t k = live_mark; k < tree.live_entry_ids.size(); k++) {
            int eid = tree.live_entry_ids[k];
            if (tree.entries[eid].owner_ns_frame >= 0) keep.push_back(eid);
        }
        parse::popFrame(tree);
        restoreResolveState(tree, saved);
        tree.live_entry_ids.insert(tree.live_entry_ids.end(),
                                   keep.begin(), keep.end());
    }
    for (auto& pci : tree.pending_class_instances) {
        if (!pci.host_list || !pci.node) continue;
        auto& list = *pci.host_list;
        std::size_t at = 0;   // def-not-found fallback: the FRONT, like a
                              // function instance
        for (std::size_t j = 0; j < list.size(); j++) {
            if (list[j].get() == pci.after) { at = j + 1; break; }
        }
        list.insert(list.begin() + at, std::move(pci.node));
    }
    tree.pending_class_instances.clear();
}

// Post-resolve instances (minted at classify): hand the nodes to the caller for
// the late stages, and move each into pending_tmpl_instances so classify's
// end-of-walk splice places it like a function instance.
std::vector<parse::Node*> takeResolvedClassInstances(parse::Tree& tree) {
    std::vector<parse::Node*> out;
    for (auto& pci : tree.pending_class_instances) {
        if (!pci.node) continue;
        out.push_back(pci.node.get());
        tree.pending_tmpl_instances.push_back(
            {pci.host_list, pci.after, std::move(pci.node)});
    }
    tree.pending_class_instances.clear();
    return out;
}

void run(parse::Tree& tree, diagnostic::Sink& diag) {
    parse::Node* program = nullptr;
    for (auto& n : tree.nodes) {
        if (n && n->kind == parse::Kind::kProgram) {
            program = n.get();
            break;
        }
    }
    if (!program) return;

    parse::pushFrame(tree);   // program frame

    // TEMPLATE-SOURCE files (a `vector.sl` loaded beside its imported header so
    // LOCAL-type instances have bodies to clone): only their TEMPLATE content
    // participates in this TU — template function definitions (including the
    // qualified out-of-line member form), class-template openings, and alias
    // templates. Everything else — private helpers, plain classes and their
    // out-of-line bodies, consts, globals — is the template source's own TU's
    // business and is DROPPED here, before relocation or registration can see
    // it. (Consequence: a template body that references a TU-private name
    // fails with the natural unresolved-name error when instantiated with a
    // local type — such a body is only emittable by its own TU.)
    {
        bool dropped = false;
        for (auto& ch : program->children) {
            if (!ch || !fileIsTemplateSource(tree, ch->file_id)) continue;
            bool keep = !ch->type_params.empty()
                && (ch->kind == parse::Kind::kFunctionDef
                    || ch->kind == parse::Kind::kClassDef
                    || ch->kind == parse::Kind::kAliasDecl);
            if (!keep) {
                // Remember the dropped NAME: an inline-local instance body
                // that references it gets the full private-name chain instead
                // of a bare "Unknown ...". (The tokens survive the drop, so
                // the record can caret the private definition.)
                if (!ch->name.empty()) {
                    char const* kind =
                        ch->kind == parse::Kind::kFunctionDef  ? "function"
                        : ch->kind == parse::Kind::kClassDef   ? "class"
                        : ch->kind == parse::Kind::kAliasDecl  ? "alias"
                        : ch->kind == parse::Kind::kEnumDecl   ? "enum"
                        : ch->kind == parse::Kind::kNamespaceDecl ? "namespace"
                                                               : "declaration";
                    tree.stripped_privates.emplace(ch->name,
                        parse::Tree::StrippedPrivate{kind, ch->file_id,
                                                     ch->name_tok});
                }
                ch.reset();
                dropped = true;
            }
        }
        if (dropped) {
            program->children.erase(
                std::remove(program->children.begin(), program->children.end(),
                            nullptr),
                program->children.end());
        }
    }

    // Out-of-line member defs — every external re-open form (`Ret Class:method(){…}`,
    // `Class:Ns{}`, `Class:R(){}`, `const int Class:k=…;`, `alias Class:A=…;`,
    // `enum int Class:E(…);`) — move into their target class before any registration
    // pass sees them, so what remains here is only same-scope (non-qualified) members.
    relocateOutOfLineMembers(tree, program->children, diag);

    // Dissolve anonymous global groups into bare members in their enclosing scope
    // (all depths), before any name registration sees them.
    int anon_group_counter = 0;
    explodeAnonGlobalGroups(*program, anon_group_counter);

    // Every function name, so alias registration can tell a FUNCTION alias from a TYPE
    // alias (a function alias merges into the overload set; it must not mint a kAlias).
    collectFunctionNames(*program, tree.all_function_names);

    // Pass 1a-alias — register all file-scope value aliases first, so any decl
    // below (in any order) can resolve through them. Targets are validated after
    // enums / namespaces / classes register (an alias may target one of those —
    // `alias Time = Space;`). Bare `alias Ns;` (no target) is a namespace import,
    // handled at use scope. A qualified alias was relocated above.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl
            && ch->return_type != widen::kNoType) {
            if (isFuncAlias(tree, *ch)) recordFuncAlias(tree, *ch, kGlobalFrame);
            else registerAlias(tree, *ch, diag);
        }
    }

    // Pass 1a-enum — register every file-scope enum (alias + namespace + member
    // consts, or bare consts for anonymous) before namespaces / bare aliases so
    // a later `alias EnumName;` import and qualified `Enum:member` both resolve.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kEnumDecl) {
            registerEnum(tree, *ch, kGlobalFrame, diag);
        }
    }

    // Pass 1a-names — the global NAME phase: one recursive registerScopeNames over
    // every file-scope namespace and class registers all member NAMES + frames +
    // slotless kSlid + placeholder ClassInfo (NO type resolution), recursing through
    // nested namespaces AND classes uniformly. Every class NODE — file-scope,
    // namespace-nested, hoisted, any depth — is collected into `all_classes`.
    std::vector<parse::Node*> all_classes;
    // Re-open nodes are held out of all_classes (they own no layout body) but still carry
    // their own member set — collected here so the virtual-class rules (notably: a re-open
    // may not add a NEW virtual method) run on them too.
    std::vector<parse::Node*> reopen_classes;
    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kNamespaceDecl) {
            int ns = openNamespace(tree, ch->name, ch->name_tok, ch->file_id,
                                   kGlobalFrame, diag);
            if (ns < 0) continue;
            ch->resolved_entry_id = ns;
            registerScopeNames(tree, *ch, ns, all_classes, diag);
        } else if (ch->kind == parse::Kind::kClassDef) {
            // A file-scope CLASS TEMPLATE: register the pattern only — no members,
            // no ClassInfo, no layout passes; uses instantiate it on demand.
            if (!ch->type_params.empty()) {
                registerClassTemplate(tree, *ch, &program->children,
                                      /*owner=*/-1, diag);
                continue;
            }
            registerClassName(tree, *ch, diag);   // file scope: member_of = -1
            if (ch->is_reopen) reopen_classes.push_back(ch.get());
            else all_classes.push_back(ch.get());   // re-open owns no body
            if (ch->resolved_entry_id >= 0)
                registerScopeNames(tree, *ch,
                    tree.entries[ch->resolved_entry_id].ns_frame_id, all_classes, diag);
        }
    }

    // Pass 1a-types — the global TYPES phase: every name now exists, so one recursive
    // resolveScopeTypes per file-scope namespace/class resolves EVERY declared type —
    // member alias targets, class FIELD types (attach slots), and const / function /
    // method SIGNATURE types — recursing with frames open. Deferring all type
    // resolution out of registration is what lets a member type name ANY class
    // regardless of pass order (the forward-ref bugs). File-scope function entries
    // resolve in their own pass below (already after classes).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef)
            resolveScopeTypes(tree, *ch, /*isClass=*/true, diag);
        else if (ch && ch->kind == parse::Kind::kNamespaceDecl)
            resolveScopeTypes(tree, *ch, /*isClass=*/false, diag);
    }

    // Pass 1a-layout — mark the classes deriving from an opaque base BEFORE the by-value
    // walk below, which asks the flag to tell that legal embedding from an illegal one.
    propagateRuntimeLayout(tree, all_classes, diag);

    // Pass 1a-cycle — reject infinite-size by-value cycles now that every field type
    // is resolved + slotted (the transitive needs-fixpoint runs below over tree.classes).
    for (parse::Node* c : all_classes) checkClassByValueAcyclic(tree, *c, diag);

    // Pass 1a-alias-validate — now that enums, namespaces, and classes exist,
    // validate each alias target (an alias may name any of them). An alias
    // TEMPLATE builds (and thereby checks) its PATTERN instead — the node's
    // own target stays pristine.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl
            && ch->return_type != widen::kNoType && !isFuncAlias(tree, *ch)) {
            if (!ch->type_params.empty()) {
                validateAliasTemplate(tree, *ch, diag);
                continue;
            }
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        }
    }

    // Transitive lifecycle: a class needs a ctor/dtor if a by-value field's class
    // does (its hooks must run when the container is built / torn down). Fixpoint
    // over the field graph, then publish to the kSlid type flags codegen reads.
    {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& [cname, info] : tree.classes) {
                for (widen::TypeRef ft : info.field_types) {
                    // A by-value field that IS a hook class, or an array / tuple
                    // OF one, makes its container need the matching hook.
                    if (!info.needs_ctor && fieldContributesNeed(tree, ft, true)) {
                        info.needs_ctor = true; changed = true;
                    }
                    if (!info.needs_dtor && fieldContributesNeed(tree, ft, false)) {
                        info.needs_dtor = true; changed = true;
                    }
                }
            }
        }
        for (auto& [ctype, info] : tree.classes) {
            widen::setSlidNeeds(ctype, info.needs_ctor, info.needs_dtor);
        }
    }
    // Virtual-class rules (base-must-be-virtual, dtor-must-be-virtual, override + re-open
    // rules) — method signatures are resolved by now.
    for (parse::Node* c : all_classes) validateVirtualClass(tree, *c, diag);
    for (parse::Node* c : reopen_classes) validateVirtualClass(tree, *c, diag);

    // Pass 1a — collect entries at program scope WITHOUT walking init
    // expressions. This lets globals reference each other regardless of
    // declaration order (forward-decl semantics).
    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kFunctionDef
         || ch->kind == parse::Kind::kFunctionDecl) {
            // A file-scope TEMPLATE: register + resolve its pattern signature (every
            // class/alias name exists by this pass); the body-resolution pass below
            // takes its scope snapshot instead of resolving the body.
            if (!ch->type_params.empty()) {
                registerTemplateFunction(tree, *ch, &program->children,
                                         /*nested=*/false, /*owner=*/-1, diag);
                if (ch->resolved_entry_id >= 0)
                    resolveTemplatePatterns(tree, *ch, ch->resolved_entry_id, diag);
                continue;
            }
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            std::vector<widen::TypeRef> param_types;
            int num_required = 0;
            bool seen_default = false;
            for (auto& p : ch->params) {
                if (!p) continue;
                bool has_default = !p->children.empty();
                if (p->return_type == widen::kNoType && !has_default) {
                    diagnostic::report(diag, {p->file_id, p->name_tok,
                        "Parameter '" + p->name
                            + "' needs an explicit type or a default value.", {}});
                }
                if (has_default) {
                    seen_default = true;
                } else {
                    if (seen_default) {
                        diagnostic::report(diag, {p->file_id, p->name_tok,
                            "A required parameter cannot follow an optional "
                            "parameter.", {}});
                    }
                    num_required++;
                }
                if (p->return_type != widen::kNoType) {
                    std::string declared = widen::spellOrEmpty(p->return_type);   // pre-erasure
                    resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
                    if (declared != widen::spellOrEmpty(p->return_type)) p->alias_label = declared;
                }
                param_types.push_back(p->return_type);
            }
            bool is_def = (ch->kind == parse::Kind::kFunctionDef);
            // Overloading: a same-name function with a DIFFERENT parameter list is
            // a new overload. We match an earlier entry only when the parameter
            // types are identical (a redeclaration / the definition of a forward
            // decl). Two same-parameter entries with different RETURN types are
            // not overloadable — an error.
            int existing = -1;
            for (std::size_t idx = 0; idx < tree.entries.size(); idx++) {
                parse::Entry const& pe = tree.entries[idx];
                if (pe.kind == parse::EntryKind::kFunction && pe.owner_ns_frame < 0
                    && pe.name == ch->name && pe.param_types == param_types) {
                    existing = (int)idx;
                    break;
                }
            }
            if (existing >= 0) {
                parse::Entry& prev = tree.entries[existing];
                if (prev.slids_type != ch->return_type) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Return type '" + widen::spellOrEmpty(ch->return_type)
                        + "' does not match earlier declaration's '"
                        + widen::spellOrEmpty(prev.slids_type) + "'.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                    continue;
                }
                if (is_def && prev.defined) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Duplicate definition of '" + ch->name + "'.",
                        {{prev.def_file_id, prev.def_tok, "first defined here"}}});
                    continue;
                }
                if (is_def) {
                    prev.defined = true;
                    prev.num_required = num_required;
                    prev.def_file_id = ch->file_id;
                    prev.def_tok = ch->name_tok;
                }
                ch->resolved_entry_id = existing;
                continue;
            }
            // A TEMPLATE with this name owns it — a plain function is a collision,
            // not an overload.
            if (int tmpl = findSameScopeTemplate(tree, ch->name, /*owner=*/-1);
                tmpl >= 0) {
                reportTemplateNameClash(diag, *ch, tree.entries[tmpl]);
                continue;
            }
            // No matching-signature function. A same-name NON-function entry (a
            // class / alias / enum / namespace / const) is a collision, not an
            // overload — find one and report.
            int clash = -1;
            for (int id : tree.live_entry_ids) {
                parse::Entry const& pe = tree.entries[id];
                if (pe.name == ch->name && pe.owner_ns_frame < 0
                    && pe.parent_frame_id == parse::currentFrameId(tree)
                    && pe.kind != parse::EntryKind::kFunction) {
                    clash = id;
                    break;
                }
            }
            if (clash >= 0) {
                parse::Entry const& prev = tree.entries[clash];
                reportNameCollision(diag, "Duplicate declaration of '" + ch->name + "'.",
                                    prev.file_id, prev.tok, ch->file_id, ch->name_tok);
                continue;
            }
            parse::Entry e;
            e.kind = parse::EntryKind::kFunction;
            e.name = ch->name;
            e.slids_type = ch->return_type;
            e.param_types = std::move(param_types);
            e.num_required = num_required;
            e.file_id = ch->file_id;
            e.tok = ch->name_tok;
            e.defined = is_def;
            e.is_external = !is_def && fileIsImported(tree, ch->file_id);
            e.is_foreign = ch->is_foreign;
            if (is_def) {
                e.def_file_id = ch->file_id;
                e.def_tok = ch->name_tok;
            }
            ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
            continue;
        }
        if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const) {
            // A qualified file-scope const (`const int C:k = 7;`) was relocated into
            // its target class by relocateOutOfLineMembers, so only a local const is
            // here.
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            // A non-scalar const at file scope is a not-mutable GLOBAL (allocated,
            // not substituted). Globals now have storage (the compound-lazy path,
            // desugar.cpp:2489), but the const path has not been routed through it
            // yet, so this stays a diagnostic — see todo.txt FEATURES.
            if (constNeedsStorage(ch->return_type)) {
                diagnostic::report(diag, {ch->file_id, ch->name_tok,
                    "A const variable of a non-scalar type (array, tuple, class, or "
                    "pointer) requires global storage, which is not yet supported.", {}});
            }
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
            if (existing >= 0) {
                parse::Entry const& prev = tree.entries[existing];
                reportNameCollision(diag, "Duplicate declaration of '" + ch->name + "'.",
                                    prev.file_id, prev.tok, ch->file_id, ch->name_tok);
                continue;
            }
            ch->resolved_entry_id = addVarEntry(tree, parse::EntryKind::kConst, *ch);
            continue;
        }
        if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_global) {
            // A file-scope GLOBAL — mutable static storage in the unnamed global
            // namespace (reached bare or via `::`). Register a kGlobalVar in the
            // global frame (owner_ns_frame < 0 so it resolves bare); constfold
            // folds its init, codegen emits the `internal @`-global.
            if (ch->return_type != widen::kNoType)
                resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
            if (existing >= 0) {
                // A header-declared global MERGES with its definition here (decl+def).
                int merged = tryMergeGlobalDecl(tree, existing, *ch);
                if (merged >= 0) { ch->resolved_entry_id = merged; continue; }
                parse::Entry const& prev = tree.entries[existing];
                reportNameCollision(diag, "Duplicate declaration of '" + ch->name + "'.",
                                    prev.file_id, prev.tok, ch->file_id, ch->name_tok);
                continue;
            }
            // kNoType stays as-is when inferred — classify fills it.
            ch->resolved_entry_id = addVarEntry(tree, parse::EntryKind::kGlobalVar, *ch);
            continue;
        }
        if (ch->kind == parse::Kind::kAliasDecl) continue;     // handled above
        if (ch->kind == parse::Kind::kNamespaceDecl) continue; // handled above
        if (ch->kind == parse::Kind::kClassDef) continue;      // handled above
        if (ch->kind == parse::Kind::kEnumDecl) continue;      // handled above
        // Other top-level shapes not supported today. Grammar rejects them; if one
        // slips through, it's a grammar bug.
        diagnostic::report(diag, {ch->file_id, ch->tok,
            "Only function definitions, function declarations, "
            "constants, and globals are allowed at file scope.", {}});
    }

    // Pass 1a-import — file-scope bare `alias Ns;`. Opens the namespace for the
    // whole file: pushed onto open_ns_frames and never popped within run() (file
    // scope = the whole file), so all const inits and function bodies below see
    // its members unqualified.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl && ch->return_type == widen::kNoType) {
            resolveBareAlias(tree, *ch, diag);
        }
    }

    // Pass 1b-enum — resolve file-scope enum member init expressions now that
    // every program-scope entry is registered, so a member init can reference a
    // file-scope const (registration happened early in Pass 1a-enum).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kEnumDecl) {
            resolveEnumMemberInits(tree, *ch, diag);
        }
    }

    // Pass 1a-func-alias — every function (namespace AND file-scope) is now registered
    // with a RESOLVED signature, so duplicate each function alias's target overloads
    // under the alias name; they join the overload set for the passes below.
    processFuncAliases(tree, diag);

    // Pass 1a-class-tmpl-snapshot — every file-scope entry is registered and the
    // file-level namespace imports are open, so capture each CLASS template's
    // definition-point visibility now, BEFORE the first expression/body walks
    // (Pass 1b / Pass 2) can demand an instance. A namespace member's body
    // resolves with its owner chain open — reconstruct and push that chain
    // around the capture. (A block-scope template snapshots at its own site;
    // resolveScopeBodies refreshes a member's snapshot with the natural chain.)
    for (auto& [tid, ti] : tree.templates) {
        if (!ti.def || ti.def->kind != parse::Kind::kClassDef) continue;
        if (ti.snapshot_taken) continue;
        std::vector<int> chain;
        for (int owner = tree.entries[tid].owner_ns_frame; owner >= 0; ) {
            chain.push_back(owner);
            int oid = -1;
            for (std::size_t i = 0; i < tree.entries.size(); i++)
                if (tree.entries[i].ns_frame_id == owner) { oid = (int)i; break; }
            owner = oid >= 0 ? tree.entries[oid].owner_ns_frame : -1;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            tree.open_ns_frames.push_back(*it);
        snapshotTemplate(tree, *ti.def);
        for (std::size_t i = 0; i < chain.size(); i++)
            tree.open_ns_frames.pop_back();
    }

    // Pass 1b — walk each top-level const's / global's init expression to resolve
    // ident references. By now every program-scope entry is in the table,
    // so forward refs between globals resolve cleanly.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kVarDeclStmt
            || !(ch->is_const || ch->is_global)) continue;
        if (isQualified(*ch)) continue;   // inline member init resolved above
        for (auto& init : ch->children) {
            if (init) resolveExpr(tree, *init, diag);
        }
    }

    // Pass 1b-alias-dims — resolve a const-expression array dim in a file-scope
    // alias TARGET (`alias V = int[N]`) now every program-scope const exists.
    // constfold folds + bakes it and refreshes the alias's eager uses.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl) {
            for (auto& d : ch->dim_exprs) {
                if (d) resolveExpr(tree, *d, diag);
            }
        }
    }

    // (Default copy/move/swap ops are synthesized inside resolveScopeBodies — the single
    // choke point every class-body pass funnels through — so file-scope AND local classes
    // are covered by one mechanism; no per-driver synthesis pass here.)

    // Pass 2 — walk each top-level function body. A TEMPLATE's body stays pristine;
    // this is where its scope snapshot is taken (every file-scope entry is registered
    // by now, so the snapshot has the same forward-ref visibility a real body would).
    for (auto& ch : program->children) {
        if (!ch) continue;
        // A header's BODYLESS template declaration still snapshots (a consumer
        // instantiates a declaration-only instance from it — the signature
        // resolution needs the same visibility).
        if (ch->kind == parse::Kind::kFunctionDecl && !ch->type_params.empty()) {
            if (tree.templates.count(ch->resolved_entry_id))
                snapshotTemplate(tree, *ch);
            continue;
        }
        if (ch->kind != parse::Kind::kFunctionDef) continue;
        if (!ch->type_params.empty()) { snapshotTemplate(tree, *ch); continue; }
        resolveFunctionBody(tree, *ch, diag);
    }

    // Pass 2-global-region — the `global;` scope rules over `main`. If main opens the
    // scope itself, access before/after it (and a second `global;`) is an error; if it
    // does not, the scope is auto-inserted over all of main, so everything is active.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kFunctionDef && ch->name == "main") {
            // Check main's CHILDREN for an explicit `global;` (main itself is a
            // kFunctionDef, which subtreeOpensGlobalScope treats as a scope boundary).
            bool opens = false;
            for (auto& c : ch->children)
                if (c && subtreeOpensGlobalScope(*c)) { opens = true; break; }
            checkGlobalRegion(tree, *ch, /*active=*/!opens, diag);
        }
    }

    // Pass 2-scope — the BODY phase for every file-scope namespace and class:
    // field-default exprs, member const/enum inits, and member function bodies
    // (methods/ctor/dtor or free functions), recursing into nested scopes — a
    // class in a namespace, a namespace in a class — through the ONE
    // resolveScopeBodies routine. Every entry/type exists by now (registration +
    // the global two-phase + the lifecycle fixpoint all ran), so this folds in the
    // former 1b member-init / field-default passes: order within the body phase is
    // free.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef) {
            if (!ch->type_params.empty()) continue;   // a pattern has no body phase
            resolveScopeBodies(tree, *ch, /*isClass=*/true, diag);
        } else if (ch && ch->kind == parse::Kind::kNamespaceDecl) {
            resolveScopeBodies(tree, *ch, /*isClass=*/false, diag);
        }
    }

    // --instantiate demands (this TU is the template source): the CLASS
    // flavors instantiate here — a demanded spelling rides resolveDeclType
    // exactly like a local use, memoized against any use this file already
    // made. Function / method flavors wait for classify, where those
    // instances mint. Runs BEFORE the drain so demanded bodies drain with
    // everything else.
    for (auto& d : tree.inst_demands) {
        widen::TypeRef t = widen::intern(d.spelling);
        if (widen::form(t) != widen::Type::Form::kTmplUse) {
            diagnostic::report(diag, {-1, -1,
                "Bad instantiation demand '" + d.spelling + "'.", {}});
            d.consumed = true;
            continue;
        }
        std::string name = widen::get(t).name;
        bool reported = false;
        int id = lookupAliasTemplateEntry(tree, name, -1, -1, diag, reported);
        if (id >= 0 && tree.entries[id].kind == parse::EntryKind::kClass
            && tree.entries[id].is_template) {
            resolveDeclType(tree, t, -1, -1, diag);
            d.consumed = true;
        }
        // else: a function / method template spelling — classify's turn — or
        // an unknown name, reported as a leftover there.
    }

    // Pass 2-tmpl-class — the deferred BODY phase for every class-template
    // instance minted during resolve: every file-scope body has resolved and
    // every snapshot is taken, so re-enter each instance under its template's
    // snapshot (T re-bound). A body may demand further instances — the drain
    // loops to empty — then every instance node splices into its host list
    // after its template, where the remaining passes (munge below, then
    // constfold/classify/desugar/codegen) walk it like any class.
    drainClassTemplateBodies(tree, diag);

    // A class template whose `...` was never closed — used or not — errors: a
    // plain class left open completes in another TU via its header, but
    // cross-TU templates have not landed. (A USED open template reported at
    // its use; open_reported keeps this to one diagnostic.)
    for (auto& [tid, ti] : tree.templates) {
        if (!ti.def || ti.def->kind != parse::Kind::kClassDef) continue;
        if (ti.cls_open && !ti.open_reported) {
            ti.open_reported = true;
            diagnostic::report(diag, {ti.def->file_id, ti.def->name_tok,
                "Class template '" + ti.def->name + "' is declared incomplete "
                "('...') and never completed; cross-TU completion of a template "
                "is not supported yet — close it with a re-open that omits the "
                "'...'.", {}});
        }
    }
    tree.resolve_done = true;

    // Pass 3 — orphan declarations. A function declared but never defined
    // (anywhere, used or not) is a compile error: a call to it would emit a
    // `declare` with no `define` and llc would reject the IR. Caret at the
    // declaration's name. (Cross-TU `.slh` headers legitimately declare-only —
    // their entries are flagged is_external at registration and skipped below.)
    for (parse::Entry const& e : tree.entries) {
        // A PURE virtual (`= delete`) is intentionally bodyless — not an orphan.
        // An EXTERNAL declaration (from an imported `.slh`) is defined in another
        // translation unit and linked in — also not an orphan here. A FOREIGN import
        // (`= import`) binds a C library symbol at link — likewise (stage 1 gates it to
        // file / namespace scope at PARSE, so no block/class foreign entry reaches here).
        if (e.kind == parse::EntryKind::kFunction && !e.defined && !e.is_pure
            && !e.is_external && !e.is_foreign) {
            // A forward declaration is SATISFIED by a same-signature DEFINITION in
            // the same scope. A free function merges its decl + def into ONE entry
            // (Pass 1a, matched by signature); a class METHOD's decl + def stay
            // SEPARATE entries (registered before types resolve), so match them here
            // — parity with bare functions ("a method is a function").
            bool defined_elsewhere = false;
            for (parse::Entry const& q : tree.entries) {
                // Same EXACT scope: owner_ns_frame (class/namespace) AND
                // parent_frame_id (a nested function's host body) — else a nested
                // `bar` in one host would wrongly satisfy a decl in another.
                if (&q != &e && q.kind == parse::EntryKind::kFunction && q.defined
                    && q.name == e.name && q.owner_ns_frame == e.owner_ns_frame
                    && q.parent_frame_id == e.parent_frame_id
                    && q.param_types == e.param_types) {
                    defined_elsewhere = true;
                    break;
                }
            }
            if (defined_elsewhere) continue;
            diagnostic::report(diag, {e.file_id, e.tok,
                "Function '" + e.name + "' is declared but never defined.", {}});
        }
    }

    // mungeParamTypes — non-primitive params must be pointers or arrays; a tuple /
    // class VALUE param is rejected (param types are now resolved).
    for (auto& ch : program->children) {
        if (ch) mungeParamTypes(tree, *ch, diag);
    }

    // A variable (local or global) whose type EMBEDS an imported opaque class by value in
    // an aggregate — `String a[3]`, `(String, int) t` — needs a static size/stride this TU
    // does not have. A bare `String h` LOCAL is fine (Slice 2 sizes it at runtime); a class
    // FIELD is caught at the class definition (checkClassByValueAcyclic). A GLOBAL is the
    // exception: its storage is STATIC (sized at compile time), so the runtime-alloca trick
    // has no analogue — even a BARE opaque global is illegal. Runs last: every class's
    // opaque flag and every variable type is final.
    for (parse::Entry const& e : tree.entries) {
        if (e.kind != parse::EntryKind::kLocalVar
            && e.kind != parse::EntryKind::kGlobalVar)
            continue;
        widen::TypeRef s = widen::strip(e.slids_type);
        if (e.kind == parse::EntryKind::kGlobalVar && isImportOpaque(s)) {
            auto oit = tree.classes.find(s);
            std::string oname = oit != tree.classes.end() ? oit->second.name : "?";
            diagnostic::report(diag, {e.file_id, e.tok,
                "Global '" + e.name + "' has imported incomplete class type '" + oname
                + "'; its size is unknown here and a global needs static storage — "
                "use a reference '" + oname + "^'.", {}});
            continue;
        }
        std::set<widen::TypeRef> seen;
        widen::TypeRef op = aggregateEmbedsOpaque(e.slids_type, seen);
        if (op == widen::kNoType) continue;
        auto oit = tree.classes.find(op);
        std::string oname = oit != tree.classes.end() ? oit->second.name : "?";
        std::string what = e.kind == parse::EntryKind::kGlobalVar ? "Global" : "Variable";
        diagnostic::report(diag, {e.file_id, e.tok,
            what + " '" + e.name + "' embeds imported incomplete class '" + oname
            + "' by value; its layout is private to the module that completes it — "
            "use a reference '" + oname + "^'.", {}});
    }

    parse::popFrame(tree);
}

}  // namespace resolve
