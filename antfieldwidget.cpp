#include "antfieldwidget.h"
#include <QPainter>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QPinchGesture>
#include <QApplication>
#include <QtMath>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <QMutexLocker>
#include <QTimer>
#include <QCursor>
#include <QDataStream>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <map>
#include <limits>
#include <QPaintEngine>
#include <QRandomGenerator>
#include <QThread>

#include "QPoint64.h"

// // --- Architecture Constants ---

// Direction Vectors: 0=Up, 1=Right, 2=Down, 3=Left
struct Vec2 { int64_t x, y; };
static constexpr Vec2 DIRECTIONS[4] = {
    {0, -1}, {1, 0}, {0, 1}, {-1, 0}
};

// Branchless Corner Lookup Table
// Index = (entrySide << 2) | exitSide. Returns corner 0-3, or -1 if no corner.
static constexpr int CORNER_LUT[16] = {
    -1,  1, -1,  0,  // entry 0 (Top)
     1, -1,  2, -1,  // entry 1 (Right)
    -1,  2, -1,  3,  // entry 2 (Bottom)
     0, -1,  3, -1   // entry 3 (Left)
};

// --- Implementation ---

AntFieldWidget::AntFieldWidget(QWidget *parent)
    : QWidget(parent) {
    setMinimumSize(400, 400);
    setMouseTracking(true);
#ifndef Q_OS_MACOS
    // Cocoa already sends native magnification events for trackpad pinches.
    grabGesture(Qt::PinchGesture);
#endif
    reset();

    mouseUpdateTimer = new QTimer(this);
    mouseUpdateTimer->setInterval(50);
    connect(mouseUpdateTimer, &QTimer::timeout, [this]() {
        if (underMouse()) {
            updateMousePosition(mapFromGlobal(QCursor::pos()));
        }
    });
    mouseUpdateTimer->start();
}

AntFieldWidget::~AntFieldWidget() {
    cancelHighwayCheck();
    chunks.clear();
    statChunks.clear();
    stateColorCache.clear();
}

struct AntFieldWidget::CompiledRules {
    QString text;
    std::vector<uint32_t> next;
    std::vector<int> turns;
    QVector<QColor> colors;
    QBitArray diagonal;

    CompiledRules(const QString &rule, QBitArray selection)
        : text(rule), next(rule.size()), turns(rule.size()), diagonal(std::move(selection)) {
        const bool useDefaultSelection = diagonal.size() != rule.size();
        if (useDefaultSelection) diagonal.resize(rule.size());
        for (size_t i = 0; i < next.size(); ++i) {
            next[i] = (i + 1) % next.size();
            turns[i] = rule.at(i) == 'L' ? 3 : 1;
            if (useDefaultSelection) diagonal.setBit(i, rule.at(i) != rule.at(next[i]));
        }
        const auto count = std::max<qint64>(2, rule.size());
        colors.reserve(count);
        for (qint64 i = 0; i < count; ++i) {
            const float ratio = float(i) / float(count - 1);
            colors.append(QColor::fromHsv(int(ratio * 360) % 360, 200, 230));
        }
    }
};

void AntFieldWidget::installRules(CompiledRules &&compiled) {
    rules.swap(compiled.text);
    nextStateLUT.swap(compiled.next);
    directionChangeLUT.swap(compiled.turns);
    stateColorCache.swap(compiled.colors);
    diagonalStateMask.swap(compiled.diagonal);
}

void AntFieldWidget::setRules(const QString &rules_str) {
    const QString text = rules_str.trimmed().toUpper();
    CompiledRules compiled(text, text == rules ? diagonalStateMask : QBitArray{});
    cancelHighwayCheck();
    installRules(std::move(compiled));
    reset();
}

QColor AntFieldWidget::cellColor(uint32_t state) const {
    return state == 0 ? QColor(Qt::white) : stateToColor(state);
}

void AntFieldWidget::setDiagonalStates(const QBitArray &states) {
    if (states.size() != rules.size()) return;
    diagonalStateMask = states;
    needsRedraw = true;
    update();
}

void AntFieldWidget::reset() {
    clearHighwayWindow();
    viewOrigin = {};
    completeReadHistory = true;
    lastBoardEdit = 0;
    readsAtEdit.clear();
    chunks.clear();
    activeChunkList.clear();
    {
        QMutexLocker locker(&statisticsMutex);
        statChunks.clear();
        cachedTopCells.clear();
        rankingDirty = true;
        mostVisitedCell = QPoint64(0, 0);
        maxVisits = 0;
        uniqueCellsCount = 0;
        trackedVisits = 0;
    }

    antX = antY = 0;
    antDir = 0;
    stepCount = 0;
    minX = minY = -50;
    maxX = maxY = 50;

    offsetX = width() / 2.0;
    offsetY = height() / 2.0;

    simulationTimer.restart();
    needsRedraw = true;
    update();

    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
}

void AntFieldWidget::nextStep(const qint64 steps) {
    if (rules.isEmpty() || steps <= 0) return;
    clearHighwayWindow();
    if (advanceSteps(steps, 0) > 0) publishPosition();
}

// A zero budget is used by synchronous callers that need the complete batch.
qint64 AntFieldWidget::advanceSteps(qint64 limit, int timeBudgetMs) {
    if (rules.isEmpty() || limit <= 0) return 0;
    if (highwayJob) clearHighwayWindow();
    const auto maxStep = std::numeric_limits<qint64>::max();
    if (stepCount > maxStep - limit) limit = maxStep - stepCount;
    if (limit == 0) return 0;
    QMutexLocker locker(statisticsEnabled ? &statisticsMutex : nullptr);
    if (statisticsEnabled) rankingDirty = true;

    QElapsedTimer clock;
    clock.start();
    qint64 done = 0;
    do {
        constexpr qint64 blockSize = 8192;
        const qint64 block = std::min(blockSize, limit - done);
        advanceBlock(block);
        done += block;
    } while (done < limit && (timeBudgetMs == 0 || clock.elapsed() < timeBudgetMs));
    if (statisticsEnabled) trackedVisits += done;
    return done;
}

