// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include "json/pointer.hpp"

#include <cstddef>

namespace jsonschema::detail::json_pointer {

std::string escape(std::string_view token)
{
    std::string result;
    result.reserve(token.size());
    for (const char c : token) {
        if (c == '~') {
            result += "~0";
        } else if (c == '/') {
            result += "~1";
        } else {
            result += c;
        }
    }
    return result;
}

void append(std::string& pointer, std::string_view token)
{
    pointer += '/';
    pointer += escape(token);
}

std::optional<std::vector<std::string>> parse(std::string_view pointer)
{
    std::vector<std::string> tokens;
    if (pointer.empty()) {
        return tokens;
    }
    if (pointer.front() != '/') {
        return std::nullopt;
    }
    std::string current;
    for (std::size_t i = 1; i <= pointer.size(); ++i) {
        if (i == pointer.size() || pointer[i] == '/') {
            tokens.push_back(std::move(current));
            current.clear();
            continue;
        }
        const char c = pointer[i];
        if (c == '~') {
            if (i + 1 >= pointer.size()) {
                return std::nullopt;
            }
            const char next = pointer[++i];
            if (next == '0') {
                current += '~';
            } else if (next == '1') {
                current += '/';
            } else {
                return std::nullopt;
            }
        } else {
            current += c;
        }
    }
    return tokens;
}

namespace {

std::optional<std::size_t> array_index(const std::string& token)
{
    if (token.empty() || (token.size() > 1 && token.front() == '0')) {
        return std::nullopt;
    }
    std::size_t value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::size_t>(c - '0');
        if (value > (static_cast<std::size_t>(-1) - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

} // namespace

const json* navigate(const json& root, std::string_view pointer)
{
    const auto tokens = parse(pointer);
    if (!tokens) {
        return nullptr;
    }
    const json* current = &root;
    for (const auto& token : *tokens) {
        if (current->is_object()) {
            const auto it = current->find(token);
            if (it == current->end()) {
                return nullptr;
            }
            current = &*it;
        } else if (current->is_array()) {
            const auto index = array_index(token);
            if (!index || *index >= current->size()) {
                return nullptr;
            }
            current = &(*current)[*index];
        } else {
            return nullptr;
        }
    }
    return current;
}

} // namespace jsonschema::detail::json_pointer
