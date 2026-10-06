#pragma once
// Native geometry shared by the window, the exports and the tests: node sizes, layouts, curves.
// Everything is in map units and independent from GDI+, so it stays deterministic and testable.
#include "documents.hpp"
#include <numbers>
#include <unordered_map>

namespace document {
struct Point { double x = 0, y = 0; };
struct Box {
    double x = 0, y = 0, w = 0, h = 0;  // centre and size
    double left() const { return x - w / 2; }
    double right() const { return x + w / 2; }
    double top() const { return y - h / 2; }
    double bottom() const { return y + h / 2; }
    bool contains(double px, double py) const { return px >= left() && px <= right() && py >= top() && py <= bottom(); }
    bool overlaps(const Box& o) const { return left() < o.right() && o.left() < right() && top() < o.bottom() && o.top() < bottom(); }
};
struct Bounds { double left=0, top=0, right=0, bottom=0; };
struct Curve {
    Point a, b, c, d;
    Point at(double t) const {
        const double u = 1 - t, k0 = u*u*u, k1 = 3*u*u*t, k2 = 3*u*t*t, k3 = t*t*t;
        return {k0*a.x + k1*b.x + k2*c.x + k3*d.x, k0*a.y + k1*b.y + k2*c.y + k3*d.y};
    }
    double distance(Point p, int samples = 32) const {
        double best = 1e300;
        Point previous = a;
        for (int i = 1; i <= samples; ++i) {
            Point q = at(static_cast<double>(i) / samples);
            const double vx = q.x - previous.x, vy = q.y - previous.y, len = vx*vx + vy*vy;
            double t = len > 0 ? std::clamp(((p.x - previous.x)*vx + (p.y - previous.y)*vy) / len, 0.0, 1.0) : 0;
            best = std::min(best, std::hypot(p.x - previous.x - t*vx, p.y - previous.y - t*vy));
            previous = q;
        }
        return best;
    }
};

inline size_t glyphs(std::string_view s) {
    size_t count = 0;
    for (unsigned char c : s) count += (c & 0xC0) != 0x80;
    return count;
}
// Splits text into at most max_lines lines of chars characters, breaking on spaces when possible.
inline std::vector<std::string> wrap(const std::string& text, size_t chars, size_t max_lines) {
    std::vector<std::string> lines;
    chars = std::max<size_t>(chars, 4);
    size_t start = 0;
    while (start <= text.size() && lines.size() < max_lines) {
        size_t end = text.find('\n', start);
        std::string paragraph = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!paragraph.empty() && paragraph.back() == '\r') paragraph.pop_back();
        do {
            if (glyphs(paragraph) <= chars) { lines.push_back(paragraph); paragraph.clear(); break; }
            size_t cut = 0, count = 0, last_space = std::string::npos;
            while (cut < paragraph.size() && count < chars) {
                if (paragraph[cut] == ' ') last_space = cut;
                ++cut;
                while (cut < paragraph.size() && (static_cast<unsigned char>(paragraph[cut]) & 0xC0) == 0x80) ++cut;
                ++count;
            }
            if (last_space != std::string::npos && last_space > 0) cut = last_space;
            lines.push_back(paragraph.substr(0, cut));
            paragraph.erase(0, cut);
            if (!paragraph.empty() && paragraph[0] == ' ') paragraph.erase(0, 1);
        } while (!paragraph.empty() && lines.size() < max_lines);
        if (!paragraph.empty() && !lines.empty()) { lines.back() += "…"; return lines; }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (start < text.size() && lines.size() == max_lines && text.find('\n', start) != std::string::npos && lines.back().find("…") == std::string::npos)
        lines.back() += "…";
    return lines;
}

// Text metrics approximate Segoe UI so that layout, hit-testing and exports agree.
struct Metrics { double w = 0, h = 0, title_h = 0, note_h = 0; size_t title_chars = 0, note_chars = 0; };
constexpr double title_char = 7.6, root_char = 9.6, note_char = 6.4, title_line = 19, root_line = 23, note_line = 16;
constexpr size_t max_title_lines = 4, max_note_lines = 4;
inline Metrics metrics(const State& state, long long id) {
    auto n = state.map->nodes.at(id);
    const bool root = id == 1;
    const double cw = root ? root_char : title_char, pad = 44;
    Metrics m;
    m.w = std::clamp(static_cast<double>(glyphs(n->title)) * cw + pad, root ? 170.0 : 120.0, root ? 300.0 : 240.0);
    const bool notes = state.show_notes && !n->note.empty();
    if (notes) m.w = std::max(m.w, 210.0);
    m.title_chars = static_cast<size_t>((m.w - pad) / cw);
    m.title_h = static_cast<double>(wrap(n->title, m.title_chars, max_title_lines).size()) * (root ? root_line : title_line);
    if (notes) {
        m.note_chars = static_cast<size_t>((m.w - 36) / note_char);
        m.note_h = static_cast<double>(wrap(n->note, m.note_chars, max_note_lines).size()) * note_line + 8;
    }
    m.h = std::max(root ? 70.0 : 54.0, m.title_h + m.note_h + 26);
    return m;
}
inline Box box(const State& state, long long id) {
    auto n = state.map->nodes.at(id);
    auto m = metrics(state, id);
    return {n->x, n->y, m.w, m.h};
}

// Tree connector: leaves the parent on the side facing the child, so it works on both sides and in rings.
inline Curve connector(const Box& a, const Box& b) {
    if (b.left() >= a.right() - 4 || b.right() <= a.left() + 4) {
        const double side = b.x >= a.x ? 1 : -1;
        Point s{side > 0 ? a.right() : a.left(), a.y}, e{side > 0 ? b.left() : b.right(), b.y};
        const double bend = std::max(24.0, std::abs(e.x - s.x) * .5) * side;
        return {s, {s.x + bend, s.y}, {e.x - bend, e.y}, e};
    }
    const double down = b.y >= a.y ? 1 : -1;
    Point s{a.x, down > 0 ? a.bottom() : a.top()}, e{b.x, down > 0 ? b.top() : b.bottom()};
    const double bend = std::max(20.0, std::abs(e.y - s.y) * .5) * down;
    return {s, {s.x, s.y + bend}, {e.x, e.y - bend}, e};
}
inline Point edge_point(const Box& b, double dx, double dy) {
    const double hw = b.w / 2 + 5, hh = b.h / 2 + 5;
    const double tx = std::abs(dx) > 1e-9 ? hw / std::abs(dx) : 1e300, ty = std::abs(dy) > 1e-9 ? hh / std::abs(dy) : 1e300;
    const double t = std::min(tx, ty);
    return t >= 1e299 ? Point{b.x, b.top()} : Point{b.x + dx * t, b.y + dy * t};
}
// Cross links arc away from the straight line so they never hide the tree connectors.
inline Curve link_curve(const Box& a, const Box& b) {
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len < 1) return {{a.x, a.top()}, {a.x - 60, a.top() - 80}, {a.x + 60, a.top() - 80}, {a.x, a.top()}};
    const double nx = -dy / len, ny = dx / len, bulge = std::min(140.0, len * .28);
    Point c1{a.x + dx / 3 + nx * bulge, a.y + dy / 3 + ny * bulge}, c2{a.x + 2 * dx / 3 + nx * bulge, a.y + 2 * dy / 3 + ny * bulge};
    return {edge_point(a, c1.x - a.x, c1.y - a.y), c1, c2, edge_point(b, c2.x - b.x, c2.y - b.y)};
}
inline Bounds bounds(const State& state) {
    Bounds b{1e9, 1e9, -1e9, -1e9};
    for (auto id : state.map->visible()) {
        auto r = box(state, id);
        b.left = std::min(b.left, r.left()); b.right = std::max(b.right, r.right());
        b.top = std::min(b.top, r.top() - 10); b.bottom = std::max(b.bottom, r.bottom());
    }
    return b;
}
// Hidden ideas (inside a collapsed branch) are drawn nowhere; links attach to their closest visible ancestor.
inline long long visible_anchor(const State& state, long long id) {
    long long shown = id;
    for (long long cursor = state.map->nodes.at(id)->parent; cursor != -1; cursor = state.map->nodes.at(cursor)->parent)
        if (state.map->nodes.at(cursor)->collapsed) shown = cursor;
    return shown;
}

