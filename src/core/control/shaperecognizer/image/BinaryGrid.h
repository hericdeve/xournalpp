/*
 * Xournal++
 * Circuit Diagram Shape Recognition Engine
 *
 * Binary Grid Container for Image-Based Topological Circuit Recognition
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace xoj::circuit {

struct ConnectedBlob {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    size_t pixelCount = 0;
    std::vector<std::pair<int, int>> pixels;

    [[nodiscard]] auto width() const -> int { return maxX - minX + 1; }
    [[nodiscard]] auto height() const -> int { return maxY - minY + 1; }
    [[nodiscard]] auto maxDim() const -> int { return (width() > height()) ? width() : height(); }
};

class BinaryGrid {
public:
    BinaryGrid(int width, int height);
    ~BinaryGrid() = default;

    [[nodiscard]] auto getWidth() const -> int { return width; }
    [[nodiscard]] auto getHeight() const -> int { return height; }

    [[nodiscard]] auto inBounds(int x, int y) const -> bool {
        return x >= 0 && x < width && y >= 0 && y < height;
    }

    [[nodiscard]] auto get(int x, int y) const -> uint8_t {
        if (!inBounds(x, y)) {
            return 0;
        }
        return data[static_cast<size_t>(y) * width + x];
    }

    void set(int x, int y, uint8_t val) {
        if (inBounds(x, y)) {
            data[static_cast<size_t>(y) * width + x] = val;
        }
    }

    void clear();

    [[nodiscard]] auto findConnectedBlobs(int minPixelCount = 4) const -> std::vector<ConnectedBlob>;

    void clearBlob(const ConnectedBlob& blob);

    [[nodiscard]] auto count8Neighbors(int x, int y) const -> int;

    [[nodiscard]] auto rawData() -> std::vector<uint8_t>& { return data; }
    [[nodiscard]] auto rawData() const -> const std::vector<uint8_t>& { return data; }

private:
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;
};

}  // namespace xoj::circuit
