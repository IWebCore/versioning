#ifndef IVERSIONRANGE_H
#define IVERSIONRANGE_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IVersion.h"

//! \brief Parse and evaluate npm-style semver range expressions.
//!
//! Supported syntax:
//!   - Basic comparators: =, >, >=, <, <=, bare version (implicit =)
//!   - Tilde (~): ~1.2.3 → >=1.2.3 <1.3.0-0
//!   - Caret (^): ^1.2.3 → >=1.2.3 <2.0.0-0
//!   - Wildcards: * (matches all), x (matches none)
//!   - Component wildcards: 1.x, 1.2.*, 1.2.x
//!   - Hyphen ranges: 1.2.3 - 2.3.4
//!   - Intersection (space): ">=1.0.0 <2.0.0"
//!   - Union (||): "1.0.0 || 2.0.0"
//!
//! Prerelease versions are excluded from matching by default per semver spec.

class IVersionRange {
public:
    IVersionRange() = default;

    //! Parse a range expression string.
    //! Returns true on success, false on invalid syntax.
    static bool parse(std::string_view input, IVersionRange& out) noexcept;

    //! Check if the given version is contained within this range.
    //! Prerelease versions are excluded by default.
    bool contains(const IVersion& version) const noexcept;

    //! Check if the range is empty (matches no versions).
    bool empty() const noexcept {
        if (isUniversal_) return false;
        if (isNone_) return true;
        return range_sets_.empty();
    }

    //! Check if the range is universal (matches all versions).
    bool isUniversal() const noexcept { return isUniversal_; }
    //! Check if the range matches no versions (the null set).
    bool isNone() const noexcept { return isNone_; }

    //! \brief Return the union of this range with \a other.
    //!        The resulting range matches any version that matches
    //!        either this range or \a other.
    IVersionRange united(const IVersionRange& other) const;

    //! \brief Return the intersection of this range with \a other.
    //!        The resulting range matches any version that matches
    //!        both this range and \a other.
    IVersionRange intersected(const IVersionRange& other) const;

private:
    // A single comparator after desugaring.
    // The .prerelease field is only meaningful for EQ comparators.
    struct Comparator {
        enum Op { Eq, Gt, Gte, Lt, Lte };
        Op op;
        uint32_t major;
        uint32_t minor;
        uint32_t patch;
        std::string prerelease; // for exact prerelease match (EQ only)

        bool matches(const IVersion& v) const noexcept;
    };

    // A set of comparators AND-ed together (intersection).
    struct RangeSet {
        std::vector<Comparator> comparators;

        bool matches(const IVersion& v) const noexcept;
        bool empty() const noexcept { return comparators.empty(); }
    };

    // All range sets OR-ed together (union).
    std::vector<RangeSet> range_sets_;
    bool isUniversal_ = false;
    bool isNone_ = false;

    // Helpers (Qt camelCase naming)
    static bool expandTilde(std::string_view token, RangeSet& rs);
    static bool expandCaret(std::string_view token, RangeSet& rs);
    static bool expandHyphen(std::string_view lhs, std::string_view rhs, RangeSet& rs);
    static bool expandComponentWildcard(std::string_view token, RangeSet& rs);
    static bool parsePartialVersion(std::string_view input,
                                     std::optional<uint32_t>& major,
                                     std::optional<uint32_t>& minor,
                                     std::optional<uint32_t>& patch,
                                     std::string& prerelease) noexcept;
    // Check if a RangeSet can possibly match any version (for simplification)
    static bool rangeSetIsSatisfiable(const RangeSet& rs) noexcept;

};

#endif // IVERSIONRANGE_H
