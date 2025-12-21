#include "antfieldwidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QtMath>
#include <algorithm>

AntFieldWidget::AntFieldWidget(QWidget *parent)
    : QWidget(parent) {
    setMinimumSize(400, 400);
    setMouseTracking(true);
    reset();
}

AntFieldWidget::~AntFieldWidget() {
    cells.clear();
    stateColorCache.clear();
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
    antX = antY = 0;
    antDir = 0;
    stepCount = 0;
    minX = minY = -50;
    maxX = maxY = 50;

    // Center view
    offsetX = width() / 2.0;
    offsetY = height() / 2.0;

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

    for (int s = 0; s < steps; ++s) {
        QPair<int, int> cellKey(antX, antY);
        quint8 &currentState = cells[cellKey];

        if (currentState < ruleLength) {
            QChar rule = rules.at(currentState);
            currentState = (currentState + 1) % ruleLength;

            // Update direction - optimized switch
            switch (rule.unicode()) {
            case 'L': antDir = (antDir + 3) % 4; break;
            case 'R': antDir = (antDir + 1) % 4; break;
            case 'F': antDir = (antDir + 2) % 4; break;
            case 'B': // Move backward without turning
                // Will be handled in movement
                break;
            }
        }

        // Move ant - using direction vector
        const QPoint &dir = directions[antDir];
        antX += dir.x();
        antY += dir.y();

        // Expand bounds only when needed
        if (antX < minX) minX = antX;
        if (antX > maxX) maxX = antX;
        if (antY < minY) minY = antY;
        if (antY > maxY) maxY = antY;

        stepCount++;

        // Process events periodically
        if ((stepCount % PROCESS_EVENTS_INTERVAL) == 0) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    setUpdatesEnabled(true);
    needsRedraw = true;
    update();

    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
}

void AntFieldWidget::setCellSize(int size) {
    cellSize = qBound(1, size, 50);
    needsRedraw = true;
    update();
}

void AntFieldWidget::setZoom(double zoom) {
    QPoint antScreenPosBefore = fieldToScreen(QPoint(antX, antY));
    zoomFactor = qBound(MIN_ZOOM, zoom, MAX_ZOOM);
    QPoint antScreenPosAfter = fieldToScreen(QPoint(antX, antY));

    offsetX += antScreenPosBefore.x() - antScreenPosAfter.x();
    offsetY += antScreenPosBefore.y() - antScreenPosAfter.y();

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

    // Check if we can draw individual cells
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

        // Vertical lines
        for (int x = drawStartX; x <= drawEndX; ++x) {
            double screenX = offsetX + x * scaledCellSize;
            painter.drawLine(QPointF(screenX, offsetY + drawStartY * scaledCellSize),
                             QPointF(screenX, offsetY + drawEndY * scaledCellSize));
        }

        // Horizontal lines
        for (int y = drawStartY; y <= drawEndY; ++y) {
            double screenY = offsetY + y * scaledCellSize;
            painter.drawLine(QPointF(offsetX + drawStartX * scaledCellSize, screenY),
                             QPointF(offsetX + drawEndX * scaledCellSize, screenY));
        }
    }

    // Draw cells - optimized iteration
    QHash<QPair<int, int>, quint8>::const_iterator it;
    for (int y = drawStartY; y < drawEndY; ++y) {
        for (int x = drawStartX; x < drawEndX; ++x) {
            it = cells.constFind(QPair<int, int>(x, y));
            if (it != cells.constEnd() && it.value() > 0) {
                QColor color = stateToColor(it.value());
                double screenX = offsetX + x * scaledCellSize;
                double screenY = offsetY + y * scaledCellSize;
                painter.fillRect(QRectF(screenX, screenY,
                                        scaledCellSize, scaledCellSize), color);
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

        // Pre-calculated triangle points
        const double radius = scaledCellSize * 0.4;
        QPolygonF triangle;
        triangle << antCenter + QPointF(0, -radius)
                 << antCenter + QPointF(radius * 0.7, radius * 0.7)
                 << antCenter + QPointF(-radius * 0.7, radius * 0.7);

        // Rotate based on direction
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

inline QPoint AntFieldWidget::screenToField(const QPoint &screenPos) const {
    const double scaledCellSize = cellSize * zoomFactor;
    if (qFuzzyIsNull(scaledCellSize)) return QPoint(0, 0);
    return QPoint(qRound((screenPos.x() - offsetX) / scaledCellSize),
                  qRound((screenPos.y() - offsetY) / scaledCellSize));
}

inline QPoint AntFieldWidget::fieldToScreen(const QPoint &fieldPos) const {
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
        zoomFactor = qMin(MAX_ZOOM, zoomFactor * zoomChange);
    } else {
        zoomFactor = qMax(MIN_ZOOM, zoomFactor / zoomChange);
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
