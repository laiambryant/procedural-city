#ifndef PARALLEL_ROWS_H
#define PARALLEL_ROWS_H

#include <algorithm>
#include <thread>
#include <vector>

namespace godot {

constexpr int DEFAULT_MIN_ROWS_PER_BAND = 16;

constexpr int HEAVY_MIN_ROWS_PER_BAND = 1;

template <typename EmitRows>
void parallel_for_rows(int p_rows, const EmitRows &p_emit_rows,
		int p_min_rows_per_band = DEFAULT_MIN_ROWS_PER_BAND) {
	const int min_rows = std::max(1, p_min_rows_per_band);
	const int hw = (int)std::thread::hardware_concurrency();
	const int bands = std::clamp(p_rows / min_rows, 1, std::max(1, hw));
	if (bands <= 1) {
		p_emit_rows(0, p_rows);
		return;
	}
	const int step = (p_rows + bands - 1) / bands;
	std::vector<std::thread> workers;
	workers.reserve((size_t)bands);
	for (int b = 1; b < bands; b++) {
		const int begin = b * step;
		const int end = std::min(p_rows, begin + step);
		if (begin >= end) {
			break;
		}
		workers.emplace_back([&p_emit_rows, begin, end]() { p_emit_rows(begin, end); });
	}
	p_emit_rows(0, std::min(p_rows, step));
	for (std::thread &worker : workers) {
		worker.join();
	}
}

} // namespace godot

#endif // PARALLEL_ROWS_H
