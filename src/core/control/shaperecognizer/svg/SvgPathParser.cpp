/*
 * Xournal++
 *
 * SVG Path and Vector Parser for Shape Recognition
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "SvgPathParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <glib.h>

#include "util/PathUtil.h"

namespace xoj::svg {

namespace {

// Vector angle calculation for elliptical arc
auto vectorAngle(double ux, double uy, double vx, double vy) -> double {
    double dot = ux * vx + uy * vy;
    double lenU = std::hypot(ux, uy);
    double lenV = std::hypot(vx, vy);
    if (lenU == 0.0 || lenV == 0.0) {
        return 0.0;
    }
    double cosVal = std::clamp(dot / (lenU * lenV), -1.0, 1.0);
    double angle = std::acos(cosVal);
    if (ux * vy - uy * vx < 0.0) {
        angle = -angle;
    }
    return angle;
}

// Cubic Bézier curve sampling
void sampleCubicBezier(const Point& p0, const Point& p1, const Point& p2, const Point& p3, std::vector<Point>& out,
                       int steps = 12) {
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        double u = 1.0 - t;
        double tt = t * t;
        double uu = u * u;
        double uuu = uu * u;
        double ttt = tt * t;

        double x = uuu * p0.x + 3.0 * uu * t * p1.x + 3.0 * u * tt * p2.x + ttt * p3.x;
        double y = uuu * p0.y + 3.0 * uu * t * p1.y + 3.0 * u * tt * p2.y + ttt * p3.y;
        out.emplace_back(x, y);
    }
}

// Quadratic Bézier curve sampling
void sampleQuadBezier(const Point& p0, const Point& p1, const Point& p2, std::vector<Point>& out, int steps = 10) {
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        double u = 1.0 - t;
        double x = u * u * p0.x + 2.0 * u * t * p1.x + t * t * p2.x;
        double y = u * u * p0.y + 2.0 * u * t * p1.y + t * t * p2.y;
        out.emplace_back(x, y);
    }
}

// Elliptical arc sampling according to W3C SVG specification Appendix F.6
void sampleArc(const Point& p0, double rx, double ry, double xAxisRotDeg, bool largeArcFlag, bool sweepFlag,
               const Point& p1, std::vector<Point>& out) {
    if (std::abs(p0.x - p1.x) < 1e-6 && std::abs(p0.y - p1.y) < 1e-6) {
        return;
    }
    if (rx == 0.0 || ry == 0.0) {
        out.emplace_back(p1);
        return;
    }

    rx = std::abs(rx);
    ry = std::abs(ry);
    double phi = xAxisRotDeg * (G_PI / 180.0);
    double cosPhi = std::cos(phi);
    double sinPhi = std::sin(phi);

    double dx2 = (p0.x - p1.x) / 2.0;
    double dy2 = (p0.y - p1.y) / 2.0;

    double x1p = cosPhi * dx2 + sinPhi * dy2;
    double y1p = -sinPhi * dx2 + cosPhi * dy2;

    double rxSq = rx * rx;
    double rySq = ry * ry;
    double x1pSq = x1p * x1p;
    double y1pSq = y1p * y1p;

    double radiiCheck = (x1pSq / rxSq) + (y1pSq / rySq);
    if (radiiCheck > 1.0) {
        double scale = std::sqrt(radiiCheck);
        rx *= scale;
        ry *= scale;
        rxSq = rx * rx;
        rySq = ry * ry;
    }

    double sign = (largeArcFlag == sweepFlag) ? -1.0 : 1.0;
    double numerator = rxSq * rySq - rxSq * y1pSq - rySq * x1pSq;
    double denominator = rxSq * y1pSq + rySq * x1pSq;
    double factor = 0.0;
    if (denominator > 0.0 && numerator > 0.0) {
        factor = sign * std::sqrt(numerator / denominator);
    }

    double cxp = factor * (rx * y1p / ry);
    double cyp = -factor * (ry * x1p / rx);

    double cx = cosPhi * cxp - sinPhi * cyp + (p0.x + p1.x) / 2.0;
    double cy = sinPhi * cxp + cosPhi * cyp + (p0.y + p1.y) / 2.0;

    double ux = (x1p - cxp) / rx;
    double uy = (y1p - cyp) / ry;
    double vx = (-x1p - cxp) / rx;
    double vy = (-y1p - cyp) / ry;

    double theta1 = vectorAngle(1.0, 0.0, ux, uy);
    double dTheta = vectorAngle(ux, uy, vx, vy);

    if (!sweepFlag && dTheta > 0.0) {
        dTheta -= 2.0 * G_PI;
    } else if (sweepFlag && dTheta < 0.0) {
        dTheta += 2.0 * G_PI;
    }

    int steps = std::max(6, static_cast<int>(std::ceil(std::abs(dTheta) / (G_PI / 8.0))));
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        double currentAngle = theta1 + t * dTheta;
        double ex = rx * std::cos(currentAngle);
        double ey = ry * std::sin(currentAngle);
        double px = cosPhi * ex - sinPhi * ey + cx;
        double py = sinPhi * ex + cosPhi * ey + cy;
        out.emplace_back(px, py);
    }
}

// Tokenize numbers in SVG path strings
auto getNextNumber(const std::string& str, size_t& pos, double& val) -> bool {
    const size_t len = str.size();
    while (pos < len && (std::isspace(str[pos]) || str[pos] == ',')) {
        pos++;
    }
    if (pos >= len) {
        return false;
    }

    char c = str[pos];
    if (std::isalpha(c) && c != 'e' && c != 'E') {
        return false;
    }

    size_t start = pos;
    if (c == '+' || c == '-') {
        pos++;
    }
    bool hasDigits = false;
    while (pos < len && std::isdigit(str[pos])) {
        pos++;
        hasDigits = true;
    }
    if (pos < len && str[pos] == '.') {
        pos++;
        while (pos < len && std::isdigit(str[pos])) {
            pos++;
            hasDigits = true;
        }
    }
    if (hasDigits && pos < len && (str[pos] == 'e' || str[pos] == 'E')) {
        size_t expPos = pos + 1;
        if (expPos < len && (str[expPos] == '+' || str[expPos] == '-')) {
            expPos++;
        }
        if (expPos < len && std::isdigit(str[expPos])) {
            pos = expPos;
            while (pos < len && std::isdigit(str[pos])) {
                pos++;
            }
        }
    }

    if (!hasDigits) {
        pos = start;
        return false;
    }

    std::string numStr = str.substr(start, pos - start);
    val = std::strtod(numStr.c_str(), nullptr);
    return true;
}

// Tokenize points attribute from polyline/polygon
auto parsePointsList(const std::string& str) -> std::vector<Point> {
    std::vector<Point> pts;
    size_t pos = 0;
    double x = 0.0;
    double y = 0.0;
    while (getNextNumber(str, pos, x) && getNextNumber(str, pos, y)) {
        pts.emplace_back(x, y);
    }
    return pts;
}

}  // namespace

auto SvgPathParser::parsePathData(const std::string& d) -> std::vector<SvgSubpath> {
    std::vector<SvgSubpath> subpaths;
    SvgSubpath currentSubpath;

    Point currentPoint(0.0, 0.0);
    Point subpathStart(0.0, 0.0);
    Point lastCubicControl(0.0, 0.0);
    Point lastQuadControl(0.0, 0.0);
    char lastCmd = '\0';

    size_t pos = 0;
    const size_t len = d.size();

    auto flushSubpath = [&]() {
        if (!currentSubpath.points.empty()) {
            subpaths.push_back(std::move(currentSubpath));
            currentSubpath = SvgSubpath();
        }
    };

    while (pos < len) {
        while (pos < len && (std::isspace(d[pos]) || d[pos] == ',')) {
            pos++;
        }
        if (pos >= len) {
            break;
        }

        char cmd = d[pos];
        if (std::isalpha(cmd)) {
            pos++;
        } else {
            // Implicit repeated command
            if (lastCmd == 'M') {
                cmd = 'L';
            } else if (lastCmd == 'm') {
                cmd = 'l';
            } else if (lastCmd != '\0') {
                cmd = lastCmd;
            } else {
                pos++;
                continue;
            }
        }

        bool isRelative = std::islower(cmd);
        char upperCmd = static_cast<char>(std::toupper(cmd));

        if (upperCmd == 'M') {
            double x = 0.0;
            double y = 0.0;
            if (getNextNumber(d, pos, x) && getNextNumber(d, pos, y)) {
                flushSubpath();
                if (isRelative) {
                    currentPoint.x += x;
                    currentPoint.y += y;
                } else {
                    currentPoint = Point(x, y);
                }
                subpathStart = currentPoint;
                currentSubpath.points.push_back(currentPoint);
                lastCubicControl = currentPoint;
                lastQuadControl = currentPoint;
                lastCmd = isRelative ? 'm' : 'M';
            }
        } else if (upperCmd == 'L') {
            double x = 0.0;
            double y = 0.0;
            while (getNextNumber(d, pos, x) && getNextNumber(d, pos, y)) {
                if (isRelative) {
                    currentPoint.x += x;
                    currentPoint.y += y;
                } else {
                    currentPoint = Point(x, y);
                }
                if (currentSubpath.points.empty()) {
                    currentSubpath.points.push_back(currentPoint);
                    subpathStart = currentPoint;
                } else {
                    currentSubpath.points.push_back(currentPoint);
                }
                lastCubicControl = currentPoint;
                lastQuadControl = currentPoint;
                lastCmd = isRelative ? 'l' : 'L';
            }
        } else if (upperCmd == 'H') {
            double x = 0.0;
            while (getNextNumber(d, pos, x)) {
                if (isRelative) {
                    currentPoint.x += x;
                } else {
                    currentPoint.x = x;
                }
                currentSubpath.points.push_back(currentPoint);
                lastCubicControl = currentPoint;
                lastQuadControl = currentPoint;
                lastCmd = isRelative ? 'h' : 'H';
            }
        } else if (upperCmd == 'V') {
            double y = 0.0;
            while (getNextNumber(d, pos, y)) {
                if (isRelative) {
                    currentPoint.y += y;
                } else {
                    currentPoint.y = y;
                }
                currentSubpath.points.push_back(currentPoint);
                lastCubicControl = currentPoint;
                lastQuadControl = currentPoint;
                lastCmd = isRelative ? 'v' : 'V';
            }
        } else if (upperCmd == 'C') {
            double x1 = 0.0;
            double y1 = 0.0;
            double x2 = 0.0;
            double y2 = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (getNextNumber(d, pos, x1) && getNextNumber(d, pos, y1) && getNextNumber(d, pos, x2) &&
                   getNextNumber(d, pos, y2) && getNextNumber(d, pos, x) && getNextNumber(d, pos, y)) {
                Point p1 = isRelative ? Point(currentPoint.x + x1, currentPoint.y + y1) : Point(x1, y1);
                Point p2 = isRelative ? Point(currentPoint.x + x2, currentPoint.y + y2) : Point(x2, y2);
                Point p = isRelative ? Point(currentPoint.x + x, currentPoint.y + y) : Point(x, y);

                sampleCubicBezier(currentPoint, p1, p2, p, currentSubpath.points);
                currentPoint = p;
                lastCubicControl = p2;
                lastQuadControl = currentPoint;
                lastCmd = isRelative ? 'c' : 'C';
            }
        } else if (upperCmd == 'S') {
            double x2 = 0.0;
            double y2 = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (getNextNumber(d, pos, x2) && getNextNumber(d, pos, y2) && getNextNumber(d, pos, x) &&
                   getNextNumber(d, pos, y)) {
                Point p1 = currentPoint;
                if (std::toupper(lastCmd) == 'C' || std::toupper(lastCmd) == 'S') {
                    p1 = Point(2.0 * currentPoint.x - lastCubicControl.x, 2.0 * currentPoint.y - lastCubicControl.y);
                }
                Point p2 = isRelative ? Point(currentPoint.x + x2, currentPoint.y + y2) : Point(x2, y2);
                Point p = isRelative ? Point(currentPoint.x + x, currentPoint.y + y) : Point(x, y);

                sampleCubicBezier(currentPoint, p1, p2, p, currentSubpath.points);
                currentPoint = p;
                lastCubicControl = p2;
                lastQuadControl = currentPoint;
                lastCmd = isRelative ? 's' : 'S';
            }
        } else if (upperCmd == 'Q') {
            double x1 = 0.0;
            double y1 = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (getNextNumber(d, pos, x1) && getNextNumber(d, pos, y1) && getNextNumber(d, pos, x) &&
                   getNextNumber(d, pos, y)) {
                Point p1 = isRelative ? Point(currentPoint.x + x1, currentPoint.y + y1) : Point(x1, y1);
                Point p = isRelative ? Point(currentPoint.x + x, currentPoint.y + y) : Point(x, y);

                sampleQuadBezier(currentPoint, p1, p, currentSubpath.points);
                currentPoint = p;
                lastQuadControl = p1;
                lastCubicControl = currentPoint;
                lastCmd = isRelative ? 'q' : 'Q';
            }
        } else if (upperCmd == 'T') {
            double x = 0.0;
            double y = 0.0;
            while (getNextNumber(d, pos, x) && getNextNumber(d, pos, y)) {
                Point p1 = currentPoint;
                if (std::toupper(lastCmd) == 'Q' || std::toupper(lastCmd) == 'T') {
                    p1 = Point(2.0 * currentPoint.x - lastQuadControl.x, 2.0 * currentPoint.y - lastQuadControl.y);
                }
                Point p = isRelative ? Point(currentPoint.x + x, currentPoint.y + y) : Point(x, y);

                sampleQuadBezier(currentPoint, p1, p, currentSubpath.points);
                currentPoint = p;
                lastQuadControl = p1;
                lastCubicControl = currentPoint;
                lastCmd = isRelative ? 't' : 'T';
            }
        } else if (upperCmd == 'A') {
            double rx = 0.0;
            double ry = 0.0;
            double xRot = 0.0;
            double largeArc = 0.0;
            double sweep = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (getNextNumber(d, pos, rx) && getNextNumber(d, pos, ry) && getNextNumber(d, pos, xRot) &&
                   getNextNumber(d, pos, largeArc) && getNextNumber(d, pos, sweep) && getNextNumber(d, pos, x) &&
                   getNextNumber(d, pos, y)) {
                Point p = isRelative ? Point(currentPoint.x + x, currentPoint.y + y) : Point(x, y);
                sampleArc(currentPoint, rx, ry, xRot, largeArc != 0.0, sweep != 0.0, p, currentSubpath.points);
                currentPoint = p;
                lastCubicControl = currentPoint;
                lastQuadControl = currentPoint;
                lastCmd = isRelative ? 'a' : 'A';
            }
        } else if (upperCmd == 'Z') {
            if (!currentSubpath.points.empty() &&
                (currentPoint.x != subpathStart.x || currentPoint.y != subpathStart.y)) {
                currentSubpath.points.push_back(subpathStart);
            }
            currentSubpath.closed = true;
            currentPoint = subpathStart;
            lastCubicControl = currentPoint;
            lastQuadControl = currentPoint;
            lastCmd = isRelative ? 'z' : 'Z';
            flushSubpath();
        }
    }

    flushSubpath();
    return subpaths;
}

namespace {

struct ParserState {
    SvgDocument doc;
};

void startElement(GMarkupParseContext* context, const gchar* element_name, const gchar** attribute_names,
                  const gchar** attribute_values, gpointer user_data, GError** error) {
    auto* state = static_cast<ParserState*>(user_data);
    std::string tag = element_name ? element_name : "";

    auto getAttr = [&](const char* name) -> const char* {
        for (int i = 0; attribute_names && attribute_names[i]; ++i) {
            if (std::strcmp(attribute_names[i], name) == 0) {
                return attribute_values[i];
            }
        }
        return nullptr;
    };

    if (tag == "svg") {
        if (const char* vb = getAttr("viewBox")) {
            std::stringstream ss(vb);
            ss >> state->doc.viewBoxX >> state->doc.viewBoxY >> state->doc.viewBoxWidth >> state->doc.viewBoxHeight;
        }
        if (const char* w = getAttr("width")) {
            state->doc.width = std::strtod(w, nullptr);
        }
        if (const char* h = getAttr("height")) {
            state->doc.height = std::strtod(h, nullptr);
        }
    } else if (tag == "path") {
        if (const char* d = getAttr("d")) {
            auto subpaths = SvgPathParser::parsePathData(d);
            for (auto& sp: subpaths) {
                if (!sp.points.empty()) {
                    state->doc.subpaths.push_back(std::move(sp));
                }
            }
        }
    } else if (tag == "line") {
        const char* x1Str = getAttr("x1");
        const char* y1Str = getAttr("y1");
        const char* x2Str = getAttr("x2");
        const char* y2Str = getAttr("y2");
        if (x1Str && y1Str && x2Str && y2Str) {
            double x1 = std::strtod(x1Str, nullptr);
            double y1 = std::strtod(y1Str, nullptr);
            double x2 = std::strtod(x2Str, nullptr);
            double y2 = std::strtod(y2Str, nullptr);
            SvgSubpath sp;
            sp.points.emplace_back(x1, y1);
            sp.points.emplace_back(x2, y2);
            state->doc.subpaths.push_back(std::move(sp));
        }
    } else if (tag == "polyline" || tag == "polygon") {
        if (const char* ptsStr = getAttr("points")) {
            auto pts = parsePointsList(ptsStr);
            if (!pts.empty()) {
                SvgSubpath sp;
                sp.points = std::move(pts);
                if (tag == "polygon") {
                    sp.closed = true;
                    if (sp.points.front().x != sp.points.back().x || sp.points.front().y != sp.points.back().y) {
                        sp.points.push_back(sp.points.front());
                    }
                }
                state->doc.subpaths.push_back(std::move(sp));
            }
        }
    } else if (tag == "rect") {
        double x = getAttr("x") ? std::strtod(getAttr("x"), nullptr) : 0.0;
        double y = getAttr("y") ? std::strtod(getAttr("y"), nullptr) : 0.0;
        double w = getAttr("width") ? std::strtod(getAttr("width"), nullptr) : 0.0;
        double h = getAttr("height") ? std::strtod(getAttr("height"), nullptr) : 0.0;
        if (w > 0.0 && h > 0.0) {
            SvgSubpath sp;
            sp.points.emplace_back(x, y);
            sp.points.emplace_back(x + w, y);
            sp.points.emplace_back(x + w, y + h);
            sp.points.emplace_back(x, y + h);
            sp.points.emplace_back(x, y);
            sp.closed = true;
            state->doc.subpaths.push_back(std::move(sp));
        }
    } else if (tag == "circle") {
        double cx = getAttr("cx") ? std::strtod(getAttr("cx"), nullptr) : 0.0;
        double cy = getAttr("cy") ? std::strtod(getAttr("cy"), nullptr) : 0.0;
        double r = getAttr("r") ? std::strtod(getAttr("r"), nullptr) : 0.0;
        if (r > 0.0) {
            SvgSubpath sp;
            const int steps = 24;
            for (int i = 0; i <= steps; ++i) {
                double a = 2.0 * G_PI * static_cast<double>(i) / steps;
                sp.points.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a));
            }
            sp.closed = true;
            state->doc.subpaths.push_back(std::move(sp));
        }
    }
}

}  // namespace

auto SvgPathParser::parseSvgString(const std::string& svgContent) -> SvgDocument {
    ParserState state;
    GMarkupParser parser = {startElement, nullptr, nullptr, nullptr, nullptr};
    GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, static_cast<GMarkupParseFlags>(0), &state, nullptr);

    GError* err = nullptr;
    g_markup_parse_context_parse(ctx, svgContent.c_str(), static_cast<gssize>(svgContent.size()), &err);
    if (!err) {
        g_markup_parse_context_end_parse(ctx, &err);
    }
    if (err) {
        g_error_free(err);
    }
    g_markup_parse_context_free(ctx);

    return state.doc;
}

auto SvgPathParser::parseSvgFile(const std::string& filePath) -> SvgDocument {
    auto content = Util::readString(filePath, false);
    if (!content.has_value()) {
        return SvgDocument();
    }
    return parseSvgString(content.value());
}

}  // namespace xoj::svg
