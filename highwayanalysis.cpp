#include "highwayanalysis.h"

std::optional<HighwayAnalysis> detectHighway(const std::vector<HighwayStep> &trace) {
    const int size = static_cast<int>(trace.size());
    if (size < highwayMinimumRepeats) return std::nullopt;

    // The longest proper prefix that is also a suffix gives the shortest
    // period, including when the preview ends partway through a repetition.
    std::vector<int> prefix(size);
    for (int i = 1; i < size; ++i) {
        int matched = prefix[i - 1];
        while (matched > 0 && !(trace[i] == trace[matched]))
            matched = prefix[matched - 1];
        if (trace[i] == trace[matched]) ++matched;
        prefix[i] = matched;
    }
    const int period = size - prefix.back();
    if (size / period < highwayMinimumRepeats) return std::nullopt;

    HighwayAnalysis result{period, 0, 0, size / period, {}};
    for (int i = 0; i < period; ++i) {
        switch (trace[i].heading) {
        case 0: --result.dy; break;
        case 1: ++result.dx; break;
        case 2: ++result.dy; break;
        case 3: --result.dx; break;
        }
    }
    if (result.dx == 0 && result.dy == 0) return std::nullopt;
    return result;
}
