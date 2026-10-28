#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

struct CsvTable {
    vector<string> headers;
    vector<map<string, string>> rows;
};

struct Series {
    string name;
    vector<double> values;
};

struct ScalabilityAggregate {
    string runName;
    int trials = 0;
    double h1Cost = 0.0, h2Cost = 0.0, h3Cost = 0.0;
    double h1Gap = 0.0, h2Gap = 0.0, h3Gap = 0.0;
    double h1GapTol = 0.0, h2GapTol = 0.0, h3GapTol = 0.0;
    int h1Best = 0, h2Best = 0, h3Best = 0;
    int h1Within5 = 0, h2Within5 = 0, h3Within5 = 0;
    double h1Ms = 0.0, h2Ms = 0.0, h3Ms = 0.0;
};

struct MetricTotals {
    int count = 0;
    double accuracy = 0.0;
    double within5 = 0.0;
    double avgAbsGap = 0.0;
    double maxAbsGap = 0.0;
    double avgRelGap = 0.0;
    double maxRelGap = 0.0;
    double avgRelGapTol = 0.0;
    double maxRelGapTol = 0.0;
};

static vector<string> parseCsvLine(const string& line) {
    vector<string> fields;
    string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    fields.push_back(cur);
    return fields;
}

static CsvTable readCsv(const fs::path& path) {
    ifstream in(path);
    if (!in) throw runtime_error("Could not open " + path.string());

    CsvTable table;
    string line;
    if (!getline(in, line)) return table;
    table.headers = parseCsvLine(line);

    while (getline(in, line)) {
        if (line.empty()) continue;
        vector<string> fields = parseCsvLine(line);
        map<string, string> row;
        for (size_t i = 0; i < table.headers.size(); ++i) {
            row[table.headers[i]] = (i < fields.size() ? fields[i] : "");
        }
        table.rows.push_back(row);
    }
    return table;
}

static double toDouble(const map<string, string>& row, const string& key) {
    auto it = row.find(key);
    if (it == row.end() || it->second.empty()) return 0.0;
    return stod(it->second);
}

static int toInt(const map<string, string>& row, const string& key) {
    return (int)llround(toDouble(row, key));
}

static string csvEscape(const string& value) {
    if (value.find_first_of(",\"\n\r") == string::npos) return value;
    string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out += "\"";
    return out;
}

static vector<fs::path> discoverRunDirs(const fs::path& runsDir) {
    vector<fs::path> dirs;
    regex pattern(R"(run_(\d+))");
    if (!fs::exists(runsDir)) return dirs;
    for (const auto& entry : fs::directory_iterator(runsDir)) {
        if (!entry.is_directory()) continue;
        string name = entry.path().filename().string();
        if (regex_match(name, pattern)) dirs.push_back(entry.path());
    }
    sort(dirs.begin(), dirs.end());
    return dirs;
}

static int nextRunNumber(const fs::path& runsDir) {
    int best = 0;
    regex pattern(R"(run_(\d+))");
    if (fs::exists(runsDir)) {
        for (const auto& entry : fs::directory_iterator(runsDir)) {
            if (!entry.is_directory()) continue;
            smatch match;
            string name = entry.path().filename().string();
            if (regex_match(name, match, pattern)) {
                best = max(best, stoi(match[1].str()));
            }
        }
    }
    return best + 1;
}

static void saveCurrentRun(const fs::path& runsDir) {
    vector<string> files = {
        "benchmark_runs.csv",
        "benchmark_summary.csv",
        "benchmark_trial_jobs.csv",
        "scalability_runs.csv"
    };

    for (const auto& file : files) {
        if (!fs::exists(file)) {
            throw runtime_error("Missing " + file + ". Run the benchmark first.");
        }
    }

    fs::create_directories(runsDir);
    int runNumber = nextRunNumber(runsDir);
    ostringstream name;
    name << "run_" << setw(3) << setfill('0') << runNumber;
    fs::path runDir = runsDir / name.str();
    fs::create_directories(runDir);

    for (const auto& file : files) {
        fs::copy_file(file, runDir / file, fs::copy_options::overwrite_existing);
    }

    cout << "Saved current CSV files into " << runDir.string() << "\n";
}

