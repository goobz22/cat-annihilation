// Minimal GLFW type stubs for compilation checking without GLFW
// This file provides just enough types to make headers parse without linking
#ifndef GLFW_STUBS_H
#define GLFW_STUBS_H

// GLFW version macros
#define GLFW_VERSION_MAJOR 3
#define GLFW_VERSION_MINOR 3

// GLFW handle types
typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWcursor GLFWcursor;

// GLFW constants
#define GLFW_TRUE 1
#define GLFW_FALSE 0

// Key codes
#define GLFW_KEY_SPACE 32
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_ENTER 257
#define GLFW_KEY_TAB 258
#define GLFW_KEY_BACKSPACE 259
#define GLFW_KEY_LEFT 263
#define GLFW_KEY_RIGHT 262
#define GLFW_KEY_UP 265
#define GLFW_KEY_DOWN 264
#define GLFW_KEY_W 87
#define GLFW_KEY_A 65
#define GLFW_KEY_S 83
#define GLFW_KEY_D 68

// Mouse buttons
#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOUSE_BUTTON_MIDDLE 2

// Action codes
#define GLFW_RELEASE 0
#define GLFW_PRESS 1
#define GLFW_REPEAT 2

// Window hints
#define GLFW_RESIZABLE 0x00020003
#define GLFW_VISIBLE 0x00020004
#define GLFW_DECORATED 0x00020005
#define GLFW_FOCUSED 0x00020001
#define GLFW_CLIENT_API 0x00022001
#define GLFW_NO_API 0

// Error codes
#define GLFW_NOT_INITIALIZED 0x00010001
#define GLFW_NO_CURRENT_CONTEXT 0x00010002
#define GLFW_INVALID_ENUM 0x00010003
#define GLFW_INVALID_VALUE 0x00010004

// Callback types
typedef void (*GLFWerrorfun)(int, const char*);
typedef void (*GLFWwindowsizefun)(GLFWwindow*, int, int);
typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow*, double, double);
typedef void (*GLFWscrollfun)(GLFWwindow*, double, double);

// Stub function declarations
#ifdef __cplusplus
extern "C" {
#endif

int glfwInit(void);
void glfwTerminate(void);
void glfwGetVersion(int* major, int* minor, int* rev);
const char* glfwGetVersionString(void);
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback);

void glfwWindowHint(int hint, int value);
GLFWwindow* glfwCreateWindow(int width, int height, const char* title, GLFWmonitor* monitor, GLFWwindow* share);
void glfwDestroyWindow(GLFWwindow* window);
int glfwWindowShouldClose(GLFWwindow* window);
void glfwSetWindowShouldClose(GLFWwindow* window, int value);

void glfwPollEvents(void);
void glfwWaitEvents(void);
void glfwSwapBuffers(GLFWwindow* window);

GLFWkeyfun glfwSetKeyCallback(GLFWwindow* window, GLFWkeyfun callback);
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* window, GLFWmousebuttonfun callback);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* window, GLFWcursorposfun callback);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow* window, GLFWscrollfun callback);

void glfwGetCursorPos(GLFWwindow* window, double* xpos, double* ypos);
int glfwGetKey(GLFWwindow* window, int key);
int glfwGetMouseButton(GLFWwindow* window, int button);

void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
void glfwSetWindowSize(GLFWwindow* window, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // GLFW_STUBS_H
