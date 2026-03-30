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

World::World(GenerationType generationType, uint32_t seed) : generationType(generationType), seed(seed) {
	// Start generation threads
	for (int i = 0; i < 4; i++) {
		generationThreads.emplace_back(&World::generationWorkerThread, this, i);
	}

	// Start meshing threads
	for (int i = 0; i < 4; i++) {
		meshingThreads.emplace_back(&World::meshingWorkerThread, this, i);
	}
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
		const int unloadDistance = static_cast<int>(renderDistance * 1.5f);

		// Unload chunks outside render distance
		std::shared_lock lock1(chunksMutex);
		for (auto it = chunks.begin(); it != chunks.end();) {
			const glm::ivec2& chunkIndex = it->first;

			if (std::abs(chunkIndex.x - centerChunkIndex.x) > unloadDistance || std::abs(chunkIndex.y - centerChunkIndex.y) > unloadDistance) {
				it = chunks.erase(it);
			}
			else {
				it++;
			}
		}

		// Unload meshes outside render distance
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
		ZoneScopedN("Process Finished Meshes");

		// Swap drain meshing completed list (if it's not empty)
		std::vector<ChunkMeshingOutput> completed;
		{
			std::lock_guard lock(meshingCompletedListMutex);
			completed.swap(meshingCompletedList);
		}

		// Only process if list isn't empty
		if (!completed.empty()) {
			std::vector<std::pair<glm::ivec2, std::shared_ptr<ChunkMeshData>>> processed;

			// Create mesh data from meshing output
			for (ChunkMeshingOutput& meshingOutput : completed) {
				std::shared_ptr<ChunkMeshData> meshData = std::make_shared<ChunkMeshData>();

				meshData->meshOpaque = std::make_unique<Mesh>(meshingOutput.facesOpaque);
				meshData->meshLiquid = std::make_unique<Mesh>(meshingOutput.facesLiquid);
				meshData->modelPositions = std::move(meshingOutput.modelPositions);

				processed.push_back({ meshingOutput.chunkIndex, meshData });
			}

			// Add to meshes
			{
				std::lock_guard lock(meshesMutex);
				for (const auto& [chunkIndex, meshData] : processed) {
					meshes[chunkIndex] = meshData;
				}
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
				std::shared_ptr<ChunkMeshData> currentMeshData;

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

					currentMeshData = it->second;
				}

				// Calculate max distance and distance to chunk center in world space
				glm::vec2 chunkCenterWorld = getChunkCenterWorld(currentChunkPos);
				float distanceToChunkCenterWorld = glm::length(chunkCenterWorld - glm::vec2(worldPosition.x, worldPosition.z));
				float maxDistanceWorld = float(renderDistance) * float(CHUNK_SIZE);

				// Skip if outside render distance
				if (distanceToChunkCenterWorld > maxDistanceWorld) {
					continue;
				}

				// Add to draw list
				chunksToDraw.push_back({ currentMeshData, currentChunkPos * CHUNK_SIZE, distanceToChunkCenterWorld });
			}
		}
	}

	renderedChunkCount = chunksToDraw.size();
}

void World::drawOpaque(const glm::ivec3& worldPosition, const int renderDistance, const glm::mat4& view, const glm::mat4& projection, Shader& shader, const bool wireframe) {
	ZoneScopedN("World Draw Opaque");

	// Set polygon mode to line if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDisable(GL_CULL_FACE);
	}

	// Draw opaque chunk meshes
	for (const ChunkDrawingInfo& chunkInfo : chunksToDraw) {
		chunkInfo.mesh->meshOpaque->draw(glm::vec3(chunkInfo.offset.x, 0.0f, chunkInfo.offset.y), view, projection, shader);
	}

	// Reset polygon mode if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glEnable(GL_CULL_FACE);
	}
}

