#include "widen.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "diagnostic.h"

namespace widen {

bool classify(std::string const& t, TypeKind& out) {
    if (t == "bool")    { out = {Category::kBool, 1}; return true; }
    if (t == "char")    { out = {Category::kUnsignedInt, 8}; return true; }
    if (t == "int8")    { out = {Category::kSignedInt, 8}; return true; }
    if (t == "int16")   { out = {Category::kSignedInt, 16}; return true; }
    if (t == "int" || t == "int32") { out = {Category::kSignedInt, 32}; return true; }
    if (t == "int64" || t == "intptr") { out = {Category::kSignedInt, 64}; return true; }
    if (t == "uint8")   { out = {Category::kUnsignedInt, 8}; return true; }
    if (t == "uint16")  { out = {Category::kUnsignedInt, 16}; return true; }
    if (t == "uint" || t == "uint32") { out = {Category::kUnsignedInt, 32}; return true; }
    if (t == "uint64")  { out = {Category::kUnsignedInt, 64}; return true; }
    if (t == "float" || t == "float32") { out = {Category::kFloat, 32}; return true; }
    if (t == "float64") { out = {Category::kFloat, 64}; return true; }
    return false;
}

// Structured reader: a primitive's (cat, bits) come straight off the interned
// Type — no re-lex. Non-primitive forms are not numeric.
bool classify(TypeRef ref, TypeKind& out) {
    Type const& t = get(ref);
    if (t.form == Type::Form::kAlias) return classify(t.underlying, out);  // see through
    if (t.form != Type::Form::kPrimitive) return false;
    out = {t.cat, t.bits};
    return true;
}

namespace {

std::string llvmIntType(int bits) {
    return std::string("i") + std::to_string(bits);
}

std::string llvmFloatType(int bits) {
    return (bits == 32) ? "float" : "double";
}

bool parseSignedDigits(std::string const& text, bool& negative, uint64_t& mag) {
    if (text.empty()) return false;
    char const* s = text.c_str();
    negative = (*s == '-');
    if (negative) s++;
    if (*s == '\0') return false;
    char* end = nullptr;
    errno = 0;
    mag = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (errno == ERANGE) return false;
    return true;
}

bool intFitsSigned(uint64_t mag, bool negative, int bits) {
    if (bits >= 64) {
        if (negative) return mag <= (uint64_t)9223372036854775807ULL + 1ULL;
        return mag <= (uint64_t)9223372036854775807ULL;
    }
    if (negative) {
        uint64_t max_neg = ((uint64_t)1) << (bits - 1);
        return mag <= max_neg;
    }
    uint64_t max_pos = (((uint64_t)1) << (bits - 1)) - 1ULL;
    return mag <= max_pos;
}

bool intFitsUnsigned(uint64_t mag, bool negative, int bits) {
    if (negative) return false;
    if (bits >= 64) return true;
    uint64_t max_val = (((uint64_t)1) << bits) - 1ULL;
    return mag <= max_val;
}

std::string canonicalSigned(int bits) {
    if (bits <= 8) return "int8";
    if (bits <= 16) return "int16";
    if (bits <= 32) return "int32";
    return "int64";
}

std::string canonicalUnsigned(int bits) {
    if (bits <= 8) return "uint8";
    if (bits <= 16) return "uint16";
    if (bits <= 32) return "uint32";
    return "uint64";
}

std::string canonicalFloat(int bits) {
    return (bits == 32) ? "float32" : "float64";
}

// The four width-less ("no-width") spellings the language prefers to keep:
// char (u8), int (s32), uint (u32), float (f32). bool is deliberately excluded.
bool isNoWidth(std::string const& t) {
    return t == "char" || t == "int" || t == "uint" || t == "float";
}

// The no-width spelling for a (category, bits), or "" when none exists.
std::string noWidthName(Category cat, int bits) {
    if (cat == Category::kUnsignedInt && bits == 8)  return "char";
    if (cat == Category::kSignedInt   && bits == 32) return "int";
    if (cat == Category::kUnsignedInt && bits == 32) return "uint";
    if (cat == Category::kFloat       && bits == 32) return "float";
    return "";
}

// Choose the spelling for a common type of category `cat`/`bits` derived from
// operands spelled `t1`/`t2`. Precedence: (1) an operand's EXPLICIT width name
// that matches the result type wins (the author pinned that width — honor it,
// e.g. `int + int32 -> int32`, `intptr` preserved); (2) otherwise the no-width
// name, but only when a no-width operand contributed it (so two width-named
// operands never collapse to a no-width type); (3) otherwise the canonical
// width name.
std::string spellCommon(Category cat, int bits,
                        std::string const& t1, std::string const& t2) {
    std::string const* ops[2] = {&t1, &t2};
    for (std::string const* t : ops) {
        TypeKind k;
        if (!isNoWidth(*t) && classify(*t, k) && k.cat == cat && k.bits == bits) {
            return *t;
        }
    }
    std::string nw = noWidthName(cat, bits);
    if (!nw.empty() && (isNoWidth(t1) || isNoWidth(t2))) return nw;
    if (cat == Category::kFloat) return canonicalFloat(bits);
    if (cat == Category::kUnsignedInt) return canonicalUnsigned(bits);
    return canonicalSigned(bits);
}

void reportIntFit(diagnostic::Sink& diag, std::string const& literal,
                  std::string const& dest_type, int file_id, int tok) {
    (void)literal;   // n/a: caret-rendered source carries the spelling
    diagnostic::report(diag, {file_id, tok,
        "Integer literal does not fit in '" + dest_type + "'.", {}});
}

void reportFloatFit(diagnostic::Sink& diag, std::string const& literal,
                    std::string const& dest_type, int file_id, int tok) {
    (void)literal;
    diagnostic::report(diag, {file_id, tok,
        "Float literal does not fit in '" + dest_type + "'.", {}});
}

int nextTmpId() {
    static int n = 0;
    return n++;
}

std::string newWidenTmp() {
    return std::string("%wid_") + std::to_string(nextTmpId());
}

// Loud-fit cores — dest already classified to `tk`; `dest_s` is the spelling for
// messages. Shared by the std::string and TypeRef check overloads (which differ
// only in how they classify the dest — a TypeRef classify sees through kAlias).
bool checkIntFitsCore(std::string const& literal_text, TypeKind tk,
                      std::string const& dest_s, int file_id, int tok,
                      diagnostic::Sink& diag) {
    bool negative = false;
    uint64_t mag = 0;
    if (!parseSignedDigits(literal_text, negative, mag)) {
        reportIntFit(diag, literal_text, dest_s, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kFloat) {
        diagnostic::report(diag, {file_id, tok,
            "Cannot implicitly convert 'int' to '" + dest_s
            + "'; use an explicit type conversion.", {}});
        return false;
    }
    if (tk.cat == Category::kBool) {
        if (!negative && (mag == 0 || mag == 1)) return true;
        reportIntFit(diag, literal_text, dest_s, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kSignedInt && !intFitsSigned(mag, negative, tk.bits)) {
        reportIntFit(diag, literal_text, dest_s, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kUnsignedInt && !intFitsUnsigned(mag, negative, tk.bits)) {
        reportIntFit(diag, literal_text, dest_s, file_id, tok);
        return false;
    }
    return true;
}

bool checkFloatFitsCore(std::string const& literal_text, TypeKind tk,
                        std::string const& dest_s, int file_id, int tok,
                        diagnostic::Sink& diag) {
    errno = 0;
    double v = std::strtod(literal_text.c_str(), nullptr);
    if (errno == ERANGE) {
        reportFloatFit(diag, literal_text, dest_s, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kFloat) {
        double m = (tk.bits == 32) ? (double)FLT_MAX : DBL_MAX;
        if (v > m || v < -m) {
            reportFloatFit(diag, literal_text, dest_s, file_id, tok);
            return false;
        }
        return true;
    }
    diagnostic::report(diag, {file_id, tok,
        "Cannot implicitly convert 'float' to '" + dest_s
        + "'; use an explicit type conversion.", {}});
    return false;
}

}  // namespace

bool checkIntLiteralFits(std::string const& literal_text,
                         std::string const& dest_type,
                         int file_id, int tok,
                         diagnostic::Sink& diag) {
    if (dest_type.empty()) return true;
    TypeKind tk;
    if (!classify(dest_type, tk)) {
        reportIntFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    return checkIntFitsCore(literal_text, tk, dest_type, file_id, tok, diag);
}

bool checkFloatLiteralFits(std::string const& literal_text,
                           std::string const& dest_type,
                           int file_id, int tok,
                           diagnostic::Sink& diag) {
    if (dest_type.empty()) return true;
    TypeKind tk;
    if (!classify(dest_type, tk)) {
        reportFloatFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    return checkFloatFitsCore(literal_text, tk, dest_type, file_id, tok, diag);
}

// Silent variant — returns false on any parse failure / overflow / disallowed
// conversion without reporting. Callers decide whether to fire a diagnostic
// (typically the loud variant checkIntLiteralFits does, while literal-flex
// callers in classify/constfold treat false as "doesn't fit; fall back to
// defaultLiteralType / partner type"). user notified, accepts state.
// Kind-based cores — shared by the std::string and TypeRef overloads, which
// differ only in how they classify the destination (a TypeRef classify sees
// through a future kAlias to its underlying for free).
static bool intLiteralFitsKind(std::string const& literal_text, TypeKind tk) {
    bool negative = false;
    uint64_t mag = 0;
    if (!parseSignedDigits(literal_text, negative, mag)) return false;
    if (tk.cat == Category::kFloat) return false;  // no silent int → float
    if (tk.cat == Category::kBool) return !negative && (mag == 0 || mag == 1);
    if (tk.cat == Category::kSignedInt) return intFitsSigned(mag, negative, tk.bits);
    if (tk.cat == Category::kUnsignedInt) return intFitsUnsigned(mag, negative, tk.bits);
    return false;
}

static bool floatLiteralFitsKind(std::string const& literal_text, TypeKind tk) {
    errno = 0;
    double v = std::strtod(literal_text.c_str(), nullptr);
    if (errno == ERANGE) return false;
    if (tk.cat == Category::kFloat) {
        double m = (tk.bits == 32) ? (double)FLT_MAX : DBL_MAX;
        return v <= m && v >= -m;
    }
    return false;   // no silent float → int-class
}

bool intLiteralFits(std::string const& literal_text, std::string const& dest_type) {
    if (dest_type.empty()) return true;
    TypeKind tk;
    if (!classify(dest_type, tk)) return false;
    return intLiteralFitsKind(literal_text, tk);
}

bool floatLiteralFits(std::string const& literal_text, std::string const& dest_type) {
    if (dest_type.empty()) return true;
    TypeKind tk;
    if (!classify(dest_type, tk)) return false;
    return floatLiteralFitsKind(literal_text, tk);
}

bool intLiteralFits(std::string const& literal_text, TypeRef dest) {
    if (dest == kNoType) return true;
    TypeKind tk;
    if (!classify(dest, tk)) return false;
    return intLiteralFitsKind(literal_text, tk);
}

bool floatLiteralFits(std::string const& literal_text, TypeRef dest) {
    if (dest == kNoType) return true;
    TypeKind tk;
    if (!classify(dest, tk)) return false;
    return floatLiteralFitsKind(literal_text, tk);
}

std::string convert(std::string const& src_val,
                    std::string const& src_type,
                    std::string const& dest_type,
                    int file_id, int tok,
                    std::ostream& out,
                    diagnostic::Sink& diag) {
    if (src_type == dest_type) return src_val;

    // Pointer reinterpretation. Every pointer is LLVM `ptr` and `intptr` is
    // `i64`, so a pointer↔pointer cast is a value no-op; only the pointer↔intptr
    // boundary emits an instruction. classify (above) has already approved the
    // cast — here we just lower it. A pointer-ish spelling is `^`, `[]`, or the
    // typeless null `anyptr`.
    TypeRef src_ref = intern(src_type), dest_ref = intern(dest_type);
    auto isPtrish = [](TypeRef r) {
        Type::Form f = form(r);
        return f == Type::Form::kPointer || f == Type::Form::kIterator
            || f == Type::Form::kAnyptr;
    };
    bool src_ptr = isPtrish(src_ref), dest_ptr = isPtrish(dest_ref);
    if (src_ptr && dest_ptr) return src_val;             // ptr ↔ ptr: no-op
    if (src_ptr && dest_type == "intptr") {
        std::string tmp = newWidenTmp();
        out << "  " << tmp << " = ptrtoint ptr " << src_val << " to i64\n";
        return tmp;
    }
    if (src_type == "intptr" && dest_ptr) {
        std::string tmp = newWidenTmp();
        out << "  " << tmp << " = inttoptr i64 " << src_val << " to ptr\n";
        return tmp;
    }

    TypeKind src_tk, dest_tk;
    // A pointer source that survives to here has a non-pointer destination. The
    // only legitimate such case is a discard (dest_type empty / non-scalar — the
    // classify() below fails and returns the value untouched). A pointer flowing
    // into a SCALAR destination would silently store a `ptr` as an integer
    // (invalid LL): classify (cast-target + implicit-assign rules, including
    // kStoreStmt) rejects every ptr->non-intptr conversion, so this is
    // unreachable — assert rather than emit bad IR if a gate is ever missed.
    {
        TypeKind probe;
        assert(!(src_ptr && classify(dest_ref, probe))
            && "convert: ungated pointer->scalar conversion reached codegen");
    }
    if (!classify(src_ref, src_tk) || !classify(dest_ref, dest_tk)) {
        return src_val;
    }

    if (src_tk.cat == dest_tk.cat && src_tk.bits == dest_tk.bits) return src_val;

    auto emitSext = [&](std::string const& src_ll, std::string const& dest_ll) {
        std::string tmp = newWidenTmp();
        out << "  " << tmp << " = sext " << src_ll << " " << src_val
            << " to " << dest_ll << "\n";
        return tmp;
    };
    auto emitZext = [&](std::string const& src_ll, std::string const& dest_ll) {
        std::string tmp = newWidenTmp();
        out << "  " << tmp << " = zext " << src_ll << " " << src_val
            << " to " << dest_ll << "\n";
        return tmp;
    };
    auto emitFpext = [&](std::string const& src_ll, std::string const& dest_ll) {
        std::string tmp = newWidenTmp();
        out << "  " << tmp << " = fpext " << src_ll << " " << src_val
            << " to " << dest_ll << "\n";
        return tmp;
    };
    auto narrow = [&]() {
        diagnostic::report(diag, {file_id, tok,
            "Cannot implicitly narrow '" + src_type + "' to '" + dest_type
            + "'; use an explicit type conversion.", {}});
        return src_val;
    };
    auto convertErr = [&](char const* tail) {
        diagnostic::report(diag, {file_id, tok,
            std::string("Cannot implicitly convert '") + src_type + "' to '" + dest_type
            + "' (" + tail + "); use an explicit type conversion.", {}});
        return src_val;
    };
    auto convertErrPlain = [&]() {
        diagnostic::report(diag, {file_id, tok,
            std::string("Cannot implicitly convert '") + src_type + "' to '" + dest_type
            + "'; use an explicit type conversion.", {}});
        return src_val;
    };

    if (src_tk.cat == Category::kBool) {
        if (dest_tk.cat == Category::kSignedInt || dest_tk.cat == Category::kUnsignedInt) {
            return emitZext("i1", llvmIntType(dest_tk.bits));
        }
        // bool → float is cross-family: no silent mix.
        return convertErrPlain();
    }

    if (src_tk.cat == Category::kSignedInt && dest_tk.cat == Category::kSignedInt) {
        if (dest_tk.bits > src_tk.bits)
            return emitSext(llvmIntType(src_tk.bits), llvmIntType(dest_tk.bits));
        return narrow();
    }

    if (src_tk.cat == Category::kUnsignedInt && dest_tk.cat == Category::kUnsignedInt) {
        if (dest_tk.bits > src_tk.bits)
            return emitZext(llvmIntType(src_tk.bits), llvmIntType(dest_tk.bits));
        return narrow();
    }

    if (src_tk.cat == Category::kUnsignedInt && dest_tk.cat == Category::kSignedInt) {
        if (dest_tk.bits > src_tk.bits)
            return emitZext(llvmIntType(src_tk.bits), llvmIntType(dest_tk.bits));
        return convertErr("unsigned to same-width signed");
    }

    if (src_tk.cat == Category::kSignedInt && dest_tk.cat == Category::kUnsignedInt) {
        return convertErr("signed \xe2\x86\x92 unsigned");
    }

    if (src_tk.cat == Category::kFloat && dest_tk.cat == Category::kFloat) {
        if (dest_tk.bits > src_tk.bits)
            return emitFpext(llvmFloatType(src_tk.bits), llvmFloatType(dest_tk.bits));
        return narrow();
    }

    // int-class → float and float → int-class are cross-family: no silent mix.
    return convertErrPlain();
}

std::string convertExplicit(std::string const& src_val,
                            TypeRef src, TypeRef dest,
                            std::ostream& out) {
    TypeRef src_ref = deepStrip(src), dest_ref = deepStrip(dest);
    if (src_ref == dest_ref) return src_val;

    auto isPtrish = [](TypeRef r) {
        Type::Form f = form(r);
        return f == Type::Form::kPointer || f == Type::Form::kIterator
            || f == Type::Form::kAnyptr;
    };
    TypeKind src_tk, dest_tk;
    bool dest_num = classify(dest_ref, dest_tk);
    assert(dest_num && "convertExplicit: non-value destination reached codegen");

    // Pointer source — only `intptr` (ptrtoint) or `bool` (non-null test); both
    // gated by classify, so anything else is a missed gate.
    if (isPtrish(src_ref)) {
        std::string tmp = newWidenTmp();
        if (spell(dest_ref) == "intptr") {
            out << "  " << tmp << " = ptrtoint ptr " << src_val << " to i64\n";
        } else if (dest_tk.cat == Category::kBool) {
            out << "  " << tmp << " = icmp ne ptr " << src_val << ", null\n";
        } else {
            assert(false && "convertExplicit: pointer source to non-bool/intptr");
            return src_val;
        }
        return tmp;
    }

    bool src_num = classify(src_ref, src_tk);
    assert(src_num && "convertExplicit: non-value source reached codegen");
    if (src_tk.cat == dest_tk.cat && src_tk.bits == dest_tk.bits) return src_val;

    // Destination bool — a nonzero test (NOT a truncation).
    if (dest_tk.cat == Category::kBool) {
        std::string tmp = newWidenTmp();
        if (src_tk.cat == Category::kFloat) {
            out << "  " << tmp << " = fcmp une " << llvmFloatType(src_tk.bits)
                << " " << src_val << ", 0.0\n";
        } else {
            out << "  " << tmp << " = icmp ne " << llvmIntType(src_tk.bits)
                << " " << src_val << ", 0\n";
        }
        return tmp;
    }

    // Destination float.
    if (dest_tk.cat == Category::kFloat) {
        std::string dll = llvmFloatType(dest_tk.bits);
        std::string tmp = newWidenTmp();
        if (src_tk.cat == Category::kFloat) {
            char const* opc = (dest_tk.bits > src_tk.bits) ? "fpext" : "fptrunc";
            out << "  " << tmp << " = " << opc << " " << llvmFloatType(src_tk.bits)
                << " " << src_val << " to " << dll << "\n";
        } else {
            // bool / unsigned / char -> uitofp; signed -> sitofp.
            char const* opc = (src_tk.cat == Category::kSignedInt) ? "sitofp" : "uitofp";
            out << "  " << tmp << " = " << opc << " " << llvmIntType(src_tk.bits)
                << " " << src_val << " to " << dll << "\n";
        }
        return tmp;
    }

    // Destination integer (signed / unsigned / char).
    std::string dll = llvmIntType(dest_tk.bits);
    std::string tmp = newWidenTmp();
    if (src_tk.cat == Category::kFloat) {
        char const* opc = (dest_tk.cat == Category::kSignedInt) ? "fptosi" : "fptoui";
        out << "  " << tmp << " = " << opc << " " << llvmFloatType(src_tk.bits)
            << " " << src_val << " to " << dll << "\n";
        return tmp;
    }
    std::string sll = llvmIntType(src_tk.bits);
    if (dest_tk.bits == src_tk.bits) return src_val;   // same width: sign reinterpret is free
    if (dest_tk.bits > src_tk.bits) {
        // Extend by SOURCE signedness (sext signed; zext unsigned / char / bool).
        char const* opc = (src_tk.cat == Category::kSignedInt) ? "sext" : "zext";
        out << "  " << tmp << " = " << opc << " " << sll << " " << src_val
            << " to " << dll << "\n";
    } else {
        out << "  " << tmp << " = trunc " << sll << " " << src_val
            << " to " << dll << "\n";
    }
    return tmp;
}

bool checkIntLiteralFits(std::string const& literal_text, TypeRef dest,
                         int file_id, int tok, diagnostic::Sink& diag) {
    if (dest == kNoType) return true;
    TypeKind tk;
    std::string dest_s = spell(dest);
    if (!classify(dest, tk)) {            // classify(TypeRef) sees through kAlias
        reportIntFit(diag, literal_text, dest_s, file_id, tok);
        return false;
    }
    return checkIntFitsCore(literal_text, tk, dest_s, file_id, tok, diag);
}

bool checkFloatLiteralFits(std::string const& literal_text, TypeRef dest,
                           int file_id, int tok, diagnostic::Sink& diag) {
    if (dest == kNoType) return true;
    TypeKind tk;
    std::string dest_s = spell(dest);
    if (!classify(dest, tk)) {
        reportFloatFit(diag, literal_text, dest_s, file_id, tok);
        return false;
    }
    return checkFloatFitsCore(literal_text, tk, dest_s, file_id, tok, diag);
}

std::string convert(std::string const& src_val,
                    TypeRef src, TypeRef dest,
                    int file_id, int tok,
                    std::ostream& out,
                    diagnostic::Sink& diag) {
    // Strip aliases to the underlying so the string convert's classify works (an
    // alias widening must still emit its sext/zext/fpext). The string convert is
    // codegen-only — classify already validated, so message-spelling is moot here.
    std::string s = (src == kNoType) ? std::string() : spell(strip(src));
    std::string d = (dest == kNoType) ? std::string() : spell(strip(dest));
    return convert(src_val, s, d, file_id, tok, out, diag);
}

bool commonType(std::string const& t1, std::string const& t2, std::string& out) {
    if (t1 == t2) { out = t1; return true; }
    TypeKind k1, k2;
    if (!classify(t1, k1) || !classify(t2, k2)) return false;
    if (k1.cat == k2.cat && k1.bits == k2.bits) { out = t1; return true; }

    bool int1 = k1.cat == Category::kBool
             || k1.cat == Category::kSignedInt
             || k1.cat == Category::kUnsignedInt;
    bool int2 = k2.cat == Category::kBool
             || k2.cat == Category::kSignedInt
             || k2.cat == Category::kUnsignedInt;

    if (int1 && int2) {
        // bool classifies as kBool (1-bit, unsigned-like); s1/s2 false for it.
        bool s1 = (k1.cat == Category::kSignedInt);
        bool s2 = (k2.cat == Category::kSignedInt);
        Category cat;
        int bits;
        if (s1 == s2) {
            cat = s1 ? Category::kSignedInt : Category::kUnsignedInt;
            bits = std::max(k1.bits, k2.bits);
        } else {
            int signed_bits   = s1 ? k1.bits : k2.bits;
            int unsigned_bits = s1 ? k2.bits : k1.bits;
            bits = std::max(signed_bits, unsigned_bits + 1);
            if (bits > 64) return false;
            cat = Category::kSignedInt;
        }
        out = spellCommon(cat, bits, t1, t2);
        return true;
    }

    if (k1.cat == Category::kFloat && k2.cat == Category::kFloat) {
        out = spellCommon(Category::kFloat, std::max(k1.bits, k2.bits), t1, t2);
        return true;
    }

    // int-class and float never silently mix.
    return false;
}

// Structured: a type is "known" if its leaf is a built-in primitive or void.
// A reference/iterator/array is known iff its pointee/element is; a named slid
// type and the internal anyptr are not (matches the pre-migration predicate).
bool isKnownType(TypeRef ref) {
    Type const& t = get(ref);
    switch (t.form) {
        case Type::Form::kPrimitive: return true;
        case Type::Form::kVoid:      return true;
        case Type::Form::kAnyptr:    return false;
        case Type::Form::kSlid:      return false;
        case Type::Form::kPointer:   return isKnownType(t.pointee);
        case Type::Form::kIterator:  return isKnownType(t.pointee);
        case Type::Form::kArray:     return isKnownType(t.elem);
        case Type::Form::kAlias:     return isKnownType(t.underlying);  // see through
        case Type::Form::kTuple: {
            for (TypeRef slot : t.slots) if (!isKnownType(slot)) return false;
            return true;
        }
        case Type::Form::kNone:      return false;   // no type
    }
    return false;
}

bool isKnownType(std::string const& t) {
    return isKnownType(intern(t));
}

// Structured: a pointer / iterator / anyptr is 8; a fixed array is the product
// of its dims times the element size (-1 if the element is unsized); a primitive
// rounds its bit width up to whole bytes (bool -> 1); void / slid / tuple are not
// statically sized (-1).
long long typeByteSize(TypeRef ref) {
    Type const& t = get(ref);
    switch (t.form) {
        case Type::Form::kPointer:
        case Type::Form::kIterator:
        case Type::Form::kAnyptr:
            return 8;
        case Type::Form::kPrimitive:
            return (t.bits + 7) / 8;
        case Type::Form::kArray: {
            long long elem = typeByteSize(t.elem);
            if (elem < 0) return -1;
            long long total = elem;
            for (int d : t.dims) total *= d;
            return total;
        }
        case Type::Form::kAlias:
            return typeByteSize(t.underlying);   // see through
        case Type::Form::kVoid:
        case Type::Form::kSlid:
        case Type::Form::kTuple:
        case Type::Form::kNone:
            return -1;
    }
    return -1;
}

long long typeByteSize(std::string const& t) {
    return typeByteSize(intern(t));
}

// ---------------------------------------------------------------------------
// Structured-type arena (Stage 0). A process-lifetime interned table; matches
// the existing function-local-static pattern (nextTmpId). One TU per process,
// so type identity is process-stable.
// ---------------------------------------------------------------------------

namespace {

struct Arena {
    std::vector<Type> types;
    std::unordered_map<std::string, TypeRef> by_struct;     // STRUCTURAL dedup (primary)
    std::unordered_map<std::string, TypeRef> by_spelling;   // intern(spelling) parse memo
};

Arena& arena() {
    static Arena a;
    return a;
}

// A type's STRUCTURAL identity — form + child handles (which are themselves
// canonical). This is the primary dedup key: it distinguishes a kAlias-leaf
// composite from a kSlid-leaf one (same spelling, different structure), which a
// spelling key cannot. Child refs are already interned, so equal structure ->
// equal key -> one handle.
std::string structKey(Type const& t) {
    using F = Type::Form;
    switch (t.form) {
        case F::kNone:      return "N";
        case F::kVoid:      return "V";
        case F::kAnyptr:    return "A";
        case F::kPrimitive: return "P" + t.name;
        case F::kSlid:      return "S" + t.name;
        case F::kAlias:     return "L" + t.name + "=" + std::to_string(t.underlying);
        case F::kPointer:   return "p" + std::to_string(t.pointee);
        case F::kIterator:  return "i" + std::to_string(t.pointee);
        case F::kArray: {
            std::string k = "a" + std::to_string(t.elem);
            for (int d : t.dims) k += "," + std::to_string(d);
            return k;
        }
        case F::kTuple: {
            std::string k = "t";
            for (TypeRef s : t.slots) k += std::to_string(s) + ",";
            return k;
        }
    }
    return "";
}

// Intern a fully-built Type by its structure. The one mint point for the arena.
TypeRef internStruct(Type&& t) {
    Arena& a = arena();
    std::string key = structKey(t);
    auto it = a.by_struct.find(key);
    if (it != a.by_struct.end()) return it->second;
    TypeRef ref = static_cast<TypeRef>(a.types.size());
    a.types.push_back(std::move(t));
    a.by_struct.emplace(key, ref);
    return ref;
}

// If `s` ends with one or more contiguous `[digits]` groups, set `base` to the
// prefix before the run and `dims` to the dim values in source order; else false.
bool splitArraySuffix(std::string const& s, std::string& base, std::vector<int>& dims) {
    std::size_t end = s.size();
    std::vector<int> rev;
    while (end >= 3 && s[end - 1] == ']') {
        std::size_t lb = s.rfind('[', end - 1);
        if (lb == std::string::npos || lb + 1 >= end - 1) break;   // empty -> not `[N]`
        bool digits = true;
        for (std::size_t i = lb + 1; i < end - 1; i++) {
            if (s[i] < '0' || s[i] > '9') { digits = false; break; }
        }
        if (!digits) break;
        rev.push_back(std::atoi(s.substr(lb + 1, end - 1 - (lb + 1)).c_str()));
        end = lb;
    }
    if (rev.empty()) return false;
    base = s.substr(0, end);
    dims.assign(rev.rbegin(), rev.rend());
    return true;
}

// Split the interior of a tuple spelling on top-level (paren-depth-0) commas,
// interning each slot. spell() emits ", " between slots, so a leading space is
// trimmed. Nested tuples raise the depth so their inner commas don't split.
std::vector<TypeRef> internTupleSlots(std::string const& inner) {
    std::vector<TypeRef> slots;
    if (inner.empty()) return slots;
    auto push = [&](std::size_t b, std::size_t e) {
        while (b < e && inner[b] == ' ') b++;
        slots.push_back(intern(inner.substr(b, e - b)));
    };
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < inner.size(); i++) {
        char c = inner[i];
        if (c == '(') depth++;
        else if (c == ')') depth--;
        else if (c == ',' && depth == 0) { push(start, i); start = i + 1; }
    }
    push(start, inner.size());
    return slots;
}

}  // namespace

TypeRef intern(std::string const& s) {
    Arena& a = arena();
    auto it = a.by_spelling.find(s);
    if (it != a.by_spelling.end()) return it->second;

    // Decompose from the RIGHT (the outermost / last-applied suffix), mirroring
    // isKnownType's strip order: iterator `[]`, reference `^`, array `[N]...`.
    // Children intern (and structurally dedup) recursively; the built Type is then
    // structurally interned, so intern("<x>^") and internPointer(x) share a handle.
    Type t;
    std::string array_base;
    std::vector<int> array_dims;
    if (s.size() >= 2 && s.compare(s.size() - 2, 2, "[]") == 0) {
        t.form = Type::Form::kIterator;
        t.pointee = intern(s.substr(0, s.size() - 2));
    } else if (!s.empty() && s.back() == '^') {
        t.form = Type::Form::kPointer;
        t.pointee = intern(s.substr(0, s.size() - 1));
    } else if (splitArraySuffix(s, array_base, array_dims)) {
        t.form = Type::Form::kArray;
        t.elem = intern(array_base);
        t.dims = std::move(array_dims);
    } else if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        std::vector<TypeRef> slots = internTupleSlots(s.substr(1, s.size() - 2));
        if (slots.size() == 1) {                   // size-1 tuple == scalar
            a.by_spelling.emplace(s, slots[0]);
            return slots[0];
        }
        t.form = Type::Form::kTuple;
        t.slots = std::move(slots);
    } else if (s == "void") {
        t.form = Type::Form::kVoid;
    } else if (s == "anyptr") {
        t.form = Type::Form::kAnyptr;
    } else {
        TypeKind k;
        if (classify(s, k)) {
            t.form = Type::Form::kPrimitive;
            t.cat = k.cat;
            t.bits = k.bits;
            t.name = s;
        } else {
            t.form = Type::Form::kSlid;   // a named slid/class type
            t.name = s;
        }
    }

    TypeRef ref = internStruct(std::move(t));
    a.by_spelling.emplace(s, ref);
    return ref;
}

TypeRef internPointer(TypeRef pointee) {
    Type t;
    t.form = Type::Form::kPointer;
    t.pointee = pointee;
    return internStruct(std::move(t));
}

TypeRef internIterator(TypeRef pointee) {
    Type t;
    t.form = Type::Form::kIterator;
    t.pointee = pointee;
    return internStruct(std::move(t));
}

TypeRef internArray(TypeRef elem, std::vector<int> const& dims) {
    Type t;
    t.form = Type::Form::kArray;
    t.elem = elem;
    t.dims = dims;
    return internStruct(std::move(t));
}

TypeRef internAlias(std::string const& name, TypeRef underlying) {
    Type t;
    t.form = Type::Form::kAlias;
    t.name = name;
    t.underlying = underlying;
    return internStruct(std::move(t));
}

// A named class/slid type, carrying its field-slot types (the named-tuple half
// of "a class is a namespace + a named tuple"). A kSlid is interned by name
// alone (structKey == "S" + name), so there is exactly one handle per class
// name; resolve calls this once the field list is resolved to ATTACH the slot
// types to that unique handle. Every prior reference (grammar spellings, a
// `Class^` pointee) shares the handle, so they all gain the layout without a
// rewrite, and codegen (which has no symbol table) reads the layout off the
// type. Idempotent: re-attaching the same slots is a no-op.
TypeRef internSlid(std::string const& name, std::vector<TypeRef> const& slots) {
    Type t;
    t.form = Type::Form::kSlid;
    t.name = name;
    TypeRef ref = internStruct(std::move(t));
    arena().types[ref].slots = slots;
    return ref;
}

TypeRef strip(TypeRef ref) {
    while (get(ref).form == Type::Form::kAlias) ref = get(ref).underlying;
    return ref;
}

TypeRef deepStrip(TypeRef ref) {
    Type const& t = get(ref);
    switch (t.form) {
        case Type::Form::kAlias:    return deepStrip(t.underlying);
        case Type::Form::kPointer:  { TypeRef p = t.pointee; return internPointer(deepStrip(p)); }
        case Type::Form::kIterator: { TypeRef p = t.pointee; return internIterator(deepStrip(p)); }
        case Type::Form::kArray: {
            TypeRef e = t.elem;
            std::vector<int> d = t.dims;
            return internArray(deepStrip(e), d);
        }
        case Type::Form::kTuple: {
            std::vector<TypeRef> s = t.slots;
            for (TypeRef& x : s) x = deepStrip(x);
            return internTuple(s);
        }
        case Type::Form::kPrimitive:
        case Type::Form::kVoid:
        case Type::Form::kAnyptr:
        case Type::Form::kSlid:
        case Type::Form::kNone:
            return ref;
    }
    return ref;
}

std::string spell(TypeRef ref) {
    Type const& t = get(ref);
    switch (t.form) {
        case Type::Form::kNone:      return "";   // no type (kNoType) -> empty
        case Type::Form::kAlias:     return t.name;   // spells as the alias name
        case Type::Form::kPrimitive: return t.name;
        case Type::Form::kVoid:      return "void";
        case Type::Form::kAnyptr:    return "anyptr";
        case Type::Form::kSlid:      return t.name;
        case Type::Form::kPointer:   return spell(t.pointee) + "^";
        case Type::Form::kIterator:  return spell(t.pointee) + "[]";
        case Type::Form::kArray: {
            std::string s = spell(t.elem);
            for (int d : t.dims) s += "[" + std::to_string(d) + "]";
            return s;
        }
        case Type::Form::kTuple: {
            std::string s = "(";
            for (std::size_t i = 0; i < t.slots.size(); i++) {
                if (i) s += ", ";
                s += spell(t.slots[i]);
            }
            return s + ")";
        }
    }
    return "";
}

Type const& get(TypeRef ref) {
    // kNoType ("no type yet" / an already-reported error) reads as the kNone
    // sentinel: every structural predicate (isPtrLikeType / isArrayType / classify
    // ...) returns false/none on it, matching the pre-TypeRef behavior where an
    // empty spelling fell through every check. Its form is kNone, NOT kVoid — so a
    // `form(x) == kVoid` test never mistakes a no-type for void (it would instead
    // surface loudly, e.g. an llvmForRef assert). Callers needing the spelling use
    // spellOrEmpty(), which maps kNoType -> "".
    static const Type sentinel{};   // form defaults to kNone
    if (ref == kNoType) return sentinel;
    return arena().types[ref];
}

TypeRef internOrNone(std::string const& s) {
    return s.empty() ? kNoType : intern(s);
}

// Construct (and intern) a tuple type from its slot handles. Builds the
// canonical spelling and routes through intern() so it dedups with a parsed
// `(...)`; a 1-slot tuple collapses to that slot (size-1 tuple == scalar).
TypeRef internTuple(std::vector<TypeRef> const& slots) {
    if (slots.size() == 1) return slots[0];   // size-1 tuple == scalar
    Type t;
    t.form = Type::Form::kTuple;
    t.slots = slots;
    return internStruct(std::move(t));
}

std::string spellOrEmpty(TypeRef ref) {
    return ref == kNoType ? std::string() : spell(ref);
}

bool typeSelfTest(std::ostream& out) {
    char const* const cases[] = {
        "bool", "char",
        "int8", "int16", "int32", "int64", "int",
        "uint8", "uint16", "uint32", "uint64", "uint", "intptr",
        "float", "float32", "float64",
        "void", "anyptr",
        "int^", "int[]", "int[3]", "int[3][5]", "int^[3]", "int[3]^",
        "float64[]^", "char[]", "char[]^",
        "Point", "Point^", "Point[4]", "Point[2][3]^",
        "(int, bool)", "((int, int), bool)", "(int, bool)^", "(int, bool)[3]",
        "(char[], float64, Dir)",
    };
    bool ok = true;
    int n = 0;
    for (char const* c : cases) {
        std::string s = c;
        TypeRef r = intern(s);
        std::string back = spell(r);
        if (back != s) {
            ok = false;
            out << "round-trip FAIL: '" << s << "' -> '" << back << "'\n";
        }
        if (intern(s) != r) {
            ok = false;
            out << "intern not stable: '" << s << "'\n";
        }
        n++;
    }
    if (ok) out << "type self-test: " << n << " cases OK\n";
    return ok;
}

}  // namespace widen
