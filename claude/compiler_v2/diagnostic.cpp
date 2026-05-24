#include "diagnostic.h"

namespace diagnostic {

void report(Sink& sink, Record const& record) {
    sink.records.push_back(record);
}

}  // namespace diagnostic
