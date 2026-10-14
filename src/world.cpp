#include "world.h"
#include "chunk.h"
#include "shader.h"
#include "primitives/mesh.h"
#include "generation.h"
#include <unordered_set>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <chrono>
#include <thread>
#include <tracy/Tracy.hpp>
#include <limits>

World::World(GenerationType generationType, uint32_t seed) : generationType(generationType), seed(seed) {

}

World::~World() {
	// Handle generation threads
	stopGeneration.store(true);
	generationCondition.notify_all();

	for (auto& thread : generationThreads) {
		if (thread.joinable()) {
			thread.join();
		}
	}

	// Handle meshing threads
	stopMeshing.store(true);
	meshingCondition.notify_all();

	for (auto& thread : meshingThreads) {
		if (thread.joinable()) {
			thread.join();
		}
	}
}

void World::update(const glm::ivec3& worldPosition, const int renderDistance, const glm::mat4& view, const glm::mat4& projection) {
	// Extract frustrum planes (for culling)
	const glm::mat4 vpt = glm::transpose(projection * view);
	const std::vector<glm::vec4> frustumPlanes = {
		(vpt[3] + vpt[0]),
		(vpt[3] - vpt[0]),
		(vpt[3] + vpt[1]),
		(vpt[3] - vpt[1]),
		(vpt[3] + vpt[2]),
		(vpt[3] - vpt[2])
	};

	glm::ivec2 centerChunkIndex = getChunkIndex(worldPosition);
	chunksToDraw.clear();

	{
		ZoneScopedN("Unload Chunks");
		std::shared_lock lock1(chunksMutex);

		const int unloadDistance = static_cast<int>(renderDistance * 1.5f);

		for (auto it = chunks.begin(); it != chunks.end();) {
			const glm::ivec2& chunkIndex = it->first;

			if (std::abs(chunkIndex.x - centerChunkIndex.x) > unloadDistance || std::abs(chunkIndex.y - centerChunkIndex.y) > unloadDistance) {
				it = chunks.erase(it);
			}
			else {
				it++;
			}
		}
		std::shared_lock lock2(meshesMutex);

		for (auto it = meshes.begin(); it != meshes.end();) {
			const glm::ivec2& chunkIndex = it->first;

			if (std::abs(chunkIndex.x - centerChunkIndex.x) > unloadDistance || std::abs(chunkIndex.y - centerChunkIndex.y) > unloadDistance) {
				it = meshes.erase(it);
			}
			else {
				it++;
			}
		}
	}

	{
		ZoneScopedN("Process Chunks");

		// Process chunks in render distance (really need to merge the branch that splits this up, and only need to do some parts when moving to a new chunk)
		for (int x = -renderDistance; x <= renderDistance; x++)
		{
			for (int z = -renderDistance; z <= renderDistance; z++)
			{
				ZoneScopedN("Process Chunk");

				glm::ivec2 currentChunkPos = centerChunkIndex + glm::ivec2(x, z);
				std::shared_ptr<ChunkMesh> currentMesh;

				// Skip if chunk isn't visible
				if (!frustrumAABBVisibility(currentChunkPos, frustumPlanes)) {
					continue;
				}

				// Skip if mesh doesn't exist
				{
					std::shared_lock lock(meshesMutex);

					auto it = meshes.find(currentChunkPos);
					if (it == meshes.end()) {
						continue;
					}

					currentMesh = it->second;
				}

				// Calculate max distance and distance to chunk center in world space
				glm::vec2 chunkCenterWorld = getChunkCenterWorld(currentChunkPos);
				float distanceToChunkCenterWorld = glm::length(chunkCenterWorld - glm::vec2(worldPosition.x, worldPosition.z));
				float maxDistanceWorld = float(renderDistance) * float(CHUNK_SIZE);

				// Skip if outside render distance
				if (distanceToChunkCenterWorld > maxDistanceWorld) {
					continue;
				}

				// Update mesh
				currentMesh->update();

				// Skip if mesh isn't valid
				if (!currentMesh->isValid()) {
					continue;
				}

				// Add to draw list
				chunksToDraw.push_back({ currentMesh, currentChunkPos * CHUNK_SIZE, distanceToChunkCenterWorld });
			}
		}
	}

	renderedChunkCount = chunksToDraw.size();
}

