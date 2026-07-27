#include <catch2/catch_test_macros.hpp>

#if !defined(_WIN32)
#include <csignal>
#include <sys/resource.h>
#endif

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "text_document_writer.hpp"
#include "domain/field.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"

#include "support/temp_files.hpp"

using mondoc::FieldId;
using mondoc::TemplateId;
using mondoc::adapters::formats::TextDocumentWriter;
using mondoc::domain::Field;
using mondoc::domain::FieldType;
using mondoc::domain::Fill;
using mondoc::domain::Template;

namespace {

std::filesystem::path uniqueTempPath(const std::string& ext) {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_textw_", ext);
}

using mondoc::tests_support::TempFile;
using mondoc::tests_support::writeFile;
using mondoc::tests_support::readFile;

Template makeTpl(const std::filesystem::path& src,
                 std::vector<Field> fields,
                 const std::string& fmt = "txt") {
    Template t;
    t.id_ = TemplateId{"t1"};
    t.name_ = "test";
    t.source_format_ = fmt;
    t.fields_ = std::move(fields);
    t.source_path_ = src;
    return t;
}

}  // namespace

TEST_CASE("TextDocumentWriter: substitutes {{name}} pattern",
          "[formats.text_writer]") {
    TempFile src{uniqueTempPath(".txt")};
    TempFile dst{uniqueTempPath(".txt")};
    writeFile(src.path, "Hello {{first_name}}!");

    auto tpl = makeTpl(src.path,
                       {Field{FieldId{"f1"}, "first_name", FieldType::Text}});
    std::vector<Fill> fills{Fill{FieldId{"f1"}, "Jane", {}}};

    auto r = TextDocumentWriter{}.write(tpl, fills, dst.path);
    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) == "Hello Jane!");
}

TEST_CASE("TextDocumentWriter: substitutes [NAME] pattern",
          "[formats.text_writer]") {
    TempFile src{uniqueTempPath(".txt")};
    TempFile dst{uniqueTempPath(".txt")};
    writeFile(src.path, "Signed [DATE_SIGNED].");

    auto tpl = makeTpl(src.path,
                       {Field{FieldId{"f1"}, "date_signed", FieldType::Text}});
    std::vector<Fill> fills{Fill{FieldId{"f1"}, "2026-05-07", {}}};

    auto r = TextDocumentWriter{}.write(tpl, fills, dst.path);
    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) == "Signed 2026-05-07.");
}

TEST_CASE("TextDocumentWriter: substitutes <NAME> pattern",
          "[formats.text_writer]") {
    TempFile src{uniqueTempPath(".txt")};
    TempFile dst{uniqueTempPath(".txt")};
    writeFile(src.path, "From <COUNTRY>.");

    auto tpl = makeTpl(src.path,
                       {Field{FieldId{"f1"}, "country", FieldType::Text}});
    std::vector<Fill> fills{Fill{FieldId{"f1"}, "France", {}}};

    auto r = TextDocumentWriter{}.write(tpl, fills, dst.path);
    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) == "From France.");
}

TEST_CASE("TextDocumentWriter: unfilled placeholders are kept verbatim",
          "[formats.text_writer]") {
    TempFile src{uniqueTempPath(".txt")};
    TempFile dst{uniqueTempPath(".txt")};
    writeFile(src.path, "Hello {{first_name}}, signed [DATE_SIGNED].");

    auto tpl = makeTpl(src.path,
                       {Field{FieldId{"f1"}, "first_name", FieldType::Text},
                        Field{FieldId{"f2"}, "date_signed", FieldType::Text}});
    std::vector<Fill> fills{Fill{FieldId{"f1"}, "Jane", {}}};

    auto r = TextDocumentWriter{}.write(tpl, fills, dst.path);
    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) == "Hello Jane, signed [DATE_SIGNED].");
}

TEST_CASE("TextDocumentWriter: substitutes mixed patterns in one template",
          "[formats.text_writer]") {
    TempFile src{uniqueTempPath(".txt")};
    TempFile dst{uniqueTempPath(".txt")};
    writeFile(src.path, "{{first_name}} from <COUNTRY> on [DATE_SIGNED].");

    auto tpl = makeTpl(src.path,
                       {Field{FieldId{"f1"}, "first_name", FieldType::Text},
                        Field{FieldId{"f2"}, "country", FieldType::Text},
                        Field{FieldId{"f3"}, "date_signed", FieldType::Text}});
    std::vector<Fill> fills{
        Fill{FieldId{"f1"}, "Jane", {}},
        Fill{FieldId{"f2"}, "France", {}},
        Fill{FieldId{"f3"}, "2026-05-07", {}},
    };

    auto r = TextDocumentWriter{}.write(tpl, fills, dst.path);
    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) ==
            "Jane from France on 2026-05-07.");
}

