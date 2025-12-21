#ifndef ANTFIELDWIDGET_H
#define ANTFIELDWIDGET_H

#include <QWidget>
#include <QString>
#include <QColor>
#include <vector>
#include <map>

class AntFieldWidget : public QWidget {
    Q_OBJECT

public:
    explicit AntFieldWidget(QWidget *parent = nullptr);

    void setRules(const QString &rules);
    void reset();
    void nextStep(int steps = 1);
    void setCellSize(int size);
    void setZoom(double zoom);
    double getZoom() const { return zoomFactor; }
    int getCellSize() const { return cellSize; }
    void centerOnAnt();
    void moveView(int dx, int dy);

signals:
    void antMoved(int x, int y, int direction, int steps);
    void zoomChanged(double zoom);
    void stepsChanged(int steps);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QColor stateToColor(int state) const;
    QPointF screenToField(const QPointF &screenPos) const;
    QPointF fieldToScreen(const QPointF &fieldPos) const;
    void ensureAntVisible();
    void expandGridIfNeeded(int newX, int newY);

    // Use a map for sparse storage instead of a vector for the entire grid
    std::map<std::pair<int, int>, int> cells;
    QString rules;
    int cellSize;
    int antX, antY;
    int antDir;
    int stepCount;

    // Dynamic grid bounds
    int minX, maxX, minY, maxY;

    double offsetX, offsetY;
    double zoomFactor;

    bool dragging = false;
    QPoint lastMousePos;

    // Performance optimization
    bool needsRedraw = true;
    QPixmap bufferPixmap;
    void redrawBuffer();
};

#endif // ANTFIELDWIDGET_H
