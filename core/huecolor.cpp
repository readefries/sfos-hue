// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huecolor.h"

#include <QtMath>

namespace HueColor {

namespace {

QPointF point(const QJsonObject &obj)
{
    return QPointF(obj.value(QStringLiteral("x")).toDouble(), obj.value(QStringLiteral("y")).toDouble());
}

double cross(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

QPointF closestOnSegment(const QPointF &a, const QPointF &b, const QPointF &p)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 0)
        return a;
    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = qBound(0.0, t, 1.0);
    return a + ab * t;
}

double distance2(const QPointF &a, const QPointF &b)
{
    const QPointF d = a - b;
    return d.x() * d.x() + d.y() * d.y();
}

double toLinear(double c)
{
    return c > 0.04045 ? qPow((c + 0.055) / 1.055, 2.4) : c / 12.92;
}

double toGamma(double c)
{
    return c <= 0.0031308 ? 12.92 * c : 1.055 * qPow(c, 1.0 / 2.4) - 0.055;
}

} // namespace

Gamut defaultGamut()
{
    Gamut g;
    g.red = QPointF(0.6915, 0.3083);
    g.green = QPointF(0.17, 0.7);
    g.blue = QPointF(0.1532, 0.0475);
    return g;
}

Gamut gamutFromJson(const QJsonObject &gamut)
{
    Gamut g;
    g.red = point(gamut.value(QStringLiteral("red")).toObject());
    g.green = point(gamut.value(QStringLiteral("green")).toObject());
    g.blue = point(gamut.value(QStringLiteral("blue")).toObject());
    return g.isValid() ? g : defaultGamut();
}

QPointF rgbToXy(const QColor &color)
{
    const double r = toLinear(color.redF());
    const double g = toLinear(color.greenF());
    const double b = toLinear(color.blueF());
    const double X = r * 0.664511 + g * 0.154324 + b * 0.162028;
    const double Y = r * 0.283881 + g * 0.668433 + b * 0.047685;
    const double Z = r * 0.000088 + g * 0.072310 + b * 0.986039;
    const double sum = X + Y + Z;
    if (sum <= 0)
        return QPointF(0.3127, 0.3290); // black has no chromaticity; use the white point
    return QPointF(X / sum, Y / sum);
}

QColor xyToRgb(const QPointF &xy)
{
    if (xy.y() <= 0)
        return QColor(Qt::white);
    const double Y = 1.0;
    const double X = Y / xy.y() * xy.x();
    const double Z = Y / xy.y() * (1.0 - xy.x() - xy.y());
    double r = X * 1.656492 - Y * 0.354851 - Z * 0.255038;
    double g = -X * 0.707196 + Y * 1.655397 + Z * 0.036152;
    double b = X * 0.051713 - Y * 0.121364 + Z * 1.011530;
    r = qMax(0.0, r);
    g = qMax(0.0, g);
    b = qMax(0.0, b);
    const double max = qMax(r, qMax(g, b));
    if (max <= 0)
        return QColor(Qt::white);
    return QColor::fromRgbF(qBound(0.0, toGamma(r / max), 1.0),
                            qBound(0.0, toGamma(g / max), 1.0),
                            qBound(0.0, toGamma(b / max), 1.0));
}

bool inGamut(const QPointF &xy, const Gamut &gamut)
{
    const double d1 = cross(gamut.green - gamut.red, xy - gamut.red);
    const double d2 = cross(gamut.blue - gamut.green, xy - gamut.green);
    const double d3 = cross(gamut.red - gamut.blue, xy - gamut.blue);
    const bool hasNeg = d1 < 0 || d2 < 0 || d3 < 0;
    const bool hasPos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(hasNeg && hasPos);
}

QPointF clampToGamut(const QPointF &xy, const Gamut &gamut)
{
    if (inGamut(xy, gamut))
        return xy;
    const QPointF a = closestOnSegment(gamut.red, gamut.green, xy);
    const QPointF b = closestOnSegment(gamut.green, gamut.blue, xy);
    const QPointF c = closestOnSegment(gamut.blue, gamut.red, xy);
    QPointF best = a;
    if (distance2(b, xy) < distance2(best, xy))
        best = b;
    if (distance2(c, xy) < distance2(best, xy))
        best = c;
    return best;
}

QColor mirekToRgb(int mirek)
{
    // Tanner Helland's fit of the black body curve, in units of 100 K.
    const double t = 1000000.0 / qBound(100, mirek, 1000) / 100.0;
    double r, g, b;
    if (t <= 66) {
        r = 255;
        g = 99.4708025861 * qLn(t) - 161.1195681661;
        b = t <= 19 ? 0 : 138.5177312231 * qLn(t - 10) - 305.0447927307;
    } else {
        r = 329.698727446 * qPow(t - 60, -0.1332047592);
        g = 288.1221695283 * qPow(t - 60, -0.0755148492);
        b = 255;
    }
    return QColor(qBound(0, qRound(r), 255), qBound(0, qRound(g), 255), qBound(0, qRound(b), 255));
}

} // namespace HueColor
