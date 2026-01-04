#include "antfieldwidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QtMath>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <algorithm>
#include <QMutexLocker>
#include <QTimer>
#include <QCursor>  // Add this include

AntFieldWidget::AntFieldWidget(QWidget *parent)
    : QWidget(parent) {
    setMinimumSize(400, 400);
    setMouseTracking(true);
    reset();

    // Mouse update timer for smoother coordinate updates
    mouseUpdateTimer = new QTimer(this);
    mouseUpdateTimer->setInterval(50);  // Update every 50ms
    connect(mouseUpdateTimer, &QTimer::timeout, [this]() {
        if (underMouse()) {
            updateMousePosition(mapFromGlobal(QCursor::pos()));
        }
    });
    mouseUpdateTimer->start();
}

AntFieldWidget::~AntFieldWidget() {
    if (mouseUpdateTimer) {
        mouseUpdateTimer->stop();
        delete mouseUpdateTimer;
    }
    cells.clear();
    cellStatistics.clear();
    stateColorCache.clear();
    recentlyVisitedCells.clear();
}

void AntFieldWidget::setRules(const QString &rules) {
    this->rules = rules.trimmed().toUpper();
    updateStateColors();
    reset();
}

void AntFieldWidget::updateStateColors() {
    stateColorCache.clear();
    int maxStates = qMax(2, rules.length());

    for (int state = 0; state < maxStates; ++state) {
        float ratio = static_cast<float>(state) / (maxStates - 1);
        int hue = static_cast<int>(ratio * 360) % 360;
        stateColorCache.append(QColor::fromHsv(hue, 200, 230));
    }
}

void AntFieldWidget::reset() {
    cells.clear();
    if (statisticsEnabled) {
        QMutexLocker locker(&statisticsMutex);
        cellStatistics.clear();
        recentlyVisitedCells.clear();
        mostVisitedCell = QPoint(0, 0);
        maxVisits = 0;
        uniqueCellsCount = 0;
    }

    antX = antY = 0;
    antDir = 0;
    stepCount = 0;
    minX = minY = -50;
    maxX = maxY = 50;

    // Center view
    offsetX = width() / 2.0;
    offsetY = height() / 2.0;

    // Start/restart simulation timer
    simulationTimer.restart();

    needsRedraw = true;
    update();

    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
}

void AntFieldWidget::nextStep(int steps) {
    if (rules.isEmpty() || steps <= 0) return;

    // Performance: batch updates
    setUpdatesEnabled(false);

    const int ruleLength = rules.length();
    static const QVector<QPoint> directions = {
        QPoint(0, -1),  // Up
        QPoint(1, 0),   // Right
        QPoint(0, 1),   // Down
        QPoint(-1, 0)   // Left
    };

    // Batch statistics updates for performance
    QHash<QPair<int, int>, int> batchUpdates;

    for (int s = 0; s < steps; ++s) {
        QPair<int, int> cellKey(antX, antY);
        quint8 &currentState = cells[cellKey];

        if (currentState < ruleLength) {
            QChar rule = rules.at(currentState);
            currentState = (currentState + 1) % ruleLength;

            // Update direction
            switch (rule.unicode()) {
            case 'L': antDir = (antDir + 3) % 4; break;
            case 'R': antDir = (antDir + 1) % 4; break;
            case 'F': antDir = (antDir + 2) % 4; break;
            case 'B': break;
            }
        }

        // Batch statistics update for performance
        if (statisticsEnabled) {
            batchUpdates[cellKey]++;
        }

        // Move ant
        const QPoint &dir = directions[antDir];
        antX += dir.x();
        antY += dir.y();

        // Expand bounds (do this less frequently for performance)
        if (antX < minX) minX = antX;
        if (antX > maxX) maxX = antX;
        if (antY < minY) minY = antY;
        if (antY > maxY) maxY = antY;

        stepCount++;
    }

    // Apply batched statistics updates
    if (statisticsEnabled && !batchUpdates.isEmpty()) {
        QMutexLocker locker(&statisticsMutex);

        for (auto it = batchUpdates.constBegin(); it != batchUpdates.constEnd(); ++it) {
            QPair<int, int> cellKey = it.key();
            int visitsToAdd = it.value();

            CellStatistics &stats = cellStatistics[cellKey];

            // Initialize first visit
            if (stats.visitCount == 0) {
                stats.firstVisitStep = stepCount - visitsToAdd + 1;
                uniqueCellsCount++;
            }

            // Update visit count
            stats.visitCount += visitsToAdd;
            stats.lastVisitStep = stepCount;

            // Track recently visited cells for faster drawing
            recentlyVisitedCells.append(QPoint(cellKey.first, cellKey.second));
            if (recentlyVisitedCells.size() > RECENT_CELLS_BUFFER_SIZE) {
                recentlyVisitedCells.remove(0, recentlyVisitedCells.size() - RECENT_CELLS_BUFFER_SIZE);
            }

            // Update max visits if needed
            if (stats.visitCount > maxVisits) {
                maxVisits = stats.visitCount;
                mostVisitedCell = QPoint(cellKey.first, cellKey.second);
            }
        }
    }

    setUpdatesEnabled(true);
    needsRedraw = true;
    update();

    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
}