void AntFieldWidget::advanceBlock(qint64 steps) {
    qint64 x = antX, y = antY;
    int direction = antDir & 3;
    int dx = DIRECTIONS[direction].x;
    int dy = DIRECTIONS[direction].y;
    ChunkKey currentKey{x >> CHUNK_SHIFT, y >> CHUNK_SHIFT};
    Chunk* currentChunk = nullptr;
    StatChunk* currentStatChunk = nullptr;

    auto fetchChunks = [&]() {
        auto& chunkPtr = chunks[currentKey];
        if (!chunkPtr) {
            chunkPtr = std::make_unique<Chunk>(); // zero-initialise
            activeChunkList.push_back({currentKey.cx, currentKey.cy, chunkPtr.get()});
        }
        currentChunk = chunkPtr.get();

        if (statisticsEnabled) {
            auto& statPtr = statChunks[currentKey];
            if (!statPtr) statPtr = std::make_unique<StatChunk>(); // zero-initialise
            currentStatChunk = statPtr.get();
            currentStatChunk->rankingDirty = true;
        }
    };
    fetchChunks();

    for (qint64 i = 0; i < steps; ++i) {
        int64_t newCx = x >> CHUNK_SHIFT;
        int64_t newCy = y >> CHUNK_SHIFT;

        if (newCx != currentKey.cx || newCy != currentKey.cy) {
            currentKey = {newCx, newCy};
            fetchChunks();
        }

        const int localIndex = (y & CHUNK_MASK) * CHUNK_SIZE + (x & CHUNK_MASK);
        currentChunk->markRead(localIndex);
        uint32_t& state = currentChunk->states[localIndex];
        const int turn = recolorAndGetTurn(state);
        // Rotating the vector keeps the next cell address independent of the heading table.
        std::swap(dx, dy);
        if (turn == 3) dy = -dy;  // left
        else dx = -dx;            // right

        if (currentStatChunk) {
            const int oldDir = direction;
            direction = (direction + turn) & 3;
            const auto visits = ++currentStatChunk->visits[localIndex];
            currentStatChunk->maxVisits = std::max(currentStatChunk->maxVisits, visits);

            if (currentStatChunk->firstVisitStep[localIndex] == -1) {
                currentStatChunk->firstVisitStep[localIndex] = stepCount + i + 1;
                ++uniqueCellsCount;
            }
            currentStatChunk->lastVisitStep[localIndex] = stepCount + i + 1;

            if (visits > maxVisits) {
                maxVisits = visits;
                mostVisitedCell = QPoint64(x, y);
            }

            int entrySide = (oldDir + 2) & 3;
            int cornerIndex = CORNER_LUT[(entrySide << 2) | direction];
            if (cornerIndex >= 0 && cornerIndex < 4) {
                currentStatChunk->corners[localIndex][cornerIndex]++;
            }
        }

        x += dx;
        y += dy;

        if (x < minX) minX = x; else if (x > maxX) maxX = x;
        if (y < minY) minY = y; else if (y > maxY) maxY = y;
    }
    antX = x;
    antY = y;
    if (dx != 0) antDir = dx > 0 ? 1 : 3;
    else antDir = dy > 0 ? 2 : 0;
    stepCount += steps;
}

void AntFieldWidget::publishPosition() {
    centerOnAnt();

    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
}

std::optional<HighwayAnalysis> AntFieldWidget::analyzeHighway() const {
    return findHighway(nullptr);
}

std::optional<HighwayAnalysis> AntFieldWidget::findHighway(const std::atomic_bool *cancel) const {
    auto cancelled = [cancel](qint64 step) {
        return (step & 1023) == 0 && cancel && cancel->load(std::memory_order_relaxed);
    };
    if (rules.isEmpty()) return std::nullopt;
    // Both lookahead and rewind write only to private copies of touched chunks.
    std::unordered_map<ChunkKey, Chunk, ChunkKeyHash> scratch;
    auto cellAt = [&](qint64 x, qint64 y) -> uint32_t & {
        const ChunkKey key{x >> CHUNK_SHIFT, y >> CHUNK_SHIFT};
        auto [copy, inserted] = scratch.try_emplace(key);
        if (inserted) {
            const auto original = chunks.find(key);
            if (original != chunks.end()) copy->second = *original->second;
        }
        return copy->second.states[(y & CHUNK_MASK) * CHUNK_SIZE + (x & CHUNK_MASK)];
    };
    auto x = antX, y = antY;
    int heading = antDir;
    std::vector<HighwayStep> trace;
    trace.reserve(highwayPreviewSteps);
    for (int i = 0; i < highwayPreviewSteps; ++i) {
        if (cancelled(i)) return std::nullopt;
        auto &state = cellAt(x, y);
        const int turn = recolorAndGetTurn(state);
        heading = (heading + turn) & 3;
        trace.push_back({turn, heading});
        x += DIRECTIONS[heading].x;
        y += DIRECTIONS[heading].y;
    }
    auto result = detectHighway(trace);
    if (!result) return result;

    scratch.clear();
    x = antX;
    y = antY;
    heading = antDir;
    auto rewind = [&]() -> HighwayStep {
        const int outgoing = heading;
        x -= DIRECTIONS[outgoing].x;
        y -= DIRECTIONS[outgoing].y;
        auto &state = cellAt(x, y);
        if (state >= nextStateLUT.size()) return {0, outgoing};
        state = previousState(state);
        const int turn = directionChangeLUT[state];
        heading = (outgoing - turn + 4) % 4;
        return {turn, outgoing};
    };
    const qint64 boundary = std::max<qint64>(0, lastBoardEdit);
    int phase = 0;
    qint64 step = stepCount;
    while (step > boundary) {
        if (cancelled(step)) return std::nullopt;
        const int preceding = (phase + result->period - 1) % result->period;
        if (!(rewind() == trace[preceding])) {
            // The mismatch is the last transient step. Put its result back.
            heading = (heading + recolorAndGetTurn(cellAt(x, y))) & 3;
            x += DIRECTIONS[heading].x;
            y += DIRECTIONS[heading].y;
            break;
        }
        --step;
        phase = preceding;
    }
    result->onsetStep = step;
    result->onsetX = x;
    result->onsetY = y;

    // Read positions of the periodic path from its first phase. At this
    // point scratch holds the board colors as they were at the onset.
    struct Candidate { uint32_t state; bool belongs; };
    std::map<std::pair<qint64, qint64>, Candidate> candidates;
    auto fx = x, fy = y;
    for (int i = 0; i < highwayPreviewSteps; ++i) {
        if (cancelled(i)) return std::nullopt;
        const auto [it, inserted] = candidates.try_emplace({fx, fy});
        if (inserted) {
            const auto state = cellAt(fx, fy);
            const auto past = readsAtEdit.find({fx >> CHUNK_SHIFT, fy >> CHUNK_SHIFT});
            const int index = (fy & CHUNK_MASK) * CHUNK_SIZE + (fx & CHUNK_MASK);
            const bool alreadyRead = past != readsAtEdit.end() &&
                                     (past->second[index / 8] & (1u << (index % 8)));
            it->second = {state, state != 0 || alreadyRead};
        }
        const auto delta = DIRECTIONS[trace[phase].heading];
        fx += delta.x;
        fy += delta.y;
        phase = (phase + 1) % result->period;
    }
    // A white cell belongs only if the ant had already read it before onset.
    while (step > boundary) {
        if (cancelled(step)) return std::nullopt;
        rewind();
        auto it = candidates.find({x, y});
        if (it != candidates.end()) it->second.belongs = true;
        --step;
    }
    if (lastBoardEdit < 0 && (x != 0 || y != 0 || heading != 0)) {
        // Older saves did not record board edits. Their origin cannot always be recovered.
        result->onsetStep = -1;
        return result;
    }
    for (const auto &[position, cell] : candidates) {
        if (cell.belongs) result->window.push_back({position.first, position.second, cell.state});
    }
    return result;
}

// The worker owns its scratch board and only reads the live model. All board
// mutations and destruction cancel and join it before changing that model.
void AntFieldWidget::startHighwayCheck() {
    clearHighwayWindow();
    cancelHighway.store(false, std::memory_order_relaxed);
    auto *job = new QFutureWatcher<std::optional<HighwayAnalysis>>(this);
    highwayJob = job;
    connect(job, &QFutureWatcherBase::finished, this, [this, job] {
        highwayJob = nullptr;
        const auto result = job->result();
        job->deleteLater();
        emit highwayChecked(result ? result->period : 0);
        if (result && result->onsetStep >= 0) showHighwayWindow(*result);
    });
    job->setFuture(QtConcurrent::run([this] { return findHighway(&cancelHighway); }));
}

