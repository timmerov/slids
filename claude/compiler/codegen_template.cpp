// Template function instantiation — AST cloning + type substitution
#include "codegen.h"
#include "source_map.h"
#include "codegen_helpers.h"
#include "parser.h"
#include <map>
#include <stdexcept>
#include <string>

// --- Type substitution helpers ---

static std::string subType(const std::string& t,
                            const std::map<std::string, std::string>& subst) {
    auto it = subst.find(t);
    return it != subst.end() ? it->second : t;
}

// Substitute type params inside a type string that may have ^ or [] suffix
// or be an anonymous tuple. e.g. "T[]" with T->int becomes "int[]";
// "(char[],char[],T)^" with T->int becomes "(char[],char[],int)^".
static std::string subTypeSuffix(const std::string& t,
                                  const std::map<std::string, std::string>& subst) {
    if (t.size() >= 2 && t.substr(t.size() - 2) == "[]") {
        return subTypeSuffix(t.substr(0, t.size() - 2), subst) + "[]";
    }
    if (!t.empty() && t.back() == '^') {
        return subTypeSuffix(t.substr(0, t.size() - 1), subst) + "^";
    }
    if (isAnonTupleType(t)) {
        std::string s = "(";
        bool first = true;
        for (auto& e : anonTupleElems(t)) {
            if (!first) s += ",";
            first = false;
            s += subTypeSuffix(e, subst);
        }
        return s + ")";
    }
    return subType(t, subst);
}

// --- Auto-promote rewrite state ---
//
// Stack of "names currently treated as auto-derefed" sets — one frame per
// nested block during a clone walk. instantiateTemplate seeds the initial
// frame from the template's auto-promoted params. cloneExpr's VarExpr arm
// wraps any matching name in DerefExpr; cloneStmt's AssignStmt arm converts
// to DerefAssignStmt; VarDeclStmt removes shadowing names from the top frame
// for the rest of the block.
static std::vector<std::set<std::string>> g_promoted_stack;

static bool isPromoted(const std::string& name) {
    if (g_promoted_stack.empty()) return false;
    return g_promoted_stack.back().count(name) > 0;
}

static void enterPromoteScope() {
    if (g_promoted_stack.empty()) g_promoted_stack.push_back({});
    else g_promoted_stack.push_back(g_promoted_stack.back());
}

static void exitPromoteScope() {
    if (!g_promoted_stack.empty()) g_promoted_stack.pop_back();
}

static void shadowPromoted(const std::string& name) {
    if (!g_promoted_stack.empty()) g_promoted_stack.back().erase(name);
}

// --- Expression deep clone with type substitution ---

// External linkage so other TUs (e.g. codegen_stmt.cpp's compound-assign
// desugar) can route through this single cloner with an empty subst.
std::unique_ptr<Expr> cloneExpr(const Expr& expr,
                                 const std::map<std::string, std::string>& subst);
static std::unique_ptr<Expr> cloneExprImpl(const Expr& expr,
                                            const std::map<std::string, std::string>& subst);

// Wrapper: propagate source-node file_id/tok to the cloned subtree's root so
// downstream errors caret at the right source location.
std::unique_ptr<Expr> cloneExpr(const Expr& expr,
                                 const std::map<std::string, std::string>& subst) {
    auto r = cloneExprImpl(expr, subst);
    if (r) { r->file_id = expr.file_id; r->tok = expr.tok; }
    return r;
}

static std::vector<std::unique_ptr<Expr>> cloneArgs(
    const std::vector<std::unique_ptr<Expr>>& args,
    const std::map<std::string, std::string>& subst) {
    std::vector<std::unique_ptr<Expr>> out;
    for (auto& a : args) out.push_back(cloneExpr(*a, subst));
    return out;
}