int AntFieldWidget::getVisitCount(int x, int y) const {
    QMutexLocker locker(&statisticsMutex);
    auto it = cellStatistics.constFind(QPair<int, int>(x, y));
    return (it != cellStatistics.constEnd()) ? it->visitCount : 0;
}

QPoint AntFieldWidget::getMostVisitedCell() const {
    QMutexLocker locker(&statisticsMutex);
    return mostVisitedCell;
}

int AntFieldWidget::getTotalVisitedCells() const {
    return stepCount;
}

int AntFieldWidget::getUniqueVisitedCells() const {
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
        summary.averageVisits = static_cast<double>(summary.totalCellsVisited) / summary.uniqueCellsVisited;
    }

    return summary;
}

QVector<QPair<QPoint, int>> AntFieldWidget::getTopVisitedCells(int count) const {
    QMutexLocker locker(&statisticsMutex);

    if (cellStatistics.isEmpty()) {
        return QVector<QPair<QPoint, int>>();
    }

    // Use a simple selection algorithm for top N cells (more efficient than full sort for large datasets)
    QVector<QPair<QPoint, int>> result;
    result.reserve(qMin(count, cellStatistics.size()));

    // If count is small relative to total, use a partial sort approach
    if (count * 10 < cellStatistics.size()) {
        // Copy first 'count' items
        auto it = cellStatistics.constBegin();
        for (int i = 0; i < count && it != cellStatistics.constEnd(); ++i, ++it) {
            result.append(qMakePair(QPoint(it.key().first, it.key().second), it->visitCount));
        }

        // Sort and maintain top N
        std::sort(result.begin(), result.end(),
                  [](const QPair<QPoint, int> &a, const QPair<QPoint, int> &b) {
                      return a.second > b.second;
                  });

        // Process remaining items
        for (; it != cellStatistics.constEnd(); ++it) {
            int visits = it->visitCount;
            if (visits > result.last().second) {
                // Insert in sorted position
                auto pos = std::lower_bound(result.begin(), result.end(), visits,
                                            [](const QPair<QPoint, int> &item, int value) {
                                                return item.second > value;
                                            });
                if (pos != result.end()) {
                    result.insert(pos, qMakePair(QPoint(it.key().first, it.key().second), visits));
                    result.removeLast();
                }
            }
        }
    } else {
        // Full sort for smaller datasets
        for (auto it = cellStatistics.constBegin(); it != cellStatistics.constEnd(); ++it) {
            result.append(qMakePair(QPoint(it.key().first, it.key().second), it->visitCount));
        }

        std::sort(result.begin(), result.end(),
                  [](const QPair<QPoint, int> &a, const QPair<QPoint, int> &b) {
                      return a.second > b.second;
                  });

        if (result.size() > count) {
            result.resize(count);
        }
    }

    return result;
}

void AntFieldWidget::exportStatisticsToCSV(const QString &filename) const {
    QMutexLocker locker(&statisticsMutex);

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file for writing:" << filename;
        return;
    }

    QTextStream out(&file);
    out << "X,Y,VisitCount,FirstVisitStep,LastVisitStep\n";

    for (auto it = cellStatistics.constBegin(); it != cellStatistics.constEnd(); ++it) {
        out << it.key().first << ","
            << it.key().second << ","
            << it->visitCount << ","
            << it->firstVisitStep << ","
            << it->lastVisitStep << "\n";
    }

    file.close();
}

void AntFieldWidget::resetStatistics() {
    QMutexLocker locker(&statisticsMutex);
    cellStatistics.clear();
    recentlyVisitedCells.clear();
    mostVisitedCell = QPoint(0, 0);
    maxVisits = 0;
    uniqueCellsCount = 0;
    simulationTimer.restart();
}

void AntFieldWidget::setStatisticsEnabled(bool enabled) {
    statisticsEnabled = enabled;
    if (!enabled) {
        resetStatistics();
    }
}

