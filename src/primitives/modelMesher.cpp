#include "modelMesher.h"
#include <iostream>

namespace {
	struct Offset {
		int8_t x;
		int8_t y;
		int8_t z;
	};

	struct FaceTemplate {
		Offset neighbor;
		Offset corners[4];
	};

	// PX, NX, PY, NY, PZ, NZ
	constexpr FaceTemplate FACE_TEMPLATES[static_cast<size_t>(Direction::COUNT)] = {
		{ { 1, 0, 0 }, { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } } },
		{ { -1, 0, 0 }, { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 } } },
		{ { 0, 1, 0 }, { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } } },
		{ { 0, -1, 0 }, { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } } },
		{ { 0, 0, 1 }, { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } } },
		{ { 0, 0, -1 }, { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } } }
	};

	constexpr int QUAD_CORNERS[6] = { 0, 1, 2, 0, 2, 3 };
}

namespace ModelMesher {
	std::vector<Vertex> build(const VoxelModel& model) {
		std::vector<Vertex> vertices;

		const int dimX = model.dimensions.x;
		const int dimY = model.dimensions.y;
		const int dimZ = model.dimensions.z;

		if (dimX <= 0 || dimY <= 0 || dimZ <= 0) {
			std::cerr << "Model Mesher: Model has an empty dimension." << std::endl;
			return vertices;
		}

		if (dimX > MAX_MODEL_SIZE || dimY > MAX_MODEL_SIZE || dimZ > MAX_MODEL_SIZE) {
			std::cerr << "Model Mesher: Model exceeds MAX_MODEL_SIZE." << std::endl;
			return vertices;
		}

		const size_t expectedVoxels = static_cast<size_t>(dimX) * dimY * dimZ;
		if (model.voxels.size() < expectedVoxels) {
			std::cerr << "Model Mesher: Model voxel count doesn't match its dimensions." << std::endl;
			return vertices;
		}

		const auto voxelIndex = [dimX, dimY](const int x, const int y, const int z) {
			return static_cast<size_t>(x) + static_cast<size_t>(y) * dimX + static_cast<size_t>(z) * dimX * dimY;
		};

		for (int z = 0; z < dimZ; z++) {
			for (int y = 0; y < dimY; y++) {
				for (int x = 0; x < dimX; x++) {
					const Voxel& voxel = model.voxels[voxelIndex(x, y, z)];
					if (voxel.type == VoxelType::EMPTY) continue;

					for (uint8_t direction = 0; direction < static_cast<uint8_t>(Direction::COUNT); direction++) {
						const FaceTemplate& faceTemplate = FACE_TEMPLATES[direction];

						const int neighborX = x + faceTemplate.neighbor.x;
						const int neighborY = y + faceTemplate.neighbor.y;
						const int neighborZ = z + faceTemplate.neighbor.z;

						const bool neighborInside =
							neighborX >= 0 && neighborX < dimX &&
							neighborY >= 0 && neighborY < dimY &&
							neighborZ >= 0 && neighborZ < dimZ;

						if (neighborInside && model.voxels[voxelIndex(neighborX, neighborY, neighborZ)].type != VoxelType::EMPTY) {
							continue;
						}

						for (const int corner : QUAD_CORNERS) {
							const Offset& offset = faceTemplate.corners[corner];

							vertices.push_back(Vertex{
								static_cast<uint8_t>(x + offset.x),
								static_cast<uint8_t>(y + offset.y),
								static_cast<uint8_t>(z + offset.z),
								direction,
								voxel.id
							});
						}
					}
				}
			}
		}

		return vertices;
	}
}
