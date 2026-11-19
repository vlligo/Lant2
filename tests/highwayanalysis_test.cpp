#include "highwayanalysis.h"
#include <iostream>
#include <map>
#include <utility>

int main() {
    // A translating staircase, ending partway through a repetition.
    const std::vector<HighwayStep> cycle{{3, 0}, {1, 1}};
    std::vector<HighwayStep> trace;
    for (int i = 0; i < 19; ++i) trace.push_back(cycle[i % cycle.size()]);
    const auto result = detectHighway(trace);
    if (!result || result->period != 2 || result->dx != 1 ||
        result->dy != -1 || result->completeRepeats != 9) {
        std::cerr << "Incorrect primitive period or displacement\n";
        return 1;
    }

    trace.back().turn = 1;
    if (detectHighway(trace)) return 2;
    trace.resize(7);
    if (detectHighway(trace)) return 3;

    trace.clear();
    for (int i = 0; i < 20; ++i) trace.push_back({1, i % 4});
    if (detectHighway(trace)) return 4;
    // Sample the classic LR ant from a phase inside its highway.
    std::map<std::pair<int, int>, bool> cells;
    int x = 0, y = 0, heading = 0;
    const int dx[]{0, 1, 0, -1};
    const int dy[]{-1, 0, 1, 0};
    trace.clear();
    for (int i = 0; i < 13000; ++i) {
        auto &cell = cells[{x, y}];
        heading = (heading + (cell ? 1 : 3)) % 4;
        if (i >= 12000) trace.push_back({cell ? 1 : 3, heading});
        cell = !cell;
        x += dx[heading];
        y += dy[heading];
    }
    const auto classic = detectHighway(trace);
    if (!classic || classic->period != 104 || classic->dx != 2 || classic->dy != 2)
        return 5;
    return 0;
}