void World::drawOpaque(const glm::ivec3& worldPosition, const int renderDistance, const glm::mat4& view, const glm::mat4& projection, Shader& shader, const bool wireframe) {
	ZoneScopedN("World Draw");

	// Set polygon mode to line if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDisable(GL_CULL_FACE);
	}

	// Draw chunks
	{
		ZoneScopedN("Draw Chunks");

		for (const ChunkDrawingInfo& chunkInfo : chunksToDraw) {
			chunkInfo.mesh->drawOpaque(chunkInfo.offset, view, projection, shader);
		}
	}

	// Reset polygon mode if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glEnable(GL_CULL_FACE);
	}
}

void World::drawWater(const glm::ivec3& worldPosition, const int renderDistance, const glm::mat4& view, const glm::mat4& projection, Shader& shader, const bool wireframe) {
	ZoneScopedN("World Draw Water");

	// Set polygon mode to line if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDisable(GL_CULL_FACE);
	}

	// Draw chunks
	{
		ZoneScopedN("Draw Chunks");

		for (const ChunkDrawingInfo& chunkInfo : chunksToDraw) {
			chunkInfo.mesh->drawWater(chunkInfo.offset, view, projection, shader);
		}
	}

	// Reset polygon mode if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glEnable(GL_CULL_FACE);
	}
}

bool World::hasVoxel(const glm::ivec3& worldPosition, bool ignoreLiquid) {
	glm::ivec2 chunkIndex = getChunkIndex(worldPosition);

	std::shared_ptr<Chunk> chunk;
	{
		std::shared_lock lock(chunksMutex);
		auto it = chunks.find(chunkIndex);
		if (it == chunks.end()) {
			return false;
		}
		chunk = it->second;
	}

	glm::ivec3 localPosition = getLocalPosition(worldPosition);
	return chunk->hasVoxel(localPosition, ignoreLiquid);
}

void World::setVoxel(const glm::ivec3& worldPosition, const VoxelType type) {
	const glm::ivec2 chunkIndex = getChunkIndex(worldPosition);
	const glm::ivec3 localPosition = getLocalPosition(worldPosition);

	const int dx = (localPosition.x == CHUNK_SIZE - 1) - (localPosition.x == 0);
	const int dz = (localPosition.z == CHUNK_SIZE - 1) - (localPosition.z == 0);

	std::shared_ptr<Chunk> chunk;
	std::array<std::shared_ptr<Chunk>, 2> neighbors;
	{
		std::shared_lock lock(chunksMutex);

		auto it = chunks.find(chunkIndex);
		if (it == chunks.end()) {
			return;
		}
		chunk = it->second;

		// Grab neighbors if this is a border voxel
		if (dx != 0) {
			auto itNeighbor = chunks.find(chunkIndex + glm::ivec2(dx, 0));
			if (itNeighbor != chunks.end()) {
				neighbors[0] = itNeighbor->second;
			}
		}

		if (dz != 0) {
			auto itNeighbor = chunks.find(chunkIndex + glm::ivec2(0, dz));
			if (itNeighbor != chunks.end()) {
				neighbors[1] = itNeighbor->second;
			}
		}
	}

	// Set voxel type and get result
	const VoxelChange change = chunk->setVoxelType(localPosition, type);
	
	// Mark neighbors dirty if occupancy changed
	if (change == VoxelChange::OCCUPANCY) {
		for (const std::shared_ptr<Chunk>& neighbor : neighbors) {
			if (neighbor) {
				neighbor->markDirty();
			}
		}
	}
}

void World::removeVoxel(const glm::ivec3& worldPosition) {
	setVoxel(worldPosition, VoxelType::EMPTY);
}

// Amanatides and Woo DDA algorithm
std::optional<VoxelHit> World::raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const float maxDistance)
{
	// Setup
	const glm::vec3 direction = glm::normalize(rayDirection);
	const glm::vec3 start = rayOrigin + 0.5f;

	glm::ivec3 voxel = glm::ivec3(glm::floor(start));

	// Setup each axis
	glm::ivec3 step;
	glm::vec3 tMax;
	glm::vec3 tDelta;

	for (int i = 0; i < 3; i++)
	{
		if (direction[i] > 0.0f) {
			step[i] = 1;
			tDelta[i] = 1.0f / direction[i];
			tMax[i] = (voxel[i] + 1.0f - start[i]) * tDelta[i];
		}
		else if (direction[i] < 0.0f) {
			step[i] = -1;
			tDelta[i] = -1.0f / direction[i];
			tMax[i] = (start[i] - voxel[i]) * tDelta[i];
		}
		else {
			step[i] = 0;
			tDelta[i] = std::numeric_limits<float>::infinity();
			tMax[i] = std::numeric_limits<float>::infinity();
		}
	}

	// Traversal loop
	while (true) {
		int axis = 0;
		if (tMax.y < tMax[axis]) axis = 1;
		if (tMax.z < tMax[axis]) axis = 2;

		if (tMax[axis] > maxDistance) {
			return std::nullopt;
		}

		voxel[axis] += step[axis];
		tMax[axis] += tDelta[axis];

		if (hasVoxel(voxel, true)) {
			glm::ivec3 normal{ 0 };
			normal[axis] = -step[axis];

			return VoxelHit{ voxel, normal };
		}
	}
}