static std::unique_ptr<Expr> cloneExprImpl(const Expr& expr,
                                            const std::map<std::string, std::string>& subst) {
    if (auto* e = dynamic_cast<const IntLiteralExpr*>(&expr))
        return std::make_unique<IntLiteralExpr>(e->value, e->is_char_literal, e->is_nondecimal);

    if (auto* e = dynamic_cast<const FloatLiteralExpr*>(&expr))
        return std::make_unique<FloatLiteralExpr>(e->value);

    if (auto* e = dynamic_cast<const StringLiteralExpr*>(&expr))
        return std::make_unique<StringLiteralExpr>(e->value);

    if (dynamic_cast<const NullptrExpr*>(&expr))
        return std::make_unique<NullptrExpr>();

    if (auto* e = dynamic_cast<const VarExpr*>(&expr)) {
        auto var = std::make_unique<VarExpr>(e->name);
        if (isPromoted(e->name)) {
            // template-promoted T^ param — rewrite read sites to deref.
            var->file_id = e->file_id; var->tok = e->tok;
            return std::make_unique<DerefExpr>(std::move(var));
        }
        return var;
    }

    if (auto* e = dynamic_cast<const BinaryExpr*>(&expr))
        return std::make_unique<BinaryExpr>(e->op,
            cloneExpr(*e->left, subst), cloneExpr(*e->right, subst));

    if (auto* e = dynamic_cast<const UnaryExpr*>(&expr))
        return std::make_unique<UnaryExpr>(e->op, cloneExpr(*e->operand, subst));

    if (auto* e = dynamic_cast<const FieldAccessExpr*>(&expr))
        return std::make_unique<FieldAccessExpr>(cloneExpr(*e->object, subst), e->field);

    if (auto* e = dynamic_cast<const DerefExpr*>(&expr))
        return std::make_unique<DerefExpr>(cloneExpr(*e->operand, subst));

    if (auto* e = dynamic_cast<const AddrOfExpr*>(&expr))
        return std::make_unique<AddrOfExpr>(cloneExpr(*e->operand, subst));

    if (auto* e = dynamic_cast<const PostIncDerefExpr*>(&expr))
        return std::make_unique<PostIncDerefExpr>(cloneExpr(*e->operand, subst), e->op);

    if (auto* e = dynamic_cast<const ArrayIndexExpr*>(&expr))
        return std::make_unique<ArrayIndexExpr>(cloneExpr(*e->base, subst),
                                                cloneExpr(*e->index, subst));

    if (auto* e = dynamic_cast<const SliceExpr*>(&expr))
        return std::make_unique<SliceExpr>(cloneExpr(*e->base, subst),
            e->start ? cloneExpr(*e->start, subst) : nullptr,
            e->end   ? cloneExpr(*e->end,   subst) : nullptr);

    if (auto* e = dynamic_cast<const TypeConvExpr*>(&expr))
        return std::make_unique<TypeConvExpr>(subTypeSuffix(e->target_type, subst),
                                               cloneExpr(*e->operand, subst));

    if (auto* e = dynamic_cast<const PtrCastExpr*>(&expr))
        return std::make_unique<PtrCastExpr>(subTypeSuffix(e->target_type, subst),
                                              cloneExpr(*e->operand, subst));

    if (auto* e = dynamic_cast<const NewExpr*>(&expr))
        return std::make_unique<NewExpr>(subTypeSuffix(e->elem_type, subst),
                                          cloneExpr(*e->count, subst));

    if (auto* e = dynamic_cast<const NewScalarExpr*>(&expr)) {
        std::vector<std::unique_ptr<Expr>> args;
        for (auto& a : e->args) args.push_back(cloneExpr(*a, subst));
        return std::make_unique<NewScalarExpr>(subTypeSuffix(e->elem_type, subst), std::move(args));
    }

    if (auto* e = dynamic_cast<const PlacementNewExpr*>(&expr)) {
        std::vector<std::unique_ptr<Expr>> args;
        for (auto& a : e->args) args.push_back(cloneExpr(*a, subst));
        return std::make_unique<PlacementNewExpr>(cloneExpr(*e->addr, subst),
            subTypeSuffix(e->elem_type, subst), std::move(args));
    }

    if (auto* e = dynamic_cast<const SizeofExpr*>(&expr)) {
        auto se = std::make_unique<SizeofExpr>();
        // If the operand is a VarExpr naming a template parameter, substitute
        // through the type system (handles ^/[]/tuple suffixes); generic VarExpr
        // cloning leaves names untouched.
        if (auto* ve = dynamic_cast<const VarExpr*>(e->operand.get())) {
            se->operand = std::make_unique<VarExpr>(subTypeSuffix(ve->name, subst));
        } else if (e->operand) {
            se->operand = cloneExpr(*e->operand, subst);
        }
        return se;
    }

    if (auto* e = dynamic_cast<const MethodCallExpr*>(&expr)) {
        auto r = std::make_unique<MethodCallExpr>(cloneExpr(*e->object, subst),
                                                   e->method, cloneArgs(e->args, subst));
        return r;
    }

    if (auto* e = dynamic_cast<const CallExpr*>(&expr)) {
        auto r = std::make_unique<CallExpr>(e->callee, cloneArgs(e->args, subst));
        r->type_args = e->type_args; // type args may also need substitution
        for (auto& ta : r->type_args) ta = subTypeSuffix(ta, subst);
        return r;
    }

    if (auto* e = dynamic_cast<const TupleExpr*>(&expr)) {
        auto t = std::make_unique<TupleExpr>();
        for (auto& v : e->values) t->values.push_back(cloneExpr(*v, subst));
        return t;
    }

    if (auto* e = dynamic_cast<const StringifyExpr*>(&expr))
        return std::make_unique<StringifyExpr>(e->kind, e->operand ? cloneExpr(*e->operand, subst) : nullptr);

    throw CompileError{expr.file_id, expr.tok, "cloneExpr: unhandled expression type"};
}

