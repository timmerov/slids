#include "parse.h"

#include <cassert>
#include <utility>

namespace parse {

int pushFrame(Tree& t) {
    int id = t.next_frame_id++;
    t.frame_id_stack.push_back(id);
    t.frame_entries_start_stack.push_back(t.live_entry_ids.size());
    return id;
}

void popFrame(Tree& t) {
    assert(!t.frame_id_stack.empty() && "popFrame: stack empty");
    std::size_t start = t.frame_entries_start_stack.back();
    t.live_entry_ids.resize(start);
    t.frame_entries_start_stack.pop_back();
    t.frame_id_stack.pop_back();
}

int allocFrameId(Tree& t) {
    return t.next_frame_id++;
}

int currentFrameId(Tree const& t) {
    assert(!t.frame_id_stack.empty() && "currentFrameId: no frame");
    return t.frame_id_stack.back();
}

int addEntry(Tree& t, Entry e) {
    e.parent_frame_id = currentFrameId(t);
    int id = static_cast<int>(t.entries.size());
    t.entries.push_back(std::move(e));
    t.live_entry_ids.push_back(id);
    return id;
}

int findInFrame(Tree const& t, int frame_id, std::string const& name) {
    for (int idx : t.live_entry_ids) {
        Entry const& e = t.entries[idx];
        // Namespace members share their enclosing frame's lifetime but are not
        // lexical occupants of it (they're reached by qualifier / open-ns chain);
        // a lexical dup check must skip them.
        if (e.owner_ns_frame >= 0) continue;
        if (e.parent_frame_id == frame_id && e.name == name) return idx;
    }
    return -1;
}

int findMemberDeclared(Tree const& t, int ns_frame, std::string const& name) {
    for (std::size_t id = 0; id < t.entries.size(); ++id) {
        Entry const& e = t.entries[id];
        if (e.name != name) continue;
        if (ns_frame == kGlobalFrame) {
            if (e.parent_frame_id == kGlobalFrame && e.owner_ns_frame < 0)
                return static_cast<int>(id);
        } else if (e.owner_ns_frame == ns_frame) {
            return static_cast<int>(id);
        }
    }
    return -1;
}

bool classHasField(ClassInfo const& info, std::string const& name) {
    for (std::string const& f : info.field_names)
        if (f == name) return true;
    return false;
}

bool userParamsEqual(std::vector<widen::TypeRef> const& a,
                     std::vector<widen::TypeRef> const& b) {
    if (a.size() != b.size()) return false;
    // Skip param 0 (`_$recv`): it differs by class between a base method and its
    // override, so it is never part of a method's user-visible signature.
    for (std::size_t i = 1; i < a.size(); ++i)
        if (widen::deepStrip(a[i]) != widen::deepStrip(b[i])) return false;
    return true;
}

int findMethodInFrame(Tree const& t, int ns_frame, std::string const& name,
                      std::vector<widen::TypeRef> const& params, int exclude) {
    if (ns_frame < 0) return -1;
    for (std::size_t id = 0; id < t.entries.size(); ++id) {
        if (static_cast<int>(id) == exclude) continue;
        Entry const& e = t.entries[id];
        if (e.kind == EntryKind::kFunction && e.owner_ns_frame == ns_frame
            && e.name == name && userParamsEqual(e.param_types, params))
            return static_cast<int>(id);
    }
    return -1;
}

widen::TypeRef baseTypeOf(ClassInfo const& info) {
    if (!info.field_names.empty() && info.field_names[0] == "_$base")
        return widen::strip(info.field_types[0]);
    return widen::kNoType;
}

// True for a ROOT virtual class — one carrying its own hidden `_$vptr` (the vtable
// pointer) as the unnamed first field. A derived virtual class inherits the vptr through
// `_$base` and has NO `_$vptr` of its own, so the two are mutually exclusive at slot 0.
bool hasVptr(ClassInfo const& info) {
    return !info.field_names.empty() && info.field_names[0] == "_$vptr";
}

widen::TypeRef classBaseType(Tree const& t, widen::TypeRef cls) {
    auto it = t.classes.find(widen::strip(cls));
    if (it == t.classes.end()) return widen::kNoType;
    return baseTypeOf(it->second);
}

std::vector<int> classAndBaseFrames(Tree const& t, widen::TypeRef cls) {
    std::vector<int> frames;
    // Backstop only: a cyclic base chain is the error reported by
    // resolve::checkClassByValueAcyclic (a base is a by-value `_$base` field); this
    // guard just bounds the walk so it can't hang before that diagnostic fires.
    int guard = static_cast<int>(t.classes.size()) + 2;
    for (widen::TypeRef c = widen::strip(cls); c != widen::kNoType && guard-- > 0; ) {
        int cid = classEntryForType(t, c);
        if (cid < 0) break;
        frames.push_back(t.entries[cid].ns_frame_id);
        c = classBaseType(t, c);
    }
    return frames;
}

int classEntryForType(Tree const& t, widen::TypeRef classType) {
    for (std::size_t id = 0; id < t.entries.size(); ++id) {
        Entry const& e = t.entries[id];
        if (e.kind == EntryKind::kClass && e.slids_type == classType)
            return static_cast<int>(id);
    }
    return -1;
}

int classEntryForFrame(Tree const& t, int ns_frame) {
    for (std::size_t id = 0; id < t.entries.size(); ++id) {
        Entry const& e = t.entries[id];
        if (e.kind == EntryKind::kClass && e.ns_frame_id == ns_frame)
            return static_cast<int>(id);
    }
    return -1;
}

int classNsFrame(Tree const& t, widen::TypeRef cls) {
    int cid = classEntryForType(t, widen::strip(cls));
    return cid < 0 ? -1 : t.entries[cid].ns_frame_id;
}

widen::TypeRef entryType(Tree const& t, int entry_id) {
    assert(entry_id >= 0 && entry_id < static_cast<int>(t.entries.size())
        && "entryType: out of range");
    return t.entries[entry_id].slids_type;
}

char const* implicitMemberNoun(std::string const& name) {
    if (name == "_$ctor")  return "a constructor";
    if (name == "_$dtor")  return "a destructor";
    if (name == "op=")     return "a copy operator";
    if (name == "op<--")   return "a move operator";
    if (name == "op<-->")  return "a swap operator";
    return "";
}

bool isImplicitMember(std::string const& name) {
    return implicitMemberNoun(name)[0] != '\0';
}

std::unique_ptr<Node> makeReceiverParam(widen::TypeRef type, int file_id, int tok) {
    auto n = std::make_unique<Node>();
    n->kind = Kind::kParam;
    n->name = "_$recv";
    n->file_id = file_id;
    n->tok = tok;
    n->name_tok = tok;
    n->return_type = type;
    return n;
}

// Deep copy of a subtree — every field a parse node carries, the unique_ptr
// vectors recursing with null slots preserved (a switch default's absent
// label, an empty construction slot). Serves template instantiation (resolve)
// and the param-default fill (classify clones the validated default node at
// each call site). MUST enumerate every Node field — keep in sync with Node.
std::unique_ptr<Node> cloneNode(Node const& n) {
    auto c = std::make_unique<Node>();
    c->kind = n.kind;
    c->name = n.name;
    c->text = n.text;
    c->return_type = n.return_type;
    c->return_type_seg_toks = n.return_type_seg_toks;
    c->nominal_type = n.nominal_type;
    c->inferred_type = n.inferred_type;
    c->op_type = n.op_type;
    c->alias_label = n.alias_label;
    c->strong_type = n.strong_type;
    c->file_id = n.file_id;
    c->tok = n.tok;
    c->name_tok = n.name_tok;
    c->resolved_entry_id = n.resolved_entry_id;
    c->range_dotdot_tok = n.range_dotdot_tok;
    c->label = n.label;
    c->loop_levels = n.loop_levels;
    c->is_const = n.is_const;
    c->const_method = n.const_method;
    c->is_global = n.is_global;
    c->global_group_id = n.global_group_id;
    c->is_reopen = n.is_reopen;
    c->is_incomplete = n.is_incomplete;
    c->is_construction = n.is_construction;
    c->is_temp_init = n.is_temp_init;
    c->ctor_no_args = n.ctor_no_args;
    c->class_op_chain = n.class_op_chain;
    c->op_collapse_head = n.op_collapse_head;
    c->op_bin_eid = n.op_bin_eid;
    c->op_un_eid = n.op_un_eid;
    c->op_aug_eid = n.op_aug_eid;
    c->op_eq_lhs_eid = n.op_eq_lhs_eid;
    c->op_eq_rhs_eid = n.op_eq_rhs_eid;
    c->op_move_eid = n.op_move_eid;
    c->parenless = n.parenless;
    c->class_conversion = n.class_conversion;
    c->agg_conv_spill = n.agg_conv_spill;
    c->is_mutable = n.is_mutable;
    c->tmpl_value_param = n.tmpl_value_param;
    c->is_virtual = n.is_virtual;
    c->is_pure = n.is_pure;
    c->is_foreign = n.is_foreign;
    c->bypass_virtual = n.bypass_virtual;
    c->default_move_init = n.default_move_init;
    c->default_swap_init = n.default_swap_init;
    c->construction_init = n.construction_init;
    c->infer_ref = n.infer_ref;
    c->quiet_diag = n.quiet_diag;
    c->require_homogeneous = n.require_homogeneous;
    c->non_completing = n.non_completing;
    c->qualifier = n.qualifier;
    c->qualifier_toks = n.qualifier_toks;
    c->global_qualified = n.global_qualified;
    c->type_params = n.type_params;
    c->type_param_toks = n.type_param_toks;
    c->tmpl_args = n.tmpl_args;
    c->tmpl_arg_toks = n.tmpl_arg_toks;
    c->param_types = n.param_types;
    c->captures = n.captures;
    c->capture_types = n.capture_types;
    c->self_entry_id = n.self_entry_id;
    for (auto const& d : n.dim_exprs)
        c->dim_exprs.push_back(d ? cloneNode(*d) : nullptr);
    for (auto const& ch : n.children)
        c->children.push_back(ch ? cloneNode(*ch) : nullptr);
    for (auto const& p : n.params)
        c->params.push_back(p ? cloneNode(*p) : nullptr);
    return c;
}

}  // namespace parse
