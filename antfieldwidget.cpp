#include "antfieldwidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QApplication>  // ADD THIS LINE
#include <cmath>
#include <algorithm>
#include <utility>

AntFieldWidget::AntFieldWidget(QWidget *parent)
    : QWidget(parent), cellSize(6), antX(0), antY(0), antDir(0), stepCount(0),
    minX(-50), maxX(50), minY(-50), maxY(50), offsetX(0), offsetY(0), zoomFactor(1.0) {
    setMinimumSize(400, 400);
    setMouseTracking(true);
}

void AntFieldWidget::setRules(const QString &rules) {
    this->rules = rules;
    reset();
}

void AntFieldWidget::reset() {
    cells.clear();
    antX = 0;
    antY = 0;
    antDir = 0; // 0=up, 1=right, 2=down, 3=left
    stepCount = 0;
    minX = -50; maxX = 50;
    minY = -50; maxY = 50;

    // Center the view on the ant
    offsetX = width() / 2.0;
    offsetY = height() / 2.0;

    needsRedraw = true;
    update();
    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
}

void AntFieldWidget::nextStep(int steps) {
    if (rules.isEmpty() || steps <= 0) return;

    // Disable updates temporarily for better performance
    setUpdatesEnabled(false);

    for (int s = 0; s < steps; ++s) {
        auto cellKey = std::make_pair(antX, antY);
        int currentState = cells[cellKey];

        if (currentState < rules.length()) {
            QChar rule = rules[currentState];
            cells[cellKey] = (currentState + 1) % rules.length();

            // Update direction based on rule
            if (rule == 'L') {
                antDir = (antDir + 3) % 4; // Turn left
            } else if (rule == 'R') {
                antDir = (antDir + 1) % 4; // Turn right
            } else if (rule == 'F') {
                antDir = (antDir + 2) % 4; // Turn around
            } else if (rule == 'B') {
                // Move backward (reverse direction and move)
                antDir = (antDir + 2) % 4;
            }
        }

        // Move ant forward
        switch (antDir) {
        case 0: antY--; break; // Up
        case 1: antX++; break; // Right
        case 2: antY++; break; // Down
        case 3: antX--; break; // Left
        }

        // Update bounds
        minX = std::min(minX, antX);
        maxX = std::max(maxX, antX);
        minY = std::min(minY, antY);
        maxY = std::max(maxY, antY);

        stepCount++;

        // Process events every 1000 steps to keep UI responsive
        if (stepCount % 1000 == 0) {
            QApplication::processEvents();
        }
    }

    setUpdatesEnabled(true);
    needsRedraw = true;
    update();
    emit antMoved(antX, antY, antDir, stepCount);
    emit stepsChanged(stepCount);
}

void AntFieldWidget::setCellSize(int size) {
    cellSize = std::max(1, size);
    needsRedraw = true;
    update();
}

void AntFieldWidget::setZoom(double zoom) {
    // Store current ant position in screen coordinates before zoom
    QPointF antScreenPosBefore = fieldToScreen(QPointF(antX, antY));

    // Apply zoom
    zoomFactor = std::max(0.1, std::min(20.0, zoom));

    // Calculate where the ant should be after zoom
    QPointF antScreenPosAfter = fieldToScreen(QPointF(antX, antY));

    // Adjust offset to keep ant at the same screen position
    offsetX += antScreenPosBefore.x() - antScreenPosAfter.x();
    offsetY += antScreenPosBefore.y() - antScreenPosAfter.y();

    needsRedraw = true;
    update();
    emit zoomChanged(zoomFactor);
}