void AntFieldWidget::cancelHighwayCheck() {
    if (!highwayJob) return;
    cancelHighway.store(true, std::memory_order_relaxed);
    highwayJob->disconnect(this);
    highwayJob->waitForFinished();
    delete highwayJob;
    highwayJob = nullptr;
}

void AntFieldWidget::clearHighwayWindow() {
    cancelHighwayCheck();
    highlightedWindow.clear();
    needsRedraw = true;
    update();
    emit highwayWindowCleared();
}

void AntFieldWidget::showHighwayWindow(const HighwayAnalysis &highway) {
    highlightedWindow = highway.window;
    qint64 left = highway.onsetX, right = left, top = highway.onsetY, bottom = top;
    for (const auto &cell : highlightedWindow) {
        left = std::min<qint64>(left, cell.x);
        right = std::max<qint64>(right, cell.x);
        top = std::min<qint64>(top, cell.y);
        bottom = std::max<qint64>(bottom, cell.y);
    }
    const double fit = std::min(width() / (right - left + 4.0), height() / (bottom - top + 4.0));
    zoomFactor = qBound(0.00001, std::min(32.0, fit) / cellSize, 50.0);
    centerOnPoint(left + (right - left) / 2, top + (bottom - top) / 2);
    emit zoomChanged(zoomFactor);
}

void AntFieldWidget::setDisplayStyle(DisplayStyle style) {
    if (currentStyle != style) {
        currentStyle = style;
        needsRedraw = true;
        update();
    }
}

int64_t AntFieldWidget::getVisitCount(const qint64 x, const qint64 y) const {
    if (!statisticsEnabled) return 0;
    QMutexLocker locker(&statisticsMutex);
    const ChunkKey key = { x >> CHUNK_SHIFT, y >> CHUNK_SHIFT };
    auto it = statChunks.find(key);
    if (it != statChunks.end()) {
        return it->second->visits[((y & CHUNK_MASK) << CHUNK_SHIFT) | (x & CHUNK_MASK)];
    }
    return 0;
}

QPoint64 AntFieldWidget::getMostVisitedCell() const {
    QMutexLocker locker(&statisticsMutex);
    return mostVisitedCell;
}

qint64 AntFieldWidget::getTotalVisitedCells() const {
    return stepCount;
}

qint64 AntFieldWidget::getUniqueVisitedCells() const {
    QMutexLocker locker(&statisticsMutex);
    return uniqueCellsCount;
}

AntStatisticsSummary AntFieldWidget::getStatisticsSummary() const {
    QMutexLocker locker(&statisticsMutex);
    AntStatisticsSummary summary;

    summary.totalCellsVisited = stepCount;
    summary.maxVisitsPerCell = maxVisits;
    summary.mostVisitedCell = mostVisitedCell;
    summary.uniqueCellsVisited = uniqueCellsCount;
    summary.simulationTimeMs = simulationTimer.elapsed();

    if (summary.uniqueCellsVisited > 0) {
        summary.averageVisits = static_cast<double>(trackedVisits) / static_cast<double>(summary.uniqueCellsVisited);
    }

    return summary;
}

void StatChunk::refreshRanking() {
    if (!rankingDirty) return;
    std::array<int, CHUNK_AREA> visited;
    int count = 0;
    for (int i = 0; i < CHUNK_AREA; ++i) {
        if (visits[i] > 0) visited[count++] = i;
    }

    topCount = std::min(cachedRankCount, count);
    std::partial_sort_copy(visited.begin(), visited.begin() + count,
                           topIndices.begin(), topIndices.begin() + topCount,
                           [this](int a, int b) {
        return visits[a] != visits[b] ? visits[a] > visits[b] : a < b;
    });
    rankingDirty = false;
}

QVector<QPair<QPoint64, qint64>> AntFieldWidget::getTopVisitedCells(const int count) const {
    QMutexLocker locker(&statisticsMutex);
    if (count <= 0 || statChunks.empty()) return {};

    using RankedCell = QPair<QPoint64, qint64>;
    auto better = [](const RankedCell &a, const RankedCell &b) {
        if (a.second != b.second) return a.second > b.second;
        if (a.first.y() != b.first.y()) return a.first.y() < b.first.y();
        return a.first.x() < b.first.x();
    };
    auto cellAt = [](const ChunkKey &key, const StatChunk &chunk, int index) -> RankedCell {
        return {{key.cx * CHUNK_SIZE + index % CHUNK_SIZE,
                 key.cy * CHUNK_SIZE + index / CHUNK_SIZE}, chunk.visits[index]};
    };

    if (count > cachedRankCount) {
        QVector<RankedCell> cells;
        for (const auto &[key, chunk] : statChunks) {
            for (int i = 0; i < CHUNK_AREA; ++i) {
                if (chunk->visits[i] > 0) cells.append(cellAt(key, *chunk, i));
            }
        }
        const auto n = std::min<qsizetype>(count, cells.size());
        std::partial_sort(cells.begin(), cells.begin() + n, cells.end(), better);
        cells.resize(n);
        return cells;
    }

    if (rankingDirty) {
        cachedTopCells.clear();
        auto mergeChunk = [&](const ChunkKey &key, StatChunk &chunk) {
            if (cachedTopCells.size() == cachedRankCount && chunk.maxVisits < cachedTopCells.last().second)
                return;
            chunk.refreshRanking();
            for (int i = 0; i < chunk.topCount; ++i) {
                auto cell = cellAt(key, chunk, chunk.topIndices[i]);
                if (cachedTopCells.size() == cachedRankCount) {
                    if (!better(cell, cachedTopCells.last())) break;
                    cachedTopCells.removeLast();
                }
                auto pos = std::lower_bound(cachedTopCells.begin(), cachedTopCells.end(), cell, better);
                cachedTopCells.insert(pos, cell);
            }
        };
        // Establish a useful cutoff before walking the rest of a long highway.
        auto peak = statChunks.find({mostVisitedCell.x() >> CHUNK_SHIFT, mostVisitedCell.y() >> CHUNK_SHIFT});
        if (peak != statChunks.end()) mergeChunk(peak->first, *peak->second);
        for (auto it = statChunks.begin(); it != statChunks.end(); ++it)
            if (it != peak) mergeChunk(it->first, *it->second);
        rankingDirty = false;
    }
    return count < cachedTopCells.size() ? cachedTopCells.mid(0, count) : cachedTopCells;
}

QString AntFieldWidget::getRules() const {
    QString result = "";
    int cur = 0;
    for (uint32_t i = 0; i < rules.size(); ++i) {
        cur++;
        if ((i < rules.size() - 1 && rules.at(i) != rules.at(i + 1)) || i == rules.size() - 1) {
            result.append(rules.at(i));
            if (cur > 1)
                result.append(QString::number(cur));
            cur = 0;
        }
    }
    return result;
}

