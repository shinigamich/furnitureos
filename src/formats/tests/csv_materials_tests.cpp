// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/csv_materials.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace fos;
using namespace fos::formats;
using core::ErrorCode;
using ::testing::HasSubstr;

namespace {
CsvMaterialImportOptions numberedIds() {
    CsvMaterialImportOptions options;
    auto counter = std::make_shared<int>(0);
    options.newId = [counter] { return "csv-" + std::to_string(++*counter); };
    return options;
}
} // namespace

TEST(CsvMaterials, SemicolonSeparatedWithDecimalCommas) {
    const char* text = "name;thickness;sheetLength;sheetWidth;kind;costPerSheet;hasGrain\n"
                       "Egger H1180 Oak;18,3;2800;2070;particleboard;61,99;yes\n"
                       "White HDF;3;2800,5;2070;hdf;15;no\n";
    auto r = importMaterialsCsv(text, numberedIds());
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().separator, ';');
    ASSERT_EQ(r.value().materials.size(), 2u);
    const domain::Material& oak = r.value().materials[0];
    EXPECT_EQ(oak.id.str(), "csv-1");
    EXPECT_EQ(oak.name, "Egger H1180 Oak");
    EXPECT_EQ(oak.thickness, 18.3);
    EXPECT_EQ(oak.sheetLength, 2800.0);
    EXPECT_EQ(oak.sheetWidth, 2070.0);
    EXPECT_EQ(oak.kind, domain::MaterialKind::Particleboard);
    EXPECT_EQ(oak.costPerSheet, 61.99);
    EXPECT_TRUE(oak.hasGrain);
    const domain::Material& hdf = r.value().materials[1];
    EXPECT_EQ(hdf.id.str(), "csv-2");
    EXPECT_EQ(hdf.thickness, 3.0);
    EXPECT_EQ(hdf.sheetLength, 2800.5);
    EXPECT_EQ(hdf.kind, domain::MaterialKind::Hdf);
    EXPECT_EQ(hdf.costPerSheet, 15.0);
    EXPECT_FALSE(hdf.hasGrain);
}

TEST(CsvMaterials, CommaSeparatedWithDecimalPointsAndQuotedFields) {
    const char* text = "name,thickness,sheetLength,sheetWidth,kind,costPerSheet,hasGrain\n"
                       "\"Plywood, birch \"\"BB\"\"\",12.5,2500,1250,plywood,48.20,TRUE\n"
                       "MDF raw,\"19,2\",2800,2070,mdf,\"33,5\",0\n";
    auto r = importMaterialsCsv(text, numberedIds());
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().separator, ',');
    ASSERT_EQ(r.value().materials.size(), 2u);
    EXPECT_EQ(r.value().materials[0].name, "Plywood, birch \"BB\""); // separator and quotes inside quotes
    EXPECT_EQ(r.value().materials[0].thickness, 12.5);
    EXPECT_EQ(r.value().materials[0].costPerSheet, 48.2);
    EXPECT_TRUE(r.value().materials[0].hasGrain);
    // A decimal comma is fine in a comma-separated file when the field is quoted.
    EXPECT_EQ(r.value().materials[1].thickness, 19.2);
    EXPECT_EQ(r.value().materials[1].costPerSheet, 33.5);
    EXPECT_FALSE(r.value().materials[1].hasGrain);
}

TEST(CsvMaterials, UnquotedDecimalCommaInCommaFileIsARowError) {
    const char* text = "name,thickness\n"
                       "Board A,18\n"
                       "Board B,18,5\n";
    auto r = importMaterialsCsv(text);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ParseError);
    EXPECT_THAT(r.error().message, HasSubstr("row 3: expected 2 fields but found 3 (separator ',')"));
    EXPECT_THAT(r.error().message, HasSubstr("a number with a decimal comma must be quoted, or use ';'"));
    EXPECT_THAT(r.error().message, HasSubstr("nothing was imported"));
}

TEST(CsvMaterials, HeaderMappingIsCaseInsensitiveOrderFreeAndKeepsExtraColumns) {
    const char* text = "\xEF\xBB\xBF" // UTF-8 byte order mark, as written by spreadsheet programs
                       "Decor; THICKNESS ;Name;ID;Supplier;Code;Manufacturer;Notes\r\n"
                       "H1180;18;Oak;mat-1;Wood Ltd;H1180 ST37;Egger;\r\n"
                       "\r\n"
                       "W980;16;White;;;;;discontinued\r\n";
    auto r = importMaterialsCsv(text, numberedIds());
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().materials.size(), 2u);
    const domain::Material& oak = r.value().materials[0];
    EXPECT_EQ(oak.id.str(), "mat-1"); // id column used when filled
    EXPECT_EQ(oak.name, "Oak");
    EXPECT_EQ(oak.thickness, 18.0);
    EXPECT_EQ(oak.supplier, "Wood Ltd");
    EXPECT_EQ(oak.code, "H1180 ST37");
    EXPECT_EQ(oak.manufacturer, "Egger");
    EXPECT_EQ(oak.custom, (std::map<std::string, std::string>{{"Decor", "H1180"}})); // empty "Notes" skipped
    // Documented defaults for absent / empty cells.
    EXPECT_EQ(oak.sheetLength, 2800.0);
    EXPECT_EQ(oak.sheetWidth, 2070.0);
    EXPECT_EQ(oak.kind, domain::MaterialKind::Other);
    EXPECT_EQ(oak.costPerSheet, 0.0);
    EXPECT_FALSE(oak.hasGrain);
    const domain::Material& white = r.value().materials[1];
    EXPECT_EQ(white.id.str(), "csv-1"); // generated: the id cell is empty
    EXPECT_EQ(white.custom, (std::map<std::string, std::string>{{"Decor", "W980"}, {"Notes", "discontinued"}}));
}