static ScalabilityAggregate aggregateScalability(const string& runName, const fs::path& path) {
    CsvTable table = readCsv(path);
    ScalabilityAggregate agg;
    agg.runName = runName;
    for (const auto& row : table.rows) {
        ++agg.trials;
        agg.h1Cost += toDouble(row, "contrib_n2");
        agg.h2Cost += toDouble(row, "oneshot_n3");
        agg.h3Cost += toDouble(row, "greedy_n4");
        agg.h1Gap += toDouble(row, "contrib_gap_pct");
        agg.h2Gap += toDouble(row, "oneshot_gap_pct");
        agg.h3Gap += toDouble(row, "greedy_gap_pct");
        agg.h1GapTol += toDouble(row, "contrib_gap_pct_with_5_tolerance");
        agg.h2GapTol += toDouble(row, "oneshot_gap_pct_with_5_tolerance");
        agg.h3GapTol += toDouble(row, "greedy_gap_pct_with_5_tolerance");
        agg.h1Best += toInt(row, "contrib_is_best");
        agg.h2Best += toInt(row, "oneshot_is_best");
        agg.h3Best += toInt(row, "greedy_is_best");
        agg.h1Within5 += toInt(row, "contrib_within_5_pct");
        agg.h2Within5 += toInt(row, "oneshot_within_5_pct");
        agg.h3Within5 += toInt(row, "greedy_within_5_pct");
        agg.h1Ms += toDouble(row, "contrib_ms");
        agg.h2Ms += toDouble(row, "oneshot_ms");
        agg.h3Ms += toDouble(row, "greedy_ms");
    }
    return agg;
}

static void writeQualityComparison(const fs::path& outPath, const vector<fs::path>& runDirs) {
    ofstream out(outPath);
    out << "run,heuristic,accuracy_pct,within_5_pct_accuracy,avg_abs_gap,max_abs_gap,"
        << "avg_rel_gap_pct,max_rel_gap_pct,avg_rel_gap_pct_with_5_tolerance,"
        << "max_rel_gap_pct_with_5_tolerance,matched,matched_within_5_pct,compared\n";

    for (const auto& dir : runDirs) {
        fs::path path = dir / "benchmark_summary.csv";
        if (!fs::exists(path)) continue;
        CsvTable table = readCsv(path);
        string runName = dir.filename().string();
        for (const auto& row : table.rows) {
            out << csvEscape(runName) << ","
                << csvEscape(row.at("heuristic")) << ","
                << row.at("accuracy_pct") << ","
                << row.at("within_5_pct_accuracy") << ","
                << row.at("avg_abs_gap") << ","
                << row.at("max_abs_gap") << ","
                << row.at("avg_rel_gap_pct") << ","
                << row.at("max_rel_gap_pct") << ","
                << row.at("avg_rel_gap_pct_with_5_tolerance") << ","
                << row.at("max_rel_gap_pct_with_5_tolerance") << ","
                << row.at("matched") << ","
                << row.at("matched_within_5_pct") << ","
                << row.at("compared") << "\n";
        }
    }
}

static void writeScalabilityComparison(const fs::path& outPath,
                                       const vector<ScalabilityAggregate>& aggs) {
    ofstream out(outPath);
    out << "run,trials,h1_best_pct,h2_best_pct,h3_best_pct,"
        << "h1_within5_pct,h2_within5_pct,h3_within5_pct,"
        << "h1_avg_cost,h2_avg_cost,h3_avg_cost,"
        << "h1_avg_gap_pct,h2_avg_gap_pct,h3_avg_gap_pct,"
        << "h1_avg_gap_pct_with_5_tolerance,h2_avg_gap_pct_with_5_tolerance,h3_avg_gap_pct_with_5_tolerance,"
        << "h1_avg_ms,h2_avg_ms,h3_avg_ms\n";

    for (const auto& a : aggs) {
        if (a.trials == 0) continue;
        auto pct = [&](int count) { return 100.0 * count / a.trials; };
        out << csvEscape(a.runName) << "," << a.trials << ","
            << pct(a.h1Best) << "," << pct(a.h2Best) << "," << pct(a.h3Best) << ","
            << pct(a.h1Within5) << "," << pct(a.h2Within5) << "," << pct(a.h3Within5) << ","
            << (a.h1Cost / a.trials) << "," << (a.h2Cost / a.trials) << "," << (a.h3Cost / a.trials) << ","
            << (a.h1Gap / a.trials) << "," << (a.h2Gap / a.trials) << "," << (a.h3Gap / a.trials) << ","
            << (a.h1GapTol / a.trials) << "," << (a.h2GapTol / a.trials) << "," << (a.h3GapTol / a.trials) << ","
            << (a.h1Ms / a.trials) << "," << (a.h2Ms / a.trials) << "," << (a.h3Ms / a.trials) << "\n";
    }
}