int World::getChunkCount() {
	std::shared_lock lock(chunksMutex);
	return static_cast<int>(chunks.size());
}

int World::getRenderedChunkCount() {
	return static_cast<int>(renderedChunkCount);
}

glm::ivec2 World::getChunkIndex(const glm::ivec3& worldPosition) {
	glm::vec2 chunkPos = glm::floor(glm::vec2(worldPosition.x, worldPosition.z) / float(CHUNK_SIZE));
	return glm::ivec2(chunkPos);
}

glm::ivec2 World::getChunkCenterWorld(const glm::ivec2& chunkIndex) {
	glm::ivec2 center = (chunkIndex * CHUNK_SIZE) + int(CHUNK_SIZE * 0.5f);
	return center;
}

glm::ivec3 World::getLocalPosition(const glm::ivec3& worldPosition) {
	glm::ivec3 localPosition(0);
	localPosition.x = worldPosition.x % CHUNK_SIZE;
	localPosition.z = worldPosition.z % CHUNK_SIZE;
	localPosition.y = worldPosition.y;

	// Handle negatives
	if (localPosition.x < 0) localPosition.x += CHUNK_SIZE;
	if (localPosition.z < 0) localPosition.z += CHUNK_SIZE;

	return localPosition;
}

ChunkNeighbors World::getChunkNeighbors(glm::ivec2 chunkIndex) {
	ChunkNeighbors neighbors = {};

	std::shared_lock lock(chunksMutex);

	auto it = chunks.find(chunkIndex + DirectionVectors2D::PX);
	if (it != chunks.end()) {
		neighbors.px = it->second;
	}

	it = chunks.find(chunkIndex + DirectionVectors2D::NX);
	if (it != chunks.end()) {
		neighbors.nx = it->second;
	}

	it = chunks.find(chunkIndex + DirectionVectors2D::PZ);
	if (it != chunks.end()) {
		neighbors.pz = it->second;
	}

	it = chunks.find(chunkIndex + DirectionVectors2D::NZ);
	if (it != chunks.end()) {
		neighbors.nz = it->second;
	}

	return neighbors;
}

void World::startGenerationThreads() {
	std::call_once(startedGenerationThreads, [this]() {
		// Create chunk generation threads
		for (int i = 0; i < 4; i++) {
			generationThreads.emplace_back([this, i]() {
				tracy::SetThreadName(("Chunk Generation Thread " + std::to_string(i)).c_str());

				while (!stopGeneration.load()) {
					ZoneScopedN("Process Chunk");

					glm::ivec2 chunkIndex;

					// Get next chunk to generate
					{
						std::unique_lock<std::mutex> lock(generationQueueMutex);
						generationCondition.wait(lock, [this] { return stopGeneration.load() || !generationQueue.empty(); });

						if (stopGeneration.load()) {
							break;
						}

						std::lock_guard<std::mutex> lock2(generationProcessingListMutex);

						chunkIndex = generationQueue.top().second;

						if (std::find(generationProcessingList.begin(), generationProcessingList.end(), chunkIndex) == generationProcessingList.end()) {
							generationQueue.pop();
							generationProcessingList.push_back(chunkIndex);
						}
						else {
							continue;
						}
					}

					// Generate chunk
					generateChunk(chunkIndex);

					// Remove from processing list
					std::lock_guard<std::mutex> lock(generationProcessingListMutex);
					generationProcessingList.erase(std::remove(generationProcessingList.begin(), generationProcessingList.end(), chunkIndex), generationProcessingList.end());
				}
			});
		}
	});
}

