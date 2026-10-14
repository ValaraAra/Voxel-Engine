#pragma once

#include "structs.h"
#include <vector>

namespace ModelMesher {
	std::vector<Vertex> build(const VoxelModel& model);
}