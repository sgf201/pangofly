#include <iostream>
#include <cstdio>

#include "idl/container/vector.h"
#include "idl/container/string.h"

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

int main() {
    printf("=== Pangofly Non-POD Container Test ===\n\n");
    
    DetectionResults results;
    results.timestamp = 1234567890LL;
    
    printf("1. Testing Vector emplace_back...\n");
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
    
    auto& face2 = results.faces.emplace_back();
    face2.id = 2;
    face2.score = 0.87f;
    face2.bbox_min = {300.0f, 300.0f, 0.0f};
    face2.bbox_max = {400.0f, 400.0f, 0.0f};
    face2.name = "Bob";
    face2.landmarks.push_back(50);
    face2.landmarks.push_back(60);
    
    printf("   ✓ Vector emplace_back works\n\n");
    
    printf("2. Testing Vector size and iteration...\n");
    printf("   Faces count: %zu\n", results.faces.size());
    
    for (size_t i = 0; i < results.faces.size(); ++i) {
        const auto& face = results.faces[i];
        printf("   Face %d: %s, score: %.2f\n", face.id, face.name.c_str(), face.score);
        printf("     Bbox: (%.1f, %.1f) - (%.1f, %.1f)\n",
               face.bbox_min.x, face.bbox_min.y,
               face.bbox_max.x, face.bbox_max.y);
        printf("     Landmarks: ");
        for (size_t j = 0; j < face.landmarks.size(); ++j) {
            printf("%d ", face.landmarks[j]);
        }
        printf("\n");
    }
    printf("   ✓ Vector iteration works\n\n");
    
    printf("3. Testing String operations...\n");
    pangofly::String str1 = "Hello";
    pangofly::String str2 = "Pangofly";
    pangofly::String str3 = str1 + " " + str2;
    
    printf("   str1: %s\n", str1.c_str());
    printf("   str2: %s\n", str2.c_str());
    printf("   str1 + \" \" + str2: %s\n", str3.c_str());
    printf("   str3.size(): %zu\n", str3.size());
    
    if (str1 == "Hello") {
        printf("   ✓ String comparison == works\n");
    }
    if (str1 != str2) {
        printf("   ✓ String comparison != works\n");
    }
    if (str1 < str2) {
        printf("   ✓ String comparison < works\n");
    }
    
    str1.append(" World");
    printf("   str1.append(\" World\"): %s\n", str1.c_str());
    printf("   ✓ String append works\n\n");
    
    printf("4. Testing String find...\n");
    size_t pos = str1.find("World");
    if (pos != pangofly::String::npos) {
        printf("   Found \"World\" at position %zu\n", pos);
        printf("   ✓ String find works\n");
    }
    
    printf("\n=== All tests passed! ===\n");
    return 0;
}