bool AntFieldWidget::exportStatistics(const QString &filename) const {
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out << "x,y,visits,first_step,last_step,top_left,top_right,bottom_right,bottom_left\n";
    QMutexLocker locker(&statisticsMutex);
    for (const auto &[key, stats] : statChunks) {
        for (int i = 0; i < CHUNK_AREA; ++i) {
            if (!stats->visits[i]) continue;
            out << key.cx * CHUNK_SIZE + i % CHUNK_SIZE << ',' << key.cy * CHUNK_SIZE + i / CHUNK_SIZE
                << ',' << stats->visits[i] << ',' << stats->firstVisitStep[i] << ',' << stats->lastVisitStep[i];
            for (auto n : stats->corners[i]) out << ',' << n;
            out << '\n';
        }
    }
    out.flush();
    return out.status() == QTextStream::Ok && file.commit();
}

bool AntFieldWidget::saveState(const QString &filename) const {
    QMutexLocker locker(&statisticsMutex);
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream out(&file);

    // 1. Save global simulation and view state
    out << rules << antX << antY << static_cast<qint32>(antDir) << stepCount;
    out << static_cast<qint64>(minX) << static_cast<qint64>(maxX)
        << static_cast<qint64>(minY) << static_cast<qint64>(maxY);
    out << (offsetX - viewOrigin.x() * (cellSize * zoomFactor))
        << (offsetY - viewOrigin.y() * (cellSize * zoomFactor))
        << static_cast<double>(zoomFactor) << static_cast<qint32>(cellSize);
    out << static_cast<qint64>(maxVisits) << static_cast<qint64>(uniqueCellsCount);
    out << static_cast<qint64>(mostVisitedCell.x()) << static_cast<qint64>(mostVisitedCell.y());
    out << statisticsEnabled;

    // 2. Save Chunks
    out << static_cast<quint64>(chunks.size());
    for (const auto& [key, chunk] : chunks) {
        out << static_cast<qint64>(key.cx) << static_cast<qint64>(key.cy);

        for (const unsigned int state : chunk->states) {
            out << static_cast<quint32>(state);
        }

        auto statIt = statChunks.find(key);
        bool hasStats = (statIt != statChunks.end() && statIt->second != nullptr);
        out << hasStats;
        if (hasStats) {
            const StatChunk* sc = statIt->second.get();
            for (int i = 0; i < CHUNK_AREA; ++i) {
                out << static_cast<qint64>(sc->visits[i]);
                for (int c = 0; c < 4; ++c) {
                    out << static_cast<qint64>(sc->corners[i][c]);
                }
                out << static_cast<qint64>(sc->firstVisitStep[i]) << static_cast<qint64>(sc->lastVisitStep[i]);
            }
        }
    }
    // Append display preferences so older readers can still open the simulation.
    out.writeRawData("DIA1", 4);
    const auto maskBytes = (diagonalStateMask.size() + 7) / 8;
    out.writeRawData(diagonalStateMask.bits(), maskBytes);
    out.writeRawData("WIN1", 4);
    out << completeReadHistory;
    for (const auto &[key, chunk] : chunks) {
        out << key.cx << key.cy;
        out.writeRawData(reinterpret_cast<const char *>(chunk->readMask.data()), chunk->readMask.size());
    }
    out.writeRawData("ORG1", 4);
    out << lastBoardEdit << static_cast<quint64>(readsAtEdit.size());
    for (const auto &[key, mask] : readsAtEdit) {
        out << key.cx << key.cy;
        out.writeRawData(reinterpret_cast<const char *>(mask.data()), mask.size());
    }
    out.writeRawData("CAM1", 4);
    out << viewOrigin.x() << viewOrigin.y() << offsetX << offsetY;
    return out.status() == QDataStream::Ok && file.commit();
}

bool AntFieldWidget::loadState(const QString &filename) try {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QDataStream in(&file);

    // Check the serialized UTF-16 length before allowing QString to allocate.
    quint32 length;
    in >> length;
    quint64 ruleBytes = length;
    if (length == 0xfffffffeu) in >> ruleBytes;
    if (length == 0xffffffffu) ruleBytes = 0; // Qt's null QString marker
    if (in.status() != QDataStream::Ok || ruleBytes % 2 || ruleBytes / 2 > std::numeric_limits<int>::max() ||
        ruleBytes > quint64(file.bytesAvailable()) || !file.seek(0)) return false;
    QString newRules;
    in >> newRules;
    for (QChar turn : newRules)
        if (turn != 'L' && turn != 'R') return false;

    qint64 x, y, steps, left, right, top, bottom, peak, unique, peakX, peakY;
    qint32 dir, size;
    double ox, oy, zoom;
    bool tracking;
    quint64 count;
    in >> x >> y >> dir >> steps >> left >> right >> top >> bottom
       >> ox >> oy >> zoom >> size >> peak >> unique >> peakX >> peakY >> tracking >> count;
    constexpr qint64 minChunkBytes = 2 * sizeof(qint64) + sizeof(quint32) * CHUNK_AREA + 1;
    if (in.status() != QDataStream::Ok || count > quint64(file.bytesAvailable() / minChunkBytes) ||
        (newRules.isEmpty() && (count || steps)) || dir < 0 || dir > 3 || steps < 0 || left > x || x > right || top > y || y > bottom ||
        size < 1 || size > 50 || !std::isfinite(zoom) || zoom < 0.00001 || zoom > 50 ||
        !std::isfinite(ox) || !std::isfinite(oy)) return false;

    decltype(chunks) board;
    decltype(statChunks) stats;
    decltype(activeChunkList) active;
    qint64 visits = 0, actualUnique = 0, actualPeak = 0;
    for (quint64 k = 0; k < count; ++k) {
        ChunkKey key{};
        in >> key.cx >> key.cy;
        auto validChunk = [](qint64 c) {
            return c >= std::numeric_limits<qint64>::min() / CHUNK_SIZE &&
                   c <= std::numeric_limits<qint64>::max() / CHUNK_SIZE;
        };
        if (!validChunk(key.cx) || !validChunk(key.cy) || board.count(key)) return false;
        auto chunk = std::make_unique<Chunk>();
        for (auto &state : chunk->states) {
            quint32 color;
            in >> color;
            if (color >= quint64(newRules.size())) return false;
            state = color;
        }
        bool hasStats;
        in >> hasStats;
        if (in.status() != QDataStream::Ok) return false;
        if (hasStats) {
            auto sc = std::make_unique<StatChunk>();
            for (int i = 0; i < CHUNK_AREA; ++i) {
                auto &n = sc->visits[i];
                in >> n;
                if (n < 0 || n > steps - visits) return false;
                visits += n;
                for (auto &corner : sc->corners[i]) {
                    in >> corner;
                    if (corner < 0 || corner > n) return false;
                }
                in >> sc->firstVisitStep[i] >> sc->lastVisitStep[i];
                if (in.status() != QDataStream::Ok) return false;
                if (n) {
                    if (sc->firstVisitStep[i] < 1 || sc->lastVisitStep[i] < sc->firstVisitStep[i] ||
                        sc->lastVisitStep[i] > steps) return false;
                    ++actualUnique;
                    sc->maxVisits = std::max(sc->maxVisits, n);
                    chunk->markRead(i);
                }
            }
            actualPeak = std::max(actualPeak, sc->maxVisits);
            stats.emplace(key, std::move(sc));
        }
        active.push_back({key.cx, key.cy, chunk.get()});
        board.emplace(key, std::move(chunk));
    }
    if (actualUnique != unique || actualPeak != peak) return false;

    QBitArray diagonal;
    bool complete = visits == steps;
    decltype(readsAtEdit) edits;
    qint64 edit = -1;
    auto tag = [&](const char *name) {
        if (file.peek(4) != name) return false;
        return file.read(4) == name;
    };
    if (tag("DIA1")) {
        const auto bytesNeeded = (qint64(newRules.size()) + 7) / 8;
        const auto bytes = file.read(bytesNeeded);
        if (bytes.size() != bytesNeeded) return false;
        diagonal = QBitArray::fromBits(bytes.constData(), newRules.size());
    }
    if (tag("WIN1")) {
        bool savedComplete;
        in >> savedComplete;
        std::unordered_set<ChunkKey, ChunkKeyHash> seen;
        for (quint64 i = 0; i < count; ++i) {
            ChunkKey key{};
            in >> key.cx >> key.cy;
            auto it = board.find(key);
            if (it == board.end() || !seen.insert(key).second) return false;
            auto &mask = it->second->readMask;
            if (in.readRawData(reinterpret_cast<char *>(mask.data()), mask.size()) != int(mask.size())) return false;
        }
        complete |= savedComplete;
    }
    if (tag("ORG1")) {
        quint64 n;
        in >> edit >> n;
        if (edit < -1 || edit > steps || n > count) return false;
        for (quint64 i = 0; i < n; ++i) {
            ChunkKey key{};
            in >> key.cx >> key.cy;
            if (!board.count(key) || edits.count(key)) return false;
            auto &mask = edits[key];
            if (in.readRawData(reinterpret_cast<char *>(mask.data()), mask.size()) != int(mask.size())) return false;
        }
    }
    qint64 anchorX = 0, anchorY = 0;
    if (tag("CAM1")) {
        in >> anchorX >> anchorY >> ox >> oy;
    }
    if (in.status() != QDataStream::Ok || !file.atEnd() || !std::isfinite(ox) || !std::isfinite(oy)) return false;

    CompiledRules compiled(newRules, std::move(diagonal));
    emit stateAboutToLoad();
    clearHighwayWindow();
    {
        QMutexLocker locker(&statisticsMutex);
        installRules(std::move(compiled));
        chunks.swap(board);
        statChunks.swap(stats);
        activeChunkList.swap(active);
        readsAtEdit.swap(edits);
        completeReadHistory = complete;
        lastBoardEdit = edit;
        antX = x; antY = y; antDir = dir; stepCount = steps;
        minX = left; maxX = right; minY = top; maxY = bottom;
        viewOrigin = QPoint64(anchorX, anchorY);
        offsetX = ox; offsetY = oy; zoomFactor = zoom; cellSize = size;
        trackedVisits = visits; maxVisits = peak; uniqueCellsCount = unique;
        mostVisitedCell = QPoint64(peakX, peakY);
        statisticsEnabled = tracking;
        rankingDirty = true;
        cachedTopCells.clear();
        simulationTimer.restart();
    }
    needsRedraw = true;
    update();
    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
    emit zoomChanged(zoomFactor);
    return true;
} catch (const std::bad_alloc &) {
    return false;
}

