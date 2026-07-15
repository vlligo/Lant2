#include "antfieldwidget.h"
#include <QPainter>
#include <QWheelEvent>
#include <QApplication>
#include <QtMath>
#include <QFile>
#include <algorithm>
#include <QMutexLocker>
#include <QTimer>
#include <QCursor>
#include <QDataStream>
#include <QtConcurrent/QtConcurrentMap>
#include <atomic>
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
    chunks.clear();
    statChunks.clear();
    stateColorCache.clear();
}

void AntFieldWidget::setRules(const QString &rules_str) {
    this->rules = rules_str.trimmed().toUpper();
    updateStateColors();

    // Pre-compile branchless lookup tables (LUTs) for extreme speed
    const uint32_t ruleLength = rules.length();
    nextStateLUT.resize(ruleLength);
    directionChangeLUT.resize(ruleLength);

    for (uint32_t i = 0; i < ruleLength; ++i) {
        nextStateLUT[i] = (i + 1) % ruleLength;
        QChar rule = rules.at(i);
        directionChangeLUT[i] = (rule == 'L') ? 3 : 1; // +3 is mathematically equivalent to -1 (Left) in modulo 4
    }

    reset();
}

void AntFieldWidget::updateStateColors() {
    stateColorCache.clear();
    const long long maxStates = qMax(2ll, rules.length());

    for (int state = 0; state < maxStates; ++state) {
        const float ratio = static_cast<float>(state) / static_cast<float>(maxStates > 1 ? (maxStates - 1) : 1);
        const int hue = static_cast<int>(ratio * 360) % 360;
        stateColorCache.append(QColor::fromHsv(hue, 200, 230));
    }
}