TEST_CASE("TextDocumentWriter: substitution never re-scans inserted values",
          "[formats.text_writer]") {
    TempFile src{uniqueTempPath(".txt")};
    TempFile dst{uniqueTempPath(".txt")};
    writeFile(src.path, "Name: {{name}} Note: [note]");

    auto tpl = makeTpl(src.path,
                       {Field{FieldId{"f1"}, "name", FieldType::Text},
                        Field{FieldId{"f2"}, "note", FieldType::Text}});
    std::vector<Fill> fills{
        Fill{FieldId{"f1"}, "John [note] Smith", {}},
        Fill{FieldId{"f2"}, "SECRET", {}},
    };

    auto r = TextDocumentWriter{}.write(tpl, fills, dst.path);
    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) == "Name: John [note] Smith Note: SECRET");
}

#if !defined(_WIN32)
TEST_CASE("TextDocumentWriter: removes dest and reports failure when the write cannot be flushed",
          "[formats.text_writer]") {
    TempFile src{uniqueTempPath(".txt")};
    TempFile dst{uniqueTempPath(".txt")};
    writeFile(src.path, "Hello {{first_name}}!");

    auto tpl = makeTpl(src.path,
                       {Field{FieldId{"f1"}, "first_name", FieldType::Text}});
    std::vector<Fill> fills{Fill{FieldId{"f1"}, "Jane", {}}};

    // Force any write beyond the file-size limit to fail with EFBIG instead
    // of the default action of killing the process with SIGXFSZ.
    auto oldHandler = ::signal(SIGXFSZ, SIG_IGN);
    struct rlimit oldLimit {};
    ::getrlimit(RLIMIT_FSIZE, &oldLimit);
    struct rlimit rl {0, oldLimit.rlim_max};
    ::setrlimit(RLIMIT_FSIZE, &rl);

    auto r = TextDocumentWriter{}.write(tpl, fills, dst.path);

    ::setrlimit(RLIMIT_FSIZE, &oldLimit);
    ::signal(SIGXFSZ, oldHandler);

    REQUIRE_FALSE(r.has_value());
    REQUIRE_FALSE(std::filesystem::exists(dst.path));
}
#endif

TEST_CASE("TextDocumentWriter: .md template substitution byte-matches .txt",
          "[formats.text_writer]") {
    const std::string body =
        "# Hello {{first_name}}\n\nSigned [DATE_SIGNED].";

    TempFile srcTxt{uniqueTempPath(".txt")};
    TempFile dstTxt{uniqueTempPath(".txt")};
    writeFile(srcTxt.path, body);
    auto tplTxt = makeTpl(srcTxt.path,
                          {Field{FieldId{"f1"}, "first_name", FieldType::Text},
                           Field{FieldId{"f2"}, "date_signed", FieldType::Text}},
                          "txt");
    std::vector<Fill> fills{
        Fill{FieldId{"f1"}, "Jane", {}},
        Fill{FieldId{"f2"}, "2026-05-07", {}},
    };
    REQUIRE(TextDocumentWriter{}.write(tplTxt, fills, dstTxt.path).has_value());
    const std::string resultTxt = readFile(dstTxt.path);

    TempFile srcMd{uniqueTempPath(".md")};
    TempFile dstMd{uniqueTempPath(".md")};
    writeFile(srcMd.path, body);
    auto tplMd = makeTpl(srcMd.path,
                         {Field{FieldId{"f1"}, "first_name", FieldType::Text},
                          Field{FieldId{"f2"}, "date_signed", FieldType::Text}},
                         "md");
    REQUIRE(TextDocumentWriter{}.write(tplMd, fills, dstMd.path).has_value());
    const std::string resultMd = readFile(dstMd.path);

    REQUIRE(resultTxt == resultMd);
    REQUIRE(resultTxt == "# Hello Jane\n\nSigned 2026-05-07.");
}
