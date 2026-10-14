#include "modelRegistry.h"
#include "voxParser.h"
#include "primitives/modelMesher.h"

std::vector<ModelRegistry::ModelEntry> ModelRegistry::modelList;
std::unordered_map<std::string, uint16_t> ModelRegistry::modelMap;

uint16_t ModelRegistry::Register(VoxelModel data, std::unique_ptr<ModelMesh> mesh, const std::string& stringID) {
	auto it = modelMap.find(stringID);
	if (it != modelMap.end()) {
		return it->second;
	}

	const uint16_t id = static_cast<uint16_t>(modelList.size());

	modelMap[stringID] = id;
	modelList.push_back({ std::move(data), std::move(mesh) });
	return id;
};

bool ModelRegistry::LoadAndRegister(const std::string& path, const std::string& stringID, uint8_t resolution)
{
	VoxelModel model;
	if (!VoxParser::loadVoxModel(path, model, resolution)) {
		return false;
	}

	std::vector<Vertex> vertices = ModelMesher::build(model); 
	if (vertices.empty()) {
		return false;
	}

	Register(std::move(model), std::make_unique<ModelMesh>(vertices), stringID);
	return true;
};

uint16_t ModelRegistry::GetID(const std::string& stringID) {
	auto it = modelMap.find(stringID);
	if (it != modelMap.end()) {
		return it->second;
	}

	return 0;
};

const VoxelModel* ModelRegistry::GetData(const std::string& stringID) {
	auto it = modelMap.find(stringID);
	if (it != modelMap.end()) {
		return &modelList[it->second].model;
	}

	return nullptr;
};

const ModelMesh* ModelRegistry::GetMesh(const std::string& stringID) {
	auto it = modelMap.find(stringID);
	if (it != modelMap.end()) {
		return modelList[it->second].mesh.get();
	}

	return nullptr;
};
