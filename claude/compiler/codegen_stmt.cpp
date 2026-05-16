#include "codegen.h"
#include "source_map.h"
#include "codegen_helpers.h"
#include <sstream>
#include <functional>
#include <stdexcept>

void Codegen::pushPostIncQueue() {
    post_inc_stack_.emplace_back();
    pre_done_stack_.emplace_back();
}

// Emit the deferred advance for one queued entry. Inlined here as a lambda
// captured by flushPostIncQueue / schedulePostInc, since PendingAdvance is
// private to Codegen.
//   Pointer → load ptr, GEP ±step, store ptr
//   Int     → load Ti, add/sub 1, store Ti
//   Float   → load Tf, fadd/fsub 1.0, store Tf

void Codegen::flushPostIncQueue() {
    if (post_inc_stack_.empty()) return;
    auto frame = std::move(post_inc_stack_.back());
    post_inc_stack_.pop_back();
    if (!pre_done_stack_.empty()) pre_done_stack_.pop_back();
    auto emit = [&](const PendingAdvance& pa) {
        std::string cur = newTmp();
        if (pa.kind == PendingAdvance::Pointer) {
            out_ << "    " << cur << " = load ptr, ptr " << pa.addr << "\n";
            std::string nxt = newTmp();
            out_ << "    " << nxt << " = getelementptr " << pa.llvm_type
                 << ", ptr " << cur << ", i32 " << pa.step << "\n";
            out_ << "    store ptr " << nxt << ", ptr " << pa.addr << "\n";
            return;
        }
        bool is_float = (pa.kind == PendingAdvance::Float);
        out_ << "    " << cur << " = load " << pa.llvm_type
             << ", ptr " << pa.addr << "\n";
        std::string nxt = newTmp();
        std::string instr = is_float
            ? (pa.step > 0 ? "fadd" : "fsub")
            : (pa.step > 0 ? "add"  : "sub");
        std::string lit = is_float ? "1.0" : "1";
        out_ << "    " << nxt << " = " << instr << " " << pa.llvm_type
             << " " << cur << ", " << lit << "\n";
        out_ << "    store " << pa.llvm_type << " " << nxt
             << ", ptr " << pa.addr << "\n";
    };
    for (auto& pa : frame) emit(pa);
}

void Codegen::schedulePostInc(PendingAdvance::Kind kind,
                              const std::string& addr,
                              const std::string& llvm_type, int step) {
    if (post_inc_stack_.empty()) {
        // Fallback: no active phrase frame, push a one-shot frame and flush
        // immediately so the side effect isn't dropped. Sites that should
        // defer must wrap with push/flush.
        post_inc_stack_.push_back({{kind, addr, llvm_type, step}});
        pre_done_stack_.emplace_back();
        flushPostIncQueue();
        return;
    }
    post_inc_stack_.back().push_back({kind, addr, llvm_type, step});
}

bool Codegen::preAlreadyDone(const UnaryExpr* u) {
    if (pre_done_stack_.empty()) return false;
    return pre_done_stack_.back().count(u) > 0;
}

// Walk a phrase's expression AST; for each pre-inc/dec UnaryExpr encountered,
// resolve the operand's lvalue, emit the advance, and mark the node so the
// later eval site loads (rather than re-emitting). Stops at sub-phrase
// boundaries (call/method/ctor args, tuple elements, &&/|| right operand) —
// each of those runs its own pre-pass at its own entry.
void Codegen::emitPrePass(const Expr& root) {
    std::function<void(const Expr&)> walk = [&](const Expr& e) {
        if (auto* u = dynamic_cast<const UnaryExpr*>(&e)) {
            if (u->op == "pre++" || u->op == "pre--") {
                // Resolve operand lvalue (note: if operand has its own side
                // effects like arr[i++], they fire here at phrase entry).
                auto lv = resolveLvalue(*u->operand);
                // Body-level const enforcement: pre-inc/dec writes the lvalue.
                if (typeStartsWithConst(lv.type))
                    errorAtNode(e, "Cannot '" + u->op + "' a const lvalue.");
                bool is_inc = (u->op == "pre++");
                int step = is_inc ? 1 : -1;
                std::string old_val = newTmp();
                if (isPtrType(lv.type)) {
                    std::string pointee = lv.type.substr(0, lv.type.size() - 2);
                    std::string pointee_llvm = llvmType(pointee);
                    out_ << "    " << old_val << " = load ptr, ptr " << lv.addr << "\n";
                    std::string nxt = newTmp();
                    out_ << "    " << nxt << " = getelementptr " << pointee_llvm
                         << ", ptr " << old_val << ", i32 " << step << "\n";
                    out_ << "    store ptr " << nxt << ", ptr " << lv.addr << "\n";
                } else if (isRefType(lv.type)) {
                    error(std::string("Operator '" + u->op + "' on reference: arithmetic on references is not allowed (use a pointer '[]' type)"));
                } else {
                    std::string scalar_llvm = llvmType(lv.type);
                    bool is_float = (scalar_llvm == "float" || scalar_llvm == "double");
                    out_ << "    " << old_val << " = load " << scalar_llvm << ", ptr " << lv.addr << "\n";
                    std::string nxt = newTmp();
                    std::string instr = is_float ? (is_inc ? "fadd" : "fsub")
                                                  : (is_inc ? "add" : "sub");
                    std::string lit = is_float ? "1.0" : "1";
                    out_ << "    " << nxt << " = " << instr << " " << scalar_llvm
                         << " " << old_val << ", " << lit << "\n";
                    out_ << "    store " << scalar_llvm << " " << nxt << ", ptr " << lv.addr << "\n";
                }
                if (!pre_done_stack_.empty()) pre_done_stack_.back().insert(u);
                // Don't recurse into operand — its side effects already fired
                // via resolveLvalue above.
                return;
            }
            // post++/post-- — leave for phrase-exit flush; recurse into
            // operand to find any inner pres.
            walk(*u->operand);
            return;
        }
        if (auto* b = dynamic_cast<const BinaryExpr*>(&e)) {
            walk(*b->left);
            // && / || right operand is a sub-phrase; ^^ always evaluates.
            if (b->op == "&&" || b->op == "||") return;
            walk(*b->right);
            return;
        }
        if (dynamic_cast<const CallExpr*>(&e)) return;       // args are sub-phrases
        if (dynamic_cast<const MethodCallExpr*>(&e)) return; // args are sub-phrases
        if (dynamic_cast<const TupleExpr*>(&e)) return;      // values are sub-phrases
        if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&e)) {
            walk(*fa->object);
            return;
        }
        if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&e)) {
            walk(*ai->base);
            walk(*ai->index);
            return;
        }
        if (auto* de = dynamic_cast<const DerefExpr*>(&e)) {
            walk(*de->operand);
            return;
        }
        if (auto* ao = dynamic_cast<const AddrOfExpr*>(&e)) {
            walk(*ao->operand);
            return;
        }
        // Leaves (VarExpr, literals, NullptrExpr, ...) and unhandled wrappers
        // contribute nothing. Add cases here if a wrapper hides pres.
    };
    walk(root);
}

// Statement-level pre-pass: walk into the statement's expression children,
// applying the expr-level pre-pass to each. Skips statement types that
// delegate to sub-phrases for all of their expressions (if/while/for/switch/
// block/destructure — those wire pre-pass at the sub-phrase site). Skips
// CallStmt / MethodCallStmt whose args are sub-phrases (each arg is wrapped
// at emitPhraseArg).
void Codegen::emitPrePass(const Stmt& root) {
    if (auto* es = dynamic_cast<const ExprStmt*>(&root)) {
        if (es->expr) emitPrePass(*es->expr);
        return;
    }
    if (auto* as = dynamic_cast<const AssignStmt*>(&root)) {
        if (as->value) emitPrePass(*as->value);
        return;
    }
    if (auto* vd = dynamic_cast<const VarDeclStmt*>(&root)) {
        if (vd->init) emitPrePass(*vd->init);
        // Note: ctor_args are sub-phrases handled by initFieldFromExpr.
        return;
    }
    if (auto* rs = dynamic_cast<const ReturnStmt*>(&root)) {
        if (rs->value) emitPrePass(*rs->value);
        return;
    }
    if (auto* cas = dynamic_cast<const CompoundAssignStmt*>(&root)) {
        if (cas->lhs) emitPrePass(*cas->lhs);
        if (cas->rhs) emitPrePass(*cas->rhs);
        return;
    }
    if (auto* ds = dynamic_cast<const DeleteStmt*>(&root)) {
        if (ds->operand) emitPrePass(*ds->operand);
        return;
    }
    if (auto* ss = dynamic_cast<const SwapStmt*>(&root)) {
        if (ss->lhs) emitPrePass(*ss->lhs);
        if (ss->rhs) emitPrePass(*ss->rhs);
        return;
    }
    if (auto* fa = dynamic_cast<const FieldAssignStmt*>(&root)) {
        if (fa->object) emitPrePass(*fa->object);
        if (fa->value) emitPrePass(*fa->value);
        return;
    }
    if (auto* ia = dynamic_cast<const IndexAssignStmt*>(&root)) {
        if (ia->base) emitPrePass(*ia->base);
        if (ia->index) emitPrePass(*ia->index);
        if (ia->value) emitPrePass(*ia->value);
        return;
    }
    if (auto* da = dynamic_cast<const DerefAssignStmt*>(&root)) {
        if (da->ptr) emitPrePass(*da->ptr);
        if (da->value) emitPrePass(*da->value);
        return;
    }
    // Other stmt types (if/while/for/switch/block/destructure/call/methodcall/
    // break/continue) either delegate to sub-phrases or have no expressions.
}

Codegen::Lvalue Codegen::resolveLvalue(const Expr& e) {
    if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
        auto it = locals_.find(ve->name);
        if (it != locals_.end()) return {it->second.reg, it->second.type};
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end()) {
                std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                     << ", ptr " << self << ", i32 0, i32 " << fit->second << "\n";
                std::string ftype = info.field_types[fit->second];
                // Const propagation: if self is const, the field is const too.
                // propagateConst preserves the iterator-handle-mutable form
                // for indirect fields (so a local copy can advance).
                auto sit = locals_.find("self");
                if (sit != locals_.end() && typeStartsWithConst(sit->second.type))
                    ftype = propagateConst(ftype);
                return {gep, ftype};
            }
        }
        // Global field as lvalue base — fires sentinel gate (if lazy) and
        // returns storage symbol + declared type. Mirrors the rvalue path in
        // emitExpr(VarExpr) so writes and chained reads (apples_[i].field_)
        // resolve to global storage through the canonical lvalue walker.
        if (auto* ge = lookupGlobal(ve->name)) {
            if (current_func_name_ == "main" && global_lifetime_depth_ == 0)
                errorAtNode(e, "Cannot access global '" + ve->name
                    + "' outside the `global;` scope in `main`.");
            emitLazySentinelGate(*ge);
            return {ge->llvm_symbol, ge->slids_type};
        }
        errorAtNode(e, "Undefined variable '" + ve->name + "'.");
    }
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&e)) {
        auto obj = resolveLvalue(*fa->object);
        std::string stype = obj.type;
        std::string addr = obj.addr;
        bool obj_is_const = false;
        // Strip top-level const for the slid_info_ lookup; remember it to
        // re-apply to the returned field type via propagateConst.
        if (typeStartsWithConst(stype)) {
            obj_is_const = true;
            stype = stype.substr(6);
        }
        if (isPtrType(stype) || isRefType(stype)) {
            std::string loaded = newTmp();
            out_ << "    " << loaded << " = load ptr, ptr " << addr << "\n";
            addr = loaded;
            // pointeeInfo handles both `T^` and `(const T)^` forms uniformly.
            auto pi = pointeeInfo(stype);
            stype = pi.name;
            if (pi.is_const) obj_is_const = true;
        }
        auto sit = slid_info_.find(stype);
        if (sit == slid_info_.end())
            errorAtNode(e, "Type '" + stype + "' is not a slid type.");
        auto fit = sit->second.field_index.find(fa->field);
        if (fit == sit->second.field_index.end())
            errorAtNode(e, "Unknown field '" + fa->field
                + "' on type '" + stype + "'.");
        std::string gep = newTmp();
        out_ << "    " << gep << " = getelementptr %struct." << stype
             << ", ptr " << addr << ", i32 0, i32 " << fit->second << "\n";
        std::string ftype = sit->second.field_types[fit->second];
        if (obj_is_const) ftype = propagateConst(ftype);
        return {gep, ftype};
    }
    if (auto* de = dynamic_cast<const DerefExpr*>(&e)) {
        // Class instance with op^() defined — `x^` is an lvalue at the
        // address returned by op^(). Emit the call; the result ptr is the
        // lvalue address, op^()'s return-type pointee is the lvalue type.
        {
            std::string operand_slid;
            std::string self_reg;
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto it = locals_.find(ve->name);
                if (it != locals_.end()) {
                    operand_slid = canonType(it->second.type);
                    if (slid_info_.count(operand_slid)) self_reg = it->second.reg;
                    else operand_slid.clear();
                }
            }
            if (!operand_slid.empty()) {
                std::string mangled = resolveDerefOverload(operand_slid);
                if (!mangled.empty()) {
                    std::string ret_t = func_return_types_[mangled];
                    std::string pointee = pointeeForLookup(ret_t);
                    std::string call_ret = newTmp();
                    out_ << "    " << call_ret << " = call ptr @"
                         << llvmGlobalName(mangled) << "(ptr " << self_reg << ")\n";
                    return {call_ret, pointee};
                }
            }
        }

        // post-inc/dec deref under PPID: load OLD pointer (return as deref'd
        // address), schedule the advance for the next terminator. Pre-form
        // (UnaryExpr pre++/pre--) is handled in the UnaryExpr arm below and
        // applies the side effect immediately.
        if (auto* ue = dynamic_cast<const UnaryExpr*>(de->operand.get())) {
            if (ue->op == "post++" || ue->op == "post--") {
                auto base = resolveLvalue(*ue->operand);
                std::string pointee;
                if (isPtrType(base.type))      pointee = base.type.substr(0, base.type.size() - 2);
                else if (isRefType(base.type)) pointee = base.type.substr(0, base.type.size() - 1);
                else errorAtNode(e, "Post-increment or post-decrement deref of non-pointer type '" + base.type + "'.");
                pointee = stripRedundantConstParens(pointee);
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << base.addr << "\n";
                int step = (ue->op == "post++") ? 1 : -1;
                schedulePostInc(PendingAdvance::Pointer, base.addr,
                                llvmType(pointee), step);
                return {loaded, pointee};
            }
        }
        auto base = resolveLvalue(*de->operand);
        std::string pointee;
        if (isPtrType(base.type))      pointee = base.type.substr(0, base.type.size() - 2);
        else if (isRefType(base.type)) pointee = base.type.substr(0, base.type.size() - 1);
        else errorAtNode(e, "Cannot dereference a value of non-pointer type '" + base.type + "'.");
        pointee = stripRedundantConstParens(pointee);
        std::string loaded = newTmp();
        out_ << "    " << loaded << " = load ptr, ptr " << base.addr << "\n";
        return {loaded, pointee};
    }
    if (auto* ue = dynamic_cast<const UnaryExpr*>(&e)) {
        if (ue->op == "pre++" || ue->op == "pre--") {
            // Pre-extract: the advance fired at phrase entry; the operand's
            // lvalue is unchanged. Just resolve it again. (For lvalues without
            // side effects in their address computation — VarExpr, FieldAccess,
            // etc. — re-resolution is idempotent.)
            if (preAlreadyDone(ue))
                return resolveLvalue(*ue->operand);
            auto base = resolveLvalue(*ue->operand);
            std::string pointee;
            if (isPtrType(base.type))      pointee = base.type.substr(0, base.type.size() - 2);
            else if (isRefType(base.type)) pointee = base.type.substr(0, base.type.size() - 1);
            else errorAtNode(e, "Pre-increment or pre-decrement of non-pointer type '" + base.type + "'.");
            std::string loaded = newTmp();
            out_ << "    " << loaded << " = load ptr, ptr " << base.addr << "\n";
            int step = (ue->op == "pre++") ? 1 : -1;
            std::string nxt = newTmp();
            out_ << "    " << nxt << " = getelementptr " << llvmType(pointee)
                 << ", ptr " << loaded << ", i32 " << step << "\n";
            out_ << "    store ptr " << nxt << ", ptr " << base.addr << "\n";
            return {base.addr, base.type};
        }
    }
    if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&e)) {
        (void)ai;
        // Walk the consecutive AIE chain, collecting indices outer→inner so
        // each layer can reclassify its own base type. The deepest non-AIE
        // base is `cur`.
        std::vector<const Expr*> indices;
        const Expr* cur = &e;
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
            indices.insert(indices.begin(), a->index.get());
            cur = a->base.get();
        }
        std::string addr, type;
        int consumed = 0;
        // Multi-dim native array prefix: when the deepest base is a fixed-size
        // array local (storage is a flat alloca), fold up to dims.size()
        // indices into one row-major GEP. Remaining indices then drill through
        // whatever element type produces.
        if (auto* ve = dynamic_cast<const VarExpr*>(cur)) {
            auto ait = array_info_.find(ve->name);
            if (ait != array_info_.end()) {
                auto& ainfo = ait->second;
                int n = std::min((int)indices.size(), (int)ainfo.dims.size());
                // Slids reading: leftmost type-bracket is innermost; rightmost
                // is outermost. dims is stored in source order, so dims[0] is
                // the innermost size and dims[n-1] is the outermost. Indices
                // mirror the type-brackets: indices[0] is the innermost index,
                // indices[n-1] is the outermost. Right-to-left Horner builds
                // flat = (((i_{n-1})*dims[n-2] + i_{n-2})*dims[n-3] + ...) *dims[0] + i_0.
                std::string flat = emitExpr(*indices[n - 1]);
                for (int k = n - 2; k >= 0; k--) {
                    int stride = ainfo.dims[k];
                    std::string mul = newTmp();
                    out_ << "    " << mul << " = mul i32 " << flat
                         << ", " << stride << "\n";
                    std::string iv = emitExpr(*indices[k]);
                    std::string add = newTmp();
                    out_ << "    " << add << " = add i32 " << mul
                         << ", " << iv << "\n";
                    flat = add;
                }
                int total = 1;
                for (int d : ainfo.dims) total *= d;
                std::string elt = llvmType(ainfo.elem_type);
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr [" << total
                     << " x " << elt << "], ptr " << ainfo.alloca_reg
                     << ", i32 0, i32 " << flat << "\n";
                addr = gep;
                type = ainfo.elem_type;
                consumed = n;
            }
        }
        if (addr.empty()) {
            auto base = resolveLvalue(*cur);
            addr = base.addr;
            type = base.type;
        }
        return drillIndexChain(addr, type, indices, consumed, e);
    }
    errorAtNode(e, "Unsupported lvalue shape.");
}

