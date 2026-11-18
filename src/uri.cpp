// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include <jsonschema/uri.hpp>

#include <utility>

namespace jsonschema {

namespace {

constexpr bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_hex(char c) noexcept { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

constexpr bool is_unreserved(char c) noexcept
{
    return is_alpha(c) || is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

constexpr bool is_sub_delim(char c) noexcept
{
    switch (c) {
    case '!': case '$': case '&': case '\'': case '(': case ')':
    case '*': case '+': case ',': case ';': case '=':
        return true;
    default:
        return false;
    }
}

constexpr bool is_pchar_literal(char c) noexcept { return is_unreserved(c) || is_sub_delim(c) || c == ':' || c == '@'; }
constexpr bool is_path_literal(char c) noexcept { return is_pchar_literal(c) || c == '/'; }
constexpr bool is_query_literal(char c) noexcept { return is_pchar_literal(c) || c == '/' || c == '?'; }
constexpr bool is_userinfo_literal(char c) noexcept { return is_unreserved(c) || is_sub_delim(c) || c == ':'; }
constexpr bool is_reg_name_literal(char c) noexcept { return is_unreserved(c) || is_sub_delim(c); }

constexpr char to_lower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
constexpr char to_upper(char c) noexcept { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

constexpr int hex_value(char c) noexcept
{
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// True if every character of @p text is either allowed literally or part of a valid pct-encoded triplet.
template <typename Pred>
bool valid_component(std::string_view text, Pred literal) noexcept
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%') {
            if (i + 2 >= text.size() || !is_hex(text[i + 1]) || !is_hex(text[i + 2])) {
                return false;
            }
            i += 2;
        } else if (!literal(c)) {
            return false;
        }
    }
    return true;
}

bool is_ip_future(std::string_view text) noexcept
{
    // IPvFuture = "v" 1*HEXDIG "." 1*( unreserved / sub-delims / ":" )
    if (text.size() < 4 || (text[0] != 'v' && text[0] != 'V')) {
        return false;
    }
    std::size_t i = 1;
    while (i < text.size() && is_hex(text[i])) {
        ++i;
    }
    if (i == 1 || i >= text.size() || text[i] != '.') {
        return false;
    }
    ++i;
    if (i >= text.size()) {
        return false;
    }
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (!(is_unreserved(c) || is_sub_delim(c) || c == ':')) {
            return false;
        }
    }
    return true;
}

/// Counts 16-bit pieces in a run of ':'-separated IPv6 groups. Returns -1 if malformed.
int ipv6_piece_count(std::string_view part, bool allow_trailing_ipv4) noexcept
{
    if (part.empty()) {
        return 0;
    }
    int count = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = part.find(':', start);
        const std::string_view group = part.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        const bool last = end == std::string_view::npos;
        if (last && allow_trailing_ipv4 && group.find('.') != std::string_view::npos) {
            if (!is_ipv4_address(group)) {
                return -1;
            }
            count += 2;
        } else {
            if (group.empty() || group.size() > 4) {
                return -1;
            }
            for (const char c : group) {
                if (!is_hex(c)) {
                    return -1;
                }
            }
            count += 1;
        }
        if (last) {
            return count;
        }
        start = end + 1;
    }
}

void remove_last_segment(std::string& output)
{
    const std::size_t slash = output.rfind('/');
    if (slash == std::string::npos) {
        output.clear();
    } else {
        output.erase(slash);
    }
}

/// Uppercases the hex digits of pct-encoded triplets and decodes those that encode unreserved characters.
/// When @p lowercase is set, literal characters are also lowercased (for scheme-insensitive hosts).
std::string normalize_percent_encoding(std::string_view text, bool lowercase)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%' && i + 2 < text.size() && is_hex(text[i + 1]) && is_hex(text[i + 2])) {
            const auto decoded = static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            if (is_unreserved(decoded)) {
                out.push_back(lowercase ? to_lower(decoded) : decoded);
            } else {
                out.push_back('%');
                out.push_back(to_upper(text[i + 1]));
                out.push_back(to_upper(text[i + 2]));
            }
            i += 2;
        } else {
            out.push_back(lowercase ? to_lower(c) : c);
        }
    }
    return out;
}

struct authority_parts
{
    std::optional<std::string> userinfo;
    std::string host;
    std::optional<std::string> port;
};

