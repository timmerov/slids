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

// True for any recognized type spelling: numeric primitives, void, T[] (recursive).
// Classify uses this to validate declared / return type spellings up front.
bool isKnownType(std::string const& slids_type);

// Literal fit checks. Report a diagnostic and return false if the literal
// doesn't fit in dest_type. Int literal targeting a float type requires exact
// representability against the float's significand; float literal targeting
// an int type requires the value to be integer-valued and in range. The
// (file_id, tok) attribute the error to the literal's source token.
bool checkIntLiteralFits(std::string const& literal_text,
                         std::string const& dest_type,
                         int file_id, int tok,
                         diagnostic::Sink& diag);

bool checkFloatLiteralFits(std::string const& literal_text,
                           std::string const& dest_type,
                           int file_id, int tok,
                           diagnostic::Sink& diag);

// Silent variants used by the binary-op literal-flex rule.
bool intLiteralFits(std::string const& literal_text, std::string const& dest_type);
bool floatLiteralFits(std::string const& literal_text, std::string const& dest_type);

// Variable-to-variable conversion. Emits any needed LLVM op
// (sext/zext/fpext/sitofp/uitofp) and returns the new value name. On
// disallowed conversion reports a diagnostic attributed to (file_id, tok)
// and returns the original value as a fallback.
std::string convert(std::string const& src_val,
                    std::string const& src_type,
                    std::string const& dest_type,
                    int file_id, int tok,
                    std::ostream& out,
                    diagnostic::Sink& diag);

// "Smallest type large enough to hold either operand" per the widen.sl binary
// rule. Returns false if no built-in type fits both.
bool commonType(std::string const& t1, std::string const& t2, std::string& out);

}  // namespace widen
