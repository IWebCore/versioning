#include "IVersionRange.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// Free helpers (Qt camelCase naming)
// ---------------------------------------------------------------------------

namespace {

std::string_view trimView(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::optional<uint32_t> parseU32(std::string_view& input) noexcept {
    if (input.empty() || !std::isdigit(static_cast<unsigned char>(input.front())))
        return std::nullopt;
    const char* first = input.data();
    uint32_t value = 0;
    auto [ptr, ec] = std::from_chars(first, first + input.size(), value);
    if (ec != std::errc{}) return std::nullopt;
    size_t consumed = static_cast<size_t>(ptr - first);
    if (consumed > 1 && first[0] == '0') return std::nullopt;
    input.remove_prefix(consumed);
    return value;
}

std::vector<std::string> splitUnion(std::string_view input) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < input.size(); ++i) {
        if (i + 1 < input.size() && input[i] == '|' && input[i + 1] == '|') {
            auto t = trimView(cur);
            if (!t.empty()) parts.emplace_back(t);
            cur.clear();
            ++i;
        } else {
            cur.push_back(input[i]);
        }
    }
    auto t = trimView(cur);
    if (!t.empty()) parts.emplace_back(t);
    return parts;
}

std::vector<std::string> tokenize(std::string_view input) {
    std::vector<std::string> tokens;
    std::string cur;
    for (size_t i = 0; i < input.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(input[i]))) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(input[i]);
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    // Merge hyphen: "a - b" into "a - b"
    std::vector<std::string> merged;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "-" && i > 0 && i + 1 < tokens.size()) {
            merged.back() += " - " + tokens[i + 1];
            ++i;
        } else {
            merged.push_back(tokens[i]);
        }
    }
    return merged;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Comparator::matches
// ---------------------------------------------------------------------------

bool IVersionRange::Comparator::matches(const IVersion& v) const noexcept {
    switch (op) {
    case Eq:
        if (v.major() != major || v.minor() != minor || v.patch() != patch)
            return false;
        if (prerelease.empty()) return v.prereleaseTag().empty();
        return v.prereleaseTag() == prerelease;

    case Gt: {
        IVersion bound(major, minor, patch);
        return v > bound;
    }

    case Gte: {
        IVersion bound(major, minor, patch);
        if (v > bound) return true;
        if (v.major() == major && v.minor() == minor && v.patch() == patch)
            return v.prereleaseTag().empty();
        return false;
    }

    case Lt: {
        IVersion bound(major, minor, patch);
        if (!(v < bound)) return false;
        if (v.prereleaseTag().empty()) return true;
        return !(v.major() == major && v.minor() == minor && v.patch() == patch);
    }

    case Lte: {
        IVersion bound(major, minor, patch);
        if (v.major() == major && v.minor() == minor && v.patch() == patch)
            return v.prereleaseTag().empty();
        if (v < bound) {
            if (v.prereleaseTag().empty()) return true;
            return !(v.major() == major && v.minor() == minor && v.patch() == patch);
        }
        return false;
    }
    }
    return false;
}

// ---------------------------------------------------------------------------
// RangeSet::matches
// ---------------------------------------------------------------------------

