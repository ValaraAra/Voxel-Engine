#include "worldScene.h"
#include "shader.h"
#include "world.h"
#include "renderer.h"
#include "shaderManager.h"
#include "primitives/cube.h"
#include "primitives/cubeMap.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <tracy/Tracy.hpp>

constexpr float CAMERA_SPEED = 5.0f;
constexpr float INTERACTION_DISTANCE = 10.0f;

// This looks way too messy
WorldScene::WorldScene(SceneManager& sceneManager, ShaderManager& shaderManager, InputManager& inputManager, Window& window)
	: sceneManager(sceneManager), shaderManager(shaderManager), inputManager(inputManager), window(window),
	world(std::make_unique<World>(GenerationType::Advanced, 0u)), cube(std::make_unique<Cube>()), skybox(std::make_unique<CubeMap>()),
	worldTextureAtlas(std::make_unique<TextureAtlas>(1, 1, 1)),
	shaderGeometry(shaderManager.get("src/shaders/geometry.vert.glsl", "src/shaders/geometry.frag.glsl")),
	shaderLit(shaderManager.get("src/shaders/basic.vert.glsl", "src/shaders/lit.frag.glsl")),
	shaderUnlit(shaderManager.get("src/shaders/basic.vert.glsl", "src/shaders/unlit.frag.glsl")),
	shaderForward(shaderManager.get("src/shaders/forward.vert.glsl", "src/shaders/forward.frag.glsl")),
	shaderWater(shaderManager.get("src/shaders/water.vert.glsl", "src/shaders/water.frag.glsl")),
	shaderSkybox(shaderManager.get("src/shaders/skybox.vert.glsl", "src/shaders/skybox.frag.glsl")) {
	tag = "Main";

	// Add textures (must match voxel types)
	for (VoxelData voxelData : VoxelTypeData) {
		worldTextureAtlas->addTexture(voxelData.name, voxelData.color);
	}

	worldTextureAtlas->finish();
}

WorldScene::~WorldScene() {
	// Deregister input callbacks
	if (!inputCallbackHandles.empty()) {
		for (const auto& handle : inputCallbackHandles) {
			inputManager.deregisterInputCallback(handle);
		}
		inputCallbackHandles.clear();
	}
}

void WorldScene::exit() {
	window.setCursorMode(false);
	cameraMovementDisabled = true;

	// Deregister input callbacks
	for (const auto& handle : inputCallbackHandles) {
		inputManager.deregisterInputCallback(handle);
	}
	inputCallbackHandles.clear();
}