void AntFieldWidget::resetStatistics() {
    QMutexLocker locker(&statisticsMutex);
    statChunks.clear();
    cachedTopCells.clear();
    rankingDirty = true;
    mostVisitedCell = QPoint64(0, 0);
    maxVisits = 0;
    uniqueCellsCount = 0;
    trackedVisits = 0;
    simulationTimer.restart();
    needsRedraw = true;
    update();
}

void AntFieldWidget::setStatisticsEnabled(const bool enabled) {
    statisticsEnabled = enabled;
    if (!enabled) {
        resetStatistics();
    }
}

void AntFieldWidget::setCellSize(const int size) {
    cellSize = qBound(1, size, 50);
    needsRedraw = true;
    update();
}

void AntFieldWidget::setZoom(const double zoom) {
    setZoomAt(zoom, QPointF(width() / 2.0, height() / 2.0));
}

void AntFieldWidget::setZoomAt(double zoom, const QPointF &anchor) {
    const double newZoom = qBound(0.00001, zoom, 50.0);
    if (newZoom == zoomFactor) return;
    const double ratio = newZoom / zoomFactor;
    offsetX = anchor.x() + (offsetX - anchor.x()) * ratio;
    offsetY = anchor.y() + (offsetY - anchor.y()) * ratio;
    zoomFactor = newZoom;
    needsRedraw = true;
    update();
    emit zoomChanged(zoomFactor);
}

qint64 AntFieldWidget::estimateRandomizeAreaBytes(const qint64 radius) {
    if (radius < 0)
        return 0;

    // Upper-bound estimate: treats every chunk touched by the square as
    // needing a fresh allocation. It doesn't check which chunks already
    // exist (that itself would mean walking millions of entries for a
    // large radius), so it's deliberately conservative/cheap - O(1).
    // Worst alignment puts the first cell in the last column of a chunk.
    constexpr auto max = std::numeric_limits<qint64>::max();
    const quint64 span = quint64(radius) * 2;
    const quint64 chunksPerSide = 1 + span / CHUNK_SIZE + (span % CHUNK_SIZE != 0);
    if (chunksPerSide > quint64(max) / sizeof(Chunk) / chunksPerSide) return max;
    return qint64(chunksPerSide * chunksPerSide * sizeof(Chunk));
}

