// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <string>

#include <jsonschema/version.hpp>

TEST_CASE("version: runtime matches the headers")
{
    CHECK(jsonschema::version() == JSONSCHEMA_VERSION_STRING);
    const std::string expected = std::to_string(jsonschema::version_major) + "." +
                                 std::to_string(jsonschema::version_minor) + "." +
                                 std::to_string(jsonschema::version_patch);
    CHECK(jsonschema::version() == expected);
    CHECK(JSONSCHEMA_VERSION_MAJOR == jsonschema::version_major);
}
