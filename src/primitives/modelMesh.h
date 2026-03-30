#pragma once

#include "structs.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

class ModelMesh {
public:
	ModelMesh(std::span<const Vertex> data);
	~ModelMesh();

	void setPositions(std::span<const glm::vec3> data);
	void draw(const glm::mat4& view, const glm::mat4& projection, class Shader& shader) const;

private:
	GLuint VAO, VBO;
	GLuint instanceVBO;

	GLsizei vertices = 0;
	GLsizei positions = 0;
};