static string colorFor(size_t i) {
    static const vector<string> colors = {
        "#2563eb", "#dc2626", "#16a34a", "#9333ea", "#f59e0b", "#0891b2"
    };
    return colors[i % colors.size()];
}

static string formatChartValue(double value, bool isPercent) {
    ostringstream out;
    out << fixed << setprecision(isPercent ? 0 : (value >= 100.0 ? 0 : 1)) << value;
    if (isPercent) out << "%";
    return out.str();
}

static void writeGroupedBarSvg(const fs::path& path, const string& title,
                               const vector<string>& labels, const vector<Series>& series,
                               const string& yLabel, bool isPercent) {
    if (labels.empty() || series.empty()) return;
    double maxValue = 0.0;
    for (const auto& s : series) {
        for (double v : s.values) maxValue = max(maxValue, v);
    }
    if (isPercent) {
        maxValue = 100.0;
    } else {
        if (maxValue <= 0.0) maxValue = 1.0;
        maxValue *= 1.18;
    }

    const bool fewGroups = labels.size() <= 3;
    const int left = 105, right = 55, top = 185, bottom = 145;
    const int groupWidth = fewGroups ? 210 : (series.size() >= 4 ? 118 : 105);
    const int width = fewGroups ? 900 : max(1300, left + right + groupWidth * (int)labels.size());
    const int height = 760;
    const int plotW = width - left - right;
    const int plotH = height - top - bottom;
    const double barW = fewGroups ? 34.0 : min(24.0, (groupWidth - 12.0) / max(1, (int)series.size()));
    const double groupsTotalW = groupWidth * (double)labels.size();
    const double groupsStartX = left + max(0.0, (plotW - groupsTotalW) / 2.0);

    ofstream out(path);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " " << height << "\">\n";
    out << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    out << "<text x=\"" << (width / 2) << "\" y=\"42\" text-anchor=\"middle\" "
        << "font-family=\"Arial\" font-size=\"24\" font-weight=\"700\">" << title << "</text>\n";
    out << "<text x=\"18\" y=\"" << (top + plotH / 2) << "\" transform=\"rotate(-90 18 "
        << (top + plotH / 2) << ")\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"15\">"
        << yLabel << "</text>\n";

    int legendX = fewGroups ? (width / 2 - (int)(series.size() * 105)) : left;
    int legendY = 96;
    for (size_t j = 0; j < series.size(); ++j) {
        out << "<rect x=\"" << legendX << "\" y=\"" << (legendY - 12)
            << "\" width=\"13\" height=\"13\" fill=\"" << colorFor(j) << "\"/>\n";
        out << "<text x=\"" << (legendX + 19) << "\" y=\"" << legendY
            << "\" font-family=\"Arial\" font-size=\"14\">" << series[j].name << "</text>\n";
        legendX += fewGroups ? 210 : 205;
    }

    for (int i = 0; i <= 5; ++i) {
        double value = maxValue * i / 5.0;
        double y = top + plotH - (value / maxValue) * plotH;
        out << "<line x1=\"" << left << "\" x2=\"" << (left + plotW) << "\" y1=\""
            << y << "\" y2=\"" << y << "\" stroke=\"#e5e7eb\"/>\n";
        out << "<text x=\"" << (left - 8) << "\" y=\"" << (y + 4)
            << "\" text-anchor=\"end\" font-family=\"Arial\" font-size=\"12\">"
            << fixed << setprecision(1) << value << "</text>\n";
    }

    out << "<line x1=\"" << left << "\" x2=\"" << left << "\" y1=\"" << top
        << "\" y2=\"" << (top + plotH) << "\" stroke=\"#111827\"/>\n";
    out << "<line x1=\"" << left << "\" x2=\"" << (left + plotW) << "\" y1=\""
        << (top + plotH) << "\" y2=\"" << (top + plotH) << "\" stroke=\"#111827\"/>\n";

    for (size_t i = 0; i < labels.size(); ++i) {
        double groupX = groupsStartX + i * groupWidth + (fewGroups ? 58 : 12);
        double centerX = groupX + (series.size() * barW) / 2.0;
        for (size_t j = 0; j < series.size(); ++j) {
            double value = (i < series[j].values.size() ? series[j].values[i] : 0.0);
            double plottedValue = min(value, maxValue);
            double barH = (plottedValue / maxValue) * plotH;
            double x = groupX + j * barW;
            double y = top + plotH - barH;
            out << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << (barW - 2)
                << "\" height=\"" << barH << "\" fill=\"" << colorFor(j) << "\"/>\n";
            double labelY = y - 10 - (j % 2) * 16;
            if (labelY < top - 22) labelY = top - 22 + (j % 2) * 16;
            out << "<text x=\"" << (x + (barW - 2) / 2.0) << "\" y=\"" << labelY
                << "\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"15\" "
                << "fill=\"#111827\">" << formatChartValue(value, isPercent) << "</text>\n";
        }
        out << "<text x=\"" << centerX << "\" y=\"" << (top + plotH + 32)
            << "\" transform=\"rotate(-35 " << centerX << " " << (top + plotH + 32)
            << ")\" text-anchor=\"end\" font-family=\"Arial\" font-size=\"13\">"
            << labels[i] << "</text>\n";
    }
    out << "</svg>\n";
}

