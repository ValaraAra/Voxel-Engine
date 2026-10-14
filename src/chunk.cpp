#include "chunk.h"
#include "voxelRegistry.h"
#include <tracy/Tracy.hpp>
#include <bit>

Chunk::Chunk(VoxelVolume&& data) : voxels(std::move(data.voxels)) {
	voxelCount.store(data.voxelCount);
}

bool Chunk::hasVoxel(const glm::ivec3& chunkPosition, bool ignoreLiquid) const {
	if (!isValidPosition(chunkPosition)) {
		return false;
	}

	std::shared_lock lock(voxelsMutex);

	const VoxelType type = voxels[getVoxelIndex(chunkPosition)].type;
	return type != VoxelType::EMPTY && (!ignoreLiquid || type != VoxelType::LIQUID);
}

std::optional<Voxel> Chunk::getVoxel(const glm::ivec3& position) const {
	if (!isValidPosition(position)) {
		return std::nullopt;
	}

	std::shared_lock lock(voxelsMutex);
	return voxels[getVoxelIndex(position)];
}

bool Chunk::setVoxel(const glm::ivec3& chunkPosition, const VoxelType type, const uint16_t id) {
	if (!isValidPosition(chunkPosition)) {
		return false;
	}

	std::unique_lock lock(voxelsMutex);
	Voxel& voxel = voxels[getVoxelIndex(chunkPosition)];

	// No change
	if ((voxel.type == VoxelType::EMPTY && type == VoxelType::EMPTY) || (voxel.type == type && voxel.id == id)) {
		return false;
	}

	// Update voxel count
	if (type == VoxelType::EMPTY) {
		voxelCount--;
	}
	else if (voxel.type == VoxelType::EMPTY) {
		voxelCount++;
	}

	// Set voxel
	voxel.type = type;
	voxel.id = id;

	dirty.store(true);
	return true;
}

bool Chunk::setVoxel(const glm::ivec3& chunkPosition, const Voxel newVoxel) {
	return setVoxel(chunkPosition, newVoxel.type, newVoxel.id);
}

std::vector<bool> Chunk::setVoxels(const std::vector<std::pair<glm::ivec3, Voxel>>& newVoxels) {
	std::vector<bool> results;
	std::unique_lock lock(voxelsMutex);

	for (const auto& [position, newVoxel] : newVoxels) {
		results.push_back(setVoxel(position, newVoxel));
	}

	return results;
}

void Chunk::clearVoxels() {
	std::unique_lock lock(voxelsMutex);

	for (int i = 0; i < MAX_VOXELS; i++) {
		voxels[i] = {};
	}

	voxelCount.store(0);
	dirty.store(true);
}

// Need to add translucent/transparent mask eventually
ModelPositions Chunk::getMeshingData(ChunkMasks& masks) const {
	ZoneScopedN("Build Meshing Data");

	ModelPositions modelPositions;

	std::shared_lock lock(voxelsMutex);

	for (int y = 0; y < MAX_HEIGHT; y++) {
		for (int z = 0; z < CHUNK_SIZE; z++) {
			uint32_t maskOpaque = 0;
			uint32_t maskLiquid = 0;

			for (int x = 0; x < CHUNK_SIZE; x++) {
				const int index = x + y * CHUNK_SIZE + z * CHUNK_SIZE * MAX_HEIGHT;
				const Voxel& voxel = voxels[index];

				switch (voxel.type) {
					case VoxelType::EMPTY:
						break;
					case VoxelType::ERROR:
						maskOpaque |= (1u << x);
						break;
					case VoxelType::BLOCK:
						if (VoxelRegistry::GetData(voxel.id).color.a == 255) {
							maskOpaque |= (1u << x);
						}
						break;
					case VoxelType::LIQUID:
						maskLiquid |= (1u << x);
						break;
					case VoxelType::MODEL:
						modelPositions[voxel.id].push_back(glm::ivec3{ x, y, z });
						break;
					default: {
						throw std::runtime_error("Invalid voxel type!");
					}
				}
			}

			masks.opaque[y * CHUNK_SIZE + z] = maskOpaque;
			masks.liquid[y * CHUNK_SIZE + z] = maskLiquid;
			masks.filled[y * CHUNK_SIZE + z] = maskOpaque | maskLiquid;
		}
	}

	return modelPositions;
}

uint32_t Chunk::getMask(const int y, const int z, bool liquid) const {
	std::shared_lock lock(voxelsMutex);

	uint32_t mask = 0;

	for (int x = 0; x < CHUNK_SIZE; x++) {
		const int index = x + y * CHUNK_SIZE + z * CHUNK_SIZE * MAX_HEIGHT;
		const Voxel& voxel = voxels[index];

		if (liquid ? (voxel.type != VoxelType::EMPTY) : VoxelRegistry::GetData(voxel.id).color.a == 255) {
			mask |= (1u << x);
		}
	}

	return mask;
}

