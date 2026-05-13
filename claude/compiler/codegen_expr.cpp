#include "codegen.h"
#include "source_map.h"
#include "codegen_helpers.h"
#include <sstream>
#include <functional>
#include <stdexcept>

std::string Codegen::emitExpr(const Expr& expr) {
    EmitGuard _g(*this, expr.file_id, expr.tok);
    if (auto* i = dynamic_cast<const IntLiteralExpr*>(&expr))
        return std::to_string(i->value);

    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
        // self as a value — return its current address (self_ptr_; remap-aware).
        // Empty/namespace classes have no self; reject the read explicitly.
        if (v->name == "self" && !current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.is_empty)
                error(std::string((info.is_namespace ? "namespace '" : "empty class '")
                    + current_slid_ + "' has no self"));
            return self_ptr_.empty() ? "%self" : self_ptr_;
        }
        // substitution const lookup — block stack → enclosing slid → global.
        if (auto* ce = lookupConst(v->name)) return emitConstValue(*ce);
        // check if it's a field access via self in a method
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.field_index.count(v->name)) {
                int idx = info.field_index[v->name];
                std::string field_type = llvmType(info.field_types[idx]);
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                     << ", ptr %self, i32 0, i32 " << idx << "\n";
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = load " << field_type << ", ptr " << gep << "\n";
                return tmp;
            }
        }
        auto it = locals_.find(v->name);
        if (it == locals_.end()) {
            // check if it's an enum value
            auto eit = enum_values_.find(v->name);
            if (eit != enum_values_.end())
                return std::to_string(eit->second);
            // check if it's an array — return ptr to first element
            auto ait = array_info_.find(v->name);
            if (ait != array_info_.end()) {
                int total = 1;
                for (int d : ait->second.dims) total *= d;
                std::string elt = llvmType(ait->second.elem_type);
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr [" << total << " x " << elt << "], ptr "
                     << ait->second.alloca_reg << ", i32 0, i32 0\n";
                return gep;
            }
            // Phase 1: type name used as anonymous temporary — alloca, init, ctor
            if (slid_info_.count(v->name))
                return emitSlidAlloca(v->name);
            error(std::string("Undefined variable: " + v->name));
        }
        std::string tmp = newTmp();
        auto tit = locals_.find(v->name);
        std::string load_type = (tit != locals_.end()) ? llvmType(tit->second.type) : "i32";
        out_ << "    " << tmp << " = load " << load_type << ", ptr " << it->second.reg << "\n";
        return tmp;
    }

    if (dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        // Walk to the deepest non-AIE base. If that base isn't an lvalue
        // shape (e.g. function call returning a tuple), spill its rvalue to
        // a temp alloca and drill from there. Otherwise let resolveLvalue
        // handle the whole chain — its AIE arm walks the chain and reclassifies
        // each layer (tuple slot → slid op[] → array → ptr).
        const Expr* cur = &expr;
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) cur = a->base.get();
        bool lvalue_base = dynamic_cast<const VarExpr*>(cur)
            || dynamic_cast<const FieldAccessExpr*>(cur)
            || dynamic_cast<const DerefExpr*>(cur)
            || dynamic_cast<const UnaryExpr*>(cur);
        if (!lvalue_base) {
            std::vector<const Expr*> indices;
            const Expr* c = &expr;
            while (auto* a = dynamic_cast<const ArrayIndexExpr*>(c)) {
                indices.insert(indices.begin(), a->index.get());
                c = a->base.get();
            }
            std::string base_type = inferSlidType(*cur);
            std::string val = emitExpr(*cur);
            std::string slot = newTmp();
            out_ << "    " << slot << " = alloca " << llvmType(base_type) << "\n";
            out_ << "    store " << llvmType(base_type) << " " << val
                 << ", ptr " << slot << "\n";
            auto lv = drillIndexChain(slot, base_type, indices, 0, expr);
            if (slid_info_.count(lv.type)) return lv.addr;
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = load " << llvmType(lv.type)
                 << ", ptr " << lv.addr << "\n";
            return tmp;
        }
        auto lv = resolveLvalue(expr);
        if (slid_info_.count(lv.type)) return lv.addr;
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = load " << llvmType(lv.type)
             << ", ptr " << lv.addr << "\n";
        return tmp;
    }

    if (auto* ao = dynamic_cast<const AddrOfExpr*>(&expr)) {
        // ^(p^) — cancel: address of the pointed-to value IS the inner ptr.
        // Used by the cloner-rewrite for ^b on auto-promoted ref locals.
        if (auto* de = dynamic_cast<const DerefExpr*>(ao->operand.get())) {
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto it = locals_.find(ve->name);
                if (it != locals_.end() && isIndirectType(it->second.type)) {
                    std::string loaded = newTmp();
                    out_ << "    " << loaded << " = load ptr, ptr "
                         << it->second.reg << "\n";
                    return loaded;
                }
            }
        }
        // ^x — return the alloca register (its address)
        if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
            // ^self — special case for two reasons:
            //   1) Validation: empty/namespace classes have no self to address.
            //   2) Address: read self_ptr_, not locals_["self"].reg, because
            //      self_ptr_ may be remapped during inline ctor body emission
            //      (new T[n]) where each per-iteration alloca temporarily
            //      becomes self.
            if (ve->name == "self") {
                if (!current_slid_.empty()) {
                    auto& info = slid_info_[current_slid_];
                    if (info.is_empty)
                        error(std::string((info.is_namespace ? "namespace '" : "empty class '")
                            + current_slid_ + "' has no self"));
                }
                return self_ptr_.empty() ? "%self" : self_ptr_;
            }
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                error(std::string("Undefined variable '" + ve->name + "'"));
            return it->second.reg;
        }
        // ^arr[i][j] — compute GEP but skip the final load, returning the element ptr
        if (dynamic_cast<const ArrayIndexExpr*>(ao->operand.get())) {
            std::vector<const Expr*> indices;
            const Expr* cur = ao->operand.get();
            while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
                indices.insert(indices.begin(), a->index.get());
                cur = a->base.get();
            }
            auto* ve = dynamic_cast<const VarExpr*>(cur);
            if (!ve) error(std::string("Complex array base not supported"));
            // anon-tuple base: GEP into the tuple's slot with constant indices.
            // Variable indices allowed only when the tuple is homogeneous.
            {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end() && isAnonTupleType(tit->second.type)) {
                    std::string cur_type = tit->second.type;
                    std::string cur_ptr = locals_[ve->name].reg;
                    for (int level = 0; level < (int)indices.size(); level++) {
                        if (!isAnonTupleType(cur_type))
                            error(std::string("Tuple walk hit non-tuple at level "
                                + std::to_string(level)));
                        auto elems = anonTupleElems(cur_type);
                        int idx;
                        bool is_const = constExprToInt(*indices[level], enum_values_, idx);
                        if (!is_const) {
                            bool homog = !elems.empty();
                            for (auto& t : elems) if (t != elems[0]) { homog = false; break; }
                            if (!homog)
                                error(std::string("Variable index on heterogeneous tuple '"
                                    + cur_type + "' is not allowed"));
                            std::string elem_t = elems[0];
                            std::string elem_llvm = llvmType(elem_t);
                            std::string idx_val = emitExpr(*indices[level]);
                            std::string gep = newTmp();
                            out_ << "    " << gep << " = getelementptr ["
                                 << elems.size() << " x " << elem_llvm
                                 << "], ptr " << cur_ptr << ", i32 0, i32 " << idx_val << "\n";
                            cur_ptr = gep;
                            cur_type = elem_t;
                            continue;
                        }
                        if (idx < 0 || idx >= (int)elems.size())
                            error(std::string("Tuple index "
                                + std::to_string(idx) + " out of range (size "
                                + std::to_string(elems.size()) + ")"));
                        cur_ptr = emitFieldGep(cur_type, cur_ptr, idx);
                        cur_type = elems[idx];
                    }
                    return cur_ptr;
                }
            }
            // Fixed-size array slid field: ^self.field[i] inside a method body.
            // Mirrors the read arm above, stopping at the element GEP.
            if (!current_slid_.empty()) {
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve->name);
                if (fit != info.field_index.end()
                        && isInlineArrayType(info.field_types[fit->second])) {
                    std::string ft = info.field_types[fit->second];
                    auto lb = ft.rfind('[');
                    std::string elem_type = ft.substr(0, lb);
                    std::string sz_str = ft.substr(lb + 1, ft.size() - lb - 2);
                    std::string elt = llvmType(elem_type);
                    std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                    std::string field_gep = newTmp();
                    out_ << "    " << field_gep << " = getelementptr %struct." << current_slid_
                         << ", ptr " << self << ", i32 0, i32 " << fit->second << "\n";
                    std::string idx_llvm = exprLlvmType(*indices[0]);
                    std::string idx_val = emitExpr(*indices[0]);
                    std::string elem_gep = newTmp();
                    out_ << "    " << elem_gep << " = getelementptr [" << sz_str << " x " << elt
                         << "], ptr " << field_gep << ", i32 0, " << idx_llvm << " " << idx_val << "\n";
                    return elem_gep;
                }
            }
            auto ait = array_info_.find(ve->name);
            if (ait == array_info_.end())
                error(std::string("Undefined array '" + ve->name + "'"));
            auto& ainfo = ait->second;
            std::string flat = emitExpr(*indices[0]);
            for (int k = 1; k < (int)indices.size(); k++) {
                int stride = ainfo.dims[k];
                std::string mul = newTmp();
                out_ << "    " << mul << " = mul i32 " << flat << ", " << stride << "\n";
                std::string idx_val = emitExpr(*indices[k]);
                std::string add = newTmp();
                out_ << "    " << add << " = add i32 " << mul << ", " << idx_val << "\n";
                flat = add;
            }
            int total = 1;
            for (int d : ainfo.dims) total *= d;
            std::string gep = newTmp();
            std::string elt2 = llvmType(ainfo.elem_type);
            out_ << "    " << gep << " = getelementptr [" << total << " x " << elt2 << "], ptr "
                 << ainfo.alloca_reg << ", i32 0, i32 " << flat << "\n";
            return gep; // pointer to the element, no load
        }
        // generic fallback: any lvalue resolveLvalue can handle (chained fields,
        // post/pre-inc-deref via UnaryExpr, etc.).
        {
            auto lv = resolveLvalue(*ao->operand);
            return lv.addr;
        }
    }

    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        // ptr^ — first load the pointer from its alloca, then load through it
        std::string pointee_llvm = "i32"; // default pointee type
        std::string ptr_reg;

        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                error(std::string("Undefined variable '" + ve->name + "'"));
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end() && isIndirectType(tit->second.type)) {
                // variable holds a reference or pointer — load the ptr, then load through it
                std::string loaded_ptr = newTmp();
                out_ << "    " << loaded_ptr << " = load ptr, ptr " << it->second.reg << "\n";
                ptr_reg = loaded_ptr;
                std::string pointee_type = pointeeForLookup(tit->second.type);
                pointee_llvm = llvmType(pointee_type);
            } else {
                std::string type_name = (tit != locals_.end()) ? tit->second.type : "unknown";
                error(std::string("Cannot dereference '" + ve->name + "' of type '" + type_name +
                    "': only reference (^) and pointer ([]) types can be dereferenced"));
            }
        } else {
            // general case: evaluate operand as expression to get a ptr
            ptr_reg = emitExpr(*de->operand);
            // derive pointee type from cast target if available
            if (auto* pc = dynamic_cast<const PtrCastExpr*>(de->operand.get())) {
                std::string t = pc->target_type;
                if (t.size() >= 2 && t.substr(t.size()-2) == "[]") t = t.substr(0, t.size()-2);
                else if (!t.empty() && t.back() == '^') t.pop_back();
                pointee_llvm = llvmType(t);
            } else {
                std::string ot = inferSlidType(*de->operand);
                if (isPtrType(ot) || isIndirectType(ot)) {
                    std::string pointee_type = isPtrType(ot)
                        ? ot.substr(0, ot.size()-2)
                        : ot.substr(0, ot.size()-1);
                    pointee_llvm = llvmType(pointee_type);
                } else if (!ot.empty()) {
                    errorAtNode(*de->operand, "Cannot dereference a value of type '"
                        + ot + "'; only reference (^) and pointer ([]) types can be dereferenced.");
                }
            }
        }

        std::string tmp = newTmp();
        out_ << "    " << tmp << " = load " << pointee_llvm << ", ptr " << ptr_reg << "\n";
        return tmp;
    }

    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        // substitution const lookup: instance.const_name and Type.const_name
        // resolve the receiver's slid type and check that class's const table.
        if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            std::string recv_slid;
            auto lit = locals_.find(ve->name);
            if (lit != locals_.end()) recv_slid = lit->second.type;
            else if (slid_info_.count(ve->name)) recv_slid = ve->name; // Type.const_name
            else if (!current_slid_.empty()) {
                auto& parent_info = slid_info_[current_slid_];
                auto fi = parent_info.field_index.find(ve->name);
                if (fi != parent_info.field_index.end())
                    recv_slid = parent_info.field_types[fi->second];
            }
            // strip reference/pointer suffixes
            while (!recv_slid.empty() && recv_slid.back() == '^')
                recv_slid.pop_back();
            if (recv_slid.size() >= 2
                && recv_slid.substr(recv_slid.size()-2) == "[]")
                recv_slid.resize(recv_slid.size()-2);
            if (!recv_slid.empty()) {
                if (auto* ce = lookupSlidConst(recv_slid, fa->field))
                    return emitConstValue(*ce);
            }
        }
        // handle ptr^.field — object is a DerefExpr
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            std::string ptr_val;
            std::string slid_name;
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
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
            } else {
                ptr_val = emitExpr(*de->operand);
                slid_name = derefSlidName(*de);
            }
            if (slid_name.empty() || !slid_info_.count(slid_name))
                error(std::string("Unknown slid type for field '" + fa->field + "'"));
            auto& info = slid_info_[slid_name];
            auto fit = info.field_index.find(fa->field);
            if (fit == info.field_index.end())
                error(std::string("Unknown field: " + fa->field));
            int idx = fit->second;
            std::string field_type = llvmType(info.field_types[idx]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << ptr_val << ", i32 0, i32 " << idx << "\n";
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = load " << field_type << ", ptr " << gep << "\n";
            return tmp;
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
            std::string field_type = llvmType(info.field_types[idx]);
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = load " << field_type << ", ptr " << gep << "\n";
            return tmp;
        }
        // generic fallback: any chain resolveLvalue can handle (subsumes the
        // tuple[idx].field case and previously-erroring shapes like
        // obj.sub_.field, p++^.field, arr[i].field on a slid array).
        {
            auto lv = resolveLvalue(*fa);
            if (slid_info_.count(lv.type)) return lv.addr;  // slid: callers expect ptr
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = load " << llvmType(lv.type)
                 << ", ptr " << lv.addr << "\n";
            return tmp;
        }
    }

    if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
        // obj.sizeof() and TypeName.sizeof() — compiler-generated __$sizeof, no self arg
        if (mc->method == "sizeof") {
            std::string slid_name;
            if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) slid_name = tit->second.type;
                else if (slid_info_.count(ve->name)) slid_name = ve->name; // TypeName.sizeof()
            }
            if (slid_name.empty())
                error(std::string("Sizeof(): cannot determine slid type"));
            std::string reg = newTmp();
            out_ << "    " << reg << " = call i64 @" << slid_name << "__$sizeof()\n";
            return reg;
        }

        // helper to get slid_name and obj_ptr from any object expression
        std::string slid_name, obj_ptr;
        if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
            // self.method() — special path: take obj_ptr from self_ptr_ to
            // honor inline-ctor-body remap (see self_ptr_ doc in codegen.h).
            if (ve->name == "self" && !current_slid_.empty()) {
                slid_name = current_slid_;
                obj_ptr = self_ptr_.empty() ? "%self" : self_ptr_;
            } else {
                auto type_it = locals_.find(ve->name);
                // self-field shorthand: `field.method()` inside a method body
                // resolves as `self.field.method()` when the name names a
                // field of the enclosing class rather than a local.
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
                if (mc->method == "~") {
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
                              "': use '" + ve->name + "^." + mc->method + "()' for explicit dereference");
                    if (!slid_info_.count(slid_name))
                        error("Method call on '" + ve->name + "': '" + slid_name + "' is not a slid type");
                }
                obj_ptr = self_field_addr.empty()
                    ? locals_[ve->name].reg
                    : self_field_addr;
            }
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mc->object.get())) {
            // ptr^.method() — load the pointer, use as self
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto type_it = locals_.find(ve2->name);
                if (type_it == locals_.end())
                    error(std::string("Unknown type for: " + ve2->name));
                slid_name = type_it->second.type;
                if (isRefType(slid_name)) slid_name.pop_back(); else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                // Strip const qualifier for slid_info_ lookup. The receiver's
                // const-ness is consulted separately by checkConstReceiver.
                slid_name = stripRedundantConstParens(slid_name);
                if (typeStartsWithConst(slid_name)) slid_name = slid_name.substr(6);
                // load the pointer value from the alloca
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << locals_[ve2->name].reg << "\n";
                obj_ptr = loaded;
            } else {
                slid_name = derefSlidName(*de);
                if (!slid_name.empty()) obj_ptr = emitExpr(*de->operand);
            }
        } else if (dynamic_cast<const ArrayIndexExpr*>(mc->object.get())
                || dynamic_cast<const FieldAccessExpr*>(mc->object.get())) {
            // Chained lvalue receiver — same as the stmt-form arm. resolveLvalue
            // drills the chain to (addr, slid type) and method dispatch follows.
            auto lv = resolveLvalue(*mc->object);
            if (slid_info_.count(lv.type)) {
                slid_name = lv.type;
                obj_ptr = lv.addr;
            }
        }
        if (!slid_name.empty() && mc->method == "~") {
            emitExplicitDtor(slid_name, obj_ptr);
            return "";
        }
        if (!slid_name.empty()) {
            for (auto* cur = &slid_info_[slid_name]; cur; cur = cur->base_info) {
                bool stop = false;
                for (auto& mk : cur->method_marks) {
                    if (mk.method_name != mc->method) continue;
                    if (mk.param_types.size() != mc->args.size()) continue;
                    if (mk.is_delete) {
                        errorWithNote("Class '" + slid_name + "': call to deleted method '"
                              + mc->method + "()' (deleted in '" + cur->name + "')",
                              mk.file_id, mk.tok, "Marked = delete here.");
                    }
                    stop = true; break;
                }
                if (stop) break;
            }
            auto& sinfo = slid_info_[slid_name];
            // virtual dispatch: if the static type is virtual and this method
            // matches a vtable slot, prefer the slot's resolved impl. Indirect
            // call when the object is a pointer-deref; static call otherwise.
            int vslot = -1;
            if (sinfo.is_virtual_class) {
                for (int i = 0; i < (int)sinfo.vtable.size(); i++) {
                    if (sinfo.vtable[i].method_name != mc->method) continue;
                    if (sinfo.vtable[i].param_types.size() != mc->args.size()) continue;
                    vslot = i; break;
                }
            }
            std::string mangled;
            std::vector<std::string> mptypes_vec;
            std::string ret_slids;
            if (vslot >= 0) {
                mangled = sinfo.vtable[vslot].mangled;
                mptypes_vec = sinfo.vtable[vslot].param_types;
                ret_slids = sinfo.vtable[vslot].return_type;
            } else {
                std::string dispatch_class = slid_name;
                for (auto* cur = &slid_info_[slid_name]; cur; cur = cur->base_info) {
                    if (method_overloads_.count(cur->name + "__" + mc->method)) {
                        dispatch_class = cur->name;
                        break;
                    }
                }
                std::string base = dispatch_class + "__" + mc->method;
                mangled = resolveOverloadForCall(base, mc->args);
                auto ret_it = func_return_types_.find(mangled);
                if (ret_it == func_return_types_.end())
                    error(std::string("Unknown method: " + mc->method));
                ret_slids = ret_it->second;
                mptypes_vec = func_param_types_[mangled];
            }
            checkConstReceiver(*mc->object, mc->method, mangled);
            bool empty = sinfo.is_empty;
            std::string method_args;
            for (int i = 0; i < (int)mc->args.size(); i++) {
                std::string ptype_str = (i < (int)mptypes_vec.size()) ? mptypes_vec[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                method_args += ", " + ptype + " " + emitPhraseArg(*mc->args[i], ptype_str);
            }
            std::string self_arg = empty ? "" : "ptr " + obj_ptr;
            // indirect call: ptr-deref source OR explicit self inside a virtual
            // method (runtime type may differ from current_slid_).
            // is_self_e flags self.method() — the receiver is the current
            // object whose vtable must be consulted at runtime even when the
            // method's static type matches current_slid_, because a derived
            // class may have overridden it.
            bool is_self_e = false;
            if (auto* mve = dynamic_cast<const VarExpr*>(mc->object.get()))
                is_self_e = (mve->name == "self");
            bool indirect = (vslot >= 0)
                && (dynamic_cast<const DerefExpr*>(mc->object.get()) != nullptr || is_self_e);
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
                if (slid_info_.count(ret_slids)) {
                    std::string tmp = emitRawSlidAlloca(ret_slids);
                    std::string sret_arg = "ptr sret(%struct." + ret_slids + ") " + tmp;
                    std::string args = self_arg.empty() ? sret_arg : self_arg + ", " + sret_arg;
                    out_ << "    call void " << fn << "(" << args << method_args << ")\n";
                    return tmp;
                }
                std::string ret_type = llvmType(ret_slids);
                std::string tmp = newTmp();
                std::string args_prefix = self_arg;
                if (args_prefix.empty() && !method_args.empty())
                    method_args = method_args.substr(2);
                if (ret_type == "void" || ret_type.empty()) {
                    out_ << "    call void " << fn << "(" << args_prefix << method_args << ")\n";
                    return "";
                }
                out_ << "    " << tmp << " = call " << ret_type << " " << fn
                     << "(" << args_prefix << method_args << ")\n";
                return tmp;
            }
            if (slid_info_.count(ret_slids)) {
                std::string tmp = emitRawSlidAlloca(ret_slids);
                std::string sret_arg = "ptr sret(%struct." + ret_slids + ") " + tmp;
                std::string args = self_arg.empty() ? sret_arg : self_arg + ", " + sret_arg;
                out_ << "    call void @" << llvmGlobalName(mangled)
                     << "(" << args << method_args << ")\n";
                return tmp;
            }
            std::string ret_type = llvmType(ret_slids);
            std::string tmp = newTmp();
            std::string args_prefix = self_arg;
            if (args_prefix.empty() && !method_args.empty())
                method_args = method_args.substr(2);
            out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
                 << "(" << args_prefix << method_args << ")\n";
            return tmp;
        }
        error(std::string("Complex method call not yet supported"));
    }

    if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
        // namespace-qualified call: Name:method(args)
        if (!call->qualifier.empty() && call->qualifier != "::") {
            auto sit = slid_info_.find(call->qualifier);
            if (sit == slid_info_.end())
                error(std::string("Unknown slid: " + call->qualifier));
            // inherited base method call from inside a derived's method body:
            // `Base:method(args)` with current_slid_ in Base's descendant set.
            if (!sit->second.is_namespace) {
                if (current_slid_.empty()
                    || (call->qualifier != current_slid_ && !isAncestor(call->qualifier, current_slid_)))
                    error(std::string("Operator '" + call->qualifier
                        + "' is not a namespace; use instance.method() instead"));
                bool empty = sit->second.is_empty;
                std::string base_q = call->qualifier + "__" + call->callee;
                std::string mangled_q = resolveOverloadForCall(base_q, call->args);
                auto rit_q = func_return_types_.find(mangled_q);
                if (rit_q == func_return_types_.end())
                    error(std::string("Unknown method: "
                        + call->qualifier + ":" + call->callee));
                auto& mptypes_q = func_param_types_[mangled_q];
                std::string self_str = self_ptr_.empty() ? "%self" : self_ptr_;
                std::string method_args_q;
                for (int i = 0; i < (int)call->args.size(); i++) {
                    std::string ptype_str = (i < (int)mptypes_q.size()) ? mptypes_q[i] : "";
                    std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                    method_args_q += ", " + ptype + " "
                        + emitPhraseArg(*call->args[i], ptype_str);
                }
                std::string self_arg_q = empty ? "" : "ptr " + self_str;
                if (slid_info_.count(rit_q->second)) {
                    std::string tmp = emitRawSlidAlloca(rit_q->second);
                    std::string sret_arg = "ptr sret(%struct." + rit_q->second + ") " + tmp;
                    std::string args = self_arg_q.empty() ? sret_arg : self_arg_q + ", " + sret_arg;
                    args += method_args_q;
                    out_ << "    call void @" << llvmGlobalName(mangled_q)
                         << "(" << args << ")\n";
                    return tmp;
                }
                std::string ret_type = llvmType(rit_q->second);
                std::string args_prefix = self_arg_q;
                if (args_prefix.empty() && !method_args_q.empty())
                    method_args_q = method_args_q.substr(2);
                if (ret_type == "void") {
                    out_ << "    call void @" << llvmGlobalName(mangled_q)
                         << "(" << args_prefix << method_args_q << ")\n";
                    return "";
                }
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @"
                     << llvmGlobalName(mangled_q) << "(" << args_prefix << method_args_q << ")\n";
                return tmp;
            }
            std::string base = call->qualifier + "__" + call->callee;
            std::string mangled = resolveOverloadForCall(base, call->args);
            auto rit = func_return_types_.find(mangled);
            if (rit == func_return_types_.end())
                error(std::string("Unknown namespace function: " + call->qualifier + ":" + call->callee));
            auto& ptypes = func_param_types_[mangled];
            std::string arg_str;
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (i > 0) arg_str += ", ";
                std::string ptype_str = (i < (int)ptypes.size()) ? ptypes[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                arg_str += ptype + " " + emitPhraseArg(*call->args[i], ptype_str);
            }
            if (slid_info_.count(rit->second)) {
                std::string tmp = emitRawSlidAlloca(rit->second);
                std::string sret_arg = "ptr sret(%struct." + rit->second + ") " + tmp;
                if (!arg_str.empty()) sret_arg += ", " + arg_str;
                out_ << "    call void @" << llvmGlobalName(mangled) << "(" << sret_arg << ")\n";
                return tmp;
            }
            std::string ret_type = llvmType(rit->second);
            if (ret_type == "void") {
                out_ << "    call void @" << llvmGlobalName(mangled) << "(" << arg_str << ")\n";
                return "";
            }
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
                 << "(" << arg_str << ")\n";
            return tmp;
        }
        // ::name(args) — global lookup: skip nested/template/method paths, go straight to free function
        if (call->qualifier == "::") {
            std::string mangled = resolveFreeFunctionMangledName(call->callee, call->args.size());
            if (mangled.empty())
                error(std::string("Undefined global function: " + call->callee));
            checkResolvedFreeFunction(call->callee, mangled, call->args);
            auto it = func_return_types_.find(mangled);
            auto& ptypes = func_param_types_[mangled];
            std::string arg_str;
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (i > 0) arg_str += ", ";
                std::string ptype_str = (i < (int)ptypes.size()) ? ptypes[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                arg_str += ptype + " " + emitPhraseArg(*call->args[i], ptype_str);
            }
            if (slid_info_.count(it->second)) {
                std::string tmp = emitRawSlidAlloca(it->second);
                std::string sret_arg = "ptr sret(%struct." + it->second + ") " + tmp;
                if (!arg_str.empty()) sret_arg += ", " + arg_str;
                out_ << "    call void @" << llvmGlobalName(mangled) << "(" << sret_arg << ")\n";
                return tmp;
            }
            std::string ret_type = llvmType(it->second);
            if (ret_type == "void") {
                out_ << "    call void @" << llvmGlobalName(mangled) << "(" << arg_str << ")\n";
                return "";
            }
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
                 << "(" << arg_str << ")\n";
            return tmp;
        }
        // slid ctor call as expression: SlidName(args) → alloca temp + construct, return ptr.
        // Register for statement-scope dtor so non-consume uses (e.g. call args, discarded
        // ExprStmt) still run the dtor. Consume-the-temp sites (same-type VarDecl init)
        // unregister the entry so ownership transfers cleanly.
        if (slid_info_.count(call->callee)) {
            std::string slid_name = call->callee;
            std::string tmp = emitRawSlidAlloca(slid_name);
            emitConstructAt(slid_name, tmp, call->args);
            if (hasDtorInChain(slid_name))
                pending_temp_dtors_.push_back({tmp, slid_name});
            return tmp;
        }
        // check nested function first
        auto nit = nested_info_.find(call->callee);
        if (nit != nested_info_.end()) {
            auto& info = nit->second;
            std::string mangled = info.parent_name + "__" + call->callee;
            std::string ret_type = llvmType(func_return_types_[mangled]);

            std::string arg_str;
            if (info.captures.size() == 1) {
                std::string cap = *info.captures.begin();
                arg_str = "ptr " + locals_[cap].reg;
            } else if (info.captures.size() >= 2) {
                std::string frame = newTmp() + "_frame";
                out_ << "    " << frame << " = alloca %frame." << info.parent_name << "\n";
                std::vector<std::string> ordered_caps(info.captures.begin(), info.captures.end());
                for (int i = 0; i < (int)ordered_caps.size(); i++) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %frame." << info.parent_name
                         << ", ptr " << frame << ", i32 0, i32 " << i << "\n";
                    out_ << "    store ptr " << locals_[ordered_caps[i]].reg << ", ptr " << gep << "\n";
                }
                arg_str = "ptr " + frame;
            }

            auto& nptypes_ce = func_param_types_[mangled];
            int ni_ce = 0;
            for (auto& arg : call->args) {
                if (!arg_str.empty()) arg_str += ", ";
                std::string ptype = (ni_ce < (int)nptypes_ce.size()) ? llvmType(nptypes_ce[ni_ce]) : "i32";
                arg_str += ptype + " " + emitExpr(*arg);
                ni_ce++;
            }

            std::string tmp = newTmp();
            out_ << "    " << tmp << " = call " << ret_type << " @" << mangled
                 << "(" << arg_str << ")\n";
            return tmp;
        }

        // template call: instantiate and emit (infer type args if not explicit)
        {
            auto resolved = resolveTemplateOverload(call->callee, call->type_args, call->args);
            if (resolved.entry) {
                std::string mangled = instantiateTemplate(*resolved.entry, resolved.type_args);
                std::string ret_slid = func_return_types_[mangled];
                auto& ptypes = func_param_types_[mangled];
                std::string arg_str;
                for (int i = 0; i < (int)call->args.size(); i++) {
                    if (i > 0) arg_str += ", ";
                    std::string ptype = (i < (int)ptypes.size()) ? ptypes[i] : "int";
                    arg_str += llvmType(ptype) + " " + emitPhraseArg(*call->args[i], ptype);
                }
                // sret if return type is a slid
                if (slid_info_.count(ret_slid)) {
                    std::string tmp = emitRawSlidAlloca(ret_slid);
                    std::string sret_arg = "ptr sret(%struct." + ret_slid + ") " + tmp;
                    if (!arg_str.empty()) sret_arg += ", " + arg_str;
                    out_ << "    call void @" << llvmGlobalName(mangled) << "(" << sret_arg << ")\n";
                    return tmp;
                }
                std::string ret_type = llvmType(ret_slid);
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
                     << "(" << arg_str << ")\n";
                return tmp;
            }
        }

        // implicit self method call (peer call inside a slid method) — takes precedence
        // over global free functions per the bare-name lookup rule
        if (!current_slid_.empty()) {
            std::string base = current_slid_ + "__" + call->callee;
            std::string mangled = resolveOverloadForCall(base, call->args);
            auto mit = func_return_types_.find(mangled);
            if (mit != func_return_types_.end()) {
                auto& mptypes = func_param_types_[mangled];
                bool empty = slid_info_[current_slid_].is_empty;
                std::string self_str = empty ? "" : "ptr %self";
                std::string arg_str = self_str;
                for (int i = 0; i < (int)call->args.size(); i++) {
                    std::string ptype_str = (i < (int)mptypes.size()) ? mptypes[i] : "";
                    std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                    if (!arg_str.empty()) arg_str += ", ";
                    arg_str += ptype + " " + emitPhraseArg(*call->args[i], ptype_str);
                }
                if (slid_info_.count(mit->second)) {
                    std::string tmp = emitRawSlidAlloca(mit->second);
                    std::string sret_arg = "ptr sret(%struct." + mit->second + ") " + tmp;
                    std::string final_args = arg_str.empty() ? sret_arg
                        : (self_str.empty() ? sret_arg + ", " + arg_str
                                            : self_str + ", " + sret_arg + arg_str.substr(self_str.size()));
                    out_ << "    call void @" << llvmGlobalName(mangled) << "(" << final_args << ")\n";
                    return tmp;
                }
                std::string ret_type = llvmType(mit->second);
                if (ret_type == "void") {
                    out_ << "    call void @" << llvmGlobalName(mangled) << "(" << arg_str << ")\n";
                    return "";
                }
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
                     << "(" << arg_str << ")\n";
                return tmp;
            }
        }

        // regular function call
        std::string mangled = resolveFreeFunctionMangledName(call->callee, call->args.size());
        if (mangled.empty())
            error(std::string("Undefined function: " + call->callee));
        checkResolvedFreeFunction(call->callee, mangled, call->args);
        auto it = func_return_types_.find(mangled);
        auto& ptypes = func_param_types_[mangled];
        std::string arg_str;
        for (int i = 0; i < (int)call->args.size(); i++) {
            if (i > 0) arg_str += ", ";
            std::string ptype_str = (i < (int)ptypes.size()) ? ptypes[i] : "";
            std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
            arg_str += ptype + " " + emitPhraseArg(*call->args[i], ptype_str);
        }
        // sret: callee returns a slid type — allocate temp, call with ptr sret, return temp
        if (slid_info_.count(it->second)) {
            std::string tmp = emitRawSlidAlloca(it->second);
            std::string sret_arg = "ptr sret(%struct." + it->second + ") " + tmp;
            if (!arg_str.empty()) sret_arg += ", " + arg_str;
            out_ << "    call void @" << llvmGlobalName(mangled) << "(" << sret_arg << ")\n";
            return tmp;
        }
        std::string ret_type = llvmType(it->second);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
             << "(" << arg_str << ")\n";
        return tmp;
    }

    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
        // pre/post increment/decrement — handle before evaluating operand
        if (u->op == "pre++" || u->op == "pre--" ||
            u->op == "post++" || u->op == "post--") {

            bool is_pre = (u->op == "pre++" || u->op == "pre--");
            // Pre-extract: when the advance has already fired at phrase entry,
            // the eval site just loads the (advanced) value via the operand's
            // lvalue. Idempotent for simple operands; complex operand side
            // effects re-fire here (limitation; phrase a non-trivial pre as a
            // separate statement to avoid).
            if (is_pre && preAlreadyDone(u)) {
                auto lv = resolveLvalue(*u->operand);
                std::string loaded = newTmp();
                if (isPtrType(lv.type) || isRefType(lv.type)) {
                    out_ << "    " << loaded << " = load ptr, ptr " << lv.addr << "\n";
                } else {
                    out_ << "    " << loaded << " = load " << llvmType(lv.type)
                         << ", ptr " << lv.addr << "\n";
                }
                return loaded;
            }
            std::string instr = (u->op == "pre++" || u->op == "post++") ? "add" : "sub";
            std::string ptr;

            // dereference: ++(ref^) or ref^++ — increment the value pointed to
            if (auto* de = dynamic_cast<const DerefExpr*>(u->operand.get())) {
                // Body-level const enforcement: writing through a const pointer
                // (either `const T^` or `(const T)^`) is forbidden.
                if (typeHasConst(exprType(*de->operand)))
                    errorAtNode(*u, "Cannot '" + u->op + "' through a const pointer.");
                // derive pointee LLVM width from the pointer variable's declared type
                std::string pointee_llvm = "i32";
                if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end()) {
                        std::string t = tit->second.type;
                        if (isPtrType(t)) t.resize(t.size() - 2);
                        else if (isRefType(t)) t.pop_back();
                        pointee_llvm = llvmType(t);
                    }
                }
                std::string ptr_val = emitExpr(*de->operand);
                std::string old_val = newTmp();
                out_ << "    " << old_val << " = load " << pointee_llvm << ", ptr " << ptr_val << "\n";
                std::string new_val = newTmp();
                out_ << "    " << new_val << " = " << instr << " " << pointee_llvm << " " << old_val << ", 1\n";
                out_ << "    store " << pointee_llvm << " " << new_val << ", ptr " << ptr_val << "\n";
                return is_pre ? new_val : old_val;
            }

            // operand's slids type — drives both ptr-vs-scalar dispatch and width.
            // Generic lvalue resolution covers local var, self-field, FieldAccess,
            // ArrayIndex, and chained shapes uniformly.
            std::string operand_type;
            {
                auto lv = resolveLvalue(*u->operand);
                // Body-level const enforcement: inc/dec writes the lvalue.
                if (typeStartsWithConst(lv.type))
                    errorAtNode(*u, "Cannot '" + u->op + "' a const lvalue.");
                ptr = lv.addr;
                operand_type = lv.type;
            }

            // check if the operand is a pointer type ([] — arithmetic allowed)
            // or a reference type (^ — arithmetic is a compile error)
            bool is_ptr_arith = false;
            std::string pointee_llvm = "i32";
            std::string scalar_llvm = "i32";
            if (!operand_type.empty()) {
                if (isRefType(operand_type)) {
                    auto* ve = dynamic_cast<const VarExpr*>(u->operand.get());
                    error(std::string("Operator '" + u->op + "' on reference '" + (ve ? ve->name : std::string("?")) +
                        "': arithmetic on references is not allowed (use a pointer '[]' type)"));
                }
                if (isPtrType(operand_type)) {
                    is_ptr_arith = true;
                    pointee_llvm = llvmType(operand_type.substr(0, operand_type.size()-2));
                } else {
                    scalar_llvm = llvmType(operand_type);
                }
            }

            std::string old = newTmp();
            int step = (instr == "add" || instr == "fadd") ? 1 : -1;
            if (is_ptr_arith) {
                // pointer arithmetic. Pre-form applies the advance immediately;
                // post-form (PPID) schedules the advance for the next terminator.
                out_ << "    " << old << " = load ptr, ptr " << ptr << "\n";
                if (is_pre) {
                    std::string new_val = newTmp();
                    out_ << "    " << new_val << " = getelementptr " << pointee_llvm
                         << ", ptr " << old << ", i32 " << step << "\n";
                    out_ << "    store ptr " << new_val << ", ptr " << ptr << "\n";
                    return new_val;
                }
                schedulePostInc(PendingAdvance::Pointer, ptr, pointee_llvm, step);
                return old;
            }
            // scalar inc/dec: pre applies immediately; post defers to the
            // enclosing terminator under PPID. Float scalars use fadd/fsub.
            bool is_float = (scalar_llvm == "float" || scalar_llvm == "double");
            out_ << "    " << old << " = load " << scalar_llvm << ", ptr " << ptr << "\n";
            if (is_pre) {
                std::string new_val = newTmp();
                std::string scalar_instr = is_float
                    ? (step > 0 ? "fadd" : "fsub")
                    : (step > 0 ? "add" : "sub");
                std::string lit = is_float ? "1.0" : "1";
                out_ << "    " << new_val << " = " << scalar_instr
                     << " " << scalar_llvm << " " << old << ", " << lit << "\n";
                out_ << "    store " << scalar_llvm << " " << new_val
                     << ", ptr " << ptr << "\n";
                return new_val;
            }
            schedulePostInc(is_float ? PendingAdvance::Float : PendingAdvance::Int,
                            ptr, scalar_llvm, step);
            return old;
        }
        // arity-0 slid dispatch: -a/+a/~a/!a where operand is a slid value, no slid LHS.
        // Mirrors comparison: returns a built-in (bool/int/pointer).
        if (u->op == "-" || u->op == "+" || u->op == "~" || u->op == "!") {
            std::string operand_slid = inferSlidType(*u->operand);
            if (!operand_slid.empty() && slid_info_.count(operand_slid)) {
                std::string base = operand_slid + "__op" + u->op;
                auto moit = method_overloads_.find(base);
                std::string mangled, ret_type;
                if (moit != method_overloads_.end()) {
                    for (auto& [m, ptypes, _pm, _pmt, _fid] : moit->second) {
                        if (ptypes.empty()) {
                            mangled = m;
                            auto rit = func_return_types_.find(m);
                            if (rit != func_return_types_.end()) ret_type = rit->second;
                            break;
                        }
                    }
                }
                if (mangled.empty())
                    error(std::string("No matching arity-0 'op" + u->op + "' for '"
                        + operand_slid + "': define 'op" + u->op + "()' returning a built-in"));
                std::string operand_ptr = emitArgForParam(*u->operand, operand_slid + "^");
                std::string tmp_call = newTmp();
                std::string llvm_ret = ret_type.empty() ? "i32" : llvmType(ret_type);
                out_ << "    " << tmp_call << " = call " << llvm_ret
                     << " @" << llvmGlobalName(mangled) << "(ptr " << operand_ptr << ")\n";
                return tmp_call;
            }
        }
        // other unary ops — evaluate operand first
        std::string val = emitExpr(*u->operand);
        std::string tmp = newTmp();
        if (u->op == "!") {
            std::string truthy = emitToBool(val, exprLlvmType(*u->operand));
            std::string negated = newTmp();
            out_ << "    " << negated << " = xor i1 " << truthy << ", true\n";
            std::string tmp2 = newTmp();
            out_ << "    " << tmp2 << " = zext i1 " << negated << " to i32\n";
            return tmp2;
        }
        if (u->op == "~") {
            std::string val_llvm = exprLlvmType(*u->operand);
            out_ << "    " << tmp << " = xor " << val_llvm << " " << val << ", -1\n";
            return tmp;
        }
        if (u->op == "-") {
            std::string val_llvm = exprLlvmType(*u->operand);
            out_ << "    " << tmp << " = sub " << val_llvm << " 0, " << val << "\n";
            return tmp;
        }
        if (u->op == "+") {
            return val;
        }
        error(std::string("Unknown unary op: " + u->op));
    }

    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        // element-wise tuple binary op: (a,b,c) + (x,y,z) → (a+x, b+y, c+z).
        // applies the desugar rule — operations on tuples apply per element,
        // recursing into nested anon-tuple slots and dispatching user ops on slid slots.
        {
            std::string lslid = inferSlidType(*b->left);
            std::string rslid = inferSlidType(*b->right);
            bool l_tuple = isAnonTupleType(lslid);
            bool r_tuple = isAnonTupleType(rslid);
            static const std::set<std::string> ewise_ops = {
                "+","-","*","/","%","&","|","^","<<",">>"
            };
            // Materialize an anon-tuple operand to a ptr. VarExpr of matching type
            // → use its alloca directly; otherwise emit the struct SSA and store
            // into a fresh temp.
            auto materialize = [&](const Expr& e, const std::string& t) -> std::string {
                if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
                    auto lit = locals_.find(ve->name);
                    if (lit != locals_.end()) return lit->second.reg;
                }
                std::string v = emitExpr(e);
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = alloca " << llvmType(t) << "\n";
                out_ << "    store " << llvmType(t) << " " << v << ", ptr " << tmp << "\n";
                return tmp;
            };
            if (l_tuple && r_tuple) {
                if (lslid != rslid)
                    error(std::string("Tuple type mismatch in elementwise '"
                        + b->op + "': " + lslid + " vs " + rslid));
                if (!ewise_ops.count(b->op))
                    error(std::string("Operator '" + b->op
                        + "' not supported element-wise on tuples"));
                std::string l_ptr = materialize(*b->left, lslid);
                std::string r_ptr = materialize(*b->right, rslid);
                std::string struct_llvm = llvmType(lslid);
                std::string res_ptr = newTmp();
                out_ << "    " << res_ptr << " = alloca " << struct_llvm << "\n";
                emitElementwiseAtPtr(lslid, l_ptr, r_ptr, res_ptr, b->op);
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load " << struct_llvm
                     << ", ptr " << res_ptr << "\n";
                return loaded;
            }
            // mixed shape: tuple op scalar (broadcast). Other side must be a primitive
            // scalar (not slid, not tuple, not pointer).
            if (l_tuple ^ r_tuple) {
                if (!ewise_ops.count(b->op))
                    error(std::string("Operator '" + b->op
                        + "' not supported element-wise on tuples"));
                const std::string& tup_type = l_tuple ? lslid : rslid;
                const std::string& scalar_slids = l_tuple ? rslid : lslid;
                if (slid_info_.count(scalar_slids) || isAnonTupleType(scalar_slids)
                        || isIndirectType(scalar_slids))
                    error(std::string("Broadcast: non-tuple operand of '" + b->op
                        + "' must be a primitive scalar; got '" + scalar_slids + "'"));
                const Expr& tup_expr = l_tuple ? *b->left : *b->right;
                const Expr& scalar_expr = l_tuple ? *b->right : *b->left;
                std::string tup_ptr = materialize(tup_expr, tup_type);
                std::string scalar_val = emitExpr(scalar_expr);
                std::string struct_llvm = llvmType(tup_type);
                std::string res_ptr = newTmp();
                out_ << "    " << res_ptr << " = alloca " << struct_llvm << "\n";
                emitTupleScalarBroadcastAtPtr(tup_type, tup_ptr, scalar_val,
                                              scalar_slids, res_ptr, b->op,
                                              /*scalar_on_left=*/!l_tuple);
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load " << struct_llvm
                     << ", ptr " << res_ptr << "\n";
                return loaded;
            }
        }

        // Primitive short-circuit for &&/||/^^. Slid operands fall through to
        // the op-overload dispatch below (Phase 3 fuse via op&&=/op||=/op^^=).
        if ((b->op == "&&" || b->op == "||" || b->op == "^^")
                && exprSlidType(*b->left).empty()
                && exprSlidType(*b->right).empty()) {
            std::string result_ptr = newTmp() + "_sc";
            out_ << "    " << result_ptr << " = alloca i32\n";
            std::string left_val = emitExpr(*b->left);
            std::string left_bool = emitToBool(left_val, exprLlvmType(*b->left));
            std::string eval_right = newLabel("sc_right");
            std::string done       = newLabel("sc_done");

            if (b->op == "&&") {
                out_ << "    store i32 0, ptr " << result_ptr << "\n";
                out_ << "    br i1 " << left_bool << ", label %" << eval_right
                     << ", label %" << done << "\n";
            } else if (b->op == "||") {
                out_ << "    store i32 1, ptr " << result_ptr << "\n";
                out_ << "    br i1 " << left_bool << ", label %" << done
                     << ", label %" << eval_right << "\n";
            } else {
                out_ << "    store i32 0, ptr " << result_ptr << "\n";
                out_ << "    br label %" << eval_right << "\n";
            }

            out_ << eval_right << ":\n";
            // PPID: && / || right operand is its own phrase — neither pre nor
            // post fires when short-circuited. ^^ always evaluates both sides
            // so it does not introduce a phrase boundary.
            bool right_is_phrase = (b->op == "&&" || b->op == "||");
            if (right_is_phrase) {
                pushPostIncQueue();
                emitPrePass(*b->right);
            }
            std::string right_val = emitExpr(*b->right);
            std::string right_bool = emitToBool(right_val, exprLlvmType(*b->right));
            if (right_is_phrase) flushPostIncQueue();
            std::string right_int = newTmp();
            if (b->op == "^^") {
                std::string xor_result = newTmp();
                out_ << "    " << xor_result << " = xor i1 " << left_bool << ", " << right_bool << "\n";
                out_ << "    " << right_int << " = zext i1 " << xor_result << " to i32\n";
            } else {
                out_ << "    " << right_int << " = zext i1 " << right_bool << " to i32\n";
            }
            out_ << "    store i32 " << right_int << ", ptr " << result_ptr << "\n";
            out_ << "    br label %" << done << "\n";
            out_ << done << ":\n";
            std::string result = newTmp();
            out_ << "    " << result << " = load i32, ptr " << result_ptr << "\n";
            return result;
        }

        // determine operand type before emitting (needed for correctly-typed icmp)
        // integer literals are flexible — they take the type of the other operand.
        // for two non-literals, promote to the wider of the two.
        bool left_is_literal  = (dynamic_cast<const IntLiteralExpr*>(b->left.get())  != nullptr);
        bool right_is_literal = (dynamic_cast<const IntLiteralExpr*>(b->right.get()) != nullptr);
        std::string lt = exprLlvmType(*b->left);
        std::string rt = exprLlvmType(*b->right);
        std::string op_type;
        {
            if (left_is_literal && !right_is_literal)
                op_type = rt;
            else if (right_is_literal && !left_is_literal)
                op_type = lt;
            else {
                // promote to the wider of the two; treat i8<i16<i32<i64
                if (lt == "ptr" || rt == "ptr") {
                    op_type = "ptr";
                } else {
                    static const std::map<std::string,int> rank = {
                        {"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                    auto li = rank.find(lt), ri = rank.find(rt);
                    if (li != rank.end() && ri != rank.end())
                        op_type = (ri->second > li->second) ? rt : lt;
                    else
                        op_type = "i32";
                }
            }
            if (op_type.empty()) op_type = "i32";
        }

        // Operator-overload dispatch: three independent attempts, each with a
        // "defer to compound-fuse" hint that yields when op<sym>= is also defined.
        //
        //   Phase 1 — direct overload: op<sym>(left, right).
        //   Phase 2 — coerce right operand: op=(left_slid, right) then op<sym>(left, tmp).
        //   Phase 3 — compound-fuse fallback: temp = left; temp.op<sym>=(right).
        //
        // Phases 1 and 2 abandon their own dispatch (set their candidate to "")
        // when resolveCompoundFuse returns non-empty. Phase 3 uses the same
        // helper to find its call target.
        //
        // Phase 1: in-class method (op+(sa,sb) stored in self) or free function (sret).
        {
            std::string op_func = resolveOperatorOverload(b->op, *b->left, *b->right);
            if (!op_func.empty() && !resolveCompoundFuse(b->op, *b->left, *b->right).empty()) {
                op_func = ""; // defer to Phase 3
            }
            if (!op_func.empty()) {
                std::string ret = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                bool is_method = (ret == "void");
                // determine the result slid type
                std::string res_slid;
                if (is_method) {
                    auto pos = op_func.find("__op");
                    if (pos != std::string::npos) res_slid = op_func.substr(0, pos);
                } else {
                    res_slid = ret;
                }
                if (!res_slid.empty() && slid_info_.count(res_slid)) {
                    std::string tmp_alloca = emitSlidAlloca(res_slid);
                    auto& ptypes = func_param_types_[op_func];
                    std::string args;
                    if (is_method) {
                        // method: self = tmp_alloca, then sa and sb
                        args = "ptr " + tmp_alloca;
                        std::string la = emitArgForParam(*b->left,  ptypes.size() > 0 ? ptypes[0] : "");
                        std::string ra = emitArgForParam(*b->right, ptypes.size() > 1 ? ptypes[1] : "");
                        if (ptypes.size() > 0) args += ", " + llvmType(ptypes[0]) + " " + la;
                        if (ptypes.size() > 1) args += ", " + llvmType(ptypes[1]) + " " + ra;
                    } else {
                        // free function: sret as first arg, then sa and sb
                        args = "ptr sret(%struct." + res_slid + ") " + tmp_alloca;
                        std::string la = emitArgForParam(*b->left,  ptypes.size() > 0 ? ptypes[0] : "");
                        std::string ra = emitArgForParam(*b->right, ptypes.size() > 1 ? ptypes[1] : "");
                        if (ptypes.size() > 0) args += ", " + llvmType(ptypes[0]) + " " + la;
                        if (ptypes.size() > 1) args += ", " + llvmType(ptypes[1]) + " " + ra;
                    }
                    out_ << "    call void @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                    return tmp_alloca;
                }
                // primitive-return op: class method (self = lhs, rhs is the explicit param) or
                // free function (both operands are explicit params).
                if (!res_slid.empty() && res_slid != "void") {
                    auto& ptypes = func_param_types_[op_func];
                    std::string args;
                    if (isMethodMangled(op_func)) {
                        // method: self = left; ptypes are the explicit params (typically just rhs)
                        std::string self_arg = emitArgForParam(*b->left, exprSlidType(*b->left) + "^");
                        args = "ptr " + self_arg;
                        if (ptypes.size() > 0) {
                            std::string ra = emitArgForParam(*b->right, ptypes[0]);
                            args += ", " + llvmType(ptypes[0]) + " " + ra;
                        }
                    } else {
                        // free function: ptypes hold both operands
                        std::string la = emitArgForParam(*b->left,  ptypes.size() > 0 ? ptypes[0] : "");
                        std::string ra = emitArgForParam(*b->right, ptypes.size() > 1 ? ptypes[1] : "");
                        if (ptypes.size() > 0) args = llvmType(ptypes[0]) + " " + la;
                        if (ptypes.size() > 1) args += (args.empty() ? "" : ", ") + llvmType(ptypes[1]) + " " + ra;
                    }
                    std::string ret_llvm = llvmType(res_slid);
                    std::string tmp = newTmp();
                    out_ << "    " << tmp << " = call " << ret_llvm << " @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                    return tmp;
                }
            }
        }

        // Phase 2: implicit right-operand coercion.
        // left is a slid type, right doesn't match any overload — try coercing right via op=.
        {
            std::string left_slid = exprSlidType(*b->left);
            if (!left_slid.empty()) {
                std::string coerce_mangled = resolveSingleArgOverload(left_slid + "__op=", *b->right);
                if (!coerce_mangled.empty()
                        && !resolveCompoundFuse(b->op, *b->left, *b->right).empty()) {
                    coerce_mangled = ""; // defer to Phase 3
                }
                if (!coerce_mangled.empty()) {
                    // find the op overload on left_slid that accepts (left_slid^, left_slid^)
                    std::string op_func;
                    auto moit = method_overloads_.find(left_slid + "__op" + b->op);
                    if (moit != method_overloads_.end()) {
                        for (auto& [m, ptypes, _pm, _pmt, _fid] : moit->second) {
                            if (ptypes.size() >= 2
                                && isRefType(ptypes[1])
                                && baseSlidType(ptypes[1].substr(0, ptypes[1].size()-1)) == left_slid) {
                                op_func = m; break;
                            }
                        }
                    }
                    if (!op_func.empty()) {
                        auto& op_ptypes = func_param_types_[op_func];
                        // emit left first so evaluation order matches source order
                        std::string la = emitArgForParam(*b->left,
                            op_ptypes.size() > 0 ? op_ptypes[0] : "");
                        // coerce right into a temp of left_slid type
                        std::string coerce_tmp = emitSlidAlloca(left_slid);
                        auto& coerce_ptypes = func_param_types_[coerce_mangled];
                        std::string coerce_arg = emitArgForParam(*b->right,
                            coerce_ptypes.empty() ? "" : coerce_ptypes[0]);
                        std::string coerce_ptype = coerce_ptypes.empty() ? "ptr"
                            : llvmType(coerce_ptypes[0]);
                        out_ << "    call void @" << llvmGlobalName(coerce_mangled)
                             << "(ptr " << coerce_tmp << ", " << coerce_ptype
                             << " " << coerce_arg << ")\n";
                        // alloca result and call op with (left, coerce_tmp) as args
                        std::string res_tmp = emitSlidAlloca(left_slid);
                        std::string args = "ptr " + res_tmp;
                        if (op_ptypes.size() > 0)
                            args += ", " + llvmType(op_ptypes[0]) + " " + la;
                        if (op_ptypes.size() > 1)
                            args += ", ptr " + coerce_tmp;
                        out_ << "    call void @" << llvmGlobalName(op_func)
                             << "(" << args << ")\n";
                        return res_tmp;
                    }
                }
            }
        }

        // Phase 3: op<sym>= fuse — call temp.op<sym>=(right) instead of op<sym>(left,right).
        // Semantics: temp = copy(left); temp op= right; return temp.
        // Optimization: if left is a fresh temp we own, skip the copy and call op<sym>= in place.
        {
            std::string compound_mangled = resolveCompoundFuse(b->op, *b->left, *b->right);
            if (!compound_mangled.empty()) {
                std::string left_slid = exprSlidType(*b->left);
                std::string res_tmp;
                if (isFreshSlidTemp(*b->left)) {
                    // left is a fresh temp we own — call op<sym>= on it directly
                    res_tmp = emitExpr(*b->left);
                } else {
                    // left is a named variable — alloca new temp and copy left into it
                    res_tmp = emitSlidAlloca(left_slid);
                    std::string copy_mangled = resolveSingleArgOverload(left_slid + "__op=", *b->left);
                    if (!copy_mangled.empty()) {
                        auto& cptypes = func_param_types_[copy_mangled];
                        std::string carg = emitArgForParam(*b->left,
                            cptypes.empty() ? "" : cptypes[0]);
                        std::string cptype = cptypes.empty() ? "ptr" : llvmType(cptypes[0]);
                        out_ << "    call void @" << llvmGlobalName(copy_mangled)
                             << "(ptr " << res_tmp << ", " << cptype << " " << carg << ")\n";
                    } else {
                        std::string src = emitArgForParam(*b->left, left_slid + "^");
                        emitSlidSlotAssign(left_slid, res_tmp, src, /*is_move=*/false);
                    }
                }
                // call op<sym>= on result with right
                auto& iptypes = func_param_types_[compound_mangled];
                std::string iarg = emitArgForParam(*b->right,
                    iptypes.empty() ? "" : iptypes[0]);
                std::string iptype = iptypes.empty() ? "ptr" : llvmType(iptypes[0]);
                out_ << "    call void @" << llvmGlobalName(compound_mangled)
                     << "(ptr " << res_tmp << ", " << iptype << " " << iarg << ")\n";
                return res_tmp;
            }
        }

        // No op overload matched. If either operand is a slid value (not a ref),
        // there's no fallback — error rather than letting scalar codegen mistype it.
        {
            std::string left_slid  = exprSlidType(*b->left);
            std::string right_slid = exprSlidType(*b->right);
            bool left_is_slid_value  = !left_slid.empty()  && exprLlvmType(*b->left)  != "ptr";
            bool right_is_slid_value = !right_slid.empty() && exprLlvmType(*b->right) != "ptr";
            if (left_is_slid_value || right_is_slid_value) {
                std::string lhs_t = left_slid.empty()  ? "<scalar>" : left_slid;
                std::string rhs_t = right_slid.empty() ? "<scalar>" : right_slid;
                errorAtNode(*b, "No matching 'op" + b->op + "' overload for "
                              + lhs_t + " " + b->op + " " + rhs_t + ".");
            }
        }

        // pointer + int or pointer - int → GEP
        std::string left_llvm  = exprLlvmType(*b->left);
        std::string right_llvm = exprLlvmType(*b->right);

        // pointer ([]) / reference (^) restrictions — single source of truth
        // shared with exprLlvmType / inferSlidType so static type probes can't
        // outrun the validator.
        validateBinaryPtrRefRestrictions(*b);

        // pointer comparisons require same pointee type
        static const std::set<std::string> cmp_ops = {"==","!=","<","<=",">",">="};
        if (cmp_ops.count(b->op) && left_llvm == "ptr" && right_llvm == "ptr") {
            auto getPtBase = [&](const Expr& e) -> std::string {
                if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end()) {
                        const std::string& t = tit->second.type;
                        if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                            return t.substr(0, t.size()-2);
                    }
                }
                return "";
            };
            std::string lb = getPtBase(*b->left);
            std::string rb = getPtBase(*b->right);
            if (!lb.empty() && !rb.empty() && lb != rb)
                error(std::string("Pointer comparison requires same pointee type: '"
                    + lb + "[]' vs '" + rb + "[]'"));
        }

        // invalid pointer arithmetic
        if (left_llvm == "ptr" || right_llvm == "ptr") {
            if (b->op == "+" && left_llvm == "ptr" && right_llvm == "ptr")
                error(std::string("Pointer + pointer is not allowed"));
            if (b->op == "*" || b->op == "/" || b->op == "%")
                error(std::string("Operator '" + b->op + "' is not allowed on pointer types"));
        }

        if ((b->op == "+" || b->op == "-") && (left_llvm == "ptr" || right_llvm == "ptr")) {
            // figure out pointee type from whichever side is the pointer
            auto getPt = [&](const Expr& e) -> std::string {
                if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
                    if (!current_slid_.empty()) {
                        auto& info = slid_info_[current_slid_];
                        auto fit = info.field_index.find(ve->name);
                        if (fit != info.field_index.end()) {
                            std::string ft = info.field_types[fit->second];
                            if (ft.size() >= 2 && ft.substr(ft.size()-2) == "[]")
                                return llvmType(ft.substr(0, ft.size()-2));
                        }
                    }
                    auto tit = locals_.find(ve->name);
                    if (tit != locals_.end()) {
                        std::string lt = tit->second.type;
                        if (lt.size() >= 2 && lt.substr(lt.size()-2) == "[]")
                            return llvmType(lt.substr(0, lt.size()-2));
                    }
                }
                return "i8"; // default to byte
            };
            bool left_ptr = (left_llvm == "ptr");
            bool right_ptr = (right_llvm == "ptr");

            // ptr - ptr → intptr (ptrtoint both, subtract bytes, divide by element size)
            if (b->op == "-" && left_ptr && right_ptr) {
                // both pointers must have the same pointee type
                auto getSlidsBase = [&](const Expr& e) -> std::string {
                    if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
                        auto tit = locals_.find(ve->name);
                        if (tit != locals_.end()) {
                            const std::string& t = tit->second.type;
                            if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                                return t.substr(0, t.size()-2);
                        }
                    }
                    return "";
                };
                std::string lb = getSlidsBase(*b->left);
                std::string rb = getSlidsBase(*b->right);
                if (!lb.empty() && !rb.empty() && lb != rb)
                    error(std::string("Pointer subtraction requires same pointee type: '"
                        + lb + "[]' vs '" + rb + "[]'"));
                std::string pointee = getPt(*b->left);
                std::string lv = emitExpr(*b->left);
                std::string rv = emitExpr(*b->right);
                std::string li = newTmp(), ri = newTmp(), diff = newTmp(), tmp = newTmp();
                out_ << "    " << li << " = ptrtoint ptr " << lv << " to i64\n";
                out_ << "    " << ri << " = ptrtoint ptr " << rv << " to i64\n";
                out_ << "    " << diff << " = sub i64 " << li << ", " << ri << "\n";
                // divide by sizeof(pointee) using a null-based GEP trick
                std::string szgep = newTmp(), sz = newTmp();
                out_ << "    " << szgep << " = getelementptr " << pointee << ", ptr null, i32 1\n";
                out_ << "    " << sz << " = ptrtoint ptr " << szgep << " to i64\n";
                out_ << "    " << tmp << " = sdiv i64 " << diff << ", " << sz << "\n";
                return tmp;
            }

            std::string pointee = left_ptr ? getPt(*b->left) : getPt(*b->right);
            const Expr& off_expr = left_ptr ? *b->right : *b->left;
            std::string ptr_val  = emitExpr(left_ptr ? *b->left  : *b->right);
            std::string off_val  = emitExpr(off_expr);
            std::string off_type = exprLlvmType(off_expr);
            std::string tmp = newTmp();
            if (b->op == "-") {
                std::string neg = newTmp();
                out_ << "    " << neg << " = sub " << off_type << " 0, " << off_val << "\n";
                off_val = neg;
            }
            out_ << "    " << tmp << " = getelementptr " << pointee
                 << ", ptr " << ptr_val << ", " << off_type << " " << off_val << "\n";
            return tmp;
        }

        std::string left  = emitExpr(*b->left);
        std::string right = emitExpr(*b->right);

        // Step 1: extend the narrower operand to op_type.
        // Signed operands are sign-extended; unsigned operands are zero-extended.
        // Literals are bare integer constants — LLVM accepts them at any width, no extension needed.
        {
            static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
            auto extend = [&](std::string val, const std::string& src, const Expr& e) -> std::string {
                if (src == op_type || op_type == "ptr" || src == "ptr") return val;
                auto si = rank.find(src), di = rank.find(op_type);
                if (si == rank.end() || di == rank.end() || si->second >= di->second) return val;
                std::string ext = newTmp();
                out_ << "    " << ext << " = " << (isUnsignedExpr(e) ? "zext" : "sext")
                     << " " << src << " " << val << " to " << op_type << "\n";
                return ext;
            };
            if (!left_is_literal)  left  = extend(left,  lt, *b->left);
            if (!right_is_literal) right = extend(right, rt, *b->right);
        }

        std::string tmp   = newTmp();

        // Step 2: signed→unsigned after size match — handled implicitly by choosing
        // unsigned operations (udiv/urem/ult/ugt etc.) when either operand is unsigned.
        bool unsig = isUnsignedExpr(*b->left) || isUnsignedExpr(*b->right);
        if      (b->op == "+")  { out_ << "    " << tmp << " = add "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "-")  { out_ << "    " << tmp << " = sub "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "*")  { out_ << "    " << tmp << " = mul "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "/")  { out_ << "    " << tmp << " = " << (unsig ? "udiv" : "sdiv") << " " << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "%")  { out_ << "    " << tmp << " = " << (unsig ? "urem" : "srem") << " " << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "&")  { out_ << "    " << tmp << " = and "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "|")  { out_ << "    " << tmp << " = or "   << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "^")  { out_ << "    " << tmp << " = xor "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "<<") { out_ << "    " << tmp << " = shl "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == ">>") { out_ << "    " << tmp << " = " << (unsig ? "lshr" : "ashr") << " " << op_type << " " << left << ", " << right << "\n"; return tmp; }

        std::string cmp = newTmp();
        std::string pred;
        if      (b->op == "==") pred = "eq";
        else if (b->op == "!=") pred = "ne";
        else if (b->op == "<")  pred = unsig ? "ult" : "slt";
        else if (b->op == ">")  pred = unsig ? "ugt" : "sgt";
        else if (b->op == "<=") pred = unsig ? "ule" : "sle";
        else if (b->op == ">=") pred = unsig ? "uge" : "sge";
        else error(std::string("Unknown operator: " + b->op));

        out_ << "    " << cmp << " = icmp " << pred << " " << op_type << " " << left << ", " << right << "\n";
        out_ << "    " << tmp << " = zext i1 " << cmp << " to i32\n";
        return tmp;
    }

    if (dynamic_cast<const NullptrExpr*>(&expr)) {
        return "null";
    }

    if (auto* se = dynamic_cast<const SizeofExpr*>(&expr)) {
        // helper: bytes for a primitive/pointer type name
        auto sizeofTypeName = [&](const std::string& tn) -> std::string {
            if (tn == "char" || tn == "int8" || tn == "uint8" || tn == "bool") return "1";
            if (tn == "int16" || tn == "uint16") return "2";
            if (tn == "int" || tn == "int32" || tn == "uint" || tn == "uint32" || tn == "float32") return "4";
            if (tn == "int64" || tn == "uint64" || tn == "float64" || tn == "intptr") return "8";
            // pointer/iterator types — size of a pointer
            if (tn.size() >= 1 && tn.back() == '^') return "8";
            if (tn.size() >= 2 && tn.substr(tn.size()-2) == "[]") return "8";
            // slid (struct) type — call __$sizeof
            if (slid_info_.count(tn)) {
                std::string reg = newTmp();
                out_ << "    " << reg << " = call i64 @" << tn << "__$sizeof()\n";
                return reg;
            }
            return "0";
        };

        const Expr& op = *se->operand;

        // string literal → byte length (not including null terminator)
        if (auto* sl = dynamic_cast<const StringLiteralExpr*>(&op)) {
            int len; llvmEscape(sl->value, len);
            return std::to_string(len - 1); // llvmEscape counts the null terminator
        }

        // VarExpr: variable (value form) or type name (type form).
        // Locals/fields win — type-form falls through to symbol-table check.
        if (auto* ve = dynamic_cast<const VarExpr*>(&op)) {
            // stack array → total byte size
            auto ait = array_info_.find(ve->name);
            if (ait != array_info_.end()) {
                int64_t total = 1;
                for (int d : ait->second.dims) total *= d;
                int64_t elem_bytes = 1;
                std::string elt = llvmType(ait->second.elem_type);
                if (elt == "i16") elem_bytes = 2;
                else if (elt == "i32") elem_bytes = 4;
                else if (elt == "i64" || elt == "ptr") elem_bytes = 8;
                return std::to_string(total * elem_bytes);
            }
            // ordinary variable → sizeof its declared type
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) return sizeofTypeName(tit->second.type);
            // field of current slid
            if (!current_slid_.empty()) {
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve->name);
                if (fit != info.field_index.end())
                    return sizeofTypeName(info.field_types[fit->second]);
            }
            // Not a local/field — is the name a type? (built-in keyword,
            // pointer/iterator suffix form, or known slid type.)
            const std::string& n = ve->name;
            bool is_type =
                n == "char" || n == "bool" ||
                n == "int" || n == "int8" || n == "int16" || n == "int32" || n == "int64" ||
                n == "uint" || n == "uint8" || n == "uint16" || n == "uint32" || n == "uint64" ||
                n == "intptr" || n == "float32" || n == "float64" ||
                (!n.empty() && n.back() == '^') ||
                (n.size() >= 2 && n.substr(n.size()-2) == "[]") ||
                slid_info_.count(n);
            if (is_type) return sizeofTypeName(n);
        }

        // fallback: derive size from LLVM type of the expression
        std::string lt = exprLlvmType(op);
        if (lt == "i8")  return "1";
        if (lt == "i16") return "2";
        if (lt == "i32") return "4";
        if (lt == "i64") return "8";
        if (lt == "ptr") return "8";
        return "0";
    }

    if (auto* ne = dynamic_cast<const NewExpr*>(&expr)) {
        std::string elt = llvmType(ne->elem_type);
        std::string count_val = emitExpr(*ne->count);
        std::string count_type = exprLlvmType(*ne->count);

        // normalize count to i64
        std::string count64;
        if (count_type == "i64") {
            count64 = count_val;
        } else {
            count64 = newTmp();
            out_ << "    " << count64 << " = zext " << count_type << " " << count_val << " to i64\n";
        }

        bool is_slid = slid_info_.count(ne->elem_type) > 0;

        // compute element size
        std::string elem_size;
        if (is_slid) {
            {
                std::string sz = newTmp();
                out_ << "    " << sz << " = call i64 @" << ne->elem_type << "__$sizeof()\n";
                elem_size = sz;
            }
        } else {
            int eb = 1;
            if (elt == "i16") eb = 2;
            else if (elt == "i32" || elt == "float") eb = 4;
            else if (elt == "i64" || elt == "double" || elt == "ptr") eb = 8;
            elem_size = std::to_string(eb);
        }

        std::string data_bytes = newTmp();
        out_ << "    " << data_bytes << " = mul i64 " << count64 << ", " << elem_size << "\n";

        std::string elem_ptr;
        if (is_slid) {
            // prepend 8-byte count header so delete can loop destructors
            std::string total = newTmp();
            out_ << "    " << total << " = add i64 8, " << data_bytes << "\n";
            std::string mptr = newTmp();
            out_ << "    " << mptr << " = call ptr @malloc(i64 " << total << ")\n";
            out_ << "    store i64 " << count64 << ", ptr " << mptr << "\n";
            elem_ptr = newTmp();
            out_ << "    " << elem_ptr << " = getelementptr i8, ptr " << mptr << ", i64 8\n";

            // call ctor on each element
            auto& info = slid_info_[ne->elem_type];
            const SlidDef* slid_def = nullptr;
            for (auto& s : program_.slids)
                if (s.name == ne->elem_type) { slid_def = &s; break; }

            bool has_any_ctor = info.needs_ctor_fn || (slid_def && slid_def->ctor_body);
            if (has_any_ctor) {
                std::string idx_reg = newTmp();
                out_ << "    " << idx_reg << " = alloca i64\n";
                out_ << "    store i64 0, ptr " << idx_reg << "\n";

                std::string cond_lbl = newLabel("new_ctor_cond");
                std::string body_lbl = newLabel("new_ctor_body");
                std::string end_lbl  = newLabel("new_ctor_end");

                out_ << "    br label %" << cond_lbl << "\n";
                block_terminated_ = false;
                out_ << cond_lbl << ":\n";
                std::string idx = newTmp();
                out_ << "    " << idx << " = load i64, ptr " << idx_reg << "\n";
                std::string cmp = newTmp();
                out_ << "    " << cmp << " = icmp ult i64 " << idx << ", " << count64 << "\n";
                out_ << "    br i1 " << cmp << ", label %" << body_lbl << ", label %" << end_lbl << "\n";

                block_terminated_ = false;
                out_ << body_lbl << ":\n";
                std::string elem = newTmp();
                out_ << "    " << elem << " = getelementptr %struct." << ne->elem_type
                     << ", ptr " << elem_ptr << ", i64 " << idx << "\n";

                // initialize fields to defaults
                for (int i = 0; i < (int)info.field_types.size(); i++) {
                    std::string gep = newTmp();
                    out_ << "    " << gep << " = getelementptr %struct." << ne->elem_type
                         << ", ptr " << elem << ", i32 0, i32 " << i << "\n";
                    std::string val;
                    if (slid_def && slid_def->fields[i].default_val)
                        val = emitExpr(*slid_def->fields[i].default_val);
                    else
                        val = isInlineArrayType(info.field_types[i]) ? "zeroinitializer" : "0";
                    out_ << "    store " << llvmType(info.field_types[i]) << " " << val << ", ptr " << gep << "\n";
                }
                if (slid_def && slid_def->ctor_body) {
                    // Inline ctor body emission for `new T[n]` — each slot's
                    // ctor runs with self_ptr_ remapped to the per-iteration
                    // alloca. References to `self` inside the ctor body must
                    // resolve to this temporary self via self_ptr_, not via
                    // locals_["self"].reg (which is fixed at function entry).
                    // This is the only mid-function self_ptr_ remap; it's why
                    // self_ptr_ exists as a separate variable from
                    // locals_["self"].reg (see codegen.h).
                    std::string saved_slid = current_slid_;
                    std::string saved_self = self_ptr_;
                    current_slid_ = ne->elem_type;
                    self_ptr_ = elem;
                    emitBlock(*slid_def->ctor_body);
                    current_slid_ = saved_slid;
                    self_ptr_ = saved_self;
                }
                emitCtorCall(ne->elem_type, elem);

                std::string idx_next = newTmp();
                out_ << "    " << idx_next << " = add i64 " << idx << ", 1\n";
                out_ << "    store i64 " << idx_next << ", ptr " << idx_reg << "\n";
                out_ << "    br label %" << cond_lbl << "\n";

                block_terminated_ = false;
                out_ << end_lbl << ":\n";
            }
        } else {
            elem_ptr = newTmp();
            out_ << "    " << elem_ptr << " = call ptr @malloc(i64 " << data_bytes << ")\n";
        }
        return elem_ptr;
    }

    if (auto* ne = dynamic_cast<const NewScalarExpr*>(&expr)) {
        const std::string& stype = ne->elem_type;
        if (slid_info_.count(stype)) {
            auto& info = slid_info_[stype];
            if (info.is_virtual_class) {
                for (auto& slot : info.vtable) {
                    if (slot.is_pure)
                        error(std::string("Cannot instantiate pure virtual class '" + stype
                            + "': method '" + slot.method_name + "' is not defined"));
                }
            }
        }
        // compute sizeof(stype) and malloc
        std::string size_val;
        if (slid_info_.count(stype)) {
            std::string sz = newTmp();
            out_ << "    " << sz << " = call i64 @" << stype << "__$sizeof()\n";
            size_val = sz;
        } else {
            // primitive type
            std::string elt = llvmType(stype);
            if (elt == "i8" || elt == "i1")  size_val = "1";
            else if (elt == "i16")            size_val = "2";
            else if (elt == "i32" || elt == "float") size_val = "4";
            else                               size_val = "8";
        }
        std::string ptr = newTmp();
        out_ << "    " << ptr << " = call ptr @malloc(i64 " << size_val << ")\n";

        emitConstructAt(stype, ptr, ne->args);
        return ptr;
    }

    if (auto* pne = dynamic_cast<const PlacementNewExpr*>(&expr)) {
        const std::string& stype = pne->elem_type;
        static const std::set<std::string> allowed_addr_types = {
            "void^", "void[]", "int8^", "int8[]", "uint8^", "uint8[]"
        };
        std::string addr_type = exprType(*pne->addr);
        if (!allowed_addr_types.count(addr_type))
            error(std::string("Placement new: address must be void, int8, or uint8 pointer, got '" + addr_type + "'"));
        std::string ptr = emitExpr(*pne->addr);
        emitConstructAt(stype, ptr, pne->args);
        return ptr;
    }

    if (auto* se = dynamic_cast<const StringifyExpr*>(&expr)) {
        std::string result;
        if (se->kind == "name") {
            auto* ve = dynamic_cast<const VarExpr*>(se->operand.get());
            if (!ve) error(std::string("##name requires a simple variable"));
            result = ve->name;
        } else if (se->kind == "type") {
            result = inferSlidType(*se->operand);
        } else if (se->kind == "line") {
            int ln = 0;
            if (se->file_id >= 0) {
                auto& tlocs = sm_.at(se->file_id).tokens;
                if (se->tok >= 0 && se->tok < (int)tlocs.size())
                    ln = tlocs[se->tok].line;
            }
            result = std::to_string(ln);
        } else if (se->kind == "file") {
            if (se->file_id >= 0) {
                std::string p = sm_.at(se->file_id).path;
                auto slash = p.find_last_of('/');
                result = (slash == std::string::npos) ? p : p.substr(slash + 1);
            } else {
                result = source_file_;
            }
        } else if (se->kind == "func") {
            result = current_func_name_;
        } else if (se->kind == "date") {
            result = __DATE__;
        } else if (se->kind == "time") {
            result = __TIME__;
        }
        StringLiteralExpr str(result);
        return emitExpr(str);
    }

    if (auto* s = dynamic_cast<const StringLiteralExpr*>(&expr)) {
        std::string label = registerStringConstant(s->value);
        int len; llvmEscape(s->value, len);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
             << label << ", i32 0, i32 0\n";
        return tmp;
    }

    // tuple literal (e1, e2, ...): materialize into a fresh alloca, fill per slot
    // dispatching scalar / slid / nested-tuple per the desugar rule, then return
    // the loaded struct SSA value.
    if (auto* te = dynamic_cast<const TupleExpr*>(&expr)) {
        std::string ttype = inferSlidType(*te);
        std::string struct_llvm = llvmType(ttype);
        std::string reg = newTmp();
        out_ << "    " << reg << " = alloca " << struct_llvm << "\n";
        for (int i = 0; i < (int)te->values.size(); i++) {
            // PPID: each tuple element is its own phrase.
            pushPostIncQueue();
            emitPrePass(*te->values[i]);
            std::string elem_ttype = inferSlidType(*te->values[i]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr " << struct_llvm
                 << ", ptr " << reg << ", i32 0, i32 " << i << "\n";
            if (slid_info_.count(elem_ttype)) {
                // Prvalue elision: when the slot value is an inline ctor call
                // for the same slid type, construct directly at the slot —
                // skipping the materialize-temp + field-by-field copy.
                if (auto* ce = dynamic_cast<const CallExpr*>(te->values[i].get())) {
                    if (ce->callee == elem_ttype && slid_info_.count(ce->callee)) {
                        emitConstructAt(elem_ttype, gep, ce->args);
                        flushPostIncQueue();
                        continue;
                    }
                }
                std::string src_ptr;
                if (auto* ve = dynamic_cast<const VarExpr*>(te->values[i].get())) {
                    auto lit = locals_.find(ve->name);
                    src_ptr = (lit != locals_.end()) ? lit->second.reg : emitExpr(*te->values[i]);
                } else {
                    src_ptr = emitExpr(*te->values[i]);
                }
                emitSlidSlotAssign(elem_ttype, gep, src_ptr, /*is_move=*/false, /*is_init=*/true);
                flushPostIncQueue();
                continue;
            }
            std::string val = emitExpr(*te->values[i]);
            out_ << "    store " << llvmType(elem_ttype) << " " << val
                 << ", ptr " << gep << "\n";
            flushPostIncQueue();
        }
        std::string loaded = newTmp();
        out_ << "    " << loaded << " = load " << struct_llvm
             << ", ptr " << reg << "\n";
        return loaded;
    }

    // float literal — emit as double constant
    if (auto* fl = dynamic_cast<const FloatLiteralExpr*>(&expr)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", fl->value);
        std::string s = buf;
        // LLVM IR requires a decimal point in float constants
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    }

    // type conversion: (type=expr)
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr)) {
        // slid type conversion: (SlidType=expr) — alloca a temp, init fields, call op=, return ptr
        if (slid_info_.count(nc->target_type)) {
            const std::string& stype = nc->target_type;

            std::string tmp_reg = emitRawSlidAlloca(stype);
            // default-init fields + call ctor
            emitConstructAtPtrs(stype, tmp_reg, {}, {});

            // call op= with the operand
            std::string mangled = resolveSingleArgOverload(stype + "__op=", *nc->operand);
            if (!mangled.empty()) {
                auto& ptypes = func_param_types_[mangled];
                std::string param_type = ptypes.empty() ? "" : ptypes[0];
                std::string arg_val = emitArgForParam(*nc->operand, param_type);
                std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                out_ << "    call void @" << llvmGlobalName(mangled)
                     << "(ptr " << tmp_reg << ", " << ptype_str << " " << arg_val << ")\n";
            }
            return tmp_reg;
        }

        std::string src_val  = emitExpr(*nc->operand);
        std::string src_type = exprLlvmType(*nc->operand);
        std::string dst_type = llvmType(nc->target_type);
        if (src_type == dst_type) return src_val;

        bool src_is_float = (src_type == "float" || src_type == "double");
        bool dst_is_float = (dst_type == "float" || dst_type == "double");
        static const std::set<std::string> utypes_nc = {"uint","uint8","uint16","uint32","uint64"};
        bool dst_is_unsigned = utypes_nc.count(nc->target_type) > 0;

        std::string tmp = newTmp();
        if (!src_is_float && !dst_is_float) {
            // int → int
            static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
            auto si = rank.find(src_type), di = rank.find(dst_type);
            if (si != rank.end() && di != rank.end() && si->second != di->second) {
                if (di->second > si->second)
                    out_ << "    " << tmp << " = " << (isUnsignedExpr(*nc->operand) ? "zext" : "sext")
                         << " " << src_type << " " << src_val << " to " << dst_type << "\n";
                else
                    out_ << "    " << tmp << " = trunc " << src_type << " " << src_val << " to " << dst_type << "\n";
            } else {
                return src_val; // same width (sign reinterpretation) — no instruction needed
            }
        } else if (src_is_float && !dst_is_float) {
            // float → int
            out_ << "    " << tmp << " = " << (dst_is_unsigned ? "fptoui" : "fptosi")
                 << " " << src_type << " " << src_val << " to " << dst_type << "\n";
        } else if (!src_is_float && dst_is_float) {
            // int → float
            out_ << "    " << tmp << " = " << (isUnsignedExpr(*nc->operand) ? "uitofp" : "sitofp")
                 << " " << src_type << " " << src_val << " to " << dst_type << "\n";
        } else {
            // float → float
            static const std::map<std::string,int> frank = {{"float",0},{"double",1}};
            auto si = frank.find(src_type), di = frank.find(dst_type);
            if (si != frank.end() && di != frank.end()) {
                if (di->second > si->second)
                    out_ << "    " << tmp << " = fpext " << src_type << " " << src_val << " to " << dst_type << "\n";
                else
                    out_ << "    " << tmp << " = fptrunc " << src_type << " " << src_val << " to " << dst_type << "\n";
            } else {
                return src_val;
            }
        }
        return tmp;
    }

    // qualifier-only cast: <const> expr  or  <mutable> expr — pass operand
    // value through verbatim; the cast's slid type carries the new qualifier
    // (inferSlidType handles the type-string projection).
    if (auto* qc = dynamic_cast<const QualifierCastExpr*>(&expr)) {
        return emitExpr(*qc->operand);
    }

    // pointer reinterpret cast: <Type^> expr
    if (auto* pc = dynamic_cast<const PtrCastExpr*>(&expr)) {
        auto ptrBase = [](const std::string& t) -> std::string {
            if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return t.substr(0, t.size()-2);
            if (!t.empty() && t.back() == '^') return t.substr(0, t.size()-1);
            return "";
        };
        std::string src_slids = inferSlidType(*pc->operand);
        std::string src_base  = ptrBase(src_slids);
        std::string dst_base  = ptrBase(pc->target_type);
        bool src_opaque = (src_base == "void" || src_base == "int8" || src_base == "uint8");
        bool dst_opaque = (dst_base == "void" || dst_base == "int8" || dst_base == "uint8");
        if (!src_base.empty() && !dst_base.empty()
            && !src_opaque && !dst_opaque
            && src_base != dst_base) {
            bool src_slid = slid_info_.count(src_base) > 0;
            bool dst_slid = slid_info_.count(dst_base) > 0;
            bool ok = false;
            if (src_slid && dst_slid)
                ok = isAncestor(src_base, dst_base) || isAncestor(dst_base, src_base);
            else if (!src_slid && !dst_slid)
                ok = (llvmType(src_base) == llvmType(dst_base));
            if (!ok)
                error(std::string("Cannot cast pointer of unrelated type '"
                    + src_slids + "' to '" + pc->target_type + "'"));
        }
        std::string src_val  = emitExpr(*pc->operand);
        std::string src_type = exprLlvmType(*pc->operand);
        std::string dst_type = llvmType(pc->target_type);
        if (src_type == dst_type) return src_val;
        std::string tmp = newTmp();
        if (src_type == "ptr" && dst_type == "i64") {
            out_ << "    " << tmp << " = ptrtoint ptr " << src_val << " to i64\n";
        } else if (src_type == "i64" && dst_type == "ptr") {
            out_ << "    " << tmp << " = inttoptr i64 " << src_val << " to ptr\n";
        } else {
            // ptr → ptr: all pointers are opaque in LLVM — no instruction needed
            return src_val;
        }
        return tmp;
    }

    error(std::string("Unsupported expression type"));
}

