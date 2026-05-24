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
};

void report(Sink& sink, Record const& record);
bool hasErrors(Sink const& sink);
void render(token::List const& tokens, Sink const& sink, std::ostream& os);

}  // namespace diagnostic