static map<string, map<string, double>> readQualityComparisonByRun(const fs::path& path,
                                                                    const string& metric) {
    CsvTable table = readCsv(path);
    map<string, map<string, double>> byRun;
    for (const auto& row : table.rows) {
        string h = row.at("heuristic");
        string shortName = h.find("H1") != string::npos ? "H1" :
                           h.find("H2") != string::npos ? "H2" :
                           h.find("H3") != string::npos ? "H3" : h;
        byRun[row.at("run")][shortName] = toDouble(row, metric);
    }
    return byRun;
}

static string heuristicShortName(const string& heuristic) {
    if (heuristic.find("H1") != string::npos) return "H1";
    if (heuristic.find("H2") != string::npos) return "H2";
    if (heuristic.find("H3") != string::npos) return "H3";
    return heuristic;
}

static map<string, MetricTotals> readQualityAverages(const fs::path& qualityComparisonPath) {
    CsvTable table = readCsv(qualityComparisonPath);
    map<string, MetricTotals> totals;
    for (const auto& row : table.rows) {
        string h = heuristicShortName(row.at("heuristic"));
        MetricTotals& t = totals[h];
        ++t.count;
        t.accuracy += toDouble(row, "accuracy_pct");
        t.within5 += toDouble(row, "within_5_pct_accuracy");
        t.avgAbsGap += toDouble(row, "avg_abs_gap");
        t.maxAbsGap += toDouble(row, "max_abs_gap");
        t.avgRelGap += toDouble(row, "avg_rel_gap_pct");
        t.maxRelGap += toDouble(row, "max_rel_gap_pct");
        t.avgRelGapTol += toDouble(row, "avg_rel_gap_pct_with_5_tolerance");
        t.maxRelGapTol += toDouble(row, "max_rel_gap_pct_with_5_tolerance");
    }
    return totals;
}

static double avgOrZero(double sum, int count) {
    return count > 0 ? sum / count : 0.0;
}

