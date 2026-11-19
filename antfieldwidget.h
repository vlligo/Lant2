#ifndef ANTFIELDWIDGET_H
#define ANTFIELDWIDGET_H

#include "highwayanalysis.h"
#include <QBitArray>
#include <QWidget>
#include <QString>
#include <QColor>
#include <QVector>
#include <QMutex>
#include <QElapsedTimer>
#include <QTimer>
#include <QFutureWatcher>
#include <atomic>
#include "QPoint64.h"
#include <unordered_map>
#include <vector>
#include <array>
#include <memory>


struct ChunkKey {
    qint64 cx, cy;
    bool operator==(const ChunkKey& o) const { return cx == o.cx && cy == o.cy; }
};

struct ChunkKeyHash {
    std::size_t operator()(const ChunkKey& k) const {
        uint64_t h = 0xcbf29ce484222325ULL;
        h ^= static_cast<uint64_t>(k.cx); h *= 0x100000001b3ULL;
        h ^= static_cast<uint64_t>(k.cy); h *= 0x100000001b3ULL;
        return static_cast<std::size_t>(h);
    }
};

// --- Architecture Constants ---
constexpr int CHUNK_SHIFT = 5;              // 2^5 = 32
constexpr int64_t CHUNK_SIZE = (1<<CHUNK_SHIFT);
constexpr int64_t CHUNK_MASK = CHUNK_SIZE - 1;
constexpr int CHUNK_AREA = CHUNK_SIZE * CHUNK_SIZE;


// --- Chunk Structures ---
// We use uint32_t for state to comfortably hold thousands of rules, avoiding int8_t limits
struct Chunk {
    uint32_t states[CHUNK_AREA] = {0};
    std::array<quint8, CHUNK_AREA / 8> readMask{};
    bool wasRead(int index) const { return readMask[index / 8] & (1u << (index % 8)); }
    void markRead(int index) { readMask[index / 8] |= 1u << (index % 8); }
};

constexpr int cachedRankCount = 20;

struct StatChunk {
    qint64 visits[CHUNK_AREA] = {0};
    qint64 corners[CHUNK_AREA][4]{};
    qint64 firstVisitStep[CHUNK_AREA]{};
    qint64 lastVisitStep[CHUNK_AREA] = {0};

    qint64 maxVisits = 0;
    std::array<int, cachedRankCount> topIndices{};
    int topCount = 0;
    bool rankingDirty = true;

    void refreshRanking();

    StatChunk() {
        std::fill_n(firstVisitStep, CHUNK_AREA, -1);
    }
};

struct ChunkData {
    int64_t cx, cy;
    Chunk* chunk;
};


// Forward declaration
struct AntStatisticsSummary;

class AntFieldWidget : public QWidget {
    Q_OBJECT

public:
    enum DisplayStyle {
        JustColors,
        Visits,
        Rotations,
        Arcs,
        Diagonals
    };
    Q_ENUM(DisplayStyle)

    struct CellStatistics {
        qint64 visitCount = 0;
        qint64 lastVisitStep = 0;
        qint64 firstVisitStep = 0;
        // Index 0: Top-Left (Left<->Top)
        // Index 1: Top-Right (Top<->Right)
        // Index 2: Bottom-Right (Right<->Bottom)
        // Index 3: Bottom-Left (Bottom<->Left)
        qint64 cornerCounts[4] = {0, 0, 0, 0};
    };

    explicit AntFieldWidget(QWidget *parent = nullptr);
    ~AntFieldWidget() override;

    void setRules(const QString &rules_str);
    const QString &expandedRules() const { return rules; }
    const QBitArray &diagonalStates() const { return diagonalStateMask; }
    QColor cellColor(uint32_t state) const;
    void setDiagonalStates(const QBitArray &states);
    std::optional<HighwayAnalysis> analyzeHighway() const;
    void startHighwayCheck();
    void showHighwayWindow(const HighwayAnalysis &highway);
    void clearHighwayWindow();
    bool hasCompleteReadHistory() const { return completeReadHistory; }
    void reset();
    void nextStep(qint64 steps = 1);
    qint64 advanceSteps(qint64 limit, int timeBudgetMs);
    void publishPosition();
    void setCellSize(int size);
    void setZoom(double zoom);
    void randomizeArea(qint64 radius);
    static qint64 estimateRandomizeAreaBytes(qint64 radius) ;
    double getZoom() const { return zoomFactor; }
    int getCellSize() const { return cellSize; }

    // View centering methods
    void centerOnAnt();
    void centerOnPoint(qint64 x, qint64 y);
    void centerOnPoint(const QPoint64 &point);
    void moveView(qint64 dx, qint64 dy);