class LayoutEngine {
    State& state;
    std::unordered_map<long long, Metrics> sizes;
    std::unordered_map<long long, double> spans, leaves;
    std::unordered_map<long long, double> angles, wedges;
    static constexpr double sibling_gap = 18, level_gap = 60, ring_gap = 70;
    std::shared_ptr<MapNode> node(long long id) { return state.map->nodes.at(id); }
    bool open(long long id) { auto n = node(id); return !n->collapsed && !n->children.empty(); }
    const Metrics& size(long long id) {
        auto it = sizes.find(id);
        return it != sizes.end() ? it->second : sizes.emplace(id, metrics(state, id)).first->second;
    }
    double span(long long id) {
        if (auto it = spans.find(id); it != spans.end()) return it->second;
        double children = 0;
        if (open(id)) for (auto child : node(id)->children) children += span(child);
        return spans[id] = std::max(size(id).h + sibling_gap, children);
    }
    void tree(long long id, double x, double top, double side) {
        auto n = node(id);
        const double total = span(id);
        n->x = x; n->y = top + total / 2;
        if (!open(id)) return;
        double used = 0;
        for (auto child : n->children) used += span(child);
        double offset = top + (total - used) / 2;
        for (auto child : n->children) {
            tree(child, x + side * (size(id).w / 2 + level_gap + size(child).w / 2), offset, side);
            offset += span(child);
        }
    }
    void column(const std::vector<long long>& ids, double side) {
        double total = 0;
        for (auto id : ids) total += span(id);
        double top = -total / 2;
        for (auto id : ids) { tree(id, side * (size(1).w / 2 + level_gap * 1.4 + size(id).w / 2), top, side); top += span(id); }
    }
    double leaf_count(long long id) {
        if (auto it = leaves.find(id); it != leaves.end()) return it->second;
        double count = 0;
        if (open(id)) for (auto child : node(id)->children) count += leaf_count(child);
        return leaves[id] = std::max(1.0, count);
    }
    void wedge(long long id, double from, double to, size_t depth, std::vector<std::vector<long long>>& rings) {
        angles[id] = (from + to) / 2; wedges[id] = to - from;
        if (rings.size() <= depth) rings.resize(depth + 1);
        rings[depth].push_back(id);
        if (!open(id)) return;
        const double total = leaf_count(id);
        double cursor = from;
        for (auto child : node(id)->children) {
            const double share = (to - from) * leaf_count(child) / total;
            wedge(child, cursor, cursor + share, depth + 1, rings);
            cursor += share;
        }
    }
    void radial() {
        auto root = node(1);
        root->x = 0; root->y = 0;
        if (!open(1)) return;
        std::vector<std::vector<long long>> rings;
        // A single branch points right; otherwise the first branch starts at the top, clockwise.
        const double start = root->children.size() == 1 ? -std::numbers::pi : -std::numbers::pi / 2;
        const double total = leaf_count(1);
        double cursor = start;
        for (auto child : root->children) {
            const double share = 2 * std::numbers::pi * leaf_count(child) / total;
            wedge(child, cursor, cursor + share, 1, rings);
            cursor += share;
        }
        // Each ring clears the previous one by the boxes' extent along the radius, plus a margin.
        auto extent = [&](long long id) { return std::abs(std::cos(angles[id])) * size(id).w / 2 + std::abs(std::sin(angles[id])) * size(id).h / 2; };
        double radius = 0, previous = size(1).w / 2;
        for (size_t depth = 1; depth < rings.size(); ++depth) {
            double outer = 0;
            for (auto id : rings[depth]) outer = std::max(outer, extent(id));
            double needed = radius + previous + outer + ring_gap;
            for (auto id : rings[depth]) needed = std::max(needed, (size(id).w + 24) / std::max(wedges[id], 1e-3));
            radius = std::min(needed, 900000.0);
            previous = outer;
            for (auto id : rings[depth]) { auto n = node(id); n->x = radius * std::cos(angles[id]); n->y = radius * std::sin(angles[id]); }
        }
    }
public:
    explicit LayoutEngine(State& target) : state(target) {}
    void run() {
        auto root = node(1);
        if (state.layout == LayoutRadial) { radial(); return; }
        root->x = 0; root->y = 0;
        if (!open(1)) return;
        std::vector<long long> right, left;
        double total = 0, used = 0;
        for (auto child : root->children) total += span(child);
        for (auto child : root->children) {
            if (state.layout == LayoutRight || right.empty() || used + span(child) / 2 <= total / 2) { right.push_back(child); used += span(child); }
            else left.push_back(child);
        }
        // Clockwise reading order: right side top to bottom, then left side bottom to top.
        std::reverse(left.begin(), left.end());
        column(right, 1);
        column(left, -1);
    }
};
inline void layout(State& state) { LayoutEngine(state).run(); }
} // namespace document
