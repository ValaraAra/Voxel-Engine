#include "generation.h"
#include "voxParser.h"
#include "voxelRegistry.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace {
	// Heightmap nodes
	static FastNoise::SmartNode<FastNoise::Perlin> fnPerlinHeight = [] {
		auto ptr = FastNoise::New<FastNoise::Perlin>();
		ptr->SetScale(333.0f);
		ptr->SetOutputMin(-0.7f);
		ptr->SetOutputMax(0.7f);
		return ptr;
		}();

	static FastNoise::SmartNode<FastNoise::FractalFBm> fnFractalHeight = [] {
		auto ptr = FastNoise::New<FastNoise::FractalFBm>();
		ptr->SetSource(fnPerlinHeight);
		ptr->SetOctaveCount(5);
		ptr->SetLacunarity(2.5f);
		ptr->SetWeightedStrength(0.4f);
		return ptr;
		}();

	// Density nodes
	static FastNoise::SmartNode<FastNoise::Perlin> fnPerlinDensity = [] {
		auto ptr = FastNoise::New<FastNoise::Perlin>();
		ptr->SetScale(100.0f);
		ptr->SetOutputMin(-1.0f);
		ptr->SetOutputMax(1.0f);
		return ptr;
		}();

	static FastNoise::SmartNode<FastNoise::FractalFBm> fnFractalDensity = [] {
		auto ptr = FastNoise::New<FastNoise::FractalFBm>();
		ptr->SetSource(fnPerlinDensity);
		ptr->SetGain(0.4f);
		ptr->SetWeightedStrength(0.4f);
		ptr->SetOctaveCount(3);
		ptr->SetLacunarity(2.0f);
		return ptr;
		}();

	// Murmurhash mix
	static uint32_t mix32(uint32_t x) {
		x ^= x >> 16;
		x *= 0x85ebca6b;
		x ^= x >> 13;
		x *= 0xc2b2ae35;
		x ^= x >> 16;
		return x;
	}

	static uint32_t hashCoordinates(int x, int y, uint32_t seed) {
		const uint32_t hashX = mix32(static_cast<uint32_t>(x) * 374761393u);
		const uint32_t hashY = mix32(static_cast<uint32_t>(y) * 668265263u);

		return mix32(seed ^ hashX ^ hashY);
	}

	// Convert to float in range [0, 1) using top 24 bits
	static float hashToFloat(uint32_t x) {
		constexpr float scale = 1.0f / 16777216.0f;
		return (x >> 8) * scale;
	}

	// Convert to int in range [min, max] using top 24 bits
	static int hashToInt(uint32_t x, int min, int max) {
		return min + static_cast<int>(hashToFloat(x) * (max - min + 1));
	}

	// Convert noise value to height [0, MAX_HEIGHT - 1]
	static int heightFromNoise(const float noiseValue) {
		const float normalized = glm::clamp((noiseValue + 1.0f) * 0.5f, 0.0f, 1.0f);
		return static_cast<int>(normalized * (MAX_HEIGHT - 1));
	}

	// Load tree models (cached)
	static const std::vector<VoxelModel>& getTreeModels() {
		static const std::vector<std::string> modelPaths = {
			"resources/models/tree1.vox",
			"resources/models/tree2.vox"
		};

		static std::vector<VoxelModel> treeModels = [] {
			std::vector<VoxelModel> models;
			models.reserve(modelPaths.size());

			for (const std::string& path : modelPaths) {
				VoxelModel model;
				if (VoxParser::loadVoxModel(path, model)) {
					models.push_back(std::move(model));
				}
			}

			return models;
			}();

		return treeModels;
	}

	static bool isGrassAtHeight(int heightValue) {
		if (heightValue <= WATER_HEIGHT) {
			return false;
		}

		return true;
	}
}

