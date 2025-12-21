#ifndef ANTFIELDWIDGET_H
#define ANTFIELDWIDGET_H

#include <QWidget>
#include <QString>
#include <QColor>
#include <QHash>
#include <QCache>
#include <QPair>

class AntFieldWidget : public QWidget {
    Q_OBJECT

public:
    explicit AntFieldWidget(QWidget *parent = nullptr);
    ~AntFieldWidget();

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
    // Coordinate conversion
    inline QPoint screenToField(const QPoint &screenPos) const;
    inline QPoint fieldToScreen(const QPoint &fieldPos) const;

    // Drawing and state management
    void redrawBuffer();
    void updateStateColors();
    QColor stateToColor(int state) const;
    void expandBounds(int x, int y);

    // Performance-optimized cell storage
    QHash<QPair<int, int>, quint8> cells;
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

    // Interaction state
    bool dragging = false;
    QPoint lastMousePos;

    // Rendering optimization
    bool needsRedraw = true;
    QPixmap bufferPixmap;

    // Rules
    QString rules;

    // Constants
    static constexpr double MIN_ZOOM = 0.1;
    static constexpr double MAX_ZOOM = 20.0;
    static constexpr int PROCESS_EVENTS_INTERVAL = 1000;
};

// Hash function for QPair<int, int>
inline uint qHash(const QPair<int, int> &key, uint seed = 0) {
    return qHash(key.first ^ (key.second << 16), seed);
}

#endif // ANTFIELDWIDGET_H
