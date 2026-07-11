#include "app/OutputPrinter.h"
#include "io/Utils.h"

#include <cstdio>

namespace openhi2txt {

namespace {

static void printXml(const HiScoreResult& result) {
    std::printf("<hi2txt>\n");

    for (const auto& tab : result.tables) {
        if (!tab.id.empty()) {
            std::printf("  <table id=\"");
            Utils::xmlEscapePrintPreserveEntities(tab.id);
            std::printf("\">\n");
        }
        else {
            std::printf("  <table>\n");
        }

        std::printf("    ");
        for (const auto& col : tab.columns) {
            std::printf("<col>");
            Utils::xmlEscapePrintPreserveEntities(col);
            std::printf("</col>");
        }
        std::printf("\n");

        for (const auto& row : tab.rows) {
            std::printf("    <row>");
            for (const auto& cell : row) {
                std::printf("<cell>");
                Utils::xmlEscapePrintPreserveEntities(cell);
                std::printf("</cell>");
            }
            std::printf("</row>\n");
        }

        std::printf("  </table>\n");
    }

    for (const auto& f : result.fields) {
        std::printf("  <field id=\"");
        Utils::xmlEscapePrintPreserveEntities(f.id);
        std::printf("\">");
        Utils::xmlEscapePrintPreserveEntities(f.value);
        std::printf("</field>\n");
    }

    std::printf("</hi2txt>\n");
}

static void printPipeLine(const std::vector<std::string>& cells) {
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i > 0) std::printf("|");
        std::printf("%s", cells[i].c_str());
    }
    std::printf("\n");
}

static void printText(const HiScoreResult& result) {
    for (const auto& tab : result.tables) {
        if (!tab.id.empty()) {
            std::printf("# %s\n", tab.id.c_str());
        }

        printPipeLine(tab.columns);
        for (const auto& row : tab.rows) {
            printPipeLine(row);
        }
        std::printf("\n");
    }

    for (const auto& f : result.fields) {
        std::printf("%s\n%s\n\n", f.id.c_str(), f.value.c_str());
    }
}

} // namespace

void OutputPrinter::print(const HiScoreResult& result, OutputFormat format) {
    if (format == OutputFormat::Xml) {
        printXml(result);
    }
    else {
        printText(result);
    }
}

} // namespace openhi2txt
