# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# jsonschema_configure_target(<target>)
# Applies the project's warning level, optional -Werror, and optional sanitizers.
function(jsonschema_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(JSONSCHEMA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(JSONSCHEMA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    if(JSONSCHEMA_SANITIZE)
        if(MSVC)
            message(WARNING "JSONSCHEMA_SANITIZE is ignored for MSVC")
        else()
            target_compile_options(${target} PRIVATE -fsanitize=${JSONSCHEMA_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
            target_link_options(${target} PRIVATE -fsanitize=${JSONSCHEMA_SANITIZE})
        endif()
    endif()
endfunction()
