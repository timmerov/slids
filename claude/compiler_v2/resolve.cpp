#include "resolve.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic.h"
#include "parse.h"
#include "widen.h"

namespace resolve {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

// A fixed-size array type spelling (`int[5]`, `int[3][5]`), distinct from
// `int[]` (iterator) and `int^` (reference).
bool isArrayType(widen::TypeRef t) {
    return widen::form(t) == widen::Type::Form::kArray;
}

bool isReferenceType(widen::TypeRef t) {
    return widen::form(t) == widen::Type::Form::kPointer;
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
        if (ty.form == F::kAlias) { t = ty.underlying; continue; }
        if (ty.form == F::kPointer || ty.form == F::kIterator) { t = ty.pointee; continue; }
        if (ty.form == F::kArray) { t = ty.elem; continue; }
        if (ty.form == F::kSlid) return tree.classes.count(t) > 0;
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
            if (ty.form == F::kAlias) { leaf = ty.underlying; continue; }
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

// Split a type spelling into its resolvable leaf NAME and the modifier SUFFIX
// (`^` / `[]` / `[N]...`) that rides back along once the leaf resolves —
// structurally, off the interned type, not by slicing the spelling. Mirrors the
// legacy peel exactly: a run of OUTER iterator/array wrappers, then AT MOST ONE
// reference; anything further in stays part of the base (so `int[3]^` -> base
// `int[3]`, suffix `^`, which then resolves to itself).
std::pair<std::string, std::string> splitTypeModifiers(std::string const& spelling) {
    std::vector<widen::TypeRef> chain;   // outermost wrapper first
    widen::TypeRef cur = widen::intern(spelling);
    for (;;) {
        widen::Type const& t = widen::get(cur);
        if (t.form == widen::Type::Form::kIterator) { chain.push_back(cur); cur = t.pointee; }
        else if (t.form == widen::Type::Form::kArray) { chain.push_back(cur); cur = t.elem; }
        else break;
    }
    if (widen::form(cur) == widen::Type::Form::kPointer) {
        chain.push_back(cur);
        cur = widen::get(cur).pointee;
    }
    std::string base = widen::spell(cur);
    std::string suffix;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        widen::Type const& t = widen::get(*it);
        if (t.form == widen::Type::Form::kPointer)       suffix += "^";
        else if (t.form == widen::Type::Form::kIterator) suffix += "[]";
        else for (int d : t.dims) suffix += "[" + std::to_string(d) + "]";
    }
    return {base, suffix};
}

// Substitute a type-alias spelling to its underlying type, following chains
// (`alias A = B; alias B = int` → `int`) and detecting cycles. A namespace-
// qualified spelling (`Space:Dir`) resolves through the namespace chain. The
// `[]` suffix rides along. A spelling that isn't an alias / enum / qualified
// type is returned unchanged. `reported` is set when a diagnostic was already
// emitted (a cycle, or a failed qualified resolution) so the caller skips the
// redundant "Unknown type".
std::string resolveTypeSpelling(parse::Tree& tree, std::string const& spelling,
                                std::set<std::string>& visiting, bool& reported,
                                int file_id, int tok, diagnostic::Sink& diag,
                                std::vector<int> const* seg_toks = nullptr) {
    // Split off the modifier suffix so the base type name resolves; the suffix
    // (`[]` / `[N]` / `^`) rides back along on the resolved base.
    auto bs = splitTypeModifiers(spelling);
    std::string& base = bs.first;
    std::string& suffix = bs.second;
    if (base.find(':') != std::string::npos) {
        std::string under =
            resolveQualifiedType(tree, base, file_id, tok, reported, diag, seg_toks);
        if (under.empty()) return base + suffix;   // error already reported
        return under + suffix;
    }
    int id = parse::findInLiveScopes(tree, base);
    // A type alias substitutes to its target. An enum's name doubles as a
    // namespace AND a transparent type alias to the underlying (carried on the
    // namespace entry's slids_type); treat that like an alias here. A plain
    // namespace (empty slids_type) is not a type and falls through unchanged →
    // requireKnownType then rejects it.
    bool is_alias = id >= 0 && tree.entries[id].kind == parse::EntryKind::kAlias;
    bool is_enum_type = id >= 0
        && tree.entries[id].kind == parse::EntryKind::kNamespace
        && tree.entries[id].slids_type != widen::kNoType;
    if (!is_alias && !is_enum_type) {
        return spelling;
    }
    if (is_enum_type) {
        // The underlying is already a primitive spelling (validated at decl);
        // no further chain to chase.
        return widen::spell(tree.entries[id].slids_type) + suffix;
    }
    if (visiting.count(base)) {
        reported = true;
        diagnostic::report(diag, {file_id, tok,
            "Type alias '" + base + "' is part of a cycle.", {}});
        return base;
    }
    visiting.insert(base);
    std::string resolved = resolveTypeSpelling(
        tree, widen::spell(tree.entries[id].slids_type), visiting, reported, file_id, tok, diag);
    visiting.erase(base);
    return resolved + suffix;
}

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
        case F::kSlid: {
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
            // SCOPE-AWARE: resolveName (open-ns chain + lexical-with-owner<0), NOT
            // findInLiveScopes (any live entry). A namespaced member type (a host
            // class's alias/enum, a hoisted class) resolves bare ONLY where its
            // frame is open — inside the host (member types + bodies open it), so
            // `Inner`/`Innerger` at file scope are out of scope and fail.
            int id = resolveName(tree, name);
            // A class name resolves to its registered kSlid handle. The handle
            // carries the def_id (scope distinction) and the layout; the bare
            // written-name kSlid `t` is just a placeholder. Always redirect so
            // every reference — file-scope or local — shares the one real handle.
            if (id >= 0 && tree.entries[id].kind == parse::EntryKind::kClass) {
                return tree.entries[id].slids_type;
            }
            bool is_alias = id >= 0 && tree.entries[id].kind == parse::EntryKind::kAlias;
            bool is_enum  = id >= 0
                && tree.entries[id].kind == parse::EntryKind::kNamespace
                && tree.entries[id].slids_type != widen::kNoType;
            if (!is_alias && !is_enum) return t;   // unknown name / real slid — leave
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

// Register `alias Name = Type;` as a kAlias entry in the current frame.
void registerAlias(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int existing = parse::findInFrame(tree, parse::currentFrameId(tree), s.name);
    if (existing >= 0) {
        parse::Entry const& prev = tree.entries[existing];
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Duplicate declaration of '" + s.name + "'.",
            {{prev.file_id, prev.tok, "first declared here"}}});
        return;
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kAlias;
    e.name = s.name;
    e.slids_type = s.return_type;   // target spelling; resolved at use
    e.file_id = s.file_id;
    e.tok = s.name_tok;
    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
}

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag,
                 bool unevaluated = false);
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);

// ---- Namespace lookup --------------------------------------------------------
// The global root is namespace frame 0 (the program frame). Its members are the
// file-scope entries (parent_frame_id == 0, not themselves namespace members).
constexpr int kGlobalFrame = 0;

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
    if (first >= 0) return first;
    for (auto it = tree.live_entry_ids.rbegin();
         it != tree.live_entry_ids.rend(); ++it) {
        parse::Entry const& e = tree.entries[*it];
        if (e.name == name && e.owner_ns_frame < 0) return *it;
    }
    return -1;
}

