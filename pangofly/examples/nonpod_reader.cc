#include "../pangofly.h"
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

class NonPodReader {
public:
    void run() {
        pangofly::Init(nullptr);
        
        auto node = pangofly::CreateNode("nonpod_reader");
        auto reader = node->CreateReader<DetectionResults>("nonpod_channel");
        
        printf("Waiting for Non-POD message...\n");
        
        DetectionResults results;
        pangofly::MessageInfo info;
        reader->Read(&results, &info);
        
        printf("Non-POD message received!\n");
        printf("Timestamp: %lld\n", (long long)results.timestamp);
        printf("Faces count: %zu\n", results.faces.size());
        
        for (const auto& face : results.faces) {
            printf("  Face %d: %s, score: %.2f\n", face.id, face.name.c_str(), face.score);
            printf("    Bbox: (%.1f, %.1f) - (%.1f, %.1f)\n",
                   face.bbox_min.x, face.bbox_min.y,
                   face.bbox_max.x, face.bbox_max.y);
            printf("    Landmarks: ");
            for (const auto& lm : face.landmarks) {
                printf("%d ", lm);
            }
            printf("\n");
        }
    }
};

int main() {
    NonPodReader reader;
    reader.run();
    return 0;
}
