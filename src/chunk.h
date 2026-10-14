#pragma once

#include "structs.h"
#include "generation.h"
#include "primitives/mesh.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <array>
#include <atomic>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <unordered_map>

class Chunk;

using ModelPositions = std::unordered_map<uint16_t, std::vector<glm::ivec3>>;

struct ChunkNeighbors {
	std::shared_ptr<Chunk> px;
	std::shared_ptr<Chunk> nx;
	std::shared_ptr<Chunk> pz;
	std::shared_ptr<Chunk> nz;
};

struct ChunkMasks {
	std::array<uint32_t, CHUNK_SIZE* MAX_HEIGHT> opaque = {};
	std::array<uint32_t, CHUNK_SIZE* MAX_HEIGHT> liquid = {};
	std::array<uint32_t, CHUNK_SIZE* MAX_HEIGHT> filled = {};
};

struct ChunkMeshingOutput {
	glm::ivec2 chunkIndex = {};
	std::vector<Face> facesOpaque = {};
	std::vector<Face> facesLiquid = {};
	ModelPositions modelPositions = {};
};

struct ChunkMeshData {
	std::unique_ptr<Mesh> meshOpaque = nullptr;
	std::unique_ptr<Mesh> meshLiquid = nullptr;
	ModelPositions modelPositions = {};
};

class Chunk {
public:
	Chunk(VoxelVolume&& data);

	// Voxel access/manipulation
	bool hasVoxel(const glm::ivec3& chunkPosition, bool ignoreLiquid = false) const;
	std::optional<Voxel> getVoxel(const glm::ivec3& chunkPosition) const;
	bool setVoxel(const glm::ivec3& chunkPosition, const VoxelType type, const uint16_t id = 0);
	bool setVoxel(const glm::ivec3& chunkPosition, const Voxel newVoxel);
	std::vector<bool> setVoxels(const std::vector<std::pair<glm::ivec3, Voxel>>& newVoxels);
	void clearVoxels();

	// Meshing
	ModelPositions getMeshingData(ChunkMasks& masks) const;
	uint32_t getMask(const int y, const int z, bool liquid) const;
	void emitChunkFaces(uint32_t mask, int y, int z, uint8_t direction, std::vector<Face>& faceVector) const;
	std::vector<Face> buildChunkFaces(const ChunkMasks& masks, const ChunkNeighbors& neighbors, const bool liquid) const;
	ChunkMeshingOutput buildChunkMesh(const ChunkNeighbors& neighbors) const;

	// Utility
	bool isEmpty() const { return voxelCount.load() <= 0; }
	int getVoxelCount() const { return voxelCount.load(); }
	bool isDirty() const { return dirty.load(); }
	void clearDirty() { dirty.store(false); }

private:
	std::array<Voxel, MAX_VOXELS> voxels;
	mutable std::shared_mutex voxelsMutex;

	std::atomic<int> voxelCount = 0;
	std::atomic<bool> dirty = false;

	static bool isValidPosition(const glm::ivec3& chunkPosition) {
		return (chunkPosition.x >= 0 && chunkPosition.x < CHUNK_SIZE && chunkPosition.y >= 0 && chunkPosition.y < MAX_HEIGHT && chunkPosition.z >= 0 && chunkPosition.z < CHUNK_SIZE);
	};

	static int getVoxelIndex(const glm::ivec3& chunkPosition) {
		return chunkPosition.x + chunkPosition.y * CHUNK_SIZE + chunkPosition.z * CHUNK_SIZE * MAX_HEIGHT;
	};
};