int resolveName(parse::Tree const& tree, std::string const& name) {
    int other;
    return resolveNameDetail(tree, name, other);
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

// Find a member `name` declared in `ns_frame` among ALL entries (live or not).
// Used to distinguish "declared but not visible" from "no such member".
int findMemberDeclared(parse::Tree const& tree, int ns_frame,
                       std::string const& name) {
    for (std::size_t id = 0; id < tree.entries.size(); ++id) {
        parse::Entry const& e = tree.entries[id];
        if (e.name != name) continue;
        if (ns_frame == kGlobalFrame) {
            if (e.parent_frame_id == kGlobalFrame && e.owner_ns_frame < 0)
                return static_cast<int>(id);
        } else if (e.owner_ns_frame == ns_frame) {
            return static_cast<int>(id);
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

// ---- Namespace registration -------------------------------------------------
void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag, bool nested = false);
void registerLocalClasses(parse::Tree& tree,
                          std::vector<std::unique_ptr<parse::Node>>& stmts,
                          diagnostic::Sink& diag);
void registerNamespaceTree(parse::Tree& tree, parse::Node& node,
                           int parent_ns, diagnostic::Sink& diag);
void resolveNamespaceBodies(parse::Tree& tree, parse::Node& node,
                            int parent_ns, diagnostic::Sink& diag);
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

// Register one namespace member's signature (no body): a const pre-creates its
// entry, a function records its signature, a nested namespace recurses.
void registerMemberSignature(parse::Tree& tree, parse::Node& m, int ns_frame,
                             diagnostic::Sink& diag) {
    if (m.kind == parse::Kind::kNamespaceDecl) {
        registerNamespaceTree(tree, m, ns_frame, diag);
        return;
    }
    if (m.kind == parse::Kind::kEnumDecl) {
        registerEnum(tree, m, ns_frame, diag);
        return;
    }
    if (m.kind == parse::Kind::kVarDeclStmt && m.is_const) {
        resolveDeclType(tree, m.return_type, m.file_id, m.tok, diag);
        if (findMemberDeclared(tree, ns_frame, m.name) >= 0) {
            diagnostic::report(diag, {m.file_id, m.name_tok,
                "Duplicate declaration of '" + m.name + "'.", {}});
            return;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = m.name;
        e.slids_type = m.return_type;
        e.file_id = m.file_id;
        e.tok = m.name_tok;
        e.owner_ns_frame = ns_frame;
        m.resolved_entry_id = parse::addEntry(tree, std::move(e));
        return;
    }
    if (m.kind == parse::Kind::kFunctionDef
     || m.kind == parse::Kind::kFunctionDecl) {
        resolveDeclType(tree, m.return_type, m.file_id, m.tok, diag);
        std::vector<widen::TypeRef> param_types;
        for (auto& p : m.params) {
            if (!p) continue;
            resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
            param_types.push_back(p->return_type);
        }
        if (findMemberDeclared(tree, ns_frame, m.name) >= 0) {
            diagnostic::report(diag, {m.file_id, m.name_tok,
                "Duplicate declaration of '" + m.name + "'.", {}});
            return;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kFunction;
        e.name = m.name;
        e.slids_type = m.return_type;
        e.param_types = std::move(param_types);
        e.file_id = m.file_id;
        e.tok = m.name_tok;
        e.defined = (m.kind == parse::Kind::kFunctionDef);
        if (e.defined) { e.def_file_id = m.file_id; e.def_tok = m.name_tok; }
        e.owner_ns_frame = ns_frame;
        m.resolved_entry_id = parse::addEntry(tree, std::move(e));
        return;
    }
    diagnostic::report(diag, {m.file_id, m.tok,
        "Only constants, functions, enums, and namespaces may appear in a "
        "namespace.", {}});
}

// Register a namespace and all its members' signatures (no bodies). Recurses
// into nested namespaces. Members inherit the current lexical lifetime.
void registerNamespaceTree(parse::Tree& tree, parse::Node& node,
                           int parent_ns, diagnostic::Sink& diag) {
    int ns = openNamespace(tree, node.name, node.name_tok, node.file_id,
                           parent_ns, diag);
    if (ns < 0) return;
    node.resolved_entry_id = ns;   // stash the ns frame for the body pass
    // Register type-introducing members (enums, nested namespaces) first, so a
    // const / function member's type may name a sibling enum regardless of
    // declaration order — mirrors the file-scope pass ordering. The frame is OPEN
    // while member TYPES resolve, so a sibling type resolves bare via the scope
    // frame stack (resolveName), not the findInLiveScopes leniency.
    tree.open_ns_frames.push_back(ns);
    for (auto& m : node.children) {
        if (m && (m->kind == parse::Kind::kEnumDecl
                  || m->kind == parse::Kind::kNamespaceDecl)) {
            registerMemberSignature(tree, *m, ns, diag);
        }
    }
    for (auto& m : node.children) {
        if (m && m->kind != parse::Kind::kEnumDecl
              && m->kind != parse::Kind::kNamespaceDecl) {
            registerMemberSignature(tree, *m, ns, diag);
        }
    }
    tree.open_ns_frames.pop_back();
}

// Resolve member bodies of an already-registered namespace: const inits,
// function bodies (which see the open-namespace chain), and nested namespaces.
void resolveNamespaceBodies(parse::Tree& tree, parse::Node& node,
                            int parent_ns, diagnostic::Sink& diag) {
    (void)parent_ns;
    int ns = node.resolved_entry_id;   // stashed by registerNamespaceTree
    if (ns < 0) return;   // registration already diagnosed any problem
    tree.open_ns_frames.push_back(ns);
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kNamespaceDecl) {
            resolveNamespaceBodies(tree, *m, ns, diag);
        } else if (m->kind == parse::Kind::kEnumDecl) {
            // The namespace frame is open; resolveEnumMemberInits opens the
            // enum's own frame on top, so member inits see siblings + the
            // enclosing namespace's members.
            resolveEnumMemberInits(tree, *m, diag);
        } else if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            for (auto& init : m->children) {
                if (init) resolveExpr(tree, *init, diag);
            }
        } else if (m->kind == parse::Kind::kFunctionDef) {
            resolveFunctionBody(tree, *m, diag);
        }
    }
    tree.open_ns_frames.pop_back();
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

// An inline qualified member declaration: `const int Space:kSix = 6;` defines a
// const member of an existing namespace by qualified name. Registers the entry
// (lifetime = current lexical frame) and resolves its init.
void resolveInlineQualifiedDecl(parse::Tree& tree, parse::Node& s,
                                diagnostic::Sink& diag) {
    if (!s.is_const) {
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Only constant members may be defined by qualified name.", {}});
        return;
    }
    int frame = resolveNamespaceSegments(tree, s.qualifier, s.qualifier_toks,
                                         s.global_qualified, s.file_id, diag);
    if (frame < 0) return;
    resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
    if (findMemberDeclared(tree, frame, s.name) >= 0) {
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Duplicate declaration of '" + s.name + "'.", {}});
        return;
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kConst;
    e.name = s.name;
    e.slids_type = s.return_type;
    e.file_id = s.file_id;
    e.tok = s.name_tok;
    e.owner_ns_frame = frame;
    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
    for (auto& init : s.children) {
        if (init) resolveExpr(tree, *init, diag);
    }
}

// ---- Enums ------------------------------------------------------------------
// An enum lowers (at resolve, not desugar — members must be kConst by constfold
// so `Enum:member` folds) to: a namespace of the members (named enum), which
// doubles as a transparent type alias to the underlying; or bare consts in the
// enclosing scope (anonymous enum). Member values auto-increment by 1 (int) or
// 1.0 (float) from 0, with an explicit literal resetting the running counter
// (C rules).

bool isFloatUnderlying(std::string const& t) {
    return t == "float" || t == "float32" || t == "float64";
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
    bool is_float = isFloatUnderlying(widen::spellOrEmpty(node.return_type));
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

        bool dup = (ns_frame >= 0)
            ? (findMemberDeclared(tree, ns_frame, m->name) >= 0)
            : (parse::findInFrame(tree, parse::currentFrameId(tree), m->name) >= 0);
        if (dup) {
            diagnostic::report(diag, {m->file_id, m->name_tok,
                "Duplicate declaration of '" + m->name + "'.", {}});
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

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag,
                 bool unevaluated) {
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
                if (id < 0) {
                    // Inside a ctor/dtor body, a bare name matching a class field
                    // is `self^.field`. Locals shadow fields (we only reach here
                    // when no local/const/etc. resolved), so rewrite this node to
                    // a kFieldExpr over `self^` and resolve that.
                    if (tree.method_fields) {
                        bool is_field = false;
                        for (auto const& f : *tree.method_fields)
                            if (f == e.name) { is_field = true; break; }
                        if (is_field) {
                            auto self_id = std::make_unique<parse::Node>();
                            self_id->kind = parse::Kind::kIdentExpr;
                            self_id->name = "self";
                            self_id->file_id = e.file_id;
                            self_id->tok = e.tok;
                            auto deref = std::make_unique<parse::Node>();
                            deref->kind = parse::Kind::kDerefExpr;
                            deref->file_id = e.file_id;
                            deref->tok = e.tok;
                            deref->children.push_back(std::move(self_id));
                            e.kind = parse::Kind::kFieldExpr;   // e.name stays the field
                            e.children.clear();
                            e.children.push_back(std::move(deref));
                            resolveExpr(tree, *e.children[0], diag, unevaluated);
                            return;
                        }
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
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a namespace, not a value.", {}});
                return;
            }
            if (tree.entries[id].kind == parse::EntryKind::kClass) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a type, not a value.", {}});
                return;
            }
            // Definite-assignment: reading a local before it is written is a
            // compile error (a value-before-init footgun). Only kLocalVar is
            // tracked — consts are substituted away, params are pre-seeded.
            if (tree.entries[id].kind == parse::EntryKind::kLocalVar
                && !isCaptureLocal(tree, id)) {
                // Arrays use the monotonic may-set (some prior write); scalars
                // use the strict must-set.
                bool array = isArrayType(tree.entries[id].slids_type);
                bool ok = array ? tree.assigned_arrays.count(id) > 0
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
            // Operand must be an assignable variable. Phase 1: a bare ident
            // resolving to a LocalVar (no field / index lvalues yet).
            parse::Node& operand = *e.children[0];
            if (operand.kind != parse::Kind::kIdentExpr) {
                diagnostic::report(diag, {e.file_id, e.tok,
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
            if (entry.kind != parse::EntryKind::kLocalVar) {
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
        case parse::Kind::kCastExpr: {
            // `<Type^> operand` — resolve the operand as a read, then substitute
            // and validate the target type spelling (alias chains, void[] reject,
            // unknown-type). classify enforces the cast legality rules.
            if (e.children[0]) resolveExpr(tree, *e.children[0], diag, unevaluated);
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
                if (is_type) {
                    std::string spelling = operand.global_qualified ? "::" : "";
                    for (auto const& seg : operand.qualifier) spelling += seg + ":";
                    spelling += operand.name;
                    std::set<std::string> visiting;
                    bool reported = false;
                    e.return_type = widen::internOrNone(resolveTypeSpelling(tree, spelling, visiting,
                        reported, operand.file_id, operand.tok, diag));
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
                    // Reconstruct the (possibly qualified) spelling for the chain /
                    // namespace type resolver.
                    std::string spelling = operand.global_qualified ? "::" : "";
                    for (auto const& seg : operand.qualifier) spelling += seg + ":";
                    spelling += operand.name;
                    std::set<std::string> visiting;
                    bool reported = false;
                    e.return_type = widen::internOrNone(resolveTypeSpelling(tree, spelling, visiting,
                        reported, operand.file_id, operand.tok, diag));
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
    // Allowlist: only a local variable (incl. a param) is an assignable lvalue.
    // Every other kind rejects — an explicit arm per kind, no silent default
    // (a fall-through used to set resolved_entry_id and crash codegen with no
    // alloca). "variable" is unreachable here (kLocalVar is the accepted case);
    // it mirrors resolveCallTarget's `what` so the wording stays identical.
    if (entry.kind != parse::EntryKind::kLocalVar) {
        char const* what = entry.kind == parse::EntryKind::kAlias ? "type"
                         : entry.kind == parse::EntryKind::kConst ? "constant"
                         : entry.kind == parse::EntryKind::kNamespace ? "namespace"
                         : entry.kind == parse::EntryKind::kFunction ? "function"
                         : "variable";
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Cannot assign to " + std::string(what) + " '" + s.name + "'.",
            {{entry.file_id, entry.tok, std::string(what) + " declared here"}}});
        return false;
    }
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
    if (entry.kind != parse::EntryKind::kFunction) {
        char const* what = entry.kind == parse::EntryKind::kAlias ? "type"
                         : entry.kind == parse::EntryKind::kConst ? "constant"
                         : entry.kind == parse::EntryKind::kNamespace ? "namespace"
                         : "variable";
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "'" + s.name + "' is a " + what + ", not a function.",
            {{entry.file_id, entry.tok, std::string(what) + " declared here"}}});
        return false;
    }
    s.resolved_entry_id = id;
    return true;
}

// User-function call (statement or expression form): resolve the target,
// check arity against the entry's param list, and cache return + param types
// for downstream stages. Then recurse into the argument expressions.
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    if (resolveCallTarget(tree, s, diag)) {
        parse::Entry const& callee = tree.entries[s.resolved_entry_id];
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
    //    be initialized (mirrors the read-before-write check).
    // Either way the array is read every iteration, so it is never unused.
    arr_ref.resolved_entry_id = arr_id;
    if (by_ref) {
        tree.assigned_arrays.insert(arr_id);
    } else if (tree.assigned_arrays.count(arr_id) == 0) {
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
    if (var_decl.return_type != widen::kNoType) {
        // Typed: a fresh per-iteration local (by value or by ref), as declared.
        parse::Entry e;
        e.kind = parse::EntryKind::kLocalVar;
        e.name = var_decl.name;
        e.slids_type = var_decl.return_type;
        e.file_id = vfile; e.tok = vtok;
        var_decl.resolved_entry_id = parse::addEntry(tree, std::move(e));
        tree.body_locals.push_back(var_decl.resolved_entry_id);
    } else {
        // Typeless by value: REUSE an enclosing local (the binding becomes a
        // store — flag it kAssignStmt for desugar) or declare a fresh inferred
        // one typed as the element.
        int existing = resolveName(tree, var_decl.name);
        if (existing >= 0
            && tree.entries[existing].kind == parse::EntryKind::kLocalVar) {
            var_decl.resolved_entry_id = existing;
            var_decl.kind = parse::Kind::kAssignStmt;
        } else {
            parse::Entry e;
            e.kind = parse::EntryKind::kLocalVar;
            e.name = var_decl.name;
            e.slids_type = elemRef;
            e.file_id = vfile; e.tok = vtok;
            var_decl.resolved_entry_id = parse::addEntry(tree, std::move(e));
            tree.body_locals.push_back(var_decl.resolved_entry_id);
        }
    }
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
    // Resolve the iterable: an lvalue (var / `ref^` / index) is read + init-checked
    // + use-marked; a literal's / call's sub-expressions resolve.
    resolveExpr(tree, it_ref, diag);
    // Scope the loop var to the loop; resolve the body with it bound to a slot.
    parse::pushFrame(tree);
    std::vector<int> saved_body_locals = std::move(tree.body_locals);
    tree.body_locals.clear();
    std::set<int> entry_set = tree.initialized_locals;   // S (possibly-zero body)
    if (var_decl.return_type != widen::kNoType) {
        // Typed: a fresh per-iteration local (by value or by ref), as declared.
        parse::Entry e;
        e.kind = parse::EntryKind::kLocalVar;
        e.name = var_decl.name;
        e.slids_type = var_decl.return_type;
        e.file_id = vfile; e.tok = vtok;
        var_decl.resolved_entry_id = parse::addEntry(tree, std::move(e));
        tree.body_locals.push_back(var_decl.resolved_entry_id);
    } else {
        // Typeless by value: REUSE an enclosing local (flag kAssignStmt for
        // desugar) or declare a fresh one whose element type classify infers.
        int existing = resolveName(tree, var_decl.name);
        if (existing >= 0
            && tree.entries[existing].kind == parse::EntryKind::kLocalVar) {
            var_decl.resolved_entry_id = existing;
            var_decl.kind = parse::Kind::kAssignStmt;
        } else {
            parse::Entry e;
            e.kind = parse::EntryKind::kLocalVar;
            e.name = var_decl.name;
            e.slids_type = widen::kNoType;   // classify infers from slot 0
            e.file_id = vfile; e.tok = vtok;
            var_decl.resolved_entry_id = parse::addEntry(tree, std::move(e));
            tree.body_locals.push_back(var_decl.resolved_entry_id);
        }
    }
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
        if (id < 0) {
            if (!isQualified(lv)) {
                diagnostic::report(diag, {lv.file_id, lv.tok,
                    "Cannot assign to undeclared variable '" + lv.name + "'.",
                    {}});
            }
            return;
        }
        parse::Entry const& entry = tree.entries[id];
        if (entry.kind != parse::EntryKind::kLocalVar) {
            diagnostic::report(diag, {lv.file_id, lv.tok,
                "Cannot assign to '" + lv.name + "'.",
                {{entry.file_id, entry.tok, "declared here"}}});
            return;
        }
        lv.resolved_entry_id = id;
        if (isArrayType(entry.slids_type)) {
            // An array element store assigns the array (monotonic may-set).
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
    if (lv.kind == parse::Kind::kIndexExpr || lv.kind == parse::Kind::kDerefExpr) {
        resolveStoreTarget(tree, lv, diag);
        return;
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

Completion resolveStmt(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            // A qualified name defines a namespace member, not a local.
            if (isQualified(s)) {
                resolveInlineQualifiedDecl(tree, s, diag);
                return Completion::Normal;
            }
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
                int existing = parse::findInFrame(tree, parse::currentFrameId(tree), s.name);
                if (existing >= 0) {
                    parse::Entry const& prev = tree.entries[existing];
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "Duplicate declaration of '" + s.name + "'.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                } else {
                    parse::Entry e;
                    e.kind = s.is_const ? parse::EntryKind::kConst
                                        : parse::EntryKind::kLocalVar;
                    e.name = s.name;
                    e.slids_type = s.return_type;
                    // A named type (alias / enum / qualified) erased to a different
                    // underlying — keep the as-declared spelling as the ##type label.
                    if (declared != widen::spellOrEmpty(s.return_type)) e.alias_label = declared;
                    e.file_id = s.file_id;
                    e.tok = s.name_tok;   // caret at the ident, not at 'const'/type
                    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
                    // Track body-declared locals for the unused sweep. Consts
                    // are substituted away and never "unused" in this sense.
                    if (!s.is_const) {
                        tree.body_locals.push_back(s.resolved_entry_id);
                    }
                }
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
                // A class is always constructed on declaration (its fields take
                // default / zero values), so a class-typed decl is definitely
                // initialized even with no explicit initializer.
                bool is_class = widen::form(widen::strip(
                    tree.entries[s.resolved_entry_id].slids_type))
                        == widen::Type::Form::kSlid;
                if (!s.children.empty() || is_class) {
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
            // Inferred-init: a typeless assign to an UNDECLARED bare name declares
            // a fresh local whose type classify infers from the rhs (write-back).
            // A reassign (target already a local) and a wrong-kind target (const /
            // function / ...) both fall through to resolveAssignTarget below.
            if (!isQualified(s) && resolveName(tree, s.name) < 0) {
                parse::Entry e;
                e.kind = parse::EntryKind::kLocalVar;
                e.name = s.name;
                e.slids_type = widen::kNoType;   // classify stamps it from the rhs
                e.file_id = s.file_id;
                e.tok = s.name_tok;
                s.resolved_entry_id = parse::addEntry(tree, std::move(e));
                tree.body_locals.push_back(s.resolved_entry_id);
                s.kind = parse::Kind::kVarDeclStmt;   // alloca + classify infer
                // rhs BEFORE marking initialized, so `x = x` reads x uninitialized.
                for (auto& ch : s.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
                tree.initialized_locals.insert(s.resolved_entry_id);
                return Completion::Normal;
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
            // the write. classify checks copy compatibility.
            if (s.children[1]) resolveExpr(tree, *s.children[1], diag);
            resolveMoveSwapLvalue(tree, *s.children[0], /*read=*/false,
                                  "A move target", diag);
            return Completion::Normal;
        case parse::Kind::kSwapStmt:
            // `a <--> b;` — both operands are READ and WRITTEN (exchanged), so
            // both must already be initialized lvalues. classify checks same-type.
            resolveMoveSwapLvalue(tree, *s.children[0], /*read=*/true,
                                  "A swap operand", diag);
            resolveMoveSwapLvalue(tree, *s.children[1], /*read=*/true,
                                  "A swap operand", diag);
            return Completion::Normal;
        case parse::Kind::kDestructureStmt:
            // (a, b, ) = tuple. children[0] = rhs (read), [1..] = target lvalues
            // (a null child is a skipped slot). Each target is a WRITE (like an
            // assign lhs) — resolve + mark it initialized. rhs read FIRST.
            if (s.children[0]) resolveExpr(tree, *s.children[0], diag);
            for (std::size_t i = 1; i < s.children.size(); i++) {
                if (!s.children[i]) continue;
                if (resolveAssignTarget(tree, *s.children[i], diag)
                    && tree.entries[s.children[i]->resolved_entry_id].kind
                           == parse::EntryKind::kLocalVar) {
                    tree.initialized_locals.insert(s.children[i]->resolved_entry_id);
                }
            }
            return Completion::Normal;
        case parse::Kind::kDeleteStmt: {
            // delete p; — frees p and nulls it. Resolve the operand as a read (you
            // can't delete an uninitialized pointer; the free reads it), then
            // require it be a plain variable lvalue (delete writes null back to
            // it). It stays initialized (now null). classify checks it is a
            // pointer type.
            parse::Node& operand = *s.children[0];
            resolveExpr(tree, operand, diag);
            // An unresolved bare name was already reported by resolveExpr — don't
            // cascade a second "must be a pointer variable" onto it.
            if (operand.kind == parse::Kind::kIdentExpr
                && operand.resolved_entry_id < 0) {
                return Completion::Normal;
            }
            // Otherwise the operand must be a plain variable lvalue (a non-ident
            // expression, or an ident resolving to a const / function / namespace,
            // is rejected). The id is >= 0 here, so the entry access is safe.
            bool is_var = operand.kind == parse::Kind::kIdentExpr
                && tree.entries[operand.resolved_entry_id].kind
                       == parse::EntryKind::kLocalVar;
            if (!is_var) {
                diagnostic::report(diag, {operand.file_id, operand.tok,
                    "The operand of 'delete' must be a pointer variable.", {}});
            }
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
        case parse::Kind::kAliasDecl: {
            // Bare `alias Ns;` imports a namespace's members into this scope.
            if (s.return_type == widen::kNoType) {
                resolveBareAlias(tree, s, diag);
                return Completion::Normal;
            }
            // Function-scope value alias: register in the body frame, then
            // validate the target (forward refs within a body aren't pre-scanned).
            registerAlias(tree, s, diag);
            resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
            return Completion::Normal;
        }
        case parse::Kind::kNamespaceDecl: {
            // A namespace opened in a function body: register its members
            // (lifetime = this body frame) then resolve their bodies. Parent is
            // the global namespace — a function body is not itself a namespace.
            registerNamespaceTree(tree, s, kGlobalFrame, diag);
            resolveNamespaceBodies(tree, s, kGlobalFrame, diag);
            return Completion::Normal;
        }
        case parse::Kind::kEnumDecl: {
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
                // A typeless varlist decl (no explicit type) either REUSES an
                // enclosing local of the same name or declares a fresh inferred
                // local. WITH an initializer, both route through the kAssignStmt
                // path: an in-scope name reassigns it (reuse — no fresh alloca,
                // observable after); an unknown name becomes a fresh inferred-init
                // local (classify types it from the rhs). WITHOUT an initializer
                // there is nothing to infer, so the name must already be a
                // reassignable local — reuse it as the loop var (a no-op slot);
                // otherwise it is an error.
                if (d.kind == parse::Kind::kVarDeclStmt && !d.is_const
                    && d.return_type == widen::kNoType && !isQualified(d)) {
                    if (!d.children.empty()) {
                        d.kind = parse::Kind::kAssignStmt;
                        resolveStmt(tree, d, diag);
                        continue;
                    }
                    int existing = resolveName(tree, d.name);
                    if (existing >= 0) {
                        parse::Entry const& prev = tree.entries[existing];
                        if (prev.kind == parse::EntryKind::kLocalVar) {
                            d.kind = parse::Kind::kBlockStmt;   // no-op reuse slot
                            d.children.clear();
                            continue;
                        }
                        // The name resolves but is not a reassignable local, so it
                        // cannot be reused as a loop variable (mirrors
                        // resolveAssignTarget's wrong-kind wording). The name still
                        // resolves, so the cond/update/body reads don't cascade.
                        char const* what =
                              prev.kind == parse::EntryKind::kAlias     ? "type"
                            : prev.kind == parse::EntryKind::kConst     ? "constant"
                            : prev.kind == parse::EntryKind::kNamespace ? "namespace"
                            : prev.kind == parse::EntryKind::kFunction  ? "function"
                            : "variable";
                        diagnostic::report(diag, {d.file_id, d.name_tok,
                            "Cannot use " + std::string(what) + " '" + d.name
                                + "' as a loop variable.",
                            {{prev.file_id, prev.tok,
                              std::string(what) + " declared here"}}});
                        d.kind = parse::Kind::kBlockStmt;   // neutralize the slot
                        d.children.clear();
                        continue;
                    }
                    // Truly undeclared typeless name with no initializer: nothing
                    // to infer from.
                    diagnostic::report(diag, {d.file_id, d.name_tok,
                        "Cannot infer the type of '" + d.name
                            + "'; it has no initializer.", {}});
                    // Register a placeholder local so the cond/update/body reads
                    // of this name resolve (suppressing cascade "unresolved" /
                    // "uninitialized" errors); main bails after resolve, so the
                    // empty type is never consumed downstream.
                    parse::Entry e;
                    e.kind = parse::EntryKind::kLocalVar;
                    e.name = d.name;
                    e.file_id = d.file_id;
                    e.tok = d.name_tok;
                    d.resolved_entry_id = parse::addEntry(tree, std::move(e));
                    tree.initialized_locals.insert(d.resolved_entry_id);
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
            // name resolved to.
            if (tree.entries[enum_id].kind == parse::EntryKind::kLocalVar
                && isArrayType(tree.entries[enum_id].slids_type)) {
                return understandForArray(tree, s, enum_id, diag);
            }
            if (tree.entries[enum_id].kind == parse::EntryKind::kLocalVar
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
            if (tree.entries[enum_id].kind == parse::EntryKind::kLocalVar
                && tree.entries[enum_id].slids_type == widen::kNoType) {
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
                // NAKED break: the nearest enclosing loop OR switch.
                if (n == 0) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "A 'break' statement must be inside a loop or switch.",
                        {}});
                    return Completion::Abrupt;
                }
                target = n - 1;
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
            // children[0] = scrutinee, [1..] = kCaseClause (label const-expr +
            // body block). Cases fall through: a clause body that completes
            // Normally falls into the next; a break/return/continue ends the run.
            // Each clause body ENTERS from S (any case can be matched directly, so
            // the direct-entry init-set is the weakest and dominates the join).
            assert(!s.children.empty() && "kSwitchStmt needs a scrutinee");
            resolveExpr(tree, *s.children[0], diag);
            std::set<int> entry = tree.initialized_locals;   // S
            tree.loop_stack.push_back({});
            tree.loop_stack.back().is_switch = true;
            bool has_default = false;
            bool last_normal = false;
            std::set<int> bottom_fall;
            for (std::size_t i = 1; i < s.children.size(); i++) {
                parse::Node& clause = *s.children[i];   // kCaseClause
                if (clause.children[0]) {
                    resolveExpr(tree, *clause.children[0], diag);   // label
                } else {
                    has_default = true;
                }
                tree.initialized_locals = entry;                    // enter from S
                Completion c = resolveStmt(tree, *clause.children[1], diag);
                if (i + 1 == s.children.size() && c == Completion::Normal) {
                    last_normal = true;                  // falls out the bottom
                    bottom_fall = tree.initialized_locals;
                }
            }
            parse::Tree::LoopFrame lf = std::move(tree.loop_stack.back());
            tree.loop_stack.pop_back();
            // after = ∩ over the exit paths: each break point, the bottom-fall,
            // and (default-less) the no-match path = S. No exit path (every clause
            // returns/continues with a default) -> Abrupt: control never reaches
            // after the switch.
            bool empty_body = (s.children.size() == 1);
            bool normal_exit = lf.break_seen || last_normal || !has_default
                               || empty_body;
            std::set<int> after;
            bool have = false;
            auto fold = [&](std::set<int> const& set) {
                after = have ? intersectInit(after, set) : set;
                have = true;
            };
            if (lf.break_seen) fold(lf.break_init);
            if (last_normal) fold(bottom_fall);
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
            // to swallow quietly.
            assert(s.resolved_entry_id >= 0
                   && "kClassDef reached resolveStmt unregistered — a body path "
                      "skipped resolveStmtList's local-class pre-pass");
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
    registerLocalClasses(tree, stmts, diag);
    for (std::size_t i = 0; i < stmts.size(); i++) {
        if (!stmts[i]) continue;
        Completion c = resolveStmt(tree, *stmts[i], diag);
        // Stop at the first error (the design's first-error policy): resolving
        // later statements after one failed only spawns cascading follow-on
        // diagnostics (e.g. a bad for-iterable leaves its body-var undeclared).
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
            return Completion::Abrupt;
        }
    }
    return Completion::Normal;
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
    int saved_floor = tree.capture_floor;
    parse::Node* saved_capture_node = tree.capture_node;
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
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = ch->name;
        e.slids_type = ch->return_type;
        e.file_id = ch->file_id;
        e.tok = ch->name_tok;
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    // Local-class pre-pass for the body's TOP LEVEL — BEFORE the nested-function
    // pre-pass, so a nested function may name a body-local class in its signature
    // (`void use(LocalClass^ p)`). resolveStmtList's own pre-pass (which also
    // covers nested blocks) is idempotent — it skips a class already registered
    // here — so these don't double-register. A nested fn sits at the body top, so
    // its signature can only name a top-of-body local class (or a file-scope one).
    registerLocalClasses(tree, fn.children, diag);
    // Nested-function pre-pass: register each nested function's signature in this
    // body frame so the host (and the nested function's own body, for recursion)
    // can call it regardless of textual order. Deep nesting is unsupported.
    for (auto& ch : fn.children) {
        if (!ch || (ch->kind != parse::Kind::kFunctionDef
                    && ch->kind != parse::Kind::kFunctionDecl)) {
            continue;
        }
        if (nested) {
            diagnostic::report(diag, {ch->file_id, ch->name_tok,
                "Nested functions may not contain further nested functions.", {}});
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
        // A nested function may be forward-declared (signature only) and defined
        // later in the same body — match an existing same-name entry: the
        // signature must agree, and a second definition is a duplicate.
        int existing = parse::findInFrame(tree, parse::currentFrameId(tree),
                                          ch->name);
        if (existing >= 0) {
            parse::Entry& prev = tree.entries[existing];
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
        if (is_def) {
            e.def_file_id = ch->file_id;
            e.def_tok = ch->name_tok;
        }
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    // (Local classes in this body are registered by resolveStmtList's pre-pass,
    // which runs for every scope — including nested blocks/if/loops/switch.)
    resolveStmtList(tree, fn.children, diag);
    // Deferred nested-function BODY pass: resolve each nested function's body now
    // that every top-level host local exists, so a nested function may reference a
    // host local declared anywhere in the host (forward capture). Captures are
    // published on the nested entry here — before the call-site DA check below.
    for (auto& ch : fn.children) {
        if (ch && ch->kind == parse::Kind::kFunctionDef) {
            resolveFunctionBody(tree, *ch, diag, /*nested=*/true);
        }
    }
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
}

void registerClassName(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag,
                       int member_of);
void registerClassBody(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag);
void checkClassByValueAcyclic(parse::Tree& tree, parse::Node& node,
                              diagnostic::Sink& diag);

// Register a class's body members (aliases, consts, enums, HOISTED classes) into
// its namespace frame. Aliases first, so a const/enum member can name a sibling
// alias type; then consts/enums; then nested classes two-phase (names then bodies,
// so a nested class field may forward-ref a sibling). Const/enum inits and the
// nested classes' ctor/dtor bodies are resolved later (resolveClassMember*).
void registerClassMembers(parse::Tree& tree, parse::Node& node, int frame,
                          diagnostic::Sink& diag) {
    auto isDup = [&](parse::Node& m) {
        if (findMemberDeclared(tree, frame, m.name) >= 0) {
            diagnostic::report(diag, {m.file_id, m.name_tok,
                "Duplicate declaration of '" + m.name + "'.", {}});
            return true;
        }
        return false;
    };
    // Open THIS class's frame while resolving member TYPES (an alias target, a
    // nested-class field) so a sibling member resolves bare via the scope frame
    // stack — not the findInLiveScopes leniency. The names were registered before
    // their types in the two-phase, so forward references resolve too.
    tree.open_ns_frames.push_back(frame);
    for (auto& m : node.children) {
        if (!m || m->kind != parse::Kind::kAliasDecl) continue;
        if (isDup(*m)) continue;
        resolveDeclType(tree, m->return_type, m->file_id, m->tok, diag);
        parse::Entry e;
        e.kind = parse::EntryKind::kAlias;
        e.name = m->name;
        e.slids_type = m->return_type;
        e.file_id = m->file_id;
        e.tok = m->name_tok;
        e.owner_ns_frame = frame;
        m->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            if (isDup(*m)) continue;
            resolveDeclType(tree, m->return_type, m->file_id, m->tok, diag);
            parse::Entry e;
            e.kind = parse::EntryKind::kConst;
            e.name = m->name;
            e.slids_type = m->return_type;
            e.file_id = m->file_id;
            e.tok = m->name_tok;
            e.owner_ns_frame = frame;
            m->resolved_entry_id = parse::addEntry(tree, std::move(e));
        } else if (m->kind == parse::Kind::kEnumDecl) {
            registerEnum(tree, *m, frame, diag);
        }
    }
    // Hoisted classes — two-phase among the sibling set (names then bodies), each
    // a member of this frame. Their ctor/dtor bodies resolve in resolveClassMemberBodies.
    std::vector<parse::Node*> nested;
    for (auto& m : node.children) {
        if (m && m->kind == parse::Kind::kClassDef) nested.push_back(m.get());
    }
    for (parse::Node* c : nested) registerClassName(tree, *c, diag, frame);
    for (parse::Node* c : nested) registerClassBody(tree, *c, diag);
    for (parse::Node* c : nested) checkClassByValueAcyclic(tree, *c, diag);
    tree.open_ns_frames.pop_back();
}

// Resolve a class's member const inits and enum member inits, once file-scope
// entries (which an init may reference) exist. The class frame is opened so a
// member init can reference a sibling member bare.
void resolveClassMemberInits(parse::Tree& tree, parse::Node& node,
                             diagnostic::Sink& diag) {
    if (node.resolved_entry_id < 0) return;
    int frame = tree.entries[node.resolved_entry_id].ns_frame_id;
    tree.open_ns_frames.push_back(frame);
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            for (auto& init : m->children) {
                if (init) resolveExpr(tree, *init, diag);
            }
        } else if (m->kind == parse::Kind::kEnumDecl) {
            resolveEnumMemberInits(tree, *m, diag);
        } else if (m->kind == parse::Kind::kClassDef) {
            resolveClassMemberInits(tree, *m, diag);   // hoisted class's member inits
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
                       int member_of = -1) {
    int decl_frame = parse::currentFrameId(tree);
    int def_id = member_of >= 0 ? member_of
               : (decl_frame == kGlobalFrame) ? -1 : decl_frame;
    int prev_id = member_of >= 0
        ? findMemberDeclared(tree, member_of, node.name)
        : parse::findInFrame(tree, decl_frame, node.name);
    if (prev_id >= 0) {
        // Any same-name entry already in this frame collides — another class
        // (aliases/enums/namespaces register before classes), or, when classes
        // register first, a later const/function reports the mirror case itself.
        parse::Entry const& prev = tree.entries[prev_id];
        std::string msg = prev.kind == parse::EntryKind::kClass
            ? "Duplicate definition of class '" + node.name + "'."
            : "Duplicate declaration of '" + node.name + "'.";
        reportNameCollision(diag, msg, prev.file_id, prev.tok,
                            node.file_id, node.name_tok);
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
    registerClassMembers(tree, node, cls_frame, diag);   // members don't depend on fields
    parse::ClassInfo info;
    info.name = node.name;           // bare; def_id carries the scope distinction
    info.def_file_id = node.file_id;
    info.def_tok = node.name_tok;
    info.type = type;
    tree.classes.emplace(type, std::move(info));   // placeholder; fields filled in Phase 2
}

// Phase 2: resolve the field types (every sibling class name is known now, so a
// forward field reference validates) and attach the slots to the class's handle.
// Direct ctor/dtor needs are set here; TRANSITIVE needs are widened afterwards
// (the file-scope fixpoint in run(), or registerLocalClasses for a local class).
void registerClassBody(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag) {
    if (node.resolved_entry_id < 0) return;        // a duplicate (Phase 1 skipped it)
    widen::TypeRef type = node.return_type;
    parse::ClassInfo& info = tree.classes.at(widen::strip(type));
    int def_id = widen::get(widen::strip(type)).def_id;
    // Open this class's OWN frame while resolving its FIELD types, so a field may
    // name one of the class's hoisted members bare (`Outer(Inner i_)`) — the same
    // scope the ctor/dtor bodies see. The members were registered in Phase 1.
    int self_frame = tree.entries[node.resolved_entry_id].ns_frame_id;
    tree.open_ns_frames.push_back(self_frame);
    std::vector<std::pair<int, int>> field_locs;   // first-seen loc per field name
    for (auto& p : node.params) {
        if (!p) continue;
        int dup = info.fieldIndex(p->name);
        if (dup >= 0) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "Duplicate field '" + p->name + "' in class '" + node.name + "'.",
                {{field_locs[dup].first, field_locs[dup].second,
                  "first declared here"}}});
            continue;
        }
        if (p->return_type == widen::kNoType) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "Field '" + p->name + "' needs an explicit type.", {}});
            continue;
        }
        resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
        info.field_names.push_back(p->name);
        info.field_types.push_back(p->return_type);
        info.field_params.push_back(p.get());   // stable; default read live later
        field_locs.push_back({p->file_id, p->name_tok});
    }
    tree.open_ns_frames.pop_back();
    // Constructor / destructor presence (parsed as `_$ctor`/`_$dtor` members).
    bool has_ctor = false, has_dtor = false;
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->name == "_$ctor") has_ctor = true;
        else if (m->name == "_$dtor") has_dtor = true;
    }
    info.needs_ctor = has_ctor;
    info.needs_dtor = has_dtor;
    widen::internSlid(node.name, info.field_types, def_id);  // attach slots to `type`
    widen::setSlidLifecycle(type, has_ctor, has_dtor);
}

// Resolve a class's ctor/dtor member bodies. Each is a kFunctionDef carrying an
// implicit `self` (Class^) param; a bare field name in the body resolves to
// `self^.field` (method_fields drives the kIdentExpr fallback rewrite). Runs
// after file-scope entries exist so the bodies can call file-scope functions.
void resolveClassMemberBodies(parse::Tree& tree, parse::Node& node,
                              diagnostic::Sink& diag) {
    // Keyed by the class's kSlid handle, stamped onto node.return_type by
    // registerClass.
    auto it = tree.classes.find(widen::strip(node.return_type));
    if (it == tree.classes.end()) return;
    // Open this class's frame so a ctor/dtor body (and a HOISTED class's body) can
    // reach the class's OWN namespace members bare — its aliases/consts/enums and
    // nested classes (`Inner`, `Outerger`). Recurses into nested classes with this
    // frame still open, so they see the host's members too.
    int frame = node.resolved_entry_id >= 0
        ? tree.entries[node.resolved_entry_id].ns_frame_id : -1;
    if (frame >= 0) tree.open_ns_frames.push_back(frame);
    std::vector<std::string> const* saved = tree.method_fields;
    tree.method_fields = &it->second.field_names;
    for (auto& m : node.children) {
        if (m && (m->name == "_$ctor" || m->name == "_$dtor")) {
            // Resolve the implicit `self` (Class^) param type — for a LOCAL class
            // its bare written name redirects to the scope-mangled kSlid, so a
            // field access through self^ finds the class in tree.classes.
            for (auto& p : m->params) {
                if (p && p->return_type != widen::kNoType)
                    resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
            }
            resolveFunctionBody(tree, *m, diag, /*nested=*/false);
        }
    }
    tree.method_fields = saved;
    for (auto& m : node.children) {
        if (m && m->kind == parse::Kind::kClassDef)
            resolveClassMemberBodies(tree, *m, diag);   // hoisted class's ctor/dtor
    }
    if (frame >= 0) tree.open_ns_frames.pop_back();
}

// Resolve a class's field-default expressions (their ident references), after
// file-scope consts exist. constfold folds them to literals for construction.
void resolveClassFieldDefaults(parse::Tree& tree, parse::Node& node,
                               diagnostic::Sink& diag) {
    for (auto& p : node.params) {
        if (p && !p->children.empty() && p->children[0]) {
            resolveExpr(tree, *p->children[0], diag);
        }
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
        // alias was stripped; none/void/anyptr can't be a by-value class.
        case F::kPointer: case F::kIterator: case F::kPrimitive:
        case F::kVoid: case F::kAnyptr: case F::kAlias: case F::kNone:
            break;
    }
}

// A class whose by-value field graph cycles back to itself has INFINITE size —
// reject it (classify's recursive construction and codegen's struct lowering both
// recurse forever otherwise: a SIGSEGV, not a diagnostic). A `^` / `[]` field
// breaks the cycle. Now that the two-phase makes a class's own name known while
// its fields resolve, this is reachable (`Foo(Foo f_)`, mutual `A(B)`/`B(A)`).
void checkClassByValueAcyclic(parse::Tree& tree, parse::Node& node,
                              diagnostic::Sink& diag) {
    if (node.resolved_entry_id < 0) return;
    widen::TypeRef self = widen::strip(node.return_type);
    auto it = tree.classes.find(self);
    if (it == tree.classes.end()) return;
    std::set<widen::TypeRef> seen;
    std::vector<widen::TypeRef> stack;
    for (widen::TypeRef ft : it->second.field_types) collectByValueClasses(ft, stack);
    while (!stack.empty()) {
        widen::TypeRef cur = stack.back();
        stack.pop_back();
        if (cur == self) {
            diagnostic::report(diag, {node.file_id, node.name_tok,
                "Class '" + node.name + "' contains itself by value (infinite "
                "size); use a reference '^' field.", {}});
            return;
        }
        if (!seen.insert(cur).second) continue;
        auto cit = tree.classes.find(cur);
        if (cit == tree.classes.end()) continue;
        for (widen::TypeRef ft : cit->second.field_types)
            collectByValueClasses(ft, stack);
    }
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
        if (s && s->kind == parse::Kind::kClassDef && s->resolved_entry_id < 0)
            fresh.push_back(s.get());
    }
    if (fresh.empty()) return;
    for (parse::Node* c : fresh) registerClassName(tree, *c, diag);   // Phase 1
    for (parse::Node* c : fresh) registerClassBody(tree, *c, diag);   // Phase 2
    // Reject infinite-size by-value cycles BEFORE member-body / classify recursion.
    for (parse::Node* c : fresh) checkClassByValueAcyclic(tree, *c, diag);
    for (parse::Node* c : fresh) {
        resolveClassFieldDefaults(tree, *c, diag);
        resolveClassMemberInits(tree, *c, diag);
        resolveClassMemberBodies(tree, *c, diag);
    }
    // Transitive needs: the file-scope fixpoint already ran (before any body), so
    // a local class isn't swept by it. Run the same fixpoint over THIS sibling set
    // — a field class is either already published or in this set (chains converge).
    bool changed = true;
    while (changed) {
        changed = false;
        for (parse::Node* c : fresh) {
            if (c->resolved_entry_id < 0) continue;
            parse::ClassInfo& info = tree.classes.at(widen::strip(c->return_type));
            for (widen::TypeRef ft : info.field_types) {
                if (!info.needs_ctor && fieldContributesNeed(tree, ft, true))  { info.needs_ctor = true; changed = true; }
                if (!info.needs_dtor && fieldContributesNeed(tree, ft, false)) { info.needs_dtor = true; changed = true; }
            }
        }
    }
    for (parse::Node* c : fresh) {
        if (c->resolved_entry_id < 0) continue;
        parse::ClassInfo const& info = tree.classes.at(widen::strip(c->return_type));
        widen::setSlidNeeds(c->return_type, info.needs_ctor, info.needs_dtor);
    }
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

}  // namespace

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

    // Pass 1a-alias — register all file-scope value aliases first, so any decl
    // below (in any order) can resolve through them. Targets are validated after
    // enums / namespaces / classes register (an alias may target one of those —
    // `alias Time = Space;`). Bare `alias Ns;` (no target) is a namespace import,
    // handled at use scope.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl
            && ch->return_type != widen::kNoType) {
            registerAlias(tree, *ch, diag);
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

    // Pass 1a-ns — register every file-scope namespace and its members'
    // signatures (recursively), across all reopens, before any body or inline
    // qualified member decl. Members live in the global frame (file lifetime).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kNamespaceDecl) {
            registerNamespaceTree(tree, *ch, kGlobalFrame, diag);
        }
    }

    // Pass 1a-class — register every file-scope class (its field layout + the
    // named kSlid type) before any body, so a decl `Class c` / param `Class^`
    // resolves and carries the layout. TWO-PHASE: all class NAMES first, then all
    // BODIES, so a class field may forward-reference a sibling class defined later.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef) {
            registerClassName(tree, *ch, diag);
        }
    }
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef) {
            registerClassBody(tree, *ch, diag);
        }
    }
    // Reject infinite-size by-value cycles (`Foo(Foo f_)`, mutual `A(B)`/`B(A)`)
    // before the lifecycle fixpoint / classify / codegen recurse forever.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef) {
            checkClassByValueAcyclic(tree, *ch, diag);
        }
    }

    // Pass 1a-alias-validate — now that enums, namespaces, and classes exist,
    // validate each alias target (an alias may name any of them).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl
            && ch->return_type != widen::kNoType) {
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

    // Pass 1a — collect entries at program scope WITHOUT walking init
    // expressions. This lets globals reference each other regardless of
    // declaration order (forward-decl semantics).
    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kFunctionDef
         || ch->kind == parse::Kind::kFunctionDecl) {
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
            if (is_def) {
                e.def_file_id = ch->file_id;
                e.def_tok = ch->name_tok;
            }
            ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
            continue;
        }
        if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const) {
            // A qualified file-scope const defines a namespace member; handled
            // in the inline-member pass below (the namespace must exist first).
            if (isQualified(*ch)) continue;
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
            if (existing >= 0) {
                parse::Entry const& prev = tree.entries[existing];
                reportNameCollision(diag, "Duplicate declaration of '" + ch->name + "'.",
                                    prev.file_id, prev.tok, ch->file_id, ch->name_tok);
                continue;
            }
            parse::Entry e;
            e.kind = parse::EntryKind::kConst;
            e.name = ch->name;
            e.slids_type = ch->return_type;
            e.file_id = ch->file_id;
            e.tok = ch->name_tok;
            ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
            continue;
        }
        if (ch->kind == parse::Kind::kAliasDecl) continue;     // handled above
        if (ch->kind == parse::Kind::kNamespaceDecl) continue; // handled above
        if (ch->kind == parse::Kind::kClassDef) continue;      // handled above
        if (ch->kind == parse::Kind::kEnumDecl) continue;      // handled above
        // Mutable globals + other top-level shapes not supported today.
        // Grammar rejects them; if one slips through, it's a grammar bug.
        diagnostic::report(diag, {ch->file_id, ch->tok,
            "Only function definitions, function declarations, and "
            "constants are allowed at file scope.", {}});
    }

    // Pass 1a-inline — file-scope qualified const members (`const int Space:kSix
    // = 6;`). The namespace exists now; register the member and resolve its init.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kVarDeclStmt && isQualified(*ch)) {
            resolveInlineQualifiedDecl(tree, *ch, diag);
        }
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

    // Pass 1b — walk each top-level const's init expression to resolve
    // ident references. By now every program-scope entry is in the table,
    // so forward refs between globals resolve cleanly.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kVarDeclStmt || !ch->is_const) continue;
        if (isQualified(*ch)) continue;   // inline member init resolved above
        for (auto& init : ch->children) {
            if (init) resolveExpr(tree, *init, diag);
        }
    }

    // Pass 1b-class-members — resolve class member const inits and enum member
    // inits now that every file-scope entry exists (an init may reference one).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef) {
            resolveClassMemberInits(tree, *ch, diag);
        }
    }

    // Pass 1b-class — resolve class field-default expressions now that every
    // file-scope const/entry exists (a default may reference one).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef) {
            resolveClassFieldDefaults(tree, *ch, diag);
        }
    }

    // Pass 2 — walk each function body.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kFunctionDef) continue;
        resolveFunctionBody(tree, *ch, diag);
    }

    // Pass 2-class — resolve class ctor/dtor member bodies (self-bound).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kClassDef) {
            resolveClassMemberBodies(tree, *ch, diag);
        }
    }

    // Pass 2-ns — resolve namespace member bodies (const inits + function
    // bodies), which see their enclosing namespace's members unqualified.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kNamespaceDecl) continue;
        resolveNamespaceBodies(tree, *ch, kGlobalFrame, diag);
    }

    // Pass 3 — orphan declarations. A function declared but never defined
    // (anywhere, used or not) is a compile error: a call to it would emit a
    // `declare` with no `define` and llc would reject the IR. Caret at the
    // declaration's name. (Cross-TU `.slh` headers legitimately declare-only;
    // that distinction defers with the rest of the .slh work.)
    for (parse::Entry const& e : tree.entries) {
        if (e.kind == parse::EntryKind::kFunction && !e.defined) {
            diagnostic::report(diag, {e.file_id, e.tok,
                "Function '" + e.name + "' is declared but never defined.", {}});
        }
    }

    parse::popFrame(tree);
}

}  // namespace resolve
