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

// Same shape as the file-local helper in codegen.cpp: each TU that throws a
// raw CompileError carries its own copy (internal linkage), so the messages
// stay sentence-shaped without leaking a public symbol.
static std::string finalizeErrorMsg(std::string msg) {
    if (msg.empty()) return msg;
    char last = msg.back();
    if (last != '.' && last != '!' && last != '?') msg.push_back('.');
    return msg;
}

void Codegen::inferFieldTypes() {
    // Fills in `SlidDef.fields[i].type` for fields parsed as `name = expr`
    // (no type token). The Program is reachable as `const Program&` on
    // Codegen, but this pass is semantic analysis that completes the AST —
    // analogous to const folding, which mutates Codegen-side const tables.
    // const_cast the SlidDef locally to write back the inferred type.
    std::set<std::string> cycle;
    for (auto& slid_const : program_.slids) {
        if (!slid_const.type_params.empty()) continue; // skip template classes
        auto& slid = const_cast<SlidDef&>(slid_const);
        for (auto& f : slid.fields) {
            if (!f.type.empty()) continue;
            if (!f.default_val) {
                throw CompileError{f.file_id, f.tok,
                    finalizeErrorMsg("Field '" + f.name
                        + "' has no type and no initializer; an inferred field"
                          " must have a const-expression default")};
            }
            ConstEntry folded;
            try {
                cycle.clear();
                folded = foldConstExpr(*f.default_val, slid.name, cycle);
            } catch (CompileError& e) {
                // Preserve foldConstExpr's specific message (and its caret on
                // the offending sub-expression); attach a note pointing at the
                // field declaration so the author sees the inferred-field
                // context that drove the fold.
                e.addNote(f.file_id, f.tok, finalizeErrorMsg(
                    "While inferring the type of field '" + f.name + "'"));
                throw;
            }
            // Inferred field-type rule.
            //   integer: keep foldConstExpr's slid_type (int / int64 by
            //            range, plus the char-literal / nondecimal carve-outs).
            //   float:   range-based — float unless magnitude exceeds
            //            FLT_MAX. Distinct from foldConstExpr's lossless
            //            round-trip rule, which would pick float64 for
            //            values like 3.14 that can't be exactly represented
            //            in float32. Field inference accepts precision loss.
            if (folded.is_float) {
                double v = std::fabs(folded.float_value);
                f.type = (v <= (double)FLT_MAX) ? "float" : "float64";
            } else {
                f.type = folded.slid_type;
            }
        }
    }
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
        info.name_file_id = slid.name_file_id;
        info.name_tok = slid.name_tok;
        info.base_name = slid.base_name;
        for (int i = 0; i < (int)slid.fields.size(); i++) {
            info.field_index[slid.fields[i].name] = i;
            info.field_types.push_back(slid.fields[i].type);
        }
        info.own_field_count = (int)slid.fields.size();
        // declaration of incomplete class: consumer calls __$pinit and __$sizeof
        if (slid.has_trailing_ellipsis)
            info.has_private_suffix = true;
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
        info.is_empty = info.field_types.empty() && !info.has_private_suffix && !info.is_transport_impl;
        // _() and ~() must be declared together or both auto-generated. Declaring
        // only one is a compile error.
        if (info.has_explicit_ctor != info.has_dtor) {
            const char* present = info.has_explicit_ctor ? "_" : "~";
            const char* missing = info.has_explicit_ctor ? "~" : "_";
            error(std::string("Class '" + slid.name
                + "' declares '" + present + "()' but not '" + missing
                + "()'; the constructor and destructor must be declared together,"
                + " or both auto-generated."));
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
        error(std::string("Base class '" + userTypeName(info.base_name)
            + "' of class '" + userTypeName(info.name) + "' is not defined."));
    SlidInfo& base = bit->second;
    resolveSlidInheritanceFor(base); // ensure base's flat layout is built first
    info.base_info = &base;
    // A class deriving from an incomplete (opaque-layout) base is itself
    // opaque-layout: the base's size is only known at runtime, so the derived
    // class's size and field offsets are too. Treat it like an incomplete
    // class — alloca via __$sizeof, __$sizeof declared (not defined) here.
    if (base.has_private_suffix) info.has_private_suffix = true;
    // virtual-base validation: a virtual class must have a virtual base.
    if (info.is_virtual_class && !base.is_virtual_class)
        error(std::string("Class '" + info.name
            + "' is virtual but its base '" + base.name
            + "' is not; all ancestors of a virtual class must have a virtual destructor."));

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
            error(std::string("Field '" + own_names[i]
                + "' in class '" + info.name + "' collides with a field inherited from '"
                + info.base_name + "'."));
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
                        error(std::string("Class '" + name + "' virtual method '"
                            + m.name + "' signature does not match the inherited slot."));
                    // new slot. is_delete + no ancestor + virtual = pure-virtual introduction.
                    // is_default with no ancestor is rejected upstream by validateDefaultDelete.
                    SlidInfo::VtableSlot s;
                    s.method_name = m.name;
                    s.param_types = pts;
                    s.return_type = m.return_type;
                    s.is_pure = m.is_delete;
                    if (!m.is_delete) {
                        s.defining_class = name;
                        s.mangled = mangleMethod(name, m.name, pts);
                    }
                    info.vtable.push_back(std::move(s));
                } else {
                    // override: must match return type exactly
                    if (info.vtable[slot].return_type != m.return_type)
                        error(std::string("Class '" + name + "' virtual override '"
                            + m.name + "' return type does not match the base method."));
                    // = default / = delete with ancestor match: keep base's slot intact.
                    // The contract is enforced via method_marks at compile time.
                    if (m.is_default || m.is_delete) {
                        // no vtable mutation
                    } else {
                        info.vtable[slot].is_pure = false;
                        info.vtable[slot].defining_class = name;
                        info.vtable[slot].mangled = mangleMethod(name, m.name, pts);
                    }
                }
            } else {
                // non-virtual: must not shadow a base virtual of the same name.
                // = default / = delete are contracts, not shadows; skip the check.
                if (m.is_default || m.is_delete) continue;
                int by_name = findSlotByName(m.name);
                if (by_name >= 0)
                    error(std::string("Class '" + name + "' method '" + m.name
                        + "' shadows an inherited virtual method; declare it 'virtual' to override."));
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
                        error(std::string("Class '" + name + "' virtual method '"
                            + em.method_name + "' signature does not match the inherited slot."));
                    error(std::string("Class '" + name
                        + "' may not add new virtual methods in a reopen; '"
                        + em.method_name + "' must be declared in the original class."));
                }
                if (info.vtable[slot].return_type != em.return_type)
                    error(std::string("Class '" + name + "' virtual override '"
                        + em.method_name + "' return type does not match the base method."));
                // = default / = delete in a reopen (with ancestor match): keep base's slot.
                // method_marks enforces the contract; no vtable mutation here.
                if (em.is_default || em.is_delete) {
                    // no-op
                } else if (em.body) {
                    info.vtable[slot].is_pure = false;
                    info.vtable[slot].defining_class = name;
                    info.vtable[slot].mangled = mangleMethod(name, em.method_name, pts);
                }
            } else {
                if (em.is_default || em.is_delete) continue;
                int by_name = findSlotByName(em.method_name);
                if (by_name >= 0)
                    error(std::string("Class '" + name + "' method '" + em.method_name
                        + "' shadows an inherited virtual method; declare it 'virtual' to override."));
            }
        }
    };
    for (auto& [name, info] : slid_info_) build(name);
}