// --- Statement deep clone with type substitution ---

static std::unique_ptr<BlockStmt> cloneBlock(const BlockStmt& block,
                                              const std::map<std::string, std::string>& subst);
static std::unique_ptr<Stmt> cloneStmt(const Stmt& stmt,
                                        const std::map<std::string, std::string>& subst);
static std::unique_ptr<Stmt> cloneStmtImpl(const Stmt& stmt,
                                            const std::map<std::string, std::string>& subst);

static std::unique_ptr<Stmt> cloneStmt(const Stmt& stmt,
                                        const std::map<std::string, std::string>& subst) {
    auto r = cloneStmtImpl(stmt, subst);
    if (r) { r->file_id = stmt.file_id; r->tok = stmt.tok; }
    return r;
}

static std::unique_ptr<Stmt> cloneStmtImpl(const Stmt& stmt,
                                            const std::map<std::string, std::string>& subst) {
    if (auto* s = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        std::vector<std::unique_ptr<Expr>> ctor_args;
        for (auto& a : s->ctor_args) ctor_args.push_back(cloneExpr(*a, subst));
        // The new local shadows any promoted name with the same identifier
        // for the rest of this scope frame.
        shadowPromoted(s->name);
        return std::make_unique<VarDeclStmt>(
            subTypeSuffix(s->type, subst), s->name,
            s->init ? cloneExpr(*s->init, subst) : nullptr,
            std::move(ctor_args), s->is_move);
    }

    if (auto* s = dynamic_cast<const AssignStmt*>(&stmt)) {
        auto value = cloneExpr(*s->value, subst);
        if (isPromoted(s->name)) {
            // b = expr  →  b^ = expr  (DerefAssignStmt — value-write through ref).
            auto var = std::make_unique<VarExpr>(s->name);
            var->file_id = stmt.file_id; var->tok = stmt.tok;
            return std::make_unique<DerefAssignStmt>(
                std::move(var), std::move(value), s->is_move);
        }
        return std::make_unique<AssignStmt>(s->name, std::move(value), s->is_move);
    }

    if (auto* s = dynamic_cast<const FieldAssignStmt*>(&stmt))
        return std::make_unique<FieldAssignStmt>(
            cloneExpr(*s->object, subst), s->field, cloneExpr(*s->value, subst));

    if (auto* s = dynamic_cast<const ReturnStmt*>(&stmt))
        return std::make_unique<ReturnStmt>(
            s->value ? cloneExpr(*s->value, subst) : nullptr);

    if (auto* s = dynamic_cast<const CallStmt*>(&stmt)) {
        std::vector<std::unique_ptr<Expr>> args;
        for (auto& a : s->args) args.push_back(cloneExpr(*a, subst));
        auto r = std::make_unique<CallStmt>(s->callee, std::move(args));
        r->type_args = s->type_args;
        for (auto& ta : r->type_args) ta = subTypeSuffix(ta, subst);
        return r;
    }

    if (auto* s = dynamic_cast<const MethodCallStmt*>(&stmt)) {
        std::vector<std::unique_ptr<Expr>> args;
        for (auto& a : s->args) args.push_back(cloneExpr(*a, subst));
        return std::make_unique<MethodCallStmt>(
            cloneExpr(*s->object, subst), s->method, std::move(args));
    }

    if (auto* s = dynamic_cast<const ExprStmt*>(&stmt))
        return std::make_unique<ExprStmt>(cloneExpr(*s->expr, subst));

    if (auto* s = dynamic_cast<const DeleteStmt*>(&stmt))
        return std::make_unique<DeleteStmt>(cloneExpr(*s->operand, subst));

    if (auto* s = dynamic_cast<const DerefAssignStmt*>(&stmt))
        return std::make_unique<DerefAssignStmt>(
            cloneExpr(*s->ptr, subst), cloneExpr(*s->value, subst), s->is_move);

    if (auto* s = dynamic_cast<const PostIncDerefAssignStmt*>(&stmt))
        return std::make_unique<PostIncDerefAssignStmt>(
            cloneExpr(*s->ptr, subst), s->op, cloneExpr(*s->value, subst), s->is_move);

    if (auto* s = dynamic_cast<const IndexAssignStmt*>(&stmt))
        return std::make_unique<IndexAssignStmt>(
            cloneExpr(*s->base, subst), cloneExpr(*s->index, subst),
            cloneExpr(*s->value, subst));

    if (auto* s = dynamic_cast<const SwapStmt*>(&stmt))
        return std::make_unique<SwapStmt>(
            cloneExpr(*s->lhs, subst), cloneExpr(*s->rhs, subst));

    if (auto* s = dynamic_cast<const IfStmt*>(&stmt)) {
        auto r = std::make_unique<IfStmt>();
        r->cond       = cloneExpr(*s->cond, subst);
        r->then_block = cloneBlock(*s->then_block, subst);
        if (s->else_block) r->else_block = cloneBlock(*s->else_block, subst);
        r->block_label = s->block_label;
        return r;
    }

    if (auto* s = dynamic_cast<const WhileStmt*>(&stmt)) {
        auto r = std::make_unique<WhileStmt>();
        r->cond            = cloneExpr(*s->cond, subst);
        r->body            = cloneBlock(*s->body, subst);
        r->block_label     = s->block_label;
        r->bottom_condition = s->bottom_condition;
        return r;
    }

    if (auto* s = dynamic_cast<const ForLongStmt*>(&stmt)) {
        auto r = std::make_unique<ForLongStmt>();
        for (auto& is : s->init_stmts) r->init_stmts.push_back(cloneStmt(*is, subst));
        if (s->cond) r->cond = cloneExpr(*s->cond, subst);
        r->update_block = cloneBlock(*s->update_block, subst);
        r->body         = cloneBlock(*s->body, subst);
        r->block_label  = s->block_label;
        return r;
    }

    if (auto* s = dynamic_cast<const BlockStmt*>(&stmt))
        return cloneBlock(*s, subst);

    if (auto* s = dynamic_cast<const BreakStmt*>(&stmt)) {
        auto r = std::make_unique<BreakStmt>();
        r->label = s->label; r->number = s->number;
        return r;
    }

    if (auto* s = dynamic_cast<const ContinueStmt*>(&stmt)) {
        auto r = std::make_unique<ContinueStmt>();
        r->label = s->label; r->number = s->number;
        return r;
    }

    if (auto* s = dynamic_cast<const SwitchStmt*>(&stmt)) {
        auto r = std::make_unique<SwitchStmt>();
        r->expr = cloneExpr(*s->expr, subst);
        r->block_label = s->block_label;
        for (auto& sc : s->cases) {
            SwitchCase nc;
            if (sc.value) nc.value = cloneExpr(*sc.value, subst);
            for (auto& st : sc.stmts) nc.stmts.push_back(cloneStmt(*st, subst));
            r->cases.push_back(std::move(nc));
        }
        return r;
    }

    if (auto* s = dynamic_cast<const ArrayDeclStmt*>(&stmt)) {
        auto r = std::make_unique<ArrayDeclStmt>();
        r->elem_type = subTypeSuffix(s->elem_type, subst);
        r->name = s->name;
        r->dims = s->dims;
        for (auto& v : s->init_values) r->init_values.push_back(cloneExpr(*v, subst));
        return r;
    }

    if (auto* s = dynamic_cast<const TupleDestructureStmt*>(&stmt)) {
        auto r = std::make_unique<TupleDestructureStmt>();
        for (auto& [t, n] : s->fields) r->fields.emplace_back(subTypeSuffix(t, subst), n);
        r->init = cloneExpr(*s->init, subst);
        return r;
    }

    if (auto* s = dynamic_cast<const CompoundAssignStmt*>(&stmt))
        return std::make_unique<CompoundAssignStmt>(
            cloneExpr(*s->lhs, subst), s->op, cloneExpr(*s->rhs, subst));

    if (auto* s = dynamic_cast<const NestedFunctionDefStmt*>(&stmt)) {
        auto r = std::make_unique<NestedFunctionDefStmt>();
        r->def.return_type = subTypeSuffix(s->def.return_type, subst);
        for (auto& [t, n] : s->def.tuple_return_fields)
            r->def.tuple_return_fields.emplace_back(subTypeSuffix(t, subst), n);
        r->def.name = s->def.name;
        for (auto& [t, n] : s->def.params)
            r->def.params.emplace_back(subTypeSuffix(t, subst), n);
        r->def.param_mutable = s->def.param_mutable;
        r->def.body = cloneBlock(*s->def.body, subst);
        return r;
    }

    throw CompileError{stmt.file_id, stmt.tok, "cloneStmt: unhandled statement type"};
}

