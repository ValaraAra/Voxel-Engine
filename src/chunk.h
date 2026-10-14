#pragma once

#include "structs.h"
#include "generation.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <array>
#include <atomic>
#include <shared_mutex>
#include <mutex>

// The type of change resulting from setting a voxel type
enum class VoxelChange : uint8_t {
	NONE,
	MATERIAL,
	OCCUPANCY
};

class Chunk {
public:
	Chunk(VoxelVolume&& data);
  
	bool hasVoxel(const glm::ivec3& chunkPosition, bool ignoreLiquid = false) const;
	VoxelType getVoxelType(const glm::ivec3& chunkPosition) const;
	VoxelChange setVoxelType(const glm::ivec3& chunkPosition, const VoxelType type);
	void clearVoxels();

	void getMasks(Masks& masks) const;
	uint32_t getMask(const int y, const int z, bool liquid) const;

	int getVoxelCount() const { return voxelCount.load(); }
	bool isDirty() const { return dirty.load(); }
	void markDirty() { dirty.store(true); }
	void clearDirty() { dirty.store(false); }

private:
	std::array<Voxel, MAX_VOXELS> voxels;
	mutable std::shared_mutex voxelsMutex;

	std::atomic<int> voxelCount = 0;
	std::atomic<bool> dirty = false;

	static bool isValidPosition(const glm::ivec3& chunkPosition) {
		return (chunkPosition.x >= 0 && chunkPosition.x < CHUNK_SIZE && chunkPosition.y >= 0 && chunkPosition.y < MAX_HEIGHT && chunkPosition.z >= 0 && chunkPosition.z < CHUNK_SIZE);
	};

	int getVoxelIndex(const glm::ivec3& chunkPosition) const {
		return chunkPosition.x + chunkPosition.y * CHUNK_SIZE + chunkPosition.z * CHUNK_SIZE * MAX_HEIGHT;
	};
};