void AntFieldWidget::randomizeArea(const qint64 radius) {
    if (rules.isEmpty() || radius < 0)
        return;

    const auto stateCount = static_cast<uint32_t>(rules.length());
    if (stateCount == 0)
        return;

    // Helper for safe chunk index (floor division), same as in nextStep().
    auto chunkIndex = [&](const int64_t coord) -> int64_t {
        int64_t ci = coord / CHUNK_SIZE;
        if (coord < 0 && (coord & CHUNK_MASK) != 0) --ci;
        return ci;
    };

    clearHighwayWindow();
    lastBoardEdit = stepCount;
    readsAtEdit.clear();
    for (const auto &[key, chunk] : chunks) {
        if (std::any_of(chunk->readMask.begin(), chunk->readMask.end(), [](quint8 bits) { return bits != 0; }))
            readsAtEdit.emplace(key, chunk->readMask);
    }
    const qint64 startX = antX - radius;
    const qint64 endX = antX + radius;
    const qint64 startY = antY - radius;
    const qint64 endY = antY + radius;

    const int64_t startCX = chunkIndex(startX);
    const int64_t endCX = chunkIndex(endX);
    const int64_t startCY = chunkIndex(startY);
    const int64_t endCY = chunkIndex(endY);

    const int64_t chunkCountX = endCX - startCX + 1;
    const int64_t chunkCountY = endCY - startCY + 1;
    const auto totalChunkEstimate = static_cast<size_t>(chunkCountX * chunkCountY);

    // Pass 1 (single-threaded): get-or-create every chunk touched by the
    // area. std::unordered_map insertion isn't thread-safe, so this part
    // has to run on the calling thread. Reserving capacity up front avoids
    // repeated rehashing/reallocation while we insert potentially millions
    // of entries.
    chunks.reserve(chunks.size() + totalChunkEstimate);
    activeChunkList.reserve(activeChunkList.size() + totalChunkEstimate);

    std::vector<ChunkData> touchedChunks;
    touchedChunks.reserve(totalChunkEstimate);

    for (int64_t cy = startCY; cy <= endCY; ++cy) {
        for (int64_t cx = startCX; cx <= endCX; ++cx) {
            ChunkKey key{cx, cy};
            auto &chunkPtr = chunks[key];
            if (!chunkPtr) {
                chunkPtr = std::make_unique<Chunk>(Chunk{});
                activeChunkList.push_back({key.cx, key.cy, chunkPtr.get()});
            }
            touchedChunks.push_back({cx, cy, chunkPtr.get()});
        }
    }

    // Pass 2: fill each chunk's intersection with the target area.
    // Each lambda invocation owns its chunk exclusively, so no locking is
    // needed for the writes themselves — but we still give it a *private*
    // RNG rather than calling QRandomGenerator::global() from every
    // worker: global() is a single shared, thread-safe instance, and
    // hammering it concurrently from many threads causes real contention
    // (cache-line bouncing on its internal state) that can outweigh the
    // work being parallelized.
    auto fillChunk = [=](const ChunkData &data) {
        const int64_t chunkMinX = data.cx * CHUNK_SIZE;
        const int64_t chunkMinY = data.cy * CHUNK_SIZE;

        const int64_t localStartX = std::max<qint64>(startX, chunkMinX) - chunkMinX;
        const int64_t localEndX = std::min<qint64>(endX, chunkMinX + CHUNK_MASK) - chunkMinX;
        const int64_t localStartY = std::max<qint64>(startY, chunkMinY) - chunkMinY;
        const int64_t localEndY = std::min<qint64>(endY, chunkMinY + CHUNK_MASK) - chunkMinY;

        QRandomGenerator rng(QRandomGenerator::global()->generate());
        Chunk *chunk = data.chunk;

        for (int64_t ly = localStartY; ly <= localEndY; ++ly) {
            const int64_t rowBase = ly << CHUNK_SHIFT;
            for (int64_t lx = localStartX; lx <= localEndX; ++lx) {
                chunk->states[rowBase | lx] = rng.bounded(stateCount);
            }
        }
    };

    // A single chunk is at most CHUNK_SIZE*CHUNK_SIZE (1024) cells — a few
    // KB to write, which a plain loop finishes before the thread pool
    // could even wake a worker. Only bother going parallel once there are
    // enough independent chunks to make the scheduling overhead worth it.
    const int idealThreads = qMax(1, QThread::idealThreadCount());
    const auto parallelThreshold = static_cast<size_t>(idealThreads) * 4;

    if (touchedChunks.size() < parallelThreshold) {
        for (const auto &data : touchedChunks) fillChunk(data);
    } else {
        QtConcurrent::blockingMap(touchedChunks, fillChunk);
    }

    if (startX < minX) minX = startX;
    if (endX > maxX) maxX = endX;
    if (startY < minY) minY = startY;
    if (endY > maxY) maxY = endY;

    needsRedraw = true;
    update();
}

void AntFieldWidget::centerOnAnt() {
    centerOnPoint(antX, antY);
}

void AntFieldWidget::moveView(const qint64 dx, const qint64 dy) {
    offsetX += static_cast<qreal>(dx);
    offsetY += static_cast<qreal>(dy);
    needsRedraw = true;
    update();
}

void AntFieldWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    if (needsRedraw || bufferPixmap.size() != size()) {
        redrawBuffer();
        needsRedraw = false;
    }

    QPainter painter(this);
    painter.drawPixmap(0, 0, bufferPixmap);
}

