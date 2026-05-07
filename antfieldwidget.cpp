#include "antfieldwidget.h"
#include <QPainter>
#include <QWheelEvent>
#include <QApplication>
#include <QtMath>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <QMutexLocker>
#include <QTimer>
#include <QCursor>

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
        const float ratio = static_cast<float>(state) / (maxStates > 1 ? (maxStates - 1) : 1);
        const int hue = static_cast<int>(ratio * 360) % 360;
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

void AntFieldWidget::nextStep(const qint64 steps) {
    if (rules.isEmpty() || steps <= 0) return;

    // Performance: batch updates
    setUpdatesEnabled(false);

    const qint64 ruleLength = rules.length();
    static const QVector<QPoint> directions = {
        QPoint(0, -1),  // Up
        QPoint(1, 0),   // Right
        QPoint(0, 1),   // Down
        QPoint(-1, 0)   // Left
    };

    // Batch statistics updates for performance
    struct LocalBatchData {
        qint64 visits = 0;
        qint64 corners[4] = {0, 0, 0, 0};
        qint64 firstVisitStep = -1;
        qint64 lastVisitStep = -1;
    };
    QHash<QPair<qint64, qint64>, LocalBatchData> batchUpdates;

    for (qint64 s = 0; s < steps; ++s) {
        QPair<qint64, qint64> cellKey(antX, antY);
        int &currentState = cells[cellKey];
        int oldDir = antDir;

        // Logic
        if (currentState < ruleLength) {
            QChar rule = rules.at(currentState);
            currentState = (currentState + 1) % ruleLength;
            switch (rule.unicode()) {
            case 'L': antDir = (antDir + 3) % 4; break;
            case 'R': antDir = (antDir + 1) % 4; break;
            }
        }

        // Corner Logic
        int entrySide = (oldDir + 2) % 4;
        int exitSide = antDir;
        int cornerIndex = -1;

        bool hasTop = (entrySide == 0 || exitSide == 0);
        bool hasRight = (entrySide == 1 || exitSide == 1);
        bool hasBottom = (entrySide == 2 || exitSide == 2);
        bool hasLeft = (entrySide == 3 || exitSide == 3);

        if (hasTop && hasLeft) cornerIndex = 0;
        else if (hasTop && hasRight) cornerIndex = 1;
        else if (hasBottom && hasRight) cornerIndex = 2;
        else if (hasBottom && hasLeft) cornerIndex = 3;

        qint64 currentStepVal = stepCount + 1;

        if (statisticsEnabled) {
            LocalBatchData &data = batchUpdates[cellKey];
            data.visits++;
            if (data.firstVisitStep == -1) {
                data.firstVisitStep = currentStepVal;
            }
            data.lastVisitStep = currentStepVal; // Always update last visit to current

            if (cornerIndex != -1) {
                data.corners[cornerIndex]++;
            }
        }

        const QPoint &dir = directions[antDir];
        antX += dir.x();
        antY += dir.y();

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
            QPair<qint64, qint64> cellKey = it.key();
            const LocalBatchData &data = it.value();
            CellStatistics &stats = cellStatistics[cellKey];

            if (stats.visitCount == 0) {
                stats.firstVisitStep = data.firstVisitStep;
                uniqueCellsCount++;
            }

            stats.visitCount += data.visits;
            stats.lastVisitStep = data.lastVisitStep;

            for (int i = 0; i < 4; i++) {
                stats.cornerCounts[i] += data.corners[i];
            }

            if (stats.visitCount > maxVisits) {
                maxVisits = stats.visitCount;
                mostVisitedCell = QPoint(cellKey.first, cellKey.second);
            }

            // Collect for recent cells (do not remove here to avoid O(N^2))
            recentlyVisitedCells.append(QPoint(cellKey.first, cellKey.second));
        }

        // FIX: Optimize cleaning of recentlyVisitedCells
        if (recentlyVisitedCells.size() > RECENT_CELLS_BUFFER_SIZE) {
            // Keep only the last RECENT_CELLS_BUFFER_SIZE elements
            // This is still O(N) but called once per batch, not per cell
            recentlyVisitedCells = recentlyVisitedCells.mid(
                recentlyVisitedCells.size() - RECENT_CELLS_BUFFER_SIZE
                );
        }
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

int AntFieldWidget::getVisitCount(qint64 x, qint64 y) const {
    QMutexLocker locker(&statisticsMutex);
    auto it = cellStatistics.constFind(QPair<qint64, qint64>(x, y));
    return (it != cellStatistics.constEnd()) ? it->visitCount : 0;
}

QPoint AntFieldWidget::getMostVisitedCell() const {
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
        summary.averageVisits = static_cast<long double>(summary.totalCellsVisited) / summary.uniqueCellsVisited;
    }

    return summary;
}

