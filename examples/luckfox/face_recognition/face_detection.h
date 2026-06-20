// Auto-generated from IDL
// DO NOT EDIT MANUALLY
// Module: FaceDetection

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include "idl/container/vector.h"
#include "idl/allocator/block_allocator.h"

namespace FaceDetection {

// ImageFrame - generated from IDL
struct ImageFrame {
    static const char* GetTypeName() { return "FaceDetection::ImageFrame"; }

    int64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t stride;
    pangofly::Vector<uint8_t> data;

    ImageFrame() {
        timestamp = 0;
        width = 0;
        height = 0;
        format = 0;
        stride = 0;
    }

    explicit ImageFrame(pangofly::BlockAllocator* allocator) {
        timestamp = 0;
        width = 0;
        height = 0;
        format = 0;
        stride = 0;
        data.set_block_allocator(allocator);
    }

    size_t CalculateSize() const {
        size_t total = sizeof(*this);
        total += data.size() * sizeof(uint8_t);
        return total;
    }
};

// FaceBox - generated from IDL
struct FaceBox {
    static const char* GetTypeName() { return "FaceDetection::FaceBox"; }

    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float score;
    int32_t id;

    FaceBox() {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        score = 0.0f;
        id = 0;
    }

    explicit FaceBox(pangofly::BlockAllocator* allocator) {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        score = 0.0f;
        id = 0;
    }

    size_t CalculateSize() const {
        size_t total = sizeof(*this);
        return total;
    }
};

// FaceLandmark - generated from IDL
struct FaceLandmark {
    static const char* GetTypeName() { return "FaceDetection::FaceLandmark"; }

    int32_t x;
    int32_t y;

    FaceLandmark() {
        x = 0;
        y = 0;
    }

    explicit FaceLandmark(pangofly::BlockAllocator* allocator) {
        x = 0;
        y = 0;
    }

    size_t CalculateSize() const {
        size_t total = sizeof(*this);
        return total;
    }
};

// FaceResult - generated from IDL
struct FaceResult {
    static const char* GetTypeName() { return "FaceDetection::FaceResult"; }

    int32_t frame_id;
    int64_t timestamp;
    int32_t face_count;
    float processing_time_ms;
    pangofly::Vector<FaceBox> faces;
    pangofly::Vector<FaceLandmark> landmarks;

    FaceResult() {
        frame_id = 0;
        timestamp = 0;
        face_count = 0;
        processing_time_ms = 0.0f;
    }

    explicit FaceResult(pangofly::BlockAllocator* allocator) {
        frame_id = 0;
        timestamp = 0;
        face_count = 0;
        processing_time_ms = 0.0f;
        faces.set_block_allocator(allocator);
        landmarks.set_block_allocator(allocator);
    }

    size_t CalculateSize() const {
        size_t total = sizeof(*this);
        total += faces.size() * sizeof(FaceBox);
        total += landmarks.size() * sizeof(FaceLandmark);
        return total;
    }
};

} // namespace FaceDetection
