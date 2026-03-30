#include "modelMesh.h"
#include "shader.h"
#include <glm/gtc/matrix_transform.hpp>

ModelMesh::ModelMesh(std::span<const Vertex> data) {
	// Generate buffers
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &instanceVBO);

	// Bind VAO
	glBindVertexArray(VAO);

	// Setup VBO
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(Vertex), data.data(), GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vertex), (void*)0);
	glVertexAttribPointer(1, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vertex), (void*)3);
	glVertexAttribPointer(2, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(Vertex), (void*)4);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	// Setup instance VBO
	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
	glVertexAttribDivisor(3, 1);

	glEnableVertexAttribArray(3);

	// Unbind
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	// Save counts
	vertices = static_cast<GLsizei>(data.size());
}

ModelMesh::~ModelMesh() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &instanceVBO);
}

void ModelMesh::setPositions(std::span<const glm::vec3> data) {
	// Upload
	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
	glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(glm::vec3), data.data(), GL_STATIC_DRAW);

	// Unbind
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Save counts
	positions = static_cast<GLsizei>(data.size());
}

void ModelMesh::draw(const glm::mat4& view, const glm::mat4& projection, Shader& shader) const {
	// Set uniforms
	shader.setUniform("view", view);
	shader.setUniform("projection", projection);

	// Draw it
	glBindVertexArray(VAO);
	glDrawArraysInstanced(GL_TRIANGLES, 0, vertices, positions);
	glBindVertexArray(0);
}
