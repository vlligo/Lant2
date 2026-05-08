#ifndef QPOINT64_H
#define QPOINT64_H

// ---------- 64‑bit integer type ----------
#if __has_include(<QtGlobal>)
#  include <QtGlobal>
#  define QPOINT64_QT 1
#elif __has_include(<QtCore/qglobal.h>)
#  include <QtCore/qglobal.h>
#  define QPOINT64_QT 1
#else
#  include <cstdint>
   using qint64 = std::int64_t;
#endif

// ---------- Qt point types for conversion ----------
#if defined(QPOINT64_QT)
#  include <QPoint>
#  include <QPointF>
#endif

class QPoint64
{
public:
    // Data members – named xp/yp to avoid conflict with x()/y()
    qint64 xp = 0;
    qint64 yp = 0;

    // ---------- Constructors ----------
    constexpr QPoint64() noexcept : xp(0), yp(0) {}
    constexpr QPoint64(qint64 xpos, qint64 ypos) noexcept : xp(xpos), yp(ypos) {}

#if defined(QPOINT64_QT)
    // Implicit conversion from Qt's QPoint / QPointF
    QPoint64(const QPoint &point) noexcept
        : xp(point.x()), yp(point.y()) {}
    QPoint64(const QPointF &point) noexcept
        : xp(static_cast<qint64>(point.x())),
          yp(static_cast<qint64>(point.y())) {}
#endif

    // ---------- Accessors ----------
    constexpr qint64  x()      const noexcept { return xp; }
    constexpr qint64  y()      const noexcept { return yp; }
    constexpr qint64& rx()           noexcept { return xp; }
    constexpr qint64& ry()           noexcept { return yp; }
    void setX(qint64 xpos) noexcept { xp = xpos; }
    void setY(qint64 ypos) noexcept { yp = ypos; }

#if defined(QPOINT64_QT)
    QPoint  toPoint()  const { return QPoint(static_cast<int>(xp), static_cast<int>(yp)); }
    QPointF toPointF() const { return QPointF(static_cast<qreal>(xp), static_cast<qreal>(yp)); }
#endif

    // ---------- Arithmetic operators ----------
    QPoint64 &operator+=(const QPoint64 &p) noexcept { xp += p.xp; yp += p.yp; return *this; }
    QPoint64 &operator-=(const QPoint64 &p) noexcept { xp -= p.xp; yp -= p.yp; return *this; }
    QPoint64 &operator*=(qint64 factor)       noexcept { xp *= factor; yp *= factor; return *this; }
    QPoint64 &operator/=(qint64 divisor)      noexcept { xp /= divisor; yp /= divisor; return *this; }
    QPoint64 &operator*=(double factor)       noexcept { xp = static_cast<qint64>(xp * factor); yp = static_cast<qint64>(yp * factor); return *this; }
    QPoint64 &operator/=(double divisor)      noexcept { xp = static_cast<qint64>(xp / divisor); yp = static_cast<qint64>(yp / divisor); return *this; }

    constexpr QPoint64 operator-() const noexcept { return QPoint64(-xp, -yp); }

    // ---------- Utility ----------
    constexpr bool isNull() const noexcept { return xp == 0 && yp == 0; }

    constexpr qint64 manhattanLength() const noexcept
    {
        // constexpr‑safe absolute value (std::abs isn't constexpr until C++23)
        return (xp < 0 ? -xp : xp) + (yp < 0 ? -yp : yp);
    }

    constexpr qint64 dotProduct(const QPoint64 &p) const noexcept
    {
        return xp * p.xp + yp * p.yp;
    }
};

// ---------- Free comparison operators ----------
inline constexpr bool operator==(const QPoint64 &p1, const QPoint64 &p2) noexcept
{ return p1.xp == p2.xp && p1.yp == p2.yp; }
inline constexpr bool operator!=(const QPoint64 &p1, const QPoint64 &p2) noexcept
{ return !(p1 == p2); }

// ---------- Free arithmetic operators ----------
inline constexpr QPoint64 operator+(const QPoint64 &p1, const QPoint64 &p2) noexcept
{ return QPoint64(p1.xp + p2.xp, p1.yp + p2.yp); }
inline constexpr QPoint64 operator-(const QPoint64 &p1, const QPoint64 &p2) noexcept
{ return QPoint64(p1.xp - p2.xp, p1.yp - p2.yp); }
inline constexpr QPoint64 operator*(const QPoint64 &p, qint64 f) noexcept
{ return QPoint64(p.xp * f, p.yp * f); }
inline constexpr QPoint64 operator*(qint64 f, const QPoint64 &p) noexcept
{ return p * f; }
inline constexpr QPoint64 operator/(const QPoint64 &p, qint64 d) noexcept
{ return QPoint64(p.xp / d, p.yp / d); }
inline QPoint64 operator*(const QPoint64 &p, double f) noexcept
{ return QPoint64(static_cast<qint64>(p.xp * f), static_cast<qint64>(p.yp * f)); }
inline QPoint64 operator*(double f, const QPoint64 &p) noexcept
{ return p * f; }
inline QPoint64 operator/(const QPoint64 &p, double d) noexcept
{ return QPoint64(static_cast<qint64>(p.xp / d), static_cast<qint64>(p.yp / d)); }

// ---------- Debug stream (Qt only) ----------
#if defined(QPOINT64_QT) && (defined(QT_DEBUG) || defined(QT_TESTLIB_LIB))
#  include <QDebug>
inline QDebug operator<<(QDebug dbg, const QPoint64 &p)
{
    QDebugStateSaver saver(dbg);
    dbg.nospace() << "QPoint64(" << p.xp << ", " << p.yp << ')';
    return dbg;
}
#endif

#endif // QPOINT64_H