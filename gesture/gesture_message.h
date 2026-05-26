#ifndef GESTURE_MESSAGE_H
#define GESTURE_MESSAGE_H

#include "../idl/container/vector.h"

struct ImageMessage {
    int64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    pangofly::Vector<uint8_t> data;
};

#endif