void Chunk::emitChunkFaces(uint32_t mask, int y, int z, uint8_t direction, std::vector<Face>&faceVector) const {
	while (mask) {
		int x = std::countr_zero(mask);
		mask &= mask - 1;

		glm::ivec3 position = glm::ivec3(x, y, z);

		// Get voxel
		const auto voxelOpt = getVoxel(position);
		if (!voxelOpt) continue;

		// Build face
		Face face;
		FacePacked::setPosition(face, position);
		FacePacked::setDirection(face, direction);
		FacePacked::setTexID(face, voxelOpt->id);

		faceVector.push_back(face);
	}
}

std::vector<Face> Chunk::buildChunkFaces(const ChunkMasks& masks, const ChunkNeighbors& neighbors, const bool liquid) const {
	ZoneScopedN("Mask Meshing");

	const std::array<uint32_t, CHUNK_SIZE* MAX_HEIGHT>& occupancyMasks = liquid ? masks.liquid : masks.opaque;
	const std::array<uint32_t, CHUNK_SIZE* MAX_HEIGHT>& occlusionMasks = liquid ? masks.filled : masks.opaque;

	std::vector<Face> faces;

	const int CHUNK_SIZE_MINUS_ONE = CHUNK_SIZE - 1;
	const int MAX_HEIGHT_MINUS_ONE = MAX_HEIGHT - 1;

	for (int y = 0; y < MAX_HEIGHT; y++) {
		for (int z = 0; z < CHUNK_SIZE; z++) {
			const int index = y * CHUNK_SIZE + z;
			const uint32_t current = occupancyMasks[index];

			// Skip empty
			if (current == 0) {
				continue;
			}

			// px
			uint32_t px = current & ~(occlusionMasks[index] >> 1);

			if (neighbors.px) {
				bool neighborSolid = neighbors.px->hasVoxel(glm::ivec3(0, y, z), !liquid);
				if (neighborSolid) {
					px &= ~(1u << CHUNK_SIZE_MINUS_ONE);
				}
			}
			else {
				px |= (current & (1u << CHUNK_SIZE_MINUS_ONE));
			}

			emitChunkFaces(px, y, z, static_cast<uint8_t>(Direction::PX), faces);

			// nx
			uint32_t nx = current & ~(occlusionMasks[index] << 1);

			if (neighbors.nx) {
				bool neighborSolid = neighbors.nx->hasVoxel(glm::ivec3(CHUNK_SIZE_MINUS_ONE, y, z), !liquid);
				if (neighborSolid) {
					nx &= ~1u;
				}
			}
			else {
				nx |= (current & 1u);
			}

			emitChunkFaces(nx, y, z, static_cast<uint8_t>(Direction::NX), faces);

			// pz
			uint32_t pz;

			if (z < CHUNK_SIZE_MINUS_ONE) {
				pz = current & ~occlusionMasks[index + 1];
			}
			else if (neighbors.pz) {
				pz = current & ~neighbors.pz->getMask(y, 0, liquid);
			}
			else {
				pz = current;
			}

			emitChunkFaces(pz, y, z, static_cast<uint8_t>(Direction::PZ), faces);

			// nz
			uint32_t nz;

			if (z > 0) {
				nz = current & ~occlusionMasks[index - 1];
			}
			else if (neighbors.nz) {
				nz = current & ~neighbors.nz->getMask(y, CHUNK_SIZE_MINUS_ONE, liquid);
			}
			else {
				nz = current;
			}

			emitChunkFaces(nz, y, z, static_cast<uint8_t>(Direction::NZ), faces);

			// py
			uint32_t py;

			if (y < MAX_HEIGHT_MINUS_ONE) {
				py = current & ~occlusionMasks[(y + 1) * CHUNK_SIZE + z];
			}
			else {
				py = current;
			}

			emitChunkFaces(py, y, z, static_cast<uint8_t>(Direction::PY), faces);

			// ny
			uint32_t ny;

			if (y > 0) {
				ny = current & ~occlusionMasks[(y - 1) * CHUNK_SIZE + z];
			}
			else {
				ny = current;
			}

			emitChunkFaces(ny, y, z, static_cast<uint8_t>(Direction::NY), faces);
		}
	}

	return faces;
}

ChunkMeshingOutput Chunk::buildChunkMesh(const ChunkNeighbors& neighbors) const {
	ZoneScopedN("Start Mask Meshing");

	ChunkMeshingOutput output;

	// Skip if empty
	if (isEmpty()) {
		return output;
	}

	// Get meshing data
	thread_local ChunkMasks masks;
	output.modelPositions = getMeshingData(masks);

	// Build faces
	output.facesOpaque = buildChunkFaces(masks, neighbors, false);
	output.facesLiquid = buildChunkFaces(masks, neighbors, true);

	return output;
}
