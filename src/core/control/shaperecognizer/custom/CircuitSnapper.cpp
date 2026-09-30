/*
 * Xournal++
 *
 * Circuit Component Geometric Snapper & Lead Stretcher
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "CircuitSnapper.h"

#include <algorithm>
#include <cmath>

#include <glib.h>

namespace xoj::circuit {

auto CircuitSnapper::snapAngle(double angle, double cardinalTolerance, double diagonalTolerance) -> double {
    // Normalize angle to [-PI, PI]
    while (angle > G_PI) {
        angle -= 2.0 * G_PI;
    }
    while (angle < -G_PI) {
        angle += 2.0 * G_PI;
    }

    // Cardinal targets: 0, PI/2, PI, -PI/2, -PI
    const double cardinals[] = {0.0, G_PI / 2.0, G_PI, -G_PI / 2.0, -G_PI};
    for (double card: cardinals) {
        if (std::abs(angle - card) <= cardinalTolerance) {
            return card;
        }
    }

    // Diagonal targets: PI/4, 3*PI/4, -PI/4, -3*PI/4
    const double diagonals[] = {G_PI / 4.0, 3.0 * G_PI / 4.0, -G_PI / 4.0, -3.0 * G_PI / 4.0};
    for (double diag: diagonals) {
        if (std::abs(angle - diag) <= diagonalTolerance) {
            return diag;
        }
    }

    return angle;
}

auto CircuitSnapper::snapCircuit(const CircuitTemplate* tpl, const Point& startPt, const Point& endPt,
                                const Stroke* styleSource, bool orthoSnap, SnapToGridInputHandler* snappingHandler)
        -> std::unique_ptr<Stroke> {
    if (!tpl) {
        return nullptr;
    }

    Point p0 = startPt;
    Point p1 = endPt;

    double dx = p1.x - p0.x;
    double dy = p1.y - p0.y;
    double dist = std::hypot(dx, dy);

    if (dist < 5.0) {
        auto simple = std::make_unique<Stroke>();
        simple->applyStyleFrom(styleSource);
        simple->addPoint(p0);
        simple->addPoint(p1);
        return simple;
    }

    double angle = std::atan2(dy, dx);
    if (orthoSnap) {
        angle = snapAngle(angle);
        p1 = Point(p0.x + dist * std::cos(angle), p0.y + dist * std::sin(angle));
    }

    if (snappingHandler) {
        p0 = snappingHandler->snapToGrid(p0, false);
        p1 = snappingHandler->snapToGrid(p1, false);
        dx = p1.x - p0.x;
        dy = p1.y - p0.y;
        dist = std::hypot(dx, dy);
        angle = std::atan2(dy, dx);
        if (orthoSnap) {
            angle = snapAngle(angle);
            p1 = Point(p0.x + dist * std::cos(angle), p0.y + dist * std::sin(angle));
        }
    }

    double cosA = std::cos(angle);
    double sinA = std::sin(angle);
    Point u(cosA, sinA);
    Point v(-sinA, cosA);

    // Closed shape handling (Star, Polygon, Heart, Circle, etc.)
    if (tpl->getKind() == TemplateKind::ClosedShape) {
        Point center((p0.x + p1.x) / 2.0, (p0.y + p1.y) / 2.0);
        double drawnRadius = std::max(dist / 2.0, 5.0);

        const auto& tplBbox = tpl->getTemplateBbox();
        const auto& tplCentroid = tpl->getCentroid();
        double tplSize = std::max(std::hypot(tplBbox.width, tplBbox.height), 1.0);
        double scale = (2.0 * drawnRadius) / tplSize;

        auto result = std::make_unique<Stroke>();
        result->applyStyleFrom(styleSource);

        auto transformClosedPt = [&](const Point& pt) -> Point {
            double rx = (pt.x - tplCentroid.x) * scale;
            double ry = (pt.y - tplCentroid.y) * scale;
            return Point(center.x + rx, center.y + ry);
        };

        for (const auto& sp: tpl->getSubpaths()) {
            for (const auto& pt: sp.points) {
                result->addPoint(transformClosedPt(pt));
            }
        }
        if (result->getPointCount() == 0) {
            for (const auto& pt: tpl->getCombinedPath()) {
                result->addPoint(transformClosedPt(pt));
            }
        }
        return result;
    }

    // Ground symbol handling
    if (tpl->getId() == "ground") {
        double scale = std::max(dist / 36.0, 0.2);
        auto result = std::make_unique<Stroke>();
        result->applyStyleFrom(styleSource);

        auto transformGroundPt = [&](const Point& pt) -> Point {
            double px = (pt.x - 25.0) * scale;
            double py = pt.y * scale;
            double rx = py * cosA - px * sinA;
            double ry = py * sinA + px * cosA;
            return Point(p0.x + rx, p0.y + ry);
        };

        for (const auto& sp: tpl->getSubpaths()) {
            for (const auto& pt: sp.points) {
                result->addPoint(transformGroundPt(pt));
            }
        }
        return result;
    }

    // Two-terminal component dimensions
    double tplNominal = tpl->getNominalLength();
    double tplBodyStart = tpl->getBodyStartOffset();
    double tplBodyEnd = tpl->getBodyEndOffset();
    double tplBodyWidth = tpl->getBodyWidth();
    if (tplBodyWidth < 1.0) {
        tplBodyWidth = tplNominal * 0.6;
    }

    // Geometry mapping:
    // Support ANY size: small (20-40px), medium (60-150px), or large (200-800px).
    // The component body scales smoothly with dist while preserving comfortable connection leads.
    double placedBodyWidth = 0.0;
    double lead1Len = 0.0;
    double lead2Len = 0.0;

    if (dist < 35.0) {
        placedBodyWidth = dist * 0.80;
        lead1Len = dist * 0.10;
        lead2Len = dist * 0.10;
    } else {
        placedBodyWidth = std::clamp(dist * 0.58, 20.0, dist - 14.0);
        lead1Len = (dist - placedBodyWidth) / 2.0;
        lead2Len = (dist - placedBodyWidth) / 2.0;
    }

    double scaleY = (tplBodyWidth > 0.0) ? (placedBodyWidth / tplBodyWidth) : 1.0;

    // If styleSource is provided, adapt scaleY to match user's drawn amplitude
    if (styleSource && styleSource->getPointCount() > 4) {
        double maxDev = 0.0;
        for (const auto& p: styleSource->getPointVector()) {
            double px = p.x - p0.x;
            double py = p.y - p0.y;
            double dev = std::abs(-sinA * px + cosA * py);
            maxDev = std::max(maxDev, dev);
        }
        double tplDev = tpl->getMaxBodyDeviation();
        if (tplDev < 1.0) {
            tplDev = tpl->getTemplateHeight() / 2.0;
        }
        if (maxDev > 4.0 && tplDev > 2.0) {
            double drawnScaleY = maxDev / tplDev;
            scaleY = 0.6 * scaleY + 0.4 * drawnScaleY;
        }
    }

    // Baseline rotation of template
    const Point& tplA = tpl->getTerminalA();
    const Point& tplB = tpl->getTerminalB();
    double tplDx = tplB.x - tplA.x;
    double tplDy = tplB.y - tplA.y;
    double tplAngle = std::atan2(tplDy, tplDx);
    double tplCos = std::cos(-tplAngle);
    double tplSin = std::sin(-tplAngle);

    auto transformPoint = [&](const Point& pt) -> Point {
        double px = pt.x - tplA.x;
        double py = pt.y - tplA.y;
        double tu = tplCos * px - tplSin * py;
        double tv = tplSin * px + tplCos * py;

        double curDist = 0.0;
        if (tu <= tplBodyStart) {
            double ratio = (tplBodyStart > 0.0) ? (tu / tplBodyStart) : 0.0;
            curDist = ratio * lead1Len;
        } else if (tu >= tplBodyEnd) {
            double ratio = (tplNominal > tplBodyEnd) ? ((tu - tplBodyEnd) / (tplNominal - tplBodyEnd)) : 1.0;
            curDist = lead1Len + placedBodyWidth + ratio * lead2Len;
        } else {
            double ratio = (tu - tplBodyStart) / tplBodyWidth;
            curDist = lead1Len + ratio * placedBodyWidth;
        }

        double curDev = tv * scaleY;
        return Point(p0.x + curDist * u.x + curDev * v.x, p0.y + curDist * u.y + curDev * v.y);
    };

    auto result = std::make_unique<Stroke>();
    result->applyStyleFrom(styleSource);

    const auto& subpaths = tpl->getSubpaths();
    if (subpaths.empty()) {
        const auto& pts = tpl->getCombinedPath();
        for (const auto& pt: pts) {
            result->addPoint(transformPoint(pt));
        }
    } else {
        for (const auto& sp: subpaths) {
            for (const auto& pt: sp.points) {
                result->addPoint(transformPoint(pt));
            }
        }
    }

    return result;
}

auto CircuitSnapper::snapCircuitComposite(const CircuitTemplate* tpl, const Point& startPt, const Point& endPt,
                                         const Stroke* styleSource, bool orthoSnap,
                                         SnapToGridInputHandler* snappingHandler)
        -> std::vector<std::unique_ptr<Stroke>> {
    std::vector<std::unique_ptr<Stroke>> results;
    if (!tpl) {
        return results;
    }

    Point p0 = startPt;
    Point p1 = endPt;

    double dx = p1.x - p0.x;
    double dy = p1.y - p0.y;
    double dist = std::hypot(dx, dy);

    if (dist < 5.0) {
        auto simple = std::make_unique<Stroke>();
        simple->applyStyleFrom(styleSource);
        simple->addPoint(p0);
        simple->addPoint(p1);
        results.push_back(std::move(simple));
        return results;
    }

    double angle = std::atan2(dy, dx);
    if (orthoSnap) {
        angle = snapAngle(angle);
        p1 = Point(p0.x + dist * std::cos(angle), p0.y + dist * std::sin(angle));
    }

    if (snappingHandler) {
        p0 = snappingHandler->snapToGrid(p0, false);
        p1 = snappingHandler->snapToGrid(p1, false);
        dx = p1.x - p0.x;
        dy = p1.y - p0.y;
        dist = std::hypot(dx, dy);
        angle = std::atan2(dy, dx);
        if (orthoSnap) {
            angle = snapAngle(angle);
            p1 = Point(p0.x + dist * std::cos(angle), p0.y + dist * std::sin(angle));
        }
    }

    double cosA = std::cos(angle);
    double sinA = std::sin(angle);
    Point u(cosA, sinA);
    Point v(-sinA, cosA);

    // 1. Capacitor: Disjoint two-stroke composite with clear air gap between plates
    if (tpl->getId() == "capacitor") {
        double wGap = std::clamp(dist * 0.15, 8.0, 16.0);
        double hPlate = std::clamp(dist * 0.45, 24.0, 60.0);
        Point center((p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5);

        Point c1(center.x - (wGap * 0.5) * u.x, center.y - (wGap * 0.5) * u.y);
        Point c2(center.x + (wGap * 0.5) * u.x, center.y + (wGap * 0.5) * u.y);

        // Stroke 1: Left lead and Plate 1
        auto s1 = std::make_unique<Stroke>();
        s1->applyStyleFrom(styleSource);
        s1->addPoint(p0);
        s1->addPoint(c1);
        s1->addPoint(Point(c1.x + (hPlate * 0.5) * v.x, c1.y + (hPlate * 0.5) * v.y));
        s1->addPoint(Point(c1.x - (hPlate * 0.5) * v.x, c1.y - (hPlate * 0.5) * v.y));
        results.push_back(std::move(s1));

        // Stroke 2: Plate 2 and Right lead
        auto s2 = std::make_unique<Stroke>();
        s2->applyStyleFrom(styleSource);
        s2->addPoint(Point(c2.x - (hPlate * 0.5) * v.x, c2.y - (hPlate * 0.5) * v.y));
        s2->addPoint(Point(c2.x + (hPlate * 0.5) * v.x, c2.y + (hPlate * 0.5) * v.y));
        s2->addPoint(c2);
        s2->addPoint(p1);
        results.push_back(std::move(s2));

        return results;
    }

    // 2. Ground: Disjoint stem + 3 descending horizontal centered bars
    if (tpl->getId() == "ground") {
        double stemLen = std::clamp(dist * 0.40, 12.0, 30.0);
        Point pStem(p0.x + stemLen * u.x, p0.y + stemLen * u.y);

        double sBar = 6.0;
        double w1 = std::clamp(dist * 0.50, 24.0, 44.0);
        double w2 = w1 * 0.65;
        double w3 = w1 * 0.35;

        // Stem stroke
        auto sStem = std::make_unique<Stroke>();
        sStem->applyStyleFrom(styleSource);
        sStem->addPoint(p0);
        sStem->addPoint(pStem);
        results.push_back(std::move(sStem));

        // Bar 1 (top)
        auto sBar1 = std::make_unique<Stroke>();
        sBar1->applyStyleFrom(styleSource);
        sBar1->addPoint(Point(pStem.x - (w1 * 0.5) * v.x, pStem.y - (w1 * 0.5) * v.y));
        sBar1->addPoint(Point(pStem.x + (w1 * 0.5) * v.x, pStem.y + (w1 * 0.5) * v.y));
        results.push_back(std::move(sBar1));

        // Bar 2 (middle)
        Point c2(pStem.x + sBar * u.x, pStem.y + sBar * u.y);
        auto sBar2 = std::make_unique<Stroke>();
        sBar2->applyStyleFrom(styleSource);
        sBar2->addPoint(Point(c2.x - (w2 * 0.5) * v.x, c2.y - (w2 * 0.5) * v.y));
        sBar2->addPoint(Point(c2.x + (w2 * 0.5) * v.x, c2.y + (w2 * 0.5) * v.y));
        results.push_back(std::move(sBar2));

        // Bar 3 (bottom)
        Point c3(pStem.x + 2.0 * sBar * u.x, pStem.y + 2.0 * sBar * u.y);
        auto sBar3 = std::make_unique<Stroke>();
        sBar3->applyStyleFrom(styleSource);
        sBar3->addPoint(Point(c3.x - (w3 * 0.5) * v.x, c3.y - (w3 * 0.5) * v.y));
        sBar3->addPoint(Point(c3.x + (w3 * 0.5) * v.x, c3.y + (w3 * 0.5) * v.y));
        results.push_back(std::move(sBar3));

        return results;
    }

    // Default: Single stroke component (Resistor, Inductor, Diode, Custom SVG)
    auto single = snapCircuit(tpl, startPt, endPt, styleSource, orthoSnap, snappingHandler);
    if (single) {
        results.push_back(std::move(single));
    }
    return results;
}

}  // namespace xoj::circuit
