#include "utility.hpp"

#include <iostream>
#include <ostream>

namespace sponza::detail {

void validate(VkResult const rCode, const char *msg) { validate(static_cast<vk::Result>(rCode), msg); }
void validate(vk::Result const rCode, const char *msg) {
    if (rCode < vk::Result::eSuccess) {
        std::cerr << "[ERROR]: " << msg << ". Code: " << vk::to_string(rCode) << std::endl;
        std::abort();
    }
}

void validate(bool const test, const char *msg) {
    if (!test) {
        std::cerr << "[ERROR]: " << msg << std::endl;
        std::abort();
    }
}
} // namespace sponza::detail