bool Codegen::ancestorHasMethod(const std::string& class_name,
                                const std::string& method_name,
                                const std::vector<std::string>& pts) const {
    auto it = slid_info_.find(class_name);
    if (it == slid_info_.end()) return false;
    const SlidInfo* cur = it->second.base_info;
    while (cur) {
        for (auto& s : program_.slids) {
            if (s.name != cur->name) continue;
            for (auto& m : s.methods) {
                if (m.name != method_name) continue;
                std::vector<std::string> mpts;
                for (auto& [t, n] : m.params) mpts.push_back(t);
                if (mpts == pts) return true;
            }
        }
        for (auto& em : program_.external_methods) {
            if (em.slid_name != cur->name) continue;
            if (em.method_name != method_name) continue;
            std::vector<std::string> mpts;
            for (auto& [t, n] : em.params) mpts.push_back(t);
            if (mpts == pts) return true;
        }
        cur = cur->base_info;
    }
    return false;
}

const SlidInfo::MethodMark* Codegen::findMethodMark(const std::string& class_name,
                                                    const std::string& method_name,
                                                    const std::vector<std::string>& pts,
                                                    std::string& originating_class) const {
    auto it = slid_info_.find(class_name);
    if (it == slid_info_.end()) return nullptr;
    const SlidInfo* cur = &it->second;
    while (cur) {
        for (auto& mk : cur->method_marks) {
            if (mk.method_name == method_name && mk.param_types == pts) {
                originating_class = cur->name;
                return &mk;
            }
        }
        cur = cur->base_info;
    }
    return nullptr;
}

void Codegen::validateDefaultDelete() {
    auto isCtorDtor = [](const std::string& mname) {
        return mname == "_" || mname == "~";
    };
    auto checkAndMark = [&](const std::string& slid_name,
                            const std::string& method_name,
                            const std::vector<std::pair<std::string,std::string>>& params,
                            bool is_default, bool is_delete, bool is_virtual,
                            int decl_file_id, int decl_tok) {
        if (!is_default && !is_delete) return;
        auto sit = slid_info_.find(slid_name);
        if (sit == slid_info_.end()) return;
        if (isCtorDtor(method_name)) {
            std::string kind = is_default ? "= default" : "= delete";
            error("In class '" + userTypeName(slid_name) + "', '" + kind + "' may not be applied to '"
                  + method_name + "()'.");
        }
        std::vector<std::string> pts;
        for (auto& [t, n] : params) pts.push_back(t);
        bool ancestor = ancestorHasMethod(slid_name, method_name, pts);
        if (is_default && !ancestor)
            error("In class '" + userTypeName(slid_name) + "', '" + method_name
                  + "() = default' has no matching base method.");
        if (is_delete && !ancestor && !is_virtual)
            error("In class '" + userTypeName(slid_name) + "', '" + method_name
                  + "() = delete' is non-virtual but has no matching base method;"
                    " pure-virtual introduction requires 'virtual'.");
        // Pure-virtual introduction (delete + virtual + no ancestor) does NOT
        // bind a removal mark — descendants must be free to provide the impl.
        if (!ancestor) return;
        SlidInfo::MethodMark mk;
        mk.method_name = method_name;
        mk.param_types = pts;
        mk.is_default = is_default;
        mk.is_delete = is_delete;
        mk.file_id = decl_file_id;
        mk.tok = decl_tok;
        sit->second.method_marks.push_back(std::move(mk));
    };
    for (auto& slid : program_.slids) {
        for (auto& m : slid.methods) {
            checkAndMark(slid.name, m.name, m.params, m.is_default, m.is_delete, m.is_virtual,
                         m.file_id, m.tok);
        }
    }
    for (auto& em : program_.external_methods) {
        checkAndMark(em.slid_name, em.method_name, em.params, em.is_default, em.is_delete, em.is_virtual,
                     em.file_id, em.tok);
    }
    auto checkBody = [&](const std::string& slid_name,
                         const std::string& method_name,
                         const std::vector<std::pair<std::string,std::string>>& params,
                         int body_file_id, int body_tok) {
        std::vector<std::string> pts;
        for (auto& [t, n] : params) pts.push_back(t);
        std::string origin;
        const SlidInfo::MethodMark* mk = findMethodMark(slid_name, method_name, pts, origin);
        if (!mk) return;
        std::string kind = mk->is_default ? "= default" : "= delete";
        std::string msg = "In class '" + slid_name + "', method '" + method_name
              + "()' is " + kind + " in '" + origin + "' and may not be defined here.";
        std::string note = "Marked " + kind + " here.";
        // Prefer pinning the primary at the offending body's tok; fall back to emit_stack_.
        if (body_tok)
            throw CompileError{body_file_id, body_tok, finalizeErrorMsg(msg)}
                .addNote(mk->file_id, mk->tok, finalizeErrorMsg(note));
        errorWithNote(msg, mk->file_id, mk->tok, note);
    };
    for (auto& slid : program_.slids) {
        for (auto& m : slid.methods) {
            if (m.is_default || m.is_delete) continue;
            if (!m.body) continue;
            checkBody(slid.name, m.name, m.params, m.file_id, m.tok);
        }
    }
    for (auto& em : program_.external_methods) {
        if (em.is_default || em.is_delete) continue;
        if (!em.body) continue;
        checkBody(em.slid_name, em.method_name, em.params, em.file_id, em.tok);
    }
}