bool IVersionRange::RangeSet::matches(const IVersion& v) const noexcept {
    for (const auto& c : comparators)
        if (!c.matches(v)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// IVersionRange::contains
// ---------------------------------------------------------------------------

bool IVersionRange::contains(const IVersion& v) const noexcept {
    if (isUniversal_) return true;
    if (isNone_) return false;
    for (const auto& rs : range_sets_)
        if (rs.matches(v)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// parsePartialVersion
// ---------------------------------------------------------------------------

bool IVersionRange::parsePartialVersion(std::string_view input,
                                           std::optional<uint32_t>& major,
                                           std::optional<uint32_t>& minor,
                                           std::optional<uint32_t>& patch,
                                           std::string& prerelease) noexcept {
    major.reset(); minor.reset(); patch.reset(); prerelease.clear();
    if (input.empty()) return false;
    if (input.front() == '*' || input.front() == 'x' || input.front() == 'X')
        return false;

    auto m = parseU32(input);
    if (!m) return false;
    major = *m;
    if (input.empty()) return true;

    if (input.front() == '.') {
        input.remove_prefix(1);
        if (!input.empty() && (input.front() == 'x' || input.front() == 'X' || input.front() == '*'))
            return true; // 1.x
        auto min = parseU32(input);
        if (!min) return false;
        minor = *min;
        if (input.empty()) return true;

        if (input.front() == '.') {
            input.remove_prefix(1);
            if (!input.empty() && (input.front() == 'x' || input.front() == 'X' || input.front() == '*'))
                return true; // 1.2.x
            auto p = parseU32(input);
            if (!p) return false;
            patch = *p;
            if (!input.empty() && input.front() == '-') {
                input.remove_prefix(1);
                while (!input.empty() && input.front() != '+') {
                    char c = input.front();
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.') {
                        prerelease.push_back(c);
                        input.remove_prefix(1);
                    } else break;
                }
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// expandTilde
// ---------------------------------------------------------------------------

bool IVersionRange::expandTilde(std::string_view token, RangeSet& rs) {
    token.remove_prefix(1);
    if (token.empty()) return false;
    std::optional<uint32_t> ma, mi, pa;
    std::string pre;
    if (!parsePartialVersion(token, ma, mi, pa, pre)) return false;

    uint32_t mj = ma.value_or(0), mn = mi.value_or(0), pt = pa.value_or(0);

    Comparator lower;
    lower.op = Comparator::Gte; lower.major = mj; lower.minor = mn; lower.patch = pt;
    rs.comparators.push_back(lower);

    Comparator upper;
    upper.op = Comparator::Lt;
    if (pa.has_value() || mi.has_value()) {
        upper.major = mj; upper.minor = mn + 1; upper.patch = 0;
    } else {
        upper.major = mj + 1; upper.minor = 0; upper.patch = 0;
    }
    rs.comparators.push_back(upper);
    return true;
}

// ---------------------------------------------------------------------------
// expandCaret
// ---------------------------------------------------------------------------

bool IVersionRange::expandCaret(std::string_view token, RangeSet& rs) {
    token.remove_prefix(1);
    if (token.empty()) return false;
    std::optional<uint32_t> ma, mi, pa;
    std::string pre;
    if (!parsePartialVersion(token, ma, mi, pa, pre)) return false;

    uint32_t mj = ma.value_or(0), mn = mi.value_or(0), pt = pa.value_or(0);

    Comparator lower;
    lower.op = Comparator::Gte; lower.major = mj; lower.minor = mn; lower.patch = pt;
    rs.comparators.push_back(lower);

    Comparator upper;
    upper.op = Comparator::Lt;

    if (mj != 0) {
        // ^1.2.3
        upper.major = mj + 1; upper.minor = 0; upper.patch = 0;
    } else if (mi.has_value() && mn != 0) {
        // ^0.2.3
        upper.major = 0; upper.minor = mn + 1; upper.patch = 0;
    } else if (mi.has_value() && pa.has_value()) {
        // ^0.0.3
        upper.major = 0; upper.minor = 0; upper.patch = pt + 1;
    } else if (mi.has_value()) {
        // ^0.0 (patch not given)
        upper.major = 0; upper.minor = mn + 1; upper.patch = 0;
    } else {
        // ^0 (only major given)
        upper.major = mj + 1; upper.minor = 0; upper.patch = 0;
    }
    rs.comparators.push_back(upper);
    return true;
}

// ---------------------------------------------------------------------------
// expandHyphen
// ---------------------------------------------------------------------------

bool IVersionRange::expandHyphen(std::string_view lhs, std::string_view rhs, RangeSet& rs) {
    lhs = trimView(lhs); rhs = trimView(rhs);
    if (lhs.empty() || rhs.empty()) return false;

    std::optional<uint32_t> lma, lmi, lpa;
    std::string lpre;
    if (!parsePartialVersion(lhs, lma, lmi, lpa, lpre)) return false;

    std::optional<uint32_t> hma, hmi, hpa;
    std::string hpre;
    if (!parsePartialVersion(rhs, hma, hmi, hpa, hpre)) return false;

    Comparator lo;
    lo.op = Comparator::Gte; lo.major = lma.value_or(0); lo.minor = lmi.value_or(0); lo.patch = lpa.value_or(0);
    rs.comparators.push_back(lo);

    Comparator hi;
    if (hpa.has_value()) {
        hi.op = Comparator::Lte;
        hi.major = hma.value_or(0); hi.minor = hmi.value_or(0); hi.patch = hpa.value_or(0);
    } else {
        hi.op = Comparator::Lt;
        hi.major = hma.value_or(0) + 1; hi.minor = 0; hi.patch = 0;
    }
    rs.comparators.push_back(hi);
    return true;
}

// ---------------------------------------------------------------------------
// expandComponentWildcard
// ---------------------------------------------------------------------------

bool IVersionRange::expandComponentWildcard(std::string_view token, RangeSet& rs) {
    std::optional<uint32_t> ma, mi, pa;
    std::string pre;
    if (!parsePartialVersion(token, ma, mi, pa, pre)) return false;
    if (!ma.has_value()) return false;

    Comparator lo;
    lo.op = Comparator::Gte; lo.major = ma.value(); lo.minor = mi.value_or(0); lo.patch = pa.value_or(0);
    rs.comparators.push_back(lo);
    Comparator hi;
    hi.op = Comparator::Lt;
    if (mi.has_value()) {
        // 1.2.x or 1.2.* → <1.3.0-0
        hi.major = ma.value(); hi.minor = mi.value() + 1; hi.patch = 0;
    } else {
        // 1.x or 1.* → <2.0.0-0
        hi.major = ma.value() + 1; hi.minor = 0; hi.patch = 0;
    }

    rs.comparators.push_back(hi);
    return true;
}

// ---------------------------------------------------------------------------
// parse_version_strict
// ---------------------------------------------------------------------------

static bool parseStrictVersion(std::string_view input,
                                    uint32_t& major, uint32_t& minor, uint32_t& patch,
                                    std::string& prerelease) noexcept {
    auto m = parseU32(input);
    if (!m) return false;
    if (input.empty() || input.front() != '.') return false;
    input.remove_prefix(1);
    auto min = parseU32(input);
    if (!min) return false;
    if (input.empty() || input.front() != '.') return false;
    input.remove_prefix(1);
    auto p = parseU32(input);
    if (!p) return false;
    major = *m; minor = *min; patch = *p;
    if (!input.empty() && input.front() == '-') {
        input.remove_prefix(1);
        while (!input.empty() && input.front() != '+') {
            char c = input.front();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.') {
                prerelease.push_back(c);
                input.remove_prefix(1);
            } else break;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// RangeSet satisfiability check (for simplification)
// ---------------------------------------------------------------------------

// Check whether a RangeSet can possibly match any version, given its bounds.
// A RangeSet with contradictory bounds (e.g. >=3.0.0 and <2.0.0) is removed
// so the resulting range is properly flagged as empty when appropriate.
//
// NOTE: This is a semantic simplification — it checks structural bound conflicts
// but does NOT detect all impossible ranges:
//   - Prerelease EQ conflicts: =1.0.0-alpha and =1.0.0-beta have the same
//     major.minor.patch but are mutually exclusive (cv discards prerelease).
//   - Discrete version gaps: >1.0.0 <1.0.1 has no release version between them.
// These cases are harmless because contains() correctly rejects all versions;
// only empty()/isNone() reporting may be overly optimistic.
bool IVersionRange::rangeSetIsSatisfiable(const RangeSet& rs) noexcept {
    using Cmp = Comparator;

    IVersion loVer(0, 0, 0);
    Cmp::Op loOp = Cmp::Gte;  // default: >= 0.0.0
    bool hasLo = false;

    IVersion hiVer(0, 0, 0);
    Cmp::Op hiOp = Cmp::Lt;
    bool hasHi = false;

    // First pass: check for conflicting EQ comparators
    // NOTE: cv is constructed without prerelease, so =1.0.0-alpha and =1.0.0
    // are NOT detected as conflicting. See the NOTE above.
    IVersion eqVer(0, 0, 0);
    bool hasEq = false;
    for (const auto& c : rs.comparators) {
        if (c.op == Cmp::Eq) {
            IVersion cv(c.major, c.minor, c.patch);
            if (hasEq && cv != eqVer) return false;
            if (!hasEq) { eqVer = cv; hasEq = true; }
        }
    }

    // Second pass: collect lower and upper bounds
    for (const auto& c : rs.comparators) {
        IVersion cv(c.major, c.minor, c.patch);

        switch (c.op) {
        case Cmp::Gt:
        case Cmp::Gte:
            if (!hasLo || cv > loVer || (cv == loVer && c.op == Cmp::Gt && loOp == Cmp::Gte)) {
                loVer = cv;
                loOp = c.op;
                hasLo = true;
            }
            break;
        case Cmp::Lt:
        case Cmp::Lte:
            if (!hasHi || cv < hiVer || (cv == hiVer && c.op == Cmp::Lt && hiOp == Cmp::Lte)) {
                hiVer = cv;
                hiOp = c.op;
                hasHi = true;
            }
            break;
        case Cmp::Eq:
            // Exact match acts as both lower and upper bound
            if (!hasLo || cv > loVer) { loVer = cv; loOp = Cmp::Gte; hasLo = true; }
            if (!hasHi || cv < hiVer) { hiVer = cv; hiOp = Cmp::Lte; hasHi = true; }
            break;
        }
    }

    if (!hasLo || !hasHi) return true;      // open-ended → satisfiable

    if (loVer > hiVer) return false;        // e.g. >=3.0.0 && <2.0.0
    if (loVer == hiVer) {
        // Must be inclusive on both sides: >=x && <=x
        return loOp == Cmp::Gte && hiOp == Cmp::Lte;
    }

    return true;  // loVer < hiVer → room exists
}

// ---------------------------------------------------------------------------
// IVersionRange::united
// ---------------------------------------------------------------------------

IVersionRange IVersionRange::united(const IVersionRange& other) const {
    // Universal + anything = universal
    if (isUniversal_ || other.isUniversal_) {
        IVersionRange result;
        result.isUniversal_ = true;
        return result;
    }

    // None + X = X
    if (isNone_) {
        // Normalize: if other is default-constructed (empty, no flags),
        // ensure the result is properly marked as none.
        if (other.range_sets_.empty() && !other.isUniversal_ && !other.isNone_) {
            IVersionRange result;
            result.isNone_ = true;
            return result;
        }
        return other;
    }
    if (other.isNone_) {
        if (range_sets_.empty() && !isUniversal_ && !isNone_) {
            IVersionRange result;
            result.isNone_ = true;
            return result;
        }
        return *this;
    }

    // Otherwise, concatenate the range sets (OR together)
    IVersionRange result;
    result.range_sets_ = range_sets_;
    result.range_sets_.insert(result.range_sets_.end(),
                              other.range_sets_.begin(),
                              other.range_sets_.end());

    // Normalize: empty range_sets_ with no special flags → none.
    // A default-constructed range acts as none but isNone() returns false;
    // this ensures consistent isNone() reporting after union.
    if (result.range_sets_.empty() && !result.isUniversal_) {
        result.isNone_ = true;
    }
    return result;
}

// ---------------------------------------------------------------------------
// IVersionRange::intersected
// ---------------------------------------------------------------------------

IVersionRange IVersionRange::intersected(const IVersionRange& other) const {
    // None ∩ anything = none
    if (isNone_ || other.isNone_) {
        IVersionRange result;
        result.isNone_ = true;
        return result;
    }

    // Universal ∩ X = X
    if (isUniversal_) {
        return other;
    }
    if (other.isUniversal_) {
        return *this;
    }

    // Distribute: (A1 ∨ A2 ∨ ...) ∧ (B1 ∨ B2 ∨ ...)
    //          = ∨_{i,j} (Ai ∧ Bj)
    // Each Ai ∧ Bj is a RangeSet combining comparators from both.
    IVersionRange result;
    for (const auto& rs1 : range_sets_) {
        for (const auto& rs2 : other.range_sets_) {
            RangeSet combined;
            combined.comparators = rs1.comparators;
            combined.comparators.insert(combined.comparators.end(),
                                        rs2.comparators.begin(),
                                        rs2.comparators.end());
            // Skip unsatisfiable combinations (e.g. disjoint ranges)
            if (rangeSetIsSatisfiable(combined)) {
                result.range_sets_.push_back(std::move(combined));
            }
        }
    }

    // If no valid combinations resulted, the intersection is empty.
    if (result.range_sets_.empty()) {
        result.isNone_ = true;
    }

    return result;
}

// ---------------------------------------------------------------------------
// IVersionRange::parse
// ---------------------------------------------------------------------------

bool IVersionRange::parse(std::string_view input, IVersionRange& out) noexcept {
    out = IVersionRange();
    input = trimView(input);
    if (input.empty()) return false;

    if (input == "*") { out.isUniversal_ = true; return true; }
    if (input == "x" || input == "X") { out.isNone_ = true; return true; }

    auto parts = splitUnion(input);
    if (parts.empty()) return false;

    for (const auto& part : parts) {
        RangeSet rs;
        auto toks = tokenize(part);
        for (const auto& tok : toks) {
            if (tok.empty()) continue;
            if (tok == "*") continue; // universal within set
            if (tok[0] == '~') { if (!expandTilde(tok, rs)) return false; }
            else if (tok[0] == '^') { if (!expandCaret(tok, rs)) return false; }
            else if (tok.find(" - ") != std::string::npos) {
                auto pos = tok.find(" - ");
                if (!expandHyphen(std::string_view(tok.data(), pos),
                                   std::string_view(tok.data() + pos + 3), rs))
                    return false;
            } else if (tok.find('x') != std::string::npos ||
                       tok.find('X') != std::string::npos ||
                       tok.find('*') != std::string::npos) {
                if (!expandComponentWildcard(tok, rs)) return false;
            } else {
                Comparator comp{};
                comp.op = Comparator::Eq;
                std::string_view tv = tok;

                if (tv.size() >= 2 && tv[0] == '>') {
                    comp.op = (tv[1] == '=') ? Comparator::Gte : Comparator::Gt;
                    tv.remove_prefix(tv[1] == '=' ? 2 : 1);
                } else if (tv.size() >= 2 && tv[0] == '<') {
                    comp.op = (tv[1] == '=') ? Comparator::Lte : Comparator::Lt;
                    tv.remove_prefix(tv[1] == '=' ? 2 : 1);
                } else if (tv.size() >= 1 && tv[0] == '=') {
                    comp.op = Comparator::Eq;
                    tv.remove_prefix(1);
                }

                if (!parseStrictVersion(tv, comp.major, comp.minor, comp.patch, comp.prerelease))
                    return false;

                rs.comparators.push_back(comp);
            }
        }
        out.range_sets_.push_back(rs);
    }
    return true;
}