    // Statistics methods
    int64_t getVisitCount(qint64 x, qint64 y) const;
    QPoint64 getMostVisitedCell() const;
    qint64 getTotalVisitedCells() const;
    qint64 getUniqueVisitedCells() const;
    AntStatisticsSummary getStatisticsSummary() const;
    bool exportStatistics(const QString &filename) const;
    bool saveState(const QString &filename) const;
    bool loadState(const QString &filename);
    QString getRules() const;
    bool isStatisticsEnabled() const { return statisticsEnabled; }
    void resetStatistics();
    void setStatisticsEnabled(bool enabled);
    QVector<QPair<QPoint64, qint64>> getTopVisitedCells(int count) const;

public slots:
    void setDisplayStyle(DisplayStyle style);

signals:
    void highwayWindowCleared();
    void highwayChecked(int period);
    void stateAboutToLoad();
    void antMoved(qint64 x, qint64 y, int direction, qint64 steps);
    void zoomChanged(double zoom);
    void stepsChanged(qint64 steps);
    void statisticsUpdated(const AntStatisticsSummary &summary);
    void cellVisited(const QPoint64 &cell, qint64 visitCount);
    void mouseOverCell(qint64 x, qint64 y);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override;
#else
    void enterEvent(QEvent *event) override;
#endif
    void leaveEvent(QEvent *event) override;

private:
    // Coordinate conversion
    QPoint64 screenToField(const QPoint64 &screenPos) const;
    double screenX(qint64 x) const;
    double screenY(qint64 y) const;

    // Drawing and state management
    std::optional<HighwayAnalysis> findHighway(const std::atomic_bool *cancel) const;
    void cancelHighwayCheck();
    void advanceBlock(qint64 steps);
    void redrawBuffer();
    void redrawBufferZoomedOut(QPainter &painter, qint64 startCX, qint64 endCX, qint64 startCY, qint64 endCY);
    struct CompiledRules;
    void installRules(CompiledRules &&compiled);
    QColor stateToColor(uint32_t state) const;
    uint32_t previousState(uint32_t state) const;
    int recolorAndGetTurn(uint32_t &state) const;


    // Mouse position tracking
    void updateMousePosition(const QPoint& pos);

    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash> chunks;
    std::unordered_map<ChunkKey, std::unique_ptr<StatChunk>, ChunkKeyHash> statChunks;
    std::vector<ChunkData> activeChunkList;

    std::vector<uint32_t> nextStateLUT;
    std::vector<int> directionChangeLUT;

    // Performance-optimized cell storage
    // QHash<QPair<qint64, qint64>, int> cells;

    // Optimized statistics: use QMap for ordered access when needed
    // QHash<QPair<qint64, qint64>, CellStatistics> cellStatistics;
    QVector<QColor> stateColorCache;

    QFutureWatcher<std::optional<HighwayAnalysis>> *highwayJob = nullptr;
    std::atomic_bool cancelHighway{false};
    std::vector<HighwayWindowCell> highlightedWindow;
    bool completeReadHistory = true;
    qint64 lastBoardEdit = 0;
    std::unordered_map<ChunkKey, decltype(Chunk::readMask), ChunkKeyHash> readsAtEdit;

    // Ant state
    qint64 antX = 0, antY = 0;
    int antDir = 0; // 0=up, 1=right, 2=down, 3=left
    qint64 stepCount = 0;

    // Grid bounds
    qint64 minX = -50, maxX = 50;
    qint64 minY = -50, maxY = 50;


    // View state
    QPoint64 viewOrigin;
    qreal offsetX = 0, offsetY = 0;
    double zoomFactor = 1.0;
    int cellSize = 6;

    // Statistics state - simplified and optimized
    QPoint64 mostVisitedCell;
    qint64 trackedVisits = 0;
    qint64 maxVisits = 0;
    qint64 uniqueCellsCount = 0;
    bool statisticsEnabled = true;
    mutable QVector<QPair<QPoint64, qint64>> cachedTopCells;
    mutable bool rankingDirty = true;
    mutable QMutex statisticsMutex;
    QElapsedTimer simulationTimer;

    static constexpr int RECENT_CELLS_BUFFER_SIZE = 1000;

    // Interaction state
    bool dragging = false;
    QPoint64 lastMousePos;

    // Mouse tracking
    QPoint64 lastMouseCellPos;
    QTimer *mouseUpdateTimer;  // Add this line

    // Rendering optimization
    bool needsRedraw = true;
    QPixmap bufferPixmap;

    // Rules
    QString rules;
    QBitArray diagonalStateMask;

    // Display style
    DisplayStyle currentStyle = JustColors;
};

// inline uint qHash(const QPair<qint64, qint64> &key, uint seed = 0) {
//     return qHash(key.first ^ (key.second << 16), seed);
// }

struct AntStatisticsSummary {
    qint64 totalCellsVisited = 0;
    qint64 maxVisitsPerCell = 0;
    QPoint64 mostVisitedCell;
    double averageVisits = 0.0;
    qint64 uniqueCellsVisited = 0;
    qint64 simulationTimeMs = 0;
};

#endif // ANTFIELDWIDGET_H