void AntFieldWidget::setCellSize(int size) {
    cellSize = qBound(1, size, 50);
    needsRedraw = true;
    update();
}

void AntFieldWidget::setZoom(double zoom) {
    // Get the current center point of the widget in field coordinates
    QPoint centerPoint = screenToField(QPoint(width() / 2, height() / 2));

    // Store the current screen position of this center point
    QPoint oldCenterScreen = fieldToScreen(centerPoint);

    // Set the new zoom factor
    zoomFactor = qBound(0.1, zoom, 20.0);

    // Calculate where the same field point should be on screen after zoom
    QPoint newCenterScreen = fieldToScreen(centerPoint);

    // Adjust offset to keep the same field point at the center
    offsetX += oldCenterScreen.x() - newCenterScreen.x();
    offsetY += oldCenterScreen.y() - newCenterScreen.y();

    needsRedraw = true;
    update();
    emit zoomChanged(zoomFactor);
}

void AntFieldWidget::centerOnAnt() {
    double scaledCellSize = cellSize * zoomFactor;
    offsetX = width() / 2.0 - antX * scaledCellSize;
    offsetY = height() / 2.0 - antY * scaledCellSize;
    needsRedraw = true;
    update();
}

void AntFieldWidget::moveView(int dx, int dy) {
    offsetX += dx;
    offsetY += dy;
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
    bufferPixmap.fill(QColor(240, 240, 240));

    QPainter painter(&bufferPixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const double scaledCellSize = cellSize * zoomFactor;

    if (scaledCellSize < 0.5) {
        painter.setPen(Qt::red);
        painter.drawText(rect(), Qt::AlignCenter, "Zoomed out too far");
        return;
    }

    // Calculate visible bounds
    const int startX = qFloor((-offsetX) / scaledCellSize) - 1;
    const int endX = qCeil((width() - offsetX) / scaledCellSize) + 1;
    const int startY = qFloor((-offsetY) / scaledCellSize) - 1;
    const int endY = qCeil((height() - offsetY) / scaledCellSize) + 1;

    // Clip to actual bounds
    const int drawStartX = qMax(startX, minX);
    const int drawEndX = qMin(endX, maxX + 1);
    const int drawStartY = qMax(startY, minY);
    const int drawEndY = qMin(endY, maxY + 1);

    // Draw grid if cells are large enough
    if (scaledCellSize >= 8) {
        painter.setPen(QPen(QColor(220, 220, 220), 1));

        for (int x = drawStartX; x <= drawEndX; ++x) {
            double screenX = offsetX + x * scaledCellSize;
            painter.drawLine(QPointF(screenX, offsetY + drawStartY * scaledCellSize),
                             QPointF(screenX, offsetY + drawEndY * scaledCellSize));
        }

        for (int y = drawStartY; y <= drawEndY; ++y) {
            double screenY = offsetY + y * scaledCellSize;
            painter.drawLine(QPointF(offsetX + drawStartX * scaledCellSize, screenY),
                             QPointF(offsetX + drawEndX * scaledCellSize, screenY));
        }
    }

    // Draw all cells
    for (int y = drawStartY; y < drawEndY; ++y) {
        for (int x = drawStartX; x < drawEndX; ++x) {
            QPair<int, int> cellKey(x, y);
            auto it = cells.constFind(cellKey);
            QColor color;

            if (it != cells.constEnd()) {
                // Cell exists in the grid
                int state = it.value();
                color = (state > 0) ? stateToColor(state) : Qt::white;
            } else {
                // Check if cell was visited (for statistics)
                if (statisticsEnabled) {
                    // Check if this cell has been visited at all
                    QMutexLocker locker(&statisticsMutex);
                    auto statIt = cellStatistics.constFind(cellKey);
                    if (statIt != cellStatistics.constEnd()) {
                        // Cell has been visited, but has state 0 (white)
                        color = Qt::white;
                    } else {
                        // Cell not visited, use background color
                        color = QColor(240, 240, 240);
                    }
                } else {
                    color = QColor(240, 240, 240);
                }
            }

            double screenX = offsetX + x * scaledCellSize;
            double screenY = offsetY + y * scaledCellSize;
            painter.fillRect(QRectF(screenX, screenY,
                                    scaledCellSize, scaledCellSize), color);
        }
    }

    // Draw visit counts for ALL visited cells in the visible area
    if (scaledCellSize >= 8 && statisticsEnabled) {
        painter.setPen(Qt::black);

        // Adjust font size based on cell size
        if (scaledCellSize < 12) {
            painter.setFont(QFont("Arial", 6));
        } else {
            painter.setFont(QFont("Arial", 8));
        }

        // Get a copy of cellStatistics while holding the mutex
        QHash<QPair<int, int>, CellStatistics> statsCopy;
        {
            QMutexLocker locker(&statisticsMutex);
            statsCopy = cellStatistics;
        }

        // Now iterate through all visited cells and draw labels for those in visible area
        for (auto it = statsCopy.constBegin(); it != statsCopy.constEnd(); ++it) {
            int x = it.key().first;
            int y = it.key().second;
            int visits = it->visitCount;

            // Skip if not in visible area
            if (x < drawStartX || x >= drawEndX || y < drawStartY || y >= drawEndY) {
                continue;
            }

            // Draw visit count for ALL visited cells (even with 1 visit)
            if (visits > 0) {
                double screenX = offsetX + x * scaledCellSize;
                double screenY = offsetY + y * scaledCellSize;

                // Check if the cell is white (state 0)
                auto cellIt = cells.constFind(QPair<int, int>(x, y));
                bool isWhiteCell = (cellIt == cells.constEnd() || cellIt.value() == 0);

                if (isWhiteCell) {
                    // For white cells, draw a subtle background to make text readable
                    painter.setBrush(QColor(245, 245, 245, 200));
                    painter.setPen(Qt::NoPen);
                    painter.drawRect(QRectF(screenX, screenY,
                                            scaledCellSize, scaledCellSize));
                    painter.setPen(Qt::black);
                }

                painter.drawText(QRectF(screenX, screenY, scaledCellSize, scaledCellSize),
                                 Qt::AlignCenter, QString::number(visits));
            }
        }
    }

    // Draw ant
    const double antScreenX = offsetX + antX * scaledCellSize;
    const double antScreenY = offsetY + antY * scaledCellSize;
    const QPointF antCenter(antScreenX + scaledCellSize / 2,
                            antScreenY + scaledCellSize / 2);

    if (scaledCellSize >= 2) {
        painter.setBrush(Qt::red);
        painter.setPen(QPen(Qt::black, 1));

        const double radius = scaledCellSize * 0.4;
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

QPoint AntFieldWidget::screenToField(const QPoint &screenPos) const {
    const double scaledCellSize = cellSize * zoomFactor;
    if (qFuzzyIsNull(scaledCellSize)) return QPoint(0, 0);
    return QPoint(qRound((screenPos.x() - offsetX) / scaledCellSize),
                  qRound((screenPos.y() - offsetY) / scaledCellSize));
}

QPoint AntFieldWidget::fieldToScreen(const QPoint &fieldPos) const {
    const double scaledCellSize = cellSize * zoomFactor;
    return QPoint(qRound(fieldPos.x() * scaledCellSize + offsetX),
                  qRound(fieldPos.y() * scaledCellSize + offsetY));
}

QColor AntFieldWidget::stateToColor(int state) const {
    if (stateColorCache.isEmpty()) {
        return QColor::fromHsv(0, 200, 230);
    }
    return stateColorCache[state % stateColorCache.size()];
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
        QPoint delta = event->pos() - lastMousePos;
        offsetX += delta.x();
        offsetY += delta.y();
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
    const double zoomChange = 1.1;
    const QPointF mousePos = event->position();
    const QPointF fieldPos = screenToField(mousePos.toPoint());

    if (event->angleDelta().y() > 0) {
        zoomFactor = qMin(20.0, zoomFactor * zoomChange);
    } else {
        zoomFactor = qMax(0.1, zoomFactor / zoomChange);
    }

    const QPointF newMousePos = fieldToScreen(fieldPos.toPoint());
    offsetX += mousePos.x() - newMousePos.x();
    offsetY += mousePos.y() - newMousePos.y();

    needsRedraw = true;
    update();
    emit zoomChanged(zoomFactor);
}

void AntFieldWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    needsRedraw = true;
}

void AntFieldWidget::centerOnPoint(int x, int y) {
    double scaledCellSize = cellSize * zoomFactor;
    offsetX = width() / 2.0 - x * scaledCellSize;
    offsetY = height() / 2.0 - y * scaledCellSize;
    needsRedraw = true;
    update();
}

void AntFieldWidget::centerOnPoint(const QPoint &point) {
    centerOnPoint(point.x(), point.y());
}

void AntFieldWidget::updateMousePosition(const QPoint &pos) {
    QPoint fieldPos = screenToField(pos);

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
    lastMouseCellPos = QPoint(INT_MAX, INT_MAX);
    emit mouseOverCell(INT_MAX, INT_MAX);
}