static std::unique_ptr<BlockStmt> cloneBlock(const BlockStmt& block,
                                              const std::map<std::string, std::string>& subst) {
    auto r = std::make_unique<BlockStmt>();
    r->file_id = block.file_id;
    r->tok = block.tok;
    enterPromoteScope();
    for (auto& s : block.stmts) r->stmts.push_back(cloneStmt(*s, subst));
    exitPromoteScope();
    return r;
}

// --- Codegen::inferTypeArgs ---

std::vector<std::string> Codegen::inferTypeArgs(
    const FunctionDef& tmpl,
    const std::vector<std::unique_ptr<Expr>>& args)
{
    std::set<std::string> type_param_set(tmpl.type_params.begin(), tmpl.type_params.end());
    std::map<std::string, std::string> inferred;

    // Recursive unifier: bind type params occurring in `p` against `a`. Walks
    // parallel ^/[] suffixes (with arg-side auto-promote tolerance) and
    // recurses into anon-tuple structure, so a tuple-shaped parameter type like
    // `(char[], char[], T)^` extracts T from the matching slot of the actual
    // argument's anon-tuple type.
    std::function<void(std::string, std::string)> unify =
        [&](std::string p, std::string a) {
        auto strip = [](std::string& t) -> std::string {
            if (t.size() >= 2 && t.substr(t.size()-2) == "[]") {
                t = t.substr(0, t.size()-2); return "[]";
            }
            if (!t.empty() && t.back() == '^') {
                t.pop_back(); return "^";
            }
            return "";
        };
        // strip one suffix level. If both sides have a suffix, they must match;
        // if the arg has none, allow it (caller's auto-promote rule).
        std::string ps = strip(p);
        std::string as = strip(a);
        if (!ps.empty() && !as.empty() && ps != as) return;
        if (type_param_set.count(p)) {
            if (!inferred.count(p)) {
                inferred[p] = a;
                return;
            }
            static const std::map<std::string,int> irank =
                {{"int8",1},{"int16",2},{"int",3},{"int32",3},{"int64",4},{"intptr",4},
                 {"uint8",1},{"uint16",2},{"uint32",3},{"uint64",4}};
            if ((isRefType(inferred[p]) || isPtrType(inferred[p])) && a != inferred[p])
                error(std::string("cannot match type '" + a
                    + "' to template parameter '" + p
                    + "' (inferred as reference type '" + inferred[p] + "')"));
            auto ait = irank.find(inferred[p]);
            auto bit = irank.find(a);
            if (ait != irank.end() && bit != irank.end() && bit->second > ait->second)
                inferred[p] = a;
            else if (!slid_info_.count(inferred[p]) && slid_info_.count(a))
                inferred[p] = a;
            return;
        }
        if (isAnonTupleType(p) && isAnonTupleType(a)) {
            auto pe = anonTupleElems(p);
            auto ae = anonTupleElems(a);
            if (pe.size() != ae.size()) return;
            for (size_t i = 0; i < pe.size(); i++)
                unify(pe[i], ae[i]);
        }
        // literal match or shape mismatch: nothing to bind
    };

    for (int i = 0; i < (int)tmpl.params.size() && i < (int)args.size(); i++) {
        const std::string& ptype = tmpl.params[i].first;

        // get Slids type of actual argument
        std::string arg_slids;
        if (auto* ve = dynamic_cast<const VarExpr*>(args[i].get())) {
            auto tit = locals_.find(ve->name);
            if (tit != locals_.end()) arg_slids = tit->second.type;
        } else if (auto* ao = dynamic_cast<const AddrOfExpr*>(args[i].get())) {
            if (auto* ve = dynamic_cast<const VarExpr*>(ao->operand.get())) {
                auto tit = locals_.find(ve->name);
                if (tit != locals_.end()) arg_slids = tit->second.type + "^";
            }
        } else if (dynamic_cast<const IntLiteralExpr*>(args[i].get())) {
            arg_slids = "int";
        } else if (dynamic_cast<const FloatLiteralExpr*>(args[i].get())) {
            arg_slids = "float64";
        } else {
            arg_slids = inferSlidType(*args[i]);
        }
        if (arg_slids.empty()) continue;

        unify(ptype, arg_slids);
    }

    std::vector<std::string> result;
    for (auto& tp : tmpl.type_params) {
        auto it = inferred.find(tp);
        if (it == inferred.end())
            error(std::string("cannot infer template type '" + tp
                + "' for '" + tmpl.name + "': provide explicit type argument"));
        result.push_back(it->second);
    }
    return result;
}

