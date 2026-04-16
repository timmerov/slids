// Template function instantiation — AST cloning + type substitution
#include "codegen.h"
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

// Substitute type params inside a type string that may have ^ or [] suffix.
// e.g. "T[]" with T->int becomes "int[]"
static std::string subTypeSuffix(const std::string& t,
                                  const std::map<std::string, std::string>& subst) {
    if (t.size() >= 2 && t.substr(t.size() - 2) == "[]") {
        return subType(t.substr(0, t.size() - 2), subst) + "[]";
    }
    if (!t.empty() && t.back() == '^') {
        return subType(t.substr(0, t.size() - 1), subst) + "^";
    }
    return subType(t, subst);
}

// --- Expression deep clone with type substitution ---

static std::unique_ptr<Expr> cloneExpr(const Expr& expr,
                                        const std::map<std::string, std::string>& subst);

static std::vector<std::unique_ptr<Expr>> cloneArgs(
    const std::vector<std::unique_ptr<Expr>>& args,
    const std::map<std::string, std::string>& subst) {
    std::vector<std::unique_ptr<Expr>> out;
    for (auto& a : args) out.push_back(cloneExpr(*a, subst));
    return out;
}

static std::unique_ptr<Expr> cloneExpr(const Expr& expr,
                                        const std::map<std::string, std::string>& subst) {
    if (auto* e = dynamic_cast<const IntLiteralExpr*>(&expr))
        return std::make_unique<IntLiteralExpr>(e->value, e->is_char_literal, e->is_nondecimal);

    if (auto* e = dynamic_cast<const FloatLiteralExpr*>(&expr))
        return std::make_unique<FloatLiteralExpr>(e->value);

    if (auto* e = dynamic_cast<const StringLiteralExpr*>(&expr))
        return std::make_unique<StringLiteralExpr>(e->value);

    if (dynamic_cast<const NullptrExpr*>(&expr))
        return std::make_unique<NullptrExpr>();

    if (auto* e = dynamic_cast<const VarExpr*>(&expr))
        return std::make_unique<VarExpr>(e->name);

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

    if (auto* e = dynamic_cast<const SizeofExpr*>(&expr)) {
        auto se = std::make_unique<SizeofExpr>();
        se->type_name = e->type_name.empty() ? "" : subTypeSuffix(e->type_name, subst);
        if (e->operand) se->operand = cloneExpr(*e->operand, subst);
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

    throw std::runtime_error("cloneExpr: unhandled expression type");
}

// --- Statement deep clone with type substitution ---

static std::unique_ptr<BlockStmt> cloneBlock(const BlockStmt& block,
                                              const std::map<std::string, std::string>& subst);

static std::unique_ptr<Stmt> cloneStmt(const Stmt& stmt,
                                        const std::map<std::string, std::string>& subst) {
    if (auto* s = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        std::vector<std::unique_ptr<Expr>> ctor_args;
        for (auto& a : s->ctor_args) ctor_args.push_back(cloneExpr(*a, subst));
        return std::make_unique<VarDeclStmt>(
            subTypeSuffix(s->type, subst), s->name,
            s->init ? cloneExpr(*s->init, subst) : nullptr,
            std::move(ctor_args), s->is_move);
    }

    if (auto* s = dynamic_cast<const AssignStmt*>(&stmt))
        return std::make_unique<AssignStmt>(s->name,
            cloneExpr(*s->value, subst), s->is_move);

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
            cloneExpr(*s->ptr, subst), cloneExpr(*s->value, subst));

    if (auto* s = dynamic_cast<const PostIncDerefAssignStmt*>(&stmt))
        return std::make_unique<PostIncDerefAssignStmt>(
            cloneExpr(*s->ptr, subst), s->op, cloneExpr(*s->value, subst));

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

    if (auto* s = dynamic_cast<const ForRangeStmt*>(&stmt)) {
        auto r = std::make_unique<ForRangeStmt>();
        r->var_type   = subTypeSuffix(s->var_type, subst);
        r->var_name   = s->var_name;
        r->range_start = cloneExpr(*s->range_start, subst);
        r->range_end   = cloneExpr(*s->range_end, subst);
        r->body        = cloneBlock(*s->body, subst);
        r->block_label = s->block_label;
        return r;
    }

    if (auto* s = dynamic_cast<const ForEnumStmt*>(&stmt)) {
        auto r = std::make_unique<ForEnumStmt>();
        r->var_type   = s->var_type;
        r->var_name   = s->var_name;
        r->enum_name  = s->enum_name;
        r->body       = cloneBlock(*s->body, subst);
        r->block_label = s->block_label;
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

    throw std::runtime_error("cloneStmt: unhandled statement type");
}

static std::unique_ptr<BlockStmt> cloneBlock(const BlockStmt& block,
                                              const std::map<std::string, std::string>& subst) {
    auto r = std::make_unique<BlockStmt>();
    for (auto& s : block.stmts) r->stmts.push_back(cloneStmt(*s, subst));
    return r;
}

// --- Codegen::instantiateTemplate ---

std::string Codegen::instantiateTemplate(const std::string& name,
                                          const std::vector<std::string>& type_args) {
    auto tit = template_funcs_.find(name);
    if (tit == template_funcs_.end())
        throw std::runtime_error("unknown template function: " + name);
    const FunctionDef& tmpl = *tit->second;

    if (tmpl.type_params.size() != type_args.size())
        throw std::runtime_error("template '" + name + "': expected "
            + std::to_string(tmpl.type_params.size()) + " type argument(s), got "
            + std::to_string(type_args.size()));

    // build substitution map
    std::map<std::string, std::string> subst;
    for (int i = 0; i < (int)tmpl.type_params.size(); i++)
        subst[tmpl.type_params[i]] = type_args[i];

    // compute mangled name: add__int
    std::string mangled = name;
    for (auto& t : type_args) mangled += "__" + t;

    // already instantiated
    if (emitted_templates_.count(mangled)) return mangled;
    emitted_templates_.insert(mangled);

    // build concrete FunctionDef
    FunctionDef concrete;
    concrete.name = mangled;
    concrete.return_type = subTypeSuffix(tmpl.return_type, subst);
    for (auto& [ptype, pname] : tmpl.params)
        concrete.params.emplace_back(subTypeSuffix(ptype, subst), pname);
    concrete.body = cloneBlock(*tmpl.body, subst);

    // register signatures so call sites work
    std::vector<std::string> ptypes;
    for (auto& [pt, _] : concrete.params) ptypes.push_back(pt);
    func_return_types_[mangled] = concrete.return_type;
    func_param_types_[mangled]  = ptypes;

    // queue for emission after regular functions
    pending_instantiations_.push_back(std::move(concrete));

    return mangled;
}
