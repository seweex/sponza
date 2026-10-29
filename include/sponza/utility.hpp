#pragma once

#define SPONZA_CHECK_RCODE(expr, errType)                                                                              \
    if (auto const rCode = static_cast<vk::Result>(expr); rCode < vk::Result::eSuccess)                                \
        return errType{rCode};