static void printQualityAverages(const map<string, MetricTotals>& totals) {
    cout << fixed << setprecision(2);
    cout << "\n=== Quality averages across saved runs ===\n";
    const vector<string> heuristics = {"H1", "H2", "H3"};
    for (const string& h : heuristics) {
        auto it = totals.find(h);
        if (it == totals.end() || it->second.count == 0) continue;
        const MetricTotals& t = it->second;
        cout << h << ":\n";
        cout << "  avg exact match: " << avgOrZero(t.accuracy, t.count) << "%\n";
        cout << "  avg within 5%: " << avgOrZero(t.within5, t.count) << "%\n";
        cout << "  avg absolute gap: " << avgOrZero(t.avgAbsGap, t.count) << "\n";
        cout << "  avg max absolute gap: " << avgOrZero(t.maxAbsGap, t.count) << "\n";
        cout << "  avg relative gap: " << avgOrZero(t.avgRelGap, t.count) << "%\n";
        cout << "  avg max relative gap: " << avgOrZero(t.maxRelGap, t.count) << "%\n";
        cout << "  avg relative gap with 5% tolerance: "
             << avgOrZero(t.avgRelGapTol, t.count) << "%\n";
    }
}

static void printScalabilityAverages(const vector<ScalabilityAggregate>& aggs) {
    int totalTrials = 0;
    double h1Cost = 0.0, h2Cost = 0.0, h3Cost = 0.0;
    double h1Gap = 0.0, h2Gap = 0.0, h3Gap = 0.0;
    double h1GapTol = 0.0, h2GapTol = 0.0, h3GapTol = 0.0;
    int h1Best = 0, h2Best = 0, h3Best = 0;
    int h1Within5 = 0, h2Within5 = 0, h3Within5 = 0;
    double h1Ms = 0.0, h2Ms = 0.0, h3Ms = 0.0;

    for (const auto& a : aggs) {
        totalTrials += a.trials;
        h1Cost += a.h1Cost; h2Cost += a.h2Cost; h3Cost += a.h3Cost;
        h1Gap += a.h1Gap; h2Gap += a.h2Gap; h3Gap += a.h3Gap;
        h1GapTol += a.h1GapTol; h2GapTol += a.h2GapTol; h3GapTol += a.h3GapTol;
        h1Best += a.h1Best; h2Best += a.h2Best; h3Best += a.h3Best;
        h1Within5 += a.h1Within5; h2Within5 += a.h2Within5; h3Within5 += a.h3Within5;
        h1Ms += a.h1Ms; h2Ms += a.h2Ms; h3Ms += a.h3Ms;
    }

    if (totalTrials == 0) return;
    auto pct = [&](int count) { return 100.0 * count / totalTrials; };

    cout << fixed << setprecision(2);
    cout << "\n=== Scalability averages across saved runs ===\n";
    cout << "Compared scalability trials: " << totalTrials << "\n";
    cout << "Best observed counts:\n";
    cout << "  H1: " << h1Best << "/" << totalTrials << " (" << pct(h1Best) << "%)\n";
    cout << "  H2: " << h2Best << "/" << totalTrials << " (" << pct(h2Best) << "%)\n";
    cout << "  H3: " << h3Best << "/" << totalTrials << " (" << pct(h3Best) << "%)\n";
    cout << "Within 5% of best observed:\n";
    cout << "  H1: " << h1Within5 << "/" << totalTrials << " (" << pct(h1Within5) << "%)\n";
    cout << "  H2: " << h2Within5 << "/" << totalTrials << " (" << pct(h2Within5) << "%)\n";
    cout << "  H3: " << h3Within5 << "/" << totalTrials << " (" << pct(h3Within5) << "%)\n";
    cout << "Average objective:\n";
    cout << "  H1: " << (h1Cost / totalTrials) << "\n";
    cout << "  H2: " << (h2Cost / totalTrials) << "\n";
    cout << "  H3: " << (h3Cost / totalTrials) << "\n";
    cout << "Average gap from best observed:\n";
    cout << "  H1: " << (h1Gap / totalTrials) << "%\n";
    cout << "  H2: " << (h2Gap / totalTrials) << "%\n";
    cout << "  H3: " << (h3Gap / totalTrials) << "%\n";
    cout << "Average gap with 5% tolerance:\n";
    cout << "  H1: " << (h1GapTol / totalTrials) << "%\n";
    cout << "  H2: " << (h2GapTol / totalTrials) << "%\n";
    cout << "  H3: " << (h3GapTol / totalTrials) << "%\n";
    cout << "Average runtime:\n";
    cout << "  H1: " << (h1Ms / totalTrials) << " ms\n";
    cout << "  H2: " << (h2Ms / totalTrials) << " ms\n";
    cout << "  H3: " << (h3Ms / totalTrials) << " ms\n";
}

