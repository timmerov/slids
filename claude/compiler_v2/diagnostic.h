#pragma once

#include <string>
#include <vector>

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

}  // namespace diagnostic
