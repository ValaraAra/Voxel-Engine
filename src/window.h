#pragma once

#include "input.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>

class Window {
public:
	Window(InputManager& inputManager, const glm::ivec2 size = glm::ivec2(1280, 720), const char* title = "Practice Engine");
	~Window();
	void pollEvents();
	void swapBuffers();
	bool shouldClose() const;

	GLFWwindow* getWindow() const;
	glm::ivec2 getSize() const;
	float getAspectRatio() const;

	void setFullscreen(bool state);
	void setCursorMode(bool disabled);
	bool toggleCursorMode();

private:
	InputManager& inputManager;

	GLFWwindow* glfwWindow = nullptr;
	glm::ivec2 windowedSize = glm::ivec2(1280, 720);
	glm::ivec2 windowedPosition = glm::ivec2(0, 0);
	bool fullscreen = false;

	glm::dvec2 lastMousePosition = glm::dvec2(0.0);
	bool firstMousePosition = true;

	static void keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void mouseCallbackStatic(GLFWwindow* window, int button, int action, int mods);
	static void framebufferSizeCallbackStatic(GLFWwindow* window, int width, int height);
	static void cursorPosCallbackStatic(GLFWwindow* window, double xpos, double ypos);
	static void scrollCallbackStatic(GLFWwindow* window, double xoffset, double yoffset);

	void keyCallback(int key, int scancode, int action, int mods);
	void mouseCallback(int button, int action, int mods);
	void framebufferSizeCallback(int width, int height);
	void cursorPosCallback(double xpos, double ypos);
	void scrollCallback(double xoffset, double yoffset);
};