void AntFieldWidget::redrawBuffer() {
    bufferPixmap = QPixmap(size());
    bufferPixmap.fill(Qt::white);

    QPainter painter(&bufferPixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal scaledCellSize = cellSize * zoomFactor;

    const auto first = screenToField({-qCeil(scaledCellSize), -qCeil(scaledCellSize)});
    const auto last = screenToField({width() + 2 * qCeil(scaledCellSize),
                                     height() + 2 * qCeil(scaledCellSize)});
    const qint64 startX = first.x(), startY = first.y(), endX = last.x(), endY = last.y();

    // Safe floor division helper
    auto chunkIndex = [&](qint64 coord) -> int64_t {
        int64_t ci = coord / CHUNK_SIZE;
        if (coord < 0 && (coord & CHUNK_MASK) != 0) --ci;
        return ci;
    };

    const int64_t startCX = chunkIndex(startX);
    const int64_t endCX   = chunkIndex(endX);
    const int64_t startCY = chunkIndex(startY);
    const int64_t endCY   = chunkIndex(endY);

    bool useOptimizedDrawing = (scaledCellSize < 1.0);

    if (useOptimizedDrawing) {
        redrawBufferZoomedOut(painter, startCX, endCX, startCY, endCY);
    } else {
        // Render grid
        if (scaledCellSize >= 4) {
            painter.setPen(QPen(QColor(220, 220, 220), 0.5));
            for (qint64 x = startX; ; ++x) {
                const double px = screenX(x);
                painter.drawLine(QPointF(px, 0), QPointF(px, height()));
                if (x == endX) break;
            }
            for (qint64 y = startY; ; ++y) {
                const double py = screenY(y);
                painter.drawLine(QPointF(0, py), QPointF(width(), py));
                if (y == endY) break;
            }
        }

        // Draw visible chunks only
        for (int64_t cy = startCY; cy <= endCY; ++cy) {
            for (int64_t cx = startCX; cx <= endCX; ++cx) {
                ChunkKey key = {cx, cy};
                auto it = chunks.find(key);
                if (it == chunks.end()) continue;

                Chunk* chunk = it->second.get();
                for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                    for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                        uint32_t state = chunk->states[(ly << CHUNK_SHIFT) | lx];
                        if (state > 0) {
                            int64_t globalX = (cx * CHUNK_SIZE) + lx;
                            int64_t globalY = (cy * CHUNK_SIZE) + ly;

                            if (globalX >= startX && globalX <= endX && globalY >= startY && globalY <= endY) {
                                QColor color = stateToColor(state);
                                const double px = screenX(globalX);
                                const double py = screenY(globalY);
                                painter.fillRect(QRectF(px, py,
                                    static_cast<qreal>(scaledCellSize), static_cast<qreal>(scaledCellSize)), color);
                            }
                        }
                    }
                }
            }
        }
    }


    const int minimumOverlaySize = currentStyle == Diagonals ? 4 : 8;
    if (statisticsEnabled && currentStyle != JustColors && scaledCellSize >= minimumOverlaySize) {
        QMutexLocker locker(&statisticsMutex);
        painter.setPen(QPen(Qt::black, 3));

        // Reuse the chunk bounds calculated earlier in redrawBuffer()
        for (int64_t cy = startCY; cy <= endCY; ++cy) {
            for (int64_t cx = startCX; cx <= endCX; ++cx) {
                auto it = statChunks.find({cx, cy});
                if (it == statChunks.end()) continue; // Skip empty chunks

                StatChunk* statChunk = it->second.get();
                ChunkKey key = {cx, cy};
                Chunk* chunk = chunks.find(key)->second.get();

                for (int i = 0; i < CHUNK_AREA; ++i) {
                    if (statChunk->visits[i] == 0) continue;

                    // Convert local chunk index back to global field coordinates
                    int64_t gx = (cx * CHUNK_SIZE) + (i % CHUNK_SIZE);
                    int64_t gy = (cy * CHUNK_SIZE) + (i / CHUNK_SIZE);

                    // Calculate screen position
                    QRectF cellRect(screenX(gx),
                                   screenY(gy),
                                   scaledCellSize, scaledCellSize);

                    if (currentStyle == Visits) {
                        QString text = QString::number(statChunk->visits[i]);

                        // Dynamic Font Scaling
                        QFont font = painter.font();
                        font.setPointSizeF(1);
                        painter.setFont(font);
                        QRectF textRect = painter.fontMetrics().boundingRect(text);

                        double scale = qMin((cellRect.width() * 0.8) / textRect.width(),
                                            (cellRect.height() * 0.8) / textRect.height());

                        font.setPointSizeF(qMax(1.0, scale)); // Ensure readable size
                        painter.setFont(font);
                        painter.drawText(cellRect, Qt::AlignCenter, text);

                    } else if (currentStyle == Rotations) {
                        auto drawCornerText = [&](const QRectF &rect, qint64 value, Qt::Alignment align) {
                            if (value == 0) return;

                            QString cur_text = QString::number(value);
                            QRectF cornerRect(0, 0, rect.width() / 2, rect.height() / 2);

                            if (align & Qt::AlignRight) cornerRect.moveLeft(rect.center().x());
                            else cornerRect.moveLeft(rect.left());

                            if (align & Qt::AlignBottom) cornerRect.moveTop(rect.center().y());
                            else cornerRect.moveTop(rect.top());

                            QFont font = painter.font();
                            font.setPointSizeF(1);
                            painter.setFont(font);
                            QRectF textRect = painter.fontMetrics().boundingRect(cur_text);

                            double scale = qMin((cornerRect.width() * 0.85) / textRect.width(),
                                                (cornerRect.height() * 0.85) / textRect.height());

                            font.setPointSizeF(qMax(1.0, scale));
                            painter.setFont(font);
                            painter.drawText(cornerRect, Qt::AlignCenter, cur_text);
                        };

                        // Draw the 4 corner counters from our statChunk array
                        drawCornerText(cellRect, statChunk->corners[i][0], Qt::AlignLeft | Qt::AlignTop);
                        drawCornerText(cellRect, statChunk->corners[i][1], Qt::AlignRight | Qt::AlignTop);
                        drawCornerText(cellRect, statChunk->corners[i][2], Qt::AlignRight | Qt::AlignBottom);
                        drawCornerText(cellRect, statChunk->corners[i][3], Qt::AlignLeft | Qt::AlignBottom);
                    } else if (currentStyle == Diagonals) {
                        if (diagonalStateMask.testBit(chunk->states[i])) {
                            if  ((rules.at(previousState(chunk->states[i])) == 'R') ^ ((i % CHUNK_SIZE + i / CHUNK_SIZE) % 2 != 0)) {
                                painter.drawLine(cellRect.bottomLeft(), cellRect.topRight());
                            } else {
                                painter.drawLine(cellRect.bottomRight(), cellRect.topLeft());
                            }
                        }
                    } else if (currentStyle == Arcs) {
                        if ((rules.at(previousState(chunk->states[i])) == 'L') ^ ((i % CHUNK_SIZE + i / CHUNK_SIZE) % 2 == 0)) {
                            painter.drawArc(cellRect.adjusted(cellRect.width()/2.0, cellRect.height()/2.0,
                                      cellRect.width()/2.0, cellRect.height()/2.0), 180 * 16, -90 * 16);
                            painter.drawArc(cellRect.adjusted(-cellRect.width()/2.0, -cellRect.height()/2.0,
                                      -cellRect.width()/2.0, -cellRect.height()/2.0), 0 * 16, -90 * 16);
                        } else {
                            painter.drawArc(cellRect.adjusted(cellRect.width()/2.0, -cellRect.height()/2.0,
                                      cellRect.width()/2.0, -cellRect.height()/2.0), 270 * 16, -90 * 16);
                            painter.drawArc(cellRect.adjusted(-cellRect.width()/2.0, cellRect.height()/2.0,
                                      -cellRect.width()/2.0, cellRect.height()/2.0), 90 * 16, -90 * 16);
                        }

                    }
                }
            }
        }
    }

    // Draw each member separately so adjacent cells and holes remain distinguishable.
    if (!highlightedWindow.empty()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, scaledCellSize < 12);
        const QPen halo(Qt::white, std::min(3.0, scaledCellSize / 6));
        const QPen outline(Qt::black, std::min(1.0, scaledCellSize / 12));
        const qreal inset = std::min(2.0, scaledCellSize * 0.12);
        painter.setBrush(Qt::NoBrush);
        for (const auto &cell : highlightedWindow) {
            QRectF rect(screenX(cell.x), screenY(cell.y),
                        scaledCellSize, scaledCellSize);
            rect.adjust(inset, inset, -inset, -inset);
            painter.setPen(halo);
            painter.drawRect(rect);
            painter.setPen(outline);
            painter.drawRect(rect);
        }
        painter.restore();
    }

    // Draw ant
    const qreal antScreenX = screenX(antX);
    const qreal antScreenY = screenY(antY);
    const QPointF antCenter(antScreenX + scaledCellSize / 2, antScreenY + scaledCellSize / 2);

    if (scaledCellSize >= 2) {
        painter.setBrush(Qt::red);
        painter.setPen(QPen(Qt::black, 1));

        const qreal radius = scaledCellSize * 0.4;
        QPolygonF triangle;
        triangle << antCenter + QPointF(0, -radius)
                 << antCenter + QPointF(radius * 0.7, radius * 0.7)
                 << antCenter + QPointF(-radius * 0.7, radius * 0.7);

        QTransform transform;
        transform.translate(antCenter.x(), antCenter.y());
        transform.rotate(antDir * 90.0);
        transform.translate(-antCenter.x(), -antCenter.y());
        painter.drawPolygon(transform.map(triangle));
    } else {
        painter.setBrush(Qt::red);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(antCenter, scaledCellSize / 2, scaledCellSize / 2);
    }
}

