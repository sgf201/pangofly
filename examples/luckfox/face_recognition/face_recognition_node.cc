#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>

#include "pangofly/pangofly.h"
#include "pangofly/node/node.h"
#include "idl/generated/face_detection.h"

// RKNN RetinaFace includes
#include "rknn/rknn_api.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"

using namespace pangofly;
using namespace FaceDetection;

// RKNN model context
typedef struct {
    rknn_context rknn_ctx;
    rknn_tensor_mem* input_mems[1];
    rknn_tensor_mem* output_mems[3];
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    int model_width;
    int model_height;
    int model_channel;
    bool is_quant;
} rknn_app_context_t;

static rknn_app_context_t rknn_ctx;

// Face detection result
typedef struct {
    int left;
    int top;
    int right;
    int bottom;
    float score;
} face_box_t;

class FaceRecognitionNode {
public:
    FaceRecognitionNode() : node_(nullptr), reader_(), writer_(), running_(true), frame_id_(0) {}
    
    ~FaceRecognitionNode() {
        Shutdown();
    }
    
    bool Init() {
        // Initialize RKNN model first
        if (!init_retinaface_model("./model/retinaface.rknn")) {
            std::cerr << "Failed to initialize RetinaFace model" << std::endl;
            return false;
        }
        
        if (!pangofly::Init()) {
            std::cerr << "Failed to initialize pangofly" << std::endl;
            return false;
        }
        
        node_ = CreateNode("face_recognition_node");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return false;
        }
        
        reader_ = node_->CreateReader<ImageFrame>("image_channel", 
            std::bind(&FaceRecognitionNode::OnImageReceived, this, std::placeholders::_1));
        if (!reader_) {
            std::cerr << "Failed to create reader" << std::endl;
            return false;
        }
        
        writer_ = node_->CreateWriter<FaceResult>("face_result_channel");
        if (!writer_) {
            std::cerr << "Failed to create writer" << std::endl;
            return false;
        }
        