void Codegen::synthesizeFieldDtors() {
    // A class whose own fields include a slid-typed field needs a dtor to
    // destroy those fields. If the user didn't write one, mark has_dtor anyway
    // so emitDtors schedules the call and emitSlidCtorDtor emits a synthetic
    // body containing only the field-destruction loop.
    for (auto& [name, info] : slid_info_) {
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

void Codegen::synthesizeCtorNeeds() {
    // Fixed-point: a class needs __$ctor iff it has any own work, or its base
    // / any own field's class needs __$ctor (recursive descent at runtime).
    // Iterate until stable since field/base order isn't guaranteed.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [name, info] : slid_info_) {
            if (info.needs_ctor_fn) continue;
            if (info.is_empty) continue;
            bool need = info.has_explicit_ctor
                || info.has_dtor
                || info.is_transport_impl
                || info.is_virtual_class
                || (info.base_info && info.base_info->needs_ctor_fn);
            if (!need) {
                bool owns_vptr = info.is_virtual_class && info.base_info == nullptr;
                int vptr_local = owns_vptr ? 1 : 0;
                int n_own = info.own_field_count - vptr_local;
                for (int j = 0; j < n_own && !need; j++) {
                    int flat_idx = info.base_field_count + vptr_local + j;
                    if (flat_idx >= (int)info.field_types.size()) break;
                    const std::string& ftype = info.field_types[flat_idx];
                    auto fit = slid_info_.find(ftype);
                    if (fit != slid_info_.end() && fit->second.needs_ctor_fn) {
                        need = true; break;
                    }
                    if (isAnonTupleType(ftype)) {
                        for (auto& et : anonTupleElems(ftype)) {
                            auto eit = slid_info_.find(et);
                            if (eit != slid_info_.end() && eit->second.needs_ctor_fn) {
                                need = true; break;
                            }
                        }
                    }
                }
            }
            if (need) {
                info.needs_ctor_fn = true;
                changed = true;
            }
        }
    }
    // Derived flag: implicit classes (no user _()/~()) that still need ctor
    // work — handled by inlining at the site rather than an emitted function.
    for (auto& [name, info] : slid_info_) {
        info.must_inline_ctor = !info.has_explicit_ctor && info.needs_ctor_fn;
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
            error(std::string("Class '" + name + "' virtual method '"
                + slot.method_name + "' is declared pure (= delete) but is never defined."));
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

void Codegen::scanForSlidTemplateUses() {
    if (template_slids_.empty()) return;

    // Recurse a type-string into its leaves (peel `^`/`[]`/`^^` suffixes,
    // descend into anon-tuple elements, drop const wraps), then ask the
    // instantiator to materialize each leaf if it's a template mangling.
    // The instantiator no-ops on primitives / already-known / non-template
    // names, so calling this on every type-bearing AST surface is safe.
    std::function<void(const std::string&, const Stmt*)> visitType =
        [&](const std::string& t, const Stmt* site) {
        std::string s = t;
        while (true) {
            if (s.size() >= 2 && s.substr(s.size() - 2) == "[]") {
                s = s.substr(0, s.size() - 2);
            } else if (s.size() >= 2 && s.substr(s.size() - 2) == "^^") {
                s = s.substr(0, s.size() - 2);
            } else if (!s.empty() && s.back() == '^') {
                s = s.substr(0, s.size() - 1);
            } else break;
        }
        if (isAnonTupleType(s)) {
            for (auto& e : anonTupleElems(s)) visitType(e, site);
            return;
        }
        if (s.rfind("const ", 0) == 0) s = s.substr(6);
        if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
            std::string inner = s.substr(1, s.size() - 2);
            if (inner.rfind("const ", 0) == 0) s = inner.substr(6);
        }
        if (s.empty()) return;
        ensureSlidInstantiated(s, site);
    };

    // Body walk — locals + everything reachable via forEachChildStmt.
    // Slid template instantiations may sit anywhere a type name appears,
    // including inside a nested-function body.
    std::function<void(const Stmt&)> scanStmt = [&](const Stmt& stmt) {
        if (auto* d = dynamic_cast<const VarDeclStmt*>(&stmt))
            visitType(d->type, d);
        forEachChildStmt(stmt, scanStmt, /*include_nested_fn_body=*/true);
    };
    auto scanBody = [&](const BlockStmt& b) {
        for (auto& s : b.stmts) scanStmt(*s);
    };

    // Surface walk — every place a type-string can be spelled in a
    // declaration. Without this, a template instance referenced only as a
    // field/base/signature type (never as a body-local decl) never gets
    // materialized, and method dispatch against it fails with `… is not a
    // slid type`. See [[project_slh_propagation]] / the bug22-thread audit.
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        if (!slid.base_name.empty()) visitType(slid.base_name, nullptr);
        for (auto& f : slid.fields)  visitType(f.type, nullptr);
        for (auto& m : slid.methods) {
            if (!m.return_type.empty()) visitType(m.return_type, nullptr);
            for (auto& p : m.params) visitType(p.first, nullptr);
        }
    }
    for (auto& em : program_.external_methods) {
        if (!em.return_type.empty()) visitType(em.return_type, nullptr);
        for (auto& p : em.params) visitType(p.first, nullptr);
    }
    for (auto& fn : program_.functions) {
        if (!fn.type_params.empty()) continue;
        if (!fn.return_type.empty()) visitType(fn.return_type, nullptr);
        for (auto& p : fn.params) visitType(p.first, nullptr);
    }
    for (auto& g : program_.globals)
        for (auto& f : g.fields) visitType(f.type, nullptr);

    // Body walks — function/method/ctor/dtor bodies (after surfaces, so
    // surface-only instances are present when bodies are emitted).
    for (auto& fn : program_.functions)
        if (fn.body && fn.type_params.empty()) scanBody(*fn.body);
    for (auto& slid : program_.slids) {
        if (!slid.type_params.empty()) continue;
        if (slid.ctor_body)          scanBody(*slid.ctor_body);
        if (slid.explicit_ctor_body) scanBody(*slid.explicit_ctor_body);
        if (slid.dtor_body)          scanBody(*slid.dtor_body);
        for (auto& m : slid.methods)
            if (m.body) scanBody(*m.body);
    }
}

