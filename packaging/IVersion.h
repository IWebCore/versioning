#ifndef IVERSION_H
#define IVERSION_H

#include <cstdint>
#include <string>
#include <string_view>

class IVersion {
public:
    //! Default constructs to 0.1.0 per semver FAQ §4
    IVersion() noexcept;

    //! Construct with major, minor, patch
    IVersion(uint32_t major, uint32_t minor, uint32_t patch) noexcept;

    //! Construct with full semver components
    IVersion(uint32_t major, uint32_t minor, uint32_t patch,
             std::string prerelease, std::string buildMetadata) noexcept;

    //!@name Accessors
    //!@{
    uint32_t major() const noexcept { return major_; }
    uint32_t minor() const noexcept { return minor_; }
    uint32_t patch() const noexcept { return patch_; }
    const std::string& prereleaseTag() const noexcept { return prereleaseTag_; }
    const std::string& buildMetadata() const noexcept { return buildMetadata_; }
    //!@}

    //!@name Comparison operators (build metadata ignored per semver §10)
    //!@{
    bool operator==(const IVersion& other) const noexcept;
    bool operator!=(const IVersion& other) const noexcept;
    bool operator<(const IVersion& other) const noexcept;
    bool operator<=(const IVersion& other) const noexcept;
    bool operator>(const IVersion& other) const noexcept;
    bool operator>=(const IVersion& other) const noexcept;
    //!@}

    //! Parse from string per semver 2.0.0.
    //! Returns true on success, false on invalid input.
    //! Valid: "1.2.3", "1.2.3-alpha", "1.2.3+build", "1.2.3-alpha.1+build.123"
    //! Invalid: empty string, leading zeros ("01.02.03"), trailing garbage.
    static bool parse(std::string_view input, IVersion& out) noexcept;

    //! Serialize to string: "M.m.p" or "M.m.p-pre" or "M.m.p+build" or "M.m.p-pre+build"
    std::string toString() const;

private:
    uint32_t major_;
    uint32_t minor_;
    uint32_t patch_;
    std::string prereleaseTag_;
    std::string buildMetadata_;
};

#endif // IVERSION_H
