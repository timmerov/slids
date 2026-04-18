#include "codegen.h"
#include "codegen_helpers.h"
#include <sstream>
#include <functional>
#include <stdexcept>

std::string Codegen::emitExpr(const Expr& expr) {
    if (auto* i = dynamic_cast<const IntLiteralExpr*>(&expr))
        return std::to_string(i->value);

    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
        // self — pointer to the current object
        if (v->name == "self" && !current_slid_.empty())
            return self_ptr_.empty() ? "%self" : self_ptr_;
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
            throw std::runtime_error("undefined variable: " + v->name);
        }
        std::string tmp = newTmp();
        auto tit = local_types_.find(v->name);
        std::string load_type = (tit != local_types_.end()) ? llvmType(tit->second) : "i32";
        out_ << "    " << tmp << " = load " << load_type << ", ptr " << it->second << "\n";
        return tmp;
    }

    if (dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        // support chained indexing: base[i][j] — base may be another ArrayIndexExpr
        // collect index chain and base name
        std::vector<const Expr*> indices;
        const Expr* cur = &expr;
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
            indices.insert(indices.begin(), a->index.get());
            cur = a->base.get();
        }
        // cur is now the root VarExpr
        auto* ve = dynamic_cast<const VarExpr*>(cur);
        if (!ve) throw std::runtime_error("complex array base not supported");

        // fixed-size local array: use flat alloca GEP
        auto ait = array_info_.find(ve->name);
        if (ait != array_info_.end()) {
            auto& ainfo = ait->second;
            std::string flat = emitExpr(*indices[0]);
            for (int k = 1; k < (int)indices.size(); k++) {
                int stride = ainfo.dims[k];
                std::string mul = newTmp();
                out_ << "    " << mul << " = mul i32 " << flat << ", " << std::to_string(stride) << "\n";
                std::string idx_val = emitExpr(*indices[k]);
                std::string add = newTmp();
                out_ << "    " << add << " = add i32 " << mul << ", " << idx_val << "\n";
                flat = add;
            }
            int total = 1;
            for (int d : ainfo.dims) total *= d;
            std::string elt = llvmType(ainfo.elem_type);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr [" << total << " x " << elt << "], ptr "
                 << ainfo.alloca_reg << ", i32 0, i32 " << flat << "\n";
            std::string val = newTmp();
            out_ << "    " << val << " = load " << elt << ", ptr " << gep << "\n";
            return val;
        }

        // pointer-type indexing: char[], int[], etc. — either a field or a local ptr var
        std::string base_ptr;
        std::string elem_type_str;
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
            throw std::runtime_error("undefined array: " + ve->name);

        std::string idx_val = emitExpr(*indices[0]);
        std::string elt = llvmType(elem_type_str);
        std::string gep = newTmp();
        out_ << "    " << gep << " = getelementptr " << elt << ", ptr " << base_ptr << ", i32 " << idx_val << "\n";
        std::string val = newTmp();
        out_ << "    " << val << " = load " << elt << ", ptr " << gep << "\n";
        return val;
    }

    if (auto* ao = dynamic_cast<const AddrOfExpr*>(&expr)) {
        // ^x — return the alloca register (its address)
        if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
            // ^self — address of the current object (implicit method parameter)
            if (ve->name == "self")
                return self_ptr_.empty() ? "%self" : self_ptr_;
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                throw std::runtime_error("AddrOf: undefined variable '" + ve->name + "'");
            return it->second;
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
            if (!ve) throw std::runtime_error("AddrOf: complex array base not supported");
            auto ait = array_info_.find(ve->name);
            if (ait == array_info_.end())
                throw std::runtime_error("AddrOf: undefined array '" + ve->name + "'");
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
        throw std::runtime_error("AddrOf: unsupported operand");
    }

    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        // ptr^ — first load the pointer from its alloca, then load through it
        std::string pointee_llvm = "i32"; // default pointee type
        std::string ptr_reg;

        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                throw std::runtime_error("DerefExpr: undefined variable '" + ve->name + "'");
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && isIndirectType(tit->second)) {
                // variable holds a reference or pointer — load the ptr, then load through it
                std::string loaded_ptr = newTmp();
                out_ << "    " << loaded_ptr << " = load ptr, ptr " << it->second << "\n";
                ptr_reg = loaded_ptr;
                std::string pointee_type = ( isPtrType(tit->second) ? tit->second.substr(0, tit->second.size()-2) : tit->second.substr(0, tit->second.size()-1) );
                pointee_llvm = llvmType(pointee_type);
            } else {
                std::string type_name = (tit != local_types_.end()) ? tit->second : "unknown";
                throw std::runtime_error(
                    "cannot dereference '" + ve->name + "' of type '" + type_name +
                    "': only reference (^) and pointer ([]) types can be dereferenced");
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
            }
        }

        std::string tmp = newTmp();
        out_ << "    " << tmp << " = load " << pointee_llvm << ", ptr " << ptr_reg << "\n";
        return tmp;
    }

    if (auto* pide = dynamic_cast<const PostIncDerefExpr*>(&expr)) {
        // ptr++^ — load value at current ptr, then advance ptr
        auto* ve = dynamic_cast<const VarExpr*>(pide->operand.get());
        if (!ve) throw std::runtime_error("PostIncDerefExpr: only simple pointer variables supported");
        auto it = locals_.find(ve->name);
        if (it == locals_.end())
            throw std::runtime_error("PostIncDerefExpr: undefined variable '" + ve->name + "'");
        auto tit = local_types_.find(ve->name);
        if (tit == local_types_.end() || !isPtrType(tit->second))
            throw std::runtime_error("PostIncDerefExpr: '" + ve->name + "' is not a pointer ([]) type");
        std::string pointee_type = tit->second.substr(0, tit->second.size()-2);
        std::string pointee_llvm = llvmType(pointee_type);
        int step = (pide->op == "++") ? 1 : -1;
        // load current ptr
        std::string cur_ptr = newTmp();
        out_ << "    " << cur_ptr << " = load ptr, ptr " << it->second << "\n";
        // load value at current ptr
        std::string val = newTmp();
        out_ << "    " << val << " = load " << pointee_llvm << ", ptr " << cur_ptr << "\n";
        // advance ptr
        std::string new_ptr = newTmp();
        out_ << "    " << new_ptr << " = getelementptr " << pointee_llvm << ", ptr " << cur_ptr << ", i32 " << step << "\n";
        out_ << "    store ptr " << new_ptr << ", ptr " << it->second << "\n";
        return val;
    }

    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        // handle ptr^.field — object is a DerefExpr
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
                throw std::runtime_error("DerefFieldAccess: unknown slid type for field '" + fa->field + "'");
            auto& info = slid_info_[slid_name];
            auto fit = info.field_index.find(fa->field);
            if (fit == info.field_index.end())
                throw std::runtime_error("unknown field: " + fa->field);
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
            auto type_it = local_types_.find(ve->name);
            std::string slid_name = type_it->second;
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            std::string field_type = llvmType(info.field_types[idx]);
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = load " << field_type << ", ptr " << gep << "\n";
            return tmp;
        }
        throw std::runtime_error("complex field access not yet supported");
    }

    if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
        // helper to get slid_name and obj_ptr from any object expression
        std::string slid_name, obj_ptr;
        if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
            auto type_it = local_types_.find(ve->name);
            if (type_it == local_types_.end())
                throw std::runtime_error("unknown type for: " + ve->name);
            slid_name = type_it->second;
            obj_ptr = locals_[ve->name];
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mc->object.get())) {
            // ptr^.method() — load the pointer, use as self
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto type_it = local_types_.find(ve2->name);
                if (type_it == local_types_.end())
                    throw std::runtime_error("unknown type for: " + ve2->name);
                slid_name = type_it->second;
                if (isRefType(slid_name)) slid_name.pop_back(); else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                // load the pointer value from the alloca
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << locals_[ve2->name] << "\n";
                obj_ptr = loaded;
            }
        }
        if (!slid_name.empty()) {
            std::string base = slid_name + "__" + mc->method;
            std::string mangled = resolveOverloadForCall(base, mc->args);
            auto ret_it = func_return_types_.find(mangled);
            if (ret_it == func_return_types_.end())
                throw std::runtime_error("unknown method: " + mc->method);
            std::string ret_type = llvmType(ret_it->second);
            std::string arg_str = "ptr " + obj_ptr;
            auto& mptypes = func_param_types_[mangled];
            for (int i = 0; i < (int)mc->args.size(); i++) {
                std::string ptype = (i < (int)mptypes.size()) ? llvmType(mptypes[i]) : "i32";
                arg_str += ", " + ptype + " " + emitExpr(*mc->args[i]);
            }
            std::string tmp = newTmp();
            out_ << "    " << tmp << " = call " << ret_type << " @" << mangled
                 << "(" << arg_str << ")\n";
            return tmp;
        }
        throw std::runtime_error("complex method call not yet supported");
    }

    if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
        // check nested function first
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

        // template call: instantiate and emit
        if (!call->type_args.empty()) {
            std::string mangled = instantiateTemplate(call->callee, call->type_args);
            std::string ret_slid = func_return_types_[mangled];
            auto& ptypes = func_param_types_[mangled];
            std::string arg_str;
            for (int i = 0; i < (int)call->args.size(); i++) {
                if (i > 0) arg_str += ", ";
                std::string ptype = (i < (int)ptypes.size()) ? ptypes[i] : "int";
                arg_str += llvmType(ptype) + " " + emitArgForParam(*call->args[i], ptype);
            }
            // sret if return type is a slid
            if (slid_info_.count(ret_slid)) {
                std::string tmp = "%tmp_" + std::to_string(tmp_counter_++);
                out_ << "    " << tmp << " = alloca %struct." << ret_slid << "\n";
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

        // regular function call
        auto it = func_return_types_.find(call->callee);
        if (it == func_return_types_.end())
            throw std::runtime_error("undefined function: " + call->callee);
        auto& ptypes = func_param_types_[call->callee];
        std::string arg_str;
        for (int i = 0; i < (int)call->args.size(); i++) {
            if (i > 0) arg_str += ", ";
            std::string ptype_str = (i < (int)ptypes.size()) ? ptypes[i] : "";
            std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
            arg_str += ptype + " " + emitArgForParam(*call->args[i], ptype_str);
        }
        // sret: callee returns a slid type — allocate temp, call with ptr sret, return temp
        if (slid_info_.count(it->second)) {
            std::string tmp = "%tmp_" + std::to_string(tmp_counter_++);
            out_ << "    " << tmp << " = alloca %struct." << it->second << "\n";
            std::string sret_arg = "ptr sret(%struct." + it->second + ") " + tmp;
            if (!arg_str.empty()) sret_arg += ", " + arg_str;
            out_ << "    call void @" << llvmGlobalName(call->callee) << "(" << sret_arg << ")\n";
            return tmp;
        }
        std::string ret_type = llvmType(it->second);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(call->callee)
             << "(" << arg_str << ")\n";
        return tmp;
    }

    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
        // pre/post increment/decrement — handle before evaluating operand
        if (u->op == "pre++" || u->op == "pre--" ||
            u->op == "post++" || u->op == "post--") {

            bool is_pre = (u->op == "pre++" || u->op == "pre--");
            std::string instr = (u->op == "pre++" || u->op == "post++") ? "add" : "sub";
            std::string ptr;

            // dereference: ++(ref^) or ref^++ — increment the value pointed to
            if (auto* de = dynamic_cast<const DerefExpr*>(u->operand.get())) {
                // emit the pointer value itself (load the ptr variable)
                std::string ptr_val = emitExpr(*de->operand);
                std::string old_val = newTmp();
                out_ << "    " << old_val << " = load i32, ptr " << ptr_val << "\n";
                std::string new_val = newTmp();
                out_ << "    " << new_val << " = " << instr << " i32 " << old_val << ", 1\n";
                out_ << "    store i32 " << new_val << ", ptr " << ptr_val << "\n";
                return is_pre ? new_val : old_val;
            }

            // field access via self in a method
            if (!current_slid_.empty()) {
                auto* ve = dynamic_cast<const VarExpr*>(u->operand.get());
                if (ve) {
                    auto& info = slid_info_[current_slid_];
                    if (info.field_index.count(ve->name)) {
                        int idx = info.field_index[ve->name];
                        std::string gep = newTmp();
                        std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                        out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                             << ", ptr " << self << ", i32 0, i32 " << idx << "\n";
                        ptr = gep;
                    }
                }
            }

            // plain local variable
            if (ptr.empty()) {
                auto* ve = dynamic_cast<const VarExpr*>(u->operand.get());
                if (!ve) throw std::runtime_error("++/-- requires a variable");
                auto it = locals_.find(ve->name);
                if (it == locals_.end()) throw std::runtime_error("undefined variable: " + ve->name);
                ptr = it->second;
            }

            // check if the variable is a pointer type ([] — arithmetic allowed)
            // or a reference type (^ — arithmetic is a compile error)
            bool is_ptr_arith = false;
            std::string pointee_llvm = "i32";
            if (auto* ve = dynamic_cast<const VarExpr*>(u->operand.get())) {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) {
                    if (isRefType(tit->second))
                        throw std::runtime_error(
                            "'" + u->op + "' on reference '" + ve->name +
                            "': arithmetic on references is not allowed (use a pointer '[]' type)");
                    if (isPtrType(tit->second)) {
                        is_ptr_arith = true;
                        std::string pointee_type = tit->second.substr(0, tit->second.size()-2);
                        pointee_llvm = llvmType(pointee_type);
                    }
                }
            }

            std::string old = newTmp();
            std::string new_val = newTmp();
            if (is_ptr_arith) {
                // pointer arithmetic: load ptr, GEP ±1, store back
                out_ << "    " << old << " = load ptr, ptr " << ptr << "\n";
                int step = (instr == "add") ? 1 : -1;
                out_ << "    " << new_val << " = getelementptr " << pointee_llvm << ", ptr " << old << ", i32 " << step << "\n";
                out_ << "    store ptr " << new_val << ", ptr " << ptr << "\n";
            } else {
                out_ << "    " << old << " = load i32, ptr " << ptr << "\n";
                out_ << "    " << new_val << " = " << instr << " i32 " << old << ", 1\n";
                out_ << "    store i32 " << new_val << ", ptr " << ptr << "\n";
            }
            return is_pre ? new_val : old;
        }
        // other unary ops — evaluate operand first
        std::string val = emitExpr(*u->operand);
        std::string tmp = newTmp();
        if (u->op == "!") {
            out_ << "    " << tmp << " = icmp eq i32 " << val << ", 0\n";
            std::string tmp2 = newTmp();
            out_ << "    " << tmp2 << " = zext i1 " << tmp << " to i32\n";
            return tmp2;
        }
        if (u->op == "~") {
            out_ << "    " << tmp << " = xor i32 " << val << ", -1\n";
            return tmp;
        }
        throw std::runtime_error("unknown unary op: " + u->op);
    }

    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        if (b->op == "&&" || b->op == "||" || b->op == "^^") {
            std::string result_ptr = newTmp() + "_sc";
            out_ << "    " << result_ptr << " = alloca i32\n";
            std::string left_val = emitExpr(*b->left);
            std::string left_bool = newTmp();
            std::string left_type = exprLlvmType(*b->left);
            out_ << "    " << left_bool << " = icmp ne " << left_type << " " << left_val << ", 0\n";
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
            std::string right_val = emitExpr(*b->right);
            std::string right_bool = newTmp();
            std::string right_type = exprLlvmType(*b->right);
            out_ << "    " << right_bool << " = icmp ne " << right_type << " " << right_val << ", 0\n";
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

        // operator overload — in-class method (op+(sa,sb) stored in self) or free function (sret)
        // skip op+ when op+= is also defined: Phase 3 handles it more efficiently
        {
            std::string op_func = resolveOperatorOverload(b->op, *b->left, *b->right);
            if (!op_func.empty()) {
                std::string left_slid = exprSlidType(*b->left);
                if (!left_slid.empty()) {
                    std::string compound_base = left_slid + "__op" + b->op + "=";
                    if (method_overloads_.count(compound_base)
                            && !resolveOpEq(compound_base, *b->right).empty())
                        op_func = ""; // defer to Phase 3
                }
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
                    auto& info = slid_info_[res_slid];
                    // alloca a temp struct for the result
                    std::string tmp_alloca = newTmp();
                    out_ << "    " << tmp_alloca << " = alloca %struct." << res_slid << "\n";
                    // zero-init fields
                    for (int i = 0; i < (int)info.field_types.size(); i++) {
                        std::string ft = llvmType(info.field_types[i]);
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr %struct." << res_slid << ", ptr " << tmp_alloca << ", i32 0, i32 " << i << "\n";
                        if (isIndirectType(info.field_types[i]))
                            out_ << "    store ptr null, ptr " << gep << "\n";
                        else
                            out_ << "    store " << ft << " 0, ptr " << gep << "\n";
                    }
                    // init: pinit (for imported transport types) or ctor
                    if (info.has_pinit && !info.is_transport_impl)
                        out_ << "    call void @" << res_slid << "__pinit(ptr " << tmp_alloca << ")\n";
                    else if (info.has_explicit_ctor)
                        out_ << "    call void @" << res_slid << "__ctor(ptr " << tmp_alloca << ")\n";
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
            }
        }

        // Phase 2: implicit right-operand coercion
        // left is a slid type, right doesn't match any overload — try coercing right via op=
        // skip when op+= is available: Phase 3 handles it more efficiently
        {
            std::string left_slid = exprSlidType(*b->left);
            if (!left_slid.empty()) {
                std::string coerce_mangled = resolveOpEq(left_slid + "__op=", *b->right);
                if (!coerce_mangled.empty()) {
                    // skip if op+= also matches
                    std::string compound_base = left_slid + "__op" + b->op + "=";
                    if (method_overloads_.count(compound_base)
                            && !resolveOpEq(compound_base, *b->right).empty())
                        coerce_mangled = ""; // defer to Phase 3
                }
                if (!coerce_mangled.empty()) {
                    // find the op overload on left_slid that accepts (left_slid^, left_slid^)
                    std::string op_func;
                    auto moit = method_overloads_.find(left_slid + "__op" + b->op);
                    if (moit != method_overloads_.end()) {
                        for (auto& [m, ptypes] : moit->second) {
                            if (ptypes.size() >= 2
                                && isRefType(ptypes[1])
                                && ptypes[1].substr(0, ptypes[1].size()-1) == left_slid) {
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

        // Phase 3: op+= fallback — no op+ defined, but op+= exists
        // semantics: temp = copy(left); temp op= right; return temp
        // optimization: if left is a fresh temp we own, skip the copy and call op+= in place
        {
            std::string left_slid = exprSlidType(*b->left);
            if (!left_slid.empty()) {
                std::string compound_base = left_slid + "__op" + b->op + "=";
                auto compound_it = method_overloads_.find(compound_base);
                if (compound_it != method_overloads_.end()) {
                    std::string compound_mangled = resolveOpEq(compound_base, *b->right);
                    if (!compound_mangled.empty()) {
                        std::string res_tmp;
                        if (isFreshSlidTemp(*b->left)) {
                            // left is a fresh temp we own — call op+= on it directly
                            res_tmp = emitExpr(*b->left);
                        } else {
                            // left is a named variable — alloca new temp and copy left into it
                            res_tmp = emitSlidAlloca(left_slid);
                            std::string copy_mangled = resolveOpEq(left_slid + "__op=", *b->left);
                            if (!copy_mangled.empty()) {
                                auto& cptypes = func_param_types_[copy_mangled];
                                std::string carg = emitArgForParam(*b->left,
                                    cptypes.empty() ? "" : cptypes[0]);
                                std::string cptype = cptypes.empty() ? "ptr" : llvmType(cptypes[0]);
                                out_ << "    call void @" << llvmGlobalName(copy_mangled)
                                     << "(ptr " << res_tmp << ", " << cptype << " " << carg << ")\n";
                            } else {
                                std::string src = emitArgForParam(*b->left, left_slid + "^");
                                emitSlidCopy(left_slid, res_tmp, src);
                            }
                        }
                        // call op+= on result with right
                        auto& iptypes = func_param_types_[compound_mangled];
                        std::string iarg = emitArgForParam(*b->right,
                            iptypes.empty() ? "" : iptypes[0]);
                        std::string iptype = iptypes.empty() ? "ptr" : llvmType(iptypes[0]);
                        out_ << "    call void @" << llvmGlobalName(compound_mangled)
                             << "(ptr " << res_tmp << ", " << iptype << " " << iarg << ")\n";
                        return res_tmp;
                    }
                }
            }
        }

        // pointer + int or pointer - int → GEP
        std::string left_llvm  = exprLlvmType(*b->left);
        std::string right_llvm = exprLlvmType(*b->right);
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
                    auto tit = local_types_.find(ve->name);
                    if (tit != local_types_.end()) {
                        std::string lt = tit->second;
                        if (lt.size() >= 2 && lt.substr(lt.size()-2) == "[]")
                            return llvmType(lt.substr(0, lt.size()-2));
                    }
                }
                return "i8"; // default to byte
            };
            bool left_ptr = (left_llvm == "ptr");
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
        else throw std::runtime_error("unknown operator: " + b->op);

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
            // slid (struct) type — use sizeof_override if annotated, else GEP null trick
            if (slid_info_.count(tn)) {
                auto& sinfo = slid_info_[tn];
                if (sinfo.sizeof_override > 0) {
                    std::string reg = newTmp();
                    out_ << "    " << reg << " = add i64 " << sinfo.sizeof_override << ", 0\n";
                    return reg;
                }
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << tn << ", ptr null, i32 1\n";
                std::string sz = newTmp();
                out_ << "    " << sz << " = ptrtoint ptr " << gep << " to i64\n";
                return sz;
            }
            return "0";
        };

        if (!se->type_name.empty()) {
            return sizeofTypeName(se->type_name);
        }

        // expression form
        const Expr& op = *se->operand;

        // string literal → byte length (not including null terminator)
        if (auto* sl = dynamic_cast<const StringLiteralExpr*>(&op)) {
            int len; llvmEscape(sl->value, len);
            return std::to_string(len - 1); // llvmEscape counts the null terminator
        }

        // variable → look up its type
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
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) return sizeofTypeName(tit->second);
            // field of current slid
            if (!current_slid_.empty()) {
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve->name);
                if (fit != info.field_index.end())
                    return sizeofTypeName(info.field_types[fit->second]);
            }
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
        int elem_bytes = 1;
        if (elt == "i32") elem_bytes = 4;
        else if (elt == "i64") elem_bytes = 8;
        else if (elt == "i16") elem_bytes = 2;
        std::string bytes64;
        if (count_type == "i64") {
            bytes64 = newTmp();
            out_ << "    " << bytes64 << " = mul i64 " << count_val << ", " << elem_bytes << "\n";
        } else {
            std::string bytes = newTmp();
            out_ << "    " << bytes << " = mul " << count_type << " " << count_val << ", " << elem_bytes << "\n";
            bytes64 = newTmp();
            out_ << "    " << bytes64 << " = zext " << count_type << " " << bytes << " to i64\n";
        }
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = call ptr @malloc(i64 " << bytes64 << ")\n";
        return tmp;
    }

    if (auto* s = dynamic_cast<const StringLiteralExpr*>(&expr)) {
        std::string label = "@.str" + std::to_string(str_counter_++);
        int len; llvmEscape(s->value, len);
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = getelementptr [" << len << " x i8], ptr "
             << label << ", i32 0, i32 0\n";
        return tmp;
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
            auto& info = slid_info_[stype];

            std::string tmp_reg = newTmp();
            out_ << "    " << tmp_reg << " = alloca %struct." << stype << "\n";

            // find SlidDef for default field values
            const SlidDef* slid_def = nullptr;
            for (auto& s : program_.slids)
                if (s.name == stype) { slid_def = &s; break; }

            // initialize fields to defaults
            for (int i = 0; i < (int)info.field_types.size(); i++) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << stype
                     << ", ptr " << tmp_reg << ", i32 0, i32 " << i << "\n";
                std::string val = (slid_def && slid_def->fields[i].default_val)
                                  ? emitExpr(*slid_def->fields[i].default_val)
                                  : (isInlineArrayType(info.field_types[i]) ? "zeroinitializer" : "0");
                out_ << "    store " << llvmType(info.field_types[i])
                     << " " << val << ", ptr " << gep << "\n";
            }

            // call ctor if any
            if (info.has_pinit && !info.is_transport_impl)
                out_ << "    call void @" << stype << "__pinit(ptr " << tmp_reg << ")\n";
            else if (info.has_explicit_ctor)
                out_ << "    call void @" << stype << "__ctor(ptr " << tmp_reg << ")\n";

            // call op= with the operand
            std::string mangled = resolveOpEq(stype + "__op=", *nc->operand);
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

    // pointer reinterpret cast: <Type^> expr
    if (auto* pc = dynamic_cast<const PtrCastExpr*>(&expr)) {
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

    throw std::runtime_error("unsupported expression type");
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
    if (dynamic_cast<const NewExpr*>(&expr)) return "ptr";

    // string literal — ptr
    if (dynamic_cast<const StringLiteralExpr*>(&expr)) return "ptr";

    // variable — look up declared type
    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
        // field access via self in a method
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.field_index.count(v->name))
                return llvmType(info.field_types[info.field_index.at(v->name)]);
        }
        auto tit = local_types_.find(v->name);
        if (tit != local_types_.end()) return llvmType(tit->second);
        // enum value — i32
        if (enum_values_.count(v->name)) return "i32";
        // array name used as pointer — ptr
        if (array_info_.count(v->name)) return "ptr";
        return "i32";
    }

    // dereference: type is the pointee type
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && isIndirectType(tit->second)) {
                std::string pt = isPtrType(tit->second)
                    ? tit->second.substr(0, tit->second.size()-2)
                    : tit->second.substr(0, tit->second.size()-1);
                return llvmType(pt);
            }
        }
        return "i32";
    }

    // field access — look up field type
    if (auto* fa = dynamic_cast<const FieldAccessExpr*>(&expr)) {
        std::string slid_name;
        if (auto* de = dynamic_cast<const DerefExpr*>(fa->object.get())) {
            if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto tit = local_types_.find(ve->name);
                if (tit != local_types_.end()) {
                    slid_name = isPtrType(tit->second)
                        ? tit->second.substr(0, tit->second.size()-2)
                        : tit->second.substr(0, tit->second.size()-1);
                }
            }
        } else if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) slid_name = tit->second;
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
        // check operator overload first
        {
            std::string op_func = resolveOperatorOverload(b->op, *b->left, *b->right);
            if (!op_func.empty()) {
                std::string ret = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                if (ret == "void") return "ptr"; // method overload: self is the alloca result
                if (!ret.empty() && slid_info_.count(ret)) return "ptr"; // free function: sret alloca
            }
        }
        if (b->op == "==" || b->op == "!=" || b->op == "<" ||
            b->op == ">"  || b->op == "<=" || b->op == ">=" ||
            b->op == "&&" || b->op == "||" || b->op == "^^")
            return "i32";
        // arithmetic/bitwise: result is the wider of the two operands
        {
            bool left_is_literal  = (dynamic_cast<const IntLiteralExpr*>(b->left.get())  != nullptr);
            bool right_is_literal = (dynamic_cast<const IntLiteralExpr*>(b->right.get()) != nullptr);
            std::string lt = exprLlvmType(*b->left);
            std::string rt = exprLlvmType(*b->right);
            if (left_is_literal && !right_is_literal) return rt;
            if (right_is_literal && !left_is_literal) return lt;
            // pointer arithmetic: ptr +/- int → ptr
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
        if (u->op == "!" || u->op == "~") return "i32";
        // pre/post inc/dec — same type as operand
        return exprLlvmType(*u->operand);
    }

    // address-of — ptr
    if (dynamic_cast<const AddrOfExpr*>(&expr)) return "ptr";

    // DerefExpr: ptr^ — element type of the pointer
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        if (auto* ve = dynamic_cast<const VarExpr*>(de->operand.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                std::string t = tit->second;
                if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                    return llvmType(t.substr(0, t.size()-2));
                if (!t.empty() && t.back() == '^')
                    return "ptr"; // dereffing a reference gives the slid (ptr to struct)
            }
        }
        return "i32";
    }

    // PostIncDerefExpr: ptr++^ — element type of the pointer
    if (auto* pi = dynamic_cast<const PostIncDerefExpr*>(&expr)) {
        if (auto* ve = dynamic_cast<const VarExpr*>(pi->operand.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                std::string t = tit->second;
                if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                    return llvmType(t.substr(0, t.size()-2));
            }
        }
        return "i32";
    }

    // array index — elem type
    if (auto* ai = dynamic_cast<const ArrayIndexExpr*>(&expr)) {
        const Expr* cur = &expr;
        while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur))
            cur = a->base.get();
        if (auto* ve = dynamic_cast<const VarExpr*>(cur)) {
            auto ait = array_info_.find(ve->name);
            if (ait != array_info_.end()) return llvmType(ait->second.elem_type);
            // pointer-type field
            if (!current_slid_.empty()) {
                auto& info = slid_info_[current_slid_];
                auto fit = info.field_index.find(ve->name);
                if (fit != info.field_index.end()) {
                    std::string ft = info.field_types[fit->second];
                    if (ft.size() >= 2 && ft.substr(ft.size()-2) == "[]")
                        return llvmType(ft.substr(0, ft.size()-2));
                }
            }
            // pointer-type local
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                std::string lt = tit->second;
                if (lt.size() >= 2 && lt.substr(lt.size()-2) == "[]")
                    return llvmType(lt.substr(0, lt.size()-2));
            }
        }
        (void)ai;
        return "i32";
    }

    // method call — look up return type
    if (auto* mc = dynamic_cast<const MethodCallExpr*>(&expr)) {
        std::string slid_name;
        if (auto* ve = dynamic_cast<const VarExpr*>(mc->object.get())) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) slid_name = tit->second;
        }
        if (!slid_name.empty()) {
            std::string base = slid_name + "__" + mc->method;
            std::string mangled = resolveOverloadForCall(base, mc->args);
            auto rit = func_return_types_.find(mangled);
            if (rit != func_return_types_.end()) return llvmType(rit->second);
        }
        return "i32";
    }

    // function call — look up return type
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        if (!ce->type_args.empty()) {
            // template call: derive return type from template definition
            auto tit = template_funcs_.find(ce->callee);
            if (tit != template_funcs_.end()) {
                const FunctionDef& tmpl = *tit->second;
                std::map<std::string, std::string> subst;
                for (int i = 0; i < (int)tmpl.type_params.size() && i < (int)ce->type_args.size(); i++)
                    subst[tmpl.type_params[i]] = ce->type_args[i];
                std::string rt = tmpl.return_type;
                auto it2 = subst.find(rt);
                if (it2 != subst.end()) rt = it2->second;
                return llvmType(rt);
            }
        }
        // check if already instantiated
        auto rit = func_return_types_.find(ce->callee);
        if (rit != func_return_types_.end()) return llvmType(rit->second);
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

    // sizeof always returns intptr (i64 on 64-bit)
    if (dynamic_cast<const SizeofExpr*>(&expr)) return "i64";

    return "i32";
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
    std::string t = exprLlvmType(expr);
    std::string cond_bool = newTmp();
    if (t == "ptr") {
        out_ << "    " << cond_bool << " = icmp ne ptr " << val << ", null\n";
    } else {
        out_ << "    " << cond_bool << " = icmp ne " << t << " " << val << ", 0\n";
    }
    return cond_bool;
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
    // nullptr → intptr
    if (dynamic_cast<const NullptrExpr*>(&expr)) return "intptr";
    // new Type[n] → Type[]
    if (auto* ne = dynamic_cast<const NewExpr*>(&expr)) return ne->elem_type + "[]";
    // ^x → elem_type^ (take address)
    if (auto* ae = dynamic_cast<const AddrOfExpr*>(&expr)) {
        std::string inner = inferSlidType(*ae->operand);
        return inner + "^";
    }
    // type conversion — use the target type
    if (auto* nc = dynamic_cast<const TypeConvExpr*>(&expr)) return nc->target_type;
    // pointer reinterpret cast — use the target type
    if (auto* pc = dynamic_cast<const PtrCastExpr*>(&expr)) return pc->target_type;
    // variable — look up its declared type, then fall back to current slid's fields
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        auto it = local_types_.find(ve->name);
        if (it != local_types_.end()) return it->second;
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            auto fit = info.field_index.find(ve->name);
            if (fit != info.field_index.end())
                return info.field_types[fit->second];
        }
        // type name used as anonymous temporary (e.g. ValueBoth in ValueBoth + 10)
        if (slid_info_.count(ve->name)) return ve->name;
    }
    // free function call — look up return type
    if (auto* ce = dynamic_cast<const CallExpr*>(&expr)) {
        if (!ce->type_args.empty()) {
            // template call: substitute return type without instantiating yet
            auto tit = template_funcs_.find(ce->callee);
            if (tit != template_funcs_.end()) {
                const FunctionDef& tmpl = *tit->second;
                std::map<std::string, std::string> subst;
                for (int i = 0; i < (int)tmpl.type_params.size() && i < (int)ce->type_args.size(); i++)
                    subst[tmpl.type_params[i]] = ce->type_args[i];
                auto it2 = subst.find(tmpl.return_type);
                return it2 != subst.end() ? it2->second : tmpl.return_type;
            }
        }
        auto it = func_return_types_.find(ce->callee);
        if (it != func_return_types_.end()) return it->second;
    }
    // method call — look up return type via slid type of object
    if (auto* me = dynamic_cast<const MethodCallExpr*>(&expr)) {
        std::string slid_type = exprSlidType(*me->object);
        if (!slid_type.empty()) {
            std::string mangled = slid_type + "__" + me->method;
            auto it = func_return_types_.find(mangled);
            if (it != func_return_types_.end()) return it->second;
        }
    }
    // dereference ptr^ → element type (strip trailing ^ or [])
    if (auto* de = dynamic_cast<const DerefExpr*>(&expr)) {
        std::string pt = inferSlidType(*de->operand);
        if (pt.size() >= 2 && pt.substr(pt.size()-2) == "[]") return pt.substr(0, pt.size()-2);
        if (!pt.empty() && pt.back() == '^') return pt.substr(0, pt.size()-1);
        return pt;
    }
    // ptr++^ / ptr--^ → element type
    if (auto* pid = dynamic_cast<const PostIncDerefExpr*>(&expr)) {
        std::string pt = inferSlidType(*pid->operand);
        if (pt.size() >= 2 && pt.substr(pt.size()-2) == "[]") return pt.substr(0, pt.size()-2);
        if (!pt.empty() && pt.back() == '^') return pt.substr(0, pt.size()-1);
        return pt;
    }
    // binary expression — use exprSlidType if it produces a slid, else infer from left
    if (auto* be = dynamic_cast<const BinaryExpr*>(&expr)) {
        std::string slid = exprSlidType(expr);
        if (!slid.empty()) return slid;
        return inferSlidType(*be->left);
    }
    // unary — propagate through
    if (auto* ue = dynamic_cast<const UnaryExpr*>(&expr)) {
        if (ue->op == "-" || ue->op == "~" || ue->op == "!")
            return inferSlidType(*ue->operand);
    }
    // sizeof → intptr
    if (dynamic_cast<const SizeofExpr*>(&expr)) return "intptr";
    // default: int
    return "int";
}
