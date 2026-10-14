#pragma once

#include "structs.h"
#include "primitives/modelMesh.h"
#include <memory>

class ModelRegistry {
public:
	struct ModelEntry {
		VoxelModel model;
		std::unique_ptr<ModelMesh> mesh;
	};

	static uint16_t Register(VoxelModel data, std::unique_ptr<ModelMesh> mesh, const std::string& stringID);
	static bool LoadAndRegister(const std::string& path, const std::string& stringID, uint8_t resolution);

	static uint16_t GetID(const std::string& stringID);

	static const VoxelModel* GetData(const std::string& stringID);
	static const VoxelModel* GetData(const uint16_t id) {
		if (id < modelList.size()) {
			return &modelList[id].model;
		}

		return nullptr;
	};

	static const ModelMesh* GetMesh(const std::string& stringID);
	static const ModelMesh* GetMesh(const uint16_t id) {
		if (id < modelList.size()) {
			return modelList[id].mesh.get();
		}

		return nullptr;
	};
private:
	static std::vector<ModelEntry> modelList;
	static std::unordered_map<std::string, uint16_t> modelMap;
};