namespace Generation::Poisson {
	static bool isValidPoisson(const glm::ivec2& candidate, const glm::ivec2& candidateCell, const std::vector<glm::ivec2>& points, const std::vector<int>& grid, const int radiusSquared, const int gridSize, const int size) {
		if (candidate.x < 0 || candidate.x >= size || candidate.y < 0 || candidate.y >= size) {
			return false;
		}

		if (candidateCell.x < 0 || candidateCell.x >= gridSize || candidateCell.y < 0 || candidateCell.y >= gridSize) {
			return false;
		}

		int startX = std::max(0, candidateCell.x - 2);
		int endX = std::min(gridSize - 1, candidateCell.x + 2);
		int startY = std::max(0, candidateCell.y - 2);
		int endY = std::min(gridSize - 1, candidateCell.y + 2);

		for (int x = startX; x <= endX; x++) {
			for (int y = startY; y <= endY; y++) {
				int pointIndex = grid[x + y * gridSize];

				if (pointIndex == -1) {
					continue;
				}

				const glm::ivec2& point = points[pointIndex];
				const glm::ivec2 diff = candidate - point;
				const int dot = diff.x * diff.x + diff.y * diff.y;

				if (dot < radiusSquared) {
					return false;
				}
			}
		}

		return true;
	}

	// Based on Bridson's algorithm
	std::vector<glm::ivec2> generatePoisson(const int size, const int radius, const int kSamples, std::mt19937& rng) {
		const float cellSize = radius / glm::root_two<float>();
		const int gridSize = static_cast<int>(std::ceil(size / cellSize));
		const int radiusSquared = radius * radius;

		std::vector<int> grid(gridSize * gridSize, -1);
		std::vector<glm::ivec2> points;
		std::vector<glm::ivec2> active;

		// Start in the center
		const glm::ivec2 startPoint(size / 2, size / 2);

		const int startCellX = static_cast<int>(startPoint.x / cellSize);
		const int startCellY = static_cast<int>(startPoint.y / cellSize);
		grid[startCellX + startCellY * gridSize] = 0;

		points.push_back(startPoint);
		active.push_back(startPoint);

		// Generate points
		while (!active.empty()) {
			// Pick random point from active
			std::uniform_int_distribution<int> distActive(0, static_cast<int>(active.size()) - 1);
			const int index = distActive(rng);
			const glm::ivec2 point = active[index];

			// Generate new points (up to k) around it (annulus, [r, 2r])
			std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());
			std::uniform_int_distribution<int> distRange(radius, 2 * radius);

			bool accepted = false;

			// Up to k samples per point
			for (int i = 0; i < kSamples; i++) {
				float angle = distAngle(rng);
				int distance = distRange(rng);

				const glm::ivec2 candidate = point + glm::ivec2(std::cos(angle) * distance, std::sin(angle) * distance);
				const glm::ivec2 candidateCell(static_cast<int>(candidate.x / cellSize), static_cast<int>(candidate.y / cellSize));

				// Check if candidate is valid
				if (!isValidPoisson(candidate, candidateCell, points, grid, radiusSquared, gridSize, size)) {
					continue;
				}

				// Accept candidate
				grid[candidateCell.x + candidateCell.y * gridSize] = static_cast<int>(points.size());

				points.push_back(candidate);
				active.push_back(candidate);

				accepted = true;
				break;
			}

			// Remove from active if no candidates were accepted
			if (!accepted) {
				active.erase(active.begin() + index);
			}
		}

		return points;
	}
}

namespace Generation {
	static NoiseOutputPtr generateNoise(const uint32_t seed, const glm::ivec2& offset, const FastNoise::SmartNode<FastNoise::FractalFBm>& noiseNode) {
		NoiseOutputPtr noiseOutput = std::make_shared<std::array<float, CHUNK_SIZE * CHUNK_SIZE>>();
		noiseNode->GenUniformGrid2D(noiseOutput->data(), offset.x * CHUNK_SIZE, offset.y * CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE, 1, 1, seed);

		return noiseOutput;
	}

