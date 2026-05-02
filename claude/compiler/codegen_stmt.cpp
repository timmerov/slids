#include "codegen.h"
#include "source_map.h"
#include "codegen_helpers.h"
#include <sstream>
#include <functional>
#include <stdexcept>

void Codegen::emitDestructure(
    const std::vector<std::pair<std::string,std::string>>& targets,
    const Expr& init) {
    // tuple literal rhs: desugar (t1 a, t2 b) = (x, y); into t1 a = x; t2 b = y;
    if (auto* te = dynamic_cast<const TupleExpr*>(&init)) {
        if (te->values.size() != targets.size())
            error(std::string("destructure size mismatch: "
                + std::to_string(targets.size()) + " targets, "
                + std::to_string(te->values.size()) + " values"));
        for (int i = 0; i < (int)targets.size(); i++) {
            auto& [type, name] = targets[i];
            if (name.empty()) continue; // empty slot — skip this element
            std::string eff_type = type.empty() ? inferSlidType(*te->values[i]) : type;
            // if name is already in scope AND no explicit type: reassign the existing local
            bool reassign = type.empty() && locals_.count(name) > 0;
            if (reassign) {
                auto ltit = local_types_.find(name);
                if (ltit == local_types_.end() || ltit->second != eff_type)
                    error(std::string("destructure reassign: '" + name
                        + "' has type '" + (ltit==local_types_.end()?"<unknown>":ltit->second)
                        + "' but source element is '" + eff_type + "'"));
            }
            std::string reg = reassign ? locals_[name] : uniqueAllocaReg(name);
            // slid or anon-tuple target: init path (alloca + ctor) unless reassigning.
            if (slid_info_.count(eff_type) || isAnonTupleType(eff_type)) {
                requireCompatibleInit(eff_type, *te->values[i]);
                std::string src_type = inferSlidType(*te->values[i]);
                if (src_type != eff_type)
                    errorAtNode(*te->values[i], "cannot initialize '" + eff_type
                        + "' from value of type '" + src_type + "'");
                if (!reassign)
                    out_ << "    " << reg << " = alloca " << llvmType(eff_type) << "\n";
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                    auto lit = locals_.find(ve->name);
                    src_ptr = (lit != locals_.end()) ? lit->second : emitExpr(*te->values[i]);
                } else {
                    src_ptr = emitExpr(*te->values[i]);
                }
                bool is_move = isFreshSlidTemp(*te->values[i]);
                emitSlidSlotAssign(eff_type, reg, src_ptr, is_move, /*is_init=*/!reassign);
                if (!reassign) {
                    locals_[name] = reg;
                    local_types_[name] = eff_type;
                    if (slid_info_.count(eff_type) && hasDtorInChain(eff_type))
                        dtor_vars_.push_back({name, eff_type});
                }
                continue;
            }
            requireCompatibleInit(eff_type, *te->values[i]);
            std::string llvm_t = llvmType(eff_type);
            if (!reassign)
                out_ << "    " << reg << " = alloca " << llvm_t << "\n";
            std::string val = emitExpr(*te->values[i]);
            std::string src_t = exprLlvmType(*te->values[i]);
            if (src_t != llvm_t) {
                static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                auto sit = rank.find(src_t), dit = rank.find(llvm_t);
                if (sit != rank.end() && dit != rank.end()) {
                    std::string coerced = newTmp();
                    if (dit->second > sit->second)
                        out_ << "    " << coerced << " = sext " << src_t << " " << val << " to " << llvm_t << "\n";
                    else
                        out_ << "    " << coerced << " = trunc " << src_t << " " << val << " to " << llvm_t << "\n";
                    val = coerced;
                } else {
                    errorAtNode(*te->values[i], "cannot initialize '" + eff_type
                        + "' from value of type '" + inferSlidType(*te->values[i]) + "'");
                }
            }
            out_ << "    store " << llvm_t << " " << val << ", ptr " << reg << "\n";
            if (!reassign) {
                locals_[name] = reg;
                local_types_[name] = eff_type;
            }
        }
        return;
    }
    // VarExpr of anon-tuple type: per-slot GEP; slid/anon-tuple slots move element-wise,
    // scalar slots take the extractvalue + store path (IR byte-identical to pre-refactor).
    if (auto* ve = dynamic_cast<const VarExpr*>(&init)) {
        auto tit = local_types_.find(ve->name);
        if (tit != local_types_.end() && isAnonTupleType(tit->second)) {
            auto elems = anonTupleElems(tit->second);
            if (elems.size() != targets.size())
                error(std::string("destructure size mismatch: "
                    + std::to_string(targets.size()) + " targets, "
                    + std::to_string(elems.size()) + " elements in '" + ve->name + "'"));
            const std::string& src_type = tit->second;
            const std::string& src_ptr = locals_[ve->name];
            // Any slid/anon-tuple slot? If so, we need per-slot GEP rather than a whole-struct load.
            bool any_struct_slot = false;
            for (auto& e : elems)
                if (slid_info_.count(e) || isAnonTupleType(e)) { any_struct_slot = true; break; }
            std::string struct_llvm = llvmType(src_type);
            std::string loaded;
            if (!any_struct_slot) {
                loaded = newTmp();
                out_ << "    " << loaded << " = load " << struct_llvm << ", ptr " << src_ptr << "\n";
            }
            for (int i = 0; i < (int)targets.size(); i++) {
                auto& [type, name] = targets[i];
                if (name.empty()) continue; // empty slot
                const std::string& elem_type = elems[i];
                std::string eff_type = type.empty() ? elem_type : type;
                bool reassign = type.empty() && locals_.count(name) > 0;
                if (reassign) {
                    auto ltit = local_types_.find(name);
                    if (ltit == local_types_.end() || ltit->second != eff_type)
                        error(std::string("destructure reassign: '" + name
                            + "' has type '" + (ltit==local_types_.end()?"<unknown>":ltit->second)
                            + "' but source element is '" + eff_type + "'"));
                }
                std::string reg = reassign ? locals_[name] : uniqueAllocaReg(name);
                if (slid_info_.count(elem_type) || isAnonTupleType(elem_type)) {
                    if (!type.empty() && type != elem_type)
                        error(std::string("cannot initialize '" + eff_type
                            + "' from value of type '" + elem_type + "'"));
                    if (!reassign)
                        out_ << "    " << reg << " = alloca " << llvmType(elem_type) << "\n";
                    std::string src_gep = emitFieldGep(src_type, src_ptr, i);
                    emitSlidSlotAssign(elem_type, reg, src_gep, /*is_move=*/true, /*is_init=*/!reassign);
                    if (!reassign) {
                        locals_[name] = reg;
                        local_types_[name] = eff_type;
                        if (slid_info_.count(elem_type) && hasDtorInChain(elem_type))
                            dtor_vars_.push_back({name, elem_type});
                    }
                    continue;
                }
                std::string elem_llvm = llvmType(elem_type);
                std::string dst_llvm = llvmType(eff_type);
                std::string extracted = newTmp();
                out_ << "    " << extracted << " = extractvalue " << struct_llvm
                     << " " << loaded << ", " << i << "\n";
                if (elem_llvm != dst_llvm) {
                    static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                    auto sit = rank.find(elem_llvm), dit = rank.find(dst_llvm);
                    if (sit != rank.end() && dit != rank.end()) {
                        std::string coerced = newTmp();
                        if (dit->second > sit->second)
                            out_ << "    " << coerced << " = sext " << elem_llvm << " " << extracted << " to " << dst_llvm << "\n";
                        else
                            out_ << "    " << coerced << " = trunc " << elem_llvm << " " << extracted << " to " << dst_llvm << "\n";
                        extracted = coerced;
                    } else {
                        error(std::string("cannot initialize '" + eff_type
                            + "' from value of type '" + elem_type + "'"));
                    }
                }
                if (!reassign)
                    out_ << "    " << reg << " = alloca " << dst_llvm << "\n";
                out_ << "    store " << dst_llvm << " " << extracted << ", ptr " << reg << "\n";
                if (!reassign) {
                    locals_[name] = reg;
                    local_types_[name] = eff_type;
                }
            }
            return;
        }
    }
    std::string result = emitExpr(init);
    std::string tuple_type = exprLlvmType(init);
    // if the source produces an anon-tuple (e.g. tuple-returning call), use its
    // element types to fill in bare-name targets missing an explicit type.
    std::string src_slids = inferSlidType(init);
    std::vector<std::string> src_elems;
    if (isAnonTupleType(src_slids)) src_elems = anonTupleElems(src_slids);
    for (int i = 0; i < (int)targets.size(); i++) {
        auto& [type, name] = targets[i];
        if (name.empty()) continue; // empty slot
        std::string eff_type = type;
        if (eff_type.empty() && i < (int)src_elems.size())
            eff_type = src_elems[i];
        if (eff_type.empty())
            error(std::string("destructure type inference from non-tuple-variable source not supported yet"));
        bool reassign = type.empty() && locals_.count(name) > 0;
        if (reassign) {
            auto ltit = local_types_.find(name);
            if (ltit == local_types_.end() || ltit->second != eff_type)
                error(std::string("destructure reassign: '" + name
                    + "' has type '" + (ltit==local_types_.end()?"<unknown>":ltit->second)
                    + "' but source element is '" + eff_type + "'"));
        }
        std::string llvm_t = llvmType(eff_type);
        std::string reg = reassign ? locals_[name] : uniqueAllocaReg(name);
        if (!reassign)
            out_ << "    " << reg << " = alloca " << llvm_t << "\n";
        std::string extracted = newTmp();
        out_ << "    " << extracted << " = extractvalue " << tuple_type << " " << result << ", " << i << "\n";
        out_ << "    store " << llvm_t << " " << extracted << ", ptr " << reg << "\n";
        if (!reassign) {
            locals_[name] = reg;
            local_types_[name] = eff_type;
        }
    }
}

// Defined in codegen_template.cpp — single source of truth for AST cloning.
// The desugar fallback in CompoundAssignStmt passes an empty subst.
std::unique_ptr<Expr> cloneExpr(const Expr& expr,
                                 const std::map<std::string, std::string>& subst);

static std::unique_ptr<Expr> cloneExpr(const Expr& e) {
    static const std::map<std::string, std::string> kEmpty;
    return cloneExpr(e, kEmpty);
}