static void writeGraphs(const fs::path& comparisonDir, const fs::path& graphsDir,
                        const vector<ScalabilityAggregate>& aggs) {
    fs::create_directories(graphsDir);
    fs::path qualityPath = comparisonDir / "quality_comparison.csv";
    vector<string> runLabels;
    vector<double> h1Acc, h2Acc, h3Acc, h3Within, h1Gap, h2Gap, h3Gap;

    auto acc = readQualityComparisonByRun(qualityPath, "accuracy_pct");
    auto within = readQualityComparisonByRun(qualityPath, "within_5_pct_accuracy");
    auto gap = readQualityComparisonByRun(qualityPath, "avg_rel_gap_pct");
    for (const auto& kv : acc) {
        runLabels.push_back(kv.first);
        h1Acc.push_back(kv.second.count("H1") ? kv.second.at("H1") : 0.0);
        h2Acc.push_back(kv.second.count("H2") ? kv.second.at("H2") : 0.0);
        h3Acc.push_back(kv.second.count("H3") ? kv.second.at("H3") : 0.0);
        h3Within.push_back(within[kv.first].count("H3") ? within[kv.first].at("H3") : 0.0);
        h1Gap.push_back(gap[kv.first].count("H1") ? gap[kv.first].at("H1") : 0.0);
        h2Gap.push_back(gap[kv.first].count("H2") ? gap[kv.first].at("H2") : 0.0);
        h3Gap.push_back(gap[kv.first].count("H3") ? gap[kv.first].at("H3") : 0.0);
    }

    writeGroupedBarSvg(graphsDir / "quality_accuracy.svg",
                       "Quality Accuracy vs Baseline", runLabels,
                       {{"H1 exact %", h1Acc}, {"H2 exact %", h2Acc}, {"H3 exact %", h3Acc},
                        {"H3 within 5%", h3Within}},
                       "percent", true);
    writeGroupedBarSvg(graphsDir / "quality_avg_relative_gap.svg",
                       "Quality Average Relative Gap vs Baseline", runLabels,
                       {{"H1", h1Gap}, {"H2", h2Gap}, {"H3", h3Gap}},
                       "percent", true);

    vector<double> h1Best, h2Best, h3Best, h3ScalWithin, h1Ms, h2Ms, h3Ms;
    vector<string> scalLabels;
    for (const auto& a : aggs) {
        if (a.trials == 0) continue;
        scalLabels.push_back(a.runName);
        h1Best.push_back(100.0 * a.h1Best / a.trials);
        h2Best.push_back(100.0 * a.h2Best / a.trials);
        h3Best.push_back(100.0 * a.h3Best / a.trials);
        h3ScalWithin.push_back(100.0 * a.h3Within5 / a.trials);
        h1Ms.push_back(a.h1Ms / a.trials);
        h2Ms.push_back(a.h2Ms / a.trials);
        h3Ms.push_back(a.h3Ms / a.trials);
    }

    writeGroupedBarSvg(graphsDir / "scalability_best_observed.svg",
                       "Scalability Best Observed Frequency", scalLabels,
                       {{"H1 best %", h1Best}, {"H2 best %", h2Best}, {"H3 best %", h3Best},
                        {"H3 within 5%", h3ScalWithin}},
                       "percent", true);
    writeGroupedBarSvg(graphsDir / "scalability_runtime.svg",
                       "Scalability Average Runtime", scalLabels,
                       {{"H1 ms", h1Ms}, {"H2 ms", h2Ms}, {"H3 ms", h3Ms}},
                       "milliseconds", false);

    map<string, MetricTotals> qualityAverages = readQualityAverages(qualityPath);
    vector<string> heuristicLabels = {"H1", "H2", "H3"};
    vector<double> avgExact, avgWithin5;
    for (const string& h : heuristicLabels) {
        const MetricTotals& t = qualityAverages[h];
        avgExact.push_back(avgOrZero(t.accuracy, t.count));
        avgWithin5.push_back(avgOrZero(t.within5, t.count));
    }
    writeGroupedBarSvg(graphsDir / "quality_average_all_runs.svg",
                       "Quality Average Across All Runs", heuristicLabels,
                       {{"exact match %", avgExact}, {"within 5%", avgWithin5}},
                       "percent", true);

    int totalTrials = 0;
    int h1BestTotal = 0, h2BestTotal = 0, h3BestTotal = 0;
    int h1Within5Total = 0, h2Within5Total = 0, h3Within5Total = 0;
    for (const auto& a : aggs) {
        totalTrials += a.trials;
        h1BestTotal += a.h1Best;
        h2BestTotal += a.h2Best;
        h3BestTotal += a.h3Best;
        h1Within5Total += a.h1Within5;
        h2Within5Total += a.h2Within5;
        h3Within5Total += a.h3Within5;
    }
    if (totalTrials > 0) {
        auto pct = [&](int count) { return 100.0 * count / totalTrials; };
        writeGroupedBarSvg(graphsDir / "scalability_average_all_runs.svg",
                           "Scalability Average Across All Runs", heuristicLabels,
                           {{"best observed %", {pct(h1BestTotal), pct(h2BestTotal), pct(h3BestTotal)}},
                            {"within 5%", {pct(h1Within5Total), pct(h2Within5Total), pct(h3Within5Total)}}},
                           "percent", true);
    }
}

