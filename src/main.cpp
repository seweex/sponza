#include <sponza/context.hpp>
#include <sponza/renderer.hpp>
#include <sponza/resources.hpp>

int main() {
    sponza::Context ctx;
    sponza::Renderer renderer{ ctx };
    sponza::Resources resources{ ctx };
}