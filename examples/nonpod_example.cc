#include <cstdio>

#include "../../idl/container/vector.h"
#include "../../idl/container/string.h"

struct Point3D {
    float x;
    float y;
    float z;
};

struct FaceDetectionResult {
    int32_t id;
    float score;
    Point3D bbox_min;
    Point3D bbox_max;
    pangofly::String name;
    pangofly::Vector<int32_t> landmarks;
};

struct DetectionResults {
    int64_t timestamp;
    pangofly::Vector<FaceDetectionResult> faces;
};

void test_vector() {
    printf("=== Testing pangofly::Vector ===\n\n");

    pangofly::Vector<int> vec;
    printf("1. Testing empty vector:\n");
    printf("   size: %zu, empty: %d\n\n", vec.size(), vec.empty());

    printf("2. Testing push_back:\n");
    vec.push_back(10);
    vec.push_back(20);
    vec.push_back(30);
    printf("   After push_back 10, 20, 30:\n");
    printf("   size: %zu\n", vec.size());
    printf("   vec[0]=%d, vec[1]=%d, vec[2]=%d\n\n", vec[0], vec[1], vec[2]);

    printf("3. Testing emplace_back:\n");
    auto& val = vec.emplace_back(40);
    printf("   After emplace_back(40):\n");
    printf("   size: %zu, last value: %d\n\n", vec.size(), val);

    printf("4. Testing iteration:\n");
    printf("   ");
    for (const auto& v : vec) {
        printf("%d ", v);
    }
    printf("\n\n");

    printf("5. Testing resize:\n");
    vec.resize(6);
    printf("   After resize(6): size=%zu\n", vec.size());
    vec.resize(3);
    printf("   After resize(3): size=%zu\n\n", vec.size());

    printf("6. Testing pop_back:\n");
    vec.pop_back();
    printf("   After pop_back: size=%zu\n", vec.size());

    printf("\n=== Vector tests passed! ===\n\n");
}

void test_string() {
    printf("=== Testing pangofly::String ===\n\n");

    printf("1. Testing constructors:\n");
    pangofly::String s1;
    printf("   Empty string: '%s', size: %zu\n", s1.c_str(), s1.size());

    pangofly::String s2 = "Hello";
    printf("   From const char*: '%s', size: %zu\n", s2.c_str(), s2.size());

    pangofly::String s3(5, 'X');
    printf("   Repeated char: '%s', size: %zu\n\n", s3.c_str(), s3.size());

    printf("2. Testing operations:\n");
    s1 = "World";
    printf("   operator=: '%s'\n", s1.c_str());

    pangofly::String s4 = s2 + " " + s1;
    printf("   operator+: '%s'\n", s4.c_str());

    s4.append("!");
    printf("   append: '%s'\n", s4.c_str());

    s4.push_back('!');
    printf("   push_back: '%s'\n\n", s4.c_str());

    printf("3. Testing comparison:\n");
    pangofly::String a = "Apple";
    pangofly::String b = "Banana";
    printf("   a='%s', b='%s'\n", a.c_str(), b.c_str());
    printf("   a == a: %d\n", (a == a) ? 1 : 0);
    printf("   a == b: %d\n", (a == b) ? 1 : 0);
    printf("   a < b: %d\n", (a < b) ? 1 : 0);
    printf("   a > b: %d\n\n", (a > b) ? 1 : 0);

    printf("4. Testing find:\n");
    pangofly::String text = "Hello Pangofly World";
    size_t pos = text.find("Pangofly");
    printf("   text='%s'\n", text.c_str());
    printf("   find('Pangofly'): %zu\n", pos);
    pos = text.find('o');
    printf("   find('o'): %zu\n", pos);

    printf("\n=== String tests passed! ===\n\n");
}

void test_complex_nested() {
    printf("=== Testing Complex Nested Structures ===\n\n");

    DetectionResults results;
    results.timestamp = 1234567890LL;
    printf("1. Creating DetectionResults:\n");
    printf("   timestamp: %lld\n\n", (long long)results.timestamp);

    printf("2. Adding FaceDetectionResult #1:\n");
    auto& face1 = results.faces.emplace_back();
    face1.id = 1;
    face1.score = 0.95f;
    face1.bbox_min = {100.0f, 100.0f, 0.0f};
    face1.bbox_max = {200.0f, 200.0f, 0.0f};
    face1.name = "Alice";
    face1.landmarks.push_back(10);
    face1.landmarks.push_back(20);
    face1.landmarks.push_back(30);
    face1.landmarks.push_back(40);
    printf("   id: %d, name: %s, score: %.2f\n", face1.id, face1.name.c_str(), face1.score);
    printf("   bbox: (%.1f, %.1f) - (%.1f, %.1f)\n",
           face1.bbox_min.x, face1.bbox_min.y,
           face1.bbox_max.x, face1.bbox_max.y);
    printf("   landmarks: ");
    for (const auto& lm : face1.landmarks) {
        printf("%d ", lm);
    }
    printf("\n\n");

    printf("3. Adding FaceDetectionResult #2:\n");
    auto& face2 = results.faces.emplace_back();
    face2.id = 2;
    face2.score = 0.87f;
    face2.bbox_min = {300.0f, 300.0f, 0.0f};
    face2.bbox_max = {400.0f, 400.0f, 0.0f};
    face2.name = "Bob";
    face2.landmarks.push_back(50);
    face2.landmarks.push_back(60);
    printf("   id: %d, name: %s, score: %.2f\n", face2.id, face2.name.c_str(), face2.score);
    printf("   bbox: (%.1f, %.1f) - (%.1f, %.1f)\n",
           face2.bbox_min.x, face2.bbox_min.y,
           face2.bbox_max.x, face2.bbox_max.y);
    printf("   landmarks: ");
    for (const auto& lm : face2.landmarks) {
        printf("%d ", lm);
    }
    printf("\n\n");

    printf("4. Iterating over all faces:\n");
    printf("   Total faces: %zu\n", results.faces.size());
    for (size_t i = 0; i < results.faces.size(); ++i) {
        const auto& face = results.faces[i];
        printf("   Face %d: %s, score: %.2f\n", face.id, face.name.c_str(), face.score);
    }

    printf("\n=== Complex Nested Structure tests passed! ===\n\n");
}

int main() {
    printf("===========================================\n");
    printf("  Pangofly Non-POD Container Example\n");
    printf("===========================================\n\n");

    test_vector();
    test_string();
    test_complex_nested();

    printf("===========================================\n");
    printf("  All Tests Passed Successfully!\n");
    printf("===========================================\n");

    return 0;
}