	static HeightMapPtr generateHeightMap(const uint32_t seed, const glm::ivec2& offset) {
		NoiseOutputPtr noiseOutput = generateNoise(seed, offset, fnFractalHeight);
		auto& noiseOutputRef = *noiseOutput;

		HeightMapPtr heightmap = std::make_shared<std::array<int, CHUNK_SIZE * CHUNK_SIZE>>();
		auto& heightmapRef = *heightmap;

		// Conver to height values
		for (int x = 0; x < CHUNK_SIZE; x++) {
			for (int z = 0; z < CHUNK_SIZE; z++) {
				const float noiseValue = noiseOutputRef[x + z * CHUNK_SIZE];
				const float normalized = glm::clamp((noiseValue + 1.0f) * 0.5f, 0.0f, 1.0f);
				const int height = static_cast<int>(normalized * (MAX_HEIGHT - 1));

				const int index = x + z * CHUNK_SIZE;
				heightmapRef[index] = height;
			}
		}

		return heightmap;
	}

	static std::vector<glm::ivec2> generateTreeMap(const uint32_t seed, const glm::ivec2& offset, const int size, const int radius) {
		const int cellSize = std::max(1, radius);

		const glm::ivec2 boundsMin = offset * size;
		const glm::ivec2 boundsMax = boundsMin + glm::ivec2(size - 1, size - 1);

		const int cellMinX = static_cast<int>(std::floor(boundsMin.x / static_cast<float>(cellSize)));
		const int cellMaxX = static_cast<int>(std::floor(boundsMax.x / static_cast<float>(cellSize)));
		const int cellMinZ = static_cast<int>(std::floor(boundsMin.y / static_cast<float>(cellSize)));
		const int cellMaxZ = static_cast<int>(std::floor(boundsMax.y / static_cast<float>(cellSize)));

		std::vector<glm::ivec2> treePoints;

		for (int x = cellMinX; x <= cellMaxX; x++) {
			for (int z = cellMinZ; z <= cellMaxZ; z++) {
				// Lowest hash wins, starting with current cell
				const uint32_t cellHash = hashCoordinates(x, z, seed);

				// Check neighbors
				bool valid = true;
				for (int cellOffsetX = -1; cellOffsetX <= 1; cellOffsetX++) {
					for (int cellOffsetZ = -1; cellOffsetZ <= 1; cellOffsetZ++) {
						if (cellOffsetX == 0 && cellOffsetZ == 0) continue;

						const uint32_t neighborHash = hashCoordinates(x + cellOffsetX, z + cellOffsetZ, seed);

						if (neighborHash < cellHash) {
							valid = false;
							break;
						}
					}

					if (!valid) break;
				}

				if (!valid) continue;

				// Jitter cell
				const glm::ivec2 jitter(static_cast<int>(hashToFloat(cellHash) * cellSize), static_cast<int>(hashToFloat(cellHash ^ 3039394381u) * cellSize));
				const glm::ivec2 candidate = glm::ivec2(x * cellSize, z * cellSize) + jitter;

				// Skip if outside bounds
				if (candidate.x < boundsMin.x || candidate.x > boundsMax.x || candidate.y < boundsMin.y || candidate.y > boundsMax.y) {
					continue;
				}

				// Add candidate
				const glm::ivec2 localPoint = candidate - boundsMin;
				treePoints.push_back(localPoint);
			}
		}

		return treePoints;
	}