// --- Codegen::emitTemplateDeclare ---

void Codegen::emitTemplateDeclare(const FunctionDef& fn) {
    bool is_sret = !fn.return_type.empty() && slid_info_.count(fn.return_type);
    std::string ret = (is_sret || fn.return_type.empty()) ? "void" : llvmType(fn.return_type);
    out_ << "declare " << ret << " @" << llvmGlobalName(fn.name) << "(";
    bool first = true;
    if (is_sret) {
        out_ << "ptr sret(%struct." << fn.return_type << ")";
        first = false;
    }
    for (auto& [pt, _] : fn.params) {
        if (!first) out_ << ", ";
        out_ << llvmType(pt);
        first = false;
    }
    out_ << ")\n";
}

// --- Codegen::recordSliEntry ---

void Codegen::recordSliEntry(const std::string& func_name,
                              const std::vector<std::string>& type_args,
                              const std::vector<std::string>& param_types,
                              const std::string& impl_module) {
    // template module (this overload's impl source)
    if (!impl_module.empty()) {
        if (sli_import_set_.insert("t:" + impl_module).second)
            sli_imports_.push_back({impl_module, true});
    }
    auto sit2 = slid_template_modules_.find(func_name);
    if (sit2 != slid_template_modules_.end()) {
        if (sli_import_set_.insert("t:" + sit2->second).second)
            sli_imports_.push_back({sit2->second, true});
    }
    // class modules for slid type args
    for (auto& ta : type_args) {
        auto sit = program_.slid_modules.find(ta);
        if (sit != program_.slid_modules.end()) {
            if (sli_import_set_.insert("c:" + sit->second).second)
                sli_imports_.push_back({sit->second, false});
        }
    }
    // class modules for slid types appearing in param types (e.g. String^ → import string)
    std::function<void(const std::string&)> scan = [&](const std::string& t) {
        std::string base = t;
        if (base.size() >= 2 && base.substr(base.size()-2) == "[]") base = base.substr(0, base.size()-2);
        else if (!base.empty() && base.back() == '^') base.pop_back();
        if (isAnonTupleType(base)) {
            for (auto& e : anonTupleElems(base)) scan(e);
            return;
        }
        auto sit = program_.slid_modules.find(base);
        if (sit != program_.slid_modules.end()) {
            if (sli_import_set_.insert("c:" + sit->second).second)
                sli_imports_.push_back({sit->second, false});
        }
    };
    for (auto& pt : param_types) scan(pt);
    // instantiation record (key includes param signature for overload disambiguation)
    std::string key = func_name;
    for (auto& ta : type_args) key += "__" + ta;
    for (auto& pt : param_types) key += "__" + paramTokenForType(pt);
    if (sli_instantiation_set_.insert(key).second)
        sli_instantiations_.push_back({func_name, type_args, param_types});
}