// Infer the LLVM type that emitExpr will produce for this expression,
// without emitting any IR. Used by emitCondBool to build a correctly-typed
// icmp regardless of whether the condition involves i8, i16, i32, ptr, etc.
std::string Codegen::exprLlvmType(const Expr& expr) {
    // integer literal — always i32 (emitExpr returns a bare integer string,
    // and the consumer hardcodes i32 for BinaryExpr; literals used in conditions
    // are compared as i32)
    if (auto* il = dynamic_cast<const IntLiteralExpr*>(&expr))
        return (il->value > INT32_MAX || il->value < INT32_MIN) ? "i64" : "i32";

    // nullptr — ptr
    if (dynamic_cast<const NullptrExpr*>(&expr)) return "ptr";

    // new expr — ptr
    if (dynamic_cast<const NewExpr*>(&expr))          return "ptr";
    if (dynamic_cast<const NewScalarExpr*>(&expr))    return "ptr";
    if (dynamic_cast<const PlacementNewExpr*>(&expr)) return "ptr";

    // string literal — ptr
    if (dynamic_cast<const StringLiteralExpr*>(&expr)) return "ptr";

    // stringify — ptr (char[])
    if (dynamic_cast<const StringifyExpr*>(&expr)) return "ptr";

    // variable — look up declared type
    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
        // substitution const — return the const's type
        if (auto* ce = lookupConst(v->name)) return llvmType(ce->slid_type);
        // field access via self in a method
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.field_index.count(v->name))
                return llvmType(info.field_types[info.field_index.at(v->name)]);
        }
        auto tit = locals_.find(v->name);
        if (tit != locals_.end()) return llvmType(tit->second.type);
        // enum value — i32
        if (enum_values_.count(v->name)) return "i32";
        // array name used as pointer — ptr
        if (array_info_.count(v->name)) return "ptr";
        // type name used as anonymous slid temp — ptr
        if (slid_info_.count(v->name)) return "ptr";
        return "i32";
    }

    // dereference: type is the pointee type
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end() && isIndirectType(tit->second.type)) {
                std::string pt = isPtrType(tit->second.type)
                    ? tit->second.type.substr(0, tit->second.type.size()-2)
                    : tit->second.type.substr(0, tit->second.type.size()-1);
                return llvmType(pt);
            }
        }
        std::string ot = inferSlidType(*de->operand);
        if (isPtrType(ot) || isIndirectType(ot)) {
            std::string pt = isPtrType(ot)
                ? ot.substr(0, ot.size()-2)
                : ot.substr(0, ot.size()-1);
            return llvmType(pt);
        }
        return "i32";
    }

    // field access — look up field type
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        // substitution const lookup on instance/Type receiver
        if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            std::string recv_slid;
            auto lit = locals_.find(ve->name);
            if (lit != locals_.end()) recv_slid = lit->second.type;
            else if (slid_info_.count(ve->name)) recv_slid = ve->name;
            while (!recv_slid.empty() && recv_slid.back() == '^')
                recv_slid.pop_back();
            if (recv_slid.size() >= 2
                && recv_slid.substr(recv_slid.size()-2) == "[]")
                recv_slid.resize(recv_slid.size()-2);
            if (!recv_slid.empty()) {
                if (auto* ce = lookupSlidConst(recv_slid, fa->field))
                    return llvmType(ce->slid_type);
            }
        }
        std::string slid_name;
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            slid_name = derefSlidName(*de);
        } else if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) slid_name = tit->second.type;
        } else if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(fa->object.get())) {
            // tuple[idx].field: resolve the slot's slid type
            if (auto* bve = dynamic_cast<const VarExpr*>(ai->base.get())) {
                auto tit = locals_.find(bve->name);
                if (tit != locals_.end() && isAnonTupleType(tit->second.type)) {
                    auto elems = anonTupleElems(tit->second.type);
                    int idx;
                    if (constExprToInt(*ai->index, enum_values_, idx)
                            && idx >= 0 && idx < (int)elems.size())
                        slid_name = elems[idx];
                }
            }
        }
        if (!slid_name.empty() && slid_info_.count(slid_name)) {
            auto& info = slid_info_[slid_name];
            auto fit = info.field_index.find(fa->field);
            if (fit != info.field_index.end())
                return llvmType(info.field_types[fit->second]);
        }
        return "i32";
    }

    // comparison operators always produce i1 -> zext to i32 in emitExpr
    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        // Run ptr/ref restriction validator first so static probes (here and in
        // inferSlidType) surface the same diagnostics emitExpr would. Without
        // this, requirePtrInit's exprLlvmType call lets `ref - ref2` slip
        // through as i64 and we throw a confusing same-type init error.
        validateBinaryPtrRefRestrictions(*b);
        // any phase that produces a slid value (direct overload, right-operand
        // coercion, or op<sym>= fuse) lowers to a ptr (alloca temp). Defer to
        // exprSlidType which already checks all three phases.
        if (!exprSlidType(*b).empty()) return "ptr";
        if (b->op == "==" || b->op == "!=" || b->op == "<" ||
            b->op == ">"  || b->op == "<=" || b->op == ">=" ||
            b->op == "&&" || b->op == "||" || b->op == "^^")
            return "i32";
        // elementwise tuple binary op (including scalar broadcast): the result is a
        // loaded struct SSA matching the tuple operand's LLVM type.
        {
            std::string ls = inferSlidType(*b->left);
            std::string rs = inferSlidType(*b->right);
            if (isAnonTupleType(ls)) return llvmType(ls);
            if (isAnonTupleType(rs)) return llvmType(rs);
        }
        // arithmetic/bitwise: result is the wider of the two operands
        {
            bool left_is_literal  = (dynamic_cast<const IntLiteralExpr*>(b->left.get())  != nullptr);
            bool right_is_literal = (dynamic_cast<const IntLiteralExpr*>(b->right.get()) != nullptr);
            std::string lt = exprLlvmType(*b->left);
            std::string rt = exprLlvmType(*b->right);
            if (left_is_literal && !right_is_literal) return rt;
            if (right_is_literal && !left_is_literal) return lt;
            // pointer arithmetic: ptr - ptr → intptr (i64); ptr +/- int → ptr
            if (b->op == "-" && lt == "ptr" && rt == "ptr") return "i64";
            if ((b->op == "+" || b->op == "-") && (lt == "ptr" || rt == "ptr"))
                return "ptr";
            static const std::map<std::string,int> rank = {
                {"i8",0},{"i16",1},{"i32",2},{"i64",3}};
            auto li = rank.find(lt), ri = rank.find(rt);
            if (li != rank.end() && ri != rank.end())
                return (ri->second > li->second) ? rt : lt;
        }
        return "i32";
    }

    // unary: ! and ~ produce i32; inc/dec preserve operand type
    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
        // arity-0 slid dispatch: return type comes from the method
        if (u->op == "-" || u->op == "+" || u->op == "~" || u->op == "!") {
            std::string operand_slid = inferSlidType(*u->operand);
            if (!operand_slid.empty() && slid_info_.count(operand_slid)) {
                auto moit = method_overloads_.find(operand_slid + "__op" + u->op);
                if (moit != method_overloads_.end())
                    for (auto& [m, ptypes, _pm, _pmt, _fid] : moit->second)
                        if (ptypes.empty()) {
                            auto rit = func_return_types_.find(m);
                            return rit != func_return_types_.end() ? llvmType(rit->second) : "i32";
                        }
            }
        }
        if (u->op == "!" || u->op == "~") return "i32";
        // pre/post inc/dec — same type as operand
        return exprLlvmType(*u->operand);
    }

    // address-of — ptr
    if (dynamic_cast<const AddrOfExpr*>(&expr)) return "ptr";

    // DerefExpr: ptr^ — element type of the pointer
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) {
                std::string t = tit->second.type;
                if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                    return llvmType(t.substr(0, t.size()-2));
                if (!t.empty() && t.back() == '^')
                    return "ptr"; // dereffing a reference gives the slid (ptr to struct)
            }
        }
        return "i32";
    }


    // array index — drill the chain layer by layer (mirrors the runtime
    // dispatch in resolveLvalue / drillIndexChain). Slid-typed final element
    // is returned as `ptr` per emitExpr's convention.
    if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        (void)ai;
        std::vector<const Expr*> indices;
        const Expr* cur = &expr;
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
            indices.insert(indices.begin(), a->index.get());
            cur = a->base.get();
        }
        std::string type;
        int consumed = 0;
        if (auto* ve = dynamic_cast<const VarExpr*>(cur)) {
            auto ait = array_info_.find(ve->name);
            if (ait != array_info_.end()) {
                type = ait->second.elem_type;
                consumed = std::min((int)indices.size(), (int)ait->second.dims.size());
            }
        }
        if (consumed == 0) type = inferSlidType(*cur);
        std::string final_type = drillIndexedType(type, indices, consumed);
        if (slid_info_.count(final_type)) return "ptr";
        return llvmType(final_type);
    }

    // method call — look up return type
    if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
        if (mc->method == "sizeof") return "i64";
        std::string slid_name;
        if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
            // self short-circuits to current_slid_ (equivalent to locals_["self"].type
            // but avoids the map lookup; both yield the same value).
            if (ve->name == "self" && !current_slid_.empty()) slid_name = current_slid_;
            else {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) slid_name = tit->second.type;
            }
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mc->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = locals_.find(ve2->name);
                if (tit != locals_.end()) {
                    std::string t = tit->second.type;
                    if (!t.empty() && t.back() == '^') t.pop_back();
                    else if (t.size() >= 2 && t.substr(t.size()-2) == "[]") t.resize(t.size()-2);
                    slid_name = t;
                }
            }
        }
        if (!slid_name.empty()) {
            // virtual class: prefer the vtable slot's resolved return type
            auto sit = slid_info_.find(slid_name);
            if (sit != slid_info_.end() && sit->second.is_virtual_class) {
                for (auto& slot : sit->second.vtable) {
                    if (slot.method_name == mc->method
                        && slot.param_types.size() == mc->args.size())
                        return llvmType(slot.return_type);
                }
            }
            std::string base = slid_name + "__" + mc->method;
            std::string mangled = resolveOverloadForCall(base, mc->args);
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end()) return llvmType(rit->second);
        }
        return "i32";
    }

    // function call — look up return type
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        auto resolved = resolveTemplateOverload(ce->callee, ce->type_args, ce->args);
        if (resolved.entry) {
            const FunctionDef& tmpl = *resolved.entry->def;
            std::map<std::string, std::string> subst;
            for (int i = 0; i < (int)tmpl.type_params.size(); i++)
                subst[tmpl.type_params[i]] = resolved.type_args[i];
            std::string rt = tmpl.return_type;
            auto it2 = subst.find(rt);
            if (it2 != subst.end()) rt = it2->second;
            if (slid_info_.count(rt)) return "ptr";
            return llvmType(rt);
        }
        // check if already instantiated
        std::string mangled = resolveFreeFunctionMangledName(ce->callee, ce->args.size());
        if (!mangled.empty()) {
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end()) return llvmType(rit->second);
        }
        return "i32";
    }

    // float literal — always double internally
    if (dynamic_cast<const FloatLiteralExpr*>(&expr)) return "double";

    // type conversion — result is the target type
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr))
        return llvmType(nc->target_type);

    // pointer reinterpret cast — result is the target type
    if (auto* pc = dynamic_cast<const PtrCastExpr*>(&expr))
        return llvmType(pc->target_type);

    // qualifier-only cast — result LLVM type matches the operand (qualifier
    // strips through canonType in llvmType).
    if (auto* qc = dynamic_cast<const QualifierCastExpr*>(&expr))
        return exprLlvmType(*qc->operand);

    // sizeof always returns intptr (i64 on 64-bit)
    if (dynamic_cast<const SizeofExpr*>(&expr)) return "i64";

    return "i32";
}