	static void terrainPass(const uint32_t seed, const glm::ivec2& offset, VoxelVolumePtr volume) {
		HeightMapPtr heightmap = generateHeightMap(seed, offset);
		auto& heightmapRef = *heightmap;

		const uint16_t stoneID = VoxelRegistry::GetID("core:stone");
		const uint16_t dirtID = VoxelRegistry::GetID("core:dirt");
		const uint16_t grassID = VoxelRegistry::GetID("core:grass");
		const uint16_t sandID = VoxelRegistry::GetID("core:sand");
		const uint16_t waterID = VoxelRegistry::GetID("core:water");

		for (int x = 0; x < CHUNK_SIZE; x++) {
			for (int z = 0; z < CHUNK_SIZE; z++) {
				const int heightValue = heightmapRef[x + z * CHUNK_SIZE];
				const int baseIndex = x + z * CHUNK_SIZE * MAX_HEIGHT;

				int y = heightValue;

				// Water
				if (heightValue < WATER_HEIGHT) {
					// Water (water height to height value)
					for (y = WATER_HEIGHT; y > heightValue; y--) {
						Voxel& voxel = volume->voxels[baseIndex + y * CHUNK_SIZE];
						voxel.type = VoxelType::LIQUID;
						voxel.id = waterID;
						volume->voxelCount++;
					}

					// Sand (height value to 3 blocks under)
					for (; y >= heightValue - 3 && y >= 0; y--) {
						Voxel& voxel = volume->voxels[baseIndex + y * CHUNK_SIZE];
						voxel.type = VoxelType::BLOCK;
						voxel.id = sandID;
						volume->voxelCount++;
					}
				}
				// Beach
				else if (heightValue == WATER_HEIGHT) {
					// Sand (height value to 2 blocks under)
					for (; y >= heightValue - 2 && y >= 0; y--) {
						Voxel& voxel = volume->voxels[baseIndex + y * CHUNK_SIZE];
						voxel.type = VoxelType::BLOCK;
						voxel.id = sandID;
						volume->voxelCount++;
					}
				}
				// Land
				else {
					// Grass (first block only)
					Voxel& voxel = volume->voxels[baseIndex + y * CHUNK_SIZE];
					voxel.type = VoxelType::BLOCK;
					voxel.id = grassID;
					volume->voxelCount++;
					y--;

					// Dirt (the 3 blocks under grass)
					for (; y >= heightValue - 3 && y >= 0; y--) {
						Voxel& voxel = volume->voxels[baseIndex + y * CHUNK_SIZE];
						voxel.type = VoxelType::BLOCK;
						voxel.id = dirtID;
						volume->voxelCount++;
					}
				}

				// Stone (underground)
				for (; y >= 0; y--) {
					Voxel& voxel = volume->voxels[baseIndex + y * CHUNK_SIZE];
					voxel.type = VoxelType::BLOCK;
					voxel.id = stoneID;
					volume->voxelCount++;
				}
			}
		}
	}