// --- Codegen::writeSliFile ---

void Codegen::writeSliFile(std::ostream& out) const {
    if (sli_imports_.empty() && sli_instantiations_.empty()) return;

    out << "/* class declarations. */\n";
    for (auto& [module, is_tmpl] : sli_imports_)
        if (!is_tmpl) out << "import " << module << ";\n";
    out << "\n";

    out << "/* template declarations. */\n";
    for (auto& [module, is_tmpl] : sli_imports_)
        if (is_tmpl) out << "import " << module << ";\n";
    out << "\n";

    out << "/* explicit template instantiations. */\n";
    for (auto& [func, type_args, param_types] : sli_instantiations_) {
        out << func << "<";
        for (int i = 0; i < (int)type_args.size(); i++) {
            if (i > 0) out << ", ";
            out << type_args[i];
        }
        out << ">(";
        for (int i = 0; i < (int)param_types.size(); i++) {
            if (i > 0) out << ", ";
            out << param_types[i];
        }
        out << ");\n";
    }
}

// --- Codegen::instantiateTemplate ---

std::string Codegen::instantiateTemplate(const TemplateFuncEntry& entry,
                                          const std::vector<std::string>& type_args,
                                          bool force) {
    const FunctionDef& tmpl = *entry.def;
    const std::string& name = tmpl.name;

    if (tmpl.type_params.size() != type_args.size())
        error(std::string("template '" + name + "': expected "
            + std::to_string(tmpl.type_params.size()) + " type argument(s), got "
            + std::to_string(type_args.size())));

    // build substitution map
    std::map<std::string, std::string> subst;
    for (int i = 0; i < (int)tmpl.type_params.size(); i++)
        subst[tmpl.type_params[i]] = type_args[i];

    // post-substitution param types (with class-type auto-promote to ref).
    // promoted[i] tracks which params got the value→ref promotion — used by
    // codegen to enable auto-deref on uses inside the instantiated body.
    std::vector<std::string> ptypes;
    std::vector<bool> promoted;
    for (auto& [ptype, pname] : tmpl.params) {
        std::string pt = subTypeSuffix(ptype, subst);
        bool p = false;
        if (slid_info_.count(pt)) { pt += "^"; p = true; }
        ptypes.push_back(pt);
        promoted.push_back(p);
    }

    // mangled name: <name>__<typeArg>...__<paramToken>...
    std::string mangled = name;
    for (auto& t : type_args) mangled += "__" + t;
    for (auto& pt : ptypes) mangled += "__" + paramTokenForType(pt);

    // already instantiated
    if (emitted_templates_.count(mangled)) return mangled;
    emitted_templates_.insert(mangled);

    // build concrete FunctionDef
    FunctionDef concrete;
    concrete.name = mangled;
    concrete.user_name = !tmpl.user_name.empty() ? tmpl.user_name : tmpl.name;
    concrete.return_type = subTypeSuffix(tmpl.return_type, subst);
    for (int i = 0; i < (int)tmpl.params.size(); i++)
        concrete.params.emplace_back(ptypes[i], tmpl.params[i].second);
    concrete.param_mutable = tmpl.param_mutable;
    concrete.param_auto_promoted = promoted;

    // Seed the cloner's promote stack with the auto-promoted param names.
    // cloneBlock will wrap reads of these names in DerefExpr and convert
    // assigns to DerefAssignStmt.
    std::set<std::string> promoted_names;
    for (int i = 0; i < (int)tmpl.params.size(); i++)
        if (i < (int)promoted.size() && promoted[i])
            promoted_names.insert(tmpl.params[i].second);
    g_promoted_stack.clear();
    g_promoted_stack.push_back(std::move(promoted_names));
    concrete.body = cloneBlock(*tmpl.body, subst);
    g_promoted_stack.clear();

    // register signatures so call sites work
    func_return_types_[mangled] = concrete.return_type;
    func_param_types_[mangled]  = ptypes;

    if (force || entry.is_local) {
        // inline: emit full definition
        if (force) exported_symbols_.insert(mangled); // explicit instantiation = linkable entry point
        pending_instantiations_.push_back(std::move(concrete));
    } else {
        // imported template: defer declare to module scope + record .sli entry
        recordSliEntry(name, type_args, ptypes, entry.impl_module);
        pending_declares_.push_back(std::move(concrete));
    }

    return mangled;
}