void WorldScene::enter() {
	window.setCursorMode(true);
	cameraMovementDisabled = false;

	// Controls
	inputCallbackHandles.push_back(inputManager.registerInputCallback([this](InputAction action, bool pressed) {
		// Exit scene
		if (action == InputAction::Exit && pressed) {
			exitSceneRequested = true;
		}

		// Movement
		if (action == InputAction::MoveForward) {
			movement.forward = pressed;
		}
		if (action == InputAction::MoveBackward) {
			movement.backward = pressed;
		}
		if (action == InputAction::MoveLeft) {
			movement.left = pressed;
		}
		if (action == InputAction::MoveRight) {
			movement.right = pressed;
		}
		if (action == InputAction::MoveUp) {
			movement.up = pressed;
		}
		if (action == InputAction::MoveDown) {
			movement.down = pressed;
		}
		if (action == InputAction::Shift) {
			movement.sprint = pressed;
		}

		// Mouse
		if ((action == InputAction::MouseLeft || action == InputAction::MouseRight) && pressed && !cameraMovementDisabled) {
			const std::optional<VoxelHit> hit = world->raycast(cameraPos, cameraFront, INTERACTION_DISTANCE);

			if (hit) {
				if (action == InputAction::MouseLeft) {
					world->removeVoxel(hit->position);
				}
				else {
					glm::ivec3 placePosition = hit->position + hit->normal;

					// Avoid placing at the camera position
					if (placePosition != glm::ivec3(glm::floor(cameraPos + 0.5f))) {
						world->setVoxel(placePosition, VoxelType::STONE);
					}
				}
			}
		}

		// Toggles
		if (action == InputAction::Escape && pressed) {
			cameraMovementDisabled = !window.toggleCursorMode();
		}
		if (action == InputAction::ToggleFlashlight && pressed) {
			flashlightEnabled = !flashlightEnabled;
		}
		if (action == InputAction::ToggleLighting && pressed) {
			lightingEnabled = !lightingEnabled;
		}
		if (action == InputAction::ToggleDebug && pressed) {
			lightingDebugEnabled = !lightingDebugEnabled;
		}
		})
	);

	// Mouse movement
	inputCallbackHandles.push_back(inputManager.registerMouseCallback([this](double deltaX, double deltaY) {
		// Check if camera movement is disabled
		if (cameraMovementDisabled) {
			return;
		}

		// Apply sensitivity
		const float sensitivity = 0.1f;
		deltaX *= sensitivity;
		deltaY *= sensitivity;

		// Update yaw and pitch
		cameraYaw += static_cast<float>(deltaX);
		cameraPitch += static_cast<float>(deltaY);

		// Constrain pitch
		if (cameraPitch > 89.0f) {
			cameraPitch = 89.0f;
		}
		if (cameraPitch < -89.0f) {
			cameraPitch = -89.0f;
		}

		// Update camera front
		glm::vec3 direction(0.0f);
		direction.x = cos(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
		direction.y = sin(glm::radians(cameraPitch));
		direction.z = sin(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
		cameraFront = glm::normalize(direction);
		})
	);
}

void WorldScene::update(float deltaTime) {
	if (exitSceneRequested) {
		exitSceneRequested = false;
		sceneManager.setScene("Menu");
		return;
	}

	if (!cameraMovementDisabled) {
		updateCamera(deltaTime);
	}

	// Light movement
	const float radius = 16.0f;
	static float angle = 0.0f;
	angle += deltaTime * 0.5f;

	light2Pos.x = 0.0f + radius * cos(angle);
	light2Pos.z = 0.0f + radius * sin(angle);

	// World generation
	static float accumulatedTime = 0.0f;
	accumulatedTime += deltaTime;

	if (accumulatedTime >= 0.1f) {
		// Chunk generation queue update
		auto startTime = std::chrono::high_resolution_clock::now();
		world->updateGenerationQueue(cameraPos, renderDistance);
		auto endTime = std::chrono::high_resolution_clock::now();

		profilingInfo.chunkQueueTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
		if (profilingInfo.chunkQueueTime > profilingInfo.maxChunkQueueTime) {
			profilingInfo.maxChunkQueueTime = profilingInfo.chunkQueueTime;
		}

		accumulatedTime = 0.0f;

		// Chunk meshing queue update
		startTime = std::chrono::high_resolution_clock::now();
		world->updateMeshingQueue(cameraPos, renderDistance);
		endTime = std::chrono::high_resolution_clock::now();

		profilingInfo.meshQueueTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
		if (profilingInfo.meshQueueTime > profilingInfo.maxMeshQueueTime) {
			profilingInfo.maxMeshQueueTime = profilingInfo.meshQueueTime;
		}

		accumulatedTime = 0.0f;
	}
}

void WorldScene::updateCamera(float deltaTime) {
	ZoneScopedN("Update Camera");
	glm::vec3 velocity(0.0f);

	// Calculate velocity
	if (movement.forward) {
		velocity += cameraFront;
	}
	if (movement.backward) {
		velocity -= cameraFront;
	}
	if (movement.left) {
		velocity -= glm::normalize(glm::cross(cameraFront, cameraUp));
	}
	if (movement.right) {
		velocity += glm::normalize(glm::cross(cameraFront, cameraUp));
	}
	if (movement.up) {
		velocity += cameraUp;
	}
	if (movement.down) {
		velocity -= cameraUp;
	}

	// Normalize and apply velocity
	if (glm::length(velocity) > 0.0f) {
		velocity = glm::normalize(velocity);

		float currentSpeedMult = speedMultiplier;
		if (movement.sprint) {
			currentSpeedMult *= 3.0f;
		}

		cameraPos += velocity * CAMERA_SPEED * currentSpeedMult * deltaTime;
	}
}

void WorldScene::render(Renderer& renderer) {
	auto startTime = std::chrono::high_resolution_clock::now();

	// Setup view and projection matrices
	glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
	renderer.setProjectionSettings(60.0f, 0.1f, 5000.0f);
	glm::mat4 projection = renderer.getProjectionMatrix();

	// Set settings
	renderer.setSSAOEnabled(ssaoEnabled);
	renderer.setSSAOBlurEnabled(ssaoBlurEnabled);

	renderer.setSSAOKernelSize(ssaoQuality * 16);
	renderer.setSSAOBlurRadius(ssaoBlurRadius);

	renderer.setSSAORadius(ssaoRadius);
	renderer.setSSAOBias(ssaoBias);

	// Update world
	world->update(cameraPos, renderDistance, view, projection);

	// Geometry pass
	renderer.beginGeometry();
	renderGeometry(renderer, view, projection);

	// Deferred pass
	renderer.beginDeferred();

	if (lightingEnabled) {
		renderLit(renderer, view, projection, worldMaterial);
	}
	else {
		renderUnlit(renderer, view, projection);
	}

	// Opaque forward pass
	renderer.beginForward();
	renderExtras(renderer, view, projection);
	renderSkybox(renderer, view, projection);

	// Translucent forward pass
	renderer.beginTranslucent();
	renderWater(renderer, view, projection);

	auto endTime = std::chrono::high_resolution_clock::now();

	profilingInfo.renderTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
	if (profilingInfo.renderTime > profilingInfo.maxRenderTime) {
		profilingInfo.maxRenderTime = profilingInfo.renderTime;
	}
}

void WorldScene::renderGeometry(Renderer& renderer, const glm::mat4& view, const glm::mat4& projection) {
	// Use texture atlas
	glActiveTexture(GL_TEXTURE0);
	worldTextureAtlas->use();

	renderer.useShader(&shaderGeometry);
	shaderGeometry.setUniform("textureArray", 0);

	auto worldDrawTimeStart = std::chrono::high_resolution_clock::now();
	world->drawOpaque(cameraPos, renderDistance, view, projection, shaderGeometry, wireframeEnabled);
	auto worldDrawTimeEnd = std::chrono::high_resolution_clock::now();

	profilingInfo.worldDrawTime = std::chrono::duration_cast<std::chrono::microseconds>(worldDrawTimeEnd - worldDrawTimeStart);
	if (profilingInfo.worldDrawTime > profilingInfo.maxWorldDrawTime) {
		profilingInfo.maxWorldDrawTime = profilingInfo.worldDrawTime;
	}
}

void WorldScene::renderLit(Renderer& renderer, const glm::mat4& view, const glm::mat4& projection, const Material worldMaterial) {
	DirectLight directLightInfo = {
		glm::vec3(glm::mat3(view) * lightDirection),
		glm::vec3(0.08f, 0.09f, 0.10f),
		glm::vec3(0.2f, 0.14f, 0.07f),
		glm::vec3(0.1f)
	};

	PointLight lightCubeInfo = {
		glm::vec3(view * glm::vec4(lightPos, 1.0)),
		1.0f,
		0.09f,
		0.032f,
		glm::vec3(0.05f),
		glm::vec3(0.8f),
		glm::vec3(1.0f)
	};

	PointLight lightCube2Info = {
		glm::vec3(view * glm::vec4(light2Pos, 1.0)),
		1.0f,
		0.09f,
		0.032f,
		light2Color * glm::vec3(0.05f),
		light2Color * glm::vec3(0.8f),
		light2Color * glm::vec3(1.0f)
	};

	SpotLight spotLightInfo = {
		glm::vec3(view * glm::vec4(cameraPos, 1.0)),
		glm::vec3(glm::mat3(view) * cameraFront),
		glm::cos(glm::radians(12.5f)),
		glm::cos(glm::radians(15.0f)),
		1.0f,
		0.09f,
		0.032f,
		glm::vec3(0.00f),
		glm::vec3(1.0f),
		glm::vec3(1.0f)
	};

	if (!flashlightEnabled) {
		spotLightInfo.ambient = glm::vec3(0.0f);
		spotLightInfo.diffuse = glm::vec3(0.0f);
		spotLightInfo.specular = glm::vec3(0.0f);
	}

	if (!lightingEnabled) {
		directLightInfo.ambient = glm::vec3(0.0f);
		directLightInfo.diffuse = glm::vec3(0.0f);
		directLightInfo.specular = glm::vec3(0.0f);
		lightCubeInfo.ambient = glm::vec3(0.0f);
		lightCubeInfo.diffuse = glm::vec3(0.0f);
		lightCubeInfo.specular = glm::vec3(0.0f);
		lightCube2Info.ambient = glm::vec3(0.0f);
		lightCube2Info.diffuse = glm::vec3(0.0f);
		lightCube2Info.specular = glm::vec3(0.0f);
		spotLightInfo.ambient = glm::vec3(0.0f);
		spotLightInfo.diffuse = glm::vec3(0.0f);
		spotLightInfo.specular = glm::vec3(0.0f);
	}

	renderer.useShader(&shaderLit);
	renderer.bindDeferred(shaderLit);

	shaderLit.setUniforms(directLightInfo);
	shaderLit.setUniforms(lightCubeInfo, 0);
	shaderLit.setUniforms(lightCube2Info, 1);
	shaderLit.setUniforms(spotLightInfo, 0);

	shaderLit.setUniform("material.ambient", worldMaterial.ambient);
	shaderLit.setUniform("material.diffuse", worldMaterial.diffuse);
	shaderLit.setUniform("material.specular", worldMaterial.specular);
	shaderLit.setUniform("material.shininess", worldMaterial.shininess);

	glDisable(GL_DEPTH_TEST);
	renderer.drawQuad();
	glEnable(GL_DEPTH_TEST);
}

void WorldScene::renderUnlit(Renderer& renderer, const glm::mat4& view, const glm::mat4& projection) {
	renderer.useShader(&shaderUnlit);
	renderer.bindDeferred(shaderUnlit);

	glDisable(GL_DEPTH_TEST);
	renderer.drawQuad();
	glEnable(GL_DEPTH_TEST);
}

void WorldScene::renderExtras(Renderer& renderer, const glm::mat4& view, const glm::mat4& projection) {
	renderer.useShader(&shaderForward);

	cube->draw(lightPos, view, projection, shaderForward, lightCubeMaterial);
	cube->draw(light2Pos, view, projection, shaderForward, lightCube2Material);

	// Directional light direction indicator
	if (lightingDebugEnabled) {
		glm::vec3 lightIndicatorPos = cameraPos + glm::normalize(-lightDirection) * 25.0f;
		Material lightIndicatorMaterial = { glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 2.0f };

		cube->draw(lightIndicatorPos, view, projection, shaderForward, lightIndicatorMaterial);
	}
}

void WorldScene::renderSkybox(Renderer& renderer, const glm::mat4& view, const glm::mat4& projection) {
	renderer.useShader(&shaderSkybox);

	// Remove translation from the view matrix
	const glm::mat4& untranslateView = glm::mat4(glm::mat3(view));

	// Set skybox uniforms
	shaderSkybox.setUniform("view", untranslateView);
	shaderSkybox.setUniform("projection", projection);
	shaderSkybox.setUniform("skybox", 0);

	skybox->draw(untranslateView, projection, shaderSkybox);
}

void WorldScene::renderWater(Renderer& renderer, const glm::mat4& view, const glm::mat4& projection) {
	// Use texture atlas
	glActiveTexture(GL_TEXTURE0);
	worldTextureAtlas->use();

	renderer.useShader(&shaderWater);

	world->drawWater(cameraPos, renderDistance, view, projection, shaderWater, wireframeEnabled);
}

void WorldScene::gui() {
	glm::ivec2 windowSize = window.getSize();

	ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Always);
	ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	if (ImGui::Button("Back to Menu")) {
		sceneManager.setScene("Menu");
	}
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(windowSize.x - 400.0f, 50.0f), ImGuiCond_Always);
	ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);

	ImGui::SliderFloat("Speed Multiplier", &speedMultiplier, 0.5f, 10.0f);
	ImGui::SliderInt("Render Distance", &renderDistance, 6, 64);

	ImGui::Text("Total Chunks: %d", world->getChunkCount());
	ImGui::Text("Rendered Chunks: %d", world->getRenderedChunkCount());

	ImGui::Checkbox("Wireframe Mode", &wireframeEnabled);

	if (ImGui::CollapsingHeader("Profiling Data")) {
		ImGui::Text("Chunk Queue Time: %.2f ms (Max: %.2f ms)", profilingInfo.chunkQueueTime.count() / 1000.0f, profilingInfo.maxChunkQueueTime.count() / 1000.0f);
		ImGui::Text("Mesh Queue Time: %.2f ms (Max: %.2f ms)", profilingInfo.meshQueueTime.count() / 1000.0f, profilingInfo.maxMeshQueueTime.count() / 1000.0f);
		ImGui::Text("Chunk Generation Time: %.2f ms (Max: %.2f ms)", profilingInfo.chunkGenTime.count() / 1000.0f, profilingInfo.maxChunkGenTime.count() / 1000.0f);
		ImGui::Text("World Draw Time: %.2f ms (Max: %.2f ms)", profilingInfo.worldDrawTime.count() / 1000.0f, profilingInfo.maxWorldDrawTime.count() / 1000.0f);
		ImGui::Text("Total Render Time: %.2f ms (Max: %.2f ms)", profilingInfo.renderTime.count() / 1000.0f, profilingInfo.maxRenderTime.count() / 1000.0f);
	}

	if (ImGui::CollapsingHeader("SSAO Settings")) {
		ImGui::Checkbox("SSAO", &ssaoEnabled);
		ImGui::Checkbox("SSAO Blur", &ssaoBlurEnabled);
		ImGui::SliderInt("SSAO Quality", &ssaoQuality, 1, 4);

		ImGui::SliderFloat("SSAO Radius", &ssaoRadius, 0.1f, 2.0f);
		ImGui::SliderFloat("SSAO Bias", &ssaoBias, 0.001f, 0.1f, "%.3f");
		ImGui::SliderInt("SSAO Blur Radius", &ssaoBlurRadius, 1, 2);
	}

	if (ImGui::CollapsingHeader("Lighting Settings")) {
		ImGui::Checkbox("Lighting", &lightingEnabled);
		ImGui::Checkbox("Lighting Debug", &lightingDebugEnabled);
		ImGui::Checkbox("Flashlight", &flashlightEnabled);
	}

	ImGui::End();
}
