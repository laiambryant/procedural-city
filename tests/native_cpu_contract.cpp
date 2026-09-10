#include "native/native_cpu.h"

#include <cppdisplacementx/engine.h>

#include <chrono>
#include <iostream>

int main() {
	cppdx::SpriteAtlas atlas;
	const uint32_t sprite[] = { 0xFF302010u, 0x80706050u, 0x00FFFFFFu, 0xFFEEDDCCu };
	atlas.add_sprite(sprite, 2);
	cppdx::Params params;
	params.iterations = 100;
	params.sprites_enabled = false;
	for (const auto size : { std::pair{ 1u, 1u }, { 17u, 513u }, { 257u, 511u }, { 1024u, 769u } }) {
		for (uint64_t seed : { 0u, 42u, 424242u }) {
			auto commands = cppdx::build_command_list(params, size.first, size.second, seed);
			for (uint32_t rotation : { 0u, 90u, 180u, 270u }) {
				commands.push_back({ cppdx::CMD_SPRITE, cppdx::MODE_SOURCE_OVER, -9, -7,
						(int)size.first + 17, (int)size.first + 17, 0, 100, 0, rotation,
						0, 0, (int)size.first, (int)std::min(size.first + 10, size.second), 0, 0 });
			}
			cppdx::Canvas reference(size.first, size.second);
			std::fill(reference.pixels.begin(), reference.pixels.end(), 0x80402010u);
			auto banded = reference;
			cppdx::composite_cpu(reference, commands, atlas.view(), 1);
			godot::composite_native_cpu(banded, commands, atlas.view());
			if (reference.pixels != banded.pixels) {
				std::cerr << "Parity failed: " << size.first << 'x' << size.second << " seed " << seed << '\n';
				return 1;
			}
		}
	}
	std::cout << "native_cpu_contract: byte-identical across 12 size/seed cases and all sprite rotations\n";
	params.iterations = 420;
	const auto commands = cppdx::build_command_list(params, 2048, 2048, 424242);
	for (bool banded : { false, true }) {
		for (int run = 0; run < 4; run++) {
			cppdx::Canvas canvas(2048, 2048);
			const auto start = std::chrono::steady_clock::now();
			if (banded) {
				godot::composite_native_cpu(canvas, commands, atlas.view());
			} else {
				cppdx::composite_cpu(canvas, commands, atlas.view(), 0);
			}
			std::cout << (banded ? "banded" : "baseline") << " run " << run << ": "
					  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms\n";
		}
	}
}
