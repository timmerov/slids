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

widen::TypeRef baseTypeOf(ClassInfo const& info) {
    if (!info.field_names.empty() && info.field_names[0] == "_$base")
        return widen::strip(info.field_types[0]);
    return widen::kNoType;
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

widen::TypeRef entryType(Tree const& t, int entry_id) {
    assert(entry_id >= 0 && entry_id < static_cast<int>(t.entries.size())
        && "entryType: out of range");
    return t.entries[entry_id].slids_type;
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

}  // namespace parse