QVector<QPair<QPoint, qint64>> AntFieldWidget::getTopVisitedCells(const int count) const {
    QMutexLocker locker(&statisticsMutex);

    if (cellStatistics.isEmpty()) {
        return {};
    }

    // Use a simple selection algorithm for top N cells (more efficient than full sort for large datasets)
    QVector<QPair<QPoint, qint64>> result;
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
                  [](const QPair<QPoint, qint64> &a, const QPair<QPoint, qint64> &b) {
                      return a.second > b.second;
                  });

        // Process remaining items
        for (; it != cellStatistics.constEnd(); ++it) {
            qint64 visits = it->visitCount;
            if (visits > result.last().second) {
                // Insert in sorted position
                auto pos = std::lower_bound(result.begin(), result.end(), visits,
                                            [](const QPair<QPoint, qint64> &item, qint64 value) {
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
    QPoint centerPoint = screenToField(QPoint(width() / 2, height() / 2));

    // Store the current screen position of this center point
    QPoint oldCenterScreen = fieldToScreen(centerPoint);

    // Set the new zoom factor
    zoomFactor = qBound(0.00001, zoom, 50.0);

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
    const long double scaledCellSize = cellSize * zoomFactor;
    offsetX = width() / 2.0 - antX * scaledCellSize;
    offsetY = height() / 2.0 - antY * scaledCellSize;
    needsRedraw = true;
    update();
}

void AntFieldWidget::moveView(const qint64 dx, const qint64 dy) {
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
    bufferPixmap.fill(Qt::white);

    QPainter painter(&bufferPixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const long double scaledCellSize = cellSize * zoomFactor;

    // Calculate visible bounds
    const qint64 startX = qFloor((-offsetX) / scaledCellSize) - 1;
    const qint64 endX = qCeil((width() - offsetX) / scaledCellSize) + 1;
    const qint64 startY = qFloor((-offsetY) / scaledCellSize) - 1;
    const qint64 endY = qCeil((height() - offsetY) / scaledCellSize) + 1;

    // --- Drawing Optimization ---
    const qint64 viewWidth = endX - startX;
    const qint64 viewHeight = endY - startY;
    const qint64 viewportCellCount = (viewWidth > 0 && viewHeight > 0 && viewWidth > LLONG_MAX / viewHeight) ? LLONG_MAX : viewWidth * viewHeight;

    bool useOptimizedDrawing = (scaledCellSize < 1.0) && (viewportCellCount > cells.size());

    if (useOptimizedDrawing) {
        // Optimized path for high zoom-out:
        // Render to a QImage first to ensure one cell state per pixel.
        QImage image(size(), QImage::Format_ARGB32);
        image.fill(Qt::white);

        auto pixels = reinterpret_cast<QRgb*>(image.bits());
        int imageWidth = image.width();
        int imageHeight = image.height();

        // Create a cache of QRgb colors for performance
        QVector<QRgb> rgbColorCache;
        rgbColorCache.reserve(stateColorCache.size());
        for (const QColor &color : stateColorCache) {
            rgbColorCache.append(color.rgb());
        }
        if (rgbColorCache.isEmpty()) {
             rgbColorCache.append(QColor::fromHsv(0, 200, 230).rgb());
        }

        for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
            const int state = it.value();
            if (state > 0) {
                const qint64 cellX = it.key().first;
                const qint64 cellY = it.key().second;

                qint64 screenX = qFloor(offsetX + cellX * scaledCellSize);
                qint64 screenY = qFloor(offsetY + cellY * scaledCellSize);

                if (screenX >= 0 && screenX < imageWidth && screenY >= 0 && screenY < imageHeight) {
                    qint64 pixelIndex = screenY * imageWidth + screenX;
                    // "First come, first served" for a given pixel
                    if (pixels[pixelIndex] == 0xFFFFFFFF) { // Check for white
                        pixels[pixelIndex] = rgbColorCache[state % rgbColorCache.size()];
                    }
                }
            }
        }
        painter.drawImage(0, 0, image);

    } else {
        // Default path for zoom-in: Iterate over the visible grid
        // Draw grid if cells are large enough
        if (scaledCellSize >= 4) {
            painter.setPen(QPen(QColor(220, 220, 220), 0.5));
            for (qint64 x = startX; x <= endX; ++x) {
                long double screenX = offsetX + x * scaledCellSize;
                painter.drawLine(QPointF(screenX, offsetY + startY * scaledCellSize), QPointF(screenX, offsetY + endY * scaledCellSize));
            }
            for (qint64 y = startY; y <= endY; ++y) {
                long double screenY = offsetY + y * scaledCellSize;
                painter.drawLine(QPointF(offsetX + startX * scaledCellSize, screenY), QPointF(offsetX + endX * scaledCellSize, screenY));
            }
        }

        // Draw cell colors
        for (qint64 y = startY; y <= endY; ++y) {
            for (qint64 x = startX; x <= endX; ++x) {
                auto it = cells.constFind({x, y});
                if (it != cells.constEnd()) {
                    int state = it.value();
                    QColor color = (state > 0) ? stateToColor(state) : Qt::white;
                    long double screenX = offsetX + x * scaledCellSize;
                    long double screenY = offsetY + y * scaledCellSize;
                    painter.fillRect(QRectF(screenX, screenY, scaledCellSize, scaledCellSize), color);
                }
            }
        }
    }


    // Draw statistics overlay (only when zoomed in enough)
    if (!useOptimizedDrawing && statisticsEnabled && scaledCellSize >= 8 && currentStyle != JustColors) {
        QMutexLocker locker(&statisticsMutex);
        painter.setPen(Qt::black);

        for (qint64 y = startY; y <= endY; ++y) {
            for (qint64 x = startX; x <= endX; ++x) {
                auto statIt = cellStatistics.constFind({x, y});
                if (statIt == cellStatistics.constEnd()) continue;

                QRectF cellRect(offsetX + x * scaledCellSize, offsetY + y * scaledCellSize, scaledCellSize, scaledCellSize);
                QString text = QString::number(statIt->visitCount);

                if (currentStyle == Visits) {
                    QFont font = painter.font();
                    font.setPointSizeF(1);
                    painter.setFont(font);
                    QRectF textRect = painter.fontMetrics().boundingRect(text);

                    long double scaleX = (cellRect.width() * 0.9) / textRect.width();
                    long double scaleY = (cellRect.height() * 0.9) / textRect.height();
                    long double scale = qMin(scaleX, scaleY);

                    font.setPointSizeF(font.pointSizeF() * scale);
                    painter.setFont(font);

                    painter.drawText(cellRect, Qt::AlignCenter, text);
                } else if (currentStyle == Rotations) {
                    auto drawCornerText = [&](const QRectF &rect, const int value, const Qt::Alignment align) {
                        if (value <= 0) return;
                        QString cur_text = QString::number(value);
                        QRectF cornerRect = rect;
                        cornerRect.setWidth(rect.width() / 2);
                        cornerRect.setHeight(rect.height() / 2);
                        if (align & Qt::AlignRight) cornerRect.moveLeft(rect.center().x());
                        if (align & Qt::AlignBottom) cornerRect.moveTop(rect.center().y());

                        QFont font = painter.font();
                        font.setPointSizeF(1);
                        painter.setFont(font);
                        const QRectF textRect = painter.fontMetrics().boundingRect(cur_text);

                        const long double scaleX = (cornerRect.width() * 0.9) / textRect.width();
                        const long double scaleY = (cornerRect.height() * 0.9) / textRect.height();
                        const long double scale = qMin(scaleX, scaleY);

                        font.setPointSizeF(font.pointSizeF() * scale);
                        painter.setFont(font);

                        painter.drawText(cornerRect, Qt::AlignCenter, cur_text);
                    };

                    drawCornerText(cellRect, statIt->cornerCounts[0], Qt::AlignLeft | Qt::AlignTop);
                    drawCornerText(cellRect, statIt->cornerCounts[1], Qt::AlignRight | Qt::AlignTop);
                    drawCornerText(cellRect, statIt->cornerCounts[2], Qt::AlignRight | Qt::AlignBottom);
                    drawCornerText(cellRect, statIt->cornerCounts[3], Qt::AlignLeft | Qt::AlignBottom);
                }
            }
        }
    }

    // Draw ant
    const long double antScreenX = offsetX + antX * scaledCellSize;
    const long double antScreenY = offsetY + antY * scaledCellSize;
    const QPointF antCenter(antScreenX + scaledCellSize / 2,
                            antScreenY + scaledCellSize / 2);

    if (scaledCellSize >= 2) {
        painter.setBrush(Qt::red);
        painter.setPen(QPen(Qt::black, 1));

        const long double radius = scaledCellSize * 0.4;
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
    return QPoint(qFloor((screenPos.x() - offsetX) / scaledCellSize),
                  qFloor((screenPos.y() - offsetY) / scaledCellSize));
}

QPoint AntFieldWidget::fieldToScreen(const QPoint &fieldPos) const {
    const double scaledCellSize = cellSize * zoomFactor;
    return QPoint(qRound(double(fieldPos.x() * scaledCellSize + offsetX)),
                  qRound(double(fieldPos.y() * scaledCellSize + offsetY)));
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
    constexpr double zoomStep = 1.2;
    const double oldZoom = zoomFactor;
    double newZoom = zoomFactor;

    if (event->angleDelta().y() > 0) {
        newZoom *= zoomStep;
    } else {
        newZoom /= zoomStep;
    }

    if (newZoom < 0.00001 || newZoom > 50.0) return;

    QPoint mousePos = event->position().toPoint();

    double zoomRatio = newZoom / oldZoom;

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
    lastMouseCellPos = QPoint(0, 0);
    emit mouseOverCell(0, 0);
}
