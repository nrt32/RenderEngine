#include "scene/layout.hpp"

#include <algorithm>
#include <cmath>

namespace re::scene {

std::vector<Rect> Layout::resolve(glm::ivec2 windowSize, glm::vec2 contentScale) const {
    const int physW = static_cast<int>(std::round(static_cast<float>(windowSize.x) * contentScale.x));
    const int physH = static_cast<int>(std::round(static_cast<float>(windowSize.y) * contentScale.y));
    return resolvePhysical(glm::ivec2(physW, physH));
}

std::vector<Rect> Layout::resolvePhysical(glm::ivec2 framebufferSize) const {
    std::vector<Rect> out;
    out.reserve(specs.size());
    if (specs.empty()) return out;

    const int physW = framebufferSize.x;
    const int physH = framebufferSize.y;

    // Single view fast-path: whole framebuffer.
    if (specs.size() == 1) {
        out.push_back(Rect{0, 0, physW, physH});
        return out;
    }

    // General grid: compute total rows/cols including rowSpan/colSpan, handling rowSpan==0 as fill remainder.
    int totalRows = 0;
    int totalCols = 0;
    for (const auto& s : specs) {
        int rs = s.rowSpan == 0 ? 1 : s.rowSpan;
        int cs = s.colSpan == 0 ? 1 : s.colSpan;
        totalRows = std::max(totalRows, s.row + rs);
        totalCols = std::max(totalCols, s.col + cs);
    }
    if (totalRows == 0) totalRows = 1;
    if (totalCols == 0) totalCols = 1;

    // Handle rowSpan==0 fill remainder: adjust effective spans where 0.
    // For simplicity, if rowSpan==0, effective rowspan = totalRows - row (fill remainder)
    // Similarly colSpan==0 fill remainder.
    // Compute per-spec effective spans
    std::vector<int> effRowSpan(specs.size());
    std::vector<int> effColSpan(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        effRowSpan[i] = specs[i].rowSpan == 0 ? (totalRows - specs[i].row) : specs[i].rowSpan;
        effColSpan[i] = specs[i].colSpan == 0 ? (totalCols - specs[i].col) : specs[i].colSpan;
        if (effRowSpan[i] <= 0) effRowSpan[i] = 1;
        if (effColSpan[i] <= 0) effColSpan[i] = 1;
    }

    // Group by row for weight distribution (flex). If all weights equal, width splits equally
    // via grid cols. If weights differ, distribute row width proportionally within row.
    // For simplicity, if totalRows==1 (single row, horizontal split) use weight proportional.
    // If totalCols==1 (single column, vertical split) use weight proportional for height.
    // Otherwise use uniform grid.

    // Detect if we should use weight-proportional distribution:
    // If totalRows==1, all specs share same row, weight matters horizontally.
    // If totalCols==1, all share same col, weight matters vertically.
    bool useWeightHorizontal = (totalRows == 1);
    bool useWeightVertical = (totalCols == 1);
    // Also, if specs are on same row group, use weight within that row.
    // For mixed grid, fallback to uniform grid and ignore weight (still within 1px).

    if (useWeightHorizontal) {
        float sumW = 0.0f;
        for (const auto& s : specs) sumW += s.weight > 0 ? s.weight : 1.0f;
        if (sumW == 0) sumW = static_cast<float>(specs.size());
        int consumedX = 0;
        for (size_t i = 0; i < specs.size(); ++i) {
            float w = specs[i].weight > 0 ? specs[i].weight : 1.0f;
            int wi;
            if (i + 1 == specs.size()) {
                wi = physW - consumedX; // remainder to last
            } else {
                wi = static_cast<int>(std::floor(static_cast<float>(physW) * w / sumW));
            }
            int xi = consumedX;
            int yi = 0;
            int hi = physH;
            out.push_back(Rect{xi, yi, wi, hi});
            consumedX += wi;
        }
        return out;
    }
    if (useWeightVertical) {
        float sumW = 0.0f;
        for (const auto& s : specs) sumW += s.weight > 0 ? s.weight : 1.0f;
        if (sumW == 0) sumW = static_cast<float>(specs.size());
        int consumedY = 0;
        for (size_t i = 0; i < specs.size(); ++i) {
            float w = specs[i].weight > 0 ? specs[i].weight : 1.0f;
            int hi;
            if (i + 1 == specs.size()) {
                hi = physH - consumedY;
            } else {
                hi = static_cast<int>(std::floor(static_cast<float>(physH) * w / sumW));
            }
            int yi = consumedY;
            int xi = 0;
            int wi = physW;
            out.push_back(Rect{xi, yi, wi, hi});
            consumedY += hi;
        }
        return out;
    }

    // Uniform grid fallback (within 1px via integer division remainder to last cell per row/col).
    // Compute cell dimensions via integer division with remainder handling for last col/row.
    // For ncols, compute base cellW = physW / totalCols, remainder = physW % totalCols add to last col.
    // Similarly for rows.
    int baseCellW = physW / totalCols;
    int remW = physW % totalCols;
    int baseCellH = physH / totalRows;
    int remH = physH % totalRows;

    for (size_t i = 0; i < specs.size(); ++i) {
        int col = specs[i].col;
        int row = specs[i].row;
        int cs = effColSpan[i];
        int rs = effRowSpan[i];
        // X: sum of widths before col + partial
        int x = 0;
        for (int c = 0; c < col; ++c) {
            int w = baseCellW + (c == totalCols - 1 ? remW : 0);
            x += w;
        }
        int w = 0;
        for (int c = col; c < col + cs; ++c) {
            int cw = baseCellW + (c == totalCols - 1 ? remW : 0);
            w += cw;
        }
        int y = 0;
        for (int r = 0; r < row; ++r) {
            int h = baseCellH + (r == totalRows - 1 ? remH : 0);
            y += h;
        }
        int h = 0;
        for (int r = row; r < row + rs; ++r) {
            int rh = baseCellH + (r == totalRows - 1 ? remH : 0);
            h += rh;
        }
        // Weight adjust within cell: scale w/h by weight normalized (flex).
        // If weight !=1, scale uniformly but keep within cell bounds; for determinism keep w as computed.
        // No extra weight scaling in grid mode (weights equal case already handled).
        out.push_back(Rect{x, y, w, h});
    }
    return out;
}

std::vector<std::pair<uint64_t, Rect>> Layout::resolveWithIds(glm::ivec2 windowSize,
                                                               glm::vec2 contentScale) const {
    auto rects = resolve(windowSize, contentScale);
    std::vector<std::pair<uint64_t, Rect>> out;
    out.reserve(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        out.emplace_back(specs[i].viewId, rects[i]);
    }
    return out;
}

std::optional<Rect> Layout::rectFor(uint64_t viewId, glm::ivec2 windowSize,
                                    glm::vec2 contentScale) const {
    auto rects = resolve(windowSize, contentScale);
    for (size_t i = 0; i < specs.size(); ++i) {
        if (specs[i].viewId == viewId) return rects[i];
    }
    return std::nullopt;
}

} // namespace re::scene