void Codegen::scanForTemplateFunctionUses() {
    if (template_funcs_.empty()) return;

    std::function<void(const Expr&)> scanExpr;
    std::function<void(const Stmt&)> scanStmt;
    std::function<void(const BlockStmt&)> scanBlock = [&](const BlockStmt& b) {
        for (auto& s : b.stmts) scanStmt(*s);
    };
    // A template-function call with explicit type args needs no inference, so
    // it can be resolved and instantiated up front (idempotent — instantiate
    // dedups). This pulls any local classes it carries into the struct pass.
    auto tryInstantiate = [&](const std::string& callee,
                              const std::vector<std::string>& type_args,
                              const std::vector<std::unique_ptr<Expr>>& args) {
        if (type_args.empty()) return;
        if (!template_funcs_.count(callee)) return;
        auto resolved = resolveTemplateOverload(callee, type_args, args);
        if (resolved.entry) instantiateTemplate(*resolved.entry, resolved.type_args);
    };
    scanExpr = [&](const Expr& e) {
        if (auto* c = dynamic_cast<const CallExpr*>(&e)) {
            tryInstantiate(c->callee, c->type_args, c->args);
            for (auto& a : c->args) scanExpr(*a);
        } else if (auto* b = dynamic_cast<const BinaryExpr*>(&e)) {
            scanExpr(*b->left); scanExpr(*b->right);
        } else if (auto* u = dynamic_cast<const UnaryExpr*>(&e)) {
            scanExpr(*u->operand);
        } else if (auto* d = dynamic_cast<const DerefExpr*>(&e)) {
            scanExpr(*d->operand);
        } else if (auto* a = dynamic_cast<const AddrOfExpr*>(&e)) {
            scanExpr(*a->operand);
        } else if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&e)) {
            scanExpr(*fa->object);
        } else if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&e)) {
            scanExpr(*ai->base); scanExpr(*ai->index);
        } else if (auto* mc = dynamic_cast<const MethodCallExpr*>(&e)) {
            scanExpr(*mc->object);
            for (auto& a : mc->args) scanExpr(*a);
        } else if (auto* t = dynamic_cast<const TupleExpr*>(&e)) {
            for (auto& v : t->values) scanExpr(*v);
        }
    };
    scanStmt = [&](const Stmt& stmt) {
        // Leaf-level expression scans; child stmts walked by forEachChildStmt
        // below. Template-function calls may appear anywhere — including in
        // nested-function bodies — so descend into those too.
        if (auto* c = dynamic_cast<const CallStmt*>(&stmt)) {
            tryInstantiate(c->callee, c->type_args, c->args);
            for (auto& a : c->args) scanExpr(*a);
        } else if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
            scanExpr(*es->expr);
        } else if (auto* d = dynamic_cast<const VarDeclStmt*>(&stmt)) {
            if (d->init) scanExpr(*d->init);
            for (auto& a : d->ctor_args) scanExpr(*a);
        } else if (auto* as = dynamic_cast<const AssignStmt*>(&stmt)) {
            scanExpr(*as->value);
        } else if (auto* r = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (r->value) scanExpr(*r->value);
        } else if (auto* fa = dynamic_cast<const FieldAssignStmt*>(&stmt)) {
            scanExpr(*fa->object); scanExpr(*fa->value);
        } else if (auto* ia = dynamic_cast<const IndexAssignStmt*>(&stmt)) {
            scanExpr(*ia->base); scanExpr(*ia->index); scanExpr(*ia->value);
        } else if (auto* da = dynamic_cast<const DerefAssignStmt*>(&stmt)) {
            scanExpr(*da->ptr); scanExpr(*da->value);
        } else if (auto* ca = dynamic_cast<const CompoundAssignStmt*>(&stmt)) {
            scanExpr(*ca->lhs); scanExpr(*ca->rhs);
        } else if (auto* mc = dynamic_cast<const MethodCallStmt*>(&stmt)) {
            scanExpr(*mc->object);
            for (auto& a : mc->args) scanExpr(*a);
        } else if (auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
            scanExpr(*i->cond);
        } else if (auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
            scanExpr(*w->cond);
        } else if (auto* fl = dynamic_cast<const ForLongStmt*>(&stmt)) {
            if (fl->cond) scanExpr(*fl->cond);
        } else if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
            scanExpr(*sw->expr);
            for (auto& sc : sw->cases)
                if (sc.value) scanExpr(*sc.value);
        }
        forEachChildStmt(stmt, scanStmt, /*include_nested_fn_body=*/true);
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

// --- substitution constants -------------------------------------------------