std::optional<authority_parts> parse_authority(std::string_view authority)
{
    authority_parts parts;
    std::string_view host_port = authority;
    if (const std::size_t at = authority.find('@'); at != std::string_view::npos) {
        const std::string_view userinfo = authority.substr(0, at);
        if (!valid_component(userinfo, is_userinfo_literal)) {
            return std::nullopt;
        }
        parts.userinfo = std::string(userinfo);
        host_port = authority.substr(at + 1);
    }

    std::string_view host;
    std::string_view rest;
    if (!host_port.empty() && host_port.front() == '[') {
        const std::size_t close = host_port.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view literal = host_port.substr(1, close - 1);
        if (!is_ipv6_address(literal) && !is_ip_future(literal)) {
            return std::nullopt;
        }
        host = host_port.substr(0, close + 1);
        rest = host_port.substr(close + 1);
        if (!rest.empty() && rest.front() != ':') {
            return std::nullopt;
        }
    } else {
        const std::size_t colon = host_port.find(':');
        host = host_port.substr(0, colon);
        rest = colon == std::string_view::npos ? std::string_view{} : host_port.substr(colon);
        if (!valid_component(host, is_reg_name_literal)) {
            return std::nullopt;
        }
    }
    parts.host = std::string(host);

    if (!rest.empty()) {
        const std::string_view port = rest.substr(1);
        for (const char c : port) {
            if (!is_digit(c)) {
                return std::nullopt;
            }
        }
        parts.port = std::string(port);
    }
    return parts;
}

} // namespace

bool is_ipv4_address(std::string_view text) noexcept
{
    int octets = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find('.', start);
        const std::string_view octet = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (octet.empty() || octet.size() > 3 || (octet.size() > 1 && octet.front() == '0')) {
            return false;
        }
        int value = 0;
        for (const char c : octet) {
            if (!is_digit(c)) {
                return false;
            }
            value = value * 10 + (c - '0');
        }
        if (value > 255) {
            return false;
        }
        ++octets;
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return octets == 4;
}

bool is_ipv6_address(std::string_view text) noexcept
{
    const std::size_t double_colon = text.find("::");
    if (double_colon == std::string_view::npos) {
        return ipv6_piece_count(text, true) == 8;
    }
    if (text.find("::", double_colon + 1) != std::string_view::npos) {
        return false;
    }
    const int left = ipv6_piece_count(text.substr(0, double_colon), false);
    const int right = ipv6_piece_count(text.substr(double_colon + 2), true);
    return left >= 0 && right >= 0 && left + right <= 7;
}

std::optional<std::string> percent_decode(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size() || !is_hex(text[i + 1]) || !is_hex(text[i + 2])) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2])));
        i += 2;
    }
    return out;
}

std::string remove_dot_segments(std::string_view path)
{
    std::string input(path);
    std::string output;
    output.reserve(input.size());
    while (!input.empty()) {
        if (input.starts_with("../")) {
            input.erase(0, 3);
        } else if (input.starts_with("./")) {
            input.erase(0, 2);
        } else if (input.starts_with("/./")) {
            input.erase(0, 2);
        } else if (input == "/.") {
            input = "/";
        } else if (input.starts_with("/../")) {
            input.erase(0, 3);
            remove_last_segment(output);
        } else if (input == "/..") {
            input = "/";
            remove_last_segment(output);
        } else if (input == "." || input == "..") {
            input.clear();
        } else {
            const std::size_t next = input.find('/', input.front() == '/' ? 1 : 0);
            output.append(input, 0, next);
            input.erase(0, next);
        }
    }
    return output;
}

uri::uri(std::string_view text)
{
    auto parsed = parse(text);
    if (!parsed) {
        throw uri_error("invalid URI-reference: \"" + std::string(text) + "\"");
    }
    *this = std::move(*parsed);
}

std::optional<uri> uri::parse(std::string_view text)
{
    uri result;
    std::string_view rest = text;

    // scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"
    if (const std::size_t colon = rest.find(':'); colon != std::string_view::npos && colon > 0 && is_alpha(rest.front())) {
        bool is_scheme = true;
        for (std::size_t i = 1; i < colon; ++i) {
            const char c = rest[i];
            if (!(is_alpha(c) || is_digit(c) || c == '+' || c == '-' || c == '.')) {
                is_scheme = false;
                break;
            }
        }
        if (is_scheme) {
            result.scheme_ = std::string(rest.substr(0, colon));
            rest.remove_prefix(colon + 1);
        }
    }

    if (const std::size_t hash = rest.find('#'); hash != std::string_view::npos) {
        const std::string_view fragment = rest.substr(hash + 1);
        if (!valid_component(fragment, is_query_literal)) {
            return std::nullopt;
        }
        result.fragment_ = std::string(fragment);
        rest = rest.substr(0, hash);
    }

    if (const std::size_t question = rest.find('?'); question != std::string_view::npos) {
        const std::string_view query = rest.substr(question + 1);
        if (!valid_component(query, is_query_literal)) {
            return std::nullopt;
        }
        result.query_ = std::string(query);
        rest = rest.substr(0, question);
    }

    if (rest.starts_with("//")) {
        const std::size_t slash = rest.find('/', 2);
        const std::string_view authority = rest.substr(2, slash == std::string_view::npos ? std::string_view::npos : slash - 2);
        auto parts = parse_authority(authority);
        if (!parts) {
            return std::nullopt;
        }
        result.authority_ = std::string(authority);
        result.userinfo_ = std::move(parts->userinfo);
        result.host_ = std::move(parts->host);
        result.port_ = std::move(parts->port);
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
    }

    if (!valid_component(rest, is_path_literal)) {
        return std::nullopt;
    }
    if (!result.scheme_ && !result.authority_) {
        // path-noscheme: the first segment of a relative-path reference may not contain ':'
        const std::string_view first_segment = rest.substr(0, rest.find('/'));
        if (first_segment.find(':') != std::string_view::npos) {
            return std::nullopt;
        }
    }
    result.path_ = std::string(rest);
    return result;
}