TEST(CsvMaterials, RowErrorsCarryFileLineNumbersAndAreAllListed) {
    const char* text = "name;thickness;kind;hasGrain;costPerSheet;sheetLength\n" // line 1
                       "Good;18;mdf;yes;10;2800\n"                              // line 2
                       "\n"                                                     // line 3 (blank, skipped)
                       "Bad thickness;abc;mdf;no;10;2800\n"                     // line 4
                       ";18;mdf;no;10;2800\n"                                   // line 5: no name
                       "Bad kind;18;chipboard;no;10;2800\n"                     // line 6
                       "Bad grain;18;mdf;maybe;10;2800\n"                       // line 7
                       "Negative;-18;mdf;no;-1;0\n"                             // line 8: three problems
                       "Thousands;1.800,5;mdf;no;10;2 800\n"                    // line 9: two problems
                       "Short;18;mdf\n"                                         // line 10
                       "Quote;18;\"mdf;no;10;2800\n";                           // line 11
    auto r = importMaterialsCsv(text);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ParseError);
    const std::string& m = r.error().message;
    EXPECT_THAT(m, HasSubstr("Material CSV has 11 problems; nothing was imported:"));
    EXPECT_THAT(m, HasSubstr("\nrow 4, column 'thickness': expected a positive number, got 'abc'."));
    EXPECT_THAT(m, HasSubstr("\nrow 5, column 'name': expected a material name, got ''."));
    EXPECT_THAT(m, HasSubstr("\nrow 6, column 'kind': expected one of \"particleboard\", \"mdf\", \"hdf\", \"plywood\", "
                             "\"solid_wood\", \"other\", got 'chipboard'."));
    EXPECT_THAT(m, HasSubstr("\nrow 7, column 'hasGrain': expected true/false, yes/no or 1/0, got 'maybe'."));
    EXPECT_THAT(m, HasSubstr("\nrow 8, column 'thickness': expected a positive number, got '-18'."));
    EXPECT_THAT(m, HasSubstr("\nrow 8, column 'costPerSheet': expected a non-negative number, got '-1'."));
    EXPECT_THAT(m, HasSubstr("\nrow 8, column 'sheetLength': expected a positive number, got '0'."));
    EXPECT_THAT(m, HasSubstr("\nrow 9, column 'thickness': expected a positive number, got '1.800,5'."));
    EXPECT_THAT(m, HasSubstr("\nrow 9, column 'sheetLength': expected a positive number, got '2 800'."));
    EXPECT_THAT(m, HasSubstr("\nrow 10: expected 6 fields but found 3 (separator ';')."));
    EXPECT_THAT(m, HasSubstr("\nrow 11: unterminated or malformed quoted field."));
    EXPECT_THAT(m, ::testing::Not(HasSubstr("row 2")));
    EXPECT_THAT(m, ::testing::Not(HasSubstr("row 3")));
}

TEST(CsvMaterials, HeaderProblems) {
    EXPECT_THAT(importMaterialsCsv("").error().message, HasSubstr("is empty"));
    EXPECT_THAT(importMaterialsCsv("\n  \n").error().message, HasSubstr("is empty"));
    EXPECT_THAT(importMaterialsCsv("name;kind\nA;mdf\n").error().message,
                HasSubstr("row 1 (header): the required column 'thickness' is missing (separator ';'; columns found: 2)"));
    EXPECT_THAT(importMaterialsCsv("thickness\n18\n").error().message, HasSubstr("required column 'name' is missing"));
    EXPECT_THAT(importMaterialsCsv("name;thickness;Name\nA;18;B\n").error().message,
                HasSubstr("column 'Name' appears more than once"));
    EXPECT_THAT(importMaterialsCsv("name;;thickness\nA;;18\n").error().message, HasSubstr("column 2 has no name"));
    // A tab-separated file is not supported: the header is one unknown column.
    EXPECT_EQ(importMaterialsCsv("name\tthickness\nA\t18\n").error().code, ErrorCode::ParseError);
    // The header row keeps its real line number when blank lines precede it.
    EXPECT_THAT(importMaterialsCsv("\n\nname\nA\n").error().message, HasSubstr("row 3 (header)"));
}