void World::startMeshingThreads() {
	std::call_once(startedMeshingThreads, [this]() {
		// Create chunk generation threads
		for (int i = 0; i < 4; i++) {
			meshingThreads.emplace_back([this, i]() {
				tracy::SetThreadName(("Meshing Thread " + std::to_string(i)).c_str());

				while (!stopMeshing.load()) {
					ZoneScopedN("Mesh Chunk");

					glm::ivec2 chunkIndex;

					// Get next chunk to mesh
					{
						std::unique_lock<std::mutex> lock(meshingQueueMutex);
						meshingCondition.wait(lock, [this] { return stopMeshing.load() || !meshingQueue.empty(); });

						if (stopMeshing.load()) {
							break;
						}

						std::lock_guard<std::mutex> lock2(meshingProcessingListMutex);

						chunkIndex = meshingQueue.top().second;

						if (std::find(meshingProcessingList.begin(), meshingProcessingList.end(), chunkIndex) == meshingProcessingList.end()) {
							meshingQueue.pop();
							meshingProcessingList.push_back(chunkIndex);
						}
						else {
							continue;
						}
					}

					// Get chunk, queue should guarantee it exists
					std::shared_ptr<Chunk> chunk;
					{
						std::shared_lock lock(chunksMutex);
						auto it = chunks.find(chunkIndex);
						if (it == chunks.end()) {
							std::cerr << "Chunk doesn't exist!" << std::endl;
							continue;
						}
						chunk = it->second;
					}

					// Get existing mesh or make a new one
					std::shared_ptr<ChunkMesh> mesh;
					{
						std::unique_lock lock(meshesMutex);
						auto it = meshes.find(chunkIndex);
						if (it != meshes.end()) {
							mesh = it->second;
						}
						else {
							mesh = std::make_shared<ChunkMesh>();
							meshes[chunkIndex] = mesh;
						}
					}

					// Mesh
					if (!mesh->isValid() || chunk->isDirty()) {
						chunk->clearDirty();
						mesh->build(chunk, getChunkNeighbors(chunkIndex));
					}

					// Remove from processing list
					std::lock_guard<std::mutex> lock(meshingProcessingListMutex);
					meshingProcessingList.erase(std::remove(meshingProcessingList.begin(), meshingProcessingList.end(), chunkIndex), meshingProcessingList.end());
				}
				});
		}
		});
}

void World::updateGenerationQueue(const glm::ivec3& worldPosition, const int renderDistance) {
	ZoneScopedN("Update Generation Queue");

	startGenerationThreads();

	std::priority_queue<std::pair<float, glm::ivec2>, std::vector<std::pair<float, glm::ivec2>>, ChunkQueueCompare> tempQueue;

	glm::ivec2 centerChunkIndex = getChunkIndex(worldPosition);
	glm::vec2 worldPos2D(worldPosition.x, worldPosition.z);

	std::lock_guard<std::mutex> processingLock(generationProcessingListMutex);

	// Add chunks within render distance to the generation queue
	for (int x = -renderDistance; x <= renderDistance; x++)
	{
		for (int z = -renderDistance; z <= renderDistance; z++)
		{
			glm::ivec2 current = centerChunkIndex + glm::ivec2(x, z);

			// Skip if chunk has already been generated
			{
				std::shared_lock chunksLock(chunksMutex);
				auto it = chunks.find(current);
				if (it != chunks.end()) {
					continue;
				}
			}

			// Skip if chunk already being processed
			if (std::find(generationProcessingList.begin(), generationProcessingList.end(), current) != generationProcessingList.end()) {
				continue;
			}

			// Calculate distance to chunk center in world space
			glm::vec2 chunkCenterWorld = getChunkCenterWorld(current);
			float distanceToChunkCenterWorld = glm::length(chunkCenterWorld - worldPos2D);

			tempQueue.push(std::make_pair(-distanceToChunkCenterWorld, current));
		}
	}

	// Swap the old gen queue with the new one
	std::lock_guard<std::mutex> genLock(generationQueueMutex);
	generationQueue = std::move(tempQueue);

	// Notify generation threads
	generationCondition.notify_all();
}

