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

void Codegen::emitExplicitDtor(const std::string& static_type, const std::string& obj_ptr) {
    if (isAnonTupleType(static_type)) {
        // anon tuple: walk slid slots in reverse declaration order, dtor each
        auto elems = anonTupleElems(static_type);
        for (int i = (int)elems.size() - 1; i >= 0; i--) {
            const std::string& et = elems[i];
            if (slid_info_.count(et) && slid_info_.at(et).has_dtor) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr " << llvmType(static_type)
                     << ", ptr " << obj_ptr << ", i32 0, i32 " << i << "\n";
                emitDtorChainCall(et, gep);
            } else if (isAnonTupleType(et)) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr " << llvmType(static_type)
                     << ", ptr " << obj_ptr << ", i32 0, i32 " << i << "\n";
                emitExplicitDtor(et, gep);
            }
        }
        return;
    }
    auto sit = slid_info_.find(static_type);
    if (sit == slid_info_.end()) return; // primitive / pointer — no-op
    if (sit->second.has_dtor)
        emitDtorChainCall(static_type, obj_ptr);
}

void Codegen::emitDtorChainCall(const std::string& slid_type, const std::string& target) {
    auto sit = slid_info_.find(slid_type);
    if (sit == slid_info_.end()) return;
    auto& info = sit->second;
    if (!info.has_dtor) return;     // no dtor work for this class
    if (info.must_inline_ctor) {
        // Implicit class — no __$dtor symbol; inline the synthesized walk.
        emitInlineDtorWalk(slid_type, target);
        return;
    }
    // Explicit class — single call. The dtor function handles its own fields
    // and chains to its base internally. (is_empty classes still get a real
    // __$dtor function with `ptr %self` signature; the call form matches.)
    out_ << "    call void @" << slid_type << "__$dtor(ptr " << target << ")\n";
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
    // Fire scope-exit hooks (e.g. `__$global_dtor_all()` from `global;`) in
    // reverse, walking from the innermost frame outward. Hooks remain in the
    // frames so popScope's symmetric path skips them — block_terminated_ is
    // set right after this by the caller's `ret`/`br`.
    for (int i = (int)scope_stack_.size() - 1; i >= 0; i--) {
        auto& frame = scope_stack_[i];
        for (int j = (int)frame.exit_emits.size() - 1; j >= 0; j--)
            out_ << frame.exit_emits[j];
    }
}

bool Codegen::derivesFromTransportBase(const std::string& slid_name) const {
    auto it = slid_info_.find(slid_name);
    if (it == slid_info_.end()) return false;
    for (const SlidInfo* b = it->second.base_info; b; b = b->base_info)
        if (b->is_transport_impl || b->has_private_suffix) return true;
    return false;
}