// --- Codegen::instantiateSlidTemplate ---

std::string Codegen::instantiateSlidTemplate(const std::string& name,
                                              const std::vector<std::string>& type_args,
                                              bool force) {
    auto tit = template_slids_.find(name);
    if (tit == template_slids_.end())
        error(std::string("unknown template class: " + name));
    const SlidDef& tmpl = *tit->second;

    if (tmpl.type_params.size() != type_args.size())
        error(std::string("template class '" + name + "': expected "
            + std::to_string(tmpl.type_params.size()) + " type argument(s), got "
            + std::to_string(type_args.size())));

    std::map<std::string, std::string> subst;
    for (int i = 0; i < (int)tmpl.type_params.size(); i++)
        subst[tmpl.type_params[i]] = type_args[i];

    std::string mangled = name;
    for (auto& t : type_args) mangled += "__" + t;

    if (emitted_slid_templates_.count(mangled)) return mangled;
    emitted_slid_templates_.insert(mangled);

    // build concrete SlidDef with type substitution
    SlidDef concrete;
    concrete.name = mangled;
    concrete.has_explicit_ctor_decl = tmpl.has_explicit_ctor_decl;
    concrete.has_explicit_dtor_decl = tmpl.has_explicit_dtor_decl;

    for (auto& f : tmpl.fields) {
        FieldDef fd;
        fd.type = subTypeSuffix(f.type, subst);
        fd.name = f.name;
        if (f.default_val) fd.default_val = cloneExpr(*f.default_val, subst);
        concrete.fields.push_back(std::move(fd));
    }
    if (tmpl.ctor_body)          concrete.ctor_body          = cloneBlock(*tmpl.ctor_body, subst);
    if (tmpl.explicit_ctor_body) concrete.explicit_ctor_body = cloneBlock(*tmpl.explicit_ctor_body, subst);
    if (tmpl.dtor_body)          concrete.dtor_body          = cloneBlock(*tmpl.dtor_body, subst);

    for (auto& m : tmpl.methods) {
        MethodDef md;
        md.name        = m.name;
        md.return_type = subTypeSuffix(m.return_type, subst);
        for (auto& [pt, pn] : m.params)
            md.params.emplace_back(subTypeSuffix(pt, subst), pn);
        if (m.body) md.body = cloneBlock(*m.body, subst);
        concrete.methods.push_back(std::move(md));
    }

    // register SlidInfo for the concrete type
    SlidInfo info;
    info.name = mangled;
    for (int i = 0; i < (int)concrete.fields.size(); i++) {
        info.field_index[concrete.fields[i].name] = i;
        info.field_types.push_back(concrete.fields[i].type);
    }
    info.own_field_count = (int)concrete.fields.size();
    info.inheritance_resolved = true; // templates with inheritance are deferred; treat as flat
    info.has_explicit_ctor = concrete.has_explicit_ctor_decl || (concrete.explicit_ctor_body != nullptr);
    info.has_dtor          = (concrete.dtor_body != nullptr) || concrete.has_explicit_dtor_decl;
    slid_info_[mangled] = info;

    // register method signatures so call sites resolve correctly
    for (auto& m : concrete.methods) {
        std::string base = mangled + "__" + m.name;
        std::vector<std::string> ptypes;
        for (auto& [pt, _] : m.params) ptypes.push_back(pt);
        func_return_types_[base] = m.return_type;
        func_param_types_[base]  = ptypes;
        method_overloads_[base].push_back({base, ptypes});
    }

    // store in the stable map
    concrete_slid_template_defs_[mangled] = std::move(concrete);
    SlidDef* concrete_ptr = &concrete_slid_template_defs_[mangled];

    // decide inline vs deferred: inline if forced, local template, or any type arg is a
    // non-importable local slid (can't be reconstructed in __instantiations.sl)
    bool any_local_type_arg = false;
    for (auto& ta : type_args) {
        if (slid_info_.count(ta) && !program_.slid_modules.count(ta)) {
            any_local_type_arg = true;
            break;
        }
    }
    if (force || local_slid_template_names_.count(name) || any_local_type_arg) {
        pending_slid_instantiations_.push_back(concrete_ptr);
    } else {
        // class templates don't carry function-template param overloads — record an
        // empty param-types signature so the .sli writer emits an empty `()` segment.
        std::string impl_module;
        auto mit = slid_template_modules_.find(name);
        if (mit != slid_template_modules_.end()) impl_module = mit->second;
        recordSliEntry(name, type_args, /*param_types=*/{}, impl_module);
        pending_slid_declares_.push_back(concrete_ptr);
    }

    return mangled;
}