void AntFieldWidget::redrawBufferZoomedOut(QPainter &painter, qint64 startCX, qint64 endCX,
                                           qint64 startCY, qint64 endCY) {
    QImage image(size(), QImage::Format_RGB32);
    if (image.isNull()) return;
    constexpr int bandHeight = 32;
    struct Band { int first, end; std::vector<ChunkData> chunks; };
    std::vector<Band> bands;
    for (int y = 0; y < height(); y += bandHeight)
        bands.push_back({y, std::min(y + bandHeight, height()), {}});

    for (const auto &data : activeChunkList) {
        if (data.cx < startCX || data.cx > endCX || data.cy < startCY || data.cy > endCY) continue;
        const qint64 top = data.cy * CHUNK_SIZE;
        const double first = screenY(top), last = screenY(top + CHUNK_MASK);
        if (last < 0 || first >= height()) continue;
        const int firstBand = int(std::max(0.0, first)) / bandHeight;
        const int lastBand = int(std::min(double(height() - 1), last)) / bandHeight;
        for (int i = firstBand; i <= lastBand; ++i) bands[i].chunks.push_back(data);
    }

    std::vector<QRgb> palette;
    for (const QColor &color : stateColorCache) palette.push_back(color.rgb());
    if (palette.empty()) palette.push_back(qRgb(255, 255, 255));
    palette[0] = qRgb(255, 255, 255);
    auto *pixels = reinterpret_cast<quint32 *>(image.bits());
    const int stride = image.bytesPerLine() / sizeof(quint32);
    const int columns = image.width();
    // Each worker owns whole rows. The highest nonzero state in a pixel wins
    // regardless of chunk insertion order or worker scheduling.
    auto drawBand = [&](const Band &band) {
        for (int y = band.first; y < band.end; ++y)
            std::fill_n(pixels + qsizetype(y) * stride, columns, quint32{0});
        for (const auto &data : band.chunks) {
            const qint64 left = data.cx * CHUNK_SIZE, top = data.cy * CHUNK_SIZE;
            for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                const double y = screenY(top + ly);
                if (y < band.first || y >= band.end) continue;
                auto *row = pixels + qsizetype(int(y)) * stride;
                for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                    const auto state = data.chunk->states[ly * CHUNK_SIZE + lx];
                    if (!state) continue;
                    const double x = screenX(left + lx);
                    if (x >= 0 && x < columns) row[int(x)] = std::max(row[int(x)], state);
                }
            }
        }
        for (int y = band.first; y < band.end; ++y) {
            auto *row = pixels + qsizetype(y) * stride;
            for (int x = 0; x < columns; ++x) row[x] = palette[row[x]];
        }
    };
    QtConcurrent::blockingMap(bands, drawBand);
    painter.drawImage(0, 0, image);
}

// Subtract integer coordinates before converting so adjacent cells at either
// end of the 64-bit field remain distinguishable.
static double relativeCell(qint64 cell, qint64 origin) {
    if (cell >= origin) return double(quint64(cell) - quint64(origin));
    return -double(quint64(origin) - quint64(cell));
}

double AntFieldWidget::screenX(qint64 x) const {
    return offsetX + relativeCell(x, viewOrigin.x()) * (cellSize * zoomFactor);
}

double AntFieldWidget::screenY(qint64 y) const {
    return offsetY + relativeCell(y, viewOrigin.y()) * (cellSize * zoomFactor);
}

static qint64 fieldCoordinate(qint64 origin, double delta) {
    constexpr auto low = std::numeric_limits<qint64>::min();
    constexpr auto high = std::numeric_limits<qint64>::max();
    delta = std::floor(delta);
    if (delta >= double(high) || delta < double(low)) {
        const long double value = static_cast<long double>(origin) + delta;
        if (value >= static_cast<long double>(high)) return high;
        if (value <= static_cast<long double>(low)) return low;
        return qint64(value);
    }
    const auto n = qint64(delta);
    if (n > 0 && origin > high - n) return high;
    if (n < 0 && origin < low - n) return low;
    return origin + n;
}

QPoint64 AntFieldWidget::screenToField(const QPoint64 &screenPos) const {
    const double scale = cellSize * zoomFactor;
    return {fieldCoordinate(viewOrigin.x(), (screenPos.x() - offsetX) / scale),
            fieldCoordinate(viewOrigin.y(), (screenPos.y() - offsetY) / scale)};
}

QColor AntFieldWidget::stateToColor(const uint32_t state) const {
    if (stateColorCache.isEmpty()) {
        return QColor::fromHsv(0, 200, 230);
    }
    return stateColorCache[state % stateColorCache.size()];
}

uint32_t AntFieldWidget::previousState(const uint32_t state) const {
    return state == 0 ? rules.length() - 1 : state - 1;
}

int AntFieldWidget::recolorAndGetTurn(uint32_t &state) const {
    // Board edits generate valid colors and the snapshot reader validates them on input.
    const int turn = directionChangeLUT[state];
    state = nextStateLUT[state];
    return turn;
}

void AntFieldWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        lastMousePos = event->pos();
        dragging = true;
        setCursor(Qt::ClosedHandCursor);
    }
}

void AntFieldWidget::mouseMoveEvent(QMouseEvent *event) {
    // Update mouse position for coordinate display
    updateMousePosition(event->pos());

    if (dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint64 delta = event->pos() - lastMousePos;
        offsetX += static_cast<qreal>(delta.x());
        offsetY += static_cast<qreal>(delta.y());
        lastMousePos = event->pos();
        needsRedraw = true;
        update();
    }
}

void AntFieldWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

void AntFieldWidget::applyPinch(double scale, const QPointF &globalCenter) {
    if (scale > 0 && std::isfinite(scale))
        setZoomAt(zoomFactor * scale, globalCenter - mapToGlobal(QPoint(0, 0)));
}

bool AntFieldWidget::event(QEvent *event) {
    if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPointF center = gesture->globalPosition();
#else
            const QPointF center = gesture->screenPos();
#endif
            applyPinch(1.0 + gesture->value(), center);
            event->accept();
            return true;
        }
    } else if (event->type() == QEvent::Gesture) {
        auto *gestures = static_cast<QGestureEvent *>(event);
        auto *pinch = static_cast<QPinchGesture *>(gestures->gesture(Qt::PinchGesture));
        if (pinch) {
            // The recognizer can repeat its last scale when the fingers lift.
            if ((pinch->state() == Qt::GestureStarted || pinch->state() == Qt::GestureUpdated)
                && pinch->changeFlags().testFlag(QPinchGesture::ScaleFactorChanged))
                applyPinch(pinch->scaleFactor(), pinch->centerPoint());
            gestures->accept(pinch);
            return true;
        }
    }
    return QWidget::event(event);
}

void AntFieldWidget::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }
    constexpr double zoomStep = 1.2;
    const double newZoom = delta > 0 ? zoomFactor * zoomStep : zoomFactor / zoomStep;
    if (newZoom < 0.00001 || newZoom > 50.0) return;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QPointF mousePos = event->position();
#else
    const QPointF mousePos = event->posF();
#endif

    setZoomAt(newZoom, mousePos);
}

void AntFieldWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    needsRedraw = true;
}

void AntFieldWidget::centerOnPoint(qint64 x, qint64 y) {
    viewOrigin = {x, y};
    offsetX = width() / 2.0;
    offsetY = height() / 2.0;
    needsRedraw = true;
    update();
}

void AntFieldWidget::centerOnPoint(const QPoint64 &point) {
    centerOnPoint(point.x(), point.y());
}

void AntFieldWidget::updateMousePosition(const QPoint &pos) {
    QPoint64 fieldPos = screenToField(pos);

    // Only emit if the cell has changed
    if (fieldPos != lastMouseCellPos) {
        lastMouseCellPos = fieldPos;
        emit mouseOverCell(fieldPos.x(), fieldPos.y());
    }
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void AntFieldWidget::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    updateMousePosition(event->position().toPoint());
}
#else
void AntFieldWidget::enterEvent(QEvent *event) {
    QWidget::enterEvent(event);
    updateMousePosition(mapFromGlobal(QCursor::pos()));
}
#endif

void AntFieldWidget::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    // Clear coordinates when mouse leaves
    lastMouseCellPos = QPoint64(0, 0);
    emit mouseOverCell(0, 0);
}
