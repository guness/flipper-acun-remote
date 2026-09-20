#pragma once
#include "protocols/base.h"
typedef struct {
    const SubGhzProtocol* const* items;
    const size_t size;
} SubGhzProtocolRegistry;
