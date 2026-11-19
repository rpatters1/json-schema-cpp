// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <jsonschema/version.hpp>

namespace jsonschema {

std::string_view version() noexcept
{
    return JSONSCHEMA_VERSION_STRING;
}

} // namespace jsonschema
