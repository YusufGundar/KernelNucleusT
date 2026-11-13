/*
  knst_json_basic.cpp

  Comprehensive tutorial-style demonstration of the KernelNucleusT JSON
  module. Exercises every public API surface end-to-end.

  Sections
  --------
    A. Values       1-3   Construction, numbers, strings
    B. Parsing      4-10  Simple, nested, deep, malformed, lenient, dup, prefix
    C. Queries     11-12  Type queries, accessors (value_or, value, value_at)
    D. Mutation    13-16  Array ops, object ops, iteration
    E. Dump        17-22  Compact/pretty, escapes, ensure_ascii, sort_keys, canonical, round-trip
    F. Identity    23-25  Deep copy, compare, hash
    G. Queries     26-32  Pointer, Patch, Merge, Walk, Flatten, JSONPath, Schema
    H. I/O         33-36  File, streaming dump, SAX, NDJSON
    I. Utility     37-38  count_nodes/clear/shrink, integration build+dump

  Requirement: KernelNucleusT.hpp (pulls in knst_json.hpp).
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>


/* =====================================================================
 *  Console helpers
 * ===================================================================== */

static int s_pass = 0, s_fail = 0, s_skip = 0;

static void section(const char* title, const char* what, const char* expect) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "  WHAT    : " << what << "\n";
    std::cout << "  EXPECT  : " << expect << "\n";
    std::cout << "--------------------------------------------------\n";
}

static void check(const char* name, bool ok) {
    if (ok) { ++s_pass; std::cout << "  >>> PASS : " << name << "\n"; }
    else    { ++s_fail; std::cout << "  >>> FAIL : " << name << "\n"; }
}

static void print_buf(const knst_json_buffer& b, size_t max_len = 200) {
    const size_t n = b.size() < max_len ? b.size() : max_len;
    for (size_t i = 0; i < n; ++i) {
        const char c = b.data()[i];
        std::cout << (c >= 32 && c < 127 ? c : (c == '\n' ? '\n' : '.'));
    }
    if (b.size() > max_len) std::cout << "...";
}

static void print_cstr(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        std::cout << (c >= 32 && c < 127 ? c : '.');
    }
}


/* =====================================================================
 *  main
 * ===================================================================== */