void Codegen::emitStmt(const Stmt& stmt) {
    EmitGuard _g(*this, stmt.file_id, stmt.tok);
    // null out the storage location of a source expression after a move
    auto emitNullOut = [&](const Expr& src) {
        if (auto* ve = dynamic_cast<const VarExpr*>(&src)) {
            auto it = locals_.find(ve->name);
            if (it != locals_.end())
                out_ << "    store ptr null, ptr " << it->second << "\n";
        } else if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&src)) {
            std::string obj_ptr;
            std::string stype;
            if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
                auto it = locals_.find(ve2->name);
                auto tit = local_types_.find(ve2->name);
                if (it != locals_.end()) obj_ptr = it->second;
                if (tit != local_types_.end()) stype = tit->second;
            } else if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
                if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto it = locals_.find(ve2->name);
                    auto tit = local_types_.find(ve2->name);
                    if (it != locals_.end()) {
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load ptr, ptr " << it->second << "\n";
                        obj_ptr = loaded;
                    }
                    if (tit != local_types_.end()) {
                        stype = tit->second;
                        if (!stype.empty() && stype.back() == '^') stype.pop_back();
                        else if (stype.size() >= 2 && stype.substr(stype.size()-2) == "[]")
                            stype = stype.substr(0, stype.size()-2);
                    }
                }
            }
            if (!obj_ptr.empty() && slid_info_.count(stype)) {
                auto fit = slid_info_[stype].field_index.find(fa->field);
                if (fit != slid_info_[stype].field_index.end()) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %struct." << stype
                         << ", ptr " << obj_ptr << ", i32 0, i32 " << fit->second << "\n";
                    out_ << "    store ptr null, ptr " << gep << "\n";
                }
            }
        }
    };


    if (auto* ds = dynamic_cast<const DeleteStmt*>(&stmt)) {
        std::string ptr_val = emitExpr(*ds->operand);

        // determine element type of the pointer
        std::string ptr_type = inferSlidType(*ds->operand);
        bool is_array = ptr_type.size() >= 2 && ptr_type.substr(ptr_type.size() - 2) == "[]";
        std::string elem_type = ptr_type;
        if (is_array)
            elem_type = elem_type.substr(0, elem_type.size() - 2);
        else if (!elem_type.empty() && elem_type.back() == '^')
            elem_type.pop_back();

        bool is_slid = slid_info_.count(elem_type) > 0;

        if (is_array && is_slid) {
            // array of user structs: count stored 8 bytes before the data pointer
            // guard with null check — free(null) is safe but reading ptr-8 is not
            std::string null_lbl = newLabel("del_null");
            std::string body_lbl_outer = newLabel("del_body");
            std::string end_lbl_outer  = newLabel("del_end");
            std::string null_cmp = newTmp();
            out_ << "    " << null_cmp << " = icmp ne ptr " << ptr_val << ", null\n";
            out_ << "    br i1 " << null_cmp << ", label %" << body_lbl_outer
                 << ", label %" << end_lbl_outer << "\n";
            block_terminated_ = false;
            out_ << body_lbl_outer << ":\n";

            auto& info = slid_info_[elem_type];
            std::string hdr = newTmp();
            out_ << "    " << hdr << " = getelementptr i8, ptr " << ptr_val << ", i64 -8\n";
            if (info.has_dtor) {
                std::string count = newTmp();
                out_ << "    " << count << " = load i64, ptr " << hdr << "\n";
                // call dtor in reverse order
                std::string idx_reg = newTmp();
                out_ << "    " << idx_reg << " = alloca i64\n";
                out_ << "    store i64 " << count << ", ptr " << idx_reg << "\n";

                std::string cond_lbl = newLabel("del_dtor_cond");
                std::string dtor_body_lbl = newLabel("del_dtor_body");
                std::string dtor_end_lbl  = newLabel("del_dtor_end");

                out_ << "    br label %" << cond_lbl << "\n";
                block_terminated_ = false;
                out_ << cond_lbl << ":\n";
                std::string idx = newTmp();
                out_ << "    " << idx << " = load i64, ptr " << idx_reg << "\n";
                std::string cmp = newTmp();
                out_ << "    " << cmp << " = icmp ugt i64 " << idx << ", 0\n";
                out_ << "    br i1 " << cmp << ", label %" << dtor_body_lbl << ", label %" << dtor_end_lbl << "\n";

                block_terminated_ = false;
                out_ << dtor_body_lbl << ":\n";
                std::string idx_prev = newTmp();
                out_ << "    " << idx_prev << " = sub i64 " << idx << ", 1\n";
                out_ << "    store i64 " << idx_prev << ", ptr " << idx_reg << "\n";
                std::string elem = newTmp();
                out_ << "    " << elem << " = getelementptr %struct." << elem_type
                     << ", ptr " << ptr_val << ", i64 " << idx_prev << "\n";
                emitDtorChainCall(elem_type, elem);
                out_ << "    br label %" << cond_lbl << "\n";

                block_terminated_ = false;
                out_ << dtor_end_lbl << ":\n";
            }
            out_ << "    call void @free(ptr " << hdr << ")\n";
            out_ << "    br label %" << end_lbl_outer << "\n";
            block_terminated_ = false;
            out_ << end_lbl_outer << ":\n";
        } else {
            if (is_slid) {
                // Single dispatcher call — the dtor function (or inline walk)
                // chains to its base internally.
                emitDtorChainCall(elem_type, ptr_val);
            }
            out_ << "    call void @free(ptr " << ptr_val << ")\n";
        }
        // nullify the pointer variable so it is left in a valid (null) state
        if (auto* ve = dynamic_cast<const VarExpr*>(ds->operand.get())) {
            auto it = locals_.find(ve->name);
            if (it != locals_.end()) {
                out_ << "    store ptr null, ptr " << it->second << "\n";
            } else if (!current_slid_.empty()) {
                // bare field name inside a method: GEP through self and store null
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve->name);
                if (fit != info.field_index.end()) {
                    std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                         << ", ptr " << self << ", i32 0, i32 " << fit->second << "\n";
                    out_ << "    store ptr null, ptr " << gep << "\n";
                }
            }
        } else if (auto* fa = dynamic_cast<const FieldAccessExpr*>(ds->operand.get())) {
            // delete obj.field_ — write null back through the field GEP
            // Re-emit the GEP for the field and store null
            std::string self;
            if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
                if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto it = locals_.find(ve2->name);
                    if (it != locals_.end()) {
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load ptr, ptr " << it->second << "\n";
                        self = loaded;
                    }
                }
            } else if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
                auto it = locals_.find(ve2->name);
                if (it != locals_.end()) self = it->second;
            }
            if (!self.empty() && !current_slid_.empty()) {
                // find the slid type for fa->object
                std::string stype = current_slid_;
                if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
                    auto tit = local_types_.find(ve2->name);
                    if (tit != local_types_.end()) stype = tit->second;
                } else if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
                    if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                        auto tit = local_types_.find(ve2->name);
                        if (tit != local_types_.end()) {
                            std::string t = tit->second;
                            if (!t.empty() && (t.back() == '^' || (t.size()>=2 && t.substr(t.size()-2)=="[]")))
                                t = t.substr(0, t.find_first_of("^["));
                            stype = t;
                        }
                    }
                }
                auto sit = slid_info_.find(stype);
                if (sit != slid_info_.end()) {
                    auto fit = sit->second.field_index.find(fa->field);
                    if (fit != sit->second.field_index.end()) {
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr %struct." << stype
                             << ", ptr " << self << ", i32 0, i32 " << fit->second << "\n";
                        out_ << "    store ptr null, ptr " << gep << "\n";
                    }
                }
            }
        }
        return;
    }

    if (auto* arr = dynamic_cast<const ArrayDeclStmt*>(&stmt)) {
        int total = 1;
        for (int d : arr->dims) total *= d;
        std::string reg = "%arr_" + arr->name;
        std::string elt = llvmType(arr->elem_type);
        out_ << "    " << reg << " = alloca [" << total << " x " << elt << "]\n";
        ArrayInfo ainfo;
        ainfo.elem_type = arr->elem_type;
        ainfo.dims = arr->dims;
        ainfo.alloca_reg = reg;
        array_info_[arr->name] = ainfo;
        parent_array_info_[arr->name] = ainfo;
        locals_[arr->name] = reg; // allow array to be captured by nested functions
        // store initializer values
        for (int i = 0; i < (int)arr->init_values.size(); i++) {
            std::string val = emitExpr(*arr->init_values[i]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << total << " x " << elt << "], ptr "
                 << reg << ", i32 0, i32 " << i << "\n";
            out_ << "    store " << elt << " " << val << ", ptr " << gep << "\n";
        }
        return;
    }

    if (auto* decl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        // resolve inferred type (empty string means x = expr; with no explicit type)
        std::string inferred;
        if (decl->type.empty()) {
            if (!decl->init)
                error(std::string("inferred variable declaration requires initializer"));
            inferred = inferSlidType(*decl->init);
        }
        const std::string& eff_type = decl->type.empty() ? inferred : decl->type;

        // class instantiation
        if (slid_info_.count(eff_type)) {
            auto& info = slid_info_[eff_type];
            if (info.is_virtual_class) {
                for (auto& slot : info.vtable) {
                    if (slot.is_pure)
                        error(std::string("cannot instantiate pure virtual class '" + eff_type
                            + "': method '" + slot.method_name + "' is not defined"));
                }
            }

            // identity optimization: when init produces a fresh temp of the same type,
            // adopt that temp as the declared variable — no separate alloca, ctor, or copy.
            // Phase 1 (direct op+ overload) is excluded because it already emits into the
            // target alloca directly and is more efficient.
            if (decl->init && decl->ctor_args.empty() && !decl->is_move) {
                bool phase1_handles = false;
                bool is_empty_plus = false;  // Type + rhs pattern: use op+= instead
                if (auto* be = dynamic_cast<const BinaryExpr*>(decl->init.get())) {
                    // detect "EffType + rhs" — left side is the type name itself (not a variable)
                    if (be->op == "+") {
                        if (auto* lve = dynamic_cast<const VarExpr*>(be->left.get())) {
                            if (lve->name == eff_type && !locals_.count(lve->name)) {
                                // check that op+= exists for the rhs
                                std::string compound_base = eff_type + "__op+=";
                                std::string mangled = resolveSingleArgOverload(compound_base, *be->right);
                                if (!mangled.empty())
                                    is_empty_plus = true;
                            }
                        }
                    }
                    if (!is_empty_plus)
                        phase1_handles = !resolveOperatorOverload(be->op, *be->left, *be->right).empty();
                }
                if (!phase1_handles && !is_empty_plus && isFreshSlidTemp(*decl->init)
                        && exprSlidType(*decl->init) == eff_type) {
                    std::string temp_ptr = emitExpr(*decl->init);
                    locals_[decl->name] = temp_ptr;
                    local_types_[decl->name] = eff_type;
                    // consume-the-temp: ownership transfers from the temp to this decl's
                    // scope-exit dtor. Unregister the pending temp entry to avoid double-dtor.
                    for (auto it = pending_temp_dtors_.begin();
                         it != pending_temp_dtors_.end(); ++it) {
                        if (it->first == temp_ptr) { pending_temp_dtors_.erase(it); break; }
                    }
                    if (hasDtorInChain(eff_type))
                        dtor_vars_.push_back({decl->name, eff_type});
                    return;
                }
                if (is_empty_plus) {
                    // emit: alloca, default-construct, then op+=(rhs) — no empty temp
                    auto* be = dynamic_cast<const BinaryExpr*>(decl->init.get());
                    std::string reg = uniqueAllocaReg(decl->name);
                    if (info.has_private_suffix) {
                        std::string sz = newTmp();
                        out_ << "    " << sz << " = call i64 @" << eff_type << "__$sizeof()\n";
                        out_ << "    " << reg << " = alloca i8, i64 " << sz << "\n";
                    } else {
                        out_ << "    " << reg << " = alloca %struct." << eff_type << "\n";
                    }
                    locals_[decl->name] = reg;
                    local_types_[decl->name] = eff_type;
                    emitConstructAt(eff_type, reg, decl->ctor_args);
                    // call op+=(rhs)
                    std::string compound_base = eff_type + "__op+=";
                    std::string mangled = resolveSingleArgOverload(compound_base, *be->right);
                    auto& ptypes = func_param_types_[mangled];
                    std::string param_type = ptypes.empty() ? "" : ptypes[0];
                    std::string arg_val = emitArgForParam(*be->right, param_type);
                    std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                    out_ << "    call void @" << llvmGlobalName(mangled)
                         << "(ptr " << reg << ", " << ptype_str << " " << arg_val << ")\n";
                    if (hasDtorInChain(eff_type))
                        dtor_vars_.push_back({decl->name, eff_type});
                    return;
                }
            }

            std::string reg = uniqueAllocaReg(decl->name);
            if (info.has_private_suffix) {
                std::string sz = newTmp();
                out_ << "    " << sz << " = call i64 @" << eff_type << "__$sizeof()\n";
                out_ << "    " << reg << " = alloca i8, i64 " << sz << "\n";
            } else {
                out_ << "    " << reg << " = alloca %struct." << eff_type << "\n";
            }
            locals_[decl->name] = reg;
            local_types_[decl->name] = eff_type;

            // Type name = (a, b, c);  or  Type name(a, b) = (c, d);
            // rhs tuple overrides lhs ctor_args per position; missing rhs positions fall back to lhs.
            bool tuple_init_handled = false;
            if (!decl->is_move) {
                if (auto* te = dynamic_cast<const TupleExpr*>(decl->init.get())) {
                    emitConstructAt(eff_type, reg, decl->ctor_args, te->values);
                    tuple_init_handled = true;
                }
            }
            if (!tuple_init_handled)
                emitConstructAt(eff_type, reg, decl->ctor_args);

            // if initialized with = expr, call op= method; with <- expr, call op<- method
            if (decl->init && !tuple_init_handled) {
                bool init_handled = false;
                // if init is a binary op, try to dispatch via op overload directly into reg
                if (!decl->is_move) {
                    if (auto* be = dynamic_cast<const BinaryExpr*>(decl->init.get())) {
                        std::string op_func = resolveOperatorOverload(be->op, *be->left, *be->right);
                        if (!op_func.empty()) {
                            std::string ret = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                            bool is_method = (ret == "void");
                            auto& ptypes = func_param_types_[op_func];
                            std::string args;
                            if (is_method) {
                                args = "ptr " + reg;
                                std::string la = emitArgForParam(*be->left,  ptypes.size() > 0 ? ptypes[0] : "");
                                std::string ra = emitArgForParam(*be->right, ptypes.size() > 1 ? ptypes[1] : "");
                                if (ptypes.size() > 0) args += ", " + llvmType(ptypes[0]) + " " + la;
                                if (ptypes.size() > 1) args += ", " + llvmType(ptypes[1]) + " " + ra;
                            } else {
                                args = "ptr sret(%struct." + eff_type + ") " + reg;
                                std::string la = emitArgForParam(*be->left,  ptypes.size() > 0 ? ptypes[0] : "");
                                std::string ra = emitArgForParam(*be->right, ptypes.size() > 1 ? ptypes[1] : "");
                                if (ptypes.size() > 0) args += ", " + llvmType(ptypes[0]) + " " + la;
                                if (ptypes.size() > 1) args += ", " + llvmType(ptypes[1]) + " " + ra;
                            }
                            out_ << "    call void @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                            init_handled = true;
                        }
                    }
                    // unary arity 1: Type r = -operand → r.op-(operand)
                    if (!init_handled) {
                        if (auto* ue = dynamic_cast<const UnaryExpr*>(decl->init.get())) {
                            if (ue->op == "+" || ue->op == "-" || ue->op == "~" || ue->op == "!") {
                                std::string op_func = resolveSingleArgOverload(eff_type + "__op" + ue->op, *ue->operand);
                                if (!op_func.empty()) {
                                    auto& ptypes = func_param_types_[op_func];
                                    std::string ret = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                                    bool is_method = (ret == "void");
                                    std::string args = is_method
                                        ? "ptr " + reg
                                        : "ptr sret(%struct." + eff_type + ") " + reg;
                                    std::string param_type = ptypes.empty() ? "" : ptypes[0];
                                    std::string arg_val = emitArgForParam(*ue->operand, param_type);
                                    if (!ptypes.empty())
                                        args += ", " + llvmType(ptypes[0]) + " " + arg_val;
                                    out_ << "    call void @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                                    init_handled = true;
                                }
                            }
                        }
                    }
                }
                if (!init_handled) {
                    std::string op_name = decl->is_move ? "op<-" : "op=";
                    // peel (SameType=inner): Value v = (Value=42) → call op= with inner (42)
                    const Expr* init_expr = decl->init.get();
                    if (auto* tc = dynamic_cast<const TypeConvExpr*>(init_expr))
                        if (tc->target_type == eff_type) init_expr = tc->operand.get();
                    std::string mangled = resolveSingleArgOverload(eff_type + "__" + op_name, *init_expr);
                    if (!mangled.empty()) {
                        auto& ptypes = func_param_types_[mangled];
                        std::string param_type = ptypes.empty() ? "" : ptypes[0];
                        std::string arg_val = emitArgForParam(*init_expr, param_type);
                        std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                        out_ << "    call void @" << llvmGlobalName(mangled)
                             << "(ptr " << reg << ", " << ptype_str << " " << arg_val << ")\n";
                        // class <- pointer move: per spec, rhs pointer is set to nullptr.
                        // gate on the rhs's actual type, not the param type — `op<-(Move^)`
                        // takes a slid ref but the source `from` is a slid lvalue, not a pointer.
                        if (decl->is_move && isIndirectType(inferSlidType(*init_expr)))
                            emitNullOut(*init_expr);
                    } else {
                        // no matching op=/op<- found: synthesize a default field-by-field copy or move
                        // when init is the same slid type (value or reference)
                        std::string src_ptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(init_expr)) {
                            auto tit2 = local_types_.find(ve->name);
                            auto lit  = locals_.find(ve->name);
                            if (tit2 != local_types_.end() && lit != locals_.end()) {
                                if (tit2->second == eff_type) {
                                    src_ptr = lit->second;
                                } else if (tit2->second == eff_type + "^") {
                                    std::string loaded = newTmp();
                                    out_ << "    " << loaded << " = load ptr, ptr " << lit->second << "\n";
                                    src_ptr = loaded;
                                }
                            }
                            if (src_ptr.empty() && ve->name == "self"
                                    && !current_slid_.empty() && current_slid_ == eff_type)
                                src_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
                        }
                        // any expr whose type matches eff_type: emitExpr returns a slid ptr.
                        if (src_ptr.empty() && inferSlidType(*init_expr) == eff_type)
                            src_ptr = emitExpr(*init_expr);
                        if (!src_ptr.empty()) emitSlidSlotAssign(eff_type, reg, src_ptr, decl->is_move, /*is_init=*/true);
                    }
                }
            }

            // register for dtor call on scope exit (chain-aware: also covers inherited dtors)
            if (hasDtorInChain(eff_type)) {
                dtor_vars_.push_back({decl->name, eff_type});
            }
            return;
        }

        // anonymous tuple local: tuple = (a, b, c); or tuple2 = otherTuple;
        if (isAnonTupleType(eff_type)) {
            auto elems = anonTupleElems(eff_type);
            std::string struct_llvm = llvmType(eff_type);
            std::string reg = uniqueAllocaReg(decl->name);
            out_ << "    " << reg << " = alloca " << struct_llvm << "\n";
            locals_[decl->name] = reg;
            local_types_[decl->name] = eff_type;
            if (auto* te = dynamic_cast<const TupleExpr*>(decl->init.get())) {
                for (int i = 0; i < (int)elems.size() && i < (int)te->values.size(); i++) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr " << struct_llvm
                         << ", ptr " << reg << ", i32 0, i32 " << i << "\n";
                    // slid-typed element: copy fields from a same-typed source (var or ctor call)
                    if (slid_info_.count(elems[i])) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type != elems[i])
                            error(std::string("slid tuple element type mismatch: expected '"
                                + elems[i] + "', got '" + src_type + "'"));
                        std::string src_ptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                            src_ptr = locals_[ve->name];
                        } else {
                            src_ptr = emitExpr(*te->values[i]);
                        }
                        emitSlidSlotAssign(elems[i], gep, src_ptr, decl->is_move, /*is_init=*/true);
                        if (hasDtorInChain(elems[i]))
                            dtor_vars_.push_back({decl->name, elems[i], i});
                        continue;
                    }
                    requirePtrInit(elems[i], *te->values[i]);
                    std::string val = emitExpr(*te->values[i]);
                    out_ << "    store " << llvmType(elems[i]) << " " << val << ", ptr " << gep << "\n";
                    if (decl->is_move && isIndirectType(elems[i]))
                        emitNullOut(*te->values[i]);
                }
                return;
            }
            // non-literal initializer — require a same-typed tuple source and field-by-field copy/move
            std::string src_slids = inferSlidType(*decl->init);
            if (src_slids != eff_type)
                error(std::string("cannot initialize tuple '" + eff_type
                    + "' from expression of type '" + src_slids + "'"));
            std::string src_ptr;
            if (auto* ve = dynamic_cast<const VarExpr*>(decl->init.get())) {
                auto lit = locals_.find(ve->name);
                if (lit == locals_.end()) error(std::string("undefined tuple source: " + ve->name));
                src_ptr = lit->second;
            } else {
                // non-variable source (e.g. tuple-returning call): emit into a temp alloca
                std::string val = emitExpr(*decl->init);
                src_ptr = newTmp();
                out_ << "    " << src_ptr << " = alloca " << struct_llvm << "\n";
                out_ << "    store " << struct_llvm << " " << val << ", ptr " << src_ptr << "\n";
            }
            emitSlidSlotAssign(eff_type, reg, src_ptr, decl->is_move, /*is_init=*/true);
            for (int i = 0; i < (int)elems.size(); i++) {
                if (slid_info_.count(elems[i]) && hasDtorInChain(elems[i]))
                    dtor_vars_.push_back({decl->name, elems[i], i});
            }
            return;
        }

        // primitive or reference variable declaration
        // reject unknown types (not a slid, not a primitive, not a pointer)
        {
            static const std::set<std::string> known_primitives = {
                "int","int8","int16","int32","int64",
                "uint","uint8","uint16","uint32","uint64",
                "char","bool","float32","float64","void","intptr"
            };
            bool is_ptr = (!eff_type.empty() && eff_type.back() == '^')
                       || (eff_type.size() >= 2 && eff_type.substr(eff_type.size()-2) == "[]");
            if (!known_primitives.count(eff_type) && !is_ptr)
                error(std::string("unknown type '" + eff_type + "'"));
        }
        std::string reg = uniqueAllocaReg(decl->name);
        std::string llvm_t = llvmType(eff_type);
        out_ << "    " << reg << " = alloca " << llvm_t << "\n";
        locals_[decl->name] = reg;
        local_types_[decl->name] = eff_type;
        if (!decl->init) return; // uninitialized — alloca only
        requirePtrInit(eff_type, *decl->init);
        std::string val = emitExpr(*decl->init);
        // coerce integer or float widths if necessary
        if (!isIndirectType(eff_type)) {
            std::string src_t = exprLlvmType(*decl->init);
            // integer width coercion (sext or trunc)
            static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
            auto sit = rank.find(src_t), dit = rank.find(llvm_t);
            if (sit != rank.end() && dit != rank.end() && sit->second != dit->second) {
                std::string coerced = newTmp();
                if (dit->second > sit->second)
                    out_ << "    " << coerced << " = sext " << src_t << " " << val << " to " << llvm_t << "\n";
                else
                    out_ << "    " << coerced << " = trunc " << src_t << " " << val << " to " << llvm_t << "\n";
                val = coerced;
            }
            // float width coercion (fpext or fptrunc)
            static const std::map<std::string,int> frank = {{"float",0},{"double",1}};
            auto sf = frank.find(src_t), df = frank.find(llvm_t);
            if (sf != frank.end() && df != frank.end() && sf->second != df->second) {
                std::string coerced = newTmp();
                if (df->second > sf->second)
                    out_ << "    " << coerced << " = fpext " << src_t << " " << val << " to " << llvm_t << "\n";
                else
                    out_ << "    " << coerced << " = fptrunc " << src_t << " " << val << " to " << llvm_t << "\n";
                val = coerced;
            }
        }
        out_ << "    store " << llvm_t << " " << val << ", ptr " << reg << "\n";
        // move declaration: null out the source
        if (decl->is_move && isIndirectType(eff_type))
            emitNullOut(*decl->init);
        return;
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
        // self = expr — call op= on the current object
        if (assign->name == "self" && !current_slid_.empty()) {
            std::string op_func = resolveSingleArgOverload(current_slid_ + "__op=", *assign->value);
            if (op_func.empty())
                error(std::string("no matching op= on '" + current_slid_ + "' for 'self = <expr>'"));
            auto& ptypes = func_param_types_[op_func];
            std::string param_type = ptypes.empty() ? "" : ptypes[0];
            std::string arg_val = emitArgForParam(*assign->value, param_type);
            std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
            std::string self_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
            out_ << "    call void @" << llvmGlobalName(op_func)
                 << "(ptr " << self_ptr << ", " << ptype_str << " " << arg_val << ")\n";
            return;
        }
        // check if it's a field via self in a method
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.field_index.count(assign->name)) {
                int idx = info.field_index[assign->name];
                requirePtrInit(info.field_types[idx], *assign->value);
                std::string field_type = llvmType(info.field_types[idx]);
                std::string gep = newTmp();
                std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                     << ", ptr " << self << ", i32 0, i32 " << idx << "\n";
                std::string val = emitExpr(*assign->value);
                out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
                if (assign->is_move && isIndirectType(info.field_types[idx]))
                    emitNullOut(*assign->value);
                return;
            }
        }
        auto it = locals_.find(assign->name);
        if (it == locals_.end())
            error(std::string("undefined variable: " + assign->name));
        auto tit = local_types_.find(assign->name);
        // anonymous-tuple local LHS: route per the element-wise rule.
        if (tit != local_types_.end() && isAnonTupleType(tit->second)) {
            const std::string& lhs_t = tit->second;
            auto elems = anonTupleElems(lhs_t);
            // tuple-literal rhs: tuple = (a, b, ...); — per-element, partial overwrite allowed
            if (auto* te = dynamic_cast<const TupleExpr*>(assign->value.get())) {
                int nfields = (int)elems.size();
                if ((int)te->values.size() > nfields)
                    error(std::string("tuple has " + std::to_string(te->values.size())
                        + " values but '" + assign->name + "' has " + std::to_string(nfields)
                        + " elements"));
                for (int i = 0; i < (int)te->values.size(); i++) {
                    const std::string& ft = elems[i];
                    std::string elem_llvm = llvmType(ft);
                    std::string gep = emitFieldGep(lhs_t, it->second, i);
                    // slid-typed element: copy/move from a same-typed source
                    if (slid_info_.count(ft)) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type != ft)
                            error(std::string("slid tuple element type mismatch: expected '"
                                + ft + "', got '" + src_type + "'"));
                        std::string src_ptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                            src_ptr = locals_[ve->name];
                        } else {
                            src_ptr = emitExpr(*te->values[i]);
                        }
                        emitSlidSlotAssign(ft, gep, src_ptr, assign->is_move);
                        continue;
                    }
                    std::string val = emitExpr(*te->values[i]);
                    std::string src_t = exprLlvmType(*te->values[i]);
                    if (src_t != elem_llvm) {
                        static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                        auto sit = rank.find(src_t), dit = rank.find(elem_llvm);
                        if (sit != rank.end() && dit != rank.end()) {
                            std::string coerced = newTmp();
                            if (dit->second > sit->second)
                                out_ << "    " << coerced << " = sext " << src_t << " " << val << " to " << elem_llvm << "\n";
                            else
                                out_ << "    " << coerced << " = trunc " << src_t << " " << val << " to " << elem_llvm << "\n";
                            val = coerced;
                        } else {
                            error(std::string("type mismatch: cannot assign '"
                                + inferSlidType(*te->values[i]) + "' to tuple element "
                                + std::to_string(i) + " of type '" + ft + "'"));
                        }
                    }
                    out_ << "    store " << elem_llvm << " " << val << ", ptr " << gep << "\n";
                    if (assign->is_move && isIndirectType(ft))
                        emitNullOut(*te->values[i]);
                }
                return;
            }
            // tuple-variable rhs of matching type: route through element-wise walker
            std::string src_slids = inferSlidType(*assign->value);
            if (src_slids != lhs_t)
                error(std::string("type mismatch: cannot assign '" + src_slids
                    + "' to tuple variable '" + assign->name + "' of type '" + lhs_t + "'"));
            std::string src_ptr;
            if (auto* ve = dynamic_cast<const VarExpr*>(assign->value.get())) {
                auto lit = locals_.find(ve->name);
                if (lit != locals_.end()) src_ptr = lit->second;
            }
            if (src_ptr.empty())
                error(std::string("tuple copy from non-variable source not supported"));
            emitSlidSlotAssign(lhs_t, it->second, src_ptr, assign->is_move);
            return;
        }
        // compound assignment: x = x op rhs → detect and dispatch to op{op}= directly
        // (parser desugars x += rhs into x = x + rhs; we undo that here for slid types)
        if (tit != local_types_.end() && slid_info_.count(tit->second)) {
            if (auto* be = dynamic_cast<const BinaryExpr*>(assign->value.get())) {
                if (auto* lve = dynamic_cast<const VarExpr*>(be->left.get())) {
                    if (lve->name == assign->name && isCompoundableOp(be->op)) {
                        const std::string& slid_name = tit->second;
                        std::string compound_base = slid_name + "__op" + be->op + "=";
                        std::string mangled = resolveSingleArgOverload(compound_base, *be->right);
                        if (!mangled.empty()) {
                            auto& ptypes = func_param_types_[mangled];
                            std::string param_type = ptypes.empty() ? "" : ptypes[0];
                            std::string arg_val = emitArgForParam(*be->right, param_type);
                            std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                            out_ << "    call void @" << llvmGlobalName(mangled)
                                 << "(ptr " << it->second << ", " << ptype_str << " " << arg_val << ")\n";
                            return;
                        }
                        // no direct op{op}=: try coercing rhs to the slid type via op=,
                        // then call op{op}=(slid^) — or fall back to op{op}(slid^, slid^).
                        std::string coerce = resolveSingleArgOverload(slid_name + "__op=", *be->right);
                        if (!coerce.empty()) {
                            auto& sinfo = slid_info_[slid_name];
                            std::string tmp = emitRawSlidAlloca(slid_name);
                            emitConstructAtPtrs(slid_name, tmp, {}, {});
                            auto& cptypes = func_param_types_[coerce];
                            std::string cptype = cptypes.empty() ? "" : cptypes[0];
                            std::string carg = emitArgForParam(*be->right, cptype);
                            out_ << "    call void @" << llvmGlobalName(coerce)
                                 << "(ptr " << tmp << ", " << (cptypes.empty() ? "ptr" : llvmType(cptype))
                                 << " " << carg << ")\n";
                            // prefer op{op}=(slid^)
                            std::string via_ref;
                            auto opit = method_overloads_.find(compound_base);
                            if (opit != method_overloads_.end())
                                for (auto& [m, ptypes] : opit->second)
                                    if (ptypes.size() == 1 && isRefType(ptypes[0])) { via_ref = m; break; }
                            if (!via_ref.empty()) {
                                out_ << "    call void @" << llvmGlobalName(via_ref)
                                     << "(ptr " << it->second << ", ptr " << tmp << ")\n";
                            } else {
                                // fall back to op{op}(slid^, slid^) — result stored into lhs (sret)
                                std::string op_base = slid_name + "__op" + be->op;
                                auto oppit = method_overloads_.find(op_base);
                                if (oppit != method_overloads_.end())
                                    for (auto& [m, ptypes] : oppit->second)
                                        if (ptypes.size() == 2 && isRefType(ptypes[0]) && isRefType(ptypes[1])) {
                                            out_ << "    call void @" << llvmGlobalName(m)
                                                 << "(ptr " << it->second << ", ptr " << it->second
                                                 << ", ptr " << tmp << ")\n";
                                            break;
                                        }
                            }
                            emitDtorChainCall(slid_name, tmp);
                            return;
                        }
                    }
                }
            }
        }
        // check for operator overload: lhs is slid type, rhs is BinaryExpr
        if (tit != local_types_.end() && slid_info_.count(tit->second)) {
            if (auto* be = dynamic_cast<const BinaryExpr*>(assign->value.get())) {
                std::string op_func = resolveOperatorOverload(be->op, *be->left, *be->right);
                if (!op_func.empty()) {
                    // call op func with sret into lhs alloca
                    // first call dtor on existing lhs value
                    const std::string& slid_name = tit->second;
                    if (slid_info_.at(slid_name).has_dtor) {
                        emitDtorChainCall(slid_name, it->second);
                        // re-init fields to zero/null
                        auto& info = slid_info_[slid_name];
                        for (int i = 0; i < (int)info.field_types.size(); i++) {
                            std::string ft = llvmType(info.field_types[i]);
                            std::string gep = newTmp();
                            out_ << "    " << gep << " = getelementptr %struct." << slid_name << ", ptr " << it->second << ", i32 0, i32 " << i << "\n";
                            if (isIndirectType(info.field_types[i]))
                                out_ << "    store ptr null, ptr " << gep << "\n";
                            else
                                out_ << "    store " << ft << " 0, ptr " << gep << "\n";
                        }
                    }
                    std::string ret2 = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                    bool is_method2 = (ret2 == "void");
                    auto& ptypes = func_param_types_[op_func];
                    std::string args;
                    if (is_method2) {
                        args = "ptr " + it->second;
                    } else {
                        args = "ptr sret(%struct." + slid_name + ") " + it->second;
                    }
                    // left arg
                    std::string la = emitArgForParam(*be->left, ptypes.size() > 0 ? ptypes[0] : "");
                    if (ptypes.size() > 0) args += ", " + llvmType(ptypes[0]) + " " + la;
                    // right arg
                    std::string ra = emitArgForParam(*be->right, ptypes.size() > 1 ? ptypes[1] : "");
                    if (ptypes.size() > 1) args += ", " + llvmType(ptypes[1]) + " " + ra;
                    out_ << "    call void @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                    return;
                }
            }
        }
        // unary arity 1: lhs is slid, rhs is UnaryExpr (-operand etc.) → lhs.op-(operand)
        if (tit != local_types_.end() && slid_info_.count(tit->second)) {
            if (auto* ue = dynamic_cast<const UnaryExpr*>(assign->value.get())) {
                if (ue->op == "+" || ue->op == "-" || ue->op == "~" || ue->op == "!") {
                    const std::string& slid_name = tit->second;
                    std::string op_func = resolveSingleArgOverload(slid_name + "__op" + ue->op, *ue->operand);
                    if (!op_func.empty()) {
                        if (slid_info_.at(slid_name).has_dtor) {
                            emitDtorChainCall(slid_name, it->second);
                            auto& info = slid_info_[slid_name];
                            for (int i = 0; i < (int)info.field_types.size(); i++) {
                                std::string ft = llvmType(info.field_types[i]);
                                std::string gep = newTmp();
                                out_ << "    " << gep << " = getelementptr %struct." << slid_name
                                     << ", ptr " << it->second << ", i32 0, i32 " << i << "\n";
                                if (isIndirectType(info.field_types[i]))
                                    out_ << "    store ptr null, ptr " << gep << "\n";
                                else
                                    out_ << "    store " << ft << " 0, ptr " << gep << "\n";
                            }
                        }
                        auto& ptypes = func_param_types_[op_func];
                        std::string ret = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                        bool is_method = (ret == "void");
                        std::string args = is_method
                            ? "ptr " + it->second
                            : "ptr sret(%struct." + slid_name + ") " + it->second;
                        std::string param_type = ptypes.empty() ? "" : ptypes[0];
                        std::string arg_val = emitArgForParam(*ue->operand, param_type);
                        if (!ptypes.empty())
                            args += ", " + llvmType(ptypes[0]) + " " + arg_val;
                        out_ << "    call void @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                        return;
                    }
                }
            }
        }
        // tuple literal rhs: d = (a, b, c); or d <- (a, b, c); — per-field writes.
        if (tit != local_types_.end() && slid_info_.count(tit->second)) {
            if (auto* te = dynamic_cast<const TupleExpr*>(assign->value.get())) {
                const std::string& slid_name = tit->second;
                auto& info = slid_info_[slid_name];
                int nfields = (int)info.field_types.size();
                if ((int)te->values.size() > nfields)
                    error(std::string("tuple has " + std::to_string(te->values.size())
                        + " values but '" + slid_name + "' has " + std::to_string(nfields)
                        + " accessible fields"));
                for (int i = 0; i < (int)te->values.size(); i++) {
                    const std::string& ft = info.field_types[i];
                    std::string elem_llvm = llvmType(ft);
                    std::string gep = emitFieldGep(slid_name, it->second, i);
                    // slid-typed field: dispatch per the element-wise rule
                    if (slid_info_.count(ft)) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type != ft)
                            error(std::string("slid field '" + info.field_types[i]
                                + "' reassignment: expected '" + ft + "', got '" + src_type + "'"));
                        std::string src_ptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                            src_ptr = locals_[ve->name];
                        } else {
                            src_ptr = emitExpr(*te->values[i]);
                        }
                        emitSlidSlotAssign(ft, gep, src_ptr, assign->is_move);
                        continue;
                    }
                    // anon-tuple-typed field: recurse via walker on matching tuple-var source
                    if (isAnonTupleType(ft)) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type != ft)
                            error(std::string("tuple field reassignment: expected '"
                                + ft + "', got '" + src_type + "'"));
                        std::string src_ptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                            src_ptr = locals_[ve->name];
                        }
                        if (src_ptr.empty())
                            error(std::string("tuple-typed field copy from non-variable source not supported"));
                        emitSlidSlotAssign(ft, gep, src_ptr, assign->is_move);
                        continue;
                    }
                    std::string val = emitExpr(*te->values[i]);
                    if (!isIndirectType(ft)) {
                        std::string src_t = exprLlvmType(*te->values[i]);
                        static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                        auto sit = rank.find(src_t), dit = rank.find(elem_llvm);
                        if (sit != rank.end() && dit != rank.end() && sit->second != dit->second) {
                            std::string coerced = newTmp();
                            if (dit->second > sit->second)
                                out_ << "    " << coerced << " = sext " << src_t << " " << val << " to " << elem_llvm << "\n";
                            else
                                out_ << "    " << coerced << " = trunc " << src_t << " " << val << " to " << elem_llvm << "\n";
                            val = coerced;
                        }
                    }
                    out_ << "    store " << elem_llvm << " " << val << ", ptr " << gep << "\n";
                    if (assign->is_move && isIndirectType(ft))
                        emitNullOut(*te->values[i]);
                }
                return;
            }
        }

        // check for op= / op<- method on slid type
        if (tit != local_types_.end() && slid_info_.count(tit->second)) {
            std::string op_name = assign->is_move ? "op<-" : "op=";
            std::string mangled = resolveSingleArgOverload(tit->second + "__" + op_name, *assign->value);
            if (!mangled.empty()) {
                auto& ptypes = func_param_types_[mangled];
                std::string param_type = ptypes.empty() ? "" : ptypes[0];
                std::string arg_val = emitArgForParam(*assign->value, param_type);
                std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                out_ << "    call void @" << llvmGlobalName(mangled)
                     << "(ptr " << it->second << ", " << ptype_str << " " << arg_val << ")\n";
                // class <- pointer move: per spec, rhs pointer is set to nullptr.
                // gate on the rhs's actual type, not the param type — `op<-(Move^)`
                // takes a slid ref but the source `from` is a slid lvalue, not a pointer.
                if (assign->is_move && isIndirectType(inferSlidType(*assign->value)))
                    emitNullOut(*assign->value);
                return;
            }
            // no matching op= / op<- found: synthesize a default field-by-field copy
            {
                const std::string& slid_name = tit->second;
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(assign->value.get())) {
                    auto tit2 = local_types_.find(ve->name);
                    auto lit  = locals_.find(ve->name);
                    if (tit2 != local_types_.end() && lit != locals_.end()) {
                        if (tit2->second == slid_name) {
                            src_ptr = lit->second;
                        } else if (tit2->second == slid_name + "^") {
                            std::string loaded = newTmp();
                            out_ << "    " << loaded << " = load ptr, ptr " << lit->second << "\n";
                            src_ptr = loaded;
                        }
                    }
                }
                // any other expr whose type matches slid_name: emitExpr returns a slid ptr.
                if (src_ptr.empty() && inferSlidType(*assign->value) == slid_name)
                    src_ptr = emitExpr(*assign->value);
                if (!src_ptr.empty()) { emitSlidSlotAssign(slid_name, it->second, src_ptr, assign->is_move); return; }
            }
        }
        // pointer move: copy source to dest, null source
        if (assign->is_move && tit != local_types_.end() && isIndirectType(tit->second)) {
            requirePtrInit(tit->second, *assign->value);
            std::string src_val = emitExpr(*assign->value);
            out_ << "    store ptr " << src_val << ", ptr " << it->second << "\n";
            emitNullOut(*assign->value);
            return;
        }
        if (tit != local_types_.end())
            requirePtrInit(tit->second, *assign->value);
        std::string val = emitExpr(*assign->value);
        bool is_ptr = tit != local_types_.end() && isIndirectType(tit->second);
        std::string store_type = is_ptr ? "ptr" : llvmType(tit != local_types_.end() ? tit->second : "int");
        // coerce integer widths if necessary (sext or trunc)
        if (!is_ptr && tit != local_types_.end()) {
            std::string src_t = exprLlvmType(*assign->value);
            if (src_t == "ptr" && tit->second == "intptr") {
                std::string coerced = newTmp();
                out_ << "    " << coerced << " = ptrtoint ptr " << val << " to i64\n";
                val = coerced;
            } else {
                static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                auto sit = rank.find(src_t), dit = rank.find(store_type);
                if (sit != rank.end() && dit != rank.end() && sit->second != dit->second) {
                    std::string coerced = newTmp();
                    if (dit->second > sit->second)
                        out_ << "    " << coerced << " = sext " << src_t << " " << val << " to " << store_type << "\n";
                    else
                        out_ << "    " << coerced << " = trunc " << src_t << " " << val << " to " << store_type << "\n";
                    val = coerced;
                }
            }
        }
        out_ << "    store " << store_type << " " << val << ", ptr " << it->second << "\n";
        return;
    }

    if (auto* cas = dynamic_cast<const CompoundAssignStmt*>(&stmt)) {
        // Step 1: detect direct slid op<op>= dispatch — single-eval, no temp.
        // Only fires when the LHS resolves to a slid type that defines op<op>=
        // for the rhs; otherwise we desugar to the matching specialized
        // assign-family stmt and let its existing handler run.
        std::string slids_type;
        std::string addr;
        const Expr* lhs = cas->lhs.get();

        auto resolveAddr = [&]() {
            if (auto* ve = dynamic_cast<const VarExpr*>(lhs)) {
                auto lit = locals_.find(ve->name);
                auto tit = local_types_.find(ve->name);
                if (lit != locals_.end() && tit != local_types_.end()) {
                    addr = lit->second;
                    slids_type = tit->second;
                    return;
                }
                if (!current_slid_.empty() && slid_info_.count(current_slid_)) {
                    auto& info = slid_info_[current_slid_];
                    auto fit = info.field_index.find(ve->name);
                    if (fit != info.field_index.end()) {
                        std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr %struct."
                             << current_slid_ << ", ptr " << self
                             << ", i32 0, i32 " << fit->second << "\n";
                        addr = gep;
                        slids_type = info.field_types[fit->second];
                    }
                }
                return;
            }
            if (auto* de = dynamic_cast<const DerefExpr*>(lhs)) {
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto lit = locals_.find(ve->name);
                    auto tit = local_types_.find(ve->name);
                    if (lit != locals_.end() && tit != local_types_.end()) {
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load ptr, ptr " << lit->second << "\n";
                        addr = loaded;
                        std::string pt = tit->second;
                        if (isPtrType(pt)) pt = pt.substr(0, pt.size()-2);
                        else if (!pt.empty() && pt.back() == '^') pt = pt.substr(0, pt.size()-1);
                        slids_type = pt;
                    }
                }
                return;
            }
            if (auto* fa = dynamic_cast<const FieldAccessExpr*>(lhs)) {
                if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
                    auto lit = locals_.find(ve->name);
                    auto tit = local_types_.find(ve->name);
                    if (lit != locals_.end() && tit != local_types_.end()
                        && slid_info_.count(tit->second)) {
                        auto& info = slid_info_[tit->second];
                        auto fit = info.field_index.find(fa->field);
                        if (fit != info.field_index.end()) {
                            std::string gep = newTmp();
                            out_ << "    " << gep << " = getelementptr %struct." << tit->second
                                 << ", ptr " << lit->second << ", i32 0, i32 " << fit->second << "\n";
                            addr = gep;
                            slids_type = info.field_types[fit->second];
                        }
                    }
                }
                return;
            }
            if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(lhs)) {
                if (auto* ve = dynamic_cast<const VarExpr*>(ai->base.get())) {
                    auto ait = array_info_.find(ve->name);
                    if (ait != array_info_.end()) {
                        int total = 1;
                        for (int d : ait->second.dims) total *= d;
                        std::string elt = llvmType(ait->second.elem_type);
                        std::string idx_llvm = exprLlvmType(*ai->index);
                        std::string idx_val = emitExpr(*ai->index);
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr [" << total << " x " << elt
                             << "], ptr " << ait->second.alloca_reg << ", i32 0, "
                             << idx_llvm << " " << idx_val << "\n";
                        addr = gep;
                        slids_type = ait->second.elem_type;
                    }
                }
                return;
            }
        };
        resolveAddr();

        if (!slids_type.empty() && slid_info_.count(slids_type) && !addr.empty()) {
            std::string compound_base = slids_type + "__op" + cas->op + "=";
            std::string mangled = resolveSingleArgOverload(compound_base, *cas->rhs);
            if (!mangled.empty()) {
                auto& ptypes = func_param_types_[mangled];
                std::string param_type = ptypes.empty() ? "" : ptypes[0];
                std::string arg_val = emitArgForParam(*cas->rhs, param_type);
                std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                out_ << "    call void @" << llvmGlobalName(mangled)
                     << "(ptr " << addr << ", " << ptype_str << " " << arg_val << ")\n";
                return;
            }
        }

        // Step 2: desugar to the matching specialized assign-family stmt.
        // Existing handlers cover primitives, pointer arithmetic (iter += int),
        // slid op<op> + op= for slid types without op<op>=, etc.
        // Sub-expressions evaluate twice; safe for idempotent shapes (VarExpr
        // bases, literal/var indices, etc.).
        auto rhs_clone = cloneExpr(*cas->rhs);
        int fid = cas->file_id, tok = cas->tok;
        if (auto* ve = dynamic_cast<const VarExpr*>(lhs)) {
            auto bin = synthAt<BinaryExpr>(fid, tok, cas->op,
                synthAt<VarExpr>(fid, tok, ve->name), std::move(rhs_clone));
            AssignStmt synth(ve->name, std::move(bin), false);
            synth.file_id = fid; synth.tok = tok;
            emitStmt(synth);
            return;
        }
        if (auto* de = dynamic_cast<const DerefExpr*>(lhs)) {
            auto bin = synthAt<BinaryExpr>(fid, tok, cas->op,
                synthAt<DerefExpr>(fid, tok, cloneExpr(*de->operand)),
                std::move(rhs_clone));
            DerefAssignStmt synth(cloneExpr(*de->operand), std::move(bin));
            synth.file_id = fid; synth.tok = tok;
            emitStmt(synth);
            return;
        }
        if (auto* fa = dynamic_cast<const FieldAccessExpr*>(lhs)) {
            auto bin = synthAt<BinaryExpr>(fid, tok, cas->op,
                synthAt<FieldAccessExpr>(fid, tok, cloneExpr(*fa->object), fa->field),
                std::move(rhs_clone));
            FieldAssignStmt synth(cloneExpr(*fa->object), fa->field, std::move(bin), false);
            synth.file_id = fid; synth.tok = tok;
            emitStmt(synth);
            return;
        }
        if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(lhs)) {
            auto bin = synthAt<BinaryExpr>(fid, tok, cas->op,
                synthAt<ArrayIndexExpr>(fid, tok, cloneExpr(*ai->base), cloneExpr(*ai->index)),
                std::move(rhs_clone));
            IndexAssignStmt synth(cloneExpr(*ai->base), cloneExpr(*ai->index),
                std::move(bin), false);
            synth.file_id = fid; synth.tok = tok;
            emitStmt(synth);
            return;
        }
        if (auto* pide = dynamic_cast<const PostIncDerefExpr*>(lhs)) {
            auto bin = synthAt<BinaryExpr>(fid, tok, cas->op,
                synthAt<DerefExpr>(fid, tok, cloneExpr(*pide->operand)),
                std::move(rhs_clone));
            PostIncDerefAssignStmt synth(cloneExpr(*pide->operand), pide->op,
                std::move(bin));
            synth.file_id = fid; synth.tok = tok;
            emitStmt(synth);
            return;
        }
        error(std::string("compound assignment: unsupported LHS shape"));
    }

    if (auto* da = dynamic_cast<const DerefAssignStmt*>(&stmt)) {
        // ptr^ = val / ptr^ <- val — store-through-pointer, with op=/op<- dispatch
        // for slid pointees.
        std::string ptr_reg;
        std::string pointee_llvm = "i32";
        std::string pointee_type;
        if (auto* ve = dynamic_cast<const VarExpr*>(da->ptr.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                error(std::string("DerefAssign: undefined variable '" + ve->name + "'"));
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && isIndirectType(tit->second)) {
                std::string loaded_ptr = newTmp();
                out_ << "    " << loaded_ptr << " = load ptr, ptr " << it->second << "\n";
                ptr_reg = loaded_ptr;
                pointee_type = ( isPtrType(tit->second) ? tit->second.substr(0, tit->second.size()-2) : tit->second.substr(0, tit->second.size()-1) );
                pointee_llvm = llvmType(pointee_type);
            } else {
                ptr_reg = it->second;
            }
        } else {
            ptr_reg = emitExpr(*da->ptr);
        }
        // slid pointee: dispatch op<-/op= via emitSlidSlotAssign so user-defined
        // ops (and the field-walk fallback for pointer fields, dtor-bearing fields, etc.)
        // run instead of a raw struct store.
        if (!pointee_type.empty() && slid_info_.count(pointee_type)) {
            std::string src_ptr = emitArgForParam(*da->value, pointee_type + "^");
            emitSlidSlotAssign(pointee_type, ptr_reg, src_ptr, da->is_move);
            return;
        }
        if (!pointee_type.empty()) requirePtrInit(pointee_type, *da->value);
        std::string val = emitExpr(*da->value);
        out_ << "    store " << pointee_llvm << " " << val << ", ptr " << ptr_reg << "\n";
        return;
    }

    if (auto* ia = dynamic_cast<const IndexAssignStmt*>(&stmt)) {
        // base[index] = value — pointer-type indexed store

        // obj.field[index] = value — inline array field on an external object
        if (auto* fa = dynamic_cast<const FieldAccessExpr*>(ia->base.get())) {
            auto* ove = dynamic_cast<const VarExpr*>(fa->object.get());
            if (!ove) error(std::string("IndexAssign: complex field object not supported"));
            auto tit = local_types_.find(ove->name);
            if (tit == local_types_.end()) error(std::string("IndexAssign: unknown type for '" + ove->name + "'"));
            std::string slid_name = tit->second;
            auto& info = slid_info_[slid_name];
            auto fit = info.field_index.find(fa->field);
            if (fit == info.field_index.end()) error(std::string("IndexAssign: unknown field '" + fa->field + "'"));
            std::string ft = info.field_types[fit->second];
            // anon-tuple field: GEP into the field and delegate to tuple-element store logic
            if (isAnonTupleType(ft)) {
                auto elems = anonTupleElems(ft);
                int idx;
                if (!constExprToInt(*ia->index, enum_values_, idx))
                    error(std::string("tuple index must be a constant integer"));
                if (idx < 0 || idx >= (int)elems.size())
                    error(std::string("tuple index " + std::to_string(idx)
                        + " out of range (size " + std::to_string(elems.size()) + ")"));
                const std::string& elem_slids = elems[idx];
                std::string obj_ptr = locals_[ove->name];
                std::string field_gep = newTmp();
                out_ << "    " << field_gep << " = getelementptr %struct." << slid_name
                     << ", ptr " << obj_ptr << ", i32 0, i32 " << fit->second << "\n";
                std::string slot_gep = emitFieldGep(ft, field_gep, idx);
                if (slid_info_.count(elem_slids) || isAnonTupleType(elem_slids)) {
                    std::string src_type = inferSlidType(*ia->value);
                    if (src_type != elem_slids)
                        error(std::string("tuple element write: expected '"
                            + elem_slids + "', got '" + src_type + "'"));
                    std::string src_ptr;
                    if (auto* rve = dynamic_cast<const VarExpr*>(ia->value.get())) {
                        auto lit = locals_.find(rve->name);
                        if (lit != locals_.end()) src_ptr = lit->second;
                    } else {
                        std::string v = emitExpr(*ia->value);
                        if (isAnonTupleType(elem_slids)) {
                            std::string tmp = newTmp();
                            out_ << "    " << tmp << " = alloca " << llvmType(elem_slids) << "\n";
                            out_ << "    store " << llvmType(elem_slids) << " " << v
                                 << ", ptr " << tmp << "\n";
                            src_ptr = tmp;
                        } else {
                            src_ptr = v;
                        }
                    }
                    emitSlidSlotAssign(elem_slids, slot_gep, src_ptr, ia->is_move);
                    return;
                }
                // scalar slot
                requirePtrInit(elem_slids, *ia->value);
                std::string rhs_val = emitExpr(*ia->value);
                out_ << "    store " << llvmType(elem_slids) << " " << rhs_val
                     << ", ptr " << slot_gep << "\n";
                if (ia->is_move && isIndirectType(elem_slids))
                    emitNullOut(*ia->value);
                return;
            }
            if (!isInlineArrayType(ft)) error(std::string("IndexAssign: field '" + fa->field + "' is not an inline array"));
            auto lb = ft.rfind('[');
            std::string elem_type = ft.substr(0, lb);
            std::string sz_str = ft.substr(lb + 1, ft.size() - lb - 2);
            std::string elt = llvmType(elem_type);
            std::string obj_ptr = locals_[ove->name];
            std::string field_gep = newTmp();
            out_ << "    " << field_gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << obj_ptr << ", i32 0, i32 " << fit->second << "\n";
            std::string idx_llvm = exprLlvmType(*ia->index);
            std::string idx_val = emitExpr(*ia->index);
            std::string elem_gep = newTmp();
            out_ << "    " << elem_gep << " = getelementptr [" << sz_str << " x " << elt
                 << "], ptr " << field_gep << ", i32 0, " << idx_llvm << " " << idx_val << "\n";
            requirePtrInit(elem_type, *ia->value);
            std::string rhs_val = emitExpr(*ia->value);
            out_ << "    store " << elt << " " << rhs_val << ", ptr " << elem_gep << "\n";
            return;
        }

        // anon-tuple-ref element write: p^[N] = val where p is (t1,...)^
        if (auto* de = dynamic_cast<const DerefExpr*>(ia->base.get())) {
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) {
                    std::string t = tit->second;
                    if (!t.empty() && t.back() == '^') t.pop_back();
                    else if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                        t.resize(t.size()-2);
                    if (isAnonTupleType(t)) {
                        auto elems = anonTupleElems(t);
                        int idx;
                        if (!constExprToInt(*ia->index, enum_values_, idx))
                            error(std::string("tuple index must be a constant integer"));
                        if (idx < 0 || idx >= (int)elems.size())
                            error(std::string("tuple index " + std::to_string(idx)
                                + " out of range (size " + std::to_string(elems.size()) + ")"));
                        std::string base_ptr = newTmp();
                        out_ << "    " << base_ptr << " = load ptr, ptr "
                             << locals_.at(ve->name) << "\n";
                        std::string slot_gep = emitFieldGep(t, base_ptr, idx);
                        const std::string& elem_slids = elems[idx];
                        if (slid_info_.count(elem_slids) || isAnonTupleType(elem_slids)) {
                            std::string src_ptr;
                            if (auto* rve = dynamic_cast<const VarExpr*>(ia->value.get())) {
                                auto lit = locals_.find(rve->name);
                                if (lit != locals_.end()) src_ptr = lit->second;
                            } else {
                                std::string v = emitExpr(*ia->value);
                                if (isAnonTupleType(elem_slids)) {
                                    std::string tmp = newTmp();
                                    out_ << "    " << tmp << " = alloca "
                                         << llvmType(elem_slids) << "\n";
                                    out_ << "    store " << llvmType(elem_slids) << " "
                                         << v << ", ptr " << tmp << "\n";
                                    src_ptr = tmp;
                                } else {
                                    src_ptr = v;
                                }
                            }
                            emitSlidSlotAssign(elem_slids, slot_gep, src_ptr, ia->is_move);
                            return;
                        }
                        requirePtrInit(elem_slids, *ia->value);
                        std::string rhs_val = emitExpr(*ia->value);
                        out_ << "    store " << llvmType(elem_slids) << " " << rhs_val
                             << ", ptr " << slot_gep << "\n";
                        if (ia->is_move && isIndirectType(elem_slids))
                            emitNullOut(*ia->value);
                        return;
                    }
                }
            }
        }

        auto* ve = dynamic_cast<const VarExpr*>(ia->base.get());
        if (!ve) error(std::string("IndexAssign: complex base not supported"));

        // anonymous tuple element write: tuple[N] = val — N must be a compile-time constant
        {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && isAnonTupleType(tit->second)) {
                auto elems = anonTupleElems(tit->second);
                int idx;
                if (!constExprToInt(*ia->index, enum_values_, idx))
                    error(std::string("tuple index must be a constant integer"));
                if (idx < 0 || idx >= (int)elems.size())
                    error(std::string("tuple index " + std::to_string(idx)
                        + " out of range (size " + std::to_string(elems.size()) + ")"));
                std::string elem_slids = elems[idx];
                std::string elem_llvm = llvmType(elem_slids);
                std::string gep = emitFieldGep(tit->second, locals_[ve->name], idx);
                // slid or anon-tuple element: route through element-wise walker for op= dispatch
                if (slid_info_.count(elem_slids) || isAnonTupleType(elem_slids)) {
                    std::string src_type = inferSlidType(*ia->value);
                    if (src_type != elem_slids)
                        error(std::string("tuple element write: expected '"
                            + elem_slids + "', got '" + src_type + "'"));
                    std::string src_ptr;
                    if (auto* rve = dynamic_cast<const VarExpr*>(ia->value.get())) {
                        auto lit = locals_.find(rve->name);
                        if (lit != locals_.end()) src_ptr = lit->second;
                    } else {
                        std::string v = emitExpr(*ia->value);
                        if (isAnonTupleType(elem_slids)) {
                            // emitExpr(TupleExpr or similar) returns a loaded struct SSA.
                            // Materialize to a ptr so emitSlidSlotAssign's field walk works.
                            std::string tmp = newTmp();
                            out_ << "    " << tmp << " = alloca " << llvmType(elem_slids) << "\n";
                            out_ << "    store " << llvmType(elem_slids) << " " << v
                                 << ", ptr " << tmp << "\n";
                            src_ptr = tmp;
                        } else {
                            // slid: emitExpr returns the ptr directly
                            src_ptr = v;
                        }
                    }
                    if (src_ptr.empty())
                        error(std::string("tuple element write from unsupported source"));
                    emitSlidSlotAssign(elem_slids, gep, src_ptr, ia->is_move);
                    return;
                }
                requirePtrInit(elem_slids, *ia->value);
                std::string rhs_val = emitExpr(*ia->value);
                // width-coerce integer widths (sext/trunc) to match the field type
                if (!isIndirectType(elem_slids)) {
                    std::string src_t = exprLlvmType(*ia->value);
                    static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                    auto sit = rank.find(src_t), dit = rank.find(elem_llvm);
                    if (sit != rank.end() && dit != rank.end() && sit->second != dit->second) {
                        std::string coerced = newTmp();
                        if (dit->second > sit->second)
                            out_ << "    " << coerced << " = sext " << src_t << " " << rhs_val << " to " << elem_llvm << "\n";
                        else
                            out_ << "    " << coerced << " = trunc " << src_t << " " << rhs_val << " to " << elem_llvm << "\n";
                        rhs_val = coerced;
                    }
                }
                out_ << "    store " << elem_llvm << " " << rhs_val << ", ptr " << gep << "\n";
                return;
            }
        }

        // slid op[]= dispatch
        {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && slid_info_.count(tit->second)) {
                std::string mangled = resolveSlidIndexAssign(tit->second, *ia->index);
                if (!mangled.empty()) {
                    std::string obj_ptr = locals_[ve->name];
                    auto& mptypes = func_param_types_[mangled];
                    std::string idx_llvm = mptypes.empty() ? "i32" : llvmType(mptypes[0]);
                    std::string rhs_llvm = (mptypes.size() < 2) ? "i32" : llvmType(mptypes[1]);
                    std::string idx_val = mptypes.empty()
                        ? emitExpr(*ia->index)
                        : emitArgForParam(*ia->index, mptypes[0]);
                    std::string rhs_val = (mptypes.size() < 2)
                        ? emitExpr(*ia->value)
                        : emitArgForParam(*ia->value, mptypes[1]);
                    out_ << "    call void @" << llvmGlobalName(mangled)
                         << "(ptr " << obj_ptr << ", " << idx_llvm << " " << idx_val
                         << ", " << rhs_llvm << " " << rhs_val << ")\n";
                    return;
                }
            }
        }

        // inline-array local: arr[i] = val (or arr[i] <- val)
        {
            auto [gep, et] = emitInlineArrayElemPtr(*ia->base, *ia->index);
            if (!gep.empty()) {
                requirePtrInit(et, *ia->value);
                std::string rhs = emitExpr(*ia->value);
                out_ << "    store " << llvmType(et) << " " << rhs << ", ptr " << gep << "\n";
                if (ia->is_move && isIndirectType(et))
                    emitNullOut(*ia->value);
                return;
            }
        }

        // fixed-size array field store inside method body (e.g. int rgb_[3])
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end() && isInlineArrayType(info.field_types[fit->second])) {
                std::string ft = info.field_types[fit->second];
                auto lb = ft.rfind('[');
                std::string elem_type = ft.substr(0, lb);
                std::string sz_str = ft.substr(lb + 1, ft.size() - lb - 2);
                std::string elt = llvmType(elem_type);
                std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                std::string field_gep = newTmp();
                out_ << "    " << field_gep << " = getelementptr %struct." << current_slid_
                     << ", ptr " << self << ", i32 0, i32 " << fit->second << "\n";
                std::string idx_llvm = exprLlvmType(*ia->index);
                std::string idx_val = emitExpr(*ia->index);
                std::string elem_gep = newTmp();
                out_ << "    " << elem_gep << " = getelementptr [" << sz_str << " x " << elt
                     << "], ptr " << field_gep << ", i32 0, " << idx_llvm << " " << idx_val << "\n";
                requirePtrInit(elem_type, *ia->value);
                std::string rhs_val = emitExpr(*ia->value);
                out_ << "    store " << elt << " " << rhs_val << ", ptr " << elem_gep << "\n";
                return;
            }
        }

        std::string base_ptr;
        std::string elem_type_str;
        // check if it's a field in the current slid
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end()) {
                std::string ft = info.field_types[fit->second];
                if (ft.size() >= 2 && ft.substr(ft.size()-2) == "[]") {
                    elem_type_str = ft.substr(0, ft.size()-2);
                    std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                    std::string fgep = newTmp();
                    out_ << "    " << fgep << " = getelementptr %struct." << current_slid_
                         << ", ptr " << self << ", i32 0, i32 " << fit->second << "\n";
                    base_ptr = newTmp();
                    out_ << "    " << base_ptr << " = load ptr, ptr " << fgep << "\n";
                }
            }
        }
        // check local pointer variable
        if (base_ptr.empty()) {
            auto lit = locals_.find(ve->name);
            auto tit = local_types_.find(ve->name);
            if (lit != locals_.end() && tit != local_types_.end()) {
                std::string lt = tit->second;
                if (lt.size() >= 2 && lt.substr(lt.size()-2) == "[]") {
                    elem_type_str = lt.substr(0, lt.size()-2);
                    base_ptr = newTmp();
                    out_ << "    " << base_ptr << " = load ptr, ptr " << lit->second << "\n";
                }
            }
        }
        if (base_ptr.empty())
            error(std::string("IndexAssign: '" + ve->name + "' is not a pointer type"));

        std::string idx_llvm = exprLlvmType(*ia->index);
        std::string idx_val = emitExpr(*ia->index);
        std::string elt = llvmType(elem_type_str);
        std::string gep = newTmp();
        out_ << "    " << gep << " = getelementptr " << elt << ", ptr " << base_ptr << ", " << idx_llvm << " " << idx_val << "\n";
        requirePtrInit(elem_type_str, *ia->value);
        std::string rhs = emitExpr(*ia->value);
        out_ << "    store " << elt << " " << rhs << ", ptr " << gep << "\n";
        if (ia->is_move && isIndirectType(elem_type_str))
            emitNullOut(*ia->value);
        return;
    }

    if (auto* pida = dynamic_cast<const PostIncDerefAssignStmt*>(&stmt)) {
        // ptr++^ = val / ptr++^ <- val — assign at current ptr, then advance.
        auto* ve = dynamic_cast<const VarExpr*>(pida->ptr.get());
        if (!ve) error(std::string("PostIncDerefAssign: only simple pointer variables supported"));
        auto it = locals_.find(ve->name);
        if (it == locals_.end())
            error(std::string("PostIncDerefAssign: undefined variable '" + ve->name + "'"));
        auto tit = local_types_.find(ve->name);
        if (tit == local_types_.end() || !isPtrType(tit->second))
            error(std::string("PostIncDerefAssign: '" + ve->name + "' is not a pointer ([]) type"));
        std::string pointee_type = tit->second.substr(0, tit->second.size()-2);
        std::string pointee_llvm = llvmType(pointee_type);
        int step = (pida->op == "++") ? 1 : -1;
        // load current ptr
        std::string cur_ptr = newTmp();
        out_ << "    " << cur_ptr << " = load ptr, ptr " << it->second << "\n";
        // assign value at current ptr — slid pointee dispatches op<-/op=.
        if (slid_info_.count(pointee_type)) {
            std::string src_ptr = emitArgForParam(*pida->value, pointee_type + "^");
            emitSlidSlotAssign(pointee_type, cur_ptr, src_ptr, pida->is_move);
        } else {
            requirePtrInit(pointee_type, *pida->value);
            std::string val = emitExpr(*pida->value);
            out_ << "    store " << pointee_llvm << " " << val << ", ptr " << cur_ptr << "\n";
        }
        // advance ptr
        std::string new_ptr = newTmp();
        out_ << "    " << new_ptr << " = getelementptr " << pointee_llvm << ", ptr " << cur_ptr << ", i32 " << step << "\n";
        out_ << "    store ptr " << new_ptr << ", ptr " << it->second << "\n";
        return;
    }

    if (auto* sw = dynamic_cast<const SwapStmt*>(&stmt)) {
        // lhs <-> rhs — swap values at two lvalue locations.
        // Two shapes are supported:
        //   1. ptr++^ <-> ptr--^   — pointer-deref element swap (advance both ptrs)
        //   2. var   <-> var       — plain lvalue swap (same type required)
        auto* l_pide = dynamic_cast<const PostIncDerefExpr*>(sw->lhs.get());
        auto* r_pide = dynamic_cast<const PostIncDerefExpr*>(sw->rhs.get());
        if (l_pide && r_pide) {
            auto emitPtrAndAdvance = [&](const PostIncDerefExpr& pide) -> std::pair<std::string, std::string> {
                auto* ve = dynamic_cast<const VarExpr*>(pide.operand.get());
                if (!ve) error(std::string("SwapStmt: only simple pointer variables supported"));
                auto it  = locals_.find(ve->name);
                auto tit = local_types_.find(ve->name);
                if (it == locals_.end())
                    error(std::string("SwapStmt: undefined variable '" + ve->name + "'"));
                if (tit == local_types_.end() || !isPtrType(tit->second))
                    error(std::string("SwapStmt: '" + ve->name + "' is not a pointer ([]) type"));
                std::string pointee_type = tit->second.substr(0, tit->second.size()-2);
                std::string pointee_llvm = llvmType(pointee_type);
                int step = (pide.op == "++") ? 1 : -1;
                std::string cur = newTmp();
                out_ << "    " << cur << " = load ptr, ptr " << it->second << "\n";
                std::string nxt = newTmp();
                out_ << "    " << nxt << " = getelementptr " << pointee_llvm << ", ptr " << cur << ", i32 " << step << "\n";
                out_ << "    store ptr " << nxt << ", ptr " << it->second << "\n";
                return {cur, pointee_llvm};
            };
            auto [lptr, ltype] = emitPtrAndAdvance(*l_pide);
            auto [rptr, rtype] = emitPtrAndAdvance(*r_pide);
            std::string lval = newTmp(), rval = newTmp();
            out_ << "    " << lval << " = load " << ltype << ", ptr " << lptr << "\n";
            out_ << "    " << rval << " = load " << rtype << ", ptr " << rptr << "\n";
            out_ << "    store " << rtype << " " << rval << ", ptr " << lptr << "\n";
            out_ << "    store " << ltype << " " << lval << ", ptr " << rptr << "\n";
            return;
        }
        // plain lvalue <-> plain lvalue (same type required).
        // Supported lvalue shapes: local VarExpr, current-slid field VarExpr, and
        // FieldAccessExpr through a slid pointer deref (ptr^.field).
        auto resolveLvalue = [&](const Expr& e) -> std::pair<std::string, std::string> {
            if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&e)) {
                auto [gep, et] = emitInlineArrayElemPtr(*ai->base, *ai->index);
                if (!gep.empty()) return {gep, et};
            }
            if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
                auto lit = locals_.find(ve->name);
                auto tit = local_types_.find(ve->name);
                if (lit != locals_.end() && tit != local_types_.end())
                    return {lit->second, tit->second};
                if (!current_slid_.empty()) {
                    auto& info = slid_info_[current_slid_];
                    auto fit = info.field_index.find(ve->name);
                    if (fit != info.field_index.end()) {
                        std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                             << ", ptr " << self << ", i32 0, i32 " << fit->second << "\n";
                        return {gep, info.field_types[fit->second]};
                    }
                }
                error(std::string("SwapStmt: undefined variable '" + ve->name + "'"));
            }
            if (auto* faE = dynamic_cast<const FieldAccessExpr*>(&e)) {
                if (auto* de = dynamic_cast<const DerefExpr*>(faE->object.get())) {
                    if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                        auto lit = locals_.find(ve->name);
                        auto tit = local_types_.find(ve->name);
                        if (lit == locals_.end() || tit == local_types_.end())
                            error(std::string("SwapStmt: undefined variable '" + ve->name + "'"));
                        std::string t = tit->second;
                        std::string pointee;
                        if (isPtrType(t)) pointee = t.substr(0, t.size()-2);
                        else if (!t.empty() && t.back() == '^') pointee = t.substr(0, t.size()-1);
                        else error(std::string("SwapStmt: '" + ve->name + "' is not a slid pointer"));
                        if (!slid_info_.count(pointee))
                            error(std::string("SwapStmt: '" + pointee + "' is not a slid type"));
                        auto& info = slid_info_[pointee];
                        auto fit = info.field_index.find(faE->field);
                        if (fit == info.field_index.end())
                            error(std::string("SwapStmt: unknown field '" + faE->field
                                + "' on '" + pointee + "'"));
                        std::string ptr_val = newTmp();
                        out_ << "    " << ptr_val << " = load ptr, ptr " << lit->second << "\n";
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr %struct." << pointee
                             << ", ptr " << ptr_val << ", i32 0, i32 " << fit->second << "\n";
                        return {gep, info.field_types[fit->second]};
                    }
                }
                error(std::string("SwapStmt: complex field access not supported"));
            }
            error(std::string("SwapStmt: unsupported lvalue shape"));
        };
        auto [a_ptr, a_type] = resolveLvalue(*sw->lhs);
        auto [b_ptr, b_type] = resolveLvalue(*sw->rhs);
        if (a_type != b_type)
            error(std::string("SwapStmt: type mismatch — '"
                + a_type + "' vs '" + b_type + "'"));
        const std::string& t = a_type;
        // slid type: dispatch user op<-> if defined, else default per-field swap
        if (slid_info_.count(t)) {
            emitSlidSlotSwap(t, a_ptr, b_ptr);
            return;
        }
        // anon-tuple: default per-slot swap
        if (isAnonTupleType(t)) {
            emitSlidSwap(t, a_ptr, b_ptr);
            return;
        }
        // inline array: deferred
        if (isInlineArrayType(t))
            error(std::string("SwapStmt: swap of inline-array variables not yet supported"));
        // pointer/iterator or primitive: 4-load/store exchange
        std::string llt = llvmType(t);
        std::string av = newTmp();
        std::string bv = newTmp();
        out_ << "    " << av << " = load " << llt << ", ptr " << a_ptr << "\n";
        out_ << "    " << bv << " = load " << llt << ", ptr " << b_ptr << "\n";
        out_ << "    store " << llt << " " << bv << ", ptr " << a_ptr << "\n";
        out_ << "    store " << llt << " " << av << ", ptr " << b_ptr << "\n";
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
                if (tit != local_types_.end() && isIndirectType(tit->second)) {
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load ptr, ptr " << it->second << "\n";
                    ptr_val = loaded;
                    slid_name = ( isPtrType(tit->second) ? tit->second.substr(0, tit->second.size()-2) : tit->second.substr(0, tit->second.size()-1) );
                } else {
                    ptr_val = it->second;
                    if (tit != local_types_.end()) slid_name = tit->second;
                }
            } else {
                ptr_val = emitExpr(*de->operand);
            }
            if (slid_name.empty() || !slid_info_.count(slid_name))
                error(std::string("DerefFieldAssign: unknown slid type for field '" + fa->field + "'"));
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            requirePtrInit(info.field_types[idx], *fa->value);
            std::string field_type = llvmType(info.field_types[idx]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << ptr_val << ", i32 0, i32 " << idx << "\n";
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
            if (fa->is_move && isIndirectType(info.field_types[idx]))
                emitNullOut(*fa->value);
            return;
        }
        if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            std::string gep = emitFieldPtr(ve->name, fa->field);
            std::string slid_name;
            auto type_it = local_types_.find(ve->name);
            if (type_it != local_types_.end()) {
                slid_name = type_it->second;
            } else if (!current_slid_.empty()) {
                auto& parent_info = slid_info_[current_slid_];
                slid_name = parent_info.field_types[parent_info.field_index.at(ve->name)];
            }
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            requirePtrInit(info.field_types[idx], *fa->value);
            std::string field_type = llvmType(info.field_types[idx]);
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
            if (fa->is_move && isIndirectType(info.field_types[idx]))
                emitNullOut(*fa->value);
            return;
        }
        // chained indexed field: tuple[idx].field = val  (or  <- val)
        if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(fa->object.get())) {
            auto* bve = dynamic_cast<const VarExpr*>(ai->base.get());
            if (!bve)
                error(std::string("chained indexed FieldAssign: complex base not supported"));
            auto tit = local_types_.find(bve->name);
            if (tit == local_types_.end() || !isAnonTupleType(tit->second))
                error(std::string("chained indexed FieldAssign: '" + bve->name
                    + "' is not a tuple"));
            auto elems = anonTupleElems(tit->second);
            int idx;
            if (!constExprToInt(*ai->index, enum_values_, idx))
                error(std::string("tuple index must be a constant integer"));
            if (idx < 0 || idx >= (int)elems.size())
                error(std::string("tuple index " + std::to_string(idx)
                    + " out of range (size " + std::to_string(elems.size()) + ")"));
            const std::string& slid_name = elems[idx];
            if (!slid_info_.count(slid_name))
                error(std::string("chained indexed FieldAssign: tuple element "
                    + std::to_string(idx) + " is not a slid type"));
            auto& info = slid_info_[slid_name];
            auto fit = info.field_index.find(fa->field);
            if (fit == info.field_index.end())
                error(std::string("unknown field '" + fa->field
                    + "' on slid '" + slid_name + "'"));
            int field_idx = fit->second;
            const std::string& ft = info.field_types[field_idx];
            requirePtrInit(ft, *fa->value);
            std::string ft_llvm = llvmType(ft);
            std::string slot_gep = emitFieldGep(tit->second, locals_[bve->name], idx);
            std::string field_gep = newTmp();
            out_ << "    " << field_gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << slot_gep << ", i32 0, i32 " << field_idx << "\n";
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << ft_llvm << " " << val << ", ptr " << field_gep << "\n";
            if (fa->is_move && isIndirectType(ft))
                emitNullOut(*fa->value);
            return;
        }
        error(std::string("complex field assignment not yet supported"));
    }

    if (auto* mcs = dynamic_cast<const MethodCallStmt*>(&stmt)) {
        // TypeName.sizeof() as a statement — call __$sizeof, discard result
        if (mcs->method == "sizeof") {
            std::string slid_name;
            if (auto* ve = dynamic_cast<const VarExpr*>(mcs->object.get())) {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) slid_name = tit->second;
                else if (slid_info_.count(ve->name)) slid_name = ve->name;
            }
            if (!slid_name.empty()) {
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call i64 @" << slid_name << "__$sizeof()\n";
                return;
            }
        }

        std::string slid_name, obj_ptr;
        if (auto* ve = dynamic_cast<const VarExpr*>(mcs->object.get())) {
            if (ve->name == "self" && !current_slid_.empty()) {
                slid_name = current_slid_;
                obj_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
            } else {
                auto type_it = local_types_.find(ve->name);
                if (type_it == local_types_.end())
                    error(std::string("unknown type for: " + ve->name));
                slid_name = type_it->second;
                if (mcs->method == "~") {
                    // Dtor is structural: valid on slids (dispatch), anon-tuples
                    // (walk slots), and primitive types (no-op). Reject only the
                    // pointer-to-slid case where the user likely meant the
                    // pointed-to slid's dtor, not the pointer's.
                    if (isIndirectType(slid_name)) {
                        std::string pointee = isPtrType(slid_name)
                            ? slid_name.substr(0, slid_name.size()-2)
                            : slid_name.substr(0, slid_name.size()-1);
                        if (slid_info_.count(pointee))
                            error("dtor on '" + ve->name + "' of pointer type '" + slid_name +
                                  "': use '" + ve->name + "^.~()' to dtor the pointed-to value");
                    }
                } else {
                    if (isIndirectType(slid_name))
                        error("method call on '" + ve->name + "' of pointer type '" + slid_name +
                              "': use '" + ve->name + "^." + mcs->method + "()' for explicit dereference");
                    if (!slid_info_.count(slid_name))
                        error("method call on '" + ve->name + "': '" + slid_name + "' is not a slid type");
                }
                obj_ptr = locals_[ve->name];
            }
        } else if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(mcs->object.get())) {
            // method call on tuple element: tuple[i].method(args) or p^[i].method(args)
            std::string tup_type;
            std::string tup_ptr;
            if (auto* bve = dynamic_cast<const VarExpr*>(ai->base.get())) {
                auto tit = local_types_.find(bve->name);
                if (tit != local_types_.end() && isAnonTupleType(tit->second)) {
                    tup_type = tit->second;
                    tup_ptr = locals_[bve->name];
                }
            } else if (auto* de = dynamic_cast<const DerefExpr*>(ai->base.get())) {
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto tit = local_types_.find(ve->name);
                    if (tit != local_types_.end()) {
                        std::string t = tit->second;
                        if (!t.empty() && t.back() == '^') t.pop_back();
                        else if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                            t.resize(t.size()-2);
                        if (isAnonTupleType(t)) {
                            tup_type = t;
                            tup_ptr = newTmp();
                            out_ << "    " << tup_ptr << " = load ptr, ptr "
                                 << locals_.at(ve->name) << "\n";
                        }
                    }
                }
            }
            if (!tup_type.empty()) {
                auto elems = anonTupleElems(tup_type);
                int idx;
                if (!constExprToInt(*ai->index, enum_values_, idx))
                    error(std::string("tuple index must be a constant integer"));
                if (idx < 0 || idx >= (int)elems.size())
                    error(std::string("tuple index " + std::to_string(idx)
                        + " out of range (size " + std::to_string(elems.size()) + ")"));
                if (!slid_info_.count(elems[idx]))
                    error(std::string("tuple element " + std::to_string(idx)
                        + " is not a slid type"));
                slid_name = elems[idx];
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr " << llvmType(tup_type)
                     << ", ptr " << tup_ptr << ", i32 0, i32 " << idx << "\n";
                obj_ptr = gep;
            }
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mcs->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto type_it = local_types_.find(ve2->name);
                if (type_it == local_types_.end())
                    error(std::string("unknown type for: " + ve2->name));
                slid_name = type_it->second;
                if (isRefType(slid_name)) slid_name.pop_back(); else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << locals_[ve2->name] << "\n";
                obj_ptr = loaded;
            }
        }
        if (!slid_name.empty() && mcs->method == "~") {
            emitExplicitDtor(slid_name, obj_ptr);
            return;
        }
        if (!slid_name.empty()) {
            auto& sinfo = slid_info_[slid_name];
            // virtual dispatch: if the static type is virtual and this method
            // matches a vtable slot, use the slot's resolved impl. Indirect
            // call when the object is a pointer-deref; static call otherwise.
            int vslot = -1;
            if (sinfo.is_virtual_class) {
                for (int i = 0; i < (int)sinfo.vtable.size(); i++) {
                    if (sinfo.vtable[i].method_name != mcs->method) continue;
                    if (sinfo.vtable[i].param_types.size() != mcs->args.size()) continue;
                    vslot = i; break;
                }
            }
            std::string mangled;
            std::vector<std::string> mptypes;
            std::string ret_slids;
            if (vslot >= 0) {
                mangled = sinfo.vtable[vslot].mangled;
                mptypes = sinfo.vtable[vslot].param_types;
                ret_slids = sinfo.vtable[vslot].return_type;
            } else {
                std::string base = slid_name + "__" + mcs->method;
                mangled = resolveOverloadForCall(base, mcs->args);
                auto ret_it = func_return_types_.find(mangled);
                if (ret_it == func_return_types_.end())
                    error(std::string("unknown method: " + mcs->method));
                ret_slids = ret_it->second;
                mptypes = func_param_types_[mangled];
            }
            std::string ret_type = llvmType(ret_slids);
            bool empty = sinfo.is_empty;
            std::string arg_str = empty ? "" : "ptr " + obj_ptr;
            for (int i = 0; i < (int)mcs->args.size(); i++) {
                std::string ptype_str = (i < (int)mptypes.size()) ? mptypes[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                std::string aval = emitArgForParam(*mcs->args[i], ptype_str);
                if (!arg_str.empty()) arg_str += ", ";
                arg_str += ptype + " " + aval;
            }
            // indirect call: when the static type is virtual AND the object's
            // runtime type may differ from its static type. Two cases:
            //   - pointer-deref: ptr^.method()
            //   - explicit self: self.method() inside a virtual method (the
            //     runtime type is the most-derived class, not current_slid_).
            bool is_self = false;
            if (auto* mve = dynamic_cast<const VarExpr*>(mcs->object.get()))
                is_self = (mve->name == "self");
            bool indirect = (vslot >= 0)
                && (dynamic_cast<const DerefExpr*>(mcs->object.get()) != nullptr || is_self);
            if (indirect) {
                int n = (int)sinfo.vtable.size();
                std::string vptr_field = newTmp();
                out_ << "    " << vptr_field << " = getelementptr %struct."
                     << slid_name << ", ptr " << obj_ptr << ", i32 0, i32 0\n";
                std::string vtbl = newTmp();
                out_ << "    " << vtbl << " = load ptr, ptr " << vptr_field << "\n";
                std::string slot_ptr = newTmp();
                out_ << "    " << slot_ptr << " = getelementptr [" << n
                     << " x ptr], ptr " << vtbl << ", i32 0, i32 " << vslot << "\n";
                std::string fn = newTmp();
                out_ << "    " << fn << " = load ptr, ptr " << slot_ptr << "\n";
                if (ret_type == "void" || ret_type.empty()) {
                    out_ << "    call void " << fn << "(" << arg_str << ")\n";
                } else {
                    std::string tmp = newTmp();
                    out_ << "    " << tmp << " = call " << ret_type << " " << fn
                         << "(" << arg_str << ")\n";
                }
                return;
            }
            if (ret_type == "void" || ret_type.empty()) {
                out_ << "    call void @" << llvmGlobalName(mangled) << "(" << arg_str << ")\n";
            } else {
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
                     << "(" << arg_str << ")\n";
            }
            return;
        }
        error(std::string("complex method call statement not yet supported"));
    }

    if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (current_func_uses_sret_ && ret->value) {
            // sret return: move return value into %retval, dtor locals, ret void
            auto* ve = dynamic_cast<const VarExpr*>(ret->value.get());
            std::string slid_name;
            std::string src;
            bool src_is_fresh_temp = false;  // true if src is a new alloca not in dtor_vars_
            if (ve) {
                auto tit = local_types_.find(ve->name);
                if (tit == local_types_.end() || !slid_info_.count(tit->second))
                    error(std::string("sret: return value must be a slid type"));
                slid_name = tit->second;
                src = locals_.at(ve->name);
            } else {
                // expression: emit it (BinaryExpr/CallExpr produce a fresh alloca)
                slid_name = exprSlidType(*ret->value);
                if (slid_name.empty())
                    error(std::string("sret: return expression must produce a slid type"));
                src = emitExpr(*ret->value);
                src_is_fresh_temp = true;
            }
            auto& info = slid_info_[slid_name];
            if (!info.field_types.empty()) {
                // known fields: field-by-field move into %retval (implicit move — steal + null src ptrs)
                for (int i = 0; i < (int)info.field_types.size(); i++) {
                    std::string ft = llvmType(info.field_types[i]);
                    std::string sp = newTmp();
                    std::string dp = newTmp();
                    std::string fv = newTmp();
                    out_ << "    " << sp << " = getelementptr %struct." << slid_name << ", ptr " << src << ", i32 0, i32 " << i << "\n";
                    out_ << "    " << dp << " = getelementptr %struct." << slid_name << ", ptr %retval, i32 0, i32 " << i << "\n";
                    out_ << "    " << fv << " = load " << ft << ", ptr " << sp << "\n";
                    out_ << "    store " << ft << " " << fv << ", ptr " << dp << "\n";
                }
                // null out pointer fields in src so its dtor won't double-free
                for (int i = 0; i < (int)info.field_types.size(); i++) {
                    if (isIndirectType(info.field_types[i])) {
                        std::string sp = newTmp();
                        out_ << "    " << sp << " = getelementptr %struct." << slid_name << ", ptr " << src << ", i32 0, i32 " << i << "\n";
                        out_ << "    store ptr null, ptr " << sp << "\n";
                    }
                }
            } else {
                // opaque (transport) type: init %retval, then move or copy from src
                emitCtorCall(slid_name, "%retval");
                // prefer op<- (move), fallback to op= (copy)
                std::string move_func;
                auto mit = method_overloads_.find(slid_name + "__op<-");
                if (mit != method_overloads_.end())
                    for (auto& [m, ptypes] : mit->second)
                        if (ptypes.size() == 1 && isRefType(ptypes[0])) { move_func = m; break; }
                if (!move_func.empty()) {
                    out_ << "    call void @" << llvmGlobalName(move_func)
                         << "(ptr %retval, ptr " << src << ")\n";
                } else {
                    std::string copy_func = resolveSingleArgOverload(slid_name + "__op=", *ret->value);
                    if (!copy_func.empty()) {
                        auto& cptypes = func_param_types_[copy_func];
                        out_ << "    call void @" << llvmGlobalName(copy_func)
                             << "(ptr %retval, " << llvmType(cptypes[0]) << " " << src << ")\n";
                    }
                }
            }
            // if src was a fresh temp (not a named local), dtor it now
            // (its ptr fields are already null after the move, so dtor is a no-op for resources)
            if (src_is_fresh_temp && info.has_dtor)
                emitDtorChainCall(slid_name, src);
            emitDtors();
            out_ << "    ret void\n";
        } else {
            if (ret->value) {
                if (auto* te = dynamic_cast<const TupleExpr*>(ret->value.get())) {
                    // Per desugar rule: build the return struct slot-by-slot. Scalar
                    // slots store directly; slid/anon-tuple slots dispatch via
                    // emitSlidAssign — fresh temps get moved, named locals copied.
                    std::string ret_tup = newTmp();
                    out_ << "    " << ret_tup << " = alloca " << current_func_return_type_ << "\n";
                    auto& fields = current_func_tuple_fields_;
                    for (int i = 0; i < (int)te->values.size(); i++) {
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr " << current_func_return_type_
                             << ", ptr " << ret_tup << ", i32 0, i32 " << i << "\n";
                        const std::string& elem_type = fields[i].first;
                        if (slid_info_.count(elem_type) || isAnonTupleType(elem_type)) {
                            std::string src_ptr;
                            if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                                src_ptr = locals_[ve->name];
                            } else {
                                src_ptr = emitExpr(*te->values[i]);
                            }
                            bool is_move = isFreshSlidTemp(*te->values[i]);
                            emitSlidSlotAssign(elem_type, gep, src_ptr, is_move);
                        } else {
                            requirePtrInit(elem_type, *te->values[i]);
                            std::string val = emitExpr(*te->values[i]);
                            out_ << "    store " << llvmType(elem_type) << " " << val
                                 << ", ptr " << gep << "\n";
                        }
                    }
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load " << current_func_return_type_
                         << ", ptr " << ret_tup << "\n";
                    emitDtors();
                    out_ << "    ret " << current_func_return_type_ << " " << loaded << "\n";
                } else {
                    // If any slot is a slid or nested anon-tuple, dispatch element-wise
                    // via emitSlidAssign on the source ptr (copy — no `return <-` syntax).
                    bool has_struct_slot = false;
                    for (auto& [ft, fn] : current_func_tuple_fields_)
                        if (slid_info_.count(ft) || isAnonTupleType(ft)) { has_struct_slot = true; break; }
                    if (has_struct_slot) {
                        std::string src_ptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(ret->value.get())) {
                            src_ptr = locals_[ve->name];
                        } else {
                            std::string val = emitExpr(*ret->value);
                            src_ptr = newTmp();
                            out_ << "    " << src_ptr << " = alloca " << current_func_return_type_ << "\n";
                            out_ << "    store " << current_func_return_type_ << " " << val
                                 << ", ptr " << src_ptr << "\n";
                        }
                        std::string ret_tup = newTmp();
                        out_ << "    " << ret_tup << " = alloca " << current_func_return_type_ << "\n";
                        // reconstruct Slids-form tuple type string from fields for emitSlidAssign
                        std::string slids_tuple_type = "(";
                        for (size_t i = 0; i < current_func_tuple_fields_.size(); i++) {
                            if (i) slids_tuple_type += ",";
                            slids_tuple_type += current_func_tuple_fields_[i].first;
                        }
                        slids_tuple_type += ")";
                        emitSlidSlotAssign(slids_tuple_type, ret_tup, src_ptr, /*is_move=*/false);
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load " << current_func_return_type_
                             << ", ptr " << ret_tup << "\n";
                        emitDtors();
                        out_ << "    ret " << current_func_return_type_ << " " << loaded << "\n";
                    } else {
                        requirePtrInit(current_func_slids_return_type_, *ret->value);
                        std::string val = emitExpr(*ret->value);
                        emitDtors();
                        out_ << "    ret " << current_func_return_type_ << " " << val << "\n";
                    }
                }
            } else {
                emitDtors();
                out_ << "    ret void\n";
            }
        }
        block_terminated_ = true;
        return;
    }

    // nested function definition — already emitted after parent, skip here
    if (dynamic_cast<const NestedFunctionDefStmt*>(&stmt)) return;

    if (auto* brk = dynamic_cast<const BreakStmt*>(&stmt)) {
        std::string target;
        int target_frame = (int)loop_stack_.size() - 1;
        if (!brk->label.empty()) {
            // named break: find the frame with this label
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].block_label == brk->label) {
                    target = loop_stack_[i].break_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("break: unknown label '" + brk->label + "'"));
        } else if (brk->number > 0) {
            // numbered break: count outward N loop frames, skipping switch frames
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].is_switch) continue;
                count++;
                if (count == brk->number) {
                    target = loop_stack_[i].break_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("break " + std::to_string(brk->number) + ": not enough enclosing loops"));
        } else {
            if (break_label_.empty()) error(std::string("break outside of loop"));
            target = break_label_;
        }
        emitStackRestore(target_frame);
        out_ << "    br label %" << target << "\n";
        block_terminated_ = true;
        return;
    }

    if (auto* cont = dynamic_cast<const ContinueStmt*>(&stmt)) {
        std::string target;
        int target_frame = (int)loop_stack_.size() - 1;
        if (!cont->label.empty()) {
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].block_label == cont->label) {
                    target = loop_stack_[i].continue_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("continue: unknown label '" + cont->label + "'"));
        } else if (cont->number > 0) {
            // numbered continue: count outward N loop frames, skipping switch frames
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].is_switch) continue;
                count++;
                if (count == cont->number) {
                    target = loop_stack_[i].continue_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("continue " + std::to_string(cont->number) + ": not enough enclosing loops"));
        } else {
            if (continue_label_.empty()) error(std::string("continue outside of loop"));
            target = continue_label_;
        }
        emitStackRestore(target_frame);
        out_ << "    br label %" << target << "\n";
        block_terminated_ = true;
        return;
    }

    if (auto* if_stmt = dynamic_cast<const IfStmt*>(&stmt)) {
        std::string then_lbl = newLabel("if_then");
        std::string else_lbl = newLabel("if_else");
        std::string end_lbl  = newLabel("if_end");
        std::string cond_bool = emitCondBool(*if_stmt->cond);
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
        loop_stack_.push_back({w->block_label, end_lbl, cond_lbl, ""});
        if (w->bottom_condition) {
            // do-while: body first, condition at bottom
            out_ << "    br label %" << body_lbl << "\n";
            block_terminated_ = false;
            out_ << body_lbl << ":\n";
            { std::string sp = newTmp();
              out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
              loop_stack_.back().stack_ptr_reg = sp;
              emitBlock(*w->body);
              if (!block_terminated_) {
                  out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
                  out_ << "    br label %" << cond_lbl << "\n";
              }
            }
            block_terminated_ = false;
            out_ << cond_lbl << ":\n";
            std::string cond_bool = emitCondBool(*w->cond);
            out_ << "    br i1 " << cond_bool << ", label %" << body_lbl << ", label %" << end_lbl << "\n";
        } else {
            out_ << "    br label %" << cond_lbl << "\n";
            block_terminated_ = false;
            out_ << cond_lbl << ":\n";
            std::string cond_bool = emitCondBool(*w->cond);
            out_ << "    br i1 " << cond_bool << ", label %" << body_lbl << ", label %" << end_lbl << "\n";
            block_terminated_ = false;
            out_ << body_lbl << ":\n";
            { std::string sp = newTmp();
              out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
              loop_stack_.back().stack_ptr_reg = sp;
              emitBlock(*w->body);
              if (!block_terminated_) {
                  out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
                  out_ << "    br label %" << cond_lbl << "\n";
              }
            }
        }
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
        loop_stack_.push_back({f->block_label, end_lbl, incr_lbl, ""});

        std::string var_reg;
        bool new_var = !f->var_type.empty();
        if (new_var) {
            // declare new loop variable
            var_reg = uniqueAllocaReg(f->var_name);
            out_ << "    " << var_reg << " = alloca i32\n";
            locals_[f->var_name] = var_reg;
        } else {
            // reuse existing variable
            auto it = locals_.find(f->var_name);
            if (it == locals_.end())
                error(std::string("for loop: undefined variable '" + f->var_name + "'"));
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
        { std::string sp = newTmp();
          out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
          loop_stack_.back().stack_ptr_reg = sp;
          emitBlock(*f->body);
          if (!block_terminated_) {
              out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
              out_ << "    br label %" << incr_lbl << "\n";
          }
        }

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
        // intentionally do NOT erase new_var from locals_ — the variable persists
        // after the loop (e.g. for use after a break, as in chess2.sl row/col)
        loop_stack_.pop_back();
        break_label_ = saved_break; continue_label_ = saved_continue;
        return;
    }

    if (auto* f = dynamic_cast<const ForEnumStmt*>(&stmt)) {
        // for EnumType var in EnumType — iterate 0..enum_size
        auto sit = enum_sizes_.find(f->enum_name);
        if (sit == enum_sizes_.end())
            error(std::string("for-enum: unknown enum type '" + f->enum_name + "'"));
        int size = sit->second;

        std::string init_lbl = newLabel("for_init");
        std::string cond_lbl = newLabel("for_cond");
        std::string body_lbl = newLabel("for_body");
        std::string incr_lbl = newLabel("for_incr");
        std::string end_lbl  = newLabel("for_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl; continue_label_ = incr_lbl;
        loop_stack_.push_back({f->block_label, end_lbl, incr_lbl, ""});

        std::string var_reg = uniqueAllocaReg(f->var_name);
        out_ << "    " << var_reg << " = alloca i32\n";
        locals_[f->var_name] = var_reg;
        local_types_[f->var_name] = f->var_type;

        out_ << "    br label %" << init_lbl << "\n";
        block_terminated_ = false;
        out_ << init_lbl << ":\n";
        out_ << "    store i32 0, ptr " << var_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << cond_lbl << ":\n";
        std::string cur = newTmp();
        out_ << "    " << cur << " = load i32, ptr " << var_reg << "\n";
        std::string cmp = newTmp();
        out_ << "    " << cmp << " = icmp slt i32 " << cur << ", " << size << "\n";
        out_ << "    br i1 " << cmp << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

        block_terminated_ = false;
        out_ << body_lbl << ":\n";
        { std::string sp = newTmp();
          out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
          loop_stack_.back().stack_ptr_reg = sp;
          emitBlock(*f->body);
          if (!block_terminated_) {
              out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
              out_ << "    br label %" << incr_lbl << "\n";
          }
        }

        block_terminated_ = false;
        out_ << incr_lbl << ":\n";
        std::string old_v = newTmp();
        out_ << "    " << old_v << " = load i32, ptr " << var_reg << "\n";
        std::string new_v = newTmp();
        out_ << "    " << new_v << " = add i32 " << old_v << ", 1\n";
        out_ << "    store i32 " << new_v << ", ptr " << var_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << end_lbl << ":\n";
        loop_stack_.pop_back();
        break_label_ = saved_break; continue_label_ = saved_continue;
        return;
    }

    if (auto* ft = dynamic_cast<const ForTupleStmt*>(&stmt)) {
        std::string cond_lbl = newLabel("for_cond");
        std::string body_lbl = newLabel("for_body");
        std::string incr_lbl = newLabel("for_incr");
        std::string end_lbl  = newLabel("for_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl; continue_label_ = incr_lbl;
        loop_stack_.push_back({ft->block_label, end_lbl, incr_lbl, ""});

        int n = (int)ft->elements.size();
        std::string elem_llvm = exprLlvmType(*ft->elements[0]);
        std::string elem_slids = inferSlidType(*ft->elements[0]);

        // allocate flat array and store each element
        std::string arr_reg = newTmp();
        out_ << "    " << arr_reg << " = alloca [" << n << " x " << elem_llvm << "]\n";
        for (int i = 0; i < n; i++) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << n << " x " << elem_llvm
                 << "], ptr " << arr_reg << ", i32 0, i32 " << i << "\n";
            std::string val = emitExpr(*ft->elements[i]);
            out_ << "    store " << elem_llvm << " " << val << ", ptr " << gep << "\n";
        }

        // allocate hidden loop index
        std::string idx_reg = newTmp();
        out_ << "    " << idx_reg << " = alloca i32\n";
        out_ << "    store i32 0, ptr " << idx_reg << "\n";

        // allocate loop variable if new
        bool new_var = !locals_.count(ft->var_name);
        std::string var_reg;
        if (new_var) {
            var_reg = uniqueAllocaReg(ft->var_name);
            out_ << "    " << var_reg << " = alloca " << elem_llvm << "\n";
            locals_[ft->var_name] = var_reg;
            local_types_[ft->var_name] = elem_slids;
        } else {
            var_reg = locals_[ft->var_name];
        }

        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << cond_lbl << ":\n";
        std::string cur = newTmp();
        out_ << "    " << cur << " = load i32, ptr " << idx_reg << "\n";
        std::string cmp = newTmp();
        out_ << "    " << cmp << " = icmp slt i32 " << cur << ", " << n << "\n";
        out_ << "    br i1 " << cmp << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

        block_terminated_ = false;
        out_ << body_lbl << ":\n";
        { std::string sp = newTmp();
          out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
          loop_stack_.back().stack_ptr_reg = sp;
          // load element[idx] into loop variable
          std::string cur2 = newTmp();
          out_ << "    " << cur2 << " = load i32, ptr " << idx_reg << "\n";
          std::string elem_gep = newTmp();
          out_ << "    " << elem_gep << " = getelementptr [" << n << " x " << elem_llvm
               << "], ptr " << arr_reg << ", i32 0, i32 " << cur2 << "\n";
          std::string elem_val = newTmp();
          out_ << "    " << elem_val << " = load " << elem_llvm << ", ptr " << elem_gep << "\n";
          out_ << "    store " << elem_llvm << " " << elem_val << ", ptr " << var_reg << "\n";
          emitBlock(*ft->body);
          if (!block_terminated_) {
              out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
              out_ << "    br label %" << incr_lbl << "\n";
          }
        }

        block_terminated_ = false;
        out_ << incr_lbl << ":\n";
        std::string old_idx = newTmp();
        out_ << "    " << old_idx << " = load i32, ptr " << idx_reg << "\n";
        std::string new_idx = newTmp();
        out_ << "    " << new_idx << " = add i32 " << old_idx << ", 1\n";
        out_ << "    store i32 " << new_idx << ", ptr " << idx_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << end_lbl << ":\n";
        if (new_var) {
            locals_.erase(ft->var_name);
            local_types_.erase(ft->var_name);
        }
        loop_stack_.pop_back();
        break_label_ = saved_break; continue_label_ = saved_continue;
        return;
    }

    if (auto* fa = dynamic_cast<const ForArrayStmt*>(&stmt)) {
        std::string cond_lbl = newLabel("for_cond");
        std::string body_lbl = newLabel("for_body");
        std::string incr_lbl = newLabel("for_incr");
        std::string end_lbl  = newLabel("for_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl; continue_label_ = incr_lbl;
        loop_stack_.push_back({fa->block_label, end_lbl, incr_lbl, ""});

        std::string elem_slids, elem_llvm, arr_llvm, arr_ptr;
        int arr_count = 0;
        bool is_class_iter = false;
        std::string class_ptr, begin_fn, end_fn;

        if (auto* sl = dynamic_cast<const StringLiteralExpr*>(fa->array_expr.get())) {
            std::string label = registerStringConstant(sl->value);
            int len; llvmEscape(sl->value, len);
            arr_count  = len - 1;  // skip null terminator
            elem_slids = "char";
            elem_llvm  = "i8";
            arr_llvm   = "[" + std::to_string(len) + " x i8]";
            arr_ptr    = label;
        } else if (auto* ve = dynamic_cast<const VarExpr*>(fa->array_expr.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit == local_types_.end())
                error(std::string("for-in: unknown variable '" + ve->name + "'"));
            std::string arr_type = tit->second;
            auto lb = arr_type.rfind('[');
            if (lb != std::string::npos && lb > 0 && arr_type.back() == ']') {
                elem_slids = arr_type.substr(0, lb);
                std::string sz_str = arr_type.substr(lb + 1, arr_type.size() - lb - 2);
                arr_count  = std::stoi(sz_str);
                elem_llvm  = llvmType(elem_slids);
                arr_llvm   = "[" + sz_str + " x " + elem_llvm + "]";
                arr_ptr    = locals_[ve->name];
            } else if (slid_info_.count(arr_type)) {
                is_class_iter = true;
                class_ptr = locals_[ve->name];
                begin_fn  = arr_type + "__begin";
                end_fn    = arr_type + "__end";
                auto rit  = func_return_types_.find(begin_fn);
                if (rit == func_return_types_.end())
                    error(std::string("for-in: '" + arr_type + "' has no begin() method"));
                elem_slids = rit->second;
                elem_llvm  = llvmType(elem_slids);
            } else {
                error(std::string("for-in: '" + ve->name + "' is not a fixed-size array or iterable class"));
            }
        } else {
            error(std::string("for-in: expected string literal or array variable"));
        }

        if (is_class_iter) {
            std::string init_lbl = newLabel("for_init");
            std::string end_reg  = newTmp() + "_end";
            out_ << "    " << end_reg << " = alloca " << elem_llvm << "\n";

            bool new_var = !locals_.count(fa->var_name);
            std::string var_reg;
            if (new_var) {
                var_reg = uniqueAllocaReg(fa->var_name);
                out_ << "    " << var_reg << " = alloca " << elem_llvm << "\n";
                locals_[fa->var_name] = var_reg;
                local_types_[fa->var_name] = elem_slids;
            } else {
                var_reg = locals_[fa->var_name];
            }

            out_ << "    br label %" << init_lbl << "\n";
            block_terminated_ = false;
            out_ << init_lbl << ":\n";
            std::string bv = newTmp();
            out_ << "    " << bv << " = call " << elem_llvm << " @" << begin_fn << "(ptr " << class_ptr << ")\n";
            out_ << "    store " << elem_llvm << " " << bv << ", ptr " << var_reg << "\n";
            std::string ev = newTmp();
            out_ << "    " << ev << " = call " << elem_llvm << " @" << end_fn << "(ptr " << class_ptr << ")\n";
            out_ << "    store " << elem_llvm << " " << ev << ", ptr " << end_reg << "\n";
            out_ << "    br label %" << cond_lbl << "\n";

            block_terminated_ = false;
            out_ << cond_lbl << ":\n";
            std::string cur = newTmp();
            out_ << "    " << cur << " = load " << elem_llvm << ", ptr " << var_reg << "\n";
            std::string lim = newTmp();
            out_ << "    " << lim << " = load " << elem_llvm << ", ptr " << end_reg << "\n";
            std::string cmp = newTmp();
            out_ << "    " << cmp << " = icmp slt " << elem_llvm << " " << cur << ", " << lim << "\n";
            out_ << "    br i1 " << cmp << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

            block_terminated_ = false;
            out_ << body_lbl << ":\n";
            { std::string sp = newTmp();
              out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
              loop_stack_.back().stack_ptr_reg = sp;
              emitBlock(*fa->body);
              if (!block_terminated_) {
                  out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
                  out_ << "    br label %" << incr_lbl << "\n";
              }
            }

            block_terminated_ = false;
            out_ << incr_lbl << ":\n";
            std::string ov = newTmp();
            out_ << "    " << ov << " = load " << elem_llvm << ", ptr " << var_reg << "\n";
            std::string nv = newTmp();
            out_ << "    " << nv << " = add " << elem_llvm << " " << ov << ", 1\n";
            out_ << "    store " << elem_llvm << " " << nv << ", ptr " << var_reg << "\n";
            out_ << "    br label %" << cond_lbl << "\n";

            block_terminated_ = false;
            out_ << end_lbl << ":\n";
            if (new_var) {
                locals_.erase(fa->var_name);
                local_types_.erase(fa->var_name);
            }
            loop_stack_.pop_back();
            break_label_ = saved_break; continue_label_ = saved_continue;
            return;
        }

        // string literal or fixed-size array: index into flat array
        std::string idx_reg = newTmp();
        out_ << "    " << idx_reg << " = alloca i32\n";
        out_ << "    store i32 0, ptr " << idx_reg << "\n";

        bool new_var = !locals_.count(fa->var_name);
        std::string var_reg;
        if (new_var) {
            var_reg = uniqueAllocaReg(fa->var_name);
            out_ << "    " << var_reg << " = alloca " << elem_llvm << "\n";
            locals_[fa->var_name] = var_reg;
            local_types_[fa->var_name] = elem_slids;
        } else {
            var_reg = locals_[fa->var_name];
        }

        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << cond_lbl << ":\n";
        std::string cur = newTmp();
        out_ << "    " << cur << " = load i32, ptr " << idx_reg << "\n";
        std::string cmp = newTmp();
        out_ << "    " << cmp << " = icmp slt i32 " << cur << ", " << arr_count << "\n";
        out_ << "    br i1 " << cmp << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

        block_terminated_ = false;
        out_ << body_lbl << ":\n";
        { std::string sp = newTmp();
          out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
          loop_stack_.back().stack_ptr_reg = sp;
          std::string cur2 = newTmp();
          out_ << "    " << cur2 << " = load i32, ptr " << idx_reg << "\n";
          std::string elem_gep = newTmp();
          out_ << "    " << elem_gep << " = getelementptr " << arr_llvm
               << ", ptr " << arr_ptr << ", i32 0, i32 " << cur2 << "\n";
          std::string elem_val = newTmp();
          out_ << "    " << elem_val << " = load " << elem_llvm << ", ptr " << elem_gep << "\n";
          out_ << "    store " << elem_llvm << " " << elem_val << ", ptr " << var_reg << "\n";
          emitBlock(*fa->body);
          if (!block_terminated_) {
              out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
              out_ << "    br label %" << incr_lbl << "\n";
          }
        }

        block_terminated_ = false;
        out_ << incr_lbl << ":\n";
        std::string old_idx = newTmp();
        out_ << "    " << old_idx << " = load i32, ptr " << idx_reg << "\n";
        std::string new_idx = newTmp();
        out_ << "    " << new_idx << " = add i32 " << old_idx << ", 1\n";
        out_ << "    store i32 " << new_idx << ", ptr " << idx_reg << "\n";
        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << end_lbl << ":\n";
        if (new_var) {
            locals_.erase(fa->var_name);
            local_types_.erase(fa->var_name);
        }
        loop_stack_.pop_back();
        break_label_ = saved_break; continue_label_ = saved_continue;
        return;
    }

    if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
        std::string end_lbl = newLabel("sw_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl;
        continue_label_ = "";
        loop_stack_.push_back({sw->block_label, end_lbl, "", "", true});

        std::string disc = emitExpr(*sw->expr);

        // build per-case labels
        std::vector<std::string> case_lbls;
        for (int i = 0; i < (int)sw->cases.size(); i++)
            case_lbls.push_back(newLabel("sw_case"));

        // emit LLVM switch instruction
        // find default label (if any), else use end_lbl
        std::string default_lbl = end_lbl;
        for (int i = 0; i < (int)sw->cases.size(); i++)
            if (!sw->cases[i].value) { default_lbl = case_lbls[i]; break; }

        out_ << "    switch i32 " << disc << ", label %" << default_lbl << " [\n";
        for (int i = 0; i < (int)sw->cases.size(); i++) {
            if (!sw->cases[i].value) continue; // skip default
            int val;
            if (!constExprToInt(*sw->cases[i].value, enum_values_, val))
                error(std::string("switch case value must be a constant integer or enum"));
            out_ << "        i32 " << val << ", label %" << case_lbls[i] << "\n";
        }
        out_ << "    ]\n";
        block_terminated_ = true;

        // emit each case body — fallthrough to next case if no break
        for (int i = 0; i < (int)sw->cases.size(); i++) {
            block_terminated_ = false;
            out_ << case_lbls[i] << ":\n";
            for (auto& s : sw->cases[i].stmts)
                emitStmt(*s);
            // fallthrough: branch to next case label, or end if last
            if (!block_terminated_) {
                std::string next = (i + 1 < (int)case_lbls.size())
                    ? case_lbls[i + 1] : end_lbl;
                out_ << "    br label %" << next << "\n";
            }
        }

        block_terminated_ = false;
        out_ << end_lbl << ":\n";
        loop_stack_.pop_back();
        break_label_ = saved_break;
        continue_label_ = saved_continue;
        return;
    }

    if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
        emitExpr(*es->expr); // evaluate for side effects, discard result
        return;
    }

    if (auto* call = dynamic_cast<const CallStmt*>(&stmt)) {
        if (call->callee == "__println" || call->callee == "__print") {
            bool newline = (call->callee == "__println");
            if (call->args.size() > 1)
                error(std::string(call->callee + " expects 0 or 1 arguments"));

            if (call->args.size() == 0) {
                if (!newline) error(std::string("__print() requires an argument"));
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = getelementptr [2 x i8], ptr @.str_newline, i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                return;
            }

            // helper: emit a slice expr via printf("%.*s", len, ptr)
            auto emitSlice = [&](const SliceExpr* sl, bool nl) {
                std::string base_ptr = emitExpr(*sl->base);
                std::string start_val = emitExpr(*sl->start);
                std::string end_val   = emitExpr(*sl->end);
                std::string start_type = exprLlvmType(*sl->start);
                std::string end_type   = exprLlvmType(*sl->end);
                // use the wider of start/end types for the subtraction
                bool is64 = (start_type == "i64" || end_type == "i64");
                std::string idx_type = is64 ? "i64" : "i32";
                // widen start/end if needed
                auto widen = [&](const std::string& val, const std::string& from) -> std::string {
                    if (!is64 || from == "i64") return val;
                    std::string w = newTmp();
                    out_ << "    " << w << " = sext " << from << " " << val << " to i64\n";
                    return w;
                };
                std::string sv = widen(start_val, start_type);
                std::string ev = widen(end_val, end_type);
                std::string sliced = newTmp();
                out_ << "    " << sliced << " = getelementptr i8, ptr " << base_ptr << ", " << idx_type << " " << sv << "\n";
                std::string len = newTmp();
                out_ << "    " << len << " = sub " << idx_type << " " << ev << ", " << sv << "\n";
                // printf("%.*s", ...) needs i32 length
                std::string len32 = len;
                if (is64) {
                    len32 = newTmp();
                    out_ << "    " << len32 << " = trunc i64 " << len << " to i32\n";
                }
                std::string fmt = newTmp();
                std::string fmt_name = nl ? "@.fmt_slice" : "@.fmt_slice_nonl";
                int fmt_size = nl ? 6 : 5;
                out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", i32 " << len32 << ", ptr " << sliced << ")\n";
            };

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

            // char array variable: println(dest) where dest is char[N] — print as string
            if (segments.size() == 1) {
                if (auto* ve = dynamic_cast<const VarExpr*>(segments[0])) {
                    auto ait = array_info_.find(ve->name);
                    if (ait != array_info_.end() && ait->second.elem_type == "char") {
                        int total = 1;
                        for (int d : ait->second.dims) total *= d;
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr [" << total << " x i8], ptr "
                             << ait->second.alloca_reg << ", i32 0, i32 0\n";
                        std::string fmt = newTmp();
                        std::string fmt_name = newline ? "@.fmt_str" : "@.fmt_str_nonl";
                        int fmt_size = newline ? 4 : 3;
                        out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                        out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", ptr " << gep << ")\n";
                        return;
                    }
                    // char[] pointer variable (initialized from string literal): print as string
                    auto tit = local_types_.find(ve->name);
                    if (tit != local_types_.end() && tit->second == "char[]") {
                        auto lit = locals_.find(ve->name);
                        std::string ptr_val = newTmp();
                        out_ << "    " << ptr_val << " = load ptr, ptr " << lit->second << "\n";
                        std::string fmt = newTmp();
                        std::string fmt_name = newline ? "@.fmt_str" : "@.fmt_str_nonl";
                        int fmt_size = newline ? 4 : 3;
                        out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                        out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", ptr " << ptr_val << ")\n";
                        return;
                    }
                }
                // slice expr: println(s[0..len])
                if (auto* sl = dynamic_cast<const SliceExpr*>(segments[0])) {
                    emitSlice(sl, newline);
                    return;
                }
            }

            bool is_concat = segments.size() > 1 ||
                (segments.size() == 1 && !dynamic_cast<const StringLiteralExpr*>(segments[0]));

            if (!is_concat && segments.size() == 1) {
                // single float/double expr
                {
                    std::string val_type_check = exprLlvmType(*segments[0]);
                    if (val_type_check == "float" || val_type_check == "double") {
                        std::string val = emitExpr(*segments[0]);
                        // printf varargs promote float to double
                        if (val_type_check == "float") {
                            std::string ext = newTmp();
                            out_ << "    " << ext << " = fpext float " << val << " to double\n";
                            val = ext;
                        }
                        std::string fmt = newTmp();
                        std::string fmt_name = newline ? "@.fmt_double" : "@.fmt_double_nonl";
                        int fmt_size = newline ? 5 : 4;
                        out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr "
                             << fmt_name << ", i32 0, i32 0\n";
                        out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", double " << val << ")\n";
                        return;
                    }
                }
                // single string literal
                if (auto* s = dynamic_cast<const StringLiteralExpr*>(segments[0])) {
                    std::string full = newline ? s->value + "\n" : s->value;
                    std::string label = registerStringConstant(full);
                    int len; llvmEscape(full, len);
                    std::string tmp = newTmp();
                    out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
                         << label << ", i32 0, i32 0\n";
                    out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                    return;
                }
                // single integer expr
                // printf is variadic; sub-i32 values must be extended to i32 first.
                std::string val = emitExpr(*segments[0]);
                std::string val_type = exprLlvmType(*segments[0]);
                if (val_type != "i32" && val_type != "i64") {
                    std::string ext = newTmp();
                    out_ << "    " << ext << " = zext " << val_type << " " << val << " to i32\n";
                    val = ext;
                }
                bool is_uint = false;
                if (auto* ve = dynamic_cast<const VarExpr*>(segments[0])) {
                    auto tit = local_types_.find(ve->name);
                    if (tit != local_types_.end() && tit->second == "uint") is_uint = true;
                    if (!is_uint && !current_slid_.empty()) {
                        auto& info = slid_info_[current_slid_];
                        auto fit = info.field_index.find(ve->name);
                        if (fit != info.field_index.end() && info.field_types[fit->second] == "uint")
                            is_uint = true;
                    }
                }
                std::string fmt = newTmp();
                bool is_i64 = (val_type == "i64");
                std::string fmt_name = is_i64   ? (newline ? "@.fmt_long" : "@.fmt_long_nonl")
                                     : is_uint  ? (newline ? "@.fmt_uint" : "@.fmt_uint_nonl")
                                                : (newline ? "@.fmt_int"  : "@.fmt_int_nonl");
                std::string fmt_size = is_i64 ? (newline ? "5" : "4") : "4";
                std::string arg_type = is_i64 ? "i64" : "i32";
                out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", " << arg_type << " " << val << ")\n";
                return;
            }

            // multi-segment: emit one printf per segment, newline only on last
            for (int si = 0; si < (int)segments.size(); si++) {
                bool last = (si == (int)segments.size() - 1);
                if (auto* s = dynamic_cast<const StringLiteralExpr*>(segments[si])) {
                    std::string full = (last && newline) ? s->value + "\n" : s->value;
                    std::string label = registerStringConstant(full);
                    int len; llvmEscape(full, len);
                    std::string tmp = newTmp();
                    out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
                         << label << ", i32 0, i32 0\n";
                    out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                } else if (auto* sl = dynamic_cast<const SliceExpr*>(segments[si])) {
                    emitSlice(sl, last && newline);
                } else {
                    std::string val = emitExpr(*segments[si]);
                    std::string val_type = exprLlvmType(*segments[si]);
                    // float/double segment — printf varargs promote float to double
                    if (val_type == "float" || val_type == "double") {
                        if (val_type == "float") {
                            std::string ext = newTmp();
                            out_ << "    " << ext << " = fpext float " << val << " to double\n";
                            val = ext;
                        }
                        std::string fmt = newTmp();
                        int fmt_size = (last && newline) ? 5 : 4;
                        std::string fmt_name = (last && newline) ? "@.fmt_double" : "@.fmt_double_nonl";
                        out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr "
                             << fmt_name << ", i32 0, i32 0\n";
                        out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", double " << val << ")\n";
                        continue;
                    }
                    if (val_type == "ptr") {
                        std::string fmt = newTmp();
                        int fmt_size = (last && newline) ? 4 : 3;
                        std::string fmt_name = (last && newline) ? "@.fmt_str" : "@.fmt_str_nonl";
                        out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr "
                             << fmt_name << ", i32 0, i32 0\n";
                        out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", ptr " << val << ")\n";
                        continue;
                    }
                    if (val_type != "i32" && val_type != "i64") {
                        std::string ext = newTmp();
                        out_ << "    " << ext << " = zext " << val_type << " " << val << " to i32\n";
                        val = ext;
                    }
                    bool seg_is_uint = false;
                    if (auto* ve = dynamic_cast<const VarExpr*>(segments[si])) {
                        auto tit = local_types_.find(ve->name);
                        if (tit != local_types_.end() && tit->second == "uint") seg_is_uint = true;
                        if (!seg_is_uint && !current_slid_.empty()) {
                            auto& info = slid_info_[current_slid_];
                            auto fit = info.field_index.find(ve->name);
                            if (fit != info.field_index.end() && info.field_types[fit->second] == "uint")
                                seg_is_uint = true;
                        }
                    }
                    bool seg_is_i64 = (val_type == "i64");
                    std::string fmt = newTmp();
                    std::string fmt_name = seg_is_i64  ? ((last && newline) ? "@.fmt_long" : "@.fmt_long_nonl")
                                        : seg_is_uint  ? ((last && newline) ? "@.fmt_uint" : "@.fmt_uint_nonl")
                                                       : ((last && newline) ? "@.fmt_int"  : "@.fmt_int_nonl");
                    std::string seg_fmt_size = seg_is_i64 ? ((last && newline) ? "5" : "4") : "4";
                    std::string seg_arg_type = seg_is_i64 ? "i64" : "i32";
                    out_ << "    " << fmt << " = getelementptr [" << seg_fmt_size << " x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                    out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", " << seg_arg_type << " " << val << ")\n";
                }
            }
            return;
        }

        // template call statement: add<int>(a, b); or add(a, b) with inferred type
        {
            auto resolved = resolveTemplateOverload(call->callee, call->type_args, call->args);
            if (resolved.entry) {
                std::string mangled = instantiateTemplate(*resolved.entry, resolved.type_args);
                std::string ret_type = llvmType(func_return_types_[mangled]);
                auto& ptypes = func_param_types_[mangled];
                std::string arg_str;
                for (int i = 0; i < (int)call->args.size(); i++) {
                    if (i > 0) arg_str += ", ";
                    std::string ptype = (i < (int)ptypes.size()) ? ptypes[i] : "int";
                    arg_str += llvmType(ptype) + " " + emitArgForParam(*call->args[i], ptype);
                }
                if (ret_type == "void")
                    out_ << "    call void @" << llvmGlobalName(mangled) << "(" << arg_str << ")\n";
                else
                    out_ << "    call " << ret_type << " @" << llvmGlobalName(mangled)
                         << "(" << arg_str << ")\n";
                return;
            }
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
                std::string ptype_str = (i < (int)nptypes_cs.size()) ? nptypes_cs[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                arg_str += ptype + " " + emitArgForParam(*call->args[i], ptype_str);
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

        // implicit self method call (peer call inside a slid method) — takes precedence
        // over global free functions per the bare-name lookup rule. For virtual
        // classes, dispatch via %self's vptr so derived overrides win.
        if (!current_slid_.empty()) {
            auto& csinfo = slid_info_[current_slid_];
            int vslot = -1;
            if (csinfo.is_virtual_class) {
                for (int i = 0; i < (int)csinfo.vtable.size(); i++) {
                    if (csinfo.vtable[i].method_name != call->callee) continue;
                    if (csinfo.vtable[i].param_types.size() != call->args.size()) continue;
                    vslot = i; break;
                }
            }
            std::string mangled;
            std::vector<std::string> mptypes_self;
            std::string ret_slids;
            if (vslot >= 0) {
                mangled = csinfo.vtable[vslot].mangled;
                mptypes_self = csinfo.vtable[vslot].param_types;
                ret_slids = csinfo.vtable[vslot].return_type;
            } else {
                std::string base = current_slid_ + "__" + call->callee;
                mangled = resolveOverloadForCall(base, call->args);
                auto mit = func_return_types_.find(mangled);
                if (mit == func_return_types_.end()) goto fallthrough_self;
                ret_slids = mit->second;
                mptypes_self = func_param_types_[mangled];
            }
            {
                std::string ret_type = llvmType(ret_slids);
                bool empty = csinfo.is_empty;
                std::string self_str = self_ptr_.empty() ? "%self" : self_ptr_;
                std::string arg_str = empty ? "" : "ptr " + self_str;
                for (int i = 0; i < (int)call->args.size(); i++) {
                    std::string ptype_str = (i < (int)mptypes_self.size()) ? mptypes_self[i] : "";
                    std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                    if (!arg_str.empty()) arg_str += ", ";
                    arg_str += ptype + " " + emitArgForParam(*call->args[i], ptype_str);
                }
                if (vslot >= 0) {
                    int n = (int)csinfo.vtable.size();
                    std::string vptr_field = newTmp();
                    out_ << "    " << vptr_field << " = getelementptr %struct."
                         << current_slid_ << ", ptr " << self_str << ", i32 0, i32 0\n";
                    std::string vtbl = newTmp();
                    out_ << "    " << vtbl << " = load ptr, ptr " << vptr_field << "\n";
                    std::string slot_ptr = newTmp();
                    out_ << "    " << slot_ptr << " = getelementptr [" << n
                         << " x ptr], ptr " << vtbl << ", i32 0, i32 " << vslot << "\n";
                    std::string fn = newTmp();
                    out_ << "    " << fn << " = load ptr, ptr " << slot_ptr << "\n";
                    if (ret_type == "void" || ret_type.empty()) {
                        out_ << "    call void " << fn << "(" << arg_str << ")\n";
                    } else {
                        std::string tmp = newTmp();
                        out_ << "    " << tmp << " = call " << ret_type << " " << fn
                             << "(" << arg_str << ")\n";
                    }
                    return;
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
            fallthrough_self: ;
        }

        // regular top-level function call as statement
        std::string resolved_callee = resolveFreeFunctionMangledName(call->callee, call->args.size());
        auto fit = resolved_callee.empty()
            ? func_return_types_.end()
            : func_return_types_.find(resolved_callee);
        if (fit != func_return_types_.end()) {
            std::string ret_type = llvmType(fit->second);
            auto& rptypes = func_param_types_[resolved_callee];
            std::string arg_str;
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (i > 0) arg_str += ", ";
                std::string ptype_str = (i < (int)rptypes.size()) ? rptypes[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                arg_str += ptype + " " + emitArgForParam(*call->args[i], ptype_str);
            }
            if (ret_type == "void") {
                out_ << "    call void @" << resolved_callee << "(" << arg_str << ")\n";
            } else {
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << resolved_callee
                     << "(" << arg_str << ")\n";
            }
            return;
        }

        error(std::string("unknown function: " + call->callee));
    }

    if (auto* td = dynamic_cast<const TupleDestructureStmt*>(&stmt)) {
        emitDestructure(td->fields, *td->init);
        return;
    }

    if (auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
        // naked block — save dtor depth and locals snapshot; restore on exit for block-scoped RAII
        size_t dtor_mark = dtor_vars_.size();
        auto saved_locals = locals_;
        auto saved_local_types = local_types_;
        emitBlock(*block);
        // destroy block-scoped variables in reverse declaration order
        if (!block_terminated_) {
            for (int i = (int)dtor_vars_.size() - 1; i >= (int)dtor_mark; i--) {
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
        dtor_vars_.resize(dtor_mark);
        locals_ = saved_locals;
        local_types_ = saved_local_types;
        return;
    }

    error(std::string("unsupported statement type"));
}
