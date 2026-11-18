// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Unicode and IDNA support (design doc 5.4): Punycode, NFC, the generated property tables,
// and the idn-hostname, idn-email and hostname A-label checks beyond the test suite.
#include <doctest/doctest.h>

#include <string>

#include "formats.hpp"
#include "unicode/idna.hpp"
#include "unicode/unicode.hpp"

namespace idna = jsonschema::detail::idna;
namespace unicode = jsonschema::detail::unicode;

TEST_CASE("punycode: RFC 3492 §7.1 samples")
{
    struct sample
    {
        std::u32string decoded;
        std::string encoded;
    };
    const sample samples[] = {
        // (A) Arabic (Egyptian)
        {U"ليهمابتكلموشعربي؟",
         "egbpdaj6bu4bxfgehfvwxn"},
        // (B) Chinese (simplified)
        {U"他们为什么不说中文", "ihqwcrb4cv8a8dqg056pqjye"},
        // (L) 3<nen>B<gumi><kinpachi><sensei>
        {U"3年B組金八先生", "3B-ww4c5e180e575a65lsy2b"},
        // (R) <sono><supiido><de>
        {U"そのスピードで", "d9juau41awczczp"},
        // (S) -> $1.00 <-
        {U"-> $1.00 <-", "-> $1.00 <--"},
    };
    for (const auto& s : samples) {
        CAPTURE(s.encoded);
        const auto decoded = idna::punycode_decode(s.encoded);
        REQUIRE(decoded);
        CHECK(*decoded == s.decoded);
        const auto encoded = idna::punycode_encode(s.decoded);
        REQUIRE(encoded);
        // The encoder writes lower case; sample (L) keeps the case of its basic code points.
        CHECK(*encoded == s.encoded);
    }
    CHECK(idna::punycode_decode("EGBPDAJ6BU4BXFGEHFVWXN") == samples[0].decoded);   // case-insensitive
    CHECK_FALSE(idna::punycode_decode("x"));            // truncated
    CHECK_FALSE(idna::punycode_decode("99999999999"));  // overflow
    CHECK_FALSE(idna::punycode_decode("a\xC3\xA9-b"));  // non-basic code point before the delimiter
}

TEST_CASE("unicode: NFC and properties")
{
    CHECK(unicode::version() == "17.0.0");
    CHECK(unicode::nfc(U"café") == U"café");
    CHECK(unicode::nfc(U"각") == U"각");          // Hangul L V T
    CHECK(unicode::nfc(U"ậ") == U"ậ");               // reordered marks compose
    CHECK(unicode::nfc(U"Å") == U"Å");                      // singleton ANGSTROM SIGN
    CHECK(unicode::nfc(U"क़") == U"क़");                // composition exclusion
    CHECK(unicode::idna2008_property(U'a') == unicode::idna_property::pvalid);
    CHECK(unicode::idna2008_property(U'A') == unicode::idna_property::disallowed);
    CHECK(unicode::idna2008_property(U'‍') == unicode::idna_property::contextj);
    CHECK(unicode::idna2008_property(U'·') == unicode::idna_property::contexto);
    CHECK(unicode::idna2008_property(U'≠') == unicode::idna_property::disallowed);   // ≠: valid in UTS #46 only
    CHECK(unicode::idna2008_property(U'\U000E0000') == unicode::idna_property::unassigned);
    CHECK(unicode::bidi(U'א') == unicode::bidi_class::R);
    CHECK(unicode::bidi(U'٠') == unicode::bidi_class::AN);
    CHECK(unicode::joining(U'ب') == unicode::joining_type::D);
    CHECK(unicode::script_of(U'α') == unicode::script::greek);
    CHECK(unicode::combining_class(U'्') == 9);
    std::u32string mapping;
    CHECK(unicode::uts46_map(U'Ａ', mapping) == unicode::uts46_status::mapped);
    CHECK(mapping == U"a");
    CHECK(unicode::uts46_map(U'­', mapping) == unicode::uts46_status::ignored);
    CHECK(unicode::uts46_map(U'ß', mapping) == unicode::uts46_status::valid);        // deviation
}

TEST_CASE("idn-hostname")
{
    CHECK(idna::is_idn_hostname("m\xC3\xBCnchen.de"));                      // münchen.de
    CHECK(idna::is_idn_hostname("M\xC3\x9CNCHEN.DE"));                      // mapped to lower case
    CHECK(idna::is_idn_hostname("xn--mnchen-3ya.de"));
    CHECK(idna::is_idn_hostname("fa\xC3\x9F.de"));                          // faß.de: nontransitional
    CHECK_FALSE(idna::is_idn_hostname("a\xE2\x89\xA0" "b"));                 // a≠b: not IDNA2008
    CHECK_FALSE(idna::is_idn_hostname("xn--"));                             // empty A-label
    CHECK_FALSE(idna::is_idn_hostname("xn--mnchen-.de"));                   // decodes to ASCII only
    CHECK_FALSE(idna::is_idn_hostname("a_b"));                              // STD3
    CHECK_FALSE(idna::is_idn_hostname("\xC3"));                             // ill-formed UTF-8
    CHECK(idna::is_idn_hostname("\xD7\x90.a1"));                            // Bidi domain: an LTR label is fine...
    CHECK_FALSE(idna::is_idn_hostname("\xD7\x90.1a"));                      // ...unless it starts with a digit
    CHECK_FALSE(idna::is_idn_hostname("a\xD7\x90"));                        // R in an LTR label
}

TEST_CASE("hostname: A-labels")
{
    namespace jd = jsonschema::detail;
    CHECK(jd::is_hostname("xn--mnchen-3ya.de"));
    CHECK(jd::is_hostname("XN--MNCHEN-3YA.DE"));
    CHECK_FALSE(jd::is_hostname("xn--.de"));
    CHECK_FALSE(jd::is_hostname("xn--mnchen-.de"));      // decodes to ASCII only
    CHECK_FALSE(jd::is_hostname("xn--1ch.example"));     // ≠
    CHECK(jd::is_hostname("ab--cd.example"));
}

TEST_CASE("idn-email")
{
    namespace jd = jsonschema::detail;
    CHECK(jd::is_idn_email("\xCE\xB4\xCE\xBF\xCE\xBA@m\xC3\xBCnchen.de"));
    CHECK(jd::is_idn_email("user@[192.168.0.1]"));
    CHECK_FALSE(jd::is_idn_email("user@a\xE2\x89\xA0" "b.de"));
    CHECK_FALSE(jd::is_idn_email("\xC3@example.com"));                     // ill-formed UTF-8
    CHECK_FALSE(jd::is_email("\xCE\xB4@example.com"));                     // plain email stays ASCII
}
