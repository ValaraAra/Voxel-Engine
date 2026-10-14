#include "chunk.h"
#include <tracy/Tracy.hpp>

Chunk::Chunk(VoxelVolume&& data) : voxels(std::move(data.voxels)) {
	voxelCount.store(data.voxelCount);
}

bool Chunk::hasVoxel(const glm::ivec3& chunkPosition, bool ignoreLiquid) const {
	if (!isValidPosition(chunkPosition)) {
		return false;
	}

	std::shared_lock lock(voxelsMutex);

	const VoxelType type = voxels[getVoxelIndex(chunkPosition)].type;
	return type != VoxelType::EMPTY && (!ignoreLiquid || !VoxelTypeData[static_cast<uint8_t>(type)].isLiquid);
}

VoxelType Chunk::getVoxelType(const glm::ivec3& position) const {
	if (!isValidPosition(position)) {
		return VoxelType::EMPTY;
	}
	std::shared_lock lock(voxelsMutex);

	return voxels[getVoxelIndex(position)].type;
}

VoxelChange Chunk::setVoxelType(const glm::ivec3& chunkPosition, const VoxelType type) {
	if (!isValidPosition(chunkPosition)) {
		return VoxelChange::NONE;
	}
	std::unique_lock lock(voxelsMutex);

	Voxel& voxel = voxels[getVoxelIndex(chunkPosition)];

	if (voxel.type == type) {
		return VoxelChange::NONE;
	}

	VoxelChange change;
	if (type == VoxelType::EMPTY) {
		change = VoxelChange::OCCUPANCY;
		voxelCount--;
	}
	else if (voxel.type == VoxelType::EMPTY) {
		change = VoxelChange::OCCUPANCY;
		voxelCount++;
	}
	else {
		change = VoxelChange::MATERIAL;
	}

	voxel.type = type;
	dirty.store(true);
	return change;
}

void Chunk::clearVoxels() {
	std::unique_lock lock(voxelsMutex);

	for (int i = 0; i < MAX_VOXELS; i++) {
		voxels[i].type = VoxelType::EMPTY;
	}

	voxelCount.store(0);
	dirty.store(true);
}

void Chunk::getMasks(Masks& masks) const {
	ZoneScopedN("Build Occupancy Masks");
	std::shared_lock lock(voxelsMutex);

	masks.opaque.fill(0);
	masks.liquid.fill(0);
	masks.filled.fill(0);

	for (int y = 0; y < MAX_HEIGHT; y++) {
		for (int z = 0; z < CHUNK_SIZE; z++) {
			uint32_t maskOpaque = 0;
			uint32_t maskWater = 0;
			uint32_t maskTranslucent = 0;

			for (int x = 0; x < CHUNK_SIZE; x++) {
				const int index = x + y * CHUNK_SIZE + z * CHUNK_SIZE * MAX_HEIGHT;
				const VoxelType type = voxels[index].type;

				switch (type) {
				case VoxelType::EMPTY:
					break;
				case VoxelType::WATER:
					maskWater |= (1u << x);
					break;
				default: {
					const uint8_t typeIndex = static_cast<uint8_t>(type);

					// Opaque
					if (VoxelTypeData[typeIndex].color.a == 255) {
						maskOpaque |= (1u << x);
					}
					// Translucent
					else {
						maskTranslucent |= (1u << x);
					}
				}
				}
			}

			masks.opaque[y * CHUNK_SIZE + z] = maskOpaque;
			masks.liquid[y * CHUNK_SIZE + z] = maskWater;
			masks.filled[y * CHUNK_SIZE + z] = maskOpaque | maskWater;
		}
	}
}

uint32_t Chunk::getMask(const int y, const int z, bool liquid) const {
	std::shared_lock lock(voxelsMutex);

	uint32_t mask = 0;

	for (int x = 0; x < CHUNK_SIZE; x++) {
		const VoxelType type = voxels[getVoxelIndex(glm::ivec3(x, y, z))].type;

		if (liquid ? (type != VoxelType::EMPTY) : VoxelTypeData[static_cast<uint8_t>(type)].color.a == 255) {
			mask |= (1u << x);
		}
	}

	return mask;
}
