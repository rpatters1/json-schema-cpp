# Embedded meta-schemas

The JSON Schema 2020-12 meta-schemas, exactly as published at
`https://json-schema.org/draft/2020-12/` (retrieved 2026-11-18). The build embeds them
(`metaschemas.cpp.in`), so `$schema` and `$ref` to these URIs never need a loader.

They are Copyright (c) 2022 JSON Schema Specification Authors, under the BSD 3-clause
licence in [LICENSE](LICENSE) (the JSON Schema specification is dual-licensed BSD-3-Clause
or AFL-3.0).