Codegen::Lvalue Codegen::drillIndexChain(std::string addr, std::string type,
        const std::vector<const Expr*>& indices, int from, const Expr& err_site) {
    for (int i = from; i < (int)indices.size(); i++) {
        const Expr& idx = *indices[i];
        // Strip leading const for the dispatch (the type-shape check needs the
        // canonical kind), remembering to re-apply to the element type so
        // const propagates through the read.
        bool was_const = typeStartsWithConst(type);
        if (was_const) type = type.substr(6);
        do {
        // anon-tuple slot
        if (isAnonTupleType(type)) {
            auto elems = anonTupleElems(type);
            int k;
            if (constExprToInt(idx, enum_values_, k)) {
                if (k < 0 || k >= (int)elems.size())
                    errorAtNode(err_site, "Tuple index "
                        + std::to_string(k) + " is out of range.");
                addr = emitFieldGep(type, addr, k);
                type = elems[k];
                break;
            }
            // variable index: only allowed on a homogeneous tuple.
            bool homog = !elems.empty();
            for (auto& t : elems) if (t != elems[0]) { homog = false; break; }
            if (!homog)
                errorAtNode(err_site, "Variable index on heterogeneous tuple '"
                    + type + "' is not allowed.");
            std::string elem_t = elems[0];
            std::string elem_llvm = llvmType(elem_t);
            std::string iv = emitExpr(idx);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr ["
                 << elems.size() << " x " << elem_llvm
                 << "], ptr " << addr << ", i32 0, i32 " << iv << "\n";
            addr = gep;
            type = elem_t;
            break;
        }
        // slid with op[] dispatch. op[] returns T^ (reference to contained
        // object); the call result IS the lvalue address. Read sites load
        // through it via emitExpr's trailing load; write sites store directly.
        if (slid_info_.count(type)) {
            std::string mangled = resolveSlidIndex(type, idx);
            if (mangled.empty())
                errorAtNode(err_site, "Type '" + type
                    + "' has no op[] for the given index.");
            auto& mptypes = func_param_types_[mangled];
            std::string idx_llvm = mptypes.empty() ? "i32" : llvmType(mptypes[0]);
            std::string idx_val = mptypes.empty()
                ? emitExpr(idx)
                : emitArgForParam(idx, mptypes[0]);
            auto rit = func_return_types_.find(mangled);
            std::string ret_t = (rit != func_return_types_.end()) ? rit->second : "";
            std::string call_res = newTmp();
            out_ << "    " << call_res << " = call ptr @" << llvmGlobalName(mangled)
                 << "(ptr " << addr << ", " << idx_llvm
                 << " " << idx_val << ")\n";
            addr = call_res;
            type = pointeeForLookup(ret_t);
            break;
        }
        // inline array (e.g. obj.arr_[i] reached through a chain)
        if (isInlineArrayType(type)) {
            auto lb = type.rfind('[');
            std::string elem_type = type.substr(0, lb);
            std::string sz_str = type.substr(lb + 1, type.size() - lb - 2);
            std::string elt = llvmType(elem_type);
            std::string idx_llvm = exprLlvmType(idx);
            std::string iv = emitExpr(idx);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << sz_str
                 << " x " << elt << "], ptr " << addr
                 << ", i32 0, " << idx_llvm << " " << iv << "\n";
            addr = gep;
            type = elem_type;
            break;
        }
        // reference base: `^` has no arithmetic — indexing it is illegal.
        if (isRefType(type))
            errorAtNode(err_site, "Reference type '" + type
                + "' cannot be indexed; dereference it with '^' first.");
        // iterator base: `[]` allows arithmetic, so indexing is a GEP.
        if (isPtrType(type)) {
            std::string elem_type = type.substr(0, type.size() - 2);
            std::string base_ptr = newTmp();
            out_ << "    " << base_ptr << " = load ptr, ptr " << addr << "\n";
            std::string idx_llvm = exprLlvmType(idx);
            std::string iv = emitExpr(idx);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr " << llvmType(elem_type)
                 << ", ptr " << base_ptr << ", " << idx_llvm << " " << iv << "\n";
            addr = gep;
            type = elem_type;
            break;
        }
        errorAtNode(err_site, "Type '" + type + "' is not indexable.");
        } while (0);
        if (was_const) type = propagateConst(type);
    }
    return {addr, type};
}

std::string Codegen::drillIndexedType(std::string type,
        const std::vector<const Expr*>& indices, int from) {
    for (int i = from; i < (int)indices.size(); i++) {
        // const propagates through indexing — strip for dispatch, re-apply.
        bool was_const = typeStartsWithConst(type);
        if (was_const) type = type.substr(6);
        if (isAnonTupleType(type)) {
            auto elems = anonTupleElems(type);
            int k;
            if (constExprToInt(*indices[i], enum_values_, k)) {
                if (k < 0 || k >= (int)elems.size()) return "int";
                type = elems[k];
            } else {
                bool homog = !elems.empty();
                for (auto& t : elems) if (t != elems[0]) { homog = false; break; }
                type = homog ? elems[0] : "int";
            }
        } else if (slid_info_.count(type)) {
            std::string mangled = resolveSlidIndex(type, *indices[i]);
            if (mangled.empty()) return "int";
            auto rit = func_return_types_.find(mangled);
            // op[] returns T^; the indexed-element type is the pointee.
            type = (rit != func_return_types_.end())
                ? pointeeForLookup(rit->second) : "int";
        } else if (isInlineArrayType(type)) {
            auto lb = type.rfind('[');
            type = type.substr(0, lb);
        } else if (isPtrType(type) || isRefType(type)) {
            type = isPtrType(type)
                ? type.substr(0, type.size() - 2)
                : type.substr(0, type.size() - 1);
        } else {
            return "int";
        }
        if (was_const) type = propagateConst(type);
    }
    return type;
}

void Codegen::emitDestructure(
    const std::vector<std::pair<std::string,std::string>>& targets,
    const Expr& init) {
    // tuple literal rhs: desugar (t1 a, t2 b) = (x, y); into t1 a = x; t2 b = y;
    if (auto* te = dynamic_cast<const TupleExpr*>(&init)) {
        if (te->values.size() != targets.size())
            error(std::string("Destructure size mismatch: "
                + std::to_string(targets.size()) + " targets, "
                + std::to_string(te->values.size()) + " values"));
        for (int i = 0; i < (int)targets.size(); i++) {
            auto& [type, name] = targets[i];
            if (name.empty()) continue; // empty slot — skip this element
            // PPID: each destructure slot is its own phrase.
            pushPostIncQueue();
            emitPrePass(*te->values[i]);
            struct FlushGuard { Codegen* cg; ~FlushGuard() { cg->flushPostIncQueue(); } } _g{this};
            std::string eff_type = type.empty() ? inferSlidType(*te->values[i]) : type;
            // if name is already in scope AND no explicit type: reassign the existing local
            bool reassign = type.empty() && locals_.count(name) > 0;
            if (reassign) {
                auto ltit = locals_.find(name);
                if (ltit == locals_.end() || ltit->second.type != eff_type)
                    error(std::string("Destructure reassign: '" + name
                        + "' has type '" + (ltit==locals_.end()?"<unknown>":ltit->second.type)
                        + "' but source element is '" + eff_type + "'"));
                // Body-level const enforcement: reassign of a const local is rebind.
                if (typeStartsWithConst(ltit->second.type))
                    errorAtNode(init,
                        "Cannot reassign const variable '" + name + "' in destructure.");
            }
            std::string reg = reassign ? locals_[name].reg : uniqueAllocaReg(name);
            // slid or anon-tuple target: init path (alloca + ctor) unless reassigning.
            if (slid_info_.count(eff_type) || isAnonTupleType(eff_type)) {
                requireCompatibleInit(eff_type, *te->values[i]);
                std::string src_type = inferSlidType(*te->values[i]);
                if (src_type != eff_type)
                    errorAtNode(*te->values[i], "Cannot initialize '" + eff_type
                        + "' from a value of type '" + src_type + "'.");
                if (!reassign)
                    out_ << "    " << reg << " = alloca " << llvmType(eff_type) << "\n";
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                    auto lit = locals_.find(ve->name);
                    src_ptr = (lit != locals_.end()) ? lit->second.reg : emitExpr(*te->values[i]);
                } else {
                    src_ptr = emitExpr(*te->values[i]);
                }
                bool is_move = isFreshSlidTemp(*te->values[i]);
                emitSlidSlotAssign(eff_type, reg, src_ptr, is_move, /*is_init=*/!reassign);
                if (!reassign) {
                    locals_[name] = {reg, eff_type, init.file_id, init.tok};
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
                    errorAtNode(*te->values[i], "Cannot initialize '" + eff_type
                        + "' from a value of type '" + inferSlidType(*te->values[i]) + "'.");
                }
            }
            out_ << "    store " << llvm_t << " " << val << ", ptr " << reg << "\n";
            if (!reassign) {
                locals_[name] = {reg, eff_type, init.file_id, init.tok};
            }
        }
        return;
    }
    // VarExpr of anon-tuple type: per-slot GEP; slid/anon-tuple slots move element-wise,
    // scalar slots take the extractvalue + store path (IR byte-identical to pre-refactor).
    if (auto* ve = dynamic_cast<const VarExpr*>(&init)) {
        auto tit = locals_.find(ve->name);
        if (tit != locals_.end() && isAnonTupleType(tit->second.type)) {
            auto elems = anonTupleElems(tit->second.type);
            if (elems.size() != targets.size())
                error(std::string("Destructure size mismatch: "
                    + std::to_string(targets.size()) + " targets, "
                    + std::to_string(elems.size()) + " elements in '" + ve->name + "'"));
            const std::string& src_type = tit->second.type;
            const std::string& src_ptr = locals_[ve->name].reg;
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
                // PPID: each destructure slot is its own phrase.
                pushPostIncQueue();
                struct FlushGuard { Codegen* cg; ~FlushGuard() { cg->flushPostIncQueue(); } } _g{this};
                const std::string& elem_type = elems[i];
                std::string eff_type = type.empty() ? elem_type : type;
                bool reassign = type.empty() && locals_.count(name) > 0;
                if (reassign) {
                    auto ltit = locals_.find(name);
                    if (ltit == locals_.end() || ltit->second.type != eff_type)
                        error(std::string("Destructure reassign: '" + name
                            + "' has type '" + (ltit==locals_.end()?"<unknown>":ltit->second.type)
                            + "' but source element is '" + eff_type + "'"));
                    if (typeStartsWithConst(ltit->second.type))
                        errorAtNode(init,
                            "Cannot reassign const variable '" + name + "' in destructure.");
                }
                std::string reg = reassign ? locals_[name].reg : uniqueAllocaReg(name);
                if (slid_info_.count(elem_type) || isAnonTupleType(elem_type)) {
                    if (!type.empty() && type != elem_type)
                        error(std::string("Cannot initialize '" + eff_type
                            + "' from value of type '" + elem_type + "'"));
                    if (!reassign)
                        out_ << "    " << reg << " = alloca " << llvmType(elem_type) << "\n";
                    std::string src_gep = emitFieldGep(src_type, src_ptr, i);
                    emitSlidSlotAssign(elem_type, reg, src_gep, /*is_move=*/true, /*is_init=*/!reassign);
                    if (!reassign) {
                        locals_[name] = {reg, eff_type, init.file_id, init.tok};
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
                        error(std::string("Cannot initialize '" + eff_type
                            + "' from value of type '" + elem_type + "'"));
                    }
                }
                if (!reassign)
                    out_ << "    " << reg << " = alloca " << dst_llvm << "\n";
                out_ << "    store " << dst_llvm << " " << extracted << ", ptr " << reg << "\n";
                if (!reassign) {
                    locals_[name] = {reg, eff_type, init.file_id, init.tok};
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
            error(std::string("Destructure type inference from non-tuple-variable source not supported yet"));
        bool reassign = type.empty() && locals_.count(name) > 0;
        if (reassign) {
            auto ltit = locals_.find(name);
            if (ltit == locals_.end() || ltit->second.type != eff_type)
                error(std::string("Destructure reassign: '" + name
                    + "' has type '" + (ltit==locals_.end()?"<unknown>":ltit->second.type)
                    + "' but source element is '" + eff_type + "'"));
            if (typeStartsWithConst(ltit->second.type))
                errorAtNode(init,
                    "Cannot reassign const variable '" + name + "' in destructure.");
        }
        std::string llvm_t = llvmType(eff_type);
        std::string reg = reassign ? locals_[name].reg : uniqueAllocaReg(name);
        if (!reassign)
            out_ << "    " << reg << " = alloca " << llvm_t << "\n";
        std::string extracted = newTmp();
        out_ << "    " << extracted << " = extractvalue " << tuple_type << " " << result << ", " << i << "\n";
        out_ << "    store " << llvm_t << " " << extracted << ", ptr " << reg << "\n";
        if (!reassign) {
            locals_[name] = {reg, eff_type, init.file_id, init.tok};
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

    // `global;` lifetime statement. The dtor pass at scope-close fires
    // `__$global_dtor_all()`, which walks the runtime registration list in
    // reverse and invokes each lazy dtor. Static-allocated globals contribute
    // nothing here (their dtor list entries are never registered).
    if (dynamic_cast<const GlobalLifetimeStmt*>(&stmt)) {
        if (scope_stack_.empty())
            error(std::string("Internal: `global;` outside any scope frame."));
        scope_stack_.back().exit_emits.push_back(
            std::string("    call void @__$global_dtor_all()\n"));
        scope_stack_.back().opens_global_lifetime = true;
        global_lifetime_depth_++;
        return;
    }

    // const decl: foldable rhs → substitute via the block-const stack; emit no IR.
    // non-foldable rhs → emit as a regular alloca'd local with the qualified
    // type (immutability not enforced this scope).
    if (auto* cds = dynamic_cast<const ConstDeclStmt*>(&stmt)) {
        if (isFoldableConstShape(*cds->def.rhs, current_slid_)) {
            std::set<std::string> cycle;
            ConstEntry folded = foldConstExpr(*cds->def.rhs, current_slid_, cycle);
            ConstEntry final_e = applyConstDeclaredType(cds->def, folded);
            auto& frame = block_const_stack_.back();
            if (frame.count(cds->def.name))
                errorAtNodeWithNote(stmt,
                    "Constant '" + cds->def.name + "' is already declared in this scope.",
                    frame[cds->def.name].file_id, frame[cds->def.name].tok,
                    "First declared here.");
            frame[cds->def.name] = final_e;
            return;
        }
        // soft-fail: runtime alloca path.
        std::string slid_type = cds->def.declared_type;
        if (slid_type.empty()) slid_type = inferSlidType(*cds->def.rhs);
        // decl-keyword `const` qualifies the variable's reported type.
        if (slid_type.rfind("const ", 0) != 0
            && slid_type.rfind("mutable ", 0) != 0) {
            slid_type = "const " + slid_type;
        }
        std::string canon = canonType(slid_type);
        if (slid_info_.count(canon) || isAnonTupleType(canon))
            errorAtNode(stmt,
                "Const variable of non-primitive type requires a foldable initializer.");
        std::string ll = llvmType(slid_type);
        std::string val = emitExpr(*cds->def.rhs);
        std::string reg = uniqueAllocaReg(cds->def.name);
        out_ << "    " << reg << " = alloca " << ll << "\n";
        out_ << "    store " << ll << " " << val << ", ptr " << reg << "\n";
        locals_[cds->def.name] = {reg, slid_type, cds->def.file_id, cds->def.tok};
        return;
    }

    // null out the storage location of a source expression after a move
    auto emitNullOut = [&](const Expr& src) {
        if (auto* ve = dynamic_cast<const VarExpr*>(&src)) {
            auto it = locals_.find(ve->name);
            if (it != locals_.end())
                out_ << "    store ptr null, ptr " << it->second.reg << "\n";
        } else if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&src)) {
            std::string obj_ptr;
            std::string stype;
            if (auto* ve2 = dynamic_cast<const VarExpr*>(fa->object.get())) {
                auto it = locals_.find(ve2->name);
                auto tit = locals_.find(ve2->name);
                if (it != locals_.end()) obj_ptr = it->second.reg;
                if (tit != locals_.end()) stype = tit->second.type;
            } else if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
                if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto it = locals_.find(ve2->name);
                    auto tit = locals_.find(ve2->name);
                    if (it != locals_.end()) {
                        std::string loaded = newTmp();
                        out_ << "    " << loaded << " = load ptr, ptr " << it->second.reg << "\n";
                        obj_ptr = loaded;
                    }
                    if (tit != locals_.end()) {
                        stype = tit->second.type;
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
        // Body-level const enforcement: `delete p` runs the pointee's dtor
        // and frees its storage — destructive even when the handle is mutable.
        // Reject if `const` appears anywhere in p's chain: `const T^` blocks
        // (handle is const) and `(const T)^` blocks (pointee is const).
        if (typeHasConst(exprType(*ds->operand)))
            errorAtNode(stmt, "Cannot delete through a const pointer.");
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
        // nullify the pointer variable so it is left in a valid (null) state.
        // Gate on AST kind so non-lvalue operands (e.g. `delete getNew();`)
        // silently skip — matches the prior behavior.
        const Expr* op_expr = ds->operand.get();
        if (dynamic_cast<const VarExpr*>(op_expr)
            || dynamic_cast<const FieldAccessExpr*>(op_expr)
            || dynamic_cast<const ArrayIndexExpr*>(op_expr)) {
            auto lv = resolveLvalue(*op_expr);
            out_ << "    store ptr null, ptr " << lv.addr << "\n";
        }
        return;
    }

    if (auto* arr = dynamic_cast<const ArrayDeclStmt*>(&stmt)) {
        int total = 1;
        for (int d : arr->dims) total *= d;
        // Empty elem_type means "infer from init_values[0] and require homogeneity".
        // For dims.size() > 1, peel dims.size()-1 levels of tuple nesting from
        // init_values[0] to find the leaf element type. The structural walk
        // below enforces shape homogeneity.
        std::string elem_type = arr->elem_type;
        if (elem_type.empty()) {
            if (arr->init_values.empty())
                error(std::string("Array '" + arr->name
                    + "' has no element type and no initializer to infer from."));
            const Expr* probe = arr->init_values[0].get();
            for (int level = 0; level < (int)arr->dims.size() - 1; level++) {
                auto* te = dynamic_cast<const TupleExpr*>(probe);
                if (!te || te->values.empty())
                    errorAtNode(*probe,
                        "Init nesting does not match array dimensions.");
                probe = te->values[0].get();
            }
            elem_type = inferSlidType(*probe);
            if (arr->dims.size() == 1) {
                // Single-source anon-tuple RHS (variable / call return): the
                // tuple unpacks slot-by-slot into the array, so elem_type is
                // the tuple's homogeneous element type, not the tuple type.
                if (arr->init_values.size() == 1 && isAnonTupleType(elem_type)) {
                    auto elems = anonTupleElems(elem_type);
                    if (elems.empty())
                        errorAtNode(*probe,
                            "Cannot infer array element type from empty tuple.");
                    for (auto& t : elems)
                        if (t != elems[0])
                            errorAtNode(*probe,
                                "Cannot infer array element type: tuple '"
                                + elem_type + "' is not homogeneous.");
                    elem_type = elems[0];
                }
                for (int i = 1; i < (int)arr->init_values.size(); i++) {
                    if (inferSlidType(*arr->init_values[i]) != elem_type)
                        errorAtNode(*arr->init_values[i],
                            "Tuple element type does not match element 0 ('"
                            + elem_type + "').");
                }
            }
        }
        std::string base = "%arr_" + arr->name;
        std::string reg = base;
        if (emitted_alloca_regs_.count(reg))
            reg = base + "_" + std::to_string(tmp_counter_++);
        emitted_alloca_regs_.insert(reg);
        std::string elt = llvmType(elem_type);
        out_ << "    " << reg << " = alloca [" << total << " x " << elt << "]\n";
        ArrayInfo ainfo;
        ainfo.elem_type = elem_type;
        ainfo.dims = arr->dims;
        ainfo.alloca_reg = reg;
        array_info_[arr->name] = ainfo;
        parent_array_info_[arr->name] = ainfo;
        // allow array to be captured by nested functions and expose flat
        // shape (e.g. "int[6]") to consumers (for-array etc.).
        locals_[arr->name] = {reg, elem_type + "[" + std::to_string(total) + "]", arr->file_id, arr->tok};
        // Multi-dim slid arrays fall through to the 1D per-slot path and
        // produce wrong storage / malformed IR. Fail loud until proper
        // multi-dim slid init lands.
        if (arr->dims.size() > 1 && slid_info_.count(elem_type)) {
            error(std::string("Multi-dimensional fixed-size array of slid type '"
                + elem_type + "' is not yet supported."));
        }
        // Scalar single-value RHS to a fixed-size array of size > 1 is a
        // compile error — fixed-size arrays demand a tuple-shape initializer.
        // Single-value-promotion is allowed only when total == 1.
        // Tuple-typed single-source (variable / call return) routes to the
        // tuple-variable path below.
        if (arr->init_values.size() == 1 && total > 1) {
            auto src_t = inferSlidType(*arr->init_values[0]);
            if (!isAnonTupleType(src_t)) {
                errorAtNode(*arr->init_values[0],
                    "Fixed-size array '" + arr->name
                    + "' requires a tuple-shape initializer.");
            }
        }
        // Multi-dim primitive / anon-tuple array: structural matching against
        // dims under slids reading (leftmost type-bracket = innermost,
        // rightmost = outermost). The outer init slot count must match
        // dims[n-1]; each inner slot must be a TupleExpr matching the next
        // inner dim (or a single value when that inner dim is 1).
        // Tail-short at the outermost dim → zero-fill (decl-form rule).
        if (arr->dims.size() > 1
                && !slid_info_.count(elem_type)) {
            std::vector<const Expr*> flat(total, nullptr);
            int n_dims = (int)arr->dims.size();
            std::function<void(const std::vector<const Expr*>&, int, int, const Expr*)> walk
                = [&](const std::vector<const Expr*>& vals,
                      int from_dim, int base, const Expr* err_at) {
                int level_size = arr->dims[from_dim];
                int inner_stride = 1;
                for (int k = 0; k < from_dim; k++) inner_stride *= arr->dims[k];
                if ((int)vals.size() > level_size)
                    errorAtNode(*err_at, "Too many values for array level (got "
                        + std::to_string(vals.size()) + ", expected "
                        + std::to_string(level_size) + ").");
                if (from_dim == 0) {
                    for (int i = 0; i < (int)vals.size(); i++)
                        flat[base + i] = vals[i];
                    return;
                }
                for (int i = 0; i < (int)vals.size(); i++) {
                    if (auto* te = dynamic_cast<const TupleExpr*>(vals[i])) {
                        std::vector<const Expr*> sub;
                        sub.reserve(te->values.size());
                        for (auto& v : te->values) sub.push_back(v.get());
                        walk(sub, from_dim - 1, base + i * inner_stride, vals[i]);
                    } else if (arr->dims[from_dim - 1] == 1) {
                        std::vector<const Expr*> one{ vals[i] };
                        walk(one, from_dim - 1, base + i * inner_stride, vals[i]);
                    } else {
                        errorAtNode(*vals[i], "Init slot must be a tuple of "
                            + std::to_string(arr->dims[from_dim - 1])
                            + " values.");
                    }
                }
            };
            std::vector<const Expr*> top;
            top.reserve(arr->init_values.size());
            for (auto& iv : arr->init_values) top.push_back(iv.get());
            const Expr* err_anchor = top.empty()
                ? static_cast<const Expr*>(nullptr) : top[0];
            if (err_anchor != nullptr)
                walk(top, n_dims - 1, 0, err_anchor);

            for (int i = 0; i < total; i++) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr [" << total
                     << " x " << elt << "], ptr " << reg
                     << ", i32 0, i32 " << i << "\n";
                if (flat[i] == nullptr) {
                    if (isAnonTupleType(elem_type)) {
                        out_ << "    store " << elt
                             << " zeroinitializer, ptr " << gep << "\n";
                    } else {
                        std::string z = isIndirectType(elem_type) ? "null"
                            : (elem_type == "float" || elem_type == "float32" || elem_type == "float64") ? "0.0"
                            : "0";
                        out_ << "    store " << elt << " " << z
                             << ", ptr " << gep << "\n";
                    }
                    continue;
                }
                if (isAnonTupleType(elem_type)) {
                    if (auto* te = dynamic_cast<const TupleExpr*>(flat[i])) {
                        std::vector<const Expr*> ov;
                        ov.reserve(te->values.size());
                        for (auto& v : te->values) ov.push_back(v.get());
                        emitInitFieldsAtPtrs(elem_type, gep, {}, ov);
                    } else {
                        std::string val = emitExpr(*flat[i]);
                        out_ << "    store " << elt << " " << val
                             << ", ptr " << gep << "\n";
                    }
                    continue;
                }
                std::string val = emitExpr(*flat[i]);
                if (inferSlidType(*flat[i]) != elem_type) {
                    std::string src_t = exprLlvmType(*flat[i]);
                    if (src_t != elt) {
                        static const std::map<std::string,int> rank =
                            {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                        auto sit = rank.find(src_t), dit = rank.find(elt);
                        if (sit != rank.end() && dit != rank.end()) {
                            std::string coerced = newTmp();
                            if (dit->second > sit->second)
                                out_ << "    " << coerced << " = sext "
                                     << src_t << " " << val << " to " << elt << "\n";
                            else
                                out_ << "    " << coerced << " = trunc "
                                     << src_t << " " << val << " to " << elt << "\n";
                            val = coerced;
                        } else {
                            error(std::string("Type mismatch: cannot assign '"
                                + inferSlidType(*flat[i]) + "' to '" + elem_type + "'"));
                        }
                    }
                }
                out_ << "    store " << elt << " " << val
                     << ", ptr " << gep << "\n";
            }
            return;
        }
        // Single tuple-shape source for the whole array: unpack per slot.
        // Covers `T arr[N] = tuple_var;` and `T arr[N] = make_tuple();`.
        if (arr->init_values.size() == 1) {
            const Expr* src_expr = arr->init_values[0].get();
            std::string src_slids = inferSlidType(*src_expr);
            if (isAnonTupleType(src_slids)) {
                auto src_elems = anonTupleElems(src_slids);
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(src_expr)) {
                    auto lit = locals_.find(ve->name);
                    if (lit != locals_.end()) src_ptr = lit->second.reg;
                }
                if (src_ptr.empty()) {
                    std::string val = emitExpr(*src_expr);
                    src_ptr = newTmp();
                    out_ << "    " << src_ptr << " = alloca "
                         << llvmType(src_slids) << "\n";
                    out_ << "    store " << llvmType(src_slids) << " " << val
                         << ", ptr " << src_ptr << "\n";
                }
                std::string src_llvm = llvmType(src_slids);
                int n = std::min((int)src_elems.size(), total);
                for (int i = 0; i < n; i++) {
                    std::string dst_gep = newTmp();
                    out_ << "    " << dst_gep << " = getelementptr [" << total
                         << " x " << elt << "], ptr " << reg
                         << ", i32 0, i32 " << i << "\n";
                    std::string src_gep = newTmp();
                    out_ << "    " << src_gep << " = getelementptr " << src_llvm
                         << ", ptr " << src_ptr << ", i32 0, i32 " << i << "\n";
                    if (slid_info_.count(elem_type)) {
                        emitSlidSlotAssign(elem_type, dst_gep, src_gep,
                            /*is_move=*/false, /*is_init=*/true);
                        if (hasDtorInChain(elem_type))
                            dtor_vars_.push_back({arr->name, elem_type, i});
                    } else {
                        std::string v = newTmp();
                        out_ << "    " << v << " = load " << llvmType(src_elems[i])
                             << ", ptr " << src_gep << "\n";
                        out_ << "    store " << llvmType(src_elems[i]) << " " << v
                             << ", ptr " << dst_gep << "\n";
                    }
                }
                // Tail zero-fill / default-construct (decl-form rule).
                for (int i = n; i < total; i++) {
                    std::string dst_gep = newTmp();
                    out_ << "    " << dst_gep << " = getelementptr [" << total
                         << " x " << elt << "], ptr " << reg
                         << ", i32 0, i32 " << i << "\n";
                    if (slid_info_.count(elem_type)) {
                        emitConstructAtPtrs(elem_type, dst_gep, {}, {});
                        if (hasDtorInChain(elem_type))
                            dtor_vars_.push_back({arr->name, elem_type, i});
                    } else {
                        std::string z = isIndirectType(elem_type) ? "null"
                            : (elem_type == "float" || elem_type == "float32" || elem_type == "float64") ? "0.0"
                            : "0";
                        out_ << "    store " << elt << " " << z
                             << ", ptr " << dst_gep << "\n";
                    }
                }
                return;
            }
        }
        // Init each provided slot per the desugar rule. Slid element:
        // matching slid value → emitSlidSlotAssign; tuple-shape value →
        // recurse via emitInitFieldsAtPtrs (compound init); single value
        // → single-value promotion (feed as first ctor arg).
        for (int i = 0; i < (int)arr->init_values.size(); i++) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << total << " x " << elt << "], ptr "
                 << reg << ", i32 0, i32 " << i << "\n";
            if (slid_info_.count(elem_type)) {
                std::string src_type = inferSlidType(*arr->init_values[i]);
                if (src_type == elem_type) {
                    if (auto* ce = dynamic_cast<const CallExpr*>(arr->init_values[i].get())) {
                        if (ce->callee == elem_type) {
                            emitConstructAt(elem_type, gep, ce->args);
                            if (hasDtorInChain(elem_type))
                                dtor_vars_.push_back({arr->name, elem_type, i});
                            continue;
                        }
                    }
                    std::string src_ptr;
                    if (auto* ve = dynamic_cast<const VarExpr*>(arr->init_values[i].get());
                            ve && locals_.count(ve->name)) {
                        src_ptr = locals_[ve->name].reg;
                    } else {
                        src_ptr = emitExpr(*arr->init_values[i]);
                    }
                    emitSlidSlotAssign(elem_type, gep, src_ptr,
                        /*is_move=*/false, /*is_init=*/true);
                } else if (auto* te = dynamic_cast<const TupleExpr*>(arr->init_values[i].get())) {
                    std::vector<const Expr*> ov;
                    ov.reserve(te->values.size());
                    for (auto& v : te->values) ov.push_back(v.get());
                    emitInitFieldsAtPtrs(elem_type, gep, {}, ov);
                    emitCtorCall(elem_type, gep);
                } else {
                    std::vector<const Expr*> one{ arr->init_values[i].get() };
                    emitInitFieldsAtPtrs(elem_type, gep, one, {});
                    emitCtorCall(elem_type, gep);
                }
                if (hasDtorInChain(elem_type))
                    dtor_vars_.push_back({arr->name, elem_type, i});
            } else if (isAnonTupleType(elem_type)) {
                if (auto* te = dynamic_cast<const TupleExpr*>(arr->init_values[i].get())) {
                    std::vector<const Expr*> ov;
                    ov.reserve(te->values.size());
                    for (auto& v : te->values) ov.push_back(v.get());
                    emitInitFieldsAtPtrs(elem_type, gep, {}, ov);
                } else {
                    std::string val = emitExpr(*arr->init_values[i]);
                    out_ << "    store " << llvmType(elem_type) << " " << val
                         << ", ptr " << gep << "\n";
                }
            } else {
                std::string val = emitExpr(*arr->init_values[i]);
                if (inferSlidType(*arr->init_values[i]) != elem_type) {
                    std::string src_t = exprLlvmType(*arr->init_values[i]);
                    if (src_t != elt) {
                        static const std::map<std::string,int> rank =
                            {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                        auto sit = rank.find(src_t), dit = rank.find(elt);
                        if (sit != rank.end() && dit != rank.end()) {
                            std::string coerced = newTmp();
                            if (dit->second > sit->second)
                                out_ << "    " << coerced << " = sext "
                                     << src_t << " " << val << " to " << elt << "\n";
                            else
                                out_ << "    " << coerced << " = trunc "
                                     << src_t << " " << val << " to " << elt << "\n";
                            val = coerced;
                        } else {
                            error(std::string("Type mismatch: cannot assign '"
                                + inferSlidType(*arr->init_values[i])
                                + "' to '" + elem_type + "'"));
                        }
                    }
                }
                out_ << "    store " << elt << " " << val << ", ptr " << gep << "\n";
            }
        }
        // Decl-form rule: missing tail slots zero-fill (primitives/tuples)
        // or default-construct (slids). Includes the "no initializer at all"
        // case (slid arrays get per-slot default-construct).
        for (int i = (int)arr->init_values.size(); i < total; i++) {
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << total << " x " << elt
                 << "], ptr " << reg << ", i32 0, i32 " << i << "\n";
            if (slid_info_.count(elem_type)) {
                emitConstructAtPtrs(elem_type, gep, {}, {});
                if (hasDtorInChain(elem_type))
                    dtor_vars_.push_back({arr->name, elem_type, i});
            } else if (isAnonTupleType(elem_type)) {
                out_ << "    store " << llvmType(elem_type)
                     << " zeroinitializer, ptr " << gep << "\n";
            } else if (!arr->init_values.empty()) {
                // primitives: only zero-fill when the user provided a partial
                // tuple init (decl-form rule). Fully-uninit declarations
                // leave primitive slots alone, matching prior behavior.
                std::string z = isIndirectType(elem_type) ? "null"
                    : (elem_type == "float" || elem_type == "float32" || elem_type == "float64") ? "0.0"
                    : "0";
                out_ << "    store " << elt << " " << z << ", ptr " << gep << "\n";
            }
        }
        return;
    }

    if (auto* decl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        // resolve inferred type (empty string means x = expr; with no explicit type)
        std::string inferred;
        if (decl->type.empty()) {
            if (!decl->init)
                error(std::string("Inferred variable declaration requires initializer"));
            inferred = inferSlidType(*decl->init);
        }
        const std::string& eff_type = decl->type.empty() ? inferred : decl->type;

        // Non-primitive element types (slid, anon-tuple, fixed-array) require an
        // explicit loop var — the author must pick value vs reference. Reject with
        // caret on the loop var token (the synthesized decl carries it).
        if (decl->is_loop_var && decl->type.empty() && !isPrimitive(eff_type)) {
            errorAtNode(*decl,
                "For-loop cannot infer the loop variable type; "
                "use an explicit 'Type " + decl->name + "' or 'Type^ " + decl->name + "'.");
        }

        // class instantiation
        if (slid_info_.count(eff_type)) {
            auto& info = slid_info_[eff_type];
            if (info.is_virtual_class) {
                for (auto& slot : info.vtable) {
                    if (slot.is_pure)
                        error(std::string("Cannot instantiate pure virtual class '" + eff_type
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
                    locals_[decl->name] = {temp_ptr, eff_type, decl->file_id, decl->tok};
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
                    locals_[decl->name] = {reg, eff_type, decl->file_id, decl->tok};
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
            locals_[decl->name] = {reg, eff_type, decl->file_id, decl->tok};

            // Type name = (a, b, c);  or  Type name(a, b) = (c, d);
            // rhs tuple overrides lhs ctor_args per position; missing rhs positions fall back to lhs.
            bool tuple_init_handled = false;
            if (!decl->is_move) {
                if (auto* te = dynamic_cast<const TupleExpr*>(decl->init.get())) {
                    emitConstructAt(eff_type, reg, decl->ctor_args, te->values);
                    tuple_init_handled = true;
                }
            }
            // Tuple-shape RHS variable / call: default-construct then unpack
            // per slot from the source's memory. Decl-step defaults cover
            // tail slots automatically (the slid's ctor ran).
            if (!tuple_init_handled && decl->init && !decl->is_move) {
                std::string src_slids = inferSlidType(*decl->init);
                if (isAnonTupleType(src_slids)) {
                    emitConstructAt(eff_type, reg, decl->ctor_args);
                    auto src_elems = anonTupleElems(src_slids);
                    std::string src_ptr;
                    if (auto* ve = dynamic_cast<const VarExpr*>(decl->init.get())) {
                        auto lit = locals_.find(ve->name);
                        if (lit != locals_.end()) src_ptr = lit->second.reg;
                    }
                    if (src_ptr.empty()) {
                        std::string val = emitExpr(*decl->init);
                        src_ptr = newTmp();
                        out_ << "    " << src_ptr << " = alloca "
                             << llvmType(src_slids) << "\n";
                        out_ << "    store " << llvmType(src_slids) << " " << val
                             << ", ptr " << src_ptr << "\n";
                    }
                    std::string src_llvm = llvmType(src_slids);
                    int n = std::min((int)info.field_types.size(),
                                     (int)src_elems.size());
                    for (int i = 0; i < n; i++) {
                        const std::string& ft = info.field_types[i];
                        std::string dst_gep = emitFieldGep(eff_type, reg, i);
                        std::string src_gep = newTmp();
                        out_ << "    " << src_gep << " = getelementptr "
                             << src_llvm << ", ptr " << src_ptr
                             << ", i32 0, i32 " << i << "\n";
                        if (slid_info_.count(ft) || isAnonTupleType(ft)) {
                            emitSlidSlotAssign(ft, dst_gep, src_gep,
                                /*is_move=*/false, /*is_init=*/false);
                        } else {
                            std::string val = newTmp();
                            out_ << "    " << val << " = load " << llvmType(ft)
                                 << ", ptr " << src_gep << "\n";
                            out_ << "    store " << llvmType(ft) << " " << val
                                 << ", ptr " << dst_gep << "\n";
                        }
                    }
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
                            auto tit2 = locals_.find(ve->name);
                            auto lit  = locals_.find(ve->name);
                            if (tit2 != locals_.end() && lit != locals_.end()) {
                                if (tit2->second.type == eff_type) {
                                    src_ptr = lit->second.reg;
                                } else if (tit2->second.type == eff_type + "^") {
                                    std::string loaded = newTmp();
                                    out_ << "    " << loaded << " = load ptr, ptr " << lit->second.reg << "\n";
                                    src_ptr = loaded;
                                }
                            }
                            // Slid-typed local init from self (e.g. Foo x = self;).
                            // Use self_ptr_ for the source — fixed by locals_["self"].reg
                            // for the entry case, but self_ptr_ tracks any inline-ctor-body remap.
                            if (src_ptr.empty() && ve->name == "self"
                                    && !current_slid_.empty() && current_slid_ == eff_type)
                                src_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
                        }
                        // any expr whose type matches eff_type: emitExpr returns a slid ptr.
                        if (src_ptr.empty() && inferSlidType(*init_expr) == eff_type)
                            src_ptr = emitExpr(*init_expr);
                        // dst was already default-constructed by emitConstructAt above —
                        // pass is_init=false so the field-walk doesn't re-construct.
                        if (!src_ptr.empty()) {
                            emitSlidSlotAssign(eff_type, reg, src_ptr, decl->is_move, /*is_init=*/false);
                        } else if (!info.field_types.empty()) {
                            // Single-value promotion: rhs becomes (rhs,) →
                            // slot 0 of the slid gets rhs; other slots keep
                            // their default-constructed values.
                            const std::string& ft0 = info.field_types[0];
                            std::string gep = emitFieldGep(eff_type, reg, 0);
                            if (slid_info_.count(ft0) || isAnonTupleType(ft0)) {
                                if (auto* te2 = dynamic_cast<const TupleExpr*>(init_expr)) {
                                    std::vector<const Expr*> ov;
                                    ov.reserve(te2->values.size());
                                    for (auto& v : te2->values) ov.push_back(v.get());
                                    emitInitFieldsAtPtrs(ft0, gep, {}, ov);
                                } else {
                                    std::vector<const Expr*> one{ init_expr };
                                    emitInitFieldsAtPtrs(ft0, gep, one, {});
                                }
                            } else {
                                std::string val = emitExpr(*init_expr);
                                out_ << "    store " << llvmType(ft0) << " " << val
                                     << ", ptr " << gep << "\n";
                            }
                        }
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
            locals_[decl->name] = {reg, eff_type, decl->file_id, decl->tok};
            if (auto* te = dynamic_cast<const TupleExpr*>(decl->init.get())) {
                if ((int)te->values.size() > (int)elems.size())
                    error(std::string("Too many values for tuple '" + eff_type
                        + "': has " + std::to_string(elems.size())
                        + " elements, got " + std::to_string(te->values.size()) + "."));
                for (int i = 0; i < (int)elems.size() && i < (int)te->values.size(); i++) {
                    // PPID: each tuple-literal slot is its own phrase.
                    pushPostIncQueue();
                    emitPrePass(*te->values[i]);
                    struct FlushGuard { Codegen* cg; ~FlushGuard() { cg->flushPostIncQueue(); } } _g{this};
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr " << struct_llvm
                         << ", ptr " << reg << ", i32 0, i32 " << i << "\n";
                    // slid-typed element: matching slid → emitSlidSlotAssign;
                    // tuple-shape value → recurse via emitInitFieldsAtPtrs;
                    // single value → promote (feed as first ctor arg).
                    if (slid_info_.count(elems[i])) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type == elems[i]) {
                            // Prvalue elision: construct ctor call directly at the slot.
                            if (auto* ce = dynamic_cast<const CallExpr*>(te->values[i].get())) {
                                if (ce->callee == elems[i]) {
                                    emitConstructAt(elems[i], gep, ce->args);
                                    if (hasDtorInChain(elems[i]))
                                        dtor_vars_.push_back({decl->name, elems[i], i});
                                    continue;
                                }
                            }
                            std::string src_ptr;
                            if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get());
                                    ve && locals_.count(ve->name)) {
                                src_ptr = locals_[ve->name].reg;
                            } else {
                                src_ptr = emitExpr(*te->values[i]);
                            }
                            emitSlidSlotAssign(elems[i], gep, src_ptr, decl->is_move, /*is_init=*/true);
                        } else if (auto* te2 = dynamic_cast<const TupleExpr*>(te->values[i].get())) {
                            std::vector<const Expr*> ov;
                            ov.reserve(te2->values.size());
                            for (auto& v : te2->values) ov.push_back(v.get());
                            emitInitFieldsAtPtrs(elems[i], gep, {}, ov);
                        } else {
                            std::vector<const Expr*> one{ te->values[i].get() };
                            emitInitFieldsAtPtrs(elems[i], gep, one, {});
                        }
                        if (hasDtorInChain(elems[i]))
                            dtor_vars_.push_back({decl->name, elems[i], i});
                        continue;
                    }
                    requirePtrInit(elems[i], *te->values[i]);
                    std::string val = valOrNullptrCheck(elems[i], *te->values[i]);
                    out_ << "    store " << llvmType(elems[i]) << " " << val << ", ptr " << gep << "\n";
                    if (decl->is_move && isIndirectType(elems[i]))
                        emitNullOut(*te->values[i]);
                }
                // Decl-form rule: missing tail slots in tuples zero-fill.
                for (int i = (int)te->values.size(); i < (int)elems.size(); i++) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr " << struct_llvm
                         << ", ptr " << reg << ", i32 0, i32 " << i << "\n";
                    if (slid_info_.count(elems[i])) {
                        // slid slot — default-construct (its ctor's defaults apply).
                        emitConstructAtPtrs(elems[i], gep, {}, {});
                        if (hasDtorInChain(elems[i]))
                            dtor_vars_.push_back({decl->name, elems[i], i});
                    } else if (isAnonTupleType(elems[i])) {
                        // nested tuple slot — zero-fill recursively (memzero suffices).
                        out_ << "    store " << llvmType(elems[i])
                             << " zeroinitializer, ptr " << gep << "\n";
                    } else {
                        std::string z = isIndirectType(elems[i]) ? "null"
                            : (elems[i] == "float" || elems[i] == "float32" || elems[i] == "float64") ? "0.0"
                            : "0";
                        out_ << "    store " << llvmType(elems[i]) << " " << z
                             << ", ptr " << gep << "\n";
                    }
                }
                return;
            }
            // non-literal initializer
            std::string src_slids = inferSlidType(*decl->init);
            if (src_slids == eff_type) {
                // matching tuple-shape source: field-by-field copy/move.
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(decl->init.get())) {
                    auto lit = locals_.find(ve->name);
                    if (lit == locals_.end()) error(std::string("Undefined tuple source: " + ve->name));
                    src_ptr = lit->second.reg;
                } else {
                    // non-variable source (e.g. tuple-returning call): spill once
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
            // single-value promotion: rhs becomes (rhs,) → slot 0 = rhs;
            // remaining slots zero-fill (decl form).
            {
                const std::string& ft = elems[0];
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr " << struct_llvm
                     << ", ptr " << reg << ", i32 0, i32 0\n";
                if (slid_info_.count(ft)) {
                    if (auto* te2 = dynamic_cast<const TupleExpr*>(decl->init.get())) {
                        std::vector<const Expr*> ov;
                        ov.reserve(te2->values.size());
                        for (auto& v : te2->values) ov.push_back(v.get());
                        emitInitFieldsAtPtrs(ft, gep, {}, ov);
                    } else {
                        std::vector<const Expr*> one{ decl->init.get() };
                        emitInitFieldsAtPtrs(ft, gep, one, {});
                    }
                    if (hasDtorInChain(ft))
                        dtor_vars_.push_back({decl->name, ft, 0});
                } else if (isAnonTupleType(ft)) {
                    std::string val = emitExpr(*decl->init);
                    out_ << "    store " << llvmType(ft) << " " << val
                         << ", ptr " << gep << "\n";
                } else {
                    std::string val = emitExpr(*decl->init);
                    out_ << "    store " << llvmType(ft) << " " << val
                         << ", ptr " << gep << "\n";
                }
                // Tail zero-fill (decl-form rule).
                for (int i = 1; i < (int)elems.size(); i++) {
                    std::string tgep = newTmp();
                    out_ << "    " << tgep << " = getelementptr " << struct_llvm
                         << ", ptr " << reg << ", i32 0, i32 " << i << "\n";
                    if (slid_info_.count(elems[i])) {
                        emitConstructAtPtrs(elems[i], tgep, {}, {});
                        if (hasDtorInChain(elems[i]))
                            dtor_vars_.push_back({decl->name, elems[i], i});
                    } else if (isAnonTupleType(elems[i])) {
                        out_ << "    store " << llvmType(elems[i])
                             << " zeroinitializer, ptr " << tgep << "\n";
                    } else {
                        std::string z = isIndirectType(elems[i]) ? "null"
                            : (elems[i] == "float" || elems[i] == "float32" || elems[i] == "float64") ? "0.0"
                            : "0";
                        out_ << "    store " << llvmType(elems[i]) << " " << z
                             << ", ptr " << tgep << "\n";
                    }
                }
                return;
            }
        }

        // primitive or reference variable declaration
        // reject unknown types (not a slid, not a primitive, not a pointer)
        {
            static const std::set<std::string> known_primitives = {
                "int","int8","int16","int32","int64",
                "uint","uint8","uint16","uint32","uint64",
                "char","bool","float","float32","float64","void","intptr"
            };
            std::string canon = canonType(eff_type);
            bool is_ptr = (!canon.empty() && canon.back() == '^')
                       || (canon.size() >= 2 && canon.substr(canon.size()-2) == "[]");
            if (!known_primitives.count(canon) && !is_ptr)
                error(std::string("Unknown type '" + eff_type + "'"));
        }
        std::string reg = uniqueAllocaReg(decl->name);
        std::string llvm_t = llvmType(eff_type);
        out_ << "    " << reg << " = alloca " << llvm_t << "\n";
        locals_[decl->name] = {reg, eff_type, decl->file_id, decl->tok};
        if (!decl->init) return; // uninitialized — alloca only
        requirePtrInit(eff_type, *decl->init);
        std::string val = valOrNullptrCheck(eff_type, *decl->init);
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
        // self = expr — special semantic: write the result through op= on the
        // current object. self isn't a regular local that can be reassigned;
        // this dispatches op= dispatch on the current_slid_ class with self_ptr_
        // as the receiver (handles inline-ctor-body remap).
        if (assign->name == "self" && !current_slid_.empty()) {
            // Phase 3: writing to self in a const method is forbidden.
            auto sit = locals_.find("self");
            if (sit != locals_.end() && typeStartsWithConst(sit->second.type))
                errorAtNode(stmt, "Cannot assign to 'self' in a const method.");
            std::string op_func = resolveSingleArgOverload(current_slid_ + "__op=", *assign->value);
            if (op_func.empty())
                error(std::string("No matching op= on '" + current_slid_ + "' for 'self = <expr>'"));
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
                // Phase 3: writing to self.field in a const method is forbidden.
                auto sit = locals_.find("self");
                if (sit != locals_.end() && typeStartsWithConst(sit->second.type))
                    errorAtNode(stmt,
                        "Cannot write to field '" + assign->name + "' in a const method.");
                int idx = info.field_index[assign->name];
                requirePtrInit(info.field_types[idx], *assign->value);
                std::string field_type = llvmType(info.field_types[idx]);
                std::string gep = newTmp();
                std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                     << ", ptr " << self << ", i32 0, i32 " << idx << "\n";
                std::string val = valOrNullptrCheck(info.field_types[idx], *assign->value);
                out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
                if (assign->is_move && isIndirectType(info.field_types[idx]))
                    emitNullOut(*assign->value);
                return;
            }
        }
        auto it = locals_.find(assign->name);
        if (it == locals_.end()) {
            // global slid field — store through the registered LLVM symbol.
            if (auto* ge = lookupGlobal(assign->name)) {
                if (current_func_name_ == "main" && global_lifetime_depth_ == 0)
                    error(std::string("Cannot access global '" + assign->name
                        + "' outside the `global;` scope in `main`."));
                // Evaluate the RHS first; sentinel gate fires on the write
                // itself, after the value is computed (mirrors the read path).
                std::string llvm_ty = llvmType(ge->slids_type);
                std::string val = emitExpr(*assign->value);
                emitLazySentinelGate(*ge);
                out_ << "    store " << llvm_ty << " " << val
                     << ", ptr " << ge->llvm_symbol << "\n";
                return;
            }
            if (assign->name.size() >= 2
                && assign->name[0] == ':' && assign->name[1] == ':') {
                std::string field = assign->name.substr(2);
                error(std::string("Identifier '" + field
                    + "' is not declared in the unnamed namespace."));
            }
            if (assign->name.find(':') != std::string::npos) {
                auto git = globals_.find(assign->name);
                if (git != globals_.end()
                    && !git->second.visible_in_function.empty()
                    && git->second.visible_in_function != current_func_name_) {
                    error(std::string("Global '" + assign->name
                        + "' is not visible outside function '"
                        + git->second.visible_in_function + "'."));
                }
            }
            error(std::string("Undefined variable: " + assign->name));
        }
        // Body-level const enforcement: rebinding a `const T` local (declared
        // const, or const by-value param) is forbidden. Top-level `const`
        // blocks rebind; the leaf-const form `(const T)^` is mutable handle
        // and falls through. Phase: const-method `self` (Phase 3) and
        // const-method field-via-self (Phase 3) are NOT yet enforced here.
        if (typeStartsWithConst(it->second.type))
            errorAtNode(stmt,
                "Cannot assign to const variable '" + assign->name + "'.");
        auto tit = locals_.find(assign->name);
        // anonymous-tuple local LHS: route per the element-wise rule.
        if (tit != locals_.end() && isAnonTupleType(tit->second.type)) {
            const std::string& lhs_t = tit->second.type;
            auto elems = anonTupleElems(lhs_t);
            // tuple-literal rhs: tuple = (a, b, ...); — per-element, partial overwrite allowed
            if (auto* te = dynamic_cast<const TupleExpr*>(assign->value.get())) {
                int nfields = (int)elems.size();
                if ((int)te->values.size() > nfields)
                    errorWithNote(std::string("Tuple has " + std::to_string(te->values.size())
                        + " values but '" + assign->name + "' has " + std::to_string(nfields)
                        + " elements"),
                        tit->second.file_id, tit->second.tok,
                        "'" + assign->name + "' declared here.");
                for (int i = 0; i < (int)te->values.size(); i++) {
                    const std::string& ft = elems[i];
                    std::string elem_llvm = llvmType(ft);
                    std::string gep = emitFieldGep(lhs_t, it->second.reg, i);
                    // slid-typed element: copy/move from a same-typed source
                    if (slid_info_.count(ft)) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type != ft)
                            error(std::string("Slid tuple element type mismatch: expected '"
                                + ft + "', got '" + src_type + "'"));
                        std::string src_ptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                            src_ptr = locals_[ve->name].reg;
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
                            errorWithNote(std::string("Type mismatch: cannot assign '"
                                + inferSlidType(*te->values[i]) + "' to tuple element "
                                + std::to_string(i) + " of type '" + ft + "'"),
                                tit->second.file_id, tit->second.tok,
                                "'" + assign->name + "' declared here.");
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
            if (src_slids == lhs_t) {
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(assign->value.get())) {
                    auto lit = locals_.find(ve->name);
                    if (lit != locals_.end()) src_ptr = lit->second.reg;
                }
                if (src_ptr.empty())
                    error(std::string("Tuple copy from non-variable source not supported"));
                emitSlidSlotAssign(lhs_t, it->second.reg, src_ptr, assign->is_move);
                return;
            }
            // Tuple-shape rhs of non-matching type: per-slot copy with type
            // checks. Length must be ≤ LHS (assignment-form rule keeps tail
            // slots untouched); each slot's type must match exactly.
            if (isAnonTupleType(src_slids)) {
                auto src_elems = anonTupleElems(src_slids);
                if ((int)src_elems.size() > (int)elems.size())
                    errorWithNote(std::string("Tuple has "
                        + std::to_string(src_elems.size()) + " values but '"
                        + assign->name + "' has " + std::to_string(elems.size())
                        + " elements"),
                        tit->second.file_id, tit->second.tok,
                        "'" + assign->name + "' declared here.");
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(assign->value.get())) {
                    auto lit = locals_.find(ve->name);
                    if (lit != locals_.end()) src_ptr = lit->second.reg;
                }
                if (src_ptr.empty()) {
                    std::string val = emitExpr(*assign->value);
                    src_ptr = newTmp();
                    out_ << "    " << src_ptr << " = alloca "
                         << llvmType(src_slids) << "\n";
                    out_ << "    store " << llvmType(src_slids) << " " << val
                         << ", ptr " << src_ptr << "\n";
                }
                std::string src_llvm = llvmType(src_slids);
                for (int i = 0; i < (int)src_elems.size(); i++) {
                    const std::string& dst_ft = elems[i];
                    const std::string& src_ft = src_elems[i];
                    if (src_ft != dst_ft)
                        errorWithNote(std::string("Type mismatch: cannot assign '"
                            + src_ft + "' to tuple element "
                            + std::to_string(i) + " of type '" + dst_ft + "'"),
                            tit->second.file_id, tit->second.tok,
                            "'" + assign->name + "' declared here.");
                    std::string dst_gep = emitFieldGep(lhs_t, it->second.reg, i);
                    std::string src_gep = newTmp();
                    out_ << "    " << src_gep << " = getelementptr " << src_llvm
                         << ", ptr " << src_ptr << ", i32 0, i32 " << i << "\n";
                    if (slid_info_.count(dst_ft) || isAnonTupleType(dst_ft)) {
                        emitSlidSlotAssign(dst_ft, dst_gep, src_gep, assign->is_move);
                    } else {
                        std::string v = newTmp();
                        out_ << "    " << v << " = load " << llvmType(dst_ft)
                             << ", ptr " << src_gep << "\n";
                        out_ << "    store " << llvmType(dst_ft) << " " << v
                             << ", ptr " << dst_gep << "\n";
                    }
                }
                return;
            }
            // single-value promotion: rhs becomes (rhs,) → slot 0 = rhs,
            // remaining slots untouched (assignment-form rule).
            {
                const std::string& ft = elems[0];
                std::string gep = emitFieldGep(lhs_t, it->second.reg, 0);
                if (slid_info_.count(ft)) {
                    std::vector<const Expr*> one{ assign->value.get() };
                    emitInitFieldsAtPtrs(ft, gep, one, {});
                    return;
                }
                if (isAnonTupleType(ft)) {
                    if (auto* te2 = dynamic_cast<const TupleExpr*>(assign->value.get())) {
                        std::vector<const Expr*> ov;
                        ov.reserve(te2->values.size());
                        for (auto& v : te2->values) ov.push_back(v.get());
                        emitInitFieldsAtPtrs(ft, gep, {}, ov);
                    } else {
                        std::vector<const Expr*> one{ assign->value.get() };
                        emitInitFieldsAtPtrs(ft, gep, one, {});
                    }
                    return;
                }
                // primitive slot
                std::string val = emitExpr(*assign->value);
                std::string elem_llvm = llvmType(ft);
                std::string src_t = exprLlvmType(*assign->value);
                if (src_t != elem_llvm) {
                    static const std::map<std::string,int> rank =
                        {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                    auto sit = rank.find(src_t), dit = rank.find(elem_llvm);
                    if (sit != rank.end() && dit != rank.end()) {
                        std::string coerced = newTmp();
                        if (dit->second > sit->second)
                            out_ << "    " << coerced << " = sext " << src_t
                                 << " " << val << " to " << elem_llvm << "\n";
                        else
                            out_ << "    " << coerced << " = trunc " << src_t
                                 << " " << val << " to " << elem_llvm << "\n";
                        val = coerced;
                    } else {
                        errorWithNote(std::string("Type mismatch: cannot assign '"
                            + inferSlidType(*assign->value) + "' to '" + ft + "'"),
                            tit->second.file_id, tit->second.tok,
                            "'" + assign->name + "' declared here.");
                    }
                }
                out_ << "    store " << elem_llvm << " " << val
                     << ", ptr " << gep << "\n";
                return;
            }
        }
        // compound assignment: x = x op rhs → detect and dispatch to op{op}= directly
        // (parser desugars x += rhs into x = x + rhs; we undo that here for slid types)
        if (tit != locals_.end() && slid_info_.count(tit->second.type)) {
            if (auto* be = dynamic_cast<const BinaryExpr*>(assign->value.get())) {
                if (auto* lve = dynamic_cast<const VarExpr*>(be->left.get())) {
                    if (lve->name == assign->name && isCompoundableOp(be->op)) {
                        const std::string& slid_name = tit->second.type;
                        std::string compound_base = slid_name + "__op" + be->op + "=";
                        std::string mangled = resolveSingleArgOverload(compound_base, *be->right);
                        if (!mangled.empty()) {
                            auto& ptypes = func_param_types_[mangled];
                            std::string param_type = ptypes.empty() ? "" : ptypes[0];
                            std::string arg_val = emitArgForParam(*be->right, param_type);
                            std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                            out_ << "    call void @" << llvmGlobalName(mangled)
                                 << "(ptr " << it->second.reg << ", " << ptype_str << " " << arg_val << ")\n";
                            return;
                        }
                        // no direct op{op}=: try coercing rhs to the slid type via op=,
                        // then call op{op}=(slid^) — or fall back to op{op}(slid^, slid^).
                        std::string coerce = resolveSingleArgOverload(slid_name + "__op=", *be->right);
                        if (!coerce.empty()) {
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
                                for (auto& [m, ptypes, _pm, _pmt, _fid] : opit->second)
                                    if (ptypes.size() == 1 && isRefType(ptypes[0])) { via_ref = m; break; }
                            if (!via_ref.empty()) {
                                out_ << "    call void @" << llvmGlobalName(via_ref)
                                     << "(ptr " << it->second.reg << ", ptr " << tmp << ")\n";
                            } else {
                                // fall back to op{op}(slid^, slid^) — result stored into lhs (sret)
                                std::string op_base = slid_name + "__op" + be->op;
                                auto oppit = method_overloads_.find(op_base);
                                if (oppit != method_overloads_.end())
                                    for (auto& [m, ptypes, _pm, _pmt, _fid] : oppit->second)
                                        if (ptypes.size() == 2 && isRefType(ptypes[0]) && isRefType(ptypes[1])) {
                                            out_ << "    call void @" << llvmGlobalName(m)
                                                 << "(ptr " << it->second.reg << ", ptr " << it->second.reg
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
        if (tit != locals_.end() && slid_info_.count(tit->second.type)) {
            if (auto* be = dynamic_cast<const BinaryExpr*>(assign->value.get())) {
                std::string op_func = resolveOperatorOverload(be->op, *be->left, *be->right);
                if (!op_func.empty()) {
                    // call op func with sret into lhs alloca
                    // first call dtor on existing lhs value
                    const std::string& slid_name = tit->second.type;
                    if (slid_info_.at(slid_name).has_dtor) {
                        emitDtorChainCall(slid_name, it->second.reg);
                        // re-init fields to zero/null
                        auto& info = slid_info_[slid_name];
                        for (int i = 0; i < (int)info.field_types.size(); i++) {
                            std::string ft = llvmType(info.field_types[i]);
                            std::string gep = newTmp();
                            out_ << "    " << gep << " = getelementptr %struct." << slid_name << ", ptr " << it->second.reg << ", i32 0, i32 " << i << "\n";
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
                        args = "ptr " + it->second.reg;
                    } else {
                        args = "ptr sret(%struct." + slid_name + ") " + it->second.reg;
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
        if (tit != locals_.end() && slid_info_.count(tit->second.type)) {
            if (auto* ue = dynamic_cast<const UnaryExpr*>(assign->value.get())) {
                if (ue->op == "+" || ue->op == "-" || ue->op == "~" || ue->op == "!") {
                    const std::string& slid_name = tit->second.type;
                    std::string op_func = resolveSingleArgOverload(slid_name + "__op" + ue->op, *ue->operand);
                    if (!op_func.empty()) {
                        if (slid_info_.at(slid_name).has_dtor) {
                            emitDtorChainCall(slid_name, it->second.reg);
                            auto& info = slid_info_[slid_name];
                            for (int i = 0; i < (int)info.field_types.size(); i++) {
                                std::string ft = llvmType(info.field_types[i]);
                                std::string gep = newTmp();
                                out_ << "    " << gep << " = getelementptr %struct." << slid_name
                                     << ", ptr " << it->second.reg << ", i32 0, i32 " << i << "\n";
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
                            ? "ptr " + it->second.reg
                            : "ptr sret(%struct." + slid_name + ") " + it->second.reg;
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
        if (tit != locals_.end() && slid_info_.count(tit->second.type)) {
            if (auto* te = dynamic_cast<const TupleExpr*>(assign->value.get())) {
                const std::string& slid_name = tit->second.type;
                auto& info = slid_info_[slid_name];
                int nfields = (int)info.field_types.size();
                if ((int)te->values.size() > nfields)
                    errorWithNote(std::string("Tuple has " + std::to_string(te->values.size())
                        + " values but '" + slid_name + "' has " + std::to_string(nfields)
                        + " accessible fields"),
                        info.name_file_id, info.name_tok,
                        "'" + slid_name + "' declared here.");
                for (int i = 0; i < (int)te->values.size(); i++) {
                    const std::string& ft = info.field_types[i];
                    std::string elem_llvm = llvmType(ft);
                    std::string gep = emitFieldGep(slid_name, it->second.reg, i);
                    // slid-typed field: matching slid → dispatch op=/op<-;
                    // tuple-shape value → recurse via emitInitFieldsAtPtrs
                    // (same desugar as VarDecl). Single-value promotion falls
                    // out: the value becomes the field's first ctor arg.
                    if (slid_info_.count(ft)) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type == ft) {
                            std::string src_ptr;
                            if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                                src_ptr = locals_[ve->name].reg;
                            } else {
                                src_ptr = emitExpr(*te->values[i]);
                            }
                            emitSlidSlotAssign(ft, gep, src_ptr, assign->is_move);
                        } else if (auto* te2 = dynamic_cast<const TupleExpr*>(te->values[i].get())) {
                            std::vector<const Expr*> ov;
                            ov.reserve(te2->values.size());
                            for (auto& v : te2->values) ov.push_back(v.get());
                            emitInitFieldsAtPtrs(ft, gep, {}, ov);
                        } else {
                            // single-value promotion — feed as first ctor arg.
                            std::vector<const Expr*> one{ te->values[i].get() };
                            emitInitFieldsAtPtrs(ft, gep, one, {});
                        }
                        continue;
                    }
                    // anon-tuple-typed field: matching tuple-var → walker;
                    // tuple-shape value → unpack as overrides.
                    if (isAnonTupleType(ft)) {
                        std::string src_type = inferSlidType(*te->values[i]);
                        if (src_type == ft) {
                            std::string src_ptr;
                            if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                                src_ptr = locals_[ve->name].reg;
                            }
                            if (src_ptr.empty())
                                error(std::string("Tuple-typed field copy from non-variable source not supported"));
                            emitSlidSlotAssign(ft, gep, src_ptr, assign->is_move);
                        } else if (auto* te2 = dynamic_cast<const TupleExpr*>(te->values[i].get())) {
                            std::vector<const Expr*> ov;
                            ov.reserve(te2->values.size());
                            for (auto& v : te2->values) ov.push_back(v.get());
                            emitInitFieldsAtPtrs(ft, gep, {}, ov);
                        } else {
                            std::vector<const Expr*> one{ te->values[i].get() };
                            emitInitFieldsAtPtrs(ft, gep, one, {});
                        }
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
        if (tit != locals_.end() && slid_info_.count(tit->second.type)) {
            std::string op_name = assign->is_move ? "op<-" : "op=";
            std::string mangled = resolveSingleArgOverload(tit->second.type + "__" + op_name, *assign->value);
            if (!mangled.empty()) {
                auto& ptypes = func_param_types_[mangled];
                std::string param_type = ptypes.empty() ? "" : ptypes[0];
                std::string arg_val = emitArgForParam(*assign->value, param_type);
                std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                out_ << "    call void @" << llvmGlobalName(mangled)
                     << "(ptr " << it->second.reg << ", " << ptype_str << " " << arg_val << ")\n";
                // class <- pointer move: per spec, rhs pointer is set to nullptr.
                // gate on the rhs's actual type, not the param type — `op<-(Move^)`
                // takes a slid ref but the source `from` is a slid lvalue, not a pointer.
                if (assign->is_move && isIndirectType(inferSlidType(*assign->value)))
                    emitNullOut(*assign->value);
                return;
            }
            // no matching op= / op<- found: synthesize a default field-by-field copy
            {
                const std::string& slid_name = tit->second.type;
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(assign->value.get())) {
                    auto lit = locals_.find(ve->name);
                    if (lit != locals_.end()) {
                        if (lit->second.type == slid_name) {
                            src_ptr = lit->second.reg;
                        } else if (lit->second.type == slid_name + "^") {
                            std::string loaded = newTmp();
                            out_ << "    " << loaded << " = load ptr, ptr " << lit->second.reg << "\n";
                            src_ptr = loaded;
                        }
                    }
                }
                // any other expr whose type matches slid_name: emitExpr returns a slid ptr.
                if (src_ptr.empty() && inferSlidType(*assign->value) == slid_name)
                    src_ptr = emitExpr(*assign->value);
                if (!src_ptr.empty()) { emitSlidSlotAssign(slid_name, it->second.reg, src_ptr, assign->is_move); return; }
            }
            // single-value promotion: rhs becomes (rhs,) → first field gets
            // rhs, remaining fields untouched (assignment-form rule).
            {
                auto& info = slid_info_[tit->second.type];
                if (!info.field_types.empty()) {
                    const std::string& ft = info.field_types[0];
                    std::string gep = emitFieldGep(tit->second.type, it->second.reg, 0);
                    if (slid_info_.count(ft) || isAnonTupleType(ft)) {
                        if (auto* te2 = dynamic_cast<const TupleExpr*>(assign->value.get())) {
                            std::vector<const Expr*> ov;
                            ov.reserve(te2->values.size());
                            for (auto& v : te2->values) ov.push_back(v.get());
                            emitInitFieldsAtPtrs(ft, gep, {}, ov);
                        } else {
                            std::vector<const Expr*> one{ assign->value.get() };
                            emitInitFieldsAtPtrs(ft, gep, one, {});
                        }
                    } else {
                        // primitive slot — direct store.
                        std::string val = emitExpr(*assign->value);
                        out_ << "    store " << llvmType(ft) << " " << val
                             << ", ptr " << gep << "\n";
                    }
                    return;
                }
            }
        }
        // pointer move: copy source to dest, null source
        if (assign->is_move && tit != locals_.end() && isIndirectType(tit->second.type)) {
            requirePtrInit(tit->second.type, *assign->value);
            std::string src_val = emitExpr(*assign->value);
            out_ << "    store ptr " << src_val << ", ptr " << it->second.reg << "\n";
            emitNullOut(*assign->value);
            return;
        }
        if (tit != locals_.end())
            requirePtrInit(tit->second.type, *assign->value);
        std::string val = (tit != locals_.end())
            ? valOrNullptrCheck(tit->second.type, *assign->value)
            : emitExpr(*assign->value);
        bool is_ptr = tit != locals_.end() && isIndirectType(tit->second.type);
        std::string store_type = is_ptr ? "ptr" : llvmType(tit != locals_.end() ? tit->second.type : "int");
        // coerce integer widths if necessary (sext or trunc)
        if (!is_ptr && tit != locals_.end()) {
            std::string src_t = exprLlvmType(*assign->value);
            if (src_t == "ptr" && tit->second.type == "intptr") {
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
        out_ << "    store " << store_type << " " << val << ", ptr " << it->second.reg << "\n";
        return;
    }

    if (auto* cas = dynamic_cast<const CompoundAssignStmt*>(&stmt)) {
        // Single-eval LHS — resolveLvalue fires LHS side effects exactly once.
        // Step 1 below dispatches user op<op>= for slid LHS using `addr`;
        // Step 2 (new) handles primitive / pointer-iter LHS in-place;
        // Step 3 (existing desugar) is the fallback for slid LHS without
        // op<op>= (synthesizes op<op> + op= via Assign-family).
        // Parser whitelist (parser.cpp buildCompoundAssignFromLhs) restricts LHS
        // to VarExpr / DerefExpr / FieldAccessExpr / ArrayIndexExpr — all
        // resolveLvalue-able. Single-eval ensures LHS side effects fire once.
        const Expr* lhs = cas->lhs.get();
        auto lv = resolveLvalue(*lhs);
        std::string addr = lv.addr;
        std::string slids_type = lv.type;
        // Body-level const enforcement: the resolved lvalue's effective type
        // is the target of the write. Top-level const blocks the write.
        if (typeStartsWithConst(slids_type))
            errorAtNode(stmt, "Cannot modify const target of '" + cas->op + "='.");

        // Pointer LHS with pointer RHS: arithmetic compound-assigns are not
        // a pointer-producing operation. Catch all five (+ - * / %) here so
        // the compound family has a single focused diagnostic instead of
        // falling into the per-op BinaryExpr arms (which mix wordings and
        // miss '-=' entirely, since ptr - ptr is a valid intptr expression).
        if (isPtrType(slids_type)) {
            std::string rhs_type;
            if (auto* ve = dynamic_cast<const VarExpr*>(cas->rhs.get())) {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) rhs_type = tit->second.type;
            }
            if (isPtrType(rhs_type)) {
                static const std::set<std::string> bad = {"+","-","*","/","%"};
                if (bad.count(cas->op))
                    error(std::string("Operator '" + cas->op + "=' between two pointer operands "
                        "is not allowed (result is not a pointer)"));
            }
        }

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

        // Step 2: primitive / pointer-iter LHS — single-eval load/op/store
        // through the resolved address. LHS side effects fired exactly once
        // by resolveLvalue above; this block does NOT re-emit the LHS.
        // (Slid LHS without op<op>= falls through to Step 3 desugar.)
        if (!addr.empty() && !slid_info_.count(slids_type)
            && !isAnonTupleType(slids_type)) {
            const std::string& op = cas->op;
            bool is_logical = (op == "&&" || op == "||" || op == "^^");
            if (isRefType(slids_type)) {
                error(std::string("Operator '" + op + "=' on reference '" + slids_type
                    + "': arithmetic on references is not allowed (use a pointer '[]' type)"));
            }
            if (is_logical && (isPtrType(slids_type) || isIndirectType(slids_type))) {
                error(std::string("Operator '" + op + "=' on pointer '" + slids_type
                    + "': logical compound assign produces bool, cannot assign to pointer"));
            }
            if (is_logical) {
                // Short-circuit (&&, ||) or always-eval (^^) producing an i1,
                // then extend to the lvalue's width and store. Mirrors the
                // BinaryExpr path in codegen_expr.cpp:1255.
                std::string llt = llvmType(slids_type);
                std::string lhs_load = newTmp();
                out_ << "    " << lhs_load << " = load " << llt << ", ptr " << addr << "\n";
                std::string left_bool = emitToBool(lhs_load, llt);
                std::string res_ptr = newTmp() + "_sca";
                out_ << "    " << res_ptr << " = alloca i1\n";
                std::string eval_right = newLabel("sca_right");
                std::string done       = newLabel("sca_done");
                if (op == "&&") {
                    out_ << "    store i1 false, ptr " << res_ptr << "\n";
                    out_ << "    br i1 " << left_bool << ", label %" << eval_right
                         << ", label %" << done << "\n";
                } else if (op == "||") {
                    out_ << "    store i1 true, ptr " << res_ptr << "\n";
                    out_ << "    br i1 " << left_bool << ", label %" << done
                         << ", label %" << eval_right << "\n";
                } else { // ^^
                    out_ << "    store i1 false, ptr " << res_ptr << "\n";
                    out_ << "    br label %" << eval_right << "\n";
                }
                out_ << eval_right << ":\n";
                bool right_is_phrase = (op == "&&" || op == "||");
                if (right_is_phrase) {
                    pushPostIncQueue();
                    emitPrePass(*cas->rhs);
                }
                std::string right_val = emitExpr(*cas->rhs);
                std::string right_bool = emitToBool(right_val, exprLlvmType(*cas->rhs));
                if (right_is_phrase) flushPostIncQueue();
                std::string final_i1;
                if (op == "^^") {
                    final_i1 = newTmp();
                    out_ << "    " << final_i1 << " = xor i1 " << left_bool
                         << ", " << right_bool << "\n";
                } else {
                    final_i1 = right_bool;
                }
                out_ << "    store i1 " << final_i1 << ", ptr " << res_ptr << "\n";
                out_ << "    br label %" << done << "\n";
                out_ << done << ":\n";
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load i1, ptr " << res_ptr << "\n";
                std::string ext;
                if (llt == "float" || llt == "double") {
                    ext = newTmp();
                    out_ << "    " << ext << " = uitofp i1 " << loaded
                         << " to " << llt << "\n";
                } else if (llt == "i1") {
                    ext = loaded;
                } else {
                    ext = newTmp();
                    out_ << "    " << ext << " = zext i1 " << loaded
                         << " to " << llt << "\n";
                }
                out_ << "    store " << llt << " " << ext
                     << ", ptr " << addr << "\n";
                return;
            }
            if (isPtrType(slids_type)) {
                // pointer iter += int / -= int via GEP
                if (op != "+" && op != "-")
                    error(std::string("Operator '" + op + "=' on pointer type '"
                        + slids_type + "' is not allowed"));
                std::string rhs_slids;
                if (auto* ve = dynamic_cast<const VarExpr*>(cas->rhs.get())) {
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end()) rhs_slids = tit->second.type;
                }
                if (isRefType(rhs_slids))
                    error(std::string("Operator '" + op + "' between pointer '" + slids_type
                        + "' and reference '" + rhs_slids + "' is not allowed"));
                std::string elem = slids_type.substr(0, slids_type.size() - 2);
                std::string rhs_llt = exprLlvmType(*cas->rhs);
                std::string rhs_v = emitExpr(*cas->rhs);
                std::string idx = rhs_v;
                if (op == "-") {
                    std::string neg = newTmp();
                    out_ << "    " << neg << " = sub " << rhs_llt
                         << " 0, " << rhs_v << "\n";
                    idx = neg;
                }
                std::string old_v = newTmp();
                out_ << "    " << old_v << " = load ptr, ptr " << addr << "\n";
                std::string new_v = newTmp();
                out_ << "    " << new_v << " = getelementptr " << llvmType(elem)
                     << ", ptr " << old_v << ", " << rhs_llt << " " << idx << "\n";
                out_ << "    store ptr " << new_v << ", ptr " << addr << "\n";
                return;
            }
            // Primitive int / float: pick LLVM op, load-op-store.
            static const std::set<std::string> unsigned_types = {
                "uint","uint8","uint16","uint32","uint64","char"
            };
            bool is_unsigned = unsigned_types.count(slids_type) > 0;
            bool is_float = (slids_type == "float" || slids_type == "float32" || slids_type == "float64");
            std::string instr;
            if (is_float) {
                if (op == "+") instr = "fadd";
                else if (op == "-") instr = "fsub";
                else if (op == "*") instr = "fmul";
                else if (op == "/") instr = "fdiv";
            } else {
                if (op == "+") instr = "add";
                else if (op == "-") instr = "sub";
                else if (op == "*") instr = "mul";
                else if (op == "/") instr = is_unsigned ? "udiv" : "sdiv";
                else if (op == "%") instr = is_unsigned ? "urem" : "srem";
                else if (op == "&") instr = "and";
                else if (op == "|") instr = "or";
                else if (op == "^") instr = "xor";
                else if (op == "<<") instr = "shl";
                else if (op == ">>") instr = is_unsigned ? "lshr" : "ashr";
            }
            if (instr.empty())
                error(std::string("Operator '" + op + "=' is not supported for type '"
                    + slids_type + "'"));
            std::string llt = llvmType(slids_type);
            std::string old_v = newTmp();
            out_ << "    " << old_v << " = load " << llt << ", ptr " << addr << "\n";
            std::string rhs_v = emitExpr(*cas->rhs);
            std::string rhs_llt = exprLlvmType(*cas->rhs);
            // Coerce integer rhs to lhs width if needed.
            if (!is_float && rhs_llt != llt
                && !rhs_llt.empty() && rhs_llt[0] == 'i'
                && !llt.empty() && llt[0] == 'i') {
                int rw = std::stoi(rhs_llt.substr(1));
                int dw = std::stoi(llt.substr(1));
                if (rw < dw) {
                    std::string ext = newTmp();
                    out_ << "    " << ext << " = "
                         << (is_unsigned ? "zext " : "sext ")
                         << rhs_llt << " " << rhs_v << " to " << llt << "\n";
                    rhs_v = ext;
                } else if (rw > dw) {
                    std::string trc = newTmp();
                    out_ << "    " << trc << " = trunc "
                         << rhs_llt << " " << rhs_v << " to " << llt << "\n";
                    rhs_v = trc;
                }
            }
            std::string new_v = newTmp();
            out_ << "    " << new_v << " = " << instr
                 << " " << llt << " " << old_v << ", " << rhs_v << "\n";
            out_ << "    store " << llt << " " << new_v << ", ptr " << addr << "\n";
            return;
        }

        // Step 3a: slid LHS without op<op>= — emit inline op<op> dispatch
        // using the resolved `addr` for both the binary call's left input and
        // the op= destination. Single-eval LHS; no clone-desugar (which would
        // re-fire any post-inc/dec inside the LHS expression under PPID).
        if (!addr.empty() && slid_info_.count(slids_type)) {
            std::string op_base = slids_type + "__op" + cas->op;
            auto moit = method_overloads_.find(op_base);
            std::string op_func;
            std::string second_ptype;
            if (moit != method_overloads_.end()) {
                std::string want_first = canonicalType(slids_type + "^");
                for (auto& [m, ptypes, _pm, _pmt, _fid] : moit->second) {
                    if (ptypes.size() == 2 && canonicalType(ptypes[0]) == want_first) {
                        op_func = m;
                        second_ptype = ptypes[1];
                        break;
                    }
                }
            }
            if (!op_func.empty()) {
                std::string res_tmp = emitSlidAlloca(slids_type);
                std::string rhs_arg = emitArgForParam(*cas->rhs, second_ptype);
                std::string ret = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                bool is_method = (ret == "void");
                std::string args = is_method
                    ? ("ptr " + res_tmp)
                    : ("ptr sret(%struct." + slids_type + ") " + res_tmp);
                args += ", " + llvmType(slids_type + "^") + " " + addr;
                args += ", " + llvmType(second_ptype) + " " + rhs_arg;
                out_ << "    call void @" << llvmGlobalName(op_func)
                     << "(" << args << ")\n";
                emitSlidSlotAssign(slids_type, addr, res_tmp,
                                   /*is_move=*/false, /*is_init=*/false);
                return;
            }
            // No matching op<op> overload — fall through to Step 3 desugar
            // (which will produce its own diagnostic).
        }

        // Step 3: desugar to the matching specialized assign-family stmt.
        // Existing handlers cover primitives, pointer arithmetic (iter += int),
        // slid op<op> + op= for slid types without op<op>=, etc.
        // Sub-expressions evaluate twice; safe for idempotent shapes (VarExpr
        // bases, literal/var indices, etc.). Anon-tuple LHS still uses this
        // path; single-eval for tuple LHS is a separate TODO.
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
        error(std::string("Compound assignment: unsupported LHS shape"));
    }

    if (auto* da = dynamic_cast<const DerefAssignStmt*>(&stmt)) {
        // ptr^ = val / ptr^ <- val — store-through-pointer, with op=/op<- dispatch
        // for slid pointees.
        // Body-level const enforcement: a deref-write is forbidden if `const`
        // appears anywhere in the pointer's chain — both `const T^` (full)
        // and `(const T)^` (reference-to-const) have const at the pointee.
        if (typeHasConst(exprType(*da->ptr)))
            errorAtNode(stmt, "Cannot write through const pointer.");
        std::string ptr_reg;
        std::string pointee_llvm = "i32";
        std::string pointee_type;
        if (auto* ve = dynamic_cast<const VarExpr*>(da->ptr.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                error(std::string("Undefined variable '" + ve->name + "'"));
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end() && isIndirectType(tit->second.type)) {
                std::string loaded_ptr = newTmp();
                out_ << "    " << loaded_ptr << " = load ptr, ptr " << it->second.reg << "\n";
                ptr_reg = loaded_ptr;
                pointee_type = pointeeForLookup(tit->second.type);
                pointee_llvm = llvmType(pointee_type);
            } else {
                ptr_reg = it->second.reg;
            }
        } else {
            // generic operand (e.g., UnaryExpr post++): emitExpr returns the
            // pointer value (with side-effect emitted). Derive pointee type
            // from inferSlidType so the store width / op-dispatch are correct.
            ptr_reg = emitExpr(*da->ptr);
            std::string ot = inferSlidType(*da->ptr);
            if (isPtrType(ot) || isRefType(ot)) {
                pointee_type = isPtrType(ot)
                    ? ot.substr(0, ot.size() - 2)
                    : ot.substr(0, ot.size() - 1);
                pointee_llvm = llvmType(pointee_type);
            }
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
        // base[index] = value — chained-write driller. Walks the AIE chain on
        // ia->base, drills all-but-last indices to a (slot-parent, type), then
        // dispatches the final write step on type. Symmetric to the read-side
        // resolveLvalue AIE arm; multi-dim native array prefix is identical.
        // Body-level const enforcement: writing through an iterator-to-const
        // or const-iterator is forbidden — any `const` in the base's chain.
        if (typeHasConst(exprType(*ia->base)))
            errorAtNode(stmt, "Cannot write through const iterator or pointer.");

        // Final-step writer: stores rhs into a resolved (slot_addr, slot_type).
        // Slid / anon-tuple slots dispatch through emitSlidSlotAssign so user
        // op= / op<- and the field-walk fallback fire. Primitive slots width-
        // coerce; indirect slots null out the source on move.
        auto emitSlotWrite = [&](const std::string& slot_addr,
                                  const std::string& slot_type) {
            if (slid_info_.count(slot_type) || isAnonTupleType(slot_type)) {
                std::string src_ptr;
                if (auto* rve = dynamic_cast<const VarExpr*>(ia->value.get())) {
                    auto lit = locals_.find(rve->name);
                    if (lit != locals_.end()) src_ptr = lit->second.reg;
                }
                if (src_ptr.empty()) {
                    std::string v = emitExpr(*ia->value);
                    if (isAnonTupleType(slot_type)) {
                        // emitExpr returns a loaded struct SSA for tuple
                        // values — spill so the field walk has a ptr.
                        std::string tmp = newTmp();
                        out_ << "    " << tmp << " = alloca "
                             << llvmType(slot_type) << "\n";
                        out_ << "    store " << llvmType(slot_type) << " "
                             << v << ", ptr " << tmp << "\n";
                        src_ptr = tmp;
                    } else {
                        src_ptr = v;
                    }
                }
                emitSlidSlotAssign(slot_type, slot_addr, src_ptr, ia->is_move);
                return;
            }
            requirePtrInit(slot_type, *ia->value);
            std::string rhs_val = emitExpr(*ia->value);
            std::string elem_llvm = llvmType(slot_type);
            if (!isIndirectType(slot_type)) {
                std::string src_t = exprLlvmType(*ia->value);
                static const std::map<std::string,int> rank =
                    {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                auto sit = rank.find(src_t), dit = rank.find(elem_llvm);
                if (sit != rank.end() && dit != rank.end()
                        && sit->second != dit->second) {
                    std::string coerced = newTmp();
                    if (dit->second > sit->second)
                        out_ << "    " << coerced << " = sext " << src_t
                             << " " << rhs_val << " to " << elem_llvm << "\n";
                    else
                        out_ << "    " << coerced << " = trunc " << src_t
                             << " " << rhs_val << " to " << elem_llvm << "\n";
                    rhs_val = coerced;
                }
            }
            out_ << "    store " << elem_llvm << " " << rhs_val
                 << ", ptr " << slot_addr << "\n";
            if (ia->is_move && isIndirectType(slot_type))
                emitNullOut(*ia->value);
        };

        // Walk ia->base AIE chain, append ia->index as the final write index.
        std::vector<const Expr*> indices;
        const Expr* cur = ia->base.get();
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
            indices.insert(indices.begin(), a->index.get());
            cur = a->base.get();
        }
        indices.push_back(ia->index.get());

        // Seed (addr, type). Multi-dim native array prefix folds dims.size()
        // indices flat when the deepest base is a fixed-size array local
        // and indices supply at least that many.
        std::string addr, type;
        int consumed = 0;
        if (auto* ve = dynamic_cast<const VarExpr*>(cur)) {
            auto ait = array_info_.find(ve->name);
            if (ait != array_info_.end()
                    && (int)indices.size() >= (int)ait->second.dims.size()) {
                auto& ainfo = ait->second;
                int n = (int)ainfo.dims.size();
                // Slids reading — see read-side fold above for derivation.
                std::string flat = emitExpr(*indices[n - 1]);
                for (int k = n - 2; k >= 0; k--) {
                    int stride = ainfo.dims[k];
                    std::string mul = newTmp();
                    out_ << "    " << mul << " = mul i32 " << flat
                         << ", " << stride << "\n";
                    std::string iv = emitExpr(*indices[k]);
                    std::string add = newTmp();
                    out_ << "    " << add << " = add i32 " << mul
                         << ", " << iv << "\n";
                    flat = add;
                }
                int total = 1;
                for (int d : ainfo.dims) total *= d;
                std::string elt = llvmType(ainfo.elem_type);
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr [" << total
                     << " x " << elt << "], ptr " << ainfo.alloca_reg
                     << ", i32 0, i32 " << flat << "\n";
                addr = gep;
                type = ainfo.elem_type;
                consumed = n;
            }
        }
        if (addr.empty()) {
            auto base = resolveLvalue(*cur);
            addr = base.addr;
            type = base.type;
        }

        // Drill the middle indices so addr/type land at the parent of the
        // final write slot (or at the slot itself when the multi-dim prefix
        // already consumed everything).
        int last_pos = (int)indices.size() - 1;
        if (consumed < last_pos) {
            std::vector<const Expr*> mid(
                indices.begin() + consumed, indices.begin() + last_pos);
            auto lv = drillIndexChain(addr, type, mid, 0, *ia->index);
            addr = lv.addr;
            type = lv.type;
        }

        if (consumed == (int)indices.size()) {
            // Multi-dim prefix consumed every index — addr is the slot.
            emitSlotWrite(addr, type);
            return;
        }

        const Expr& last = *indices.back();
        if (isAnonTupleType(type)) {
            auto elems = anonTupleElems(type);
            int idx;
            if (!constExprToInt(last, enum_values_, idx))
                errorAtNode(*ia, "Tuple index must be a constant integer.");
            if (idx < 0 || idx >= (int)elems.size())
                errorAtNode(*ia, "Tuple index "
                    + std::to_string(idx) + " is out of range.");
            std::string slot = emitFieldGep(type, addr, idx);
            emitSlotWrite(slot, elems[idx]);
            return;
        }
        if (isInlineArrayType(type)) {
            auto lb = type.rfind('[');
            std::string elem_type = type.substr(0, lb);
            std::string sz_str = type.substr(lb + 1, type.size() - lb - 2);
            std::string elt = llvmType(elem_type);
            std::string idx_llvm = exprLlvmType(last);
            std::string iv = emitExpr(last);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << sz_str
                 << " x " << elt << "], ptr " << addr
                 << ", i32 0, " << idx_llvm << " " << iv << "\n";
            emitSlotWrite(gep, elem_type);
            return;
        }
        if (isRefType(type))
            errorAtNode(*ia, "Reference type '" + type
                + "' cannot be indexed; dereference it with '^' first.");
        if (isPtrType(type)) {
            std::string elem_type = type.substr(0, type.size() - 2);
            std::string base_ptr = newTmp();
            out_ << "    " << base_ptr << " = load ptr, ptr " << addr << "\n";
            std::string idx_llvm = exprLlvmType(last);
            std::string iv = emitExpr(last);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr " << llvmType(elem_type)
                 << ", ptr " << base_ptr << ", " << idx_llvm << " " << iv << "\n";
            emitSlotWrite(gep, elem_type);
            return;
        }
        // slid with op[] dispatch — op[] returns T^; the call result is the
        // write slot. Same shape as the read-side drillIndexChain arm.
        if (slid_info_.count(type)) {
            std::string mangled = resolveSlidIndex(type, last);
            if (mangled.empty())
                errorAtNode(*ia, "Type '" + type
                    + "' has no op[] for the given index.");
            auto& mptypes = func_param_types_[mangled];
            std::string idx_llvm = mptypes.empty() ? "i32" : llvmType(mptypes[0]);
            std::string idx_val = mptypes.empty()
                ? emitExpr(last)
                : emitArgForParam(last, mptypes[0]);
            std::string call_res = newTmp();
            out_ << "    " << call_res << " = call ptr @" << llvmGlobalName(mangled)
                 << "(ptr " << addr << ", " << idx_llvm
                 << " " << idx_val << ")\n";
            auto rit = func_return_types_.find(mangled);
            std::string ret_t = (rit != func_return_types_.end()) ? rit->second : "";
            emitSlotWrite(call_res, pointeeForLookup(ret_t));
            return;
        }
        errorAtNode(*ia, "Type '" + type + "' is not indexable for write.");
    }

    if (auto* sw = dynamic_cast<const SwapStmt*>(&stmt)) {
        // lhs <-> rhs — swap values at two lvalue locations.
        auto a = resolveLvalue(*sw->lhs);
        auto b = resolveLvalue(*sw->rhs);
        // Body-level const enforcement: swap writes BOTH sides. Reject if
        // either resolved type carries top-level const.
        if (typeStartsWithConst(a.type))
            errorAtNode(*sw->lhs, "Cannot swap into const lvalue.");
        if (typeStartsWithConst(b.type))
            errorAtNode(*sw->rhs, "Cannot swap into const lvalue.");
        if (a.type != b.type) {
            std::string msg = "Type mismatch — '" + a.type + "' vs '" + b.type + "'";
            // Best-effort note: when the lhs is a VarExpr naming a known local,
            // point at its declaration. (Field/deref shapes have no LocalInfo.)
            if (auto* lve = dynamic_cast<const VarExpr*>(sw->lhs.get())) {
                auto lit = locals_.find(lve->name);
                if (lit != locals_.end() && lit->second.tok)
                    errorAtNodeWithNote(*sw, msg,
                        lit->second.file_id, lit->second.tok,
                        "'" + lve->name + "' declared here.");
            }
            errorAtNode(*sw, msg);
        }
        const std::string& t = a.type;
        const std::string& a_ptr = a.addr;
        const std::string& b_ptr = b.addr;
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
            error(std::string("Swap of inline-array variables not yet supported"));
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
        // Body-level const enforcement: writing to a field of a const object
        // is forbidden — const propagates to every accessor. The object can
        // be reached as `obj.field` (obj's type starts with const) or as
        // `ptr^.field` (ptr's chain carries const → after deref, obj is const).
        {
            std::string obj_t = exprType(*fa->object);
            if (typeStartsWithConst(obj_t) || typeHasConst(obj_t))
                errorAtNode(stmt, "Cannot write to field of const object.");
        }
        // handle ptr^.field = val — object is a DerefExpr with VarExpr operand.
        // Non-VarExpr operands (e.g., DerefExpr(UnaryExpr(post++, ...)) — the
        // natural form of p++^.field) fall through to the generic fallback below.
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get());
            de && dynamic_cast<const VarExpr*>(de->operand.get())) {
            std::string ptr_val;
            std::string slid_name;
            auto* ve = dynamic_cast<const VarExpr*>(de->operand.get());
            {
                auto it = locals_.find(ve->name);
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end() && isIndirectType(tit->second.type)) {
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load ptr, ptr " << it->second.reg << "\n";
                    ptr_val = loaded;
                    slid_name = pointeeForLookup(tit->second.type);
                } else {
                    ptr_val = it->second.reg;
                    if (tit != locals_.end()) slid_name = tit->second.type;
                }
            }
            if (slid_name.empty() || !slid_info_.count(slid_name))
                error(std::string("Unknown slid type for field '" + fa->field + "'"));
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
            auto type_it = locals_.find(ve->name);
            if (type_it != locals_.end()) {
                slid_name = type_it->second.type;
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
        // generic fallback: object is any lvalue resolveLvalue can handle.
        // Subsumes the prior tuple[N].field = val branch and previously-erroring
        // shapes (chained o.i_.x_ = v, p++^.x_ = v, arr[i].x_ = v with slid array).
        {
            auto obj_lv = resolveLvalue(*fa->object);
            std::string stype = obj_lv.type;
            std::string addr = obj_lv.addr;
            if (isPtrType(stype) || isRefType(stype)) {
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << addr << "\n";
                addr = loaded;
                stype = isPtrType(stype) ? stype.substr(0, stype.size()-2)
                                         : stype.substr(0, stype.size()-1);
            }
            auto sit = slid_info_.find(stype);
            if (sit == slid_info_.end())
                error(std::string("FieldAssign: '" + stype
                    + "' is not a slid type for field '" + fa->field + "'"));
            auto fit = sit->second.field_index.find(fa->field);
            if (fit == sit->second.field_index.end())
                error(std::string("Unknown field '" + fa->field
                    + "' on '" + stype + "'"));
            const std::string& ft = sit->second.field_types[fit->second];
            requirePtrInit(ft, *fa->value);
            std::string ft_llvm = llvmType(ft);
            std::string field_gep = newTmp();
            out_ << "    " << field_gep << " = getelementptr %struct." << stype
                 << ", ptr " << addr << ", i32 0, i32 " << fit->second << "\n";
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << ft_llvm << " " << val << ", ptr " << field_gep << "\n";
            if (fa->is_move && isIndirectType(ft))
                emitNullOut(*fa->value);
            return;
        }
    }

    if (auto* mcs = dynamic_cast<const MethodCallStmt*>(&stmt)) {
        // TypeName.sizeof() as a statement — call __$sizeof, discard result
        if (mcs->method == "sizeof") {
            std::string slid_name;
            if (auto* ve = dynamic_cast<const VarExpr*>(mcs->object.get())) {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) slid_name = tit->second.type;
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
            // self.method() statement form — type from current_slid_, address
            // from self_ptr_ (remap-aware; see codegen.h doc).
            if (ve->name == "self" && !current_slid_.empty()) {
                slid_name = current_slid_;
                obj_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
            } else {
                auto type_it = locals_.find(ve->name);
                // self-field shorthand: bare `field.method()` inside a method
                // resolves as `self.field.method()` when the name isn't a local
                // but matches a field of the enclosing class.
                std::string self_field_addr;
                if (type_it == locals_.end()) {
                    if (!current_slid_.empty()) {
                        auto& info = slid_info_[current_slid_];
                        auto fit = info.field_index.find(ve->name);
                        if (fit != info.field_index.end()) {
                            slid_name = info.field_types[fit->second];
                            std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                            self_field_addr = newTmp();
                            out_ << "    " << self_field_addr
                                 << " = getelementptr %struct." << current_slid_
                                 << ", ptr " << self << ", i32 0, i32 "
                                 << fit->second << "\n";
                        }
                    }
                    if (slid_name.empty())
                        error(std::string("Unknown type for: " + ve->name));
                } else {
                    slid_name = type_it->second.type;
                }
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
                            error("Dtor on '" + ve->name + "' of pointer type '" + slid_name +
                                  "': use '" + ve->name + "^.~()' to dtor the pointed-to value");
                    }
                } else {
                    if (isIndirectType(slid_name))
                        error("Method call on '" + ve->name + "' of pointer type '" + slid_name +
                              "': use '" + ve->name + "^." + mcs->method + "()' for explicit dereference");
                    if (!slid_info_.count(slid_name))
                        error("Method call on '" + ve->name + "': '" + slid_name + "' is not a slid type");
                }
                obj_ptr = self_field_addr.empty()
                    ? locals_[ve->name].reg
                    : self_field_addr;
            }
        } else if (dynamic_cast<const ArrayIndexExpr*>(mcs->object.get())
                || dynamic_cast<const FieldAccessExpr*>(mcs->object.get())) {
            // Chained lvalue receiver: AIE chain, FieldAccess, or composition.
            // resolveLvalue's AIE arm drills every chain shape (anon-tuple slot,
            // slid array, slid op[]→slid, FieldAccess→...) into a single
            // (addr, slid type), which is exactly what method dispatch needs.
            auto lv = resolveLvalue(*mcs->object);
            if (slid_info_.count(lv.type)) {
                slid_name = lv.type;
                obj_ptr = lv.addr;
            }
            // else: receiver isn't a slid — fall through to the unsupported error.
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mcs->object.get())) {
            // walk an arbitrary chain of DerefExpr down to a VarExpr (handles
            // `p^.m()`, `pp^^.m()`, etc.). emit one load per deref level and
            // strip one pointer suffix per level off the static type.
            int deref_count = 1;
            const Expr* inner = de->operand.get();
            while (auto* d2 = dynamic_cast<const DerefExpr*>(inner)) {
                deref_count++;
                inner = d2->operand.get();
            }
            if (auto* ve2 = dynamic_cast<const VarExpr*>(inner)) {
                auto type_it = locals_.find(ve2->name);
                if (type_it == locals_.end())
                    error(std::string("Unknown type for: " + ve2->name));
                slid_name = type_it->second.type;
                for (int k = 0; k < deref_count; k++) {
                    if (isRefType(slid_name)) slid_name.pop_back();
                    else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                    else error(std::string("Method call: not enough pointer levels in '"
                                           + type_it->second.type + "' for "
                                           + std::to_string(deref_count) + "-deep deref"));
                }
                // Strip const for slid_info_ lookup; checkConstReceiver
                // consults receiver const-ness via exprType separately.
                slid_name = stripRedundantConstParens(slid_name);
                if (typeStartsWithConst(slid_name)) slid_name = slid_name.substr(6);
                std::string cur = locals_[ve2->name].reg;
                for (int k = 0; k < deref_count; k++) {
                    std::string nxt = newTmp();
                    out_ << "    " << nxt << " = load ptr, ptr " << cur << "\n";
                    cur = nxt;
                }
                obj_ptr = cur;
            }
        }
        if (!slid_name.empty() && mcs->method == "~") {
            emitExplicitDtor(slid_name, obj_ptr);
            return;
        }
        if (!slid_name.empty()) {
            {
                // Method-mark check: walk the static type's chain for a `= delete`
                // mark. Marks key on exact param-type lists; we use the arity here
                // because by-name+arity is unambiguous in current slids (no name
                // overloading by-arity-only ambiguous case; full overloads are
                // distinguished by param types and the mark records those exactly).
                for (auto* cur = &slid_info_[slid_name]; cur; cur = cur->base_info) {
                    bool stop = false;
                    for (auto& mk : cur->method_marks) {
                        if (mk.method_name != mcs->method) continue;
                        if (mk.param_types.size() != mcs->args.size()) continue;
                        if (mk.is_delete) {
                            errorWithNote("Class '" + slid_name + "': call to deleted method '"
                                  + mcs->method + "()' (deleted in '" + cur->name + "')",
                                  mk.file_id, mk.tok, "Marked = delete here.");
                        }
                        stop = true; break;
                    }
                    if (stop) break;
                }
            }
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
                // Walk the inheritance chain to find the class that registers
                // the method. Handles plain non-virtual inheritance and the
                // `= default` redirect (the receiver class has no body, the
                // ancestor does).
                std::string dispatch_class = slid_name;
                for (auto* cur = &slid_info_[slid_name]; cur; cur = cur->base_info) {
                    if (method_overloads_.count(cur->name + "__" + mcs->method)) {
                        dispatch_class = cur->name;
                        break;
                    }
                }
                std::string base = dispatch_class + "__" + mcs->method;
                mangled = resolveOverloadForCall(base, mcs->args);
                auto ret_it = func_return_types_.find(mangled);
                if (ret_it == func_return_types_.end())
                    error(std::string("Unknown method: " + mcs->method));
                ret_slids = ret_it->second;
                mptypes = func_param_types_[mangled];
            }
            checkConstReceiver(*mcs->object, mcs->method, mangled);
            std::string ret_type = llvmType(ret_slids);
            bool empty = sinfo.is_empty;
            std::string arg_str = empty ? "" : "ptr " + obj_ptr;
            for (int i = 0; i < (int)mcs->args.size(); i++) {
                std::string ptype_str = (i < (int)mptypes.size()) ? mptypes[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                std::string aval = emitPhraseArg(*mcs->args[i], ptype_str);
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
        error(std::string("Complex method call statement not yet supported"));
    }

    if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (current_func_uses_sret_ && ret->value) {
            // sret return: move return value into %retval, dtor locals, ret void
            auto* ve = dynamic_cast<const VarExpr*>(ret->value.get());
            std::string slid_name;
            std::string src;
            bool src_is_fresh_temp = false;  // true if src is a new alloca not in dtor_vars_
            if (ve) {
                auto tit = locals_.find(ve->name);
                if (tit == locals_.end() || !slid_info_.count(tit->second.type))
                    error(std::string("Sret: return value must be a slid type"));
                slid_name = tit->second.type;
                src = locals_.at(ve->name).reg;
            } else if (auto* de = dynamic_cast<const DerefExpr*>(ret->value.get())) {
                // After the cloner-rewrite, `return b;` for an auto-promoted T^
                // local b becomes `return b^;` — DerefExpr value. The "slid
                // value" lives at the address held by b, which is what we
                // need to pass as the sret source.
                if (auto* dve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto tit = locals_.find(dve->name);
                    if (tit != locals_.end() && isIndirectType(tit->second.type)) {
                        std::string pointee = isPtrType(tit->second.type)
                            ? tit->second.type.substr(0, tit->second.type.size()-2)
                            : tit->second.type.substr(0, tit->second.type.size()-1);
                        if (slid_info_.count(pointee)) {
                            slid_name = pointee;
                            src = newTmp();
                            out_ << "    " << src << " = load ptr, ptr "
                                 << locals_.at(dve->name).reg << "\n";
                        }
                    }
                }
                if (slid_name.empty()) {
                    slid_name = exprSlidType(*ret->value);
                    if (slid_name.empty())
                        error(std::string("Sret: return expression must produce a slid type"));
                    src = emitExpr(*ret->value);
                    src_is_fresh_temp = true;
                }
            } else {
                // expression: emit it (BinaryExpr/CallExpr produce a fresh alloca)
                slid_name = exprSlidType(*ret->value);
                if (slid_name.empty())
                    error(std::string("Sret: return expression must produce a slid type"));
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
                    for (auto& [m, ptypes, _pm, _pmt, _fid] : mit->second)
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
                            // Tuple literal slot value → desugar into the slot,
                            // avoiding a temp alloca. Other shapes need a ptr
                            // for emitSlidSlotAssign — VarExpr provides it,
                            // calls/etc. spill to an alloca.
                            if (auto* te2 = dynamic_cast<const TupleExpr*>(te->values[i].get())) {
                                std::vector<const Expr*> ov;
                                ov.reserve(te2->values.size());
                                for (auto& v : te2->values) ov.push_back(v.get());
                                emitInitFieldsAtPtrs(elem_type, gep, {}, ov);
                                continue;
                            }
                            std::string src_ptr;
                            if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                                src_ptr = locals_[ve->name].reg;
                            } else if (slid_info_.count(elem_type)) {
                                // emitExpr returns a slid ptr for slid-typed
                                // expressions (CallExpr, BinaryExpr, etc.).
                                src_ptr = emitExpr(*te->values[i]);
                            } else {
                                // anon-tuple or other compound: emitExpr
                                // returns a struct value — spill to alloca.
                                std::string val = emitExpr(*te->values[i]);
                                src_ptr = newTmp();
                                out_ << "    " << src_ptr << " = alloca "
                                     << llvmType(elem_type) << "\n";
                                out_ << "    store " << llvmType(elem_type)
                                     << " " << val << ", ptr " << src_ptr << "\n";
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
                            src_ptr = locals_[ve->name].reg;
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
                        std::string val = valOrNullptrCheck(current_func_slids_return_type_, *ret->value);
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
                if (loop_stack_[i].is_hidden) continue;
                if (loop_stack_[i].block_label == brk->label) {
                    target = loop_stack_[i].break_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("Break: unknown label '" + brk->label + "'"));
        } else if (brk->number > 0) {
            // numbered break: count outward N loop frames, skipping switch frames
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].is_switch) continue;
                if (loop_stack_[i].is_hidden) continue;
                count++;
                if (count == brk->number) {
                    target = loop_stack_[i].break_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("Break " + std::to_string(brk->number) + ": not enough enclosing loops"));
        } else {
            if (break_label_.empty()) error(std::string("Break outside of loop"));
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
                if (loop_stack_[i].is_hidden) continue;
                if (loop_stack_[i].block_label == cont->label) {
                    target = loop_stack_[i].continue_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("Continue: unknown label '" + cont->label + "'"));
        } else if (cont->number > 0) {
            // numbered continue: count outward N loop frames, skipping switch frames
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].is_switch) continue;
                if (loop_stack_[i].is_hidden) continue;
                count++;
                if (count == cont->number) {
                    target = loop_stack_[i].continue_target;
                    target_frame = i;
                    break;
                }
            }
            if (target.empty())
                error(std::string("Continue " + std::to_string(cont->number) + ": not enough enclosing loops"));
        } else {
            if (continue_label_.empty()) error(std::string("Continue outside of loop"));
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
        // PPID: cond is its own phrase — pre fires at entry, post fires at `)`.
        pushPostIncQueue();
        emitPrePass(*if_stmt->cond);
        std::string cond_bool = emitCondBool(*if_stmt->cond);
        flushPostIncQueue();
        out_ << "    br i1 " << cond_bool << ", label %" << then_lbl
             << ", label %" << (if_stmt->else_block ? else_lbl : end_lbl) << "\n";
        block_terminated_ = false;
        out_ << then_lbl << ":\n";
        pushScope();
        emitBlock(*if_stmt->then_block);
        popScope();
        if (!block_terminated_) out_ << "    br label %" << end_lbl << "\n";
        if (if_stmt->else_block) {
            block_terminated_ = false;
            out_ << else_lbl << ":\n";
            pushScope();
            emitBlock(*if_stmt->else_block);
            popScope();
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
              pushScope();
              emitBlock(*w->body);
              popScope();
              if (!block_terminated_) {
                  out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
                  out_ << "    br label %" << cond_lbl << "\n";
              }
            }
            block_terminated_ = false;
            out_ << cond_lbl << ":\n";
            // PPID: while cond is its own phrase.
            pushPostIncQueue();
            emitPrePass(*w->cond);
            std::string cond_bool = emitCondBool(*w->cond);
            flushPostIncQueue();
            out_ << "    br i1 " << cond_bool << ", label %" << body_lbl << ", label %" << end_lbl << "\n";
        } else {
            out_ << "    br label %" << cond_lbl << "\n";
            block_terminated_ = false;
            out_ << cond_lbl << ":\n";
            // PPID: while cond is its own phrase.
            pushPostIncQueue();
            emitPrePass(*w->cond);
            std::string cond_bool = emitCondBool(*w->cond);
            flushPostIncQueue();
            out_ << "    br i1 " << cond_bool << ", label %" << body_lbl << ", label %" << end_lbl << "\n";
            block_terminated_ = false;
            out_ << body_lbl << ":\n";
            { std::string sp = newTmp();
              out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
              loop_stack_.back().stack_ptr_reg = sp;
              pushScope();
              emitBlock(*w->body);
              popScope();
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

    if (auto* fl = dynamic_cast<const ForLongStmt*>(&stmt)) {
        std::string init_lbl   = newLabel("for_init");
        std::string cond_lbl   = newLabel("for_cond");
        std::string body_lbl   = newLabel("for_body");
        std::string update_lbl = newLabel("for_update");
        std::string end_lbl    = newLabel("for_end");

        // for-scope: pushScope so init-tuple decls and any shadowing are
        // unwound at end_lbl; popScope at the end runs init-scope dtors.
        pushScope();

        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl; continue_label_ = update_lbl;
        loop_stack_.push_back({fl->block_label, end_lbl, update_lbl, "", false, false});

        out_ << "    br label %" << init_lbl << "\n";
        block_terminated_ = false;
        out_ << init_lbl << ":\n";
        // PPID: each comma-separated init element is its own phrase.
        for (auto& s : fl->init_stmts) {
            pushPostIncQueue();
            emitPrePass(*s);
            emitStmt(*s);
            flushPostIncQueue();
        }
        out_ << "    br label %" << cond_lbl << "\n";

        block_terminated_ = false;
        out_ << cond_lbl << ":\n";
        if (fl->cond) {
            // PPID: long-for cond is its own phrase.
            pushPostIncQueue();
            emitPrePass(*fl->cond);
            std::string c = emitCondBool(*fl->cond);
            flushPostIncQueue();
            out_ << "    br i1 " << c << ", label %" << body_lbl
                 << ", label %" << end_lbl << "\n";
        } else {
            out_ << "    br label %" << body_lbl << "\n";
        }

        block_terminated_ = false;
        out_ << body_lbl << ":\n";
        {
            std::string sp = newTmp();
            out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
            loop_stack_.back().stack_ptr_reg = sp;
            // emitStmt on BlockStmt snapshots its own dtor/locals frame so
            // body-local decls don't leak into update.
            emitStmt(*fl->body);
            if (!block_terminated_) {
                out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
                out_ << "    br label %" << update_lbl << "\n";
            }
        }

        block_terminated_ = false;
        out_ << update_lbl << ":\n";
        {
            // Hide this loop frame from break/continue resolution. Naked
            // break/continue in update is also blocked by clearing the labels.
            // Per spec, break/continue is not allowed in the update block.
            loop_stack_.back().is_hidden = true;
            std::string saved_break_in = break_label_;
            std::string saved_continue_in = continue_label_;
            break_label_ = ""; continue_label_ = "";

            std::string sp = newTmp();
            out_ << "    " << sp << " = call ptr @llvm.stacksave()\n";
            loop_stack_.back().stack_ptr_reg = sp;
            emitStmt(*fl->update_block);
            if (!block_terminated_) {
                out_ << "    call void @llvm.stackrestore(ptr " << sp << ")\n";
                out_ << "    br label %" << cond_lbl << "\n";
            }

            break_label_ = saved_break_in;
            continue_label_ = saved_continue_in;
            loop_stack_.back().is_hidden = false;
        }

        block_terminated_ = false;
        out_ << end_lbl << ":\n";

        popScope();

        loop_stack_.pop_back();
        break_label_ = saved_break;
        continue_label_ = saved_continue;
        return;
    }


    if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
        std::string end_lbl = newLabel("sw_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl;
        continue_label_ = "";
        loop_stack_.push_back({sw->block_label, end_lbl, "", "", true});

        // PPID: switch scrutinee is its own phrase.
        pushPostIncQueue();
        emitPrePass(*sw->expr);
        std::string disc = emitExpr(*sw->expr);
        flushPostIncQueue();

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
                error(std::string("Switch case value must be a constant integer or enum"));
            out_ << "        i32 " << val << ", label %" << case_lbls[i] << "\n";
        }
        out_ << "    ]\n";
        block_terminated_ = true;

        // emit each case body — each case is its own scope so slid locals
        // declared in `case N:` get dtor'd at case-end (and at break).
        // Fallthrough emits a br to the next case label after popScope.
        for (int i = 0; i < (int)sw->cases.size(); i++) {
            block_terminated_ = false;
            out_ << case_lbls[i] << ":\n";
            pushScope();
            for (auto& s : sw->cases[i].stmts)
                emitStmt(*s);
            popScope();
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

            // Flatten a left-leaning "+" chain into segments. Then walk
            // segments in source order: pre-evaluate every non-literal segment
            // (firing side effects up front, before any output), build a single
            // composite printf format string by appending each segment's piece,
            // and end with one `printf(fmt, varargs...)`. The whole line is
            // staged in printf's internal buffer and emitted at once — the
            // earlier one-printf-per-segment shape let function-call segments
            // interleave their output between halves of the formatted line.
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

            // Literal-segment text is baked into the composite fmt, so any '%'
            // must be escaped to '%%'.
            auto escapePct = [](const std::string& s) {
                std::string out;
                out.reserve(s.size());
                for (char c : s) {
                    out += c;
                    if (c == '%') out += '%';
                }
                return out;
            };

            std::string fmt;
            struct Arg { std::string type; std::string val; };
            std::vector<Arg> args;

            for (auto* seg : segments) {
                if (auto* s = dynamic_cast<const StringLiteralExpr*>(seg)) {
                    fmt += escapePct(s->value);
                    continue;
                }
                if (auto* sl = dynamic_cast<const SliceExpr*>(seg)) {
                    // s[a..b] → %.*s with (i32 len, ptr) varargs.
                    std::string base_ptr = emitExpr(*sl->base);
                    std::string start_val = emitExpr(*sl->start);
                    std::string end_val   = emitExpr(*sl->end);
                    std::string start_type = exprLlvmType(*sl->start);
                    std::string end_type   = exprLlvmType(*sl->end);
                    bool is64 = (start_type == "i64" || end_type == "i64");
                    std::string idx_type = is64 ? "i64" : "i32";
                    auto widen = [&](const std::string& v, const std::string& from) {
                        if (!is64 || from == "i64") return v;
                        std::string w = newTmp();
                        out_ << "    " << w << " = sext " << from << " " << v << " to i64\n";
                        return w;
                    };
                    std::string sv = widen(start_val, start_type);
                    std::string ev = widen(end_val, end_type);
                    std::string sliced = newTmp();
                    out_ << "    " << sliced << " = getelementptr i8, ptr "
                         << base_ptr << ", " << idx_type << " " << sv << "\n";
                    std::string len = newTmp();
                    out_ << "    " << len << " = sub " << idx_type << " "
                         << ev << ", " << sv << "\n";
                    std::string len32 = len;
                    if (is64) {
                        len32 = newTmp();
                        out_ << "    " << len32 << " = trunc i64 " << len
                             << " to i32\n";
                    }
                    fmt += "%.*s";
                    args.push_back({"i32", len32});
                    args.push_back({"ptr", sliced});
                    continue;
                }
                // VarExpr referring to a fixed-size char array or char[] local
                // — print as %s with the storage address.
                if (auto* ve = dynamic_cast<const VarExpr*>(seg)) {
                    auto ait = array_info_.find(ve->name);
                    if (ait != array_info_.end() && ait->second.elem_type == "char") {
                        int total = 1;
                        for (int d : ait->second.dims) total *= d;
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr [" << total
                             << " x i8], ptr " << ait->second.alloca_reg
                             << ", i32 0, i32 0\n";
                        fmt += "%s";
                        args.push_back({"ptr", gep});
                        continue;
                    }
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end() && tit->second.type == "char[]") {
                        std::string ptr_val = newTmp();
                        out_ << "    " << ptr_val << " = load ptr, ptr "
                             << tit->second.reg << "\n";
                        fmt += "%s";
                        args.push_back({"ptr", ptr_val});
                        continue;
                    }
                }
                // General expression segment.
                std::string val = emitExpr(*seg);
                std::string val_type = exprLlvmType(*seg);
                std::string s_type = exprType(*seg);
                if (val_type == "float" || val_type == "double") {
                    if (val_type == "float") {
                        std::string ext = newTmp();
                        out_ << "    " << ext << " = fpext float " << val
                             << " to double\n";
                        val = ext;
                    }
                    fmt += "%g";
                    args.push_back({"double", val});
                    continue;
                }
                if (val_type == "ptr") {
                    fmt += "%s";
                    args.push_back({"ptr", val});
                    continue;
                }
                // Integer family. Sub-i32 zext to i32; i64 stays i64.
                if (val_type != "i32" && val_type != "i64") {
                    std::string ext = newTmp();
                    out_ << "    " << ext << " = zext " << val_type << " "
                         << val << " to i32\n";
                    val = ext;
                    val_type = "i32";
                }
                bool is_uint = false;
                if (auto* ve = dynamic_cast<const VarExpr*>(seg)) {
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end() && tit->second.type == "uint") is_uint = true;
                    if (!is_uint && !current_slid_.empty()) {
                        auto& info = slid_info_[current_slid_];
                        auto fit = info.field_index.find(ve->name);
                        if (fit != info.field_index.end()
                                && info.field_types[fit->second] == "uint")
                            is_uint = true;
                    }
                }
                // char segments format as %c (the i8 value extends to int for
                // varargs; printf prints the character). Previously printed
                // as %d, surfacing as `(d,b,c,a)` → `(100,98,99,97)`.
                if (s_type == "char")            fmt += "%c";
                else if (val_type == "i64")      fmt += "%lld";
                else if (is_uint)                fmt += "%u";
                else                             fmt += "%d";
                args.push_back({val_type, val});
            }

            if (newline) fmt += "\n";

            std::string label = registerStringConstant(fmt);
            int fmt_len; llvmEscape(fmt, fmt_len);
            std::string fmt_ptr = newTmp();
            out_ << "    " << fmt_ptr << " = getelementptr [" << fmt_len
                 << " x i8], ptr " << label << ", i32 0, i32 0\n";
            std::string line = "    call i32 (ptr, ...) @printf(ptr " + fmt_ptr;
            for (auto& a : args) line += ", " + a.type + " " + a.val;
            line += ")";
            out_ << line << "\n";
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
                    arg_str += llvmType(ptype) + " " + emitPhraseArg(*call->args[i], ptype);
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
        if (std::string mangled = nestedCallMangled(call->callee, call->args);
                !mangled.empty()) {
            auto& info = nested_info_[mangled];
            std::string ret_type = llvmType(func_return_types_[mangled]);

            std::string arg_str;
            if (info.captures.size() == 1) {
                std::string cap = *info.captures.begin();
                arg_str = "ptr " + locals_[cap].reg;
            } else if (info.captures.size() >= 2) {
                // build frame struct on stack and fill in ptrs
                std::string frame = newTmp() + "_frame";
                out_ << "    " << frame << " = alloca %frame." << info.mangled_name << "\n";
                std::vector<std::string> ordered_caps(info.captures.begin(), info.captures.end());
                for (int i = 0; i < (int)ordered_caps.size(); i++) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %frame." << info.mangled_name
                         << ", ptr " << frame << ", i32 0, i32 " << i << "\n";
                    out_ << "    store ptr " << locals_[ordered_caps[i]].reg << ", ptr " << gep << "\n";
                }
                arg_str = "ptr " + frame;
            }

            auto& nptypes_cs = func_param_types_[mangled];
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (!arg_str.empty()) arg_str += ", ";
                std::string ptype_str = (i < (int)nptypes_cs.size()) ? nptypes_cs[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                arg_str += ptype + " " + emitPhraseArg(*call->args[i], ptype_str);
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
                    arg_str += ptype + " " + emitPhraseArg(*call->args[i], ptype_str);
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
            checkResolvedFreeFunction(call->callee, resolved_callee, call->args);
            std::string ret_type = llvmType(fit->second);
            auto& rptypes = func_param_types_[resolved_callee];
            std::string arg_str;
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (i > 0) arg_str += ", ";
                std::string ptype_str = (i < (int)rptypes.size()) ? rptypes[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                arg_str += ptype + " " + emitPhraseArg(*call->args[i], ptype_str);
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

        error(std::string("Unknown function: " + call->callee));
    }

    if (auto* td = dynamic_cast<const TupleDestructureStmt*>(&stmt)) {
        emitDestructure(td->fields, *td->init);
        return;
    }

    if (auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
        pushScope();
        emitBlock(*block);
        popScope();
        return;
    }

    error(std::string("Unsupported statement type"));
}