std::string Codegen::emitToBool(const std::string& val, const std::string& llvm_type) {
    std::string b = newTmp();
    if (llvm_type == "ptr") {
        out_ << "    " << b << " = icmp ne ptr " << val << ", null\n";
    } else if (llvm_type == "float" || llvm_type == "double") {
        out_ << "    " << b << " = fcmp une " << llvm_type << " " << val << ", 0.0\n";
    } else {
        out_ << "    " << b << " = icmp ne " << llvm_type << " " << val << ", 0\n";
    }
    return b;
}

std::string Codegen::emitCondBool(const Expr& expr) {
    std::string val = emitExpr(expr);
    // comparison/logical ops already produce an i32 0 or 1 via zext —
    // just truncate back to i1 without a redundant icmp.
    if (isAlreadyBool(expr)) {
        std::string cond_bool = newTmp();
        out_ << "    " << cond_bool << " = trunc i32 " << val << " to i1\n";
        return cond_bool;
    }
    return emitToBool(val, exprLlvmType(expr));
}

// When dst is a pointer (^) or iterator ([]) type, the source expression must
// produce an LLVM ptr value. Reject scalar->pointer (e.g. `int[] p = arr[0];`),
// which would otherwise emit `store ptr %i32val, ptr %dst` — invalid IR.
void Codegen::validateBinaryPtrRefRestrictions(const BinaryExpr& b) {
    auto getVarSlidsType = [&](const Expr& e) -> std::string {
        if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) return tit->second.type;
        }
        return "";
    };
    std::string lslids = getVarSlidsType(*b.left);
    std::string rslids = getVarSlidsType(*b.right);
    bool left_ref  = isRefType(lslids);
    bool right_ref = isRefType(rslids);
    bool left_iter = isPtrType(lslids);
    bool right_iter = isPtrType(rslids);
    if (!left_ref && !right_ref && !left_iter && !right_iter) return;

    static const std::set<std::string> arith_ops_pr =
        {"+","-","*","/","%","&","|","^","<<",">>"};
    static const std::set<std::string> ord_ops_pr = {"<","<=",">",">="};

    // Cross: one pointer ([]) and one reference (^). Allow == / != only.
    bool cross = (left_iter && right_ref) || (left_ref && right_iter);
    if (cross) {
        const std::string& iter_t = left_iter ? lslids : rslids;
        const std::string& ref_t  = left_ref  ? lslids : rslids;
        if (b.op != "==" && b.op != "!=") {
            if (ord_ops_pr.count(b.op))
                error(std::string("Operator '" + b.op + "' between pointer '" + iter_t
                    + "' and reference '" + ref_t
                    + "' is not allowed (use '==' or '!=')"));
            if (arith_ops_pr.count(b.op))
                error(std::string("Operator '" + b.op + "' between pointer '" + iter_t
                    + "' and reference '" + ref_t + "' is not allowed"));
        }
    }
    // Reference-only: split arithmetic vs ordered-comparison wording.
    if (left_ref || right_ref) {
        const std::string& which = left_ref ? lslids : rslids;
        if (arith_ops_pr.count(b.op))
            error(std::string("Operator '" + b.op + "' on reference '" + which
                + "': arithmetic on references is not allowed (use a pointer '[]' type)"));
        if (ord_ops_pr.count(b.op))
            error(std::string("Operator '" + b.op + "' is not allowed on reference type '"
                + which + "': references only support '==' and '!='"));
        // == / != require same base type (strip either ^ or [] suffix).
        auto refBase = [](const std::string& t) -> std::string {
            if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return t.substr(0, t.size()-2);
            if (!t.empty() && t.back() == '^') return t.substr(0, t.size()-1);
            return t;
        };
        std::string lb = refBase(lslids), rb = refBase(rslids);
        if (!lb.empty() && !rb.empty() && lb != "void" && rb != "void" && lb != rb)
            error(std::string("Reference comparison requires same type: '"
                + lslids + "' vs '" + rslids + "'"));
    }
}

