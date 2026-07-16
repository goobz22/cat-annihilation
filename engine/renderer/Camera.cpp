#include "Camera.hpp"

// Full Input definition required for FPSCameraController::Update — the
// header forward-declares Engine::Input so it remains host-test-build
// safe (no transitive <GLFW/glfw3.h> pull-in). Only this TU needs the
// real header.
#include "../core/Input.hpp"

namespace CatEngine::Renderer {

// FPSCameraController::Update — moved out-of-line from Camera.hpp so the
// header stays decoupled from <GLFW/glfw3.h>. Behaviour is identical to
// the prior inline definition: when the controller has a bound Input
// source, sample WASD + Space + LeftControl into a local move vector
// and the mouse delta into a pitch/yaw delta, then forward both to
// UpdateFPS(). When no input source is bound the call is a no-op and
// the caller is expected to feed UpdateFPS() its own deltas.
void FPSCameraController::Update(float deltaTime) {
    if (!input) {
        return;
    }

    // Keyboard -> local move vector: x = right, y = up, z = forward.
    Engine::vec3 moveInput(0.0f);
    if (input->isKeyDown(Engine::Input::Key::W)) moveInput.z += 1.0f;
    if (input->isKeyDown(Engine::Input::Key::S)) moveInput.z -= 1.0f;
    if (input->isKeyDown(Engine::Input::Key::D)) moveInput.x += 1.0f;
    if (input->isKeyDown(Engine::Input::Key::A)) moveInput.x -= 1.0f;
    if (input->isKeyDown(Engine::Input::Key::Space)) moveInput.y += 1.0f;
    if (input->isKeyDown(Engine::Input::Key::LeftControl)) moveInput.y -= 1.0f;

    // Mouse delta (GLFW-style: +x right, +y down; yaw turns right when
    // mouse moves right). UpdateFPS treats positive yaw/pitch as
    // entity-right / entity-up, so we negate at the call site.
    double dx = 0.0, dy = 0.0;
    input->getMouseDelta(dx, dy);
    Engine::vec2 mouseDelta(static_cast<float>(dx), static_cast<float>(dy));

    UpdateFPS(deltaTime, moveInput, mouseDelta);
}

} // namespace CatEngine::Renderer