void uri::set_authority(std::optional<std::string> authority)
{
    authority_ = std::move(authority);
    userinfo_.reset();
    host_.clear();
    port_.reset();
    if (authority_) {
        auto parts = parse_authority(*authority_);
        if (parts) {
            userinfo_ = std::move(parts->userinfo);
            host_ = std::move(parts->host);
            port_ = std::move(parts->port);
        }
    }
}

uri uri::resolve(const uri& reference) const
{
    const uri& base = *this;
    uri target;
    if (reference.scheme_) {
        target.scheme_ = reference.scheme_;
        target.set_authority(reference.authority_);
        target.path_ = remove_dot_segments(reference.path_);
        target.query_ = reference.query_;
    } else {
        if (reference.authority_) {
            target.set_authority(reference.authority_);
            target.path_ = remove_dot_segments(reference.path_);
            target.query_ = reference.query_;
        } else {
            if (reference.path_.empty()) {
                target.path_ = base.path_;
                target.query_ = reference.query_ ? reference.query_ : base.query_;
            } else {
                if (reference.path_.front() == '/') {
                    target.path_ = remove_dot_segments(reference.path_);
                } else {
                    // merge (RFC 3986 §5.2.3)
                    std::string merged;
                    if (base.authority_ && base.path_.empty()) {
                        merged = "/" + reference.path_;
                    } else {
                        const std::size_t slash = base.path_.rfind('/');
                        merged = slash == std::string::npos ? reference.path_ : base.path_.substr(0, slash + 1) + reference.path_;
                    }
                    target.path_ = remove_dot_segments(merged);
                }
                target.query_ = reference.query_;
            }
            target.set_authority(base.authority_);
        }
        target.scheme_ = base.scheme_;
    }
    target.fragment_ = reference.fragment_;
    return target;
}

uri uri::normalized() const
{
    uri n;
    if (scheme_) {
        std::string scheme = *scheme_;
        for (char& c : scheme) {
            c = to_lower(c);
        }
        n.scheme_ = std::move(scheme);
    }
    if (authority_) {
        std::string authority;
        if (userinfo_) {
            authority += normalize_percent_encoding(*userinfo_, false);
            authority += '@';
        }
        authority += normalize_percent_encoding(host_, true);
        std::optional<std::string> port = port_;
        if (port && port->empty()) {
            port.reset();
        }
        if (port && n.scheme_ && ((*n.scheme_ == "http" && *port == "80") || (*n.scheme_ == "https" && *port == "443"))) {
            port.reset();
        }
        if (port) {
            authority += ':';
            authority += *port;
        }
        n.set_authority(std::move(authority));
    }
    n.path_ = normalize_percent_encoding(path_, false);
    if (scheme_) {
        n.path_ = remove_dot_segments(n.path_);
    }
    if (n.authority_ && n.path_.empty() && n.scheme_ && (*n.scheme_ == "http" || *n.scheme_ == "https")) {
        n.path_ = "/";
    }
    if (query_) {
        n.query_ = normalize_percent_encoding(*query_, false);
    }
    if (fragment_) {
        n.fragment_ = normalize_percent_encoding(*fragment_, false);
    }
    return n;
}

uri uri::without_fragment() const
{
    return with_fragment(std::nullopt);
}

uri uri::with_fragment(std::optional<std::string> fragment) const
{
    uri copy = *this;
    copy.fragment_ = std::move(fragment);
    return copy;
}

std::string uri::to_string() const
{
    std::string out;
    if (scheme_) {
        out += *scheme_;
        out += ':';
    }
    if (authority_) {
        out += "//";
        out += *authority_;
    }
    out += path_;
    if (query_) {
        out += '?';
        out += *query_;
    }
    if (fragment_) {
        out += '#';
        out += *fragment_;
    }
    return out;
}

} // namespace jsonschema
