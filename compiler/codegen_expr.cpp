#include "codegen.h"
#include "codegen_helpers.h"
#include <sstream>
#include <functional>
#include <stdexcept>

std::string Codegen::emitExpr(const Expr& expr) {
    if (auto* i = dynamic_cast<const IntLiteralExpr*>(&expr))
        return std::to_string(i->value);

    if (auto* v = dynamic_cast<const VarExpr*>(&expr)) {
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

        // regular function call
        auto it = func_return_types_.find(call->callee);
        if (it == func_return_types_.end())
            throw std::runtime_error("undefined function: " + call->callee);
        std::string ret_type = llvmType(it->second);
        std::string arg_str;
        auto& ptypes = func_param_types_[call->callee];
        for (int i = 0; i < (int)call->args.size(); i++) {
            if (i > 0) arg_str += ", ";
            std::string ptype = (i < (int)ptypes.size()) ? llvmType(ptypes[i]) : "i32";
            arg_str += ptype + " " + emitExpr(*call->args[i]);
        }
        std::string tmp = newTmp();
        out_ << "    " << tmp << " = call " << ret_type << " @" << call->callee
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
        std::string op_type;
        {
            bool left_is_literal  = (dynamic_cast<const IntLiteralExpr*>(b->left.get())  != nullptr);
            bool right_is_literal = (dynamic_cast<const IntLiteralExpr*>(b->right.get()) != nullptr);
            std::string lt = exprLlvmType(*b->left);
            std::string rt = exprLlvmType(*b->right);
            if (left_is_literal && !right_is_literal)
                op_type = rt;
            else if (right_is_literal && !left_is_literal)
                op_type = lt;
            else {
                // promote to the wider of the two; treat i8<i16<i32<i64
                static const std::map<std::string,int> rank = {
                    {"i8",0},{"i16",1},{"i32",2},{"i64",3}};
                auto li = rank.find(lt), ri = rank.find(rt);
                if (li != rank.end() && ri != rank.end())
                    op_type = (ri->second > li->second) ? rt : lt;
                else
                    op_type = "i32"; // fallback for ptr etc.
            }
            if (op_type.empty()) op_type = "i32";
        }

        // operator overload (e.g. String op+(String^ sa, String^ sb))
        {
            std::string op_func = resolveOperatorOverload(b->op, *b->left, *b->right);
            if (!op_func.empty()) {
                // find return slid type
                std::string ret_slid = func_return_types_.count(op_func) ? func_return_types_[op_func] : "";
                if (!ret_slid.empty() && slid_info_.count(ret_slid)) {
                    // alloca a temp struct, call with sret, return the alloca ptr
                    std::string tmp_alloca = newTmp();
                    out_ << "    " << tmp_alloca << " = alloca %struct." << ret_slid << "\n";
                    // zero-init
                    auto& info = slid_info_[ret_slid];
                    for (int i = 0; i < (int)info.field_types.size(); i++) {
                        std::string ft = llvmType(info.field_types[i]);
                        std::string gep = newTmp();
                        out_ << "    " << gep << " = getelementptr %struct." << ret_slid << ", ptr " << tmp_alloca << ", i32 0, i32 " << i << "\n";
                        if (isIndirectType(info.field_types[i]))
                            out_ << "    store ptr null, ptr " << gep << "\n";
                        else
                            out_ << "    store " << ft << " 0, ptr " << gep << "\n";
                    }
                    // call ctor if any
                    if (info.has_explicit_ctor)
                        out_ << "    call void @" << ret_slid << "__ctor(ptr " << tmp_alloca << ")\n";
                    auto& ptypes = func_param_types_[op_func];
                    std::string args = "ptr sret(%struct." + ret_slid + ") " + tmp_alloca;
                    std::string la = emitArgForParam(*b->left, ptypes.size() > 0 ? ptypes[0] : "");
                    args += ", " + (ptypes.size() > 0 ? llvmType(ptypes[0]) : "ptr") + " " + la;
                    std::string ra = emitArgForParam(*b->right, ptypes.size() > 1 ? ptypes[1] : "");
                    args += ", " + (ptypes.size() > 1 ? llvmType(ptypes[1]) : "ptr") + " " + ra;
                    out_ << "    call void @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                    return tmp_alloca;
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
            std::string ptr_val  = emitExpr(left_ptr ? *b->left  : *b->right);
            std::string off_val  = emitExpr(left_ptr ? *b->right : *b->left);
            std::string tmp = newTmp();
            if (b->op == "-") {
                std::string neg = newTmp();
                out_ << "    " << neg << " = sub i32 0, " << off_val << "\n";
                off_val = neg;
            }
            out_ << "    " << tmp << " = getelementptr " << pointee
                 << ", ptr " << ptr_val << ", i32 " << off_val << "\n";
            return tmp;
        }

        std::string left  = emitExpr(*b->left);
        std::string right = emitExpr(*b->right);
        std::string tmp   = newTmp();

        if      (b->op == "+")  { out_ << "    " << tmp << " = add "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "-")  { out_ << "    " << tmp << " = sub "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "*")  { out_ << "    " << tmp << " = mul "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "/")  { out_ << "    " << tmp << " = sdiv " << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "%")  { out_ << "    " << tmp << " = srem " << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "&")  { out_ << "    " << tmp << " = and "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "|")  { out_ << "    " << tmp << " = or "   << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "^")  { out_ << "    " << tmp << " = xor "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == "<<") { out_ << "    " << tmp << " = shl "  << op_type << " " << left << ", " << right << "\n"; return tmp; }
        else if (b->op == ">>") { out_ << "    " << tmp << " = ashr " << op_type << " " << left << ", " << right << "\n"; return tmp; }

        std::string cmp = newTmp();
        std::string pred;
        if      (b->op == "==") pred = "eq";
        else if (b->op == "!=") pred = "ne";
        else if (b->op == "<")  pred = "slt";
        else if (b->op == ">")  pred = "sgt";
        else if (b->op == "<=") pred = "sle";
        else if (b->op == ">=") pred = "sge";
        else throw std::runtime_error("unknown operator: " + b->op);

        out_ << "    " << cmp << " = icmp " << pred << " " << op_type << " " << left << ", " << right << "\n";
        out_ << "    " << tmp << " = zext i1 " << cmp << " to i32\n";
        return tmp;
    }

    if (dynamic_cast<const NullptrExpr*>(&expr)) {
        return "null";
    }

    if (auto* ne = dynamic_cast<const NewExpr*>(&expr)) {
        std::string elt = llvmType(ne->elem_type);
        std::string count_val = emitExpr(*ne->count);
        int elem_bytes = 1;
        if (elt == "i32") elem_bytes = 4;
        else if (elt == "i64") elem_bytes = 8;
        else if (elt == "i16") elem_bytes = 2;
        std::string bytes = newTmp();
        out_ << "    " << bytes << " = mul i32 " << count_val << ", " << elem_bytes << "\n";
        std::string bytes64 = newTmp();
        out_ << "    " << bytes64 << " = zext i32 " << bytes << " to i64\n";
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

    throw std::runtime_error("unsupported expression type");
}

// Infer the LLVM type that emitExpr will produce for this expression,
// without emitting any IR. Used by emitCondBool to build a correctly-typed
// icmp regardless of whether the condition involves i8, i16, i32, ptr, etc.
std::string Codegen::exprLlvmType(const Expr& expr) {
    // integer literal — always i32 (emitExpr returns a bare integer string,
    // and the consumer hardcodes i32 for BinaryExpr; literals used in conditions
    // are compared as i32)
    if (dynamic_cast<const IntLiteralExpr*>(&expr)) return "i32";

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
                if (!ret.empty() && slid_info_.count(ret)) return "ptr"; // sret alloca
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
        auto rit = func_return_types_.find(ce->callee);
        if (rit != func_return_types_.end()) return llvmType(rit->second);
        return "i32";
    }

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