void Codegen::requireCompatibleInit(const std::string& dst_type, const Expr& src) {
    // Indirect (^/[]) lhs is handled by requirePtrInit's pointer-base check.
    if (isIndirectType(dst_type)) return;
    std::string src_slid = exprSlidType(src);
    if (src_slid.empty() || !slid_info_.count(src_slid)) return;
    // primitive dst ← slid rhs: no implicit slid-to-primitive conversion.
    // slid dst ← different slid rhs: no implicit slid-to-slid conversion.
    if (!slid_info_.count(dst_type) || dst_type != src_slid) {
        errorAtNode(src, "Cannot initialize '" + dst_type
                       + "' from a value of type '" + src_slid + "'.");
    }
}

std::string Codegen::valOrNullptrCheck(const std::string& dst_type, const Expr& src) {
    if (dynamic_cast<const NullptrExpr*>(&src)) {
        if (isIndirectType(dst_type)) return "null";
        if (dst_type == "intptr") return "0";
        errorAtNode(src, "Cannot assign nullptr to type '" + dst_type + "'.");
    }
    return emitExpr(src);
}

void Codegen::requirePtrInit(const std::string& dst_type, const Expr& src) {
    if (!isRefType(dst_type) && !isPtrType(dst_type)) {
        // primitive dst: still need to reject slid-valued rhs (no implicit conversion).
        requireCompatibleInit(dst_type, src);
        return;
    }
    std::string src_t = exprLlvmType(src);
    if (src_t != "ptr") {
        std::string src_slids = inferSlidType(src);
        error(std::string("Cannot initialize '" + dst_type
            + "' from value of type '" + src_slids + "'"));
    }
    // slids-level pointer-base compatibility. `pointer → void^` is implicit
    // (stripping); `void^ → typed pointer` requires an explicit cast. nullptr
    // (internal type "anyptr") carries no type info and is always implicit.
    std::string src_slids = inferSlidType(src);
    if (src_slids == "anyptr") return;
    auto ptrBase = [](const std::string& t) -> std::string {
        if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return t.substr(0, t.size()-2);
        if (!t.empty() && t.back() == '^') return t.substr(0, t.size()-1);
        return "";
    };
    std::string dst_base_raw = ptrBase(dst_type);
    std::string src_base_raw = ptrBase(src_slids);
    if (dst_base_raw.empty() || src_base_raw.empty()) return;
    // Compare bases canonically (strip qualifiers) for type-shape matching;
    // const-direction is enforced separately below so `const T^ ← T^` (adding
    // const) is fine but `T^ ← const T^` (stripping const) is rejected.
    std::string dst_base = baseSlidType(dst_base_raw);
    std::string src_base = baseSlidType(src_base_raw);
    if (dst_base != "void"
        && dst_base != src_base
        && !isAncestor(dst_base, src_base))
        error(std::string("Cannot initialize '" + dst_type
            + "' from value of type '" + src_slids + "'"));
    // Const-strip rejection: src pointee carries const, dst pointee doesn't.
    if (typeHasConst(src_base_raw) && !typeHasConst(dst_base_raw))
        error(std::string("Cannot initialize '" + dst_type
            + "' from value of type '" + src_slids
            + "' — stripping const is not allowed."));
    // reference cannot promote to iterator
    if (isPtrType(dst_type) && isRefType(src_slids)
        && dst_base != "void" && src_base != "void")
        error(std::string("Cannot initialize iterator '" + dst_type
            + "' from reference '" + src_slids
            + "': references cannot promote to iterators"));
}