        std::cout << "FaceRecognitionNode initialized successfully (REAL mode with RetinaFace)" << std::endl;
        return true;
    }
    
    void Run() {
        std::cout << "Starting face recognition (REAL mode with RetinaFace)..." << std::endl;
        std::cout << "Model: retinaface.rknn (" << rknn_ctx.model_width << "x" << rknn_ctx.model_height << ")" << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        
        while (running_) {
            node_->Observe();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "Face recognition stopped" << std::endl;
    }
    
    void Stop() {
        running_ = false;
    }
    
    void Shutdown() {
        Stop();
        reader_.reset();
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
        release_retinaface_model();
    }

private:
    bool init_retinaface_model(const char* model_path) {
        int ret;
        rknn_context ctx = 0;

        ret = rknn_init(&ctx, (char*)model_path, 0, 0, NULL);
        if (ret < 0) {
            std::cerr << "rknn_init fail! ret=" << ret << std::endl;
            return false;
        }

        rknn_input_output_num io_num;
        ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if (ret != RKNN_SUCC) {
            std::cerr << "rknn_query fail! ret=" << ret << std::endl;
            return false;
        }

        rknn_tensor_attr input_attrs[io_num.n_input];
        memset(input_attrs, 0, sizeof(input_attrs));
        for (int i = 0; i < io_num.n_input; i++) {
            input_attrs[i].index = i;
            ret = rknn_query(ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
            if (ret != RKNN_SUCC) {
                std::cerr << "rknn_query input attr fail! ret=" << ret << std::endl;
                return false;
            }
        }

        rknn_tensor_attr output_attrs[io_num.n_output];
        memset(output_attrs, 0, sizeof(output_attrs));
        for (int i = 0; i < io_num.n_output; i++) {
            output_attrs[i].index = i;
            ret = rknn_query(ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
            if (ret != RKNN_SUCC) {
                std::cerr << "rknn_query output attr fail! ret=" << ret << std::endl;
                return false;
            }
        }

        input_attrs[0].type = RKNN_TENSOR_UINT8;
        input_attrs[0].fmt = RKNN_TENSOR_NHWC;

        rknn_ctx.input_mems[0] = rknn_create_mem(ctx, input_attrs[0].size_with_stride);
        ret = rknn_set_io_mem(ctx, rknn_ctx.input_mems[0], &input_attrs[0]);
        if (ret < 0) {
            std::cerr << "rknn_set_io_mem input fail! ret=" << ret << std::endl;
            return false;
        }

        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            rknn_ctx.output_mems[i] = rknn_create_mem(ctx, output_attrs[i].size_with_stride);
            ret = rknn_set_io_mem(ctx, rknn_ctx.output_mems[i], &output_attrs[i]);
            if (ret < 0) {
                std::cerr << "rknn_set_io_mem output fail! ret=" << ret << std::endl;
                return false;
            }
        }

        rknn_ctx.rknn_ctx = ctx;
        rknn_ctx.io_num = io_num;
        rknn_ctx.input_attrs = (rknn_tensor_attr*)malloc(io_num.n_input * sizeof(rknn_tensor_attr));
        memcpy(rknn_ctx.input_attrs, input_attrs, io_num.n_input * sizeof(rknn_tensor_attr));
        rknn_ctx.output_attrs = (rknn_tensor_attr*)malloc(io_num.n_output * sizeof(rknn_tensor_attr));
        memcpy(rknn_ctx.output_attrs, output_attrs, io_num.n_output * sizeof(rknn_tensor_attr));

        if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
            rknn_ctx.model_channel = input_attrs[0].dims[1];
            rknn_ctx.model_height = input_attrs[0].dims[2];
            rknn_ctx.model_width = input_attrs[0].dims[3];
        } else {
            rknn_ctx.model_height = input_attrs[0].dims[1];
            rknn_ctx.model_width = input_attrs[0].dims[2];
            rknn_ctx.model_channel = input_attrs[0].dims[3];
        }

        rknn_ctx.is_quant = (output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC);

        std::cout << "RetinaFace model loaded: " << rknn_ctx.model_width << "x" << rknn_ctx.model_height << "x" << rknn_ctx.model_channel << std::endl;
        return true;
    }

    void release_retinaface_model() {
        if (rknn_ctx.rknn_ctx != 0) {
            rknn_destroy(rknn_ctx.rknn_ctx);
            rknn_ctx.rknn_ctx = 0;
        }
        if (rknn_ctx.input_attrs != NULL) {
            free(rknn_ctx.input_attrs);
            rknn_ctx.input_attrs = NULL;
        }
        if (rknn_ctx.output_attrs != NULL) {
            free(rknn_ctx.output_attrs);
            rknn_ctx.output_attrs = NULL;
        }
        for (int i = 0; i < rknn_ctx.io_num.n_input; i++) {
            if (rknn_ctx.input_mems[i] != NULL) {
                rknn_destroy_mem(rknn_ctx.rknn_ctx, rknn_ctx.input_mems[i]);
                free(rknn_ctx.input_mems[i]);
            }
        }
        for (int i = 0; i < rknn_ctx.io_num.n_output; i++) {
            if (rknn_ctx.output_mems[i] != NULL) {
                rknn_destroy_mem(rknn_ctx.rknn_ctx, rknn_ctx.output_mems[i]);
                free(rknn_ctx.output_mems[i]);
            }
        }
    }

    int clamp(float x, int min, int max) {
        if (x > max) return max;
        if (x < min) return min;
        return (int)x;
    }

    int detect_faces(const cv::Mat& image, face_box_t* faces, int max_faces) {
        if (image.empty()) return 0;

        // Resize to model input size
        cv::Mat resized;
        cv::resize(image, resized, cv::Size(rknn_ctx.model_width, rknn_ctx.model_height));

        // Copy to input buffer (assumes RGB format)
        memcpy(rknn_ctx.input_mems[0]->virt_addr, resized.data, rknn_ctx.model_width * rknn_ctx.model_height * 3);

        // Run inference
        int ret = rknn_run(rknn_ctx.rknn_ctx, nullptr);
        if (ret < 0) {
            std::cerr << "rknn_run fail! ret=" << ret << std::endl;
            return 0;
        }

        // Get outputs
        uint8_t* location = (uint8_t*)(rknn_ctx.output_mems[0]->virt_addr);
        uint8_t* scores = (uint8_t*)(rknn_ctx.output_mems[1]->virt_addr);

        // Dequantization parameters
        int loc_zp = rknn_ctx.output_attrs[0].zp;
        float loc_scale = rknn_ctx.output_attrs[0].scale;
        int scores_zp = rknn_ctx.output_attrs[1].zp;
        float scores_scale = rknn_ctx.output_attrs[1].scale;

        const float VARIANCES[2] = {0.1, 0.2};
        const float (*prior_ptr)[4];
        int num_priors = 16800;
        
        // Simplified: use hardcoded box priors for 640x640
        static float priors[16800][4];
        static bool priors_init = false;
        if (!priors_init) {
            // Generate priors (simplified)
            float steps[3] = {8.0f, 16.0f, 32.0f};
            float min_sizes[3][2] = {{16, 32}, {64, 128}, {256, 512}};
            int idx = 0;
            for (int s = 0; s < 3; s++) {
                int fm_w = (int)(640.0f / steps[s]);
                int fm_h = (int)(640.0f / steps[s]);
                for (int y = 0; y < fm_h; y++) {
                    for (int x = 0; x < fm_w; x++) {
                        for (int k = 0; k < 2; k++) {
                            float cx = (x + 0.5f) * steps[s] / 640.0f;
                            float cy = (y + 0.5f) * steps[s] / 640.0f;
                            float w = min_sizes[s][k] / 640.0f;
                            float h = min_sizes[s][k] / 640.0f;
                            priors[idx][0] = cx - w/2;
                            priors[idx][1] = cy - h/2;
                            priors[idx][2] = w;
                            priors[idx][3] = h;
                            idx++;
                        }
                    }
                }
            }
            priors_init = true;
        }
        prior_ptr = priors;

        float loc_fp32[num_priors * 4];
        int valid_count = 0;

        // Process detections
        for (int i = 0; i < num_priors && valid_count < max_faces; i++) {
            float face_score = ((float)scores[i*2+1] - (float)scores_zp) * scores_scale;
            if (face_score > 0.5) {
                int offset = i * 4;
                float box_x = (((float)location[offset] - (float)loc_zp) * loc_scale) * VARIANCES[0] * prior_ptr[i][2] + prior_ptr[i][0];
                float box_y = (((float)location[offset+1] - (float)loc_zp) * loc_scale) * VARIANCES[0] * prior_ptr[i][3] + prior_ptr[i][1];
                float box_w = expf(((float)location[offset+2] - (float)loc_zp) * loc_scale * VARIANCES[1]) * prior_ptr[i][2];
                float box_h = expf(((float)location[offset+3] - (float)loc_zp) * loc_scale * VARIANCES[1]) * prior_ptr[i][3];

                float xmin = box_x - box_w * 0.5f;
                float ymin = box_y - box_h * 0.5f;
                float xmax = xmin + box_w;
                float ymax = ymin + box_h;

                loc_fp32[offset] = xmin;
                loc_fp32[offset+1] = ymin;
                loc_fp32[offset+2] = xmax;
                loc_fp32[offset+3] = ymax;
                valid_count++;
            }
        }

        // Convert to absolute coordinates
        int face_count = 0;
        for (int i = 0; i < num_priors && face_count < max_faces; i++) {
            float face_score = ((float)scores[i*2+1] - (float)scores_zp) * scores_scale;
            if (face_score > 0.5) {
                int offset = i * 4;
                faces[face_count].left = clamp(loc_fp32[offset] * rknn_ctx.model_width, 0, rknn_ctx.model_width);
                faces[face_count].top = clamp(loc_fp32[offset+1] * rknn_ctx.model_height, 0, rknn_ctx.model_height);
                faces[face_count].right = clamp(loc_fp32[offset+2] * rknn_ctx.model_width, 0, rknn_ctx.model_width);
                faces[face_count].bottom = clamp(loc_fp32[offset+3] * rknn_ctx.model_height, 0, rknn_ctx.model_height);
                faces[face_count].score = face_score;
                face_count++;
            }
        }

        return face_count;
    }

    void OnImageReceived(const ImageFrame& frame) {
        auto start = std::chrono::high_resolution_clock::now();

        std::cout << "\n[FaceRecognition] Received frame: "
                  << frame.width << "x" << frame.height
                  << ", data_size: " << frame.data.size()
                  << ", format: " << frame.format
                  << ", timestamp: " << frame.timestamp << std::endl;

        // Convert ImageFrame to OpenCV Mat (assuming RGB format)
        cv::Mat rgb_image(frame.height, frame.width, CV_8UC3, (void*)frame.data.data());

        // Convert RGB to BGR (RetinaFace model expects BGR format)
        cv::Mat bgr_image;
        cv::cvtColor(rgb_image, bgr_image, cv::COLOR_RGB2BGR);

        // Detect faces using RetinaFace
        face_box_t detected_faces[10];
        int face_count = detect_faces(bgr_image, detected_faces, 10);

        // Prepare result
        Sample<FaceResult> sample = writer_->LoanSample(sizeof(FaceResult) + face_count * sizeof(FaceBox));
        if (!sample.IsValid()) {
            std::cerr << "[FaceRecognition] Failed to loan sample" << std::endl;
            return;
        }

        FaceResult* result_msg = sample.message;
        result_msg->frame_id = frame_id_++;
        result_msg->face_count = face_count;
        result_msg->faces.resize(face_count);

        // Scale face coordinates back to original image size
        float scale_x = (float)frame.width / (float)rknn_ctx.model_width;
        float scale_y = (float)frame.height / (float)rknn_ctx.model_height;

        for (int i = 0; i < face_count; ++i) {
            result_msg->faces[i].id = i + 1;
            result_msg->faces[i].score = detected_faces[i].score;
            result_msg->faces[i].x = clamp(detected_faces[i].left * scale_x, 0, frame.width);
            result_msg->faces[i].y = clamp(detected_faces[i].top * scale_y, 0, frame.height);
            result_msg->faces[i].width = clamp((detected_faces[i].right - detected_faces[i].left) * scale_x, 0, frame.width);
            result_msg->faces[i].height = clamp((detected_faces[i].bottom - detected_faces[i].top) * scale_y, 0, frame.height);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result_msg->processing_time_ms = static_cast<float>(duration.count());
        result_msg->timestamp = frame.timestamp;

        if (writer_->Write(sample)) {
            std::cout << "[FaceRecognition] Detection completed in " << duration.count() << "ms" << std::endl;
            std::cout << "[FaceRecognition] Detected " << result_msg->face_count << " faces" << std::endl;

            for (int i = 0; i < result_msg->face_count; ++i) {
                const FaceBox& face = result_msg->faces[i];
                std::cout << "[FaceRecognition]   Face ID: " << face.id
                          << ", Score: " << face.score
                          << ", Box: (" << face.x << "," << face.y << ")-("
                          << face.x + face.width << "," << face.y + face.height << ")" << std::endl;
            }
        } else {
            std::cerr << "[FaceRecognition] Failed to send face result" << std::endl;
            writer_->Release(sample);
        }
    }
    
private:
    std::unique_ptr<Node> node_;
    std::shared_ptr<Reader<ImageFrame>> reader_;
    std::shared_ptr<Writer<FaceResult>> writer_;
    bool running_;
    int frame_id_;
};

int main(int argc, char** argv) {
    FaceRecognitionNode node;
    
    if (!node.Init()) {
        return -1;
    }
    
    node.Run();
    node.Shutdown();
    
    return 0;
}