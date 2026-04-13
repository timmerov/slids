#include "codegen.h"
#include "codegen_helpers.h"
#include <sstream>
#include <functional>
#include <stdexcept>

void Codegen::emitStmt(const Stmt& stmt) {
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

    // resolve op= overload: pick the best matching overload for the arg expression.
    // Priority: slid ref > scalar exact match > ptr/char[] fallback
    auto resolveOpEq = [&](const std::string& base, const Expr& arg) -> std::string {
        auto oit = method_overloads_.find(base);
        if (oit == method_overloads_.end()) return "";
        if (oit->second.size() == 1) return oit->second[0].first;

        static const std::set<std::string> unsigned_types = {"uint","uint8","uint16","uint32","uint64"};
        static const std::set<std::string> signed_int_types = {"int","int8","int16","int32","int64"};

        // determine argument category
        bool arg_is_slid = false;
        bool arg_is_char = false;       // char literal ('x') — prefers char overload
        bool arg_is_scalar_int = false; // signed integer
        bool arg_is_unsigned = false;   // unsigned integer — prefers uint64 overload
        if (auto* ile = dynamic_cast<const IntLiteralExpr*>(&arg)) {
            arg_is_char = ile->is_char_literal;
            arg_is_scalar_int = !ile->is_char_literal;
        } else if (auto* nc = dynamic_cast<const NumericCastExpr*>(&arg)) {
            arg_is_unsigned = unsigned_types.count(nc->target_type) > 0;
            arg_is_scalar_int = !arg_is_unsigned;
        } else if (auto* ve = dynamic_cast<const VarExpr*>(&arg)) {
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end()) {
                std::string t = tit->second;
                if (!t.empty() && t.back() == '^') t.pop_back();
                else if (t.size() >= 2 && t.substr(t.size()-2) == "[]") t = t.substr(0, t.size()-2);
                arg_is_slid = slid_info_.count(t) > 0;
                arg_is_char = !arg_is_slid && (tit->second == "char");
                arg_is_unsigned = !arg_is_slid && !arg_is_char && unsigned_types.count(tit->second) > 0;
                if (!arg_is_slid && !arg_is_char && !arg_is_unsigned && !isIndirectType(tit->second))
                    arg_is_scalar_int = true;
            }
        } else if (!exprSlidType(arg).empty()) {
            arg_is_slid = true; // e.g. result of op+ expression
        } else {
            // use inferred LLVM type: integer types are scalar, ptr is string/reference
            std::string lt = exprLlvmType(arg);
            if (lt == "i8")
                arg_is_char = true;
            else if (lt == "i16" || lt == "i32" || lt == "i64")
                arg_is_scalar_int = true;
        }

        // pass 1: exact category match
        for (auto& [m, ptypes] : oit->second) {
            if (ptypes.size() != 1) continue;
            if (arg_is_slid       && isRefType(ptypes[0])) return m;
            if (arg_is_char       && ptypes[0] == "char") return m;
            if (arg_is_unsigned   && unsigned_types.count(ptypes[0])) return m;
            if (arg_is_scalar_int && !isIndirectType(ptypes[0]) && ptypes[0] != "char" && !unsigned_types.count(ptypes[0])) return m;
            if (!arg_is_slid && !arg_is_char && !arg_is_scalar_int && !arg_is_unsigned && isPtrType(ptypes[0])) return m;
        }
        // pass 2: fallback — any ptr param for non-slid arg (e.g. string literal)
        if (!arg_is_slid) {
            for (auto& [m, ptypes] : oit->second) {
                if (ptypes.size() == 1 && isIndirectType(ptypes[0])) return m;
            }
        }
        return oit->second[0].first;
    };

    if (auto* ds = dynamic_cast<const DeleteStmt*>(&stmt)) {
        std::string ptr_val = emitExpr(*ds->operand);
        out_ << "    call void @free(ptr " << ptr_val << ")\n";
        // nullify the pointer variable so it is left in a valid (null) state
        if (auto* ve = dynamic_cast<const VarExpr*>(ds->operand.get())) {
            auto it = locals_.find(ve->name);
            if (it != locals_.end())
                out_ << "    store ptr null, ptr " << it->second << "\n";
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
        // class instantiation
        if (slid_info_.count(decl->type)) {
            auto& info = slid_info_[decl->type];
            std::string reg = "%var_" + decl->name;
            out_ << "    " << reg << " = alloca %struct." << decl->type << "\n";
            locals_[decl->name] = reg;
            local_types_[decl->name] = decl->type;

            // find the SlidDef
            const SlidDef* slid_def = nullptr;
            for (auto& s : program_.slids)
                if (s.name == decl->type) { slid_def = &s; break; }

            // initialize fields with defaults or ctor args
            for (int i = 0; i < (int)info.field_types.size(); i++) {
                std::string gep = newTmp();
                out_ << "    " << gep << " = getelementptr %struct." << decl->type
                     << ", ptr " << reg << ", i32 0, i32 " << i << "\n";

                std::string val;
                if (i < (int)decl->ctor_args.size()) {
                    val = emitExpr(*decl->ctor_args[i]);
                } else if (slid_def && slid_def->fields[i].default_val) {
                    val = emitExpr(*slid_def->fields[i].default_val);
                } else {
                    val = "0";
                }
                out_ << "    store " << llvmType(info.field_types[i])
                     << " " << val << ", ptr " << gep << "\n";
            }

            // run implicit constructor body if any (loose code in slid body)
            if (slid_def && slid_def->ctor_body) {
                std::string saved_slid = current_slid_;
                std::string saved_self = self_ptr_;
                current_slid_ = decl->type;
                self_ptr_ = reg;
                emitBlock(*slid_def->ctor_body);
                current_slid_ = saved_slid;
                self_ptr_ = saved_self;
            }

            // call explicit constructor __ctor if defined
            if (info.has_explicit_ctor) {
                out_ << "    call void @" << decl->type << "__ctor(ptr " << reg << ")\n";
            }

            // if initialized with = expr, call op= method; with <- expr, call op<- method
            if (decl->init) {
                std::string op_name = decl->is_move ? "op<-" : "op=";
                std::string mangled = resolveOpEq(decl->type + "__" + op_name, *decl->init);
                if (!mangled.empty()) {
                    auto& ptypes = func_param_types_[mangled];
                    std::string param_type = ptypes.empty() ? "" : ptypes[0];
                    std::string arg_val = emitArgForParam(*decl->init, param_type);
                    std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                    out_ << "    call void @" << llvmGlobalName(mangled)
                         << "(ptr " << reg << ", " << ptype_str << " " << arg_val << ")\n";
                }
            }

            // register for dtor call on scope exit
            if (info.has_dtor) {
                dtor_vars_.push_back({decl->name, decl->type});
            }
            return;
        }

        // primitive or reference variable declaration
        std::string reg = "%var_" + decl->name;
        std::string llvm_t = llvmType(decl->type);
        out_ << "    " << reg << " = alloca " << llvm_t << "\n";
        locals_[decl->name] = reg;
        local_types_[decl->name] = decl->type;
        std::string val = emitExpr(*decl->init);
        // coerce integer or float widths if necessary
        if (!isIndirectType(decl->type)) {
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
        if (decl->is_move && isIndirectType(decl->type))
            emitNullOut(*decl->init);
        return;
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
        // check if it's a field via self in a method
        if (!current_slid_.empty()) {
            auto& info = slid_info_[current_slid_];
            if (info.field_index.count(assign->name)) {
                int idx = info.field_index[assign->name];
                std::string field_type = llvmType(info.field_types[idx]);
                std::string gep = newTmp();
                std::string self = self_ptr_.empty() ? "%self" : self_ptr_;
                out_ << "    " << gep << " = getelementptr %struct." << current_slid_
                     << ", ptr " << self << ", i32 0, i32 " << idx << "\n";
                if (assign->is_move && isIndirectType(info.field_types[idx])) {
                    // free old field value before stealing
                    std::string old_val = newTmp();
                    out_ << "    " << old_val << " = load ptr, ptr " << gep << "\n";
                    out_ << "    call void @free(ptr " << old_val << ")\n";
                }
                std::string val = emitExpr(*assign->value);
                out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
                if (assign->is_move && isIndirectType(info.field_types[idx]))
                    emitNullOut(*assign->value);
                return;
            }
        }
        auto it = locals_.find(assign->name);
        if (it == locals_.end())
            throw std::runtime_error("undefined variable: " + assign->name);
        auto tit = local_types_.find(assign->name);
        // check for operator overload: lhs is slid type, rhs is BinaryExpr
        if (tit != local_types_.end() && slid_info_.count(tit->second)) {
            if (auto* be = dynamic_cast<const BinaryExpr*>(assign->value.get())) {
                std::string op_func = resolveOperatorOverload(be->op, *be->left, *be->right);
                if (!op_func.empty()) {
                    // call op func with sret into lhs alloca
                    // first call dtor on existing lhs value
                    const std::string& slid_name = tit->second;
                    if (slid_info_.at(slid_name).has_dtor) {
                        out_ << "    call void @" << slid_name << "__dtor(ptr " << it->second << ")\n";
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
                    auto& ptypes = func_param_types_[op_func];
                    std::string args;
                    args += "ptr sret(%struct." + slid_name + ") " + it->second;
                    // left arg
                    std::string la = emitArgForParam(*be->left, ptypes.size() > 0 ? ptypes[0] : "");
                    args += ", " + (ptypes.size() > 0 ? llvmType(ptypes[0]) : "ptr") + " " + la;
                    // right arg
                    std::string ra = emitArgForParam(*be->right, ptypes.size() > 1 ? ptypes[1] : "");
                    args += ", " + (ptypes.size() > 1 ? llvmType(ptypes[1]) : "ptr") + " " + ra;
                    out_ << "    call void @" << llvmGlobalName(op_func) << "(" << args << ")\n";
                    return;
                }
            }
        }
        // check for op= / op<- method on slid type
        if (tit != local_types_.end() && slid_info_.count(tit->second)) {
            std::string op_name = assign->is_move ? "op<-" : "op=";
            std::string mangled = resolveOpEq(tit->second + "__" + op_name, *assign->value);
            if (!mangled.empty()) {
                auto& ptypes = func_param_types_[mangled];
                std::string param_type = ptypes.empty() ? "" : ptypes[0];
                std::string arg_val = emitArgForParam(*assign->value, param_type);
                std::string ptype_str = ptypes.empty() ? "ptr" : llvmType(ptypes[0]);
                out_ << "    call void @" << llvmGlobalName(mangled)
                     << "(ptr " << it->second << ", " << ptype_str << " " << arg_val << ")\n";
                return;
            }
        }
        // pointer move: free old value, steal source, null source
        if (assign->is_move && tit != local_types_.end() && isIndirectType(tit->second)) {
            std::string old_val = newTmp();
            out_ << "    " << old_val << " = load ptr, ptr " << it->second << "\n";
            out_ << "    call void @free(ptr " << old_val << ")\n";
            std::string src_val = emitExpr(*assign->value);
            out_ << "    store ptr " << src_val << ", ptr " << it->second << "\n";
            emitNullOut(*assign->value);
            return;
        }
        std::string val = emitExpr(*assign->value);
        bool is_ptr = tit != local_types_.end() && isIndirectType(tit->second);
        std::string store_type = is_ptr ? "ptr" : llvmType(tit != local_types_.end() ? tit->second : "int");
        // coerce integer widths if necessary (sext or trunc)
        if (!is_ptr && tit != local_types_.end()) {
            static const std::map<std::string,int> rank = {{"i8",0},{"i16",1},{"i32",2},{"i64",3}};
            std::string src_t = exprLlvmType(*assign->value);
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
        out_ << "    store " << store_type << " " << val << ", ptr " << it->second << "\n";
        return;
    }

    if (auto* da = dynamic_cast<const DerefAssignStmt*>(&stmt)) {
        // ptr^ = val — load the pointer from its alloca, then store through it
        std::string ptr_reg;
        std::string pointee_llvm = "i32";
        if (auto* ve = dynamic_cast<const VarExpr*>(da->ptr.get())) {
            auto it = locals_.find(ve->name);
            if (it == locals_.end())
                throw std::runtime_error("DerefAssign: undefined variable '" + ve->name + "'");
            auto tit = local_types_.find(ve->name);
            if (tit != local_types_.end() && isIndirectType(tit->second)) {
                std::string loaded_ptr = newTmp();
                out_ << "    " << loaded_ptr << " = load ptr, ptr " << it->second << "\n";
                ptr_reg = loaded_ptr;
                std::string pointee_type = ( isPtrType(tit->second) ? tit->second.substr(0, tit->second.size()-2) : tit->second.substr(0, tit->second.size()-1) );
                pointee_llvm = llvmType(pointee_type);
            } else {
                ptr_reg = it->second;
            }
        } else {
            ptr_reg = emitExpr(*da->ptr);
        }
        std::string val = emitExpr(*da->value);
        out_ << "    store " << pointee_llvm << " " << val << ", ptr " << ptr_reg << "\n";
        return;
    }

    if (auto* ia = dynamic_cast<const IndexAssignStmt*>(&stmt)) {
        // base[index] = value — pointer-type indexed store
        auto* ve = dynamic_cast<const VarExpr*>(ia->base.get());
        if (!ve) throw std::runtime_error("IndexAssign: complex base not supported");

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
            throw std::runtime_error("IndexAssign: '" + ve->name + "' is not a pointer type");

        std::string idx_val = emitExpr(*ia->index);
        std::string elt = llvmType(elem_type_str);
        std::string gep = newTmp();
        out_ << "    " << gep << " = getelementptr " << elt << ", ptr " << base_ptr << ", i32 " << idx_val << "\n";
        std::string rhs = emitExpr(*ia->value);
        out_ << "    store " << elt << " " << rhs << ", ptr " << gep << "\n";
        return;
    }

    if (auto* pida = dynamic_cast<const PostIncDerefAssignStmt*>(&stmt)) {
        // ptr++^ = val — store val at current ptr, then advance ptr by one element
        auto* ve = dynamic_cast<const VarExpr*>(pida->ptr.get());
        if (!ve) throw std::runtime_error("PostIncDerefAssign: only simple pointer variables supported");
        auto it = locals_.find(ve->name);
        if (it == locals_.end())
            throw std::runtime_error("PostIncDerefAssign: undefined variable '" + ve->name + "'");
        auto tit = local_types_.find(ve->name);
        if (tit == local_types_.end() || !isPtrType(tit->second))
            throw std::runtime_error("PostIncDerefAssign: '" + ve->name + "' is not a pointer ([]) type");
        std::string pointee_type = tit->second.substr(0, tit->second.size()-2);
        std::string pointee_llvm = llvmType(pointee_type);
        int step = (pida->op == "++") ? 1 : -1;
        // load current ptr
        std::string cur_ptr = newTmp();
        out_ << "    " << cur_ptr << " = load ptr, ptr " << it->second << "\n";
        // store value at current ptr
        std::string val = emitExpr(*pida->value);
        out_ << "    store " << pointee_llvm << " " << val << ", ptr " << cur_ptr << "\n";
        // advance ptr
        std::string new_ptr = newTmp();
        out_ << "    " << new_ptr << " = getelementptr " << pointee_llvm << ", ptr " << cur_ptr << ", i32 " << step << "\n";
        out_ << "    store ptr " << new_ptr << ", ptr " << it->second << "\n";
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
                throw std::runtime_error("DerefFieldAssign: unknown slid type for field '" + fa->field + "'");
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            std::string field_type = llvmType(info.field_types[idx]);
            std::string gep = newTmp();
            out_ << "    " << gep << " = getelementptr %struct." << slid_name
                 << ", ptr " << ptr_val << ", i32 0, i32 " << idx << "\n";
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
            return;
        }
        if (auto* ve = dynamic_cast<const VarExpr*>(fa->object.get())) {
            std::string gep = emitFieldPtr(ve->name, fa->field);
            auto type_it = local_types_.find(ve->name);
            std::string slid_name = type_it->second;
            auto& info = slid_info_[slid_name];
            int idx = info.field_index[fa->field];
            std::string field_type = llvmType(info.field_types[idx]);
            std::string val = emitExpr(*fa->value);
            out_ << "    store " << field_type << " " << val << ", ptr " << gep << "\n";
            return;
        }
        throw std::runtime_error("complex field assignment not yet supported");
    }

    if (auto* mcs = dynamic_cast<const MethodCallStmt*>(&stmt)) {
        std::string slid_name, obj_ptr;
        if (auto* ve = dynamic_cast<const VarExpr*>(mcs->object.get())) {
            auto type_it = local_types_.find(ve->name);
            if (type_it == local_types_.end())
                throw std::runtime_error("unknown type for: " + ve->name);
            slid_name = type_it->second;
            obj_ptr = locals_[ve->name];
        } else if (auto* de = dynamic_cast<const DerefExpr*>(mcs->object.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(de->operand.get())) {
                auto type_it = local_types_.find(ve2->name);
                if (type_it == local_types_.end())
                    throw std::runtime_error("unknown type for: " + ve2->name);
                slid_name = type_it->second;
                if (isRefType(slid_name)) slid_name.pop_back(); else if (isPtrType(slid_name)) slid_name.resize(slid_name.size()-2);
                std::string loaded = newTmp();
                out_ << "    " << loaded << " = load ptr, ptr " << locals_[ve2->name] << "\n";
                obj_ptr = loaded;
            }
        }
        if (!slid_name.empty()) {
            std::string base = slid_name + "__" + mcs->method;
            std::string mangled = resolveOverloadForCall(base, mcs->args);
            auto ret_it = func_return_types_.find(mangled);
            if (ret_it == func_return_types_.end())
                throw std::runtime_error("unknown method: " + mcs->method);
            std::string ret_type = llvmType(ret_it->second);
            auto& mptypes = func_param_types_[mangled];
            std::string arg_str = "ptr " + obj_ptr;
            for (int i = 0; i < (int)mcs->args.size(); i++) {
                std::string ptype_str = (i < (int)mptypes.size()) ? mptypes[i] : "";
                std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                std::string aval = emitArgForParam(*mcs->args[i], ptype_str);
                arg_str += ", " + ptype + " " + aval;
            }
            if (ret_type == "void") {
                out_ << "    call void @" << llvmGlobalName(mangled) << "(" << arg_str << ")\n";
            } else {
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = call " << ret_type << " @" << llvmGlobalName(mangled)
                     << "(" << arg_str << ")\n";
            }
            return;
        }
        throw std::runtime_error("complex method call statement not yet supported");
    }

    if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (current_func_uses_sret_ && ret->value) {
            // sret return: copy fields from local slid var into %retval, null ptr fields, then dtor
            auto* ve = dynamic_cast<const VarExpr*>(ret->value.get());
            if (!ve) throw std::runtime_error("sret: return value must be a local variable");
            auto tit = local_types_.find(ve->name);
            if (tit == local_types_.end() || !slid_info_.count(tit->second))
                throw std::runtime_error("sret: return value must be a slid type");
            const std::string& slid_name = tit->second;
            auto& info = slid_info_[slid_name];
            std::string src = locals_.at(ve->name);
            // copy each field from src to %retval
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
            emitDtors();
            out_ << "    ret void\n";
        } else {
            emitDtors();
            if (ret->value) {
                if (auto* te = dynamic_cast<const TupleExpr*>(ret->value.get())) {
                    std::string acc = "undef";
                    for (int i = 0; i < (int)te->values.size(); i++) {
                        std::string val = emitExpr(*te->values[i]);
                        std::string elem_type = exprLlvmType(*te->values[i]);
                        std::string tmp = newTmp();
                        out_ << "    " << tmp << " = insertvalue " << current_func_return_type_
                             << " " << acc << ", " << elem_type << " " << val << ", " << i << "\n";
                        acc = tmp;
                    }
                    out_ << "    ret " << current_func_return_type_ << " " << acc << "\n";
                } else {
                    std::string val = emitExpr(*ret->value);
                    out_ << "    ret " << current_func_return_type_ << " " << val << "\n";
                }
            } else {
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
        if (!brk->label.empty()) {
            // named break: find the frame with this label
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].block_label == brk->label) {
                    target = loop_stack_[i].break_target;
                    break;
                }
            }
            if (target.empty())
                throw std::runtime_error("break: unknown label '" + brk->label + "'");
        } else if (brk->number > 0) {
            // numbered break: count outward N loop frames
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                count++;
                if (count == brk->number) {
                    target = loop_stack_[i].break_target;
                    break;
                }
            }
            if (target.empty())
                throw std::runtime_error("break " + std::to_string(brk->number) + ": not enough enclosing loops");
        } else {
            if (break_label_.empty()) throw std::runtime_error("break outside of loop");
            target = break_label_;
        }
        out_ << "    br label %" << target << "\n";
        block_terminated_ = true;
        return;
    }

    if (auto* cont = dynamic_cast<const ContinueStmt*>(&stmt)) {
        std::string target;
        if (!cont->label.empty()) {
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (loop_stack_[i].block_label == cont->label) {
                    target = loop_stack_[i].continue_target;
                    break;
                }
            }
            if (target.empty())
                throw std::runtime_error("continue: unknown label '" + cont->label + "'");
        } else if (cont->number > 0) {
            int count = 0;
            for (int i = (int)loop_stack_.size() - 1; i >= 0; i--) {
                if (!loop_stack_[i].continue_target.empty()) {
                    count++;
                    if (count == cont->number) {
                        target = loop_stack_[i].continue_target;
                        break;
                    }
                }
            }
            if (target.empty())
                throw std::runtime_error("continue " + std::to_string(cont->number) + ": not enough enclosing loops");
        } else {
            if (continue_label_.empty()) throw std::runtime_error("continue outside of loop");
            target = continue_label_;
        }
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
        loop_stack_.push_back({w->block_label, end_lbl, cond_lbl});
        if (w->bottom_condition) {
            // do-while: body first, condition at bottom
            out_ << "    br label %" << body_lbl << "\n";
            block_terminated_ = false;
            out_ << body_lbl << ":\n";
            emitBlock(*w->body);
            if (!block_terminated_) out_ << "    br label %" << cond_lbl << "\n";
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
            emitBlock(*w->body);
            if (!block_terminated_) out_ << "    br label %" << cond_lbl << "\n";
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
        loop_stack_.push_back({f->block_label, end_lbl, incr_lbl});

        std::string var_reg;
        bool new_var = !f->var_type.empty();
        if (new_var) {
            // declare new loop variable
            var_reg = "%var_" + f->var_name;
            out_ << "    " << var_reg << " = alloca i32\n";
            locals_[f->var_name] = var_reg;
        } else {
            // reuse existing variable
            auto it = locals_.find(f->var_name);
            if (it == locals_.end())
                throw std::runtime_error("for loop: undefined variable '" + f->var_name + "'");
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
        emitBlock(*f->body);
        if (!block_terminated_) out_ << "    br label %" << incr_lbl << "\n";

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
            throw std::runtime_error("for-enum: unknown enum type '" + f->enum_name + "'");
        int size = sit->second;

        std::string init_lbl = newLabel("for_init");
        std::string cond_lbl = newLabel("for_cond");
        std::string body_lbl = newLabel("for_body");
        std::string incr_lbl = newLabel("for_incr");
        std::string end_lbl  = newLabel("for_end");
        std::string saved_break = break_label_, saved_continue = continue_label_;
        break_label_ = end_lbl; continue_label_ = incr_lbl;
        loop_stack_.push_back({f->block_label, end_lbl, incr_lbl});

        std::string var_reg = "%var_" + f->var_name;
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
        emitBlock(*f->body);
        if (!block_terminated_) out_ << "    br label %" << incr_lbl << "\n";

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

    if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
        std::string end_lbl = newLabel("sw_end");
        std::string saved_break = break_label_;
        break_label_ = end_lbl;
        loop_stack_.push_back({sw->block_label, end_lbl, ""});

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
                throw std::runtime_error("switch case value must be a constant integer or enum");
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
                throw std::runtime_error(call->callee + " expects 0 or 1 arguments");

            if (call->args.size() == 0) {
                if (!newline) throw std::runtime_error("__print() requires an argument");
                std::string tmp = newTmp();
                out_ << "    " << tmp << " = getelementptr [2 x i8], ptr @.str_newline, i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << tmp << ")\n";
                return;
            }

            // helper: emit a slice expr via printf("%.*s", len, ptr)
            auto emitSlice = [&](const SliceExpr* sl, bool nl) {
                std::string base_ptr = emitExpr(*sl->base);
                std::string start_val = emitExpr(*sl->start);
                std::string end_val = emitExpr(*sl->end);
                std::string sliced = newTmp();
                out_ << "    " << sliced << " = getelementptr i8, ptr " << base_ptr << ", i32 " << start_val << "\n";
                std::string len = newTmp();
                out_ << "    " << len << " = sub i32 " << end_val << ", " << start_val << "\n";
                std::string fmt = newTmp();
                std::string fmt_name = nl ? "@.fmt_slice" : "@.fmt_slice_nonl";
                int fmt_size = nl ? 6 : 5;
                out_ << "    " << fmt << " = getelementptr [" << fmt_size << " x i8], ptr " << fmt_name << ", i32 0, i32 0\n";
                out_ << "    call i32 (ptr, ...) @printf(ptr " << fmt << ", i32 " << len << ", ptr " << sliced << ")\n";
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
                    std::string label = "@.str" + std::to_string(str_counter_++);
                    std::string full = newline ? s->value + "\n" : s->value;
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
                    std::string label = "@.str" + std::to_string(str_counter_++);
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

        // regular top-level function call as statement
        // resolve overloaded free functions by argument count when plain name isn't registered
        std::string resolved_callee = call->callee;
        auto fit = func_return_types_.find(call->callee);
        if (fit == func_return_types_.end()) {
            auto foit = free_func_overloads_.find(call->callee);
            if (foit != free_func_overloads_.end()) {
                for (auto& [mn, ptypes] : foit->second) {
                    if (ptypes.size() == call->args.size()) { resolved_callee = mn; break; }
                }
                fit = func_return_types_.find(resolved_callee);
            }
        }
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

        // implicit self method call — e.g. reserve(len) inside a method
        if (!current_slid_.empty()) {
            std::string base = current_slid_ + "__" + call->callee;
            std::string mangled = resolveOverloadForCall(base, call->args);
            auto mit = func_return_types_.find(mangled);
            if (mit != func_return_types_.end()) {
                std::string ret_type = llvmType(mit->second);
                auto& mptypes = func_param_types_[mangled];
                std::string arg_str = "ptr %self";
                for (int i = 0; i < (int)call->args.size(); i++) {
                    std::string ptype_str = (i < (int)mptypes.size()) ? mptypes[i] : "";
                    std::string ptype = ptype_str.empty() ? "i32" : llvmType(ptype_str);
                    arg_str += ", " + ptype + " " + emitArgForParam(*call->args[i], ptype_str);
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
        }

        throw std::runtime_error("unknown function: " + call->callee);
    }

    if (auto* td = dynamic_cast<const TupleDestructureStmt*>(&stmt)) {
        std::string result = emitExpr(*td->init);
        std::string tuple_type = exprLlvmType(*td->init);
        for (int i = 0; i < (int)td->fields.size(); i++) {
            auto& [type, name] = td->fields[i];
            std::string llvm_t = llvmType(type);
            std::string reg = "%var_" + name;
            out_ << "    " << reg << " = alloca " << llvm_t << "\n";
            std::string extracted = newTmp();
            out_ << "    " << extracted << " = extractvalue " << tuple_type << " " << result << ", " << i << "\n";
            out_ << "    store " << llvm_t << " " << extracted << ", ptr " << reg << "\n";
            locals_[name] = reg;
            local_types_[name] = type;
        }
        return;
    }

    throw std::runtime_error("unsupported statement type");
}
