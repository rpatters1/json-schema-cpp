// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <jsonschema/export.hpp>

#if defined(_MSC_VER)
#pragma warning(push)
// Exported classes with standard-library members or bases. Harmless: a C++ API like this
// one already requires the library and its clients to use the same runtime.
#pragma warning(disable : 4251 4275)
#endif

namespace jsonschema {

/// Thrown by the throwing `uri` constructor when its input is not an RFC 3986 URI-reference.
class JSONSCHEMA_EXPORT uri_error : public std::invalid_argument
{
public:
    using std::invalid_argument::invalid_argument;
};

/// An RFC 3986 URI-reference: either an absolute URI or a relative reference.
///
/// Components are stored exactly as written (still percent-encoded). An absent
/// component (`std::nullopt`) is distinct from an empty one: `"a?"` has an empty
/// query, while `"a"` has none. This distinction is preserved by `to_string()` and
/// by reference resolution, as RFC 3986 §5.3 requires.
class JSONSCHEMA_EXPORT uri
{
public:
    /// An empty relative reference (`""`).
    uri() = default;

    /// Parses @p text; throws `uri_error` if it is not a valid URI-reference.
    explicit uri(std::string_view text);

    /// Parses @p text; returns `std::nullopt` if it is not a valid URI-reference.
    [[nodiscard]] static std::optional<uri> parse(std::string_view text);

    [[nodiscard]] const std::optional<std::string>& scheme() const noexcept { return scheme_; }
    [[nodiscard]] const std::optional<std::string>& authority() const noexcept { return authority_; }
    [[nodiscard]] const std::optional<std::string>& userinfo() const noexcept { return userinfo_; }
    /// The host part of the authority (`""` when there is no authority). IP literals keep their brackets.
    [[nodiscard]] const std::string& host() const noexcept { return host_; }
    [[nodiscard]] const std::optional<std::string>& port() const noexcept { return port_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const std::optional<std::string>& query() const noexcept { return query_; }
    [[nodiscard]] const std::optional<std::string>& fragment() const noexcept { return fragment_; }

    /// True if the reference has a scheme (RFC 3986 "URI"), with or without a fragment.
    [[nodiscard]] bool has_scheme() const noexcept { return scheme_.has_value(); }
    /// True if it has a scheme and no fragment (RFC 3986 "absolute-URI"), i.e. usable as a base URI.
    [[nodiscard]] bool is_absolute() const noexcept { return scheme_.has_value() && !fragment_.has_value(); }

    /// Resolves @p reference against this URI as its base (RFC 3986 §5.2.2, strict).
    /// The result is not normalized. If this URI has no scheme, the result is still
    /// computed by the same algorithm and may itself be relative.
    [[nodiscard]] uri resolve(const uri& reference) const;

    /// Syntax-based normalization (RFC 3986 §6.2.2): lowercase scheme and host, uppercase
    /// percent-encoding hex digits, decode percent-encoded unreserved characters, and remove
    /// dot segments. Also applies scheme-based normalization for http and https (§6.2.3):
    /// an empty path becomes "/" and the default port is removed.
    [[nodiscard]] uri normalized() const;

    /// A copy with the fragment removed.
    [[nodiscard]] uri without_fragment() const;
    /// A copy with the fragment replaced (`std::nullopt` removes it). @p fragment is stored as given.
    [[nodiscard]] uri with_fragment(std::optional<std::string> fragment) const;

    /// Recomposes the reference (RFC 3986 §5.3).
    [[nodiscard]] std::string to_string() const;

    /// Component-wise equality, without normalization. Normalize both sides first to compare
    /// URIs for equivalence.
    friend bool operator==(const uri&, const uri&) = default;

private:
    std::optional<std::string> scheme_;
    std::optional<std::string> authority_;
    std::optional<std::string> userinfo_;
    std::string host_;
    std::optional<std::string> port_;
    std::string path_;
    std::optional<std::string> query_;
    std::optional<std::string> fragment_;

    void set_authority(std::optional<std::string> authority);
};

/// Decodes percent-encoded octets. Returns `std::nullopt` if a `%` is not followed by two hex digits.
[[nodiscard]] JSONSCHEMA_EXPORT std::optional<std::string> percent_decode(std::string_view text);

/// Removes "." and ".." segments from @p path (RFC 3986 §5.2.4).
[[nodiscard]] JSONSCHEMA_EXPORT std::string remove_dot_segments(std::string_view path);

/// True if @p text is a valid IPv6address (RFC 3986 §3.2.2), without brackets.
[[nodiscard]] JSONSCHEMA_EXPORT bool is_ipv6_address(std::string_view text) noexcept;

/// True if @p text is a valid dotted-decimal IPv4address (RFC 3986 §3.2.2).
[[nodiscard]] JSONSCHEMA_EXPORT bool is_ipv4_address(std::string_view text) noexcept;

} // namespace jsonschema

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
