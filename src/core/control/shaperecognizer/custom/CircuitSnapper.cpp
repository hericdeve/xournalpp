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

    // Template dimensions
    double tplNominal = tpl->getNominalLength();
    double tplBodyStart = tpl->getBodyStartOffset();
    double tplBodyEnd = tpl->getBodyEndOffset();
    double tplBodyWidth = tpl->getBodyWidth();
    if (tplBodyWidth < 1.0) {
        tplBodyWidth = tplNominal * 0.6;
    }

    // Geometry mapping: keep body standard size, stretch terminal leads
    double placedBodyWidth = 0.0;
    double lead1Len = 0.0;
    double lead2Len = 0.0;

    if (dist > tplBodyWidth) {
        placedBodyWidth = std::min(dist * 0.70, tplBodyWidth);
        lead1Len = (dist - placedBodyWidth) / 2.0;
        lead2Len = (dist - placedBodyWidth) / 2.0;
    } else {
        placedBodyWidth = dist * 0.80;
        lead1Len = dist * 0.10;
        lead2Len = dist * 0.10;
    }

    double scaleY = (tplBodyWidth > 0.0) ? (placedBodyWidth / tplBodyWidth) : 1.0;

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

}  // namespace xoj::circuit
