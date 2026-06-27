#include "IVersion.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <sstream>
#include <system_error>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

IVersion::IVersion() noexcept
    : major_(0), minor_(1), patch_(0) {}

IVersion::IVersion(uint32_t major, uint32_t minor, uint32_t patch) noexcept
    : major_(major), minor_(minor), patch_(patch) {}

IVersion::IVersion(uint32_t major, uint32_t minor, uint32_t patch,
                   std::string prerelease, std::string buildMetadata) noexcept
    : major_(major), minor_(minor), patch_(patch),
      prereleaseTag_(std::move(prerelease)),
      buildMetadata_(std::move(buildMetadata)) {}

// ---------------------------------------------------------------------------
// Comparison (semver §11 precedence rules)
// ---------------------------------------------------------------------------

// Compare prerelease identifiers: returns <0 if lhs < rhs, 0 if equal, >0 if lhs > rhs.
// Numeric identifiers have lower precedence than alphanumeric ones.
// Each identifier is compared: numeric comparison for numeric, lexicographic for alphanumeric.
static int comparePrereleaseIdentifiers(const std::string& lhs, const std::string& rhs) noexcept {
    if (lhs == rhs) return 0;

    std::istringstream lhs_stream(lhs);
    std::istringstream rhs_stream(rhs);
    std::string lhs_id, rhs_id;

    while (std::getline(lhs_stream, lhs_id, '.')) {
        if (!std::getline(rhs_stream, rhs_id, '.')) {
            // lhs has more identifiers → greater precedence
            return 1;
        }

        // Check if both are purely numeric
        auto is_numeric = [](const std::string& s) {
            return !s.empty() && std::all_of(s.begin(), s.end(),
                [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        };

        bool lhs_num = is_numeric(lhs_id);
        bool rhs_num = is_numeric(rhs_id);

        if (lhs_num && rhs_num) {
            // Numeric comparison
            auto to_uint64 = [](const std::string& s) -> uint64_t {
                uint64_t val = 0;
                std::from_chars(s.data(), s.data() + s.size(), val);
                return val;
            };
            uint64_t lv = to_uint64(lhs_id);
            uint64_t rv = to_uint64(rhs_id);
            if (lv != rv) return lv < rv ? -1 : 1;
        } else if (lhs_num != rhs_num) {
            // Numeric < alphanumeric (spec §11)
            return lhs_num ? -1 : 1;
        } else {
            // Lexicographic comparison
            int cmp = lhs_id.compare(rhs_id);
            if (cmp != 0) return cmp;
        }
    }

    // rhs has more identifiers → lhs has lower precedence
    if (std::getline(rhs_stream, rhs_id, '.')) return -1;
    return 0;
}

bool IVersion::operator==(const IVersion& other) const noexcept {
    return major_ == other.major_ &&
           minor_ == other.minor_ &&
           patch_ == other.patch_ &&
           prereleaseTag_ == other.prereleaseTag_;
    // build metadata explicitly ignored per semver §10
}

bool IVersion::operator!=(const IVersion& other) const noexcept {
    return !(*this == other);
}

bool IVersion::operator<(const IVersion& other) const noexcept {
    if (major_ != other.major_) return major_ < other.major_;
    if (minor_ != other.minor_) return minor_ < other.minor_;
    if (patch_ != other.patch_) return patch_ < other.patch_;

    // No prerelease > prerelease (release has higher precedence)
    if (prereleaseTag_.empty() && !other.prereleaseTag_.empty()) return false;
    if (!prereleaseTag_.empty() && other.prereleaseTag_.empty()) return true;

    // Both have prerelease: compare identifiers
    return comparePrereleaseIdentifiers(prereleaseTag_, other.prereleaseTag_) < 0;
}

bool IVersion::operator<=(const IVersion& other) const noexcept {
    return *this < other || *this == other;
}

bool IVersion::operator>(const IVersion& other) const noexcept {
    return other < *this;
}

bool IVersion::operator>=(const IVersion& other) const noexcept {
    return other <= *this;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

// Parse a decimal number from a string view, advancing the position.
// Returns nullopt on failure (overflow, no digits).
static std::optional<uint32_t> parseNumber(std::string_view& input) noexcept {
    if (input.empty() || !std::isdigit(static_cast<unsigned char>(input.front()))) {
        return std::nullopt;
    }

    const char* first = input.data();
    const char* last = input.data() + input.size();
    uint32_t value = 0;
    auto [ptr, ec] = std::from_chars(first, last, value);

    if (ec != std::errc{}) return std::nullopt;

    // Consume the digits from input
    size_t consumed = static_cast<size_t>(ptr - first);
    input.remove_prefix(consumed);

    if (consumed > 1 && first[0] == '0') {
        // Leading zeros are invalid per semver §2
        return std::nullopt;
    }

    return value;
}

// Consume a specific character from input, returns true if matched
static bool consumeChar(std::string_view& input, char c) noexcept {
    if (!input.empty() && input.front() == c) {
        input.remove_prefix(1);
        return true;
    }
    return false;
}

// Parse a dot-separated tag (prerelease or build metadata)
// check_leading_zeros: true for prerelease (spec §9), false for build metadata
static std::optional<std::string> parseTag(std::string_view& input, bool check_leading_zeros) noexcept {
    std::string result;

    // First identifier
    while (!input.empty()) {
        char c = input.front();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
            if (check_leading_zeros && result.empty() && c == '0') {
                // Check if next char is a digit → leading zero in numeric identifier
                if (input.size() > 1 && std::isdigit(static_cast<unsigned char>(input[1]))) {
                    return std::nullopt;
                }
            }
            result.push_back(c);
            input.remove_prefix(1);
        } else {
            break;
        }
    }

    if (result.empty()) return std::nullopt;

    // Subsequent identifiers separated by dots
    while (consumeChar(input, '.')) {
        result.push_back('.');
        bool has_any = false;
        while (!input.empty()) {
            char c = input.front();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
                if (check_leading_zeros && !has_any && c == '0') {
                    if (input.size() > 1 && std::isdigit(static_cast<unsigned char>(input[1]))) {
                        return std::nullopt;
                    }
                }
                result.push_back(c);
                input.remove_prefix(1);
                has_any = true;
            } else {
                break;
            }
        }
        if (!has_any) return std::nullopt;
    }

    return result;
}

bool IVersion::parse(std::string_view input, IVersion& out) noexcept {
    if (input.empty()) return false;

    // Trim leading/trailing whitespace
    // (implicitly handled by parsing logic — whitespace will fail on number parsing)

    // Major
    auto maj = parseNumber(input);
    if (!maj) return false;

    if (!consumeChar(input, '.')) return false;

    // Minor
    auto min = parseNumber(input);
    if (!min) return false;

    if (!consumeChar(input, '.')) return false;

    // Patch
    auto pat = parseNumber(input);
    if (!pat) return false;

    // Optional prerelease
    std::string prerelease;
    if (consumeChar(input, '-')) {
        auto tag = parseTag(input, true);
        if (!tag) return false;
        prerelease = std::move(*tag);
    }

    // Optional build metadata
    std::string build;
    if (consumeChar(input, '+')) {
        if (input.empty()) return false; // '+' must be followed by something
        auto tag = parseTag(input, false);
        if (!tag) return false;
        build = std::move(*tag);
    }

    // No trailing garbage allowed
    if (!input.empty()) return false;

    out = IVersion(*maj, *min, *pat, std::move(prerelease), std::move(build));
    return true;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

std::string IVersion::toString() const {
    std::string result = std::to_string(major_);
    result += '.';
    result += std::to_string(minor_);
    result += '.';
    result += std::to_string(patch_);

    if (!prereleaseTag_.empty()) {
        result += '-';
        result += prereleaseTag_;
    }

    if (!buildMetadata_.empty()) {
        result += '+';
        result += buildMetadata_;
    }

    return result;
}
