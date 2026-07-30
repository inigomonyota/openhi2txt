#include "app/OutputPrinter.h"
#include "io/Utils.h"

#include <cstdio>

namespace openhi2txt {

namespace {

static void printXml(const HiScoreResult& result) {
    std::printf("<hi2txt>\n");

    auto printTable = [](const HiScoreTable& tab) {
        if (!tab.id.empty()) {
            std::printf("  <table id=\"");
            Utils::xmlEscapePrintPreserveEntities(tab.id, true);
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
    };

    auto printField = [](const HiScoreField& f) {
        std::printf("  <field id=\"");
        Utils::xmlEscapePrintPreserveEntities(f.id, true);
        std::printf("\">");
        Utils::xmlEscapePrintPreserveEntities(f.value);
        std::printf("</field>\n");
    };

    if (!result.outputOrder.empty()) {
        for (const auto& item : result.outputOrder) {
            if (item.kind == HiScoreOutputKind::Table && item.index < result.tables.size())
                printTable(result.tables[item.index]);
            else if (item.kind == HiScoreOutputKind::Field && item.index < result.fields.size())
                printField(result.fields[item.index]);
        }
    }
    else {
        for (const auto& tab : result.tables) printTable(tab);
        for (const auto& f : result.fields) printField(f);
    }

    std::printf("</hi2txt>\n");
}

static void printPipeLine(const std::vector<std::string>& cells) {
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i > 0) std::printf("|");
        // A literal pipe inside a cell would be indistinguishable from the
        // text table delimiter. Official hi2txt renders it as an uppercase I;
        // keep the original value for XML and library consumers.
        for (unsigned char ch : cells[i]) {
            std::putchar(ch == (unsigned char)'|' ? 'I' : (int)ch);
        }
    }
    std::printf("\n");
}

static void printText(const HiScoreResult& result) {
    auto printTable = [](const HiScoreTable& tab) {
        if (!tab.id.empty()) {
            std::printf("# %s\n", tab.id.c_str());
        }

        printPipeLine(tab.columns);
        for (const auto& row : tab.rows) {
            printPipeLine(row);
        }
        std::printf("\n");
    };

    auto printField = [](const HiScoreField& f) {
        std::printf("%s\n%s\n\n", f.id.c_str(), f.value.c_str());
    };

    if (!result.outputOrder.empty()) {
        for (const auto& item : result.outputOrder) {
            if (item.kind == HiScoreOutputKind::Table && item.index < result.tables.size())
                printTable(result.tables[item.index]);
            else if (item.kind == HiScoreOutputKind::Field && item.index < result.fields.size())
                printField(result.fields[item.index]);
        }
    }
    else {
        for (const auto& tab : result.tables) printTable(tab);
        for (const auto& f : result.fields) printField(f);
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
