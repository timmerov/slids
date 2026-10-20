#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace token { struct List; }

namespace diagnostic {

struct Note {
    int file_id;
    int tok;
    std::string message;
};

struct Record {
    int file_id;
    int tok;
    std::string message;
    std::vector<Note> notes;
};

struct Sink {
    std::vector<Record> records;
    // THE INSTANTIATION CONTEXT: one note per template instance being built,
    // outermost first ("in the instantiation of 'X', required from here" at the
    // use that required it). report() appends them, innermost first, to an error
    // raised while any is open — the error points into the template; the notes
    // say how the program got there. Pushed/popped by InstantiationScope.
    std::vector<Note> context;
};

// RAII: open one instantiation-context frame for the guard's lifetime. A use
// site of {-1, -1} (an --instantiate demand) renders as a location-free note.
struct InstantiationScope {
    Sink& sink;
    InstantiationScope(Sink& s, int file_id, int tok, std::string const& what);
    ~InstantiationScope();
    InstantiationScope(InstantiationScope const&) = delete;
    InstantiationScope& operator=(InstantiationScope const&) = delete;
};

void report(Sink& sink, Record const& record);
bool hasErrors(Sink const& sink);
void render(token::List const& tokens, Sink const& sink, std::ostream& os);

}  // namespace diagnostic