namespace {
static const std::map<std::string, int> kIntSignedBits = {
    {"int8", 8}, {"int16", 16}, {"int", 32}, {"int32", 32}, {"int64", 64},
    {"intptr", 64}, {"bool", 32}
};
static const std::map<std::string, int> kIntUnsignedBits = {
    {"uint8", 8}, {"uint16", 16}, {"uint", 32}, {"uint32", 32}, {"uint64", 64},
    {"char", 8}
};
static bool constIsFloatType(const std::string& t) {
    return t == "float" || t == "float32" || t == "float64";
}
static bool constIsIntType(const std::string& t) {
    return kIntSignedBits.count(t) || kIntUnsignedBits.count(t);
}
static bool constIsUnsignedType(const std::string& t) {
    return kIntUnsignedBits.count(t) != 0;
}
static int constIntBits(const std::string& t) {
    auto it = kIntSignedBits.find(t);
    if (it != kIntSignedBits.end()) return it->second;
    auto it2 = kIntUnsignedBits.find(t);
    if (it2 != kIntUnsignedBits.end()) return it2->second;
    return 0;
}
static bool fitsSigned(int64_t v, int bits) {
    if (bits >= 64) return true;
    int64_t lo = -((int64_t)1 << (bits - 1));
    int64_t hi = ((int64_t)1 << (bits - 1)) - 1;
    return v >= lo && v <= hi;
}
static bool fitsUnsigned(int64_t v, int bits) {
    if (v < 0) return false;
    if (bits >= 64) return true;
    return (uint64_t)v <= (((uint64_t)1 << bits) - 1);
}
static std::string formatConstFloat(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}
}

void Codegen::collectAndFoldConsts() {
    // pre-register all const decls so cross-references see the names even when
    // unresolved. duplicate-name detection per scope.
    auto& global_frame = block_const_stack_.front();
    for (auto& cd : program_.consts) {
        // a const declared inside a namespace is keyed `ns:name`.
        std::string key = qualifiedName(cd.namespace_name, cd.name);
        if (global_frame.count(key))
            errorAtNodeWithNote(*cd.rhs,
                "Global const '" + key + "' is already declared.",
                global_frame[key].file_id, global_frame[key].tok,
                "First declared here.");
        ConstEntry e;
        e.file_id = cd.file_id;
        e.tok = cd.tok;
        global_frame[key] = e;
    }
    for (auto& slid : program_.slids) {
        for (auto& cd : slid.consts) {
            auto& tbl = slid_consts_[slid.name];
            if (tbl.count(cd.name))
                errorAtNodeWithNote(*cd.rhs,
                    "Class const '" + slid.name + "." + cd.name + "' is already declared.",
                    tbl[cd.name].file_id, tbl[cd.name].tok,
                    "First declared here.");
            ConstEntry e;
            e.file_id = cd.file_id;
            e.tok = cd.tok;
            tbl[cd.name] = e;
        }
    }

    std::set<std::string> cycle;
    for (auto& cd : program_.consts) {
        std::string key = qualifiedName(cd.namespace_name, cd.name);
        if (global_frame[key].slid_type.empty()) {
            cycle.clear();
            foldConstDef(cd, "", cycle);
        }
    }
    for (auto& slid : program_.slids) {
        for (auto& cd : slid.consts) {
            if (slid_consts_[slid.name][cd.name].slid_type.empty()) {
                cycle.clear();
                foldConstDef(cd, slid.name, cycle);
            }
        }
    }
}

void Codegen::foldConstDef(const ConstDef& cd,
                           const std::string& slid_scope,
                           std::set<std::string>& cycle) {
    std::string key = slid_scope + "::" + cd.name;
    cycle.insert(key);
    ConstEntry folded = foldConstExpr(*cd.rhs, slid_scope, cycle);
    ConstEntry final_e = applyConstDeclaredType(cd, folded);
    cycle.erase(key);
    if (slid_scope.empty()) {
        std::string ns_key = qualifiedName(cd.namespace_name, cd.name);
        block_const_stack_.front()[ns_key] = final_e;
    }
    else slid_consts_[slid_scope][cd.name] = final_e;
}