void World::updateMeshingQueue(const glm::ivec3& worldPosition, const int renderDistance) {
	ZoneScopedN("Update Meshing Queue");

	startMeshingThreads();

	std::priority_queue<std::pair<float, glm::ivec2>, std::vector<std::pair<float, glm::ivec2>>, ChunkQueueCompare> tempQueue;

	glm::ivec2 centerChunkIndex = getChunkIndex(worldPosition);
	glm::vec2 worldPos2D(worldPosition.x, worldPosition.z);

	std::lock_guard<std::mutex> processingLock(meshingProcessingListMutex);

	// Add chunks within render distance to the meshing queue if they need an update
	for (int x = -renderDistance; x <= renderDistance; x++)
	{
		for (int z = -renderDistance; z <= renderDistance; z++)
		{
			glm::ivec2 current = centerChunkIndex + glm::ivec2(x, z);
			std::shared_ptr<Chunk> chunk;

			// Skip if chunk hasn't been generated yet
			{
				std::shared_lock chunksLock(chunksMutex);

				auto it = chunks.find(current);
				if (it == chunks.end()) {
					continue;
				}
				chunk = it->second;
			}

			// Skip if neighbors haven't been generated yet
			{
				ChunkNeighbors neighbors = getChunkNeighbors(current);

				if (!neighbors.px || !neighbors.nx || !neighbors.pz || !neighbors.nz) {
					continue;
				}
			}

			// Skip if mesh exists and doesn't need an update
			{
				std::shared_lock meshesLock(meshesMutex);

				auto it = meshes.find(current);
				if (it != meshes.end()) {
					if (!chunk->isDirty()) {
						continue;
					}
				}
			}

			// Skip if mesh is already being processed
			if (std::find(meshingProcessingList.begin(), meshingProcessingList.end(), current) != meshingProcessingList.end()) {
				continue;
			}

			// Calculate distance to chunk center in world space
			glm::vec2 chunkCenterWorld = getChunkCenterWorld(current);
			float distanceToChunkCenterWorld = glm::length(chunkCenterWorld - worldPos2D);

			tempQueue.push(std::make_pair(-distanceToChunkCenterWorld, current));
		}
	}

	// Swap the old mesh queue with the new one
	std::lock_guard<std::mutex> meshLock(meshingQueueMutex);
	meshingQueue = std::move(tempQueue);

	// Notify generation threads
	meshingCondition.notify_all();
}

// Generates a chunk at the given chunk index based on the world's generation type
void World::generateChunk(const glm::ivec2& chunkIndex) {
	ZoneScopedN("Generate Chunk");

	// Check if a chunk already exists at index
	{
		std::lock_guard lock(chunksMutex);

		if (chunks.contains(chunkIndex)) {
			std::cerr << "Tried generating chunk at " << chunkIndex.x << ", " << chunkIndex.y << " but it already exists!" << std::endl;
			return;
		}
	}

	// Generate chunk data
	std::shared_ptr<Chunk> chunk;
	switch (generationType) {
		case GenerationType::Flat:
			chunk = std::make_shared<Chunk>(std::move(*Generation::generateFlat()));
			break;
		case GenerationType::Simple:
			chunk = std::make_shared<Chunk>(std::move(*Generation::generateSimple(seed, chunkIndex)));
			break;
		case GenerationType::Advanced:
			chunk = std::make_shared<Chunk>(std::move(*Generation::generateAdvanced(seed, chunkIndex)));
			break;
		default:
			throw std::runtime_error("Invalid generation type!");
			break;
	}

	{
		ZoneScopedN("Insert");
		std::lock_guard lock(chunksMutex);

		chunks.insert({ chunkIndex, chunk });
	}
}

bool World::frustrumAABBVisibility(const glm::ivec2& chunkIndex, const std::vector<glm::vec4>& frustrumPlanes) {
	glm::vec4 vmin = glm::vec4(chunkIndex.x * CHUNK_SIZE, 0, chunkIndex.y * CHUNK_SIZE, 1.0f);
	glm::vec4 vmax = vmin + glm::vec4(CHUNK_SIZE, MAX_HEIGHT, CHUNK_SIZE, 0.0f);

	for (const glm::vec4& plane : frustrumPlanes) {
		if ((glm::dot(plane, vmin) < 0.0f) &&
			(glm::dot(plane, glm::vec4(vmax.x, vmin.y, vmin.z, 1.0f)) < 0.0f) &&
			(glm::dot(plane, glm::vec4(vmin.x, vmax.y, vmin.z, 1.0f)) < 0.0f) &&
			(glm::dot(plane, glm::vec4(vmax.x, vmax.y, vmin.z, 1.0f)) < 0.0f) &&
			(glm::dot(plane, glm::vec4(vmin.x, vmin.y, vmax.z, 1.0f)) < 0.0f) &&
			(glm::dot(plane, glm::vec4(vmax.x, vmin.y, vmax.z, 1.0f)) < 0.0f) &&
			(glm::dot(plane, glm::vec4(vmin.x, vmax.y, vmax.z, 1.0f)) < 0.0f) &&
			(glm::dot(plane, vmax) < 0.0f))
		{
			return false;
		}
	}

	return true;
}
