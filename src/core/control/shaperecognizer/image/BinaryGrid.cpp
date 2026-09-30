#include "BinaryGrid.h"

#include <algorithm>
#include <queue>

namespace xoj::circuit {

BinaryGrid::BinaryGrid(int width, int height):
        width(std::max(1, width)), height(std::max(1, height)), data(static_cast<size_t>(this->width) * this->height, 0) {}

void BinaryGrid::clear() {
    std::fill(data.begin(), data.end(), 0);
}

auto BinaryGrid::count8Neighbors(int x, int y) const -> int {
    static const int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    int count = 0;
    for (int i = 0; i < 8; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        if (get(nx, ny) > 0) {
            count++;
        }
    }
    return count;
}

auto BinaryGrid::findConnectedBlobs(int minPixelCount) const -> std::vector<ConnectedBlob> {
    std::vector<ConnectedBlob> blobs;
    std::vector<bool> visited(data.size(), false);

    static const int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>(y) * width + x;
            if (data[idx] == 0 || visited[idx]) {
                continue;
            }

            ConnectedBlob blob;
            blob.minX = x;
            blob.maxX = x;
            blob.minY = y;
            blob.maxY = y;

            std::queue<std::pair<int, int>> q;
            q.emplace(x, y);
            visited[idx] = true;

            while (!q.empty()) {
                auto [cx, cy] = q.front();
                q.pop();

                blob.pixels.emplace_back(cx, cy);
                blob.minX = std::min(blob.minX, cx);
                blob.maxX = std::max(blob.maxX, cx);
                blob.minY = std::min(blob.minY, cy);
                blob.maxY = std::max(blob.maxY, cy);

                for (int i = 0; i < 8; ++i) {
                    int nx = cx + dx[i];
                    int ny = cy + dy[i];
                    if (inBounds(nx, ny)) {
                        size_t nidx = static_cast<size_t>(ny) * width + nx;
                        if (data[nidx] > 0 && !visited[nidx]) {
                            visited[nidx] = true;
                            q.emplace(nx, ny);
                        }
                    }
                }
            }

            blob.pixelCount = blob.pixels.size();
            if (static_cast<int>(blob.pixelCount) >= minPixelCount) {
                blobs.push_back(std::move(blob));
            }
        }
    }

    return blobs;
}

void BinaryGrid::clearBlob(const ConnectedBlob& blob) {
    for (const auto& [x, y]: blob.pixels) {
        set(x, y, 0);
    }
}

}  // namespace xoj::circuit