void World::drawModels(const glm::ivec3& worldPosition, const int renderDistance, const glm::mat4& view, const glm::mat4& projection, Shader& shader, const bool wireframe) {
	ZoneScopedN("World Draw Models");

	// Set polygon mode to line if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDisable(GL_CULL_FACE);
	}

	// Draw chunk models
	for (const ChunkDrawingInfo& chunkInfo : chunksToDraw) {
		for (const auto& [id, positions] : chunkInfo.mesh->modelPositions) {
			// Not implemented
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

	// Draw liquid chunk meshes
	for (const ChunkDrawingInfo& chunkInfo : chunksToDraw) {
		chunkInfo.mesh->meshLiquid->draw(glm::vec3(chunkInfo.offset.x, 0.0f, chunkInfo.offset.y), view, projection, shader);
	}

	// Reset polygon mode if wireframe mode enabled
	if (wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glEnable(GL_CULL_FACE);
	}
}

bool World::hasVoxel(const glm::ivec3& worldPosition) {
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
	return chunk->hasVoxel(localPosition);
}

bool World::addVoxel(const glm::ivec3& worldPosition) {
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
	return chunk->setVoxel(localPosition, VoxelType::BLOCK);
}

bool World::removeVoxel(const glm::ivec3& worldPosition) {
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
	return chunk->setVoxel(localPosition, VoxelType::EMPTY);
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

void World::generationWorkerThread(const int threadNumber) {
	tracy::SetThreadName(("Chunk Generation Thread " + std::to_string(threadNumber)).c_str());

	while (!stopGeneration.load()) {
		ZoneScopedN("Process Chunk");

		glm::ivec2 chunkIndex;

		{
			std::unique_lock<std::mutex> lock(generationQueueMutex);

			// Handle thread wakeup and check if stop was signaled
			generationCondition.wait(lock, [this] { return stopGeneration.load() || !generationQueue.empty(); });
			if (stopGeneration.load()) break;

			// Get chunk index from queue
			chunkIndex = generationQueue.top().second;
			generationQueue.pop();
		}

		{
			std::lock_guard<std::mutex> lock(generationProcessingListMutex);

			// Skip if chunk is already being processed
			if (std::find(generationProcessingList.begin(), generationProcessingList.end(), chunkIndex) != generationProcessingList.end()) {
				continue;
			}

			// Add to processing list
			generationProcessingList.push_back(chunkIndex);
		}

		// Check if a chunk already exists at index
		{
			std::lock_guard lock(chunksMutex);

			if (chunks.contains(chunkIndex)) {
				std::cerr << "Tried generating chunk at " << chunkIndex.x << ", " << chunkIndex.y << " but it already exists!" << std::endl;
				removeFromGenerationProcessingList(chunkIndex);
				continue;
			}
		}

		// Generate chunk
		std::shared_ptr<Chunk> chunk = generateChunk(chunkIndex);

		{
			std::lock_guard lock(chunksMutex);
			chunks[chunkIndex] = chunk;
		}

		// Remove from processing list
		removeFromGenerationProcessingList(chunkIndex);
	}
}

void World::removeFromGenerationProcessingList(const glm::ivec2& chunkIndex) {
	std::lock_guard<std::mutex> lock(generationProcessingListMutex);
	generationProcessingList.erase(std::remove(generationProcessingList.begin(), generationProcessingList.end(), chunkIndex), generationProcessingList.end());
}

void World::meshingWorkerThread(const int threadNumber) {
	tracy::SetThreadName(("Meshing Thread " + std::to_string(threadNumber)).c_str());

	while (!stopMeshing.load()) {
		ZoneScopedN("Mesh Chunk");

		glm::ivec2 chunkIndex;

		{
			std::unique_lock<std::mutex> lock(meshingQueueMutex);

			// Handle thread wakeup and check if stop was signaled
			meshingCondition.wait(lock, [this] { return stopMeshing.load() || !meshingQueue.empty(); });
			if (stopMeshing.load()) break;

			// Get chunk index from queue
			chunkIndex = meshingQueue.top().second;
			meshingQueue.pop();
		}

		{
			std::lock_guard<std::mutex> lock(meshingProcessingListMutex);

			// Skip if mesh is already being processed
			if (std::find(meshingProcessingList.begin(), meshingProcessingList.end(), chunkIndex) != meshingProcessingList.end()) {
				continue;
			}

			// Add to processing list
			meshingProcessingList.push_back(chunkIndex);
		}

		// Get chunk
		std::shared_ptr<Chunk> chunk;
		{
			std::shared_lock lock(chunksMutex);

			auto it = chunks.find(chunkIndex);
			if (it == chunks.end()) {
				std::cerr << "Chunk doesn't exist!" << std::endl;
				removeFromMeshProcessingList(chunkIndex);
				continue;
			}

			chunk = it->second;
		}

		// Get existing mesh if possible
		std::shared_ptr<ChunkMeshData> mesh;
		{
			std::shared_lock lock(meshesMutex);

			auto it = meshes.find(chunkIndex);
			if (it != meshes.end()) {
				mesh = it->second;
			}
		}

		// Capture data
		ChunkNeighbors neighbors = getChunkNeighbors(chunkIndex);
		bool isDirty = chunk->isDirty();

		chunk->clearDirty();

		// Build new mesh if needed
		if (!mesh || isDirty) {
			ChunkMeshingOutput newMeshData = chunk->buildChunkMesh(neighbors);
			newMeshData.chunkIndex = chunkIndex;

			{
				std::unique_lock lock(meshingCompletedListMutex);
				meshingCompletedList.push_back(std::move(newMeshData));
			}
		}

		// Remove from processing list
		removeFromMeshProcessingList(chunkIndex);
	}
}

void World::removeFromMeshProcessingList(const glm::ivec2& chunkIndex) {
	std::lock_guard<std::mutex> lock(meshingProcessingListMutex);
	meshingProcessingList.erase(std::remove(meshingProcessingList.begin(), meshingProcessingList.end(), chunkIndex), meshingProcessingList.end());
}

void World::updateGenerationQueue(const glm::ivec3& worldPosition, const int renderDistance) {
	ZoneScopedN("Update Generation Queue");

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
				const ChunkNeighbors neighbors = getChunkNeighbors(current);

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
const std::shared_ptr<Chunk> World::generateChunk(const glm::ivec2& chunkIndex) {
	ZoneScopedN("Generate Chunk");

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

	return chunk;
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
