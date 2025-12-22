#ifndef ANTFIELDWIDGET_H
#define ANTFIELDWIDGET_H

#include <QWidget>
#include <QString>
#include <QColor>
#include <QHash>
#include <QPair>
#include <QVector>
#include <QMutex>
#include <QElapsedTimer>
#include <QPoint>
#include <QMap>

// Forward declaration
struct AntStatisticsSummary;

class AntFieldWidget : public QWidget {
    Q_OBJECT

public:
    struct CellStatistics {
        int visitCount = 0;
        int lastVisitStep = 0;
        int firstVisitStep = 0;
    };

    explicit AntFieldWidget(QWidget *parent = nullptr);
    ~AntFieldWidget();

    void setRules(const QString &rules);
    void reset();
    void nextStep(int steps = 1);
    void setCellSize(int size);
    void setZoom(double zoom);
    double getZoom() const { return zoomFactor; }
    int getCellSize() const { return cellSize; }

    // View centering methods
    void centerOnAnt();
    void centerOnPoint(int x, int y);
    void centerOnPoint(const QPoint &point);
    void moveView(int dx, int dy);

    // Statistics methods
    int getVisitCount(int x, int y) const;
    QPoint getMostVisitedCell() const;
    int getTotalVisitedCells() const;
    int getUniqueVisitedCells() const;
    AntStatisticsSummary getStatisticsSummary() const;
    void exportStatisticsToCSV(const QString &filename) const;
    void resetStatistics();
    void setStatisticsEnabled(bool enabled);
    QVector<QPair<QPoint, int>> getTopVisitedCells(int count) const;

    // Performance optimized methods
    QVector<QPoint> getRecentlyVisitedCells() const { return recentlyVisitedCells; }

signals:
    void antMoved(int x, int y, int direction, int steps);
    void zoomChanged(double zoom);
    void stepsChanged(int steps);
    void statisticsUpdated(const AntStatisticsSummary &summary);
    void cellVisited(const QPoint &cell, int visitCount);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Coordinate conversion
    QPoint screenToField(const QPoint &screenPos) const;
    QPoint fieldToScreen(const QPoint &fieldPos) const;

    // Drawing and state management
    void redrawBuffer();
    void updateStateColors();
    QColor stateToColor(int state) const;

    // Optimized statistics update
    void updateStatistics(int x, int y);
    void updateMostVisitedCell(int x, int y, int newVisits);

    // Performance-optimized cell storage
    QHash<QPair<int, int>, quint8> cells;

    // Optimized statistics: use QMap for ordered access when needed
    QHash<QPair<int, int>, CellStatistics> cellStatistics;
    QVector<QColor> stateColorCache;

    // Ant state
    int antX = 0, antY = 0;
    int antDir = 0; // 0=up, 1=right, 2=down, 3=left
    int stepCount = 0;

    // Grid bounds
    int minX = -50, maxX = 50;
    int minY = -50, maxY = 50;

    // View state
    double offsetX = 0, offsetY = 0;
    double zoomFactor = 1.0;
    int cellSize = 6;

    // Statistics state - simplified and optimized
    QPoint mostVisitedCell;
    int maxVisits = 0;
    int uniqueCellsCount = 0;
    bool statisticsEnabled = true;
    mutable QMutex statisticsMutex;
    QElapsedTimer simulationTimer;

    // Performance optimization: track recently visited cells for faster drawing
    QVector<QPoint> recentlyVisitedCells;
    static const int RECENT_CELLS_BUFFER_SIZE = 1000;

    // Interaction state
    bool dragging = false;
    QPoint lastMousePos;

    // Rendering optimization
    bool needsRedraw = true;
    QPixmap bufferPixmap;

    // Rules
    QString rules;
};

inline uint qHash(const QPair<int, int> &key, uint seed = 0) {
    return qHash(key.first ^ (key.second << 16), seed);
}

struct AntStatisticsSummary {
    int totalCellsVisited = 0;
    int maxVisitsPerCell = 0;
    QPoint mostVisitedCell;
    double averageVisits = 0.0;
    int uniqueCellsVisited = 0;
    qint64 simulationTimeMs = 0;
};

#endif // ANTFIELDWIDGET_H
