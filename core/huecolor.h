// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUECOLOR_H
#define HUECOLOR_H

#include <QColor>
#include <QJsonObject>
#include <QPointF>

// Colour conversions for the bridge, which takes CIE xy chromaticity (clamped to what each lamp
// can show, its gamut) and colour temperature in mirek (1,000,000 / kelvin).
namespace HueColor {

struct Gamut {
    QPointF red, green, blue;
    bool isValid() const { return red != green; }
};

// Gamut C, which recent lamps use; the fallback when a lamp doesn't report its own.
Gamut defaultGamut();
// From a light's color.gamut object ({red: {x, y}, green: ..., blue: ...}).
Gamut gamutFromJson(const QJsonObject &gamut);

// sRGB to xy (wide gamut D65), ignoring brightness.
QPointF rgbToXy(const QColor &color);
// xy to the brightest sRGB colour with that chromaticity.
QColor xyToRgb(const QPointF &xy);

bool inGamut(const QPointF &xy, const Gamut &gamut);
// xy itself if the gamut contains it, otherwise the nearest point on its edge.
QPointF clampToGamut(const QPointF &xy, const Gamut &gamut);

// Approximate on-screen colour of white light at this temperature.
QColor mirekToRgb(int mirek);

} // namespace HueColor

#endif // HUECOLOR_H
