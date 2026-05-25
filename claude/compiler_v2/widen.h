#pragma once

#include <iosfwd>
#include <string>

namespace diagnostic { struct Sink; }

namespace widen {

enum class Category { kBool, kSignedInt, kUnsignedInt, kFloat };

struct TypeKind {
    Category cat;
    int bits;
};

// Returns false if the slids type isn't a numeric primitive (e.g. void, char[]).
bool classify(std::string const& slids_type, TypeKind& out);

// Literal fit checks. Report a diagnostic and return false if the literal
// value doesn't fit in dest_type.
bool checkIntLiteralFits(std::string const& literal_text,
                         std::string const& dest_type,
                         diagnostic::Sink& diag);

bool checkFloatLiteralFits(std::string const& literal_text,
                           std::string const& dest_type,
                           diagnostic::Sink& diag);

// Variable-to-variable conversion. Emits any needed LLVM op (sext/zext/fpext)
// and returns the new value name. On disallowed conversion (truncation,
// signed→unsigned, cross-family, etc.) reports a diagnostic and returns the
// original value as a fallback.
std::string convert(std::string const& src_val,
                    std::string const& src_type,
                    std::string const& dest_type,
                    std::ostream& out,
                    diagnostic::Sink& diag);

}  // namespace widen
