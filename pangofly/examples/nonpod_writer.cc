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

class NonPodWriter {
public:
    void run() {
        pangofly::Init(nullptr);
        
        auto node = pangofly::CreateNode("nonpod_writer");
        auto writer = node->CreateWriter<DetectionResults>("nonpod_channel");
        
        DetectionResults results;
        results.timestamp = 1234567890LL;
        
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
        
        writer->Write(results);
        
        printf("Non-POD message sent successfully!\n");
        printf("Timestamp: %lld\n", (long long)results.timestamp);
        printf("Faces count: %zu\n", results.faces.size());
        
        for (size_t i = 0; i < results.faces.size(); ++i) {
            const auto& face = results.faces[i];
            printf("  Face %d: %s, score: %.2f\n", face.id, face.name.c_str(), face.score);
            printf("    Landmarks: ");
            for (size_t j = 0; j < face.landmarks.size(); ++j) {
                printf("%d ", face.landmarks[j]);
            }
            printf("\n");
        }
    }
};

int main() {
    NonPodWriter writer;
    writer.run();
    return 0;
}