// Infer the Slids type string for a type-inferred variable declaration (x = expr;).
// Returns a Slids type string like "int", "int64", "uint", "float64", "char[]", etc.
std::string Codegen::inferSlidType(const Expr& expr) {
    // integer literal
    if (auto* ile = dynamic_cast<const IntLiteralExpr*>(&expr)) {
        if (ile->is_char_literal) return "char";
        if (ile->is_nondecimal) {
            // hex/binary/octal: prefer uint (uint32), else uint64
            uint64_t uval = static_cast<uint64_t>(ile->value);
            return (uval <= 0xFFFFFFFFull) ? "uint" : "uint64";
        }
        // decimal: prefer int (int32), else int64, else uint64
        int64_t val = ile->value;
        if (val >= -2147483648LL && val <= 2147483647LL) return "int";
        return "int64";
    }
    // float literal → float64
    if (dynamic_cast<const FloatLiteralExpr*>(&expr)) return "float64";
    // string literal → char[]
    if (dynamic_cast<const StringLiteralExpr*>(&expr)) return "char[]";
    // stringify → char[]
    if (dynamic_cast<const StringifyExpr*>(&expr)) return "char[]";
    // nullptr → anyptr (internal: a pointer with no type information,
    // implicitly assignable to any pointer type per spec rule 1)
    if (dynamic_cast<const NullptrExpr*>(&expr)) return "anyptr";
    // tuple literal (a, b, c) → anonymous tuple type (ta,tb,tc)
    if (auto* te = dynamic_cast<const TupleExpr*>(&expr)) {
        std::string s = "(";
        for (int i = 0; i < (int)te->values.size(); i++) {
            if (i > 0) s += ",";
            s += inferSlidType(*te->values[i]);
        }
        return s + ")";
    }
    // new T[n] → T[];  new T(args) and new(addr) T(args) → T^
    if (auto t = newExprResultType(expr); !t.empty()) return t;
    // ^x → elem_type^ (take address). When the operand is an array element
    // (ArrayIndexExpr whose deepest base is an array local), produce an
    // iterator T[] instead — the storage is walkable, so arithmetic on the
    // resulting pointer is well-defined.
    if (auto* ae = dynamic_cast<const AddrOfExpr*>(&expr)) {
        const Expr* cur = ae->operand.get();
        while (auto* aie = dynamic_cast<const ArrayIndexExpr*>(cur))
            cur = aie->base.get();
        if (cur != ae->operand.get()) {
            if (auto* ve = dynamic_cast<const VarExpr*>(cur)) {
                auto ait = array_info_.find(ve->name);
                if (ait != array_info_.end())
                    return ait->second.elem_type + "[]";
            }
        }
        std::string inner = inferSlidType(*ae->operand);
        if (inner.empty()) return "^";
        if (typeStartsWithConst(inner)) return "(" + inner + ")^";
        return inner + "^";
    }
    // type conversion — use the target type
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr)) return nc->target_type;
    // pointer reinterpret cast — use the target type
    if (auto* pc = dynamic_cast<const PtrCastExpr*>(&expr)) return pc->target_type;
    // qualifier-only cast — apply the qualifier on the operand's slid type.
    // <const> wraps the operand's type with a leading `const `; <mutable>
    // strips a leading `const ` if present (otherwise no-op).
    if (auto* qc = dynamic_cast<const QualifierCastExpr*>(&expr)) {
        std::string inner = inferSlidType(*qc->operand);
        if (qc->qualifier == "const") {
            if (inner.rfind("const ", 0) == 0) return inner;
            return "const " + inner;
        }
        // mutable: strip a single leading `const ` if present
        if (inner.rfind("const ", 0) == 0) return inner.substr(6);
        return inner;
    }
    // variable — look up its declared type, then fall back to current slid's fields
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        if (auto* ce = lookupConst(ve->name)) return ce->slid_type;
        auto it = locals_.find(ve->name);
        if (it != locals_.end()) return it->second.type;
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                return info.field_types[fit->second];
        }
        // type name used as anonymous temporary (e.g. ValueBoth in ValueBoth + 10)
        if (slid_info_.count(ve->name)) return ve->name;
    }
    // field access — resolve object's slid type, return raw field type
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            std::string recv_slid;
            auto lit = locals_.find(ve->name);
            if (lit != locals_.end()) recv_slid = lit->second.type;
            else if (slid_info_.count(ve->name)) recv_slid = ve->name;
            while (!recv_slid.empty() && recv_slid.back() == '^')
                recv_slid.pop_back();
            if (recv_slid.size() >= 2
                && recv_slid.substr(recv_slid.size()-2) == "[]")
                recv_slid.resize(recv_slid.size()-2);
            if (!recv_slid.empty()) {
                if (auto* ce = lookupSlidConst(recv_slid, fa->field))
                    return ce->slid_type;
            }
        }
        std::string slid_name;
        bool obj_is_const = false;
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            // derefSlidName canonicalizes but drops the const bit. Recover the
            // const-ness from the pointer's type so field access propagates it.
            slid_name = derefSlidName(*de);
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) {
                    auto pi = pointeeInfo(tit->second.type);
                    if (pi.is_const) obj_is_const = true;
                }
            }
        } else if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) slid_name = tit->second.type;
        } else if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(fa->object.get())) {
            if (auto* bve = dynamic_cast<const VarExpr*>(ai->base.get())) {
                auto tit = locals_.find(bve->name);
                if (tit != locals_.end() && isAnonTupleType(tit->second.type)) {
                    auto elems = anonTupleElems(tit->second.type);
                    int idx;
                    if (constExprToInt(*ai->index, enum_values_, idx)
                            && idx >= 0 && idx < (int)elems.size())
                        slid_name = elems[idx];
                }
            }
        }
        if (!slid_name.empty()) {
            // Field-read in rvalue context: const on the object qualifies the
            // lvalue (write-rejection lives in exprType / write checks). The
            // read produces a plain value of the field type — copying out of
            // a const lvalue yields a mutable rvalue.
            (void)obj_is_const;
            if (typeStartsWithConst(slid_name))
                slid_name = baseSlidType(slid_name);
            if (slid_info_.count(slid_name)) {
                auto& info = slid_info_[slid_name];
                auto fit = info.field_index.find(fa->field);
                if (fit != info.field_index.end())
                    return info.field_types[fit->second];
            }
        }
    }
    // free function call — look up return type
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        // slid ctor call: SlidName(args) → type is SlidName
        if (slid_info_.count(ce->callee)) return ce->callee;
        auto resolved = resolveTemplateOverload(ce->callee, ce->type_args, ce->args);
        if (resolved.entry) {
            const FunctionDef& tmpl = *resolved.entry->def;
            std::map<std::string, std::string> subst;
            for (int i = 0; i < (int)tmpl.type_params.size(); i++)
                subst[tmpl.type_params[i]] = resolved.type_args[i];
            auto it2 = subst.find(tmpl.return_type);
            return it2 != subst.end() ? it2->second : tmpl.return_type;
        }
        // tuple-returning function: reconstruct Slids form "(t1,t2,...)"
        std::string mangled = resolveFreeFunctionMangledName(ce->callee, ce->args.size());
        if (!mangled.empty()) {
            auto tfit = func_tuple_fields_.find(mangled);
            if (tfit != func_tuple_fields_.end()) {
                std::string s = "(";
                for (int i = 0; i < (int)tfit->second.size(); i++) {
                    if (i > 0) s += ",";
                    s += tfit->second[i].first;
                }
                return s + ")";
            }
            auto it = func_return_types_.find(mangled);
            if (it != func_return_types_.end()) return it->second;
        }
    }
    // method call — look up return type via slid type of object
    if (auto* me = dynamic_cast<const MethodCallExpr*>(&expr)) {
        std::string slid_type = exprSlidType(*me->object);
        if (!slid_type.empty()) {
            std::string mangled = slid_type + "__" + me->method;
            auto tfit = func_tuple_fields_.find(mangled);
            if (tfit != func_tuple_fields_.end()) {
                std::string s = "(";
                for (int i = 0; i < (int)tfit->second.size(); i++) {
                    if (i > 0) s += ",";
                    s += tfit->second[i].first;
                }
                return s + ")";
            }
            auto it = func_return_types_.find(mangled);
            if (it != func_return_types_.end()) return it->second;
        }
    }
    // dereference ptr^ → element type (strip trailing ^ or [])
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        std::string pt = inferSlidType(*de->operand);
        if (pt.size() >= 2 && pt.substr(pt.size()-2) == "[]")
            return stripRedundantConstParens(pt.substr(0, pt.size()-2));
        if (!pt.empty() && pt.back() == '^')
            return stripRedundantConstParens(pt.substr(0, pt.size()-1));
        return pt;
    }
    // array index — drill the chain layer by layer, mirroring the runtime
    // dispatch in resolveLvalue. The deepest non-AIE base seeds the type;
    // a fixed-size native array consumes up to dims.size() indices flat
    // (the rest drill through the element type).
    if (auto* aie = dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        (void)aie;
        std::vector<const Expr*> indices;
        const Expr* cur = &expr;
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
            indices.insert(indices.begin(), a->index.get());
            cur = a->base.get();
        }
        std::string type;
        int consumed = 0;
        if (auto* ve = dynamic_cast<const VarExpr*>(cur)) {
            auto ait = array_info_.find(ve->name);
            if (ait != array_info_.end()) {
                type = ait->second.elem_type;
                consumed = std::min((int)indices.size(), (int)ait->second.dims.size());
            }
        }
        if (consumed == 0) type = inferSlidType(*cur);
        return drillIndexedType(type, indices, consumed);
    }
    // binary expression — use exprSlidType if it produces a slid, else infer from left
    if (auto* be = dynamic_cast<const BinaryExpr*>(&expr)) {
        validateBinaryPtrRefRestrictions(*be);
        std::string slid = exprSlidType(expr);
        if (!slid.empty()) return slid;
        // comparison and logical operators always produce bool, not the operand type
        if (be->op == "==" || be->op == "!=" || be->op == "<" ||
            be->op == ">"  || be->op == "<=" || be->op == ">=" ||
            be->op == "&&" || be->op == "||" || be->op == "^^")
            return "bool";
        // elementwise tuple binary op (including scalar broadcast): result type is the
        // tuple operand's type, regardless of which side it appears on.
        std::string lt = inferSlidType(*be->left);
        std::string rt = inferSlidType(*be->right);
        if (isAnonTupleType(lt)) return lt;
        if (isAnonTupleType(rt)) return rt;
        return lt;
    }
    // unary — propagate through, except arity-0 slid dispatch returns the method's type
    if (auto* ue = dynamic_cast<const UnaryExpr*>(&expr)) {
        if (ue->op == "-" || ue->op == "+" || ue->op == "~" || ue->op == "!") {
            std::string operand_slid = inferSlidType(*ue->operand);
            if (!operand_slid.empty() && slid_info_.count(operand_slid)) {
                auto moit = method_overloads_.find(operand_slid + "__op" + ue->op);
                if (moit != method_overloads_.end())
                    for (auto& [m, ptypes, _pm, _pmt, _fid] : moit->second)
                        if (ptypes.empty()) {
                            auto rit = func_return_types_.find(m);
                            return rit != func_return_types_.end() ? rit->second : "";
                        }
            }
        }
        if (ue->op == "!") return "bool";
        if (ue->op == "-" || ue->op == "~")
            return inferSlidType(*ue->operand);
        // pre/post inc/dec — same type as operand (used by DerefExpr / DerefAssign
        // to derive pointee type when the natural form is DerefExpr(UnaryExpr(post++,...))).
        if (ue->op == "pre++" || ue->op == "pre--"
            || ue->op == "post++" || ue->op == "post--")
            return inferSlidType(*ue->operand);
    }
    // sizeof → intptr
    if (dynamic_cast<const SizeofExpr*>(&expr)) return "intptr";
    // default: int
    return "int";
}