Codegen::ConstEntry Codegen::foldConstExpr(const Expr& e,
                                            const std::string& slid_scope,
                                            std::set<std::string>& cycle) {
    if (auto* il = dynamic_cast<const IntLiteralExpr*>(&e)) {
        ConstEntry r;
        r.int_value = il->value;
        if (il->is_char_literal) r.slid_type = "char";
        else if (il->is_nondecimal) {
            uint64_t uv = (uint64_t)il->value;
            r.slid_type = (uv <= 0xFFFFFFFFull) ? "uint" : "uint64";
        } else {
            r.slid_type = (il->value >= INT32_MIN && il->value <= INT32_MAX) ? "int" : "int64";
        }
        return r;
    }
    if (auto* fl = dynamic_cast<const FloatLiteralExpr*>(&e)) {
        ConstEntry r;
        r.is_float = true;
        r.float_value = fl->value;
        float fv = (float)fl->value;
        r.slid_type = ((double)fv == fl->value) ? "float" : "float64";
        return r;
    }
    if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
        // class-scope lookup first
        if (!slid_scope.empty()) {
            auto sit = slid_consts_.find(slid_scope);
            if (sit != slid_consts_.end()) {
                auto cit = sit->second.find(ve->name);
                if (cit != sit->second.end()) {
                    if (cit->second.slid_type.empty()) {
                        std::string key = slid_scope + "::" + ve->name;
                        if (cycle.count(key))
                            errorAtNode(e,
                                "Constant '" + ve->name + "' has a cyclic initializer.");
                        for (auto& s : program_.slids)
                            if (s.name == slid_scope)
                                for (auto& cd : s.consts)
                                    if (cd.name == ve->name)
                                        foldConstDef(cd, slid_scope, cycle);
                    }
                    return cit->second;
                }
            }
        }
        // global (bottom of block_const_stack_)
        auto& global_frame = block_const_stack_.front();
        auto cit = global_frame.find(ve->name);
        if (cit != global_frame.end()) {
            if (cit->second.slid_type.empty()) {
                std::string key = std::string("::") + ve->name;
                if (cycle.count(key))
                    errorAtNode(e,
                        "Constant '" + ve->name + "' has a cyclic initializer.");
                for (auto& cd : program_.consts)
                    if (cd.name == ve->name)
                        foldConstDef(cd, "", cycle);
            }
            return cit->second;
        }
        errorAtNode(e,
            "Constant initializer references unknown name '" + ve->name + "'.");
    }
    if (auto* u = dynamic_cast<const UnaryExpr*>(&e)) {
        ConstEntry op = foldConstExpr(*u->operand, slid_scope, cycle);
        if (u->op == "-") {
            ConstEntry r = op;
            if (op.is_float) r.float_value = -op.float_value;
            else r.int_value = -op.int_value;
            return r;
        }
        if (u->op == "+") return op;
        if (u->op == "!") {
            ConstEntry r;
            r.slid_type = "bool";
            r.int_value = (op.is_float ? (op.float_value == 0.0)
                                       : (op.int_value == 0)) ? 1 : 0;
            return r;
        }
        if (u->op == "~") {
            if (op.is_float)
                errorAtNode(e, "Bitwise '~' is not allowed on a float in a constant initializer.");
            ConstEntry r = op;
            r.int_value = ~op.int_value;
            return r;
        }
        errorAtNode(e, "Operator '" + u->op + "' is not supported in a constant initializer.");
    }
    if (auto* b = dynamic_cast<const BinaryExpr*>(&e)) {
        ConstEntry L = foldConstExpr(*b->left, slid_scope, cycle);
        ConstEntry R = foldConstExpr(*b->right, slid_scope, cycle);
        bool any_float = L.is_float || R.is_float;

        auto widerInt = [&]() -> std::string {
            int bits = std::max(constIntBits(L.slid_type), constIntBits(R.slid_type));
            bool is_unsigned = constIsUnsignedType(L.slid_type) && constIsUnsignedType(R.slid_type);
            if (bits <= 32) return is_unsigned ? "uint" : "int";
            return is_unsigned ? "uint64" : "int64";
        };
        auto widerFloat = [&]() -> std::string {
            return (L.slid_type == "float64" || R.slid_type == "float64") ? "float64" : "float";
        };

        if (b->op == "+" || b->op == "-" || b->op == "*" || b->op == "/" || b->op == "%") {
            ConstEntry r;
            if (any_float) {
                if (b->op == "%")
                    errorAtNode(e, "Modulo '%' is not allowed on a float in a constant initializer.");
                double lv = L.is_float ? L.float_value : (double)L.int_value;
                double rv = R.is_float ? R.float_value : (double)R.int_value;
                double res;
                if (b->op == "+") res = lv + rv;
                else if (b->op == "-") res = lv - rv;
                else if (b->op == "*") res = lv * rv;
                else { if (rv == 0) errorAtNode(e, "Division by zero in a constant initializer."); res = lv / rv; }
                r.is_float = true;
                r.float_value = res;
                r.slid_type = widerFloat();
            } else {
                int64_t lv = L.int_value, rv = R.int_value, res;
                if (b->op == "+") res = lv + rv;
                else if (b->op == "-") res = lv - rv;
                else if (b->op == "*") res = lv * rv;
                else if (b->op == "/") { if (rv == 0) errorAtNode(e, "Division by zero in a constant initializer."); res = lv / rv; }
                else { if (rv == 0) errorAtNode(e, "Modulo by zero in a constant initializer."); res = lv % rv; }
                r.int_value = res;
                r.slid_type = widerInt();
            }
            return r;
        }
        if (b->op == "&" || b->op == "|" || b->op == "^"
                || b->op == "<<" || b->op == ">>") {
            if (any_float)
                errorAtNode(e, "Bitwise '" + b->op + "' is not allowed on a float in a constant initializer.");
            ConstEntry r;
            int64_t lv = L.int_value, rv = R.int_value, res = 0;
            if (b->op == "&") res = lv & rv;
            else if (b->op == "|") res = lv | rv;
            else if (b->op == "^") res = lv ^ rv;
            else if (b->op == "<<") res = lv << rv;
            else res = lv >> rv;
            r.int_value = res;
            r.slid_type = widerInt();
            return r;
        }
        if (b->op == "==" || b->op == "!=" || b->op == "<"
                || b->op == ">" || b->op == "<=" || b->op == ">=") {
            bool res;
            if (any_float) {
                double lv = L.is_float ? L.float_value : (double)L.int_value;
                double rv = R.is_float ? R.float_value : (double)R.int_value;
                if (b->op == "==") res = lv == rv;
                else if (b->op == "!=") res = lv != rv;
                else if (b->op == "<") res = lv < rv;
                else if (b->op == ">") res = lv > rv;
                else if (b->op == "<=") res = lv <= rv;
                else res = lv >= rv;
            } else {
                int64_t lv = L.int_value, rv = R.int_value;
                if (b->op == "==") res = lv == rv;
                else if (b->op == "!=") res = lv != rv;
                else if (b->op == "<") res = lv < rv;
                else if (b->op == ">") res = lv > rv;
                else if (b->op == "<=") res = lv <= rv;
                else res = lv >= rv;
            }
            ConstEntry r;
            r.slid_type = "bool";
            r.int_value = res ? 1 : 0;
            return r;
        }
        if (b->op == "&&" || b->op == "||" || b->op == "^^") {
            bool lb = L.is_float ? (L.float_value != 0.0) : (L.int_value != 0);
            bool rb = R.is_float ? (R.float_value != 0.0) : (R.int_value != 0);
            bool res;
            if (b->op == "&&") res = lb && rb;
            else if (b->op == "||") res = lb || rb;
            else res = lb != rb;
            ConstEntry r;
            r.slid_type = "bool";
            r.int_value = res ? 1 : 0;
            return r;
        }
        errorAtNode(e, "Operator '" + b->op + "' is not supported in a constant initializer.");
    }
    if (auto* tc = dynamic_cast<const TypeConvExpr*>(&e)) {
        ConstEntry inner = foldConstExpr(*tc->operand, slid_scope, cycle);
        const std::string& dst = tc->target_type;
        ConstEntry r;
        r.slid_type = dst;
        if (constIsFloatType(dst)) {
            r.is_float = true;
            r.float_value = inner.is_float ? inner.float_value : (double)inner.int_value;
            return r;
        }
        if (dst == "bool") {
            r.is_float = false;
            r.int_value = (inner.is_float ? (inner.float_value != 0.0)
                                          : (inner.int_value != 0)) ? 1 : 0;
            return r;
        }
        if (constIsIntType(dst)) {
            int64_t v = inner.is_float ? (int64_t)inner.float_value : inner.int_value;
            int bits = constIntBits(dst);
            if (bits > 0 && bits < 64) {
                uint64_t mask = ((uint64_t)1 << bits) - 1;
                v = (int64_t)((uint64_t)v & mask);
                if (!constIsUnsignedType(dst)) {
                    int64_t sign_bit = (int64_t)1 << (bits - 1);
                    if (v & sign_bit) v |= (int64_t)~mask;
                }
            }
            r.is_float = false;
            r.int_value = v;
            return r;
        }
        errorAtNode(e, "Type conversion to '" + dst + "' is not supported in a constant initializer.");
    }
    errorAtNode(e, "Expression is not allowed in a constant initializer.");
}

