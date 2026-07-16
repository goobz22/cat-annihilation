// stb_image_impl.cpp — host translation unit for the stb_image
// implementation in the TEST build only.
//
// ModelLoader.cpp includes <stb_image.h> without STB_IMAGE_IMPLEMENTATION
// (declarations only) and expects exactly one other TU in the final binary
// to provide the implementation. In the game binary that TU is
// engine/assets/TextureLoader.cpp; the test build excludes TextureLoader
// because it drags in the Vulkan texture-upload path, so this file plays
// the same role for unit_tests / integration_tests. Keeping the REAL
// decoder (rather than stubbing stbi_* to nullptr) means a test GLB with
// an embedded texture exercises the same decode path the engine ships.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
