#pragma once

#include "structs.h"
#include <vector>

namespace VoxParser {
	struct RawVoxel {
		uint8_t x = 0;
		uint8_t y = 0;
		uint8_t z = 0;
		uint8_t colorIndex = 0;
	};

	struct VoxChunk {
		char id[4]{};
		uint32_t size = 0;
		uint32_t childSize = 0;
		std::vector<std::byte> data{};
		std::vector<VoxChunk> children{};
	};

	struct VoxSize {
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t z = 0;
	};

	struct VoxVoxels {
		uint32_t numVoxels = 0;
		std::vector<RawVoxel> voxels{};
	};

	bool loadVoxModel(const std::string& filename, VoxelModel& model, uint8_t resolution = 1);
}