	static void treePass(const uint32_t seed, const glm::ivec2& offset, VoxelVolumePtr volume) {
		// Get tree models
		const std::vector<VoxelModel>& treeModels = getTreeModels();
		const int treeModelsCount = static_cast<int>(treeModels.size());

		if (treeModelsCount == 0) {
			throw std::runtime_error("No tree models loaded!");
		}

		// Generate potential tree spawn points (including neighbors)
		// Filter out points using density map and heightmap
		std::vector<glm::ivec3> finalPoints;
		const float minDensity = 0.0f;

		for (int neighborOffsetX = -1; neighborOffsetX <= 1; neighborOffsetX++) {
			for (int neighborOffsetZ = -1; neighborOffsetZ <= 1; neighborOffsetZ++) {
				const glm::ivec2 neighborOffset = offset + glm::ivec2(neighborOffsetX, neighborOffsetZ);
				const glm::ivec2 neighborWorldMin = neighborOffset * CHUNK_SIZE;

				std::vector<glm::ivec2> neighborPoints = generateTreeMap(seed, neighborOffset, CHUNK_SIZE, 5);

				for (const glm::ivec2& point : neighborPoints) {
					const glm::ivec2 worldPos = neighborWorldMin + point;

					// Check density
					const float noiseDensityValue = fnFractalDensity->GenSingle2D(worldPos.x, worldPos.y, seed);
					if (noiseDensityValue < minDensity) continue;

					// Check height
					const float noiseHeightValue = fnFractalHeight->GenSingle2D(worldPos.x, worldPos.y, seed);
					const int heightValue = heightFromNoise(noiseHeightValue);
					if (!isGrassAtHeight(heightValue)) continue;

					finalPoints.push_back(glm::ivec3(worldPos.x, heightValue + 1, worldPos.y));
				}
			}
		}

		// Place tree models
		for (const glm::ivec3& origin : finalPoints) {
			// Select tree model
			const uint32_t treeHash = hashCoordinates(origin.x, origin.z, seed);
			const int treeIndex = hashToInt(treeHash, 0, treeModelsCount - 1);
			const VoxelModel& treeModel = treeModels[treeIndex];

			// Skip if tree won't fit vertically
			const int topHeight = origin.y + treeModel.dimensions.y;
			if (topHeight >= MAX_HEIGHT) continue;

			// Half dimensions
			const int halfTreeX = treeModel.dimensions.x / 2;
			const int halfTreeZ = treeModel.dimensions.z / 2;

			// Place tree model (centered)
			for (int x = 0; x < treeModel.dimensions.x; x++) {
				for (int y = 0; y < treeModel.dimensions.y; y++) {
					for (int z = 0; z < treeModel.dimensions.z; z++) {
						const int modelIndex = x + y * treeModel.dimensions.x + z * treeModel.dimensions.x * treeModel.dimensions.y;

						const Voxel& modelVoxel = treeModel.voxels[modelIndex];
						if (modelVoxel.type == VoxelType::EMPTY) continue;

						const glm::ivec3 modelWorld = glm::ivec3(origin.x + x - halfTreeX, origin.y + y, origin.z + z - halfTreeZ);
						if (modelWorld.y < 0 || modelWorld.y >= MAX_HEIGHT) continue;

						const glm::ivec2 modelChunk = glm::ivec2(glm::floor(glm::vec2(modelWorld.x, modelWorld.z) / float(CHUNK_SIZE)));
						if (modelChunk != offset) continue;

						const glm::ivec2 modelLocal = glm::ivec2(modelWorld.x, modelWorld.z) - (offset * CHUNK_SIZE);
						if (modelLocal.x < 0 || modelLocal.x >= CHUNK_SIZE || modelLocal.y < 0 || modelLocal.y >= CHUNK_SIZE) continue;

						const int chunkIndex = modelLocal.x + modelWorld.y * CHUNK_SIZE + modelLocal.y * CHUNK_SIZE * MAX_HEIGHT;
						if (volume->voxels[chunkIndex].type == VoxelType::EMPTY) {
							volume->voxelCount++;
						}

						volume->voxels[chunkIndex] = modelVoxel;
					}
				}
			}
		}
	}

	VoxelVolumePtr generateFlat() {
		VoxelVolumePtr volume = std::make_shared<VoxelVolume>();

		const uint16_t stoneID = VoxelRegistry::GetID("core:stone");
		const uint16_t grassID = VoxelRegistry::GetID("core:grass");

		for (int x = 0; x < CHUNK_SIZE; x++) {
			for (int z = 0; z < CHUNK_SIZE; z++) {
				for (int y = 0; y < 5; y++) {
					int index = x + y * CHUNK_SIZE + z * CHUNK_SIZE * MAX_HEIGHT;

					Voxel& voxel = volume->voxels[index];

					if (y < 3) {
						voxel.type = VoxelType::BLOCK;
						voxel.id = stoneID;
					}
					else {
						voxel.type = VoxelType::BLOCK;
						voxel.id = grassID;
					}
					volume->voxelCount++;
				}
			}
		}

		return volume;
	}

	VoxelVolumePtr generateSimple(const uint32_t seed, const glm::ivec2& offset) {
		VoxelVolumePtr volume = std::make_shared<VoxelVolume>();

		terrainPass(seed, offset, volume);

		return volume;
	}

	VoxelVolumePtr generateAdvanced(const uint32_t seed, const glm::ivec2& offset) {
		VoxelVolumePtr volume = std::make_shared<VoxelVolume>();

		terrainPass(seed, offset, volume);
		treePass(seed, offset, volume);

		return volume;
	}
}