void Codegen::emitSlidCtorDtor(const SlidDef& slid) {
    // find ctor/dtor bodies — either inline or external
    const BlockStmt* explicit_ctor_body = slid.explicit_ctor_body.get();
    const BlockStmt* dtor_body = slid.dtor_body.get();
    for (auto& em : program_.external_methods) {
        if (em.slid_name != slid.name || !em.body) continue;
        if (em.method_name == "_")  explicit_ctor_body = em.body.get();
        if (em.method_name == "~")  dtor_body = em.body.get();
    }
    // implicit ctor body: loose code in the class body (only present when no _()/~())
    const BlockStmt* implicit_ctor_body = slid.ctor_body.get();

    auto sit = slid_info_.find(slid.name);
    if (sit == slid_info_.end()) return;
    auto& info = sit->second;

    // Emit __$ctor + __$ctor_body only when (a) the class has a user-declared
    // _() (has_explicit_ctor — true in every TU that imports the class), AND
    // (b) THIS TU has the body locally — either inline `_(){...}`, implicit
    // loose ctor_body, an external `Foo:_(){...}` definition, or this is the
    // transport-impl closing TU. This combination distinguishes the definer
    // (one TU, has body) from consumer TUs (no body), avoiding cross-TU
    // duplicate `define`s. Implicit classes (must_inline_ctor) are inlined at
    // every construction site — no symbol, no cross-TU issue.
    bool has_body_here = explicit_ctor_body || implicit_ctor_body || dtor_body
        || slid.is_transport_impl;
    bool emit_ctor_pair = info.has_explicit_ctor && has_body_here;
    bool owns_vptr = info.is_virtual_class && info.base_info == nullptr;
    int vptr_local = owns_vptr ? 1 : 0;
    int n_own = info.own_field_count - vptr_local;

    // === __$ctor ===
    // Outer entry. Initializes the private suffix (if transport impl), then
    // tail-calls __$ctor_body to do the rest of construction.
    if (emit_ctor_pair) {
        locals_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;
        self_ptr_ = "%self";
        // Add self to the scope table so type-aware lookups (inferSlidType,
        // op-method dispatch) find it like any other local. The .reg field
        // is set to the entry value; runtime remap (during inline-ctor-body
        // emission for new T[n]) lives in self_ptr_, not here.
        // Phase 4: const ctor — self is const inside the user body. Synth
        // field-init walks are direct LLVM emits (no AssignStmt) so const
        // is invisible to them; only the user body sees the rejection.
        locals_["self"].type = slid.is_const_ctor ? ("const " + slid.name) : slid.name;
        locals_["self"].reg = "%self";

        out_ << "define " << (isExported(slid.name + "__$ctor") ? "" : "internal ")
             << "void @" << slid.name << "__$ctor(ptr %self) {\n";
        out_ << "entry:\n";

        pushScope();
        // Recursive descent (Itanium order): construct the base sub-object
        // fully first, then this class's own field defaults, then __$ctor_body.
        if (info.base_info)
            emitCtorCall(info.base_info->name, "%self", /*as_base=*/true);

        // Field-default init that no construction site can do:
        //  - transport impl: the private suffix — consumers can't see it.
        //  - opaque-base class: this class's own fields — consumers can't
        //    offset past the opaque base.
        // Helper: store field `flat_idx`'s default (own-field index `own_j`).
        auto initField = [&](int flat_idx, int own_j) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct." << slid.name
                 << ", ptr %self, i32 0, i32 " << flat_idx << "\n";
            if (flat_idx >= (int)info.field_types.size()) return;
            const std::string& ft = info.field_types[flat_idx];
            std::string val;
            if (own_j >= 0 && own_j < (int)slid.fields.size()
                && slid.fields[own_j].default_val) {
                val = emitExpr(*slid.fields[own_j].default_val);
            } else {
                val = (ft == "float" || ft == "float32" || ft == "float64") ? "0.0"
                    : isIndirectType(ft) ? "null" : "0";
            }
            out_ << "    store " << llvmType(ft) << " " << val
                 << ", ptr " << gep << "\n";
        };
        if (slid.is_transport_impl) {
            for (int i = info.public_field_count; i < (int)slid.fields.size(); i++)
                initField(i, i);
        } else if (derivesFromTransportBase(slid.name)) {
            for (int j = 0; j < n_own; j++)
                initField(info.base_field_count + vptr_local + j, j);
        }

        out_ << "    musttail call void @" << slid.name << "__$ctor_body(ptr %self)\n";
        out_ << "    ret void\n";
        block_terminated_ = true;   // popScope must not emit after the ret.
        popScope();
        out_ << "}\n\n";
    }

    // === __$ctor_body ===
    // 1. set vptr (if root of virtual chain)
    // 2. call base's __$ctor (recursive descent)
    // 3. for each own slid-typed field in declaration order: call field's __$ctor
    //    (anon-tuple field: inline ctor walk over its slid slots)
    // 4. user body (implicit ctor_body, then explicit _() body)
    if (emit_ctor_pair) {
        locals_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;
        self_ptr_ = "%self";
        // Phase 4: const ctor — see __$ctor above for the rationale. The
        // synth field-init walk (base ctor, vptr, own field ctors) emits
        // direct LLVM, so const-self is invisible to it; only the user body
        // sees the rejection.
        locals_["self"].type = slid.is_const_ctor ? ("const " + slid.name) : slid.name;
        locals_["self"].reg = "%self";

        out_ << "define internal void @" << slid.name << "__$ctor_body(ptr %self) {\n";
        out_ << "entry:\n";

        pushScope();
        // The base sub-object was constructed by __$ctor before it tail-called
        // here (Itanium order: base, then our vptr/fields/body).

        // own vptr — every virtual class writes its own vtable pointer after
        // the base call, leaving the most-derived ctor's value visible
        if (info.is_virtual_class) {
            std::string vptr_gep = emitFieldGep(slid.name, "%self", 0);
            out_ << "    store ptr @_ZTV" << slid.name << ", ptr " << vptr_gep << "\n";
        }

        // 3. own slid field ctors in declaration order
        for (int j = 0; j < n_own; j++) {
            int flat_idx = info.base_field_count + vptr_local + j;
            if (flat_idx >= (int)info.field_types.size()) break;
            const std::string& ftype = info.field_types[flat_idx];
            if (slid_info_.count(ftype) && !slid_info_.at(ftype).is_empty) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct."
                     << slid.name << ", ptr %self, i32 0, i32 "
                     << flat_idx << "\n";
                emitCtorCall(ftype, gep);
            } else if (isAnonTupleType(ftype)) {
                // anon tuple: inline ctor walk over slid slots in declaration order
                auto elems = anonTupleElems(ftype);
                std::string field_gep = newTmp();
                out_ << "    " << field_gep << " = getelementptr %struct."
                     << slid.name << ", ptr %self, i32 0, i32 "
                     << flat_idx << "\n";
                for (int k = 0; k < (int)elems.size(); k++) {
                    const std::string& et = elems[k];
                    if (slid_info_.count(et) && !slid_info_.at(et).is_empty) {
                        std::string slot_gep = newTmp();
                        out_ << "    " << slot_gep << " = getelementptr "
                             << llvmType(ftype) << ", ptr " << field_gep
                             << ", i32 0, i32 " << k << "\n";
                        emitCtorCall(et, slot_gep);
                    }
                }
            }
            // primitive / pointer / inline-array: no ctor work (site init)
        }

        // 4. user body
        if (implicit_ctor_body) emitBlock(*implicit_ctor_body);
        if (!block_terminated_ && explicit_ctor_body) emitBlock(*explicit_ctor_body);

        // popScope drains this scope's dtors first — they must precede the ret.
        popScope();
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    // === __$dtor ===
    // 1. user body
    // 2. own slid-field dtors in reverse declaration order
    //    (anon-tuple field: inline reverse walk over its slid slots)
    // 3. base dtor call (if base has one)
    // Same paired emission gate as __$ctor: emit __$dtor only for explicit
    // user-paired _()/~() classes, and only in the TU that owns the body.
    // Implicit (must_inline_ctor) classes have their dtor walks inlined at
    // the site, no symbol.
    bool need_dtor_fn = info.has_explicit_ctor && info.has_dtor && has_body_here;
    if (need_dtor_fn) {
        locals_.clear();
        emitted_alloca_regs_.clear();
        tmp_counter_ = 0;
        label_counter_ = 0;
        block_terminated_ = false;
        current_slid_ = slid.name;
        self_ptr_ = "%self";
        // Phase 4: const dtor — see __$ctor above. The synth field-dtor walk
        // is direct LLVM (no AssignStmt) so const is invisible to it; only
        // the user body sees the rejection.
        locals_["self"].type = slid.is_const_dtor ? ("const " + slid.name) : slid.name;
        locals_["self"].reg = "%self";

        out_ << "define " << (isExported(slid.name + "__$dtor") ? "" : "internal ")
             << "void @" << slid.name << "__$dtor(ptr %self) {\n";
        out_ << "entry:\n";

        pushScope();
        // 1. user body
        if (dtor_body) emitBlock(*dtor_body);

        // 2. own slid field dtors in reverse declaration order (dispatcher
        //    handles must_inline_ctor / no-op cases)
        if (!block_terminated_) {
            for (int j = n_own - 1; j >= 0; j--) {
                int flat_idx = info.base_field_count + vptr_local + j;
                if (flat_idx >= (int)info.field_types.size()) continue;
                const std::string& ftype = info.field_types[flat_idx];
                if (slid_info_.count(ftype) && slid_info_.at(ftype).has_dtor) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %struct."
                         << slid.name << ", ptr %self, i32 0, i32 "
                         << flat_idx << "\n";
                    emitDtorChainCall(ftype, gep);
                } else if (isAnonTupleType(ftype)) {
                    auto elems = anonTupleElems(ftype);
                    std::string field_gep = newTmp();
                    out_ << "    " << field_gep << " = getelementptr %struct."
                         << slid.name << ", ptr %self, i32 0, i32 "
                         << flat_idx << "\n";
                    for (int k = (int)elems.size() - 1; k >= 0; k--) {
                        const std::string& et = elems[k];
                        if (slid_info_.count(et) && slid_info_.at(et).has_dtor) {
                            std::string slot_gep = newTmp();
                            out_ << "    " << slot_gep << " = getelementptr "
                                 << llvmType(ftype) << ", ptr " << field_gep
                                 << ", i32 0, i32 " << k << "\n";
                            emitDtorChainCall(et, slot_gep);
                        }
                    }
                }
            }
        }

        // 3. base dtor
        if (!block_terminated_ && info.base_info) {
            emitDtorChainCall(info.base_info->name, "%self");
        }

        // popScope drains this scope's dtors first — they must precede the ret.
        popScope();
        if (!block_terminated_) out_ << "    ret void\n";
        out_ << "}\n\n";
    }

    current_slid_ = "";
    self_ptr_ = "";

    // emit __$sizeof for every locally-complete slid (not for consumer-side
    // declarations). A class with an opaque base is not locally complete —
    // its size is unknown here; the impl TU, where the base is complete,
    // defines __$sizeof and this TU only declares it.
    if (!slid.has_trailing_ellipsis && !slid_info_[slid.name].has_private_suffix) {
        std::string linkage = isExported(slid.name + "__$sizeof") ? "" : "internal ";
        out_ << "define " << linkage << "i64 @" << slid.name << "__$sizeof() {\n";
        out_ << "entry:\n";
        out_ << "    %gep0 = getelementptr %struct." << slid.name << ", ptr null, i32 1\n";
        out_ << "    %sz0 = ptrtoint ptr %gep0 to i64\n";
        out_ << "    ret i64 %sz0\n";
        out_ << "}\n\n";
    }

    // emit __$ownbase for a class deriving from a transport base — the byte
    // offset where this class's own fields begin, behind the opaque base.
    // Defined by the impl (complete layout); consumers declare and call it to
    // place own-field accesses. Same define/declare split as __$sizeof.
    if (!slid.has_trailing_ellipsis && !slid_info_[slid.name].has_private_suffix
        && derivesFromTransportBase(slid.name)) {
        std::string linkage = isExported(slid.name + "__$ownbase") ? "" : "internal ";
        out_ << "define " << linkage << "i64 @" << slid.name << "__$ownbase() {\n";
        out_ << "entry:\n";
        out_ << "    %ob0 = getelementptr %struct." << slid.name
             << ", ptr null, i32 0, i32 " << info.base_field_count << "\n";
        out_ << "    %ob1 = ptrtoint ptr %ob0 to i64\n";
        out_ << "    ret i64 %ob1\n";
        out_ << "}\n\n";
    }
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
    // Opaque-base class (consumer view): own fields sit at a runtime offset
    // behind the opaque base. address = ptr + __$ownbase() + (static relative
    // offset of own field i within this TU's own-only %struct.<type>).
    auto sit = slid_info_.find(struct_type);
    if (sit != slid_info_.end() && sit->second.has_private_suffix
        && derivesFromTransportBase(struct_type)) {
        std::string ob = newTmp();
        out_ << "    " << ob << " = call i64 @" << struct_type << "__$ownbase()\n";
        std::string relp = newTmp();
        out_ << "    " << relp << " = getelementptr %struct." << struct_type
             << ", ptr null, i32 0, i32 " << i << "\n";
        std::string rel = newTmp();
        out_ << "    " << rel << " = ptrtoint ptr " << relp << " to i64\n";
        std::string off = newTmp();
        out_ << "    " << off << " = add i64 " << ob << ", " << rel << "\n";
        std::string gep = newTmp();
        out_ << "    " << gep << " = getelementptr i8, ptr " << ptr
             << ", i64 " << off << "\n";
        return gep;
    }
    std::string gep = newTmp();
    std::string leading = slid_info_.count(struct_type)
        ? ("%struct." + struct_type)
        : llvmType(struct_type);
    out_ << "    " << gep << " = getelementptr " << leading
         << ", ptr " << ptr << ", i32 0, i32 " << i << "\n";
    return gep;
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
            std::string want = canonicalType(elem_type + "^");
            bool empty = slid_info_[elem_type].is_empty;
            for (auto& [m, pt, _pm, _pmt, _fid] : oit->second) {
                if (pt.size() == 1 && canonicalType(pt[0]) == want) {
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
    // is_init propagates only through anon-tuple walks. When struct_type is a
    // slid, its own __$ctor (emitted before reaching this default-walk fallback)
    // already constructed every field — re-constructing them here would print
    // a second ctor for slid-typed embedded fields. When struct_type is a
    // tuple, no __$ctor exists and each slid slot needs to default-construct
    // itself before op= copies/moves into it.
    bool slot_is_init = is_init && isAnonTupleType(struct_type);
    auto fields = fieldTypesOf(struct_type);
    for (int i = 0; i < (int)fields.size(); i++) {
        const std::string& fslids = fields[i];
        std::string src_gep = emitFieldGep(struct_type, src_ptr, i);
        std::string dst_gep = emitFieldGep(struct_type, dst_ptr, i);
        if (slid_info_.count(fslids) || isAnonTupleType(fslids)) {
            emitSlidSlotAssign(fslids, dst_gep, src_gep, is_move, slot_is_init);
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
                    emitSlidSlotAssign(elem_slids, d_ep, s_ep, is_move, slot_is_init);
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
            std::string want = canonicalType(elem_type + "^");
            bool empty = slid_info_[elem_type].is_empty;
            for (auto& [m, pt, _pm, _pmt, _fid] : oit->second) {
                if (pt.size() == 1 && canonicalType(pt[0]) == want) {
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
            error(std::string("Swap of inline-array fields is not yet supported (field type '"
                + fslids + "')."));
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
        // slid slot: dispatch user op<op>(Elem^, Elem^).
        // Compare canonically (canonicalType strips const + default-const
        // paren wrap) so a `(const Elem)^` param matches the want of `Elem^`.
        if (slid_info_.count(ft)) {
            std::string op_base = ft + "__op" + op;
            auto oit = method_overloads_.find(op_base);
            std::string mangled;
            std::string want = canonicalType(ft + "^");
            if (oit != method_overloads_.end()) {
                for (auto& [m, pt, _pm, _pmt, _fid] : oit->second) {
                    if (pt.size() == 2
                        && canonicalType(pt[0]) == want
                        && canonicalType(pt[1]) == want) {
                        mangled = m; break;
                    }
                }
            }
            if (mangled.empty())
                error(std::string("Class element type '" + ft
                    + "' has no op" + op + "(" + ft + "^, " + ft + "^)."));
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
        else error(std::string("Unsupported elementwise op '" + op + "'."));
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
            std::string want_left = canonicalType(ft + "^");
            std::string want_right = canonicalType(scalar_slids);
            if (oit != method_overloads_.end()) {
                for (auto& [m, pt, _pm, _pmt, _fid] : oit->second) {
                    if (pt.size() == 2
                        && canonicalType(pt[0]) == want_left
                        && canonicalType(pt[1]) == want_right) {
                        mangled = m; break;
                    }
                }
            }
            if (mangled.empty())
                error(std::string("Class slot '" + ft + "' has no op"
                    + op + "(" + ft + "^, " + scalar_slids + ") for broadcast."));
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
            error(std::string("Tuple slot type '" + ft
                + "' does not match scalar type '" + scalar_slids + "' in broadcast."));
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
        else error(std::string("Unsupported broadcast op '" + op + "'."));
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
                error(std::string("Cannot instantiate pure virtual class '" + slid_name
                    + "': method '" + slot.method_name + "' is not defined."));
        }
    }
    std::string reg = newTmp();
    if (info.has_private_suffix) {
        std::string sz = newTmp();
        out_ << "    " << sz << " = call i64 @" << slid_name << "__$sizeof()\n";
        out_ << "    " << reg << " = alloca i8, i64 " << sz << "\n";
    } else {
        out_ << "    " << reg << " = alloca %struct." << slid_name << "\n";
    }
    // Field init + ctor call go through the unified emit path.
    emitConstructAtPtrs(slid_name, reg, {}, {});
    if (hasDtorInChain(slid_name))
        pending_temp_dtors_.push_back({reg, slid_name});
    return reg;
}

std::string Codegen::emitRawSlidAlloca(const std::string& slid_name) {
    auto& info = slid_info_[slid_name];
    std::string reg = newTmp();
    if (info.has_private_suffix) {
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

void Codegen::emitInitFieldsAtPtrs(const std::string& stype, const std::string& ptr,
                                   const std::vector<const Expr*>& args,
                                   const std::vector<const Expr*>& overrides) {
    bool is_slid = slid_info_.count(stype) != 0;
    bool is_tuple = isAnonTupleType(stype);
    if (!is_slid && !is_tuple) return;

    // An initializer list cannot place a derived class's fields when a base in
    // the chain has private/hidden fields — the layout past the opaque base is
    // not known at the construction site.
    if (is_slid && (!args.empty() || !overrides.empty())) {
        auto bit = slid_info_.find(stype);
        if (bit != slid_info_.end())
            for (const SlidInfo* b = bit->second.base_info; b; b = b->base_info)
                if (b->has_private_suffix)
                    error("An initializer list cannot be used with '" + stype
                        + "' because its base class '" + b->name
                        + "' has private fields");
    }

    auto fields = fieldTypesOf(stype);
    int nfields = (int)fields.size();
    if ((int)args.size() > nfields) {
        std::string msg = "Too many initializers for '" + stype + "': it has "
            + std::to_string(nfields) + " fields, got " + std::to_string(args.size()) + ".";
        if (is_slid) {
            auto& sinfo = slid_info_[stype];
            errorWithNote(msg, sinfo.name_file_id, sinfo.name_tok,
                          "'" + stype + "' declared here.");
        }
        error(msg);
    }
    if ((int)overrides.size() > nfields) {
        std::string msg = "Too many tuple values for '" + stype + "': it has "
            + std::to_string(nfields) + " fields, got " + std::to_string(overrides.size()) + ".";
        if (is_slid) {
            auto& sinfo = slid_info_[stype];
            errorWithNote(msg, sinfo.name_file_id, sinfo.name_tok,
                          "'" + stype + "' declared here.");
        }
        error(msg);
    }

    auto slidDefFor = [&](const std::string& name) -> const SlidDef* {
        for (auto& s : program_.slids) if (s.name == name) return &s;
        auto it = concrete_slid_template_defs_.find(name);
        if (it != concrete_slid_template_defs_.end()) return &it->second;
        return nullptr;
    };

    auto initFieldFromExpr = [&](const std::string& ftype, const std::string& gep,
                                 const Expr* arg_expr) {
        // PPID: each ctor arg slot is its own phrase. Pre fires at entry; post
        // queued during eval drains at the slot's exit.
        pushPostIncQueue();
        if (arg_expr) emitPrePass(*arg_expr);
        struct FlushOnExit {
            Codegen* cg;
            ~FlushOnExit() { cg->flushPostIncQueue(); }
        } _flush{this};
        if (slid_info_.count(ftype)) {
            if (!arg_expr) {
                // Default-init the field's primitives only. The outer
                // __$ctor_body will call this field's __$ctor to construct it.
                emitInitFieldsAtPtrs(ftype, gep, {}, {});
            } else if (inferSlidType(*arg_expr) == ftype) {
                // Matching slid arg: in tuple context the slot has no outer
                // __$ctor (tuples don't have one), so default-construct the slot
                // before op=. In class-field context the outer __$ctor_body
                // will call the field's __$ctor, so skip default-construct.
                std::string src = emitExpr(*arg_expr);
                emitSlidSlotAssign(ftype, gep, src, /*is_move=*/false,
                                   /*is_init=*/is_tuple);
            } else {
                // Compound init: route arg into the field's primitives.
                // Tuple-shape arg → unpack its elements as positional
                // overrides into the field's slot list; non-tuple arg →
                // single-value promotion (feed as the first ctor arg, the
                // rest fall to the field's defaults).
                if (auto* te = dynamic_cast<const TupleExpr*>(arg_expr)) {
                    std::vector<const Expr*> ov;
                    ov.reserve(te->values.size());
                    for (auto& v : te->values) ov.push_back(v.get());
                    emitInitFieldsAtPtrs(ftype, gep, {}, ov);
                } else {
                    std::vector<const Expr*> one{ arg_expr };
                    emitInitFieldsAtPtrs(ftype, gep, one, {});
                }
            }
            return;
        }
        std::string val;
        if (arg_expr) {
            val = emitExpr(*arg_expr);
            // Leaf type check: reject incompatible source types before the
            // store. Slids-level type-equality short-circuits (covers anon-
            // tuples whose LLVM type may format differently). For mismatched
            // primitives, width-coerce integers; reject everything else.
            std::string src_slids = inferSlidType(*arg_expr);
            if (src_slids != ftype) {
                std::string src_t = exprLlvmType(*arg_expr);
                std::string dst_t = llvmType(ftype);
                if (src_t != dst_t) {
                    static const std::map<std::string,int> rank =
                        {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                    auto sit = rank.find(src_t), dit = rank.find(dst_t);
                    if (sit != rank.end() && dit != rank.end()) {
                        std::string coerced = newTmp();
                        if (dit->second > sit->second)
                            out_ << "    " << coerced << " = sext " << src_t
                                 << " " << val << " to " << dst_t << "\n";
                        else
                            out_ << "    " << coerced << " = trunc " << src_t
                                 << " " << val << " to " << dst_t << "\n";
                        val = coerced;
                    } else if (src_t == "double" && dst_t == "float") {
                        std::string coerced = newTmp();
                        out_ << "    " << coerced << " = fptrunc double "
                             << val << " to float\n";
                        val = coerced;
                    } else if (src_t == "float" && dst_t == "double") {
                        std::string coerced = newTmp();
                        out_ << "    " << coerced << " = fpext float "
                             << val << " to double\n";
                        val = coerced;
                    } else {
                        error(std::string("Type mismatch: cannot assign '"
                            + src_slids + "' to '" + ftype + "'"));
                    }
                }
            }
        } else {
            val = isInlineArrayType(ftype) ? "zeroinitializer"
                : isIndirectType(ftype) ? "null"
                : (ftype == "float" || ftype == "float32" || ftype == "float64") ? "0.0"
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

    // slid: walk inheritance chain base→derived. Init only — vptr setting
    // and ctor calls live in __$ctor_body.
    auto chain = chainOf(stype);
    bool root_virtual = !slid_info_.at(stype).is_virtual_class ? false : true;
    int args_vptr_skew = root_virtual ? 1 : 0;
    for (SlidInfo* c_info : chain) {
        const SlidDef* c_def = slidDefFor(c_info->name);
        bool owns_vptr = c_info->is_virtual_class && c_info->base_info == nullptr;
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
}

void Codegen::emitConstructAtPtrs(const std::string& stype, const std::string& ptr,
                                  const std::vector<const Expr*>& args,
                                  const std::vector<const Expr*>& overrides) {
    // Field-init half (no ctor call). For tuples, this is the entire job.
    emitInitFieldsAtPtrs(stype, ptr, args, overrides);

    // Ctor call half (slid types only). Single call; the function handles its
    // own vptr, base call, field ctors, and user body internally.
    emitCtorCall(stype, ptr);
}

void Codegen::emitCtorCall(const std::string& class_name, const std::string& ptr,
                           bool as_base) {
    auto sit = slid_info_.find(class_name);
    if (sit == slid_info_.end()) return;       // tuple or unknown — no call
    auto& info = sit->second;
    if (info.must_inline_ctor) {
        // Implicit class (no user _()/~()) — no __$ctor symbol exists; inline
        // the synthesized walk at this site.
        emitInlineCtorWalk(class_name, ptr);
        return;
    }
    if (!info.has_explicit_ctor) return;        // no work to do
    // A base sub-object must be fully constructed (its own field defaults
    // applied); only a direct construction site uses the body-only form.
    bool use_body = info.is_transport_impl && !as_base;
    out_ << "    call void @" << class_name << "__"
         << (use_body ? "$ctor_body" : "$ctor") << "(ptr " << ptr << ")\n";
}

void Codegen::emitInlineCtorWalk(const std::string& class_name,
                                 const std::string& ptr) {
    auto sit = slid_info_.find(class_name);
    if (sit == slid_info_.end()) return;
    auto& info = sit->second;
    // 1. base call first (recursive dispatch — may inline or call)
    if (info.base_info) {
        emitCtorCall(info.base_info->name, ptr, /*as_base=*/true);
    }
    // 2. own vptr — every virtual class writes its own vtable pointer
    //    after the base call (Itanium ABI)
    if (info.is_virtual_class) {
        std::string vptr_gep = emitFieldGep(class_name, ptr, 0);
        out_ << "    store ptr @_ZTV" << class_name << ", ptr " << vptr_gep << "\n";
    }
    // 3. own slid field ctors in declaration order
    bool owns_vptr = info.is_virtual_class && info.base_info == nullptr;
    int vptr_local = owns_vptr ? 1 : 0;
    int n_own = info.own_field_count - vptr_local;
    for (int j = 0; j < n_own; j++) {
        int flat_idx = info.base_field_count + vptr_local + j;
        if (flat_idx >= (int)info.field_types.size()) break;
        const std::string& ftype = info.field_types[flat_idx];
        if (slid_info_.count(ftype) && !slid_info_.at(ftype).is_empty) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct."
                 << class_name << ", ptr " << ptr << ", i32 0, i32 "
                 << flat_idx << "\n";
            emitCtorCall(ftype, gep);
        } else if (isAnonTupleType(ftype)) {
            auto elems = anonTupleElems(ftype);
            std::string field_gep = newTmp();
            out_ << "    " << field_gep << " = getelementptr %struct."
                 << class_name << ", ptr " << ptr << ", i32 0, i32 "
                 << flat_idx << "\n";
            for (int k = 0; k < (int)elems.size(); k++) {
                const std::string& et = elems[k];
                if (slid_info_.count(et) && !slid_info_.at(et).is_empty) {
                    std::string slot_gep = newTmp();
                    out_ << "    " << slot_gep << " = getelementptr "
                         << llvmType(ftype) << ", ptr " << field_gep
                         << ", i32 0, i32 " << k << "\n";
                    emitCtorCall(et, slot_gep);
                }
            }
        }
        // primitive / pointer / inline-array: site init handled it
    }
}

void Codegen::emitInlineDtorWalk(const std::string& class_name,
                                 const std::string& ptr) {
    auto sit = slid_info_.find(class_name);
    if (sit == slid_info_.end()) return;
    auto& info = sit->second;
    bool owns_vptr = info.is_virtual_class && info.base_info == nullptr;
    int vptr_local = owns_vptr ? 1 : 0;
    int n_own = info.own_field_count - vptr_local;
    // 1. own slid field dtors in reverse declaration order
    for (int j = n_own - 1; j >= 0; j--) {
        int flat_idx = info.base_field_count + vptr_local + j;
        if (flat_idx >= (int)info.field_types.size()) continue;
        const std::string& ftype = info.field_types[flat_idx];
        if (slid_info_.count(ftype) && slid_info_.at(ftype).has_dtor) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct."
                 << class_name << ", ptr " << ptr << ", i32 0, i32 "
                 << flat_idx << "\n";
            emitDtorChainCall(ftype, gep);
        } else if (isAnonTupleType(ftype)) {
            auto elems = anonTupleElems(ftype);
            std::string field_gep = newTmp();
            out_ << "    " << field_gep << " = getelementptr %struct."
                 << class_name << ", ptr " << ptr << ", i32 0, i32 "
                 << flat_idx << "\n";
            for (int k = (int)elems.size() - 1; k >= 0; k--) {
                const std::string& et = elems[k];
                if (slid_info_.count(et) && slid_info_.at(et).has_dtor) {
                    std::string slot_gep = newTmp();
                    out_ << "    " << slot_gep << " = getelementptr "
                         << llvmType(ftype) << ", ptr " << field_gep
                         << ", i32 0, i32 " << k << "\n";
                    emitDtorChainCall(et, slot_gep);
                }
            }
        }
    }
    // 2. base dtor (recursive dispatch)
    if (info.base_info && info.base_info->has_dtor) {
        emitDtorChainCall(info.base_info->name, ptr);
    }
}