static void compareRuns(const fs::path& runsDir) {
    vector<fs::path> runDirs = discoverRunDirs(runsDir);
    if (runDirs.empty()) {
        cout << "No run folders found in " << runsDir.string() << "\n";
        cout << "Run exactRejectionFlowshop.exe first so it creates runs/run_001, runs/run_002, etc.\n";
        return;
    }

    fs::create_directories(runsDir);
    fs::path comparisonDir = runsDir / "comparison";
    fs::path graphsDir = comparisonDir / "graphs";
    fs::create_directories(comparisonDir);
    fs::create_directories(graphsDir);

    writeQualityComparison(comparisonDir / "quality_comparison.csv", runDirs);

    vector<ScalabilityAggregate> aggs;
    for (const auto& dir : runDirs) {
        fs::path scalabilityPath = dir / "scalability_runs.csv";
        if (fs::exists(scalabilityPath)) {
            aggs.push_back(aggregateScalability(dir.filename().string(), scalabilityPath));
        }
    }
    writeScalabilityComparison(comparisonDir / "scalability_comparison.csv", aggs);
    writeGraphs(comparisonDir, graphsDir, aggs);

    map<string, MetricTotals> qualityAverages =
        readQualityAverages(comparisonDir / "quality_comparison.csv");
    printQualityAverages(qualityAverages);
    printScalabilityAverages(aggs);

    cout << "Compared " << runDirs.size() << " run folders.\n";
    cout << "Wrote:\n";
    cout << "  " << (comparisonDir / "quality_comparison.csv").string() << "\n";
    cout << "  " << (comparisonDir / "scalability_comparison.csv").string() << "\n";
    cout << "  " << (graphsDir / "quality_accuracy.svg").string() << "\n";
    cout << "  " << (graphsDir / "quality_avg_relative_gap.svg").string() << "\n";
    cout << "  " << (graphsDir / "scalability_best_observed.svg").string() << "\n";
    cout << "  " << (graphsDir / "scalability_runtime.svg").string() << "\n";
    cout << "  " << (graphsDir / "quality_average_all_runs.svg").string() << "\n";
    cout << "  " << (graphsDir / "scalability_average_all_runs.svg").string() << "\n";
}

int main(int argc, char** argv) {
    try {
        fs::path runsDir = "runs";
        string command = (argc >= 2 ? argv[1] : "compare");
        if (argc >= 3) runsDir = argv[2];

        if (command == "save-current") {
            saveCurrentRun(runsDir);
        } else if (command == "compare") {
            compareRuns(runsDir);
        } else {
            cout << "Usage:\n";
            cout << "  compare_runs.exe compare [runs_dir]\n";
            return 1;
        }
    } catch (const exception& ex) {
        cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