Codegen::ConstEntry Codegen::applyConstDeclaredType(const ConstDef& cd,
                                                    const ConstEntry& folded) {
    ConstEntry r = folded;
    r.file_id = cd.file_id;
    r.tok = cd.tok;
    // Decl-keyword `const` always qualifies the declared name. The slid type
    // we store carries the user-visible "const " prefix; type-fit checks
    // operate on the canonical (qualifier-stripped) form.
    auto qualify = [&](const std::string& base) -> std::string {
        if (base.rfind("const ", 0) == 0 || base.rfind("mutable ", 0) == 0) return base;
        return "const " + base;
    };
    if (cd.declared_type.empty()) {
        r.slid_type = qualify(folded.slid_type);
        return r;
    }
    std::string dst = canonType(cd.declared_type);
    std::string final_qualified = qualify(cd.declared_type);
    auto narrow = [&](const std::string& msg) {
        throw CompileError{cd.file_id, cd.tok, finalizeErrorMsg(msg)};
    };
    if (constIsFloatType(dst)) {
        double v = folded.is_float ? folded.float_value : (double)folded.int_value;
        r.is_float = true;
        r.float_value = v;
        r.slid_type = final_qualified;
        return r;
    }
    if (dst == "bool") {
        if (folded.is_float)
            narrow("Cannot implicitly truncate constant '" + formatConstFloat(folded.float_value)
                + "' from '" + folded.slid_type + "' to 'bool'.");
        if (folded.int_value != 0 && folded.int_value != 1)
            narrow("Constant '" + std::to_string(folded.int_value)
                + "' overflows declared type 'bool'.");
        r.is_float = false;
        r.int_value = folded.int_value;
        r.slid_type = final_qualified;
        return r;
    }
    if (constIsIntType(dst)) {
        if (folded.is_float)
            narrow("Cannot implicitly truncate constant '" + formatConstFloat(folded.float_value)
                + "' from '" + folded.slid_type + "' to '" + dst + "'.");
        int bits = constIntBits(dst);
        if (constIsUnsignedType(dst)) {
            if (folded.int_value < 0)
                narrow("Cannot assign negative constant '" + std::to_string(folded.int_value)
                    + "' to unsigned type '" + dst + "'.");
            if (!fitsUnsigned(folded.int_value, bits))
                narrow("Constant '" + std::to_string(folded.int_value)
                    + "' overflows declared type '" + dst + "'.");
        } else {
            if (!fitsSigned(folded.int_value, bits))
                narrow("Constant '" + std::to_string(folded.int_value)
                    + "' overflows declared type '" + dst + "'.");
        }
        r.is_float = false;
        r.int_value = folded.int_value;
        r.slid_type = final_qualified;
        return r;
    }
    throw CompileError{cd.file_id, cd.tok,
        finalizeErrorMsg("Unsupported declared type '" + cd.declared_type
            + "' for const '" + cd.name + "'.")};
}

const Codegen::ConstEntry* Codegen::lookupConst(const std::string& name) const {
    // qualified `Class:member` (or `Outer:Inner:member`) — a class-scoped
    // const reached via the type name with the `:` scope operator. Inherits
    // up the base chain. The class path is written with `:`; a nested class
    // is keyed with `.` internally (Outer.Inner), so convert for the lookup.
    {
        std::string canon = canonicalizeShortPath(name);
        auto colon = canon.rfind(':');
        if (colon != std::string::npos && colon > 0 && canon[0] != ':') {
            std::string cls = canon.substr(0, colon);
            for (char& c : cls) if (c == ':') c = '.';
            if (slid_info_.count(cls))
                return lookupSlidConst(cls, canon.substr(colon + 1));
        }
    }
    // Walk block frames top-down EXCEPT the bottom (global) frame so that
    // the enclosing class scope is consulted between function-block frames
    // and the global frame — preserves the old global_consts_ ordering.
    for (size_t i = block_const_stack_.size(); i > 1; i--) {
        auto& frame = block_const_stack_[i - 1];
        auto cit = frame.find(name);
        if (cit != frame.end()) return &cit->second;
    }
    // enclosing class scope — walk current_slid_'s lexical enclosing chain
    // (innermost first) and at each level the base chain so a derived
    // class's methods see consts declared in a base class, and a nested
    // class's methods see consts declared in any enclosing class.
    if (!current_slid_.empty()) {
        for (auto& prefix : enclosingClassPrefixes()) {
            auto siit = slid_info_.find(prefix);
            for (const SlidInfo* b = (siit != slid_info_.end() ? &siit->second : nullptr);
                 b; b = b->base_info) {
                auto sit = slid_consts_.find(b->name);
                if (sit != slid_consts_.end()) {
                    auto cit = sit->second.find(name);
                    if (cit != sit->second.end()) return &cit->second;
                }
            }
        }
    }
    // global scope = block_const_stack_.front()
    if (!block_const_stack_.empty()) {
        auto& global_frame = block_const_stack_.front();
        auto cit = global_frame.find(name);
        if (cit != global_frame.end()) return &cit->second;
    }
    return nullptr;
}

const Codegen::ConstEntry* Codegen::lookupSlidConst(const std::string& slid_name,
                                                    const std::string& member) const {
    // direct scope, then up the base chain so `Derived:const` resolves a
    // const declared in a base class.
    auto direct = slid_consts_.find(slid_name);
    if (direct != slid_consts_.end()) {
        auto cit = direct->second.find(member);
        if (cit != direct->second.end()) return &cit->second;
    }
    auto siit = slid_info_.find(slid_name);
    for (const SlidInfo* b = (siit != slid_info_.end() ? siit->second.base_info : nullptr);
         b; b = b->base_info) {
        auto sit = slid_consts_.find(b->name);
        if (sit != slid_consts_.end()) {
            auto cit = sit->second.find(member);
            if (cit != sit->second.end()) return &cit->second;
        }
    }
    return nullptr;
}

