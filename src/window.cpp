#include "window.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <stdexcept>

Window::Window(InputManager& inputManager, const glm::ivec2 size, const char* title) : inputManager(inputManager) {
	// Initialize GLFW
	if (!glfwInit()) {
		throw std::runtime_error("Failed to initialize GLFW");
	}

	try {
		// Configure GLFW
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

		#ifdef __APPLE__
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		#endif

		// Create Window
		glfwWindow = glfwCreateWindow(size.x, size.y, title, nullptr, nullptr);

		if (glfwWindow == nullptr) {
			throw std::runtime_error("Failed to create GLFW window");
		}

		glfwMakeContextCurrent(glfwWindow);

		// Load OpenGL functions using GLAD
		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
			throw std::runtime_error("Failed to initialize GLAD");
		}

		// Register GLFW Callbacks
		glfwSetWindowUserPointer(glfwWindow, this);
		glfwSetFramebufferSizeCallback(glfwWindow, framebufferSizeCallbackStatic);
		glfwSetKeyCallback(glfwWindow, keyCallbackStatic);
		glfwSetMouseButtonCallback(glfwWindow, mouseCallbackStatic);
		glfwSetCursorPosCallback(glfwWindow, cursorPosCallbackStatic);
		glfwSetScrollCallback(glfwWindow, scrollCallbackStatic);
	}
	catch (...) {
		glfwTerminate();
		throw;
	}
}

Window::~Window() {
	glfwTerminate();
}

void Window::pollEvents() {
	glfwPollEvents();
}

void Window::swapBuffers() {
	glfwSwapBuffers(glfwWindow);
}

bool Window::shouldClose() const {
	return glfwWindowShouldClose(glfwWindow);
}

// Utility Functions
GLFWwindow* Window::getWindow() const {
	return glfwWindow;
}

glm::ivec2 Window::getSize() const {
	int width, height;
	glfwGetFramebufferSize(glfwWindow, &width, &height);

	return glm::vec2(width, height);
}

float Window::getAspectRatio() const {
	glm::ivec2 size = getSize();

	if (size.y == 0) {
		return 1.0f;
	}

	return static_cast<float>(size.x) / static_cast<float>(size.y);
}

// Fullscreen and Cursor Management
void Window::setFullscreen(bool state) {
	if (state) {
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);

		glfwGetWindowPos(glfwWindow, &windowedPosition.x, &windowedPosition.y);
		glfwGetWindowSize(glfwWindow, &windowedSize.x, &windowedSize.y);

		glfwSetWindowAttrib(glfwWindow, GLFW_DECORATED, GLFW_FALSE);

		glfwSetWindowPos(glfwWindow, 0, 0);
		glfwSetWindowSize(glfwWindow, mode->width, mode->height);
	}
	else {
		glfwSetWindowAttrib(glfwWindow, GLFW_DECORATED, GLFW_TRUE);

		glfwSetWindowSize(glfwWindow, windowedSize.x, windowedSize.y);
		glfwSetWindowPos(glfwWindow, windowedPosition.x, windowedPosition.y);
	}

	fullscreen = state;
}

void Window::setCursorMode(bool disabled) {
	if (disabled) {
		glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		if (glfwRawMouseMotionSupported()) {
			glfwSetInputMode(glfwWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		}
		firstMousePosition = true;
	}
	else {
		glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

bool Window::toggleCursorMode() {
	bool cursorMode = (glfwGetInputMode(glfwWindow, GLFW_CURSOR) != GLFW_CURSOR_DISABLED);
	setCursorMode(cursorMode);

	return cursorMode;
}

// Framebuffer Size Callback
void Window::framebufferSizeCallbackStatic(GLFWwindow* window, int width, int height) {
	Window* instance = static_cast<Window*>(glfwGetWindowUserPointer(window));
	if (instance) {
		instance->framebufferSizeCallback(width, height);
	}
}

void Window::framebufferSizeCallback(int width, int height) {
	glViewport(0, 0, width, height);
}

// Input Callbacks
void Window::keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods) {
	Window* instance = static_cast<Window*>(glfwGetWindowUserPointer(window));
	if (instance) {
		instance->keyCallback(key, scancode, action, mods);
	}
}

void Window::mouseCallbackStatic(GLFWwindow* window, int button, int action, int mods)
{
	Window* instance = static_cast<Window*>(glfwGetWindowUserPointer(window));
	if (instance) {
		instance->mouseCallback(button, action, mods);
	}
}

void Window::keyCallback(int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		inputManager.triggerAction(InputAction::Escape, true);
	}
	if (key == GLFW_KEY_END && action == GLFW_PRESS) {
		inputManager.triggerAction(InputAction::Exit, true);
	}
	if (key == GLFW_KEY_F && action == GLFW_PRESS) {
		inputManager.triggerAction(InputAction::ToggleFlashlight, true);
	}
	if (key == GLFW_KEY_DELETE && action == GLFW_PRESS) {
		inputManager.triggerAction(InputAction::ToggleLighting, true);
	}
	if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
		inputManager.triggerAction(InputAction::ToggleDebug, true);
	}
	if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
		setFullscreen(!fullscreen);
	}

	// Movement
	bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);

	if (key == GLFW_KEY_W) {
		inputManager.triggerAction(InputAction::MoveForward, pressed);
	}
	if (key == GLFW_KEY_S) {
		inputManager.triggerAction(InputAction::MoveBackward, pressed);
	}
	if (key == GLFW_KEY_A) {
		inputManager.triggerAction(InputAction::MoveLeft, pressed);
	}
	if (key == GLFW_KEY_D) {
		inputManager.triggerAction(InputAction::MoveRight, pressed);
	}
	if (key == GLFW_KEY_SPACE) {
		inputManager.triggerAction(InputAction::MoveUp, pressed);
	}
	if (key == GLFW_KEY_LEFT_SHIFT) {
		inputManager.triggerAction(InputAction::Shift, pressed);
	}
	if (key == GLFW_KEY_LEFT_CONTROL) {
		inputManager.triggerAction(InputAction::MoveDown, pressed);
	}
}

void Window::mouseCallback(int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		inputManager.triggerAction(InputAction::MouseLeft, true);
	}
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
		inputManager.triggerAction(InputAction::MouseRight, true);
	}
}

void Window::cursorPosCallbackStatic(GLFWwindow* window, double posX, double posY) {
	Window* instance = static_cast<Window*>(glfwGetWindowUserPointer(window));
	if (instance) {
		instance->cursorPosCallback(posX, posY);
	}
}

void Window::scrollCallbackStatic(GLFWwindow* window, double offsetX, double offsetY) {
	Window* instance = static_cast<Window*>(glfwGetWindowUserPointer(window));
	if (instance) {
		instance->scrollCallback(offsetX, offsetY);
	}
}

void Window::cursorPosCallback(double posX, double posY) {
	if (firstMousePosition) {
		lastMousePosition = glm::dvec2(posX, posY);
		firstMousePosition = false;
		return;
	}

	double deltaX = posX - lastMousePosition.x;
	double deltaY = lastMousePosition.y - posY;

	lastMousePosition = glm::dvec2(posX, posY);

	inputManager.triggerMouse(deltaX, deltaY);
}

void Window::scrollCallback(double offsetX, double offsetY) {
	inputManager.triggerScroll(offsetX, offsetY);
}