void AntFieldWidget::centerOnAnt() {
    offsetX = width() / 2.0 - antX * cellSize * zoomFactor;
    offsetY = height() / 2.0 - antY * cellSize * zoomFactor;
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

    // Calculate visible area in field coordinates
    double scaledCellSize = cellSize * zoomFactor;
    if (scaledCellSize < 0.5) {
        // Too small to draw individual cells
        painter.setPen(Qt::red);
        painter.drawText(rect(), Qt::AlignCenter, "Zoomed out too far");
        return;
    }

    // Calculate visible bounds in field coordinates
    int startX = std::floor((-offsetX) / scaledCellSize) - 1;
    int endX = std::ceil((width() - offsetX) / scaledCellSize) + 1;
    int startY = std::floor((-offsetY) / scaledCellSize) - 1;
    int endY = std::ceil((height() - offsetY) / scaledCellSize) + 1;

    // Clip to actual cell bounds
    startX = std::max(startX, minX);
    endX = std::min(endX, maxX + 1);
    startY = std::max(startY, minY);
    endY = std::min(endY, maxY + 1);

    // Draw grid lines if cells are large enough
    if (scaledCellSize >= 8) {
        painter.setPen(QPen(QColor(220, 220, 220), 1));
        for (int x = startX; x <= endX; ++x) {
            double screenX = offsetX + x * scaledCellSize;
            painter.drawLine(QPointF(screenX, offsetY + startY * scaledCellSize),
                             QPointF(screenX, offsetY + endY * scaledCellSize));
        }
        for (int y = startY; y <= endY; ++y) {
            double screenY = offsetY + y * scaledCellSize;
            painter.drawLine(QPointF(offsetX + startX * scaledCellSize, screenY),
                             QPointF(offsetX + endX * scaledCellSize, screenY));
        }
    }

    // Draw cells
    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            auto it = cells.find(std::make_pair(x, y));
            if (it != cells.end() && it->second > 0) {
                QColor color = stateToColor(it->second);
                double screenX = offsetX + x * scaledCellSize;
                double screenY = offsetY + y * scaledCellSize;
                painter.fillRect(QRectF(screenX, screenY, scaledCellSize, scaledCellSize), color);
            }
        }
    }

    // Draw ant
    double antScreenX = offsetX + antX * scaledCellSize;
    double antScreenY = offsetY + antY * scaledCellSize;

    if (scaledCellSize >= 2) {
        painter.setBrush(QColor(255, 0, 0));
        painter.setPen(QPen(Qt::black, 1));

        QPointF antCenter(antScreenX + scaledCellSize / 2, antScreenY + scaledCellSize / 2);

        // Draw ant as a triangle pointing in direction
        double radius = scaledCellSize * 0.4;
        QPolygonF triangle;
        triangle << antCenter + QPointF(0, -radius);
        triangle << antCenter + QPointF(radius * 0.7, radius * 0.7);
        triangle << antCenter + QPointF(-radius * 0.7, radius * 0.7);

        // Rotate triangle based on direction
        QTransform transform;
        transform.translate(antCenter.x(), antCenter.y());
        transform.rotate(antDir * 90);
        transform.translate(-antCenter.x(), -antCenter.y());
        triangle = transform.map(triangle);

        painter.drawPolygon(triangle);
    } else {
        // When zoomed out, just draw a red dot
        painter.setBrush(Qt::red);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(antScreenX + scaledCellSize / 2,
                                    antScreenY + scaledCellSize / 2),
                            scaledCellSize / 2, scaledCellSize / 2);
    }
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
    double zoomChange = 1.1;
    QPointF mousePos = event->position();

    // Convert mouse position to field coordinates
    QPointF fieldPos = screenToField(mousePos);

    if (event->angleDelta().y() > 0) {
        // Zoom in
        zoomFactor = std::min(20.0, zoomFactor * zoomChange);
    } else {
        // Zoom out
        zoomFactor = std::max(0.1, zoomFactor / zoomChange);
    }

    // Convert field position back to screen coordinates with new zoom
    QPointF newMousePos = fieldToScreen(fieldPos);

    // Adjust offset so the point under mouse stays fixed
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

QColor AntFieldWidget::stateToColor(int state) const {
    int maxStates = rules.length();
    if (maxStates <= 1) maxStates = 2;

    // Ensure state is within valid range
    state = state % maxStates;
    float ratio = static_cast<float>(state) / (maxStates - 1);
    int hue = static_cast<int>(ratio * 360) % 360;

    // Clamp saturation and value to valid ranges
    int saturation = 200;
    int value = 230;

    return QColor::fromHsv(hue, saturation, value);
}

QPointF AntFieldWidget::screenToField(const QPointF &screenPos) const {
    double scaledCellSize = cellSize * zoomFactor;
    if (qFuzzyIsNull(scaledCellSize)) return QPointF(0, 0);
    return QPointF((screenPos.x() - offsetX) / scaledCellSize,
                   (screenPos.y() - offsetY) / scaledCellSize);
}

QPointF AntFieldWidget::fieldToScreen(const QPointF &fieldPos) const {
    double scaledCellSize = cellSize * zoomFactor;
    return QPointF(fieldPos.x() * scaledCellSize + offsetX,
                   fieldPos.y() * scaledCellSize + offsetY);
}