bool Codegen::lookupEnumValueChained(const std::string& canon, int& out) const {
    auto colon = canon.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        auto eit = enum_values_.find(canon);
        if (eit != enum_values_.end()) { out = eit->second; return true; }
        return false;
    }
    std::string class_path = canon.substr(0, colon);
    std::string member = canon.substr(colon + 1);
    std::string dot_path = class_path;
    for (char& c : dot_path) if (c == ':') c = '.';
    auto siit = slid_info_.find(dot_path);
    for (const SlidInfo* b = (siit != slid_info_.end() ? &siit->second : nullptr);
         b; b = b->base_info) {
        std::string scope = b->name;
        for (char& c : scope) if (c == '.') c = ':';
        auto eit = enum_values_.find(scope + ":" + member);
        if (eit != enum_values_.end()) { out = eit->second; return true; }
    }
    auto eit = enum_values_.find(canon);
    if (eit != enum_values_.end()) { out = eit->second; return true; }
    return false;
}

bool Codegen::lookupCurrentSlidEnumValue(const std::string& name, int& out) const {
    if (current_slid_.empty()) return false;
    for (auto& prefix : enclosingClassPrefixes()) {
        auto siit = slid_info_.find(prefix);
        for (const SlidInfo* b = (siit != slid_info_.end() ? &siit->second : nullptr);
             b; b = b->base_info) {
            std::string scope = b->name;
            for (char& c : scope) if (c == '.') c = ':';
            auto eit = enum_values_.find(scope + ":" + name);
            if (eit != enum_values_.end()) { out = eit->second; return true; }
        }
    }
    return false;
}

std::vector<std::string> Codegen::enclosingClassPrefixes() const {
    std::vector<std::string> r;
    if (current_slid_.empty()) return r;
    std::string acc = current_slid_;
    while (!acc.empty()) {
        r.push_back(acc);
        size_t dot = acc.rfind('.');
        if (dot == std::string::npos) break;
        acc = acc.substr(0, dot);
    }
    return r;
}

std::string Codegen::canonicalizeShortPath(const std::string& name) const {
    if (current_slid_.empty()) return name;
    auto colon = name.find(':');
    if (colon == std::string::npos || colon == 0) return name;
    std::string first = name.substr(0, colon);
    std::string rest = name.substr(colon);
    // split current_slid_ into dot-separated segments
    std::vector<std::string> segs;
    {
        size_t start = 0;
        while (true) {
            size_t end = current_slid_.find('.', start);
            if (end == std::string::npos) {
                segs.push_back(current_slid_.substr(start));
                break;
            }
            segs.push_back(current_slid_.substr(start, end - start));
            start = end + 1;
        }
    }
    auto colonize = [](const std::string& s) {
        std::string r;
        for (char c : s) r += (c == '.') ? ':' : c;
        return r;
    };
    // walk innermost → outermost; for each enclosing class, check the
    // leading segment as self (the class's own short name) or as a class
    // nested at this enclosing level.
    for (int i = (int)segs.size() - 1; i >= 0; i--) {
        std::string path;
        for (int j = 0; j <= i; j++) {
            if (j) path += '.';
            path += segs[j];
        }
        if (segs[i] == first) return colonize(path) + rest;
        std::string candidate = path + "." + first;
        if (slid_info_.count(candidate)) return colonize(candidate) + rest;
    }
    // file scope: leading segment as a top-level class
    if (slid_info_.count(first)) return colonize(first) + rest;
    return name;
}

bool Codegen::isFoldableConstShape(const Expr& e, const std::string& slid_scope) const {
    if (dynamic_cast<const IntLiteralExpr*>(&e)) return true;
    if (dynamic_cast<const FloatLiteralExpr*>(&e)) return true;
    if (auto* u = dynamic_cast<const UnaryExpr*>(&e))
        return isFoldableConstShape(*u->operand, slid_scope);
    if (auto* b = dynamic_cast<const BinaryExpr*>(&e))
        return isFoldableConstShape(*b->left, slid_scope)
            && isFoldableConstShape(*b->right, slid_scope);
    if (auto* tc = dynamic_cast<const TypeConvExpr*>(&e))
        return isFoldableConstShape(*tc->operand, slid_scope);
    if (auto* qc = dynamic_cast<const QualifierCastExpr*>(&e))
        return isFoldableConstShape(*qc->operand, slid_scope);
    if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
        if (!slid_scope.empty()) {
            auto sit = slid_consts_.find(slid_scope);
            if (sit != slid_consts_.end() && sit->second.count(ve->name)) return true;
        }
        for (auto it = block_const_stack_.rbegin(); it != block_const_stack_.rend(); ++it)
            if (it->count(ve->name)) return true;
        return false;
    }
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&e)) {
        if (auto* veo = dynamic_cast<const VarExpr*>(fa->object.get())) {
            auto sit = slid_consts_.find(veo->name);
            if (sit != slid_consts_.end() && sit->second.count(fa->field)) return true;
        }
        return false;
    }
    return false;
}

std::string Codegen::emitConstValue(const ConstEntry& e) const {
    if (e.is_float) {
        // LLVM IR float32 constants must be representable in float32; encode
        // as a hex double of the value round-tripped through float so the
        // upcast is lossless. float64 constants emit as decimal.
        std::string canon_t = canonType(e.slid_type);
        if (canon_t == "float" || canon_t == "float32") {
            float fv = (float)e.float_value;
            double dv = (double)fv;
            uint64_t bits;
            std::memcpy(&bits, &dv, sizeof(bits));
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%016llX",
                     static_cast<unsigned long long>(bits));
            return buf;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", e.float_value);
        std::string s = buf;
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    }
    return std::to_string(e.int_value);
}