TEST(CsvMaterials, HeaderOnlyFileImportsNothing) {
    auto r = importMaterialsCsv("name;thickness\n");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().materials.empty());
}

TEST(CsvMaterials, SeparatorDetectionAndOverride) {
    // A semicolon in the header selects ';' even though commas are present too.
    auto semi = importMaterialsCsv("name;thickness\n\"Board, white\";16,5\n");
    ASSERT_TRUE(semi.ok()) << semi.error().message;
    EXPECT_EQ(semi.value().separator, ';');
    EXPECT_EQ(semi.value().materials[0].name, "Board, white");
    EXPECT_EQ(semi.value().materials[0].thickness, 16.5);

    // Forced ',' on a ';' file: the header is a single unknown column.
    CsvMaterialImportOptions forced;
    forced.separator = ',';
    EXPECT_THAT(importMaterialsCsv("name;thickness\nA;18\n", forced).error().message,
                HasSubstr("required column 'name' is missing (separator ','"));
    forced.separator = '\t';
    EXPECT_EQ(importMaterialsCsv("name;thickness\nA;18\n", forced).error().code, ErrorCode::InvalidArgument);
}

TEST(CsvMaterials, KindAndGrainSpellings) {
    const char* text = "name;thickness;kind;hasGrain\n"
                       "a;18;Solid Wood;Yes\n"
                       "b;18;solid-wood;1\n"
                       "c;18;MDF;False\n"
                       "d;18;;\n";
    auto r = importMaterialsCsv(text, numberedIds());
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().materials[0].kind, domain::MaterialKind::SolidWood);
    EXPECT_TRUE(r.value().materials[0].hasGrain);
    EXPECT_EQ(r.value().materials[1].kind, domain::MaterialKind::SolidWood);
    EXPECT_TRUE(r.value().materials[1].hasGrain);
    EXPECT_EQ(r.value().materials[2].kind, domain::MaterialKind::Mdf);
    EXPECT_FALSE(r.value().materials[2].hasGrain);
    EXPECT_EQ(r.value().materials[3].kind, domain::MaterialKind::Other);
    EXPECT_FALSE(r.value().materials[3].hasGrain);
}

TEST(CsvMaterials, IdsAreUniqueAcrossGivenAndGeneratedOnes) {
    auto duplicate = importMaterialsCsv("id;name;thickness\nx;A;18\ny;B;18\nx;C;18\n");
    ASSERT_FALSE(duplicate.ok());
    EXPECT_THAT(duplicate.error().message, HasSubstr("row 4, column 'id': the id 'x' is already used in row 2."));

    // The generator proposes "x" (taken by row 2) first; the importer asks again.
    CsvMaterialImportOptions options;
    auto calls = std::make_shared<int>(0);
    options.newId = [calls] { return ++*calls == 1 ? std::string("x") : "gen-" + std::to_string(*calls); };
    auto r = importMaterialsCsv("id;name;thickness\nx;A;18\n;B;18\n", options);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().materials[0].id.str(), "x");
    EXPECT_EQ(r.value().materials[1].id.str(), "gen-2");

    // Default generator: UUIDs, all different.
    auto uuids = importMaterialsCsv("name;thickness\nA;18\nB;18\n");
    ASSERT_TRUE(uuids.ok());
    EXPECT_EQ(uuids.value().materials[0].id.str().size(), 36u);
    EXPECT_NE(uuids.value().materials[0].id, uuids.value().materials[1].id);
}

TEST(CsvMaterials, NumberSyntaxIsStrict) {
    const auto thickness = [](const std::string& cell) {
        return importMaterialsCsv("name;thickness\nA;" + cell + "\n");
    };
    EXPECT_EQ(thickness("18").value().materials[0].thickness, 18.0);
    EXPECT_EQ(thickness(" +18.25 ").value().materials[0].thickness, 18.25);
    EXPECT_EQ(thickness("0,8").value().materials[0].thickness, 0.8);
    EXPECT_EQ(thickness(",5").value().materials[0].thickness, 0.5);
    for (const char* bad : {"", "1e3", "18mm", "1,234.5", "1.2.3", "--1", ".", "0", "0x10", "inf", "nan", "١٨"})
        EXPECT_FALSE(thickness(bad).ok()) << bad;
}

TEST(CsvMaterials, FileImport) {
    formats::test::TempDir dir;
    formats::test::writeText(dir / "materials.csv", "name;thickness\nA;18\n");
    auto r = importMaterialsCsvFile(dir / "materials.csv");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().materials.size(), 1u);
    EXPECT_EQ(importMaterialsCsvFile(dir / "missing.csv").error().code, ErrorCode::IoError);
    formats::test::writeText(dir / "bad.csv", "name;thickness\nA;x\n");
    auto bad = importMaterialsCsvFile(dir / "bad.csv");
    ASSERT_FALSE(bad.ok());
    EXPECT_THAT(bad.error().context, HasSubstr("bad.csv"));
}
