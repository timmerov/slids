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

int findInLiveScopes(Tree const& t, std::string const& name) {
    for (auto it = t.live_entry_ids.rbegin(); it != t.live_entry_ids.rend(); ++it) {
        if (t.entries[*it].name == name) return *it;
    }
    return -1;
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

std::string const& entryType(Tree const& t, int entry_id) {
    assert(entry_id >= 0 && entry_id < static_cast<int>(t.entries.size())
        && "entryType: out of range");
    return t.entries[entry_id].slids_type;
}

}  // namespace parse
