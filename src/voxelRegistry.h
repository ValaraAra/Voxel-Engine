#pragma once

#include "structs.h"

class VoxelRegistry {
public:
	static uint16_t Register(VoxelData data);

	static uint16_t GetID(const std::string& stringID);
	static const VoxelData& GetData(const std::string& stringID);

	static const VoxelData& GetData(const uint16_t id) {
		if (id < voxelList.size()) {
			return voxelList[id];
		}

		return ErrorVoxel;
	};

	static constexpr VoxelData EmptyVoxel{ VoxelType::EMPTY, "core:empty", "Empty", Texel{0, 0, 0, 0}, false };
	static constexpr VoxelData ErrorVoxel{ VoxelType::ERROR, "core:error", "ERROR", Texel{255, 70, 160, 255}, true };

	static const uint16_t EmptyVoxelID = 0u;
	static const uint16_t ErrorVoxelID = 1u;
private:
	static std::vector<VoxelData> voxelList;
	static std::unordered_map<std::string, uint16_t> voxelMap;
};