#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// One transition, the turn and the absolute heading after turning.
struct HighwayStep {
    int turn;
    int heading;

    bool operator==(const HighwayStep &other) const {
        return turn == other.turn && heading == other.heading;
    }
};

// Each position is read in the preview. Its state is the color at the onset of the highway.
struct HighwayWindowCell {
    int64_t x, y;
    uint32_t state;
};

struct HighwayAnalysis {
    int period;
    int dx;
    int dy;
    int completeRepeats;
    std::vector<HighwayWindowCell> window;
    int64_t onsetStep = -1;
    int64_t onsetX = 0, onsetY = 0;
};

constexpr int highwayPreviewSteps = 65536;
constexpr int highwayMinimumRepeats = 4;
std::optional<HighwayAnalysis> detectHighway(const std::vector<HighwayStep> &trace);
