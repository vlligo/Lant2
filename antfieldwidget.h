#ifndef ANTFIELDWIDGET_H
#define ANTFIELDWIDGET_H

#include <QWidget>
#include <QString>
#include <QColor>
#include <QHash>
#include <QVector>
#include <QMutex>
#include <QElapsedTimer>
#include <QTimer>
#include "QPoint64.h"
#include <unordered_map>
#include <vector>
#include <memory>


struct ChunkKey {
    int64_t cx, cy;
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
constexpr int CHUNK_SHIFT = 6;              // 2^6 = 64
constexpr int64_t CHUNK_SIZE = (1<<CHUNK_SHIFT);
constexpr int64_t CHUNK_MASK = CHUNK_SIZE - 1;
constexpr int CHUNK_AREA = CHUNK_SIZE * CHUNK_SIZE;


// --- Chunk Structures ---
// We use uint32_t for state to comfortably hold thousands of rules, avoiding int8_t limits
struct Chunk {
    uint32_t states[CHUNK_AREA] = {0};
};

struct StatChunk {
    uint32_t visits[CHUNK_AREA] = {0};
    uint32_t corners[CHUNK_AREA][4] = {0};
    int64_t firstVisitStep[CHUNK_AREA];
    int64_t lastVisitStep[CHUNK_AREA] = {0};

    StatChunk() {
        std::fill_n(firstVisitStep, CHUNK_AREA, -1);
    }
};

// Now the compiler knows how to delete them
inline std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash> chunks;
inline std::unordered_map<ChunkKey, std::unique_ptr<StatChunk>, ChunkKeyHash> statChunks;

inline std::vector<uint32_t> nextStateLUT;
inline std::vector<int> directionChangeLUT;

// Forward declaration
struct AntStatisticsSummary;

class AntFieldWidget : public QWidget {
    Q_OBJECT

public:
    enum DisplayStyle {
        JustColors,
        Visits,
        Rotations
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

    void setRules(const QString &rules);
    void reset();
    void nextStep(qint64 steps = 1);
    void setCellSize(int size);
    void setZoom(double zoom);
    double getZoom() const { return zoomFactor; }
    int getCellSize() const { return cellSize; }

    // View centering methods
    void centerOnAnt();
    void centerOnPoint(qint64 x, qint64 y);
    void centerOnPoint(const QPoint64 &point);
    void moveView(qint64 dx, qint64 dy);

    // Statistics methods
    int getVisitCount(qint64 x, qint64 y) const;
    QPoint64 getMostVisitedCell() const;
    qint64 getTotalVisitedCells() const;
    qint64 getUniqueVisitedCells() const;
    AntStatisticsSummary getStatisticsSummary() const;
    void exportStatisticsToCSV(const QString &filename) const;
    void resetStatistics();
    void setStatisticsEnabled(bool enabled);
    QVector<QPair<QPoint64, qint64>> getTopVisitedCells(int count) const;

    // Performance optimized methods
    QVector<QPoint64> getRecentlyVisitedCells() const { return recentlyVisitedCells; }

public slots:
    void setDisplayStyle(DisplayStyle style);

signals:
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
    QPoint64 fieldToScreen(const QPoint64 &fieldPos) const;

    // Drawing and state management
    void redrawBuffer();
    void updateStateColors();
    QColor stateToColor(int state) const;


    // Mouse position tracking
    void updateMousePosition(const QPoint& pos);  // Add this method

    // Performance-optimized cell storage
    // QHash<QPair<qint64, qint64>, int> cells;

    // Optimized statistics: use QMap for ordered access when needed
    // QHash<QPair<qint64, qint64>, CellStatistics> cellStatistics;
    QVector<QColor> stateColorCache;

    // Ant state
    qint64 antX = 0, antY = 0;
    int antDir = 0; // 0=up, 1=right, 2=down, 3=left
    qint64 stepCount = 0;

    // Grid bounds
    int minX = -50, maxX = 50;
    int minY = -50, maxY = 50;


    // View state
    long double offsetX = 0, offsetY = 0;
    double zoomFactor = 1.0;
    int cellSize = 6;

    // Statistics state - simplified and optimized
    QPoint64 mostVisitedCell;
    qint64 maxVisits = 0;
    qint64 uniqueCellsCount = 0;
    bool statisticsEnabled = true;
    mutable QMutex statisticsMutex;
    QElapsedTimer simulationTimer;

    // Performance optimization: track recently visited cells for faster drawing
    QVector<QPoint64> recentlyVisitedCells;
    static constexpr int RECENT_CELLS_BUFFER_SIZE = 1000;

    struct BatchData {
        qint64 visits = 0;
        int corners[4] = {0, 0, 0, 0};
        qint64 firstVisitStep = -1; // Track exact step
        qint64 lastVisitStep = -1;  // Track exact step
    };

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