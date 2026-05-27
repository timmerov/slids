#include "widen.h"

#include <algorithm>
#include <cerrno>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <ostream>

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

bool intExactlyRepresentableInFloat(uint64_t mag, int float_bits) {
    int sig = (float_bits == 32) ? 24 : 53;
    if (sig >= 64) return true;
    uint64_t max_exact = ((uint64_t)1) << sig;
    return mag <= max_exact;
}

bool intTypeFitsInFloat(TypeKind const& ik, int float_bits) {
    int sig = (float_bits == 32) ? 24 : 53;
    if (ik.cat == Category::kBool) return true;
    if (ik.cat == Category::kSignedInt) return ik.bits <= sig + 1;
    if (ik.cat == Category::kUnsignedInt) return ik.bits <= sig;
    return false;
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
    bool negative = false;
    uint64_t mag = 0;
    if (!parseSignedDigits(literal_text, negative, mag)) {
        reportIntFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kFloat) {
        if (!intExactlyRepresentableInFloat(mag, tk.bits)) {
            reportIntFit(diag, literal_text, dest_type, file_id, tok);
            return false;
        }
        return true;
    }
    if (tk.cat == Category::kBool) {
        if (!negative && (mag == 0 || mag == 1)) return true;
        reportIntFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kSignedInt) {
        if (!intFitsSigned(mag, negative, tk.bits)) {
            reportIntFit(diag, literal_text, dest_type, file_id, tok);
            return false;
        }
        return true;
    }
    if (tk.cat == Category::kUnsignedInt) {
        if (!intFitsUnsigned(mag, negative, tk.bits)) {
            reportIntFit(diag, literal_text, dest_type, file_id, tok);
            return false;
        }
        return true;
    }
    return true;
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
    errno = 0;
    double v = std::strtod(literal_text.c_str(), nullptr);
    if (errno == ERANGE) {
        reportFloatFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kFloat) {
        double m = (tk.bits == 32) ? (double)FLT_MAX : DBL_MAX;
        if (v > m || v < -m) {
            reportFloatFit(diag, literal_text, dest_type, file_id, tok);
            return false;
        }
        return true;
    }
    if (v != std::floor(v)) {
        reportFloatFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    bool negative = (v < 0);
    double absv = negative ? -v : v;
    if (absv > (double)UINT64_MAX) {
        reportFloatFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    uint64_t mag = (uint64_t)absv;
    if (tk.cat == Category::kBool) {
        if (!negative && (mag == 0 || mag == 1)) return true;
        reportFloatFit(diag, literal_text, dest_type, file_id, tok);
        return false;
    }
    if (tk.cat == Category::kSignedInt) {
        if (!intFitsSigned(mag, negative, tk.bits)) {
            reportFloatFit(diag, literal_text, dest_type, file_id, tok);
            return false;
        }
        return true;
    }
    if (tk.cat == Category::kUnsignedInt) {
        if (!intFitsUnsigned(mag, negative, tk.bits)) {
            reportFloatFit(diag, literal_text, dest_type, file_id, tok);
            return false;
        }
        return true;
    }
    return true;
}

bool intLiteralFits(std::string const& literal_text, std::string const& dest_type) {
    if (dest_type.empty()) return true;
    TypeKind tk;
    if (!classify(dest_type, tk)) return false;
    bool negative = false;
    uint64_t mag = 0;
    if (!parseSignedDigits(literal_text, negative, mag)) return false;
    if (tk.cat == Category::kFloat) {
        return intExactlyRepresentableInFloat(mag, tk.bits);
    }
    if (tk.cat == Category::kBool) {
        return !negative && (mag == 0 || mag == 1);
    }
    if (tk.cat == Category::kSignedInt) return intFitsSigned(mag, negative, tk.bits);
    if (tk.cat == Category::kUnsignedInt) return intFitsUnsigned(mag, negative, tk.bits);
    return false;
}

bool floatLiteralFits(std::string const& literal_text, std::string const& dest_type) {
    if (dest_type.empty()) return true;
    TypeKind tk;
    if (!classify(dest_type, tk)) return false;
    errno = 0;
    double v = std::strtod(literal_text.c_str(), nullptr);
    if (errno == ERANGE) return false;
    if (tk.cat == Category::kFloat) {
        double m = (tk.bits == 32) ? (double)FLT_MAX : DBL_MAX;
        return v <= m && v >= -m;
    }
    if (v != std::floor(v)) return false;
    bool negative = (v < 0);
    double absv = negative ? -v : v;
    if (absv > (double)UINT64_MAX) return false;
    uint64_t mag = (uint64_t)absv;
    if (tk.cat == Category::kBool) return !negative && (mag == 0 || mag == 1);
    if (tk.cat == Category::kSignedInt) return intFitsSigned(mag, negative, tk.bits);
    if (tk.cat == Category::kUnsignedInt) return intFitsUnsigned(mag, negative, tk.bits);
    return false;
}

std::string convert(std::string const& src_val,
                    std::string const& src_type,
                    std::string const& dest_type,
                    int file_id, int tok,
                    std::ostream& out,
                    diagnostic::Sink& diag) {
    if (src_type == dest_type) return src_val;

    TypeKind src_tk, dest_tk;
    if (!classify(src_type, src_tk) || !classify(dest_type, dest_tk)) {
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
    auto emitSitofp = [&](std::string const& src_ll, std::string const& dest_ll) {
        std::string tmp = newWidenTmp();
        out << "  " << tmp << " = sitofp " << src_ll << " " << src_val
            << " to " << dest_ll << "\n";
        return tmp;
    };
    auto emitUitofp = [&](std::string const& src_ll, std::string const& dest_ll) {
        std::string tmp = newWidenTmp();
        out << "  " << tmp << " = uitofp " << src_ll << " " << src_val
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
        if (dest_tk.cat == Category::kFloat) {
            return emitUitofp("i1", llvmFloatType(dest_tk.bits));
        }
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

    if ((src_tk.cat == Category::kSignedInt || src_tk.cat == Category::kUnsignedInt)
        && dest_tk.cat == Category::kFloat) {
        if (intTypeFitsInFloat(src_tk, dest_tk.bits)) {
            if (src_tk.cat == Category::kSignedInt) {
                return emitSitofp(llvmIntType(src_tk.bits), llvmFloatType(dest_tk.bits));
            }
            return emitUitofp(llvmIntType(src_tk.bits), llvmFloatType(dest_tk.bits));
        }
        return convertErrPlain();
    }

    return convertErrPlain();
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
        bool s1 = (k1.cat == Category::kSignedInt);
        bool s2 = (k2.cat == Category::kSignedInt);
        if (s1 && s2) {
            out = canonicalSigned(std::max(k1.bits, k2.bits));
            return true;
        }
        if (!s1 && !s2) {
            out = canonicalUnsigned(std::max(k1.bits, k2.bits));
            return true;
        }
        int signed_bits   = s1 ? k1.bits : k2.bits;
        int unsigned_bits = s1 ? k2.bits : k1.bits;
        int needed = std::max(signed_bits, unsigned_bits + 1);
        if (needed > 64) return false;
        out = canonicalSigned(needed);
        return true;
    }

    if (k1.cat == Category::kFloat && k2.cat == Category::kFloat) {
        out = canonicalFloat(std::max(k1.bits, k2.bits));
        return true;
    }

    TypeKind const& ik = (k1.cat == Category::kFloat) ? k2 : k1;
    TypeKind const& fk = (k1.cat == Category::kFloat) ? k1 : k2;
    int const candidates[] = {32, 64};
    for (int fb : candidates) {
        if (fb < fk.bits) continue;
        if (intTypeFitsInFloat(ik, fb)) {
            out = canonicalFloat(fb);
            return true;
        }
    }
    return false;
}

}  // namespace widen