void AntFieldWidget::reset() {
    chunks.clear();
    activeChunkList.clear();
    if (statisticsEnabled) {
        QMutexLocker locker(&statisticsMutex);
        statChunks.clear();
        mostVisitedCell = QPoint64(0, 0);
        maxVisits = 0;
        uniqueCellsCount = 0;
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
    setUpdatesEnabled(false);

    // Helper for safe chunk index (floor division) and local index.
    auto chunkIndex = [&](const int64_t coord) -> int64_t {
        // C++ division truncates toward zero → adjust for negatives
        int64_t ci = coord / CHUNK_SIZE;
        if (coord < 0 && (coord & CHUNK_MASK) != 0) --ci;
        return ci;
    };

    // Compute starting chunk
    const int64_t cx = chunkIndex(antX);
    const int64_t cy = chunkIndex(antY);
    ChunkKey currentKey{cx, cy};

    Chunk* currentChunk = nullptr;
    StatChunk* currentStatChunk = nullptr;

    auto fetchChunks = [&]() {
        auto& chunkPtr = chunks[currentKey];
        if (!chunkPtr) {
            chunkPtr = std::make_unique<Chunk>(Chunk{}); // zero-initialise
            activeChunkList.push_back({currentKey.cx, currentKey.cy, chunkPtr.get()});
        }
        currentChunk = chunkPtr.get();

        if (statisticsEnabled) {
            QMutexLocker locker(&statisticsMutex);
            auto& statPtr = statChunks[currentKey];
            if (!statPtr) statPtr = std::make_unique<StatChunk>(StatChunk{}); // zero-initialise
            currentStatChunk = statPtr.get();
        }
    };
    fetchChunks();

    for (qint64 s = 0; s < steps; ++s) {
        int64_t newCx = chunkIndex(antX);
        int64_t newCy = chunkIndex(antY);

        if (newCx != currentKey.cx || newCy != currentKey.cy) {
            currentKey = {newCx, newCy};
            fetchChunks();
        }

        // local index now computed from chunk index (always 0 … CHUNK_SIZE-1)
        int64_t lx = antX - currentKey.cx * CHUNK_SIZE;
        int64_t ly = antY - currentKey.cy * CHUNK_SIZE;
        int64_t localIndex = (ly << CHUNK_SHIFT) | lx;

        uint32_t& state = currentChunk->states[localIndex];
        int oldDir = antDir;

        if (state < nextStateLUT.size()) {
            antDir = (antDir + directionChangeLUT[state]) & 3;
            state = nextStateLUT[state];
        }

        // Statistics update (unchanged, but now safely zero-initialized)
        if (statisticsEnabled && currentStatChunk) {
            QMutexLocker locker(&statisticsMutex);

            currentStatChunk->visits[localIndex]++;

            if (currentStatChunk->firstVisitStep[localIndex] == -1) {
                currentStatChunk->firstVisitStep[localIndex] = stepCount + 1;
                ++uniqueCellsCount;
            }
            currentStatChunk->lastVisitStep[localIndex] = stepCount + 1;

            if (currentStatChunk->visits[localIndex] > maxVisits) {
                maxVisits = currentStatChunk->visits[localIndex];
                mostVisitedCell = QPoint64(antX, antY);
            }

            int entrySide = (oldDir + 2) & 3;
            int cornerIndex = CORNER_LUT[(entrySide << 2) | antDir];
            if (cornerIndex >= 0 && cornerIndex < 4) {
                currentStatChunk->corners[localIndex][cornerIndex]++;
            }
        }

        antX += DIRECTIONS[antDir].x;
        antY += DIRECTIONS[antDir].y;

        if (antX < minX) minX = antX; else if (antX > maxX) maxX = antX;
        if (antY < minY) minY = antY; else if (antY > maxY) maxY = antY;
        ++stepCount;
    }

    setUpdatesEnabled(true);
    needsRedraw = true;
    update();
    centerOnAnt();

    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
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

    // Calculate average visits (only if we have visited cells)
    if (summary.uniqueCellsVisited > 0) {
        // We can approximate average visits efficiently
        summary.averageVisits = static_cast<double>(summary.totalCellsVisited) / static_cast<double>(summary.uniqueCellsVisited);
    }

    return summary;
}

QVector<QPair<QPoint64, qint64>> AntFieldWidget::getTopVisitedCells(const int count) const {
    QMutexLocker locker(&statisticsMutex);
    if (statChunks.empty()) return {};

    QVector<QPair<QPoint64, qint64>> allCells;

    // Flatten chunks into a single list for sorting
    for (const auto& [key, statChunk] : statChunks) {
        for (int i = 0; i < CHUNK_AREA; ++i) {
            if (statChunk->visits[i] > 0) {
                const int64_t lx = i % CHUNK_SIZE;
                const int64_t ly = i / CHUNK_SIZE;
                int64_t gx = (key.cx << CHUNK_SHIFT) + lx;
                int64_t gy = (key.cy << CHUNK_SHIFT) + ly;
                allCells.append({{gx, gy}, static_cast<qint64>(statChunk->visits[i])});
            }
        }
    }

    // Partial sort is much faster for "Top N"
    const int sortLimit = std::min(static_cast<int>(allCells.size()), count);
    std::partial_sort(allCells.begin(), allCells.begin() + sortLimit, allCells.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

    if (allCells.size() > count) allCells.resize(count);
    return allCells;
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

bool AntFieldWidget::saveState(const QString &filename) const {
    QMutexLocker locker(&statisticsMutex);
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream out(&file);

    // 1. Save global simulation and view state
    out << rules << antX << antY << static_cast<qint32>(antDir) << stepCount;
    out << static_cast<qint64>(minX) << static_cast<qint64>(maxX)
        << static_cast<qint64>(minY) << static_cast<qint64>(maxY);
    out << static_cast<qreal>(offsetX) << static_cast<qreal>(offsetY)
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
    return true;
}

bool AntFieldWidget::loadState(const QString &filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QDataStream in(&file);

    QString newRules;
    in >> newRules;
    setRules(newRules); // Triggers a reset()

    QMutexLocker locker(&statisticsMutex);
    qint32 dir32, cell32;
    qint64 mx, my;
    bool statsOn;

    // 1. Restore global state
    in >> antX >> antY >> dir32 >> stepCount;
    antDir = dir32;
    in >> minX >> maxX >> minY >> maxY;
    in >> offsetX >> offsetY >> zoomFactor >> cell32;
    cellSize = cell32;
    in >> maxVisits >> uniqueCellsCount;
    in >> mx >> my;
    mostVisitedCell = QPoint64(mx, my);
    in >> statsOn;
    statisticsEnabled = statsOn;

    quint64 numChunks;
    in >> numChunks;

    chunks.clear();
    statChunks.clear();

    // 2. Restore Chunks
    for (quint64 k = 0; k < numChunks; ++k) {
        ChunkKey key{};
        in >> key.cx >> key.cy;

        auto chunk = std::make_unique<Chunk>();
        for (int i = 0; i < CHUNK_AREA; ++i) {
            quint32 state;
            in >> state;
            chunk->states[i] = state;
        }
        chunks[key] = std::move(chunk);

        bool hasStats;
        in >> hasStats;
        if (hasStats) {
            auto sc = std::make_unique<StatChunk>();
            for (int i = 0; i < CHUNK_AREA; ++i) {
                in >> sc->visits[i];
                for (int c = 0; c < 4; ++c) in >> sc->corners[i][c];
                in >> sc->firstVisitStep[i] >> sc->lastVisitStep[i];
            }
            statChunks[key] = std::move(sc);
        }
    }

    needsRedraw = true;
    update();
    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
    emit zoomChanged(zoomFactor);
    return true;
}

void AntFieldWidget::resetStatistics() {
    QMutexLocker locker(&statisticsMutex);
    statChunks.clear(); // This is the real data store
    mostVisitedCell = QPoint64(0, 0);
    maxVisits = 0;
    uniqueCellsCount = 0;
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
    // Get the current center point of the widget in field coordinates
    QPoint64 centerPoint = screenToField(QPoint64(width() / 2, height() / 2));

    // Store the current screen position of this center point
    QPoint64 oldCenterScreen = fieldToScreen(centerPoint);

    // Set the new zoom factor
    zoomFactor = qBound(0.00001, zoom, 50.0);

    // Calculate where the same field point should be on screen after zoom
    QPoint64 newCenterScreen = fieldToScreen(centerPoint);

    // Adjust offset to keep the same field point at the center
    offsetX += static_cast<qreal>(oldCenterScreen.x() - newCenterScreen.x());
    offsetY += static_cast<qreal>(oldCenterScreen.y() - newCenterScreen.y());

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
    const qint64 side = 2 * radius + 1;
    const qint64 chunksPerSide = (side + CHUNK_SIZE - 1) / CHUNK_SIZE;
    const qint64 totalChunks = chunksPerSide * chunksPerSide;
    return totalChunks * static_cast<qint64>(sizeof(Chunk));
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

        const int64_t localStartX = qMax(startX, chunkMinX) - chunkMinX;
        const int64_t localEndX = qMin(endX, chunkMinX + CHUNK_SIZE - 1) - chunkMinX;
        const int64_t localStartY = qMax(startY, chunkMinY) - chunkMinY;
        const int64_t localEndY = qMin(endY, chunkMinY + CHUNK_SIZE - 1) - chunkMinY;

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
    const long double scaledCellSize = cellSize * zoomFactor;
    offsetX = static_cast<qreal>(width()) / 2.0 - static_cast<qreal>(antX * scaledCellSize);
    offsetY = static_cast<qreal>(height()) / 2.0 - static_cast<qreal>(antY * scaledCellSize);
    needsRedraw = true;
    update();
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

    const qint64 startX = qFloor((-offsetX) / scaledCellSize) - 1;
    const qint64 endX   = qCeil((width() - offsetX) / scaledCellSize) + 1;
    const qint64 startY = qFloor((-offsetY) / scaledCellSize) - 1;
    const qint64 endY   = qCeil((height() - offsetY) / scaledCellSize) + 1;

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
        redrawBufferZoomedOut(startCX, endCX, startCY, endCY);
    } else {
        // Render grid
        if (scaledCellSize >= 4) {
            painter.setPen(QPen(QColor(220, 220, 220), 0.5));
            for (qint64 x = startX; x <= endX; ++x) {
                qreal screenX = offsetX + static_cast<qreal>(x) * scaledCellSize;
                painter.drawLine(QPointF(screenX, offsetY + static_cast<qreal>(startY) * scaledCellSize),
                    QPointF(screenX, offsetY + static_cast<qreal>(endY) * scaledCellSize));
            }
            for (qint64 y = startY; y <= endY; ++y) {
                qreal screenY = offsetY + static_cast<qreal>(y) * scaledCellSize;
                painter.drawLine(QPointF(offsetX + static_cast<qreal>(startX) * scaledCellSize, screenY),
                    QPointF(offsetX + static_cast<qreal>(endX) * scaledCellSize, screenY));
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
                            int64_t globalX = (cx << CHUNK_SHIFT) + lx;
                            int64_t globalY = (cy << CHUNK_SHIFT) + ly;

                            if (globalX >= startX && globalX <= endX && globalY >= startY && globalY <= endY) {
                                QColor color = stateToColor(state);
                                long double screenX = offsetX + static_cast<qreal>(globalX) * scaledCellSize;
                                long double screenY = offsetY + static_cast<qreal>(globalY) * scaledCellSize;
                                painter.fillRect(QRectF(static_cast<qreal>(screenX), static_cast<qreal>(screenY),
                                    static_cast<qreal>(scaledCellSize), static_cast<qreal>(scaledCellSize)), color);
                            }
                        }
                    }
                }
            }
        }
    }


    if (!useOptimizedDrawing && statisticsEnabled && scaledCellSize >= 8 && currentStyle != JustColors) {
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
                    int64_t gx = (cx << CHUNK_SHIFT) + (i % CHUNK_SIZE);
                    int64_t gy = (cy << CHUNK_SHIFT) + (i / CHUNK_SIZE);

                    // Calculate screen position
                    QRectF cellRect(offsetX + static_cast<qreal>(gx) * scaledCellSize,
                                   offsetY + static_cast<qreal>(gy) * scaledCellSize,
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
                        auto drawCornerText = [&](const QRectF &rect, uint32_t value, Qt::Alignment align) {
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
                        if (rules.at(chunk->states[i]) != rules.at(nextState(chunk->states[i]))) {
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

    // Draw ant
    const qreal antScreenX = offsetX + static_cast<qreal>(antX) * scaledCellSize;
    const qreal antScreenY = offsetY + static_cast<qreal>(antY) * scaledCellSize;
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

void AntFieldWidget::redrawBufferZoomedOut(long long startCX, long long endCX, long long startCY, long long endCY) {
    QImage image(size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    auto* pixels = reinterpret_cast<std::atomic<uint32_t>*>(image.bits());
    const int imgWidth = image.width();
    const int imgHeight = image.height();
    const qreal scaledCellSize = cellSize * zoomFactor;

    std::vector<uint32_t> rgbColorCache;
    rgbColorCache.reserve(stateColorCache.size());
    for (const QColor &c : stateColorCache) rgbColorCache.push_back(c.rgb());
    if (rgbColorCache.empty()) rgbColorCache.push_back(QColor::fromHsv(0, 200, 230).rgb());

    QtConcurrent::blockingMap(activeChunkList, [=, &rgbColorCache](const ChunkData& data) {

        // 1. Thread-local culling
        // Every thread checks its own subset of chunks.
        if (data.cx < startCX || data.cx > endCX || data.cy < startCY || data.cy > endCY) {
            return;
        }

        const int64_t cx = data.cx;
        const int64_t cy = data.cy;
        const Chunk* chunk = data.chunk;

        // 2. Render logic
        for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                const uint32_t state = chunk->states[(ly << CHUNK_SHIFT) | lx];

                if (state > 0) {
                    const int64_t globalX = (cx << CHUNK_SHIFT) + lx;
                    const int64_t globalY = (cy << CHUNK_SHIFT) + ly;

                    const qint64 screenX = qFloor(offsetX + static_cast<qreal>(globalX) * scaledCellSize);
                    const qint64 screenY = qFloor(offsetY + static_cast<qreal>(globalY) * scaledCellSize);

                    if (screenX >= 0 && screenX < imgWidth && screenY >= 0 && screenY < imgHeight) {
                        const qint64 pixelIndex = screenY * imgWidth + screenX;
                        pixels[pixelIndex].store(rgbColorCache[state % rgbColorCache.size()], std::memory_order_relaxed);
                    }
                }
            }
        }
    });

    if (QPainter* activePainter = bufferPixmap.paintEngine()->painter()) {
        activePainter->drawImage(0, 0, image);
    }
}

QPoint64 AntFieldWidget::screenToField(const QPoint64 &screenPos) const {
    const double scaledCellSize = cellSize * zoomFactor;
    if (qFuzzyIsNull(scaledCellSize)) return {0, 0};
    return {qFloor((static_cast<qreal>(screenPos.x()) - offsetX) / scaledCellSize),
                  qFloor((static_cast<qreal>(screenPos.y()) - offsetY) / scaledCellSize)};
}

QPoint64 AntFieldWidget::fieldToScreen(const QPoint64 &fieldPos) const {
    const double scaledCellSize = cellSize * zoomFactor;
    return {qRound(static_cast<double>(fieldPos.x()) * scaledCellSize + offsetX),
                  qRound(static_cast<double>(fieldPos.y()) * scaledCellSize + offsetY)};
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

uint32_t AntFieldWidget::nextState(const uint32_t state) const {
    return (state + 1) % rules.length();
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

void AntFieldWidget::wheelEvent(QWheelEvent *event) {
    constexpr double zoomStep = 1.2;
    const double oldZoom = zoomFactor;
    double newZoom = zoomFactor;

    if (event->angleDelta().y() > 0) {
        newZoom *= zoomStep;
    } else {
        newZoom /= zoomStep;
    }

    if (newZoom < 0.00001 || newZoom > 50.0) return;

    const QPointF mousePos = event->position().toPoint();

    const double zoomRatio = newZoom / oldZoom;

    offsetX = mousePos.x() - (mousePos.x() - offsetX) * zoomRatio;
    offsetY = mousePos.y() - (mousePos.y() - offsetY) * zoomRatio;

    zoomFactor = newZoom;

    needsRedraw = true;
    update();

    emit zoomChanged(zoomFactor);
}

void AntFieldWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    needsRedraw = true;
}

void AntFieldWidget::centerOnPoint(qint64 x, qint64 y) {
    const double scaledCellSize = cellSize * zoomFactor;
    offsetX = width() / 2.0 - static_cast<qreal>(x) * scaledCellSize;
    offsetY = height() / 2.0 - static_cast<qreal>(y) * scaledCellSize;
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

void AntFieldWidget::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    // Start tracking mouse when it enters the widget
    updateMousePosition(event->position().toPoint());
}

void AntFieldWidget::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    // Clear coordinates when mouse leaves
    lastMouseCellPos = QPoint64(0, 0);
    emit mouseOverCell(0, 0);
}