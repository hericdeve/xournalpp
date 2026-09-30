#include "ZhangSuenThinner.h"

#include <vector>

namespace xoj::circuit {

auto ZhangSuenThinner::thin(BinaryGrid& grid, int maxIterations) -> int {
    int w = grid.getWidth();
    int h = grid.getHeight();

    int iterations = 0;
    bool hasChanged = true;

    std::vector<std::pair<int, int>> toDelete;
    toDelete.reserve(1024);

    while (hasChanged && iterations < maxIterations) {
        hasChanged = false;
        iterations++;

        // Sub-iteration 1
        toDelete.clear();
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                if (grid.get(x, y) == 0) {
                    continue;
                }

                // Clockwise neighbors
                int p2 = grid.get(x, y - 1) > 0 ? 1 : 0;
                int p3 = grid.get(x + 1, y - 1) > 0 ? 1 : 0;
                int p4 = grid.get(x + 1, y) > 0 ? 1 : 0;
                int p5 = grid.get(x + 1, y + 1) > 0 ? 1 : 0;
                int p6 = grid.get(x, y + 1) > 0 ? 1 : 0;
                int p7 = grid.get(x - 1, y + 1) > 0 ? 1 : 0;
                int p8 = grid.get(x - 1, y) > 0 ? 1 : 0;
                int p9 = grid.get(x - 1, y - 1) > 0 ? 1 : 0;

                int b = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                if (b < 2 || b > 6) {
                    continue;
                }

                // Count 0 -> 1 transitions
                int a = 0;
                if (!p2 && p3) a++;
                if (!p3 && p4) a++;
                if (!p4 && p5) a++;
                if (!p5 && p6) a++;
                if (!p6 && p7) a++;
                if (!p7 && p8) a++;
                if (!p8 && p9) a++;
                if (!p9 && p2) a++;

                if (a != 1) {
                    continue;
                }

                // Sub-iteration 1 conditions
                if (p2 * p4 * p6 != 0) {
                    continue;
                }
                if (p4 * p6 * p8 != 0) {
                    continue;
                }

                toDelete.emplace_back(x, y);
            }
        }

        if (!toDelete.empty()) {
            hasChanged = true;
            for (const auto& [x, y]: toDelete) {
                grid.set(x, y, 0);
            }
        }

        // Sub-iteration 2
        toDelete.clear();
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                if (grid.get(x, y) == 0) {
                    continue;
                }

                int p2 = grid.get(x, y - 1) > 0 ? 1 : 0;
                int p3 = grid.get(x + 1, y - 1) > 0 ? 1 : 0;
                int p4 = grid.get(x + 1, y) > 0 ? 1 : 0;
                int p5 = grid.get(x + 1, y + 1) > 0 ? 1 : 0;
                int p6 = grid.get(x, y + 1) > 0 ? 1 : 0;
                int p7 = grid.get(x - 1, y + 1) > 0 ? 1 : 0;
                int p8 = grid.get(x - 1, y) > 0 ? 1 : 0;
                int p9 = grid.get(x - 1, y - 1) > 0 ? 1 : 0;

                int b = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                if (b < 2 || b > 6) {
                    continue;
                }

                int a = 0;
                if (!p2 && p3) a++;
                if (!p3 && p4) a++;
                if (!p4 && p5) a++;
                if (!p5 && p6) a++;
                if (!p6 && p7) a++;
                if (!p7 && p8) a++;
                if (!p8 && p9) a++;
                if (!p9 && p2) a++;

                if (a != 1) {
                    continue;
                }

                // Sub-iteration 2 conditions
                if (p2 * p4 * p8 != 0) {
                    continue;
                }
                if (p2 * p6 * p8 != 0) {
                    continue;
                }

                toDelete.emplace_back(x, y);
            }
        }

        if (!toDelete.empty()) {
            hasChanged = true;
            for (const auto& [x, y]: toDelete) {
                grid.set(x, y, 0);
            }
        }
    }

    return iterations;
}

}  // namespace xoj::circuit
