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

}  // namespace parse