int main() {

    std::cout << "knst_json_basic — comprehensive test suite\n";
    std::cout << "sizeof(knst_json)        : " << sizeof(knst_json)        << " bytes\n";
    std::cout << "sizeof(knst_json_error)  : " << sizeof(knst_json_error)  << " bytes\n";


    /* =================================================================
     *  1) Construction — every type
     * ================================================================= */
    section("1) Construction",
            "Default, bool, integral, floating, string, initializer-list",
            "all types construct");
    {
        knst_json j_null;
        knst_json j_null2(nullptr);
        knst_json j_bool(true);
        knst_json j_int(42);
        knst_json j_neg(-7);
        knst_json j_float(3.14);
        knst_json j_str("hello");
        knst_json j_arr = knst_json::make_array();
        knst_json j_obj = knst_json::make_object();

        knst_json j_arr_init = knst_json::make_array({ knst_json(1), knst_json(2), knst_json(3) });
        knst_json j_obj_init = knst_json::make_object({
            { "name", knst_json("knst") },
            { "port", knst_json(8080) }
        });

        std::cout << "  null    : " << j_null.type_name()  << "\n";
        std::cout << "  bool    : " << j_bool.type_name()  << "\n";
        std::cout << "  int     : " << j_int.type_name()   << "\n";
        std::cout << "  float   : " << j_float.type_name() << "\n";
        std::cout << "  string  : " << j_str.type_name()   << "\n";
        std::cout << "  array   : " << j_arr.type_name()   << "\n";
        std::cout << "  object  : " << j_obj.type_name()   << "\n";
        std::cout << "  init arr size : " << j_arr_init.size() << "\n";
        std::cout << "  init obj size : " << j_obj_init.size() << "\n";

        check("construction", j_null.is_null() && j_bool.get_type() == knst_json_type::boolean &&
              j_int.get_type() == knst_json_type::integer && j_float.get_type() == knst_json_type::floating &&
              j_str.is_string() && j_arr.is_array() && j_obj.is_object() &&
              j_arr_init.size() == 3 && j_obj_init.size() == 2);
    }


    /* =================================================================
     *  2) Numbers — edge cases
     * ================================================================= */
    section("2) Numbers",
            "int64 / uint64 / double precision / exponents",
            "all representable values round-trip");
    {
        knst_json j;

        j.set_int64(INT64_MIN);
        std::cout << "  INT64_MIN  : " << j.value_or<int64_t>(0) << "\n";

        j.set_int64(INT64_MAX);
        std::cout << "  INT64_MAX  : " << j.value_or<int64_t>(0) << "\n";

        j.set_uint64(UINT64_MAX);
        std::cout << "  UINT64_MAX : " << j.value_or<uint64_t>(0) << "\n";

        j.set_double(0.1);
        std::cout << "  0.1        : " << j.value_or(0.0) << "\n";

        knst_json pi;
        knst_json_error e;
        knst_json::parse("3.141592653589793", 17, pi, &e);
        std::cout << "  PI         : " << pi.value_or(0.0) << "\n";

        knst_json exp_form;
        knst_json::parse("1.5e10", 6, exp_form, &e);
        std::cout << "  1.5e10     : " << exp_form.value_or(0.0) << "\n";

        const bool ok = pi.is_float() && exp_form.value_or(0.0) == 1.5e10;
        check("numbers", ok);
    }


    /* =================================================================
     *  3) Strings — UTF-8 / UTF-16 / escapes
     * ================================================================= */
    section("3) Strings",
            "ASCII, UTF-8 multibyte, UTF-16 input, surrogate pair",
            "all round-trip");
    {
        // UTF-8 encoded "世界" = U+4E16 U+754C = E4 B8 96 E7 95 8C
        const char utf8_world[] = "\xE4\xB8\x96\xE7\x95\x8C";
        // UTF-8 encoded 😀 = U+1F600 = F0 9F 98 80
        const char utf8_emoji[] = "\xF0\x9F\x98\x80";

        knst_json j1("hello");
        knst_json j2(utf8_world, sizeof(utf8_world) - 1);
        knst_json j3(utf8_emoji, sizeof(utf8_emoji) - 1);

        std::cout << "  ascii  size : " << j1.length() << "\n";
        std::cout << "  world  size : " << j2.length() << " bytes\n";
        std::cout << "  emoji  size : " << j3.length() << " bytes\n";

        knst_json j4;
        j4.set_string_u16(u"\u4E16\u754C", 2);
        std::cout << "  u16->utf8   : " << j4.length() << " bytes\n";

        knst_json j5;
        knst_json_error e;
        knst_json::parse("\"\\uD83D\\uDE00\"", 14, j5, &e);
        std::cout << "  \\uD83D\\uDE00 : " << j5.length() << " bytes\n";

        const bool ok = j1.string_equals("hello") && j2.length() == 6 && j3.length() == 4 &&
                        j4.length() == 6 && j5.length() == 4;
        check("strings", ok);
    }


    /* =================================================================
     *  4) Parse — simple values
     * ================================================================= */
    section("4) Parse simple",
            "Each scalar type individually",
            "all parse");
    {
        knst_json j;
        knst_json_error e;
        bool ok = true;

        ok &= knst_json::parse("null",  4, j, &e) && j.is_null();
        ok &= knst_json::parse("true",  4, j, &e) && j.is_bool();
        ok &= knst_json::parse("false", 5, j, &e) && j.is_bool();
        ok &= knst_json::parse("42",    2, j, &e) && j.is_integer();
        ok &= knst_json::parse("-7",    2, j, &e) && j.is_integer();
        ok &= knst_json::parse("3.14",  4, j, &e) && j.is_float();
        ok &= knst_json::parse("\"hi\"", 4, j, &e) && j.is_string();
        ok &= knst_json::parse("[]",    2, j, &e) && j.is_array() && j.size() == 0;
        ok &= knst_json::parse("{}",    2, j, &e) && j.is_object() && j.size() == 0;

        check("parse simple", ok);
    }


    /* =================================================================
     *  5) Parse — nested
     * ================================================================= */
    section("5) Parse nested",
            "Realistic config-like document",
            "correct structure");
    {
        const char* json =
            R"({"name":"knst","version":2,"port":8080,)"
            R"("tags":["json","cpp","tutorial"],)"
            R"("active":true,"limits":{"rate":100,"burst":500}})";

        knst_json j;
        knst_json_error e;
        const bool ok = knst_json::parse(json, std::strlen(json), j, &e);

        std::cout << "  name     : " << j.value("name", "")     << "\n";
        std::cout << "  port     : " << j.value("port", 0)      << "\n";
        std::cout << "  tags     : " << (j.find("tags") ? j.find("tags")->size() : 0) << " items\n";
        std::cout << "  rate     : " << j.value_at("/limits/rate", 0) << "\n";

        check("parse nested", ok && j.is_object() && j.size() == 6 &&
              j.value("port", 0) == 8080 && j.value_at("/limits/burst", 0) == 500);
    }


    /* =================================================================
     *  6) Parse — deep nesting (parser is iterative)
     * ================================================================= */
    section("6) Parse deep",
            "256 levels OK, 600 levels exceeds max_depth (default 512)",
            "no stack overflow either way");
    {
        char deep_ok[1024];
        int n = 0;
        for (int i = 0; i < 256; ++i) deep_ok[n++] = '[';
        for (int i = 0; i < 256; ++i) deep_ok[n++] = ']';

        char deep_too_far[1300];
        int m = 0;
        for (int i = 0; i < 600; ++i) deep_too_far[m++] = '[';
        for (int i = 0; i < 600; ++i) deep_too_far[m++] = ']';

        knst_json j;
        knst_json_error e;
        const bool ok256 = knst_json::parse(deep_ok, static_cast<size_t>(n), j, &e);
        std::cout << "  256 deep : " << (ok256 ? "ok" : "fail") << "\n";

        const bool ok600 = knst_json::parse(deep_too_far, static_cast<size_t>(m), j, &e);
        std::cout << "  600 deep : " << (ok600 ? "ok" : "fail")
                  << " (err=" << e.message() << ")\n";

        check("parse deep", ok256 && !ok600 && e.code == knst_json_errc::max_depth_exceeded);
    }


    /* =================================================================
     *  7) Parse — malformed → error codes
     * ================================================================= */
    section("7) Parse malformed",
            "Every error type: missing brace, bad number, lone surrogate, invalid escape",
            "error code + offset + message");
    {
        knst_json j;
        knst_json_error e;

        knst_json::parse("{\"a\"", 4, j, &e);
        std::cout << "  missing end : " << e.message()
                  << "  offset=" << e.offset << "\n";
        const bool t1 = e.code == knst_json_errc::unexpected_end;

        knst_json::parse("01", 2, j, &e);
        std::cout << "  leading 0   : " << e.message() << "\n";
        const bool t2 = e.code == knst_json_errc::invalid_number;

        knst_json::parse("\"\\uDC00\"", 8, j, &e);
        std::cout << "  lone surr   : " << e.message() << "\n";
        const bool t3 = e.code == knst_json_errc::lone_surrogate;

        knst_json::parse("\"\\x41\"", 6, j, &e);
        std::cout << "  bad escape  : " << e.message() << "\n";
        const bool t4 = e.code == knst_json_errc::invalid_escape;

        knst_json::parse("[1,]", 4, j, &e);
        std::cout << "  trailing ,  : " << e.message() << "\n";
        const bool t5 = e.code == knst_json_errc::unexpected_char;

        knst_json::parse("", 0, j, &e);
        std::cout << "  empty input : " << e.message() << "\n";
        const bool t6 = e.code == knst_json_errc::empty_input;

        check("parse malformed", t1 && t2 && t3 && t4 && t5 && t6);
    }


    /* =================================================================
     *  8) Parse — lenient mode
     * ================================================================= */
    section("8) Parse lenient",
            "Comments, trailing commas, NaN/Infinity, single quotes, unquoted keys",
            "all accepted in lenient, rejected in strict");
    {
        knst_json_parse_options lenient = knst_json_parse_options::lenient();
        knst_json j;
        knst_json_error e;

        const char* json1 = "{a: 1, /* c */ b: 2, }";
        const bool t1 = knst_json::parse(json1, std::strlen(json1), j, &e, lenient);

        const char* json2 = "NaN";
        const bool t2 = knst_json::parse(json2, 3, j, &e, lenient);

        const char* json3 = "'single'";
        const bool t3 = knst_json::parse(json3, 8, j, &e, lenient);

        const bool strict1 = knst_json::parse(json1, std::strlen(json1), j, &e);
        const bool strict2 = knst_json::parse(json2, 3, j, &e);
        const bool strict3 = knst_json::parse(json3, 8, j, &e);

        std::cout << "  lenient: comments=" << t1 << " NaN=" << t2 << " single=" << t3 << "\n";
        std::cout << "  strict : comments=" << strict1 << " NaN=" << strict2 << " single=" << strict3 << "\n";

        check("parse lenient", t1 && t2 && t3 && !strict1 && !strict2 && !strict3);
    }


    /* =================================================================
     *  9) Parse — duplicate key policies
     * ================================================================= */
    section("9) Parse duplicate keys",
            "last_wins / first_wins / error",
            "all policies work");
    {
        const char* dup = "{\"a\":1,\"a\":2}";
        knst_json j;
        knst_json_error e;

        knst_json::parse(dup, std::strlen(dup), j, &e);
        const int64_t last = j.value("a", 0);
        std::cout << "  last_wins  : a=" << last << "\n";

        knst_json_parse_options first = knst_json_parse_options();
        first.duplicate_keys = knst_json_duplicate_keys::first_wins;
        knst_json::parse(dup, std::strlen(dup), j, &e, first);
        const int64_t first_v = j.value("a", 0);
        std::cout << "  first_wins : a=" << first_v << "\n";

        knst_json_parse_options err_opt = knst_json_parse_options();
        err_opt.duplicate_keys = knst_json_duplicate_keys::error;
        const bool err_ok = !knst_json::parse(dup, std::strlen(dup), j, &e, err_opt);
        std::cout << "  error      : " << e.message() << "\n";

        check("duplicate keys", last == 2 && first_v == 1 && err_ok &&
              e.code == knst_json_errc::duplicate_key);
    }


    /* =================================================================
     *  10) Parse prefix (concatenated values)
     * ================================================================= */
    section("10) Parse prefix",
            "Read one value at a time from a concatenated stream",
            "consumed bytes correct");
    {
        const char* stream = "42 3.14 \"hi\"";
        const size_t total = std::strlen(stream);
        size_t pos = 0, consumed = 0;
        knst_json j;
        knst_json_error e;

        int count = 0;
        while (pos < total) {
            if (!knst_json::parse_prefix(stream + pos, total - pos, j, consumed, &e)) break;
            pos += consumed;
            ++count;
            std::cout << "  value " << count << " consumed " << consumed << " bytes\n";
        }
        std::cout << "  total parsed : " << count << " values\n";
        check("parse prefix", count == 3);
    }


    /* =================================================================
     *  11) Type queries
     * ================================================================= */
    section("11) Type queries",
            "is_* flags + is_integral_value + is_scalar + is_container",
            "all distinctions correct");
    {
        knst_json j_int(42);
        knst_json j_float(3.14);
        knst_json j_int_float(3.0);
        knst_json j_str("x");
        knst_json j_arr = knst_json::make_array();
        knst_json j_obj = knst_json::make_object();

        std::cout << "  int 42     : " << (j_int.is_integer() ? "integer" : "?")
                  << "  integral=" << (j_int.is_integral_value() ? "yes" : "no") << "\n";
        std::cout << "  float 3.14 : " << (j_float.is_float() ? "float" : "?")
                  << "  integral=" << (j_float.is_integral_value() ? "yes" : "no") << "\n";
        std::cout << "  float 3.0  : " << (j_int_float.is_float() ? "float" : "?")
                  << "  integral=" << (j_int_float.is_integral_value() ? "yes" : "no") << "\n";

        const bool ok =
            j_int.is_integer() && j_int.is_number() && j_int.is_scalar() && j_int.is_integral_value() &&
            j_float.is_float() && j_float.is_number() && !j_float.is_integral_value() &&
            j_int_float.is_float() && j_int_float.is_integral_value() &&
            j_str.is_string() && j_arr.is_container() && j_obj.is_container();
        check("type queries", ok);
    }


    /* =================================================================
     *  12) Accessors — value_or, value, value_at
     * ================================================================= */
    section("12) Accessors",
            "value_or<T>(def), value(key, def), value_at(ptr, def)",
            "correct defaults on missing");
    {
        knst_json cfg;
        knst_json_error e;
        const char* src = R"({"host":"localhost","port":8080,"limits":{"rate":100}})";
        knst_json::parse(src, std::strlen(src), cfg, &e);

        std::cout << "  host         : " << cfg.value("host", "?")   << "\n";
        std::cout << "  port         : " << cfg.value("port", 0)     << "\n";
        std::cout << "  missing      : " << cfg.value("missing", -1) << "\n";
        std::cout << "  /limits/rate : " << cfg.value_at("/limits/rate", 0) << "\n";

        const bool ok = std::strcmp(cfg.value("host", "?"), "localhost") == 0 &&
                        cfg.value("port", 0) == 8080 &&
                        cfg.value("missing", -1) == -1 &&
                        cfg.value_at("/limits/rate", 0) == 100;
        check("accessors", ok);
    }


    /* =================================================================
     *  13) Array ops
     * ================================================================= */
    section("13) Array ops",
            "push_back, insert, erase_at, sort_array, unique_array, append, remove_if",
            "each op changes the array correctly");
    {
        knst_json arr = knst_json::make_array();
        arr.push_back(knst_json(3));
        arr.push_back(knst_json(1));
        arr.push_back(knst_json(2));
        arr.push_back(knst_json(1));
        std::cout << "  after push      : size=" << arr.size() << "\n";

        arr.sort_array();
        std::cout << "  after sort      : ";
        for (const auto& v : arr) std::cout << v.value_or(0) << " ";
        std::cout << "\n";

        const size_t removed = arr.unique_array();
        std::cout << "  after unique    : size=" << arr.size()
                  << " (removed " << removed << ")\n";

        arr.insert(0, knst_json(99));
        std::cout << "  after insert(0) : size=" << arr.size() << "\n";

        arr.erase_at(0);
        std::cout << "  after erase(0)  : size=" << arr.size() << "\n";

        const size_t rem2 = arr.remove_if([](const knst_json& v) {
            return v.is_number() && v.value_or(0) < 2;
        });
        std::cout << "  after remove_if : size=" << arr.size()
                  << " (removed " << rem2 << ")\n";

        knst_json a2 = knst_json::make_array({ knst_json(7), knst_json(8) });
        arr.append(a2);
        std::cout << "  after append    : size=" << arr.size() << "\n";

        check("array ops", arr.size() == 4 && a2.size() == 2 && removed == 1 && rem2 == 1);
    }


    /* =================================================================
     *  14) Array iteration and operator[]
     * ================================================================= */
    section("14) Array iteration",
            "range-for, operator[](size_t), out-of-range safe",
            "no UB on invalid index");
    {
        knst_json arr = knst_json::make_array({ knst_json(10), knst_json(20), knst_json(30) });

        int64_t sum = 0;
        for (const auto& v : arr) sum += v.value_or<int64_t>(0);

        const int64_t first = arr[0].value_or<int64_t>(0);
        const int64_t oob   = arr[100].value_or<int64_t>(-1);

        std::cout << "  sum         : " << sum << "\n";
        std::cout << "  arr[0]      : " << first << "\n";
        std::cout << "  arr[100]    : " << oob << " (safe null)\n";

        check("array iteration", sum == 60 && first == 10 && oob == -1);
    }


    /* =================================================================
     *  15) Object ops
     * ================================================================= */
    section("15) Object ops",
            "set, find, contains, erase, rename_key, sort_keys, update",
            "each op changes the object correctly");
    {
        knst_json obj = knst_json::make_object();
        obj.set("zebra", knst_json(1));
        obj.set("alpha", knst_json(2));
        obj.set("mike",  knst_json(3));

        std::cout << "  size           : " << obj.size() << "\n";
        std::cout << "  contains zebra : " << obj.contains("zebra") << "\n";

        obj.rename_key("zebra", "zzz");
        std::cout << "  renamed        : zzz size=" << (obj.find("zzz") ? 1 : 0) << "\n";

        obj.sort_keys();
        std::cout << "  sorted keys    : ";
        for (const auto& m : obj.members()) { std::cout << m.key() << " "; }
        std::cout << "\n";

        obj.erase("alpha");
        std::cout << "  after erase    : size=" << obj.size() << "\n";

        knst_json other = knst_json::make_object();
        other.set("mike",  knst_json(99));
        other.set("extra", knst_json(7));
        obj.update(other);
        std::cout << "  after update   : size=" << obj.size() << "\n";
        std::cout << "  mike value     : " << obj.value("mike", 0) << "\n";

        check("object ops", obj.size() == 3 && obj.value("mike", 0) == 99 &&
              obj.value("extra", 0) == 7 && obj.find("alpha") == nullptr);
    }


    /* =================================================================
     *  16) Object iteration
     * ================================================================= */
    section("16) Object iteration",
            "members() range with key() / key_size() / .v",
            "insertion order preserved");
    {
        knst_json obj = knst_json::make_object();
        obj.set("one",   knst_json(1));
        obj.set("two",   knst_json(2));
        obj.set("three", knst_json(3));

        std::cout << "  keys in insertion order : ";
        for (const auto& m : obj.members()) std::cout << m.key() << " ";
        std::cout << "\n";

        bool ok = true;
        const char* expected[] = { "one", "two", "three" };
        uint32_t i = 0;
        for (const auto& m : obj.members()) {
            if (i >= 3 || std::strcmp(m.key(), expected[i]) != 0) { ok = false; break; }
            ++i;
        }
        check("object iteration", ok && i == 3);
    }


    /* =================================================================
     *  17) Dump — compact vs pretty
     * ================================================================= */
    section("17) Dump compact/pretty",
            "Same value with indent=0 and indent=2",
            "output differs by whitespace only");
    {
        knst_json obj = knst_json::make_object();
        obj.set("name", knst_json("knst"));
        obj.set("port", knst_json(8080));

        knst_json_buffer compact;
        knst_json_dump_options opt_compact;
        opt_compact.indent = 0;
        obj.dump(compact, opt_compact);

        knst_json_buffer pretty;
        knst_json_dump_options opt_pretty = knst_json_dump_options::pretty(2);
        obj.dump(pretty, opt_pretty);

        std::cout << "  compact : "; print_buf(compact); std::cout << "\n";
        std::cout << "  pretty  :\n"; print_buf(pretty); std::cout << "\n";

        check("compact/pretty", compact.size() < pretty.size() &&
              compact.size() > 0 && pretty.size() > 0);
    }


    /* =================================================================
     *  18) Dump — escape handling
     * ================================================================= */
    section("18) Dump escapes",
            "Control chars, quotes, backslash",
            "properly escaped in output");
    {
        knst_json j("a\nb\tc\"d\\e");
        knst_json_buffer b;
        j.dump(b);
        std::cout << "  output : "; print_buf(b); std::cout << "\n";

        bool has_n = false, has_t = false, has_quote = false, has_backslash = false;
        for (size_t i = 0; i < b.size(); ++i) {
            if (b.data()[i] == '\\' && i + 1 < b.size()) {
                const char c = b.data()[i + 1];
                if (c == 'n') has_n = true;
                if (c == 't') has_t = true;
                if (c == '"') has_quote = true;
                if (c == '\\') has_backslash = true;
            }
        }
        check("dump escapes", has_n && has_t && has_quote && has_backslash);
    }


    /* =================================================================
     *  19) Dump — ensure_ascii
     * ================================================================= */
    section("19) Dump ensure_ascii",
            "Multibyte UTF-8 escaped as \\uXXXX",
            "no raw UTF-8 bytes remain");
    {
        const char utf8_world[] = "\xE4\xB8\x96\xE7\x95\x8C";
        knst_json j(utf8_world, sizeof(utf8_world) - 1);

        knst_json_buffer b;
        knst_json_dump_options opt;
        opt.ensure_ascii = true;
        j.dump(b, opt);

        std::cout << "  output : "; print_buf(b); std::cout << "\n";

        bool all_ascii = true;
        for (size_t i = 0; i < b.size(); ++i) {
            if (static_cast<unsigned char>(b.data()[i]) >= 0x80) { all_ascii = false; break; }
        }
        check("ensure_ascii", all_ascii && b.size() > 0);
    }


    /* =================================================================
     *  20) Dump — sort_keys
     * ================================================================= */
    section("20) Dump sort_keys",
            "Insertion order zebra/alpha/mike reordered alphabetically",
            "output has keys in sorted order");
    {
        knst_json obj = knst_json::make_object();
        obj.set("zebra", knst_json(1));
        obj.set("alpha", knst_json(2));
        obj.set("mike",  knst_json(3));

        knst_json_buffer b;
        knst_json_dump_options opt;
        opt.sort_keys = true;
        obj.dump(b, opt);

        std::cout << "  output : "; print_buf(b); std::cout << "\n";

        const char* data = b.data();
        const char* p_alpha = std::strstr(data, "\"alpha\"");
        const char* p_mike  = std::strstr(data, "\"mike\"");
        const char* p_zebra = std::strstr(data, "\"zebra\"");

        const bool ok = p_alpha && p_mike && p_zebra && p_alpha < p_mike && p_mike < p_zebra;
        check("sort_keys", ok);
    }


    /* =================================================================
     *  21) Dump — canonical (RFC 8785 JCS)
     * ================================================================= */
    section("21) Dump canonical",
            "RFC 8785 JCS: no whitespace, sorted keys",
            "canonical output is deterministic");
    {
        knst_json obj = knst_json::make_object();
        obj.set("b", knst_json(2));
        obj.set("a", knst_json(1));
        obj.set("n", knst_json(1.5));

        knst_json_buffer b;
        knst_json_dump_options opt = knst_json_dump_options::canonical_form();
        obj.dump(b, opt);

        std::cout << "  output : "; print_buf(b); std::cout << "\n";

        const bool has_sorted = (b.size() > 0 && b.data()[0] == '{' &&
                                 std::strstr(b.data(), "\"a\":1") != nullptr);
        check("canonical", has_sorted);
    }


    /* =================================================================
     *  22) Round-trip — parse→dump→parse
     * ================================================================= */
    section("22) Round-trip",
            "Original == reparsed from dump",
            "identical trees");
    {
        const char* src = R"({"a":1,"b":[true,null,"x"],"c":{"d":3.14}})";
        knst_json j1, j2;
        knst_json_error e;

        const bool p1 = knst_json::parse(src, std::strlen(src), j1, &e);
        knst_json_buffer b;
        const bool d1 = j1.dump(b);
        const bool p2 = knst_json::parse(b.data(), b.size(), j2, &e);

        std::cout << "  orig  size : " << j1.size() << "\n";
        std::cout << "  dump  size : " << b.size() << " bytes\n";
        std::cout << "  reparse eq : " << (j1 == j2 ? "yes" : "no") << "\n";

        check("round-trip", p1 && d1 && p2 && j1 == j2);
    }


    /* =================================================================
     *  23) Deep copy
     * ================================================================= */
    section("23) Deep copy",
            "copy_from() produces an independent tree",
            "mutation does not propagate");
    {
        knst_json original;
        knst_json_error e;
        const char* src = R"({"a":[1,2,{"deep":true}]})";
        knst_json::parse(src, std::strlen(src), original, &e);

        knst_json copy;
        const bool ok_copy = copy.copy_from(original);

        if (knst_json* a = original.find("a")) a->push_back(knst_json(999));

        std::cout << "  original size : " << original.size() << "\n";
        std::cout << "  copy     size : " << copy.size() << "\n";
        std::cout << "  copy arr size : "
                  << (copy.find("a") ? copy.find("a")->size() : 0) << " (expected 3)\n";

        const bool ok = ok_copy && copy.find("a") && copy.find("a")->size() == 3 &&
                        original.find("a") && original.find("a")->size() == 4;
        check("deep copy", ok);
    }


    /* =================================================================
     *  24) Comparison
     * ================================================================= */
    section("24) Comparison",
            "operator==, operator!=, operator<, compare, compare_numbers",
            "consistent total order");
    {
        knst_json a, b;
        knst_json_error e;
        const char* src1 = R"({"x":1,"y":2})";
        const char* src2 = R"({"y":2,"x":1})";
        knst_json::parse(src1, std::strlen(src1), a, &e);
        knst_json::parse(src2, std::strlen(src2), b, &e);

        std::cout << "  a == b (key order independence) : " << (a == b ? "yes" : "no") << "\n";

        knst_json i1(1), i2(2), f1(1.0);
        std::cout << "  1 == 1.0     : " << (i1 == f1 ? "yes" : "no") << "\n";
        std::cout << "  1 < 2        : " << (i1 < i2 ? "yes" : "no") << "\n";
        std::cout << "  compare_num  : " << knst_json::compare_numbers(i1, f1) << "\n";

        check("comparison", a == b && i1 == f1 && i1 < i2 &&
              knst_json::compare_numbers(i1, f1) == 0);
    }


    /* =================================================================
     *  25) Hash consistency
     * ================================================================= */
    section("25) Hash consistency",
            "Structurally equal values hash the same",
            "1 == 1.0 → hash equal; object order independence");
    {
        knst_json a, b, c;
        knst_json_error e;
        const char* s1 = R"({"x":1,"y":[1,2,3]})";
        const char* s2 = R"({"y":[1,2,3],"x":1})";
        const char* s3 = R"({"x":1,"y":[1,2,4]})";
        knst_json::parse(s1, std::strlen(s1), a, &e);
        knst_json::parse(s2, std::strlen(s2), b, &e);
        knst_json::parse(s3, std::strlen(s3), c, &e);

        const uint64_t ha = a.hash(), hb = b.hash(), hc = c.hash();
        std::cout << "  hash(a) : " << ha << "\n";
        std::cout << "  hash(b) : " << hb << " (same as a)\n";
        std::cout << "  hash(c) : " << hc << " (different content)\n";

        check("hash consistency", ha == hb && ha != hc);
    }


    /* =================================================================
     *  26) JSON Pointer (RFC 6901)
     *
     *  IMPORTANT: find_pointer() returns a RAW pointer into the tree.
     *  That pointer is invalidated by ANY subsequent mutation (set /
     *  insert / erase / push_back ...) — because those may reallocate
     *  or memmove the underlying storage. Capture the VALUE you care
     *  about right away, then mutate, then re-lookup.
     * ================================================================= */
    section("26) JSON Pointer",
            "find, set, insert, remove, replace, URI fragment form",
            "every operation correct");
    {
        knst_json j;
        knst_json_error e;
        const char* src = R"({"a":{"b":[10,20,30]}})";
        knst_json::parse(src, std::strlen(src), j, &e);

        // Capture VALUES immediately — raw pointers die on mutation.
        const knst_json* found = j.find_pointer("/a/b/1");
        const int64_t found_val = found ? found->value_or<int64_t>(-1) : -1;
        std::cout << "  /a/b/1       : " << found_val << "\n";

        const knst_json* uri_found = j.find_pointer_uri("#/a/b/2");
        const int64_t uri_val = uri_found ? uri_found->value_or<int64_t>(-1) : -1;
        std::cout << "  #/a/b/2      : " << uri_val << "\n";

        j.pointer_set("/a/b/1", knst_json(999));
        std::cout << "  after set    : /a/b/1=" << j.find_pointer("/a/b/1")->value_or(0) << "\n";

        j.pointer_insert("/a/b/0", knst_json(5));
        std::cout << "  after insert : size=" << j.find_pointer("/a/b")->size() << "\n";

        j.pointer_replace("/a/b/0", knst_json(6));
        std::cout << "  after replace: /a/b/0=" << j.find_pointer("/a/b/0")->value_or(0) << "\n";

        j.pointer_remove("/a/b/0");
        std::cout << "  after remove : size=" << j.find_pointer("/a/b")->size() << "\n";

        check("json pointer",
              found_val == 20 && uri_val == 30 &&
              j.find_pointer("/a/b/1")->value_or(0) == 999 &&
              j.find_pointer("/a/b")->size() == 3);
    }


    /* =================================================================
     *  27) JSON Patch (RFC 6902)
     * ================================================================= */
    section("27) JSON Patch",
            "apply_patch (atomic), test op, make_patch (diff)",
            "patch changes tree correctly");
    {
        knst_json j;
        knst_json_error e;
        const char* src = R"({"a":1,"b":2})";
        knst_json::parse(src, std::strlen(src), j, &e);

        knst_json patch;
        const char* psrc =
            R"([{"op":"add","path":"/c","value":3},)"
            R"({"op":"replace","path":"/a","value":100},)"
            R"({"op":"remove","path":"/b"},)"
            R"({"op":"test","path":"/c","value":3}])";
        knst_json::parse(psrc, std::strlen(psrc), patch, &e);

        const bool applied = j.apply_patch(patch, &e);

        std::cout << "  after patch : a=" << j.value("a", 0)
                  << " c=" << j.value("c", 0)
                  << " has_b=" << j.contains("b") << "\n";

        knst_json src_j, dst_j, diff;
        const char* s_src = R"({"x":1})";
        const char* s_dst = R"({"x":2,"y":3})";
        knst_json::parse(s_src, std::strlen(s_src), src_j, &e);
        knst_json::parse(s_dst, std::strlen(s_dst), dst_j, &e);
        const bool made = knst_json::make_patch(src_j, dst_j, diff);

        std::cout << "  diff ops    : " << diff.size() << "\n";

        check("json patch", applied && j.value("a", 0) == 100 &&
              j.value("c", 0) == 3 && !j.contains("b") && made && diff.size() == 2);
    }


    /* =================================================================
     *  28) JSON Merge Patch (RFC 7386)
     * ================================================================= */
    section("28) JSON Merge Patch",
            "apply + atomic + make",
            "merge semantics correct");
    {
        knst_json doc;
        knst_json_error e;
        const char* dsrc = R"({"a":1,"b":2,"c":{"d":3}})";
        knst_json::parse(dsrc, std::strlen(dsrc), doc, &e);

        knst_json patch;
        const char* psrc = R"({"b":null,"c":{"e":4}})";
        knst_json::parse(psrc, std::strlen(psrc), patch, &e);

        const bool merged = doc.merge_patch(patch);

        std::cout << "  b removed   : " << !doc.contains("b") << "\n";
        std::cout << "  c has d     : " << (doc.find_pointer("/c/d") ? "yes" : "no") << "\n";
        std::cout << "  c has e     : " << (doc.find_pointer("/c/e") ? "yes" : "no") << "\n";

        knst_json from, to, mp;
        const char* fsrc = R"({"x":1})";
        const char* tsrc = R"({"x":1,"y":2})";
        knst_json::parse(fsrc, std::strlen(fsrc), from, &e);
        knst_json::parse(tsrc, std::strlen(tsrc), to, &e);
        knst_json::make_merge_patch(from, to, mp);

        std::cout << "  merge patch : ";
        knst_json_buffer b; mp.dump(b);
        print_buf(b); std::cout << "\n";

        check("merge patch", merged && !doc.contains("b") &&
              doc.find_pointer("/c/d") != nullptr && doc.find_pointer("/c/e") != nullptr);
    }


    /* =================================================================
     *  29) Walk
     * ================================================================= */
    section("29) Walk",
            "Visit every node with its JSON Pointer path",
            "5 nodes: root, 2 members, 2 values");
    {
        knst_json j;
        knst_json_error e;
        const char* src = R"({"a":1,"b":[10,20]})";
        knst_json::parse(src, std::strlen(src), j, &e);

        int count = 0;
        j.walk([&](const knst_json& v, const char* ptr, size_t len) {
            ++count;
            std::cout << "  ";
            if (len == 0) std::cout << "(root)";
            else print_cstr(ptr, len);
            std::cout << " = " << v.type_name() << "\n";
            return true;
        });

        check("walk", count == 5);
    }


    /* =================================================================
     *  30) Flatten / unflatten
     * ================================================================= */
    section("30) Flatten/Unflatten",
            "Nested to flat pointer map and back",
            "lossless round-trip");
    {
        knst_json j;
        knst_json_error e;
        const char* src = R"({"a":{"b":1},"c":[10,20]})";
        knst_json::parse(src, std::strlen(src), j, &e);

        knst_json flat;
        const bool ok_flat = j.flatten(flat);

        std::cout << "  flat keys: ";
        for (const auto& m : flat.members()) std::cout << m.key() << " ";
        std::cout << "\n";

        knst_json unflat;
        const bool ok_un = knst_json::unflatten(flat, unflat);

        std::cout << "  unflat == orig : " << (unflat == j ? "yes" : "no") << "\n";

        check("flatten", ok_flat && ok_un && unflat == j);
    }


    /* =================================================================
     *  31) JSONPath select
     * ================================================================= */
    section("31) JSONPath select",
            "Wildcard, array index, filter predicate, recursive descent",
            "correct node set from each path");
    {
        const char* src =
            "{\"store\":{\"book\":["
            "{\"title\":\"A\",\"price\":10},"
            "{\"title\":\"B\",\"price\":20},"
            "{\"title\":\"C\",\"price\":30}"
            "]}}";

        knst_json j;
        knst_json_error e;
        knst_json::parse(src, std::strlen(src), j, &e);

        knst_json_ptr_list titles;
        const bool s1 = j.select("$.store.book[*].title", titles, &e);
        std::cout << "  book[*].title : " << titles.size() << " results\n";
        for (const knst_json* p : titles) std::cout << "    " << p->c_str() << "\n";

        knst_json_ptr_list cheap;
        const bool s2 = j.select("$.store.book[?(@.price < 25)].title", cheap, &e);
        std::cout << "  price<25      : " << cheap.size() << " results\n";
        for (const knst_json* p : cheap) std::cout << "    " << p->c_str() << "\n";

        knst_json_ptr_list idx;
        const bool s3 = j.select("$.store.book[1].title", idx, &e);
        std::cout << "  book[1]       : "
                  << (idx.size() ? idx.data()[0]->c_str() : "?") << "\n";

        knst_json_ptr_list deep;
        const bool s4 = j.select("$..title", deep, &e);
        std::cout << "  $..title      : " << deep.size() << " results\n";

        check("jsonpath",
              s1 && titles.size() == 3 &&
              s2 && cheap.size() == 2 &&
              s3 && idx.size() == 1 &&
              idx.size() > 0 && std::strcmp(idx.data()[0]->c_str(), "B") == 0 &&
              s4 && deep.size() == 3);
    }


    /* =================================================================
     *  32) JSON Schema validation
     * ================================================================= */
    section("32) JSON Schema",
            "draft-07 subset: type, required, properties, minimum",
            "validation catches violations with keyword reported");
    {
        const char* schema_src =
            "{\"type\":\"object\","
            "\"required\":[\"name\",\"age\"],"
            "\"properties\":{"
            "\"name\":{\"type\":\"string\"},"
            "\"age\":{\"type\":\"integer\",\"minimum\":0}"
            "}}";

        knst_json schema;
        knst_json_error e;
        knst_json::parse(schema_src, std::strlen(schema_src), schema, &e);

        const char* v_src = "{\"name\":\"knst\",\"age\":42}";
        knst_json valid;
        knst_json::parse(v_src, std::strlen(v_src), valid, &e);
        knst_json_validation vr;
        const bool ok_valid = valid.validate(schema, &vr);

        const char* m_src = "{\"name\":\"knst\"}";
        knst_json missing;
        knst_json::parse(m_src, std::strlen(m_src), missing, &e);
        knst_json_validation mr;
        const bool bad_missing = missing.validate(schema, &mr);

        const char* w_src = "{\"name\":\"knst\",\"age\":\"old\"}";
        knst_json wrong;
        knst_json::parse(w_src, std::strlen(w_src), wrong, &e);
        knst_json_validation wr;
        const bool bad_wrong = wrong.validate(schema, &wr);

        const char* n_src = "{\"name\":\"knst\",\"age\":-1}";
        knst_json neg;
        knst_json::parse(n_src, std::strlen(n_src), neg, &e);
        knst_json_validation nr;
        const bool bad_min = neg.validate(schema, &nr);

        std::cout << "  valid       : " << (ok_valid ? "yes" : "no") << "\n";
        std::cout << "  missing     : " << (bad_missing ? "yes" : "no")
                  << "  kw='" << mr.keyword << "'\n";
        std::cout << "  wrong type  : " << (bad_wrong ? "yes" : "no")
                  << "  kw='" << wr.keyword << "'\n";
        std::cout << "  below min   : " << (bad_min ? "yes" : "no")
                  << "  kw='" << nr.keyword << "'\n";

        check("schema", ok_valid && !bad_missing && !bad_wrong && !bad_min);
    }


    /* =================================================================
     *  33) File save/load
     * ================================================================= */
    section("33) File I/O",
            "save_file_utf8 (atomic) + load_file_utf8 round-trip",
            "reloaded document identical");
    {
       #if defined(_WIN32)
        const char* tmpdir = std::getenv("TEMP");
        if (!tmpdir) tmpdir = std::getenv("TMP");
        if (!tmpdir) tmpdir = ".";
        char path_buf[512];
        std::snprintf(path_buf, sizeof(path_buf), "%s\\knst_json_test_demo.json", tmpdir);
        const char* path = path_buf;
#else
        const char* path = "/tmp/knst_json_test_demo.json";
#endif

        knst_json src;
        knst_json_error e;
        const char* s = "{\"msg\":\"hello\",\"n\":42}";
        knst_json::parse(s, std::strlen(s), src, &e);

        knst_json_dump_options opt = knst_json_dump_options::pretty(2);
        const bool saved = src.save_file_utf8(path, opt, true, &e);

        knst_json loaded;
        const bool loaded_ok = knst_json::load_file_utf8(path, loaded, &e);

        std::cout << "  saved  : " << (saved ? "yes" : "no") << "\n";
        std::cout << "  loaded : " << (loaded_ok ? "yes" : "no") << "\n";
        std::cout << "  msg    : " << loaded.value("msg", "") << "\n";
        std::cout << "  n      : " << loaded.value("n", 0) << "\n";

        std::remove(path);

        check("file round-trip", saved && loaded_ok && loaded == src);
    }


    /* =================================================================
     *  34) Streaming dump
     * ================================================================= */
    section("34) Streaming dump",
            "dump_stream with sink callback produces same bytes",
            "callback invoked, output identical to buffered dump");
    {
        struct Ctx {
            knst_json_buffer* buf;
            size_t chunks = 0;
            size_t total  = 0;
        };

        knst_json j = knst_json::make_object();
        j.set("name", knst_json("knst"));
        for (int i = 0; i < 50; ++i) {
            char key[16];
            std::snprintf(key, sizeof(key), "k%d", i);
            j.set(key, knst_json(i));
        }

        knst_json_buffer assembled;
        Ctx ctx{ &assembled, 0, 0 };

        auto sink = +[](void* u, const char* data, size_t len) -> bool {
            auto* c = static_cast<Ctx*>(u);
            if (!c->buf->append(data, len)) return false;
            ++c->chunks;
            c->total += len;
            return true;
        };

        knst_json_dump_options opt;
        knst_json_error e;
        const bool ok = j.dump_stream(sink, &ctx, opt, &e);

        knst_json_buffer ref;
        j.dump(ref, opt);
        const bool match = (assembled.size() == ref.size() &&
                            (ref.size() == 0 ||
                             std::memcmp(assembled.data(), ref.data(), ref.size()) == 0));

        std::cout << "  chunks : " << ctx.chunks << "\n";
        std::cout << "  bytes  : " << ctx.total  << "\n";
        std::cout << "  match  : " << (match ? "yes" : "no") << "\n";

        check("streaming dump", ok && ctx.chunks >= 1 && match);
    }


    /* =================================================================
     *  35) SAX parse
     * ================================================================= */
    section("35) SAX parse",
            "Handler counts events without building a tree",
            "counts match expected structure");
    {
        struct Counter : public knst_json_sax_base {
            int nulls = 0, bools = 0, ints = 0, floats = 0;
            int strings = 0, objs = 0, arrs = 0, keys = 0;
            bool null_value()          { ++nulls;   return true; }
            bool boolean(bool)         { ++bools;   return true; }
            bool integer(int64_t)      { ++ints;    return true; }
            bool uinteger(uint64_t)    { ++ints;    return true; }
            bool number_float(double)  { ++floats;  return true; }
            bool string(const char*, size_t) { ++strings; return true; }
            bool key(const char*, size_t)    { ++keys;    return true; }
            bool start_object()        { ++objs;    return true; }
            bool start_array()         { ++arrs;    return true; }
        };

        const char* src = "{\"a\":1,\"b\":[true,null],\"c\":\"hi\"}";
        Counter c;
        knst_json_error e;
        const bool ok = knst_json::parse_sax(src, std::strlen(src), c, &e);

        std::cout << "  nulls="   << c.nulls   << " bools="   << c.bools
                  << " ints="     << c.ints    << " floats=" << c.floats
                  << " strings=" << c.strings << " objs="   << c.objs
                  << " arrs="    << c.arrs    << " keys="   << c.keys << "\n";

        check("sax", ok &&
              c.nulls == 1 && c.bools == 1 && c.ints == 1 && c.strings == 1 &&
              c.floats == 0 && c.objs == 1 && c.arrs == 1 && c.keys == 3);
    }


    /* =================================================================
     *  36) NDJSON (JSON Lines)
     * ================================================================= */
    section("36) NDJSON",
            "parse_lines + dump_lines",
            "3 lines each direction, values correct");
    {
        const char* ndjson =
            "{\"i\":1}\n"
            "{\"i\":2}\n"
            "{\"i\":3}\n";

        int count = 0;
        int64_t sum = 0;
        knst_json_error e;
        const bool p_ok = knst_json::parse_lines(
            ndjson, std::strlen(ndjson),
            [&](size_t idx, knst_json&& v) -> bool {
                ++count;
                sum += v.value("i", 0);
                std::cout << "  parse line " << idx
                          << " : i=" << v.value("i", 0) << "\n";
                return true;
            }, &e);

        knst_json arr = knst_json::make_array();
        for (int i = 10; i <= 12; ++i) {
            knst_json o = knst_json::make_object();
            o.set("i", knst_json(i));
            arr.push_back(std::move(o));
        }

        knst_json_buffer out;
        knst_json_dump_options opt;
        const bool d_ok = arr.dump_lines(out, opt, &e);

        std::cout << "  dumped:\n";
        print_buf(out);

        int nl_count = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (out.data()[i] == '\n') ++nl_count;

        check("ndjson", p_ok && count == 3 && sum == 6 && d_ok && nl_count == 3);
    }


    /* =================================================================
     *  37) Stats / clear / shrink
     * ================================================================= */
    section("37) Stats and cleanup",
            "count_nodes, clear, shrink_to_fit, append_string",
            "each behaves correctly");
    {
        const char* src = "{\"a\":[1,2,3],\"b\":{\"c\":true}}";
        knst_json j;
        knst_json_error e;
        knst_json::parse(src, std::strlen(src), j, &e);

        const size_t cnt = j.count_nodes();
        std::cout << "  count_nodes : " << cnt << "\n";

        j.clear();
        std::cout << "  after clear: size=" << j.size() << "\n";

        knst_json s("hello");
        s.append_string(" world", 6);
        std::cout << "  string      : " << s.c_str() << "\n";
        s.shrink_to_fit();
        std::cout << "  after shrink: " << s.c_str()
                  << "  len=" << s.length() << "\n";

        check("stats", cnt == 7 && j.size() == 0 &&
              std::strcmp(s.c_str(), "hello world") == 0 && s.length() == 11);
    }


    /* =================================================================
     *  38) Integration — realistic config build+serialize
     * ================================================================= */
    section("38) Integration",
            "Build realistic config: object + array, serialize, reparse",
            "identical round-trip, pointer reads work");
    {
        knst_json cfg = knst_json::make_object();
        cfg.set("app",     knst_json("KernelNucleusT"));
        cfg.set("version", knst_json("2.0"));
        cfg.set("enabled", knst_json(true));

        knst_json servers = knst_json::make_array();
        for (int i = 1; i <= 3; ++i) {
            knst_json s = knst_json::make_object();
            s.set("id",   knst_json(i));
            s.set("host", knst_json("10.0.0.1"));
            s.set("port", knst_json(8000 + i));
            servers.push_back(std::move(s));
        }
        cfg.set("servers", std::move(servers));

        knst_json_buffer out;
        knst_json_dump_options opt = knst_json_dump_options::pretty(2);
        knst_json_error e;
        const bool dumped = cfg.dump(out, opt, &e);

        std::cout << "  serialized " << out.size() << " bytes\n";
        std::cout << "  preview: "; print_buf(out, 100); std::cout << "\n";

        knst_json re;
        const bool reparsed = knst_json::parse(out.data(), out.size(), re, &e);

        std::cout << "  /servers/0/port : "
                  << re.value_at("/servers/0/port", 0) << "\n";
        std::cout << "  /servers/2/id   : "
                  << re.value_at("/servers/2/id", 0) << "\n";

        check("integration", dumped && reparsed && re == cfg &&
              re.value_at("/servers/0/port", 0) == 8001 &&
              re.value_at("/servers/2/id", 0)   == 3);
    }


    /* =================================================================
     *  Summary
     * ================================================================= */
    std::cout << "\n==================================================\n";
    std::cout << "  RESULT: " << s_pass << " PASS / " << s_fail << " FAIL";
    if (s_skip) std::cout << " / " << s_skip << " SKIP";
    std::cout << "\n==================================================\n";

    if (s_fail > 0) {
        std::cout << "\nSome checks failed.\n";
    }

    std::cout << "\nDone.\n";
    return s_fail == 0 ? 0 : 1;
}