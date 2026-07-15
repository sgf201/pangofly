#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>

#include "core/pangofly.h"
#include "core/node/node.h"
#include "face_detection.h"

// RKNN RetinaFace includes
#include "rknn/rknn_api.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include "rknn_box_priors.h"

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

    static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale) {
        return ((float)qnt - (float)zp) * scale;
    }

    static int clamp(float x, int min, int max) {
        if (x > max) return max;
        if (x < min) return min;
        return (int)x;
    }

    static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0,
                                  float xmin1, float ymin1, float xmax1, float ymax1) {
        float w = fmaxf(0.f, fminf(xmax0, xmax1) - fmaxf(xmin0, xmin1) + 1);
        float h = fmaxf(0.f, fminf(ymax0, ymax1) - fmaxf(ymin0, ymin1) + 1);
        float i = w * h;
        float u = (xmax0 - xmin0 + 1) * (ymax0 - ymin0 + 1) + (xmax1 - xmin1 + 1) * (ymax1 - ymin1 + 1) - i;
        return u <= 0.f ? 0.f : (i / u);
    }

    static void quick_sort_indice_inverse(float* input, int left, int right, int* indices) {
        float key;
        int key_index;
        int low = left;
        int high = right;
        if (left < right) {
            key_index = indices[left];
            key = input[left];
            while (low < high) {
                while (low < high && input[high] <= key) {
                    high--;
                }
                input[low] = input[high];
                indices[low] = indices[high];
                while (low < high && input[low] >= key) {
                    low++;
                }
                input[high] = input[low];
                indices[high] = indices[low];
            }
            input[low] = key;
            indices[low] = key_index;
            quick_sort_indice_inverse(input, left, low - 1, indices);
            quick_sort_indice_inverse(input, low + 1, right, indices);
        }
    }

    static int nms(int validCount, float* outputLocations, int order[], float threshold, int width, int height) {
        for (int i = 0; i < validCount; ++i) {
            if (order[i] == -1) continue;
            int n = order[i];
            for (int j = i + 1; j < validCount; ++j) {
                int m = order[j];
                if (m == -1) continue;
                float xmin0 = outputLocations[n * 4 + 0] * width;
                float ymin0 = outputLocations[n * 4 + 1] * height;
                float xmax0 = outputLocations[n * 4 + 2] * width;
                float ymax0 = outputLocations[n * 4 + 3] * height;
                float xmin1 = outputLocations[m * 4 + 0] * width;
                float ymin1 = outputLocations[m * 4 + 1] * height;
                float xmax1 = outputLocations[m * 4 + 2] * width;
                float ymax1 = outputLocations[m * 4 + 3] * height;
                float iou = CalculateOverlap(xmin0, ymin0, xmax0, ymax0, xmin1, ymin1, xmax1, ymax1);
                if (iou > threshold) {
                    order[j] = -1;
                }
            }
        }
        return 0;
    }

    int detect_faces(const cv::Mat& image, face_box_t* faces, int max_faces) {
        if (image.empty()) return 0;

        memcpy(rknn_ctx.input_mems[0]->virt_addr, image.data, rknn_ctx.model_width * rknn_ctx.model_height * 3);

        int ret = rknn_run(rknn_ctx.rknn_ctx, nullptr);
        if (ret < 0) {
            std::cerr << "rknn_run fail! ret=" << ret << std::endl;
            return 0;
        }

        uint8_t* location = (uint8_t*)(rknn_ctx.output_mems[0]->virt_addr);
        uint8_t* scores = (uint8_t*)(rknn_ctx.output_mems[1]->virt_addr);

        const float (*prior_ptr)[4] = BOX_PRIORS_640;
        int num_priors = 16800;

        int loc_zp = rknn_ctx.output_attrs[0].zp;
        float loc_scale = rknn_ctx.output_attrs[0].scale;
        int scores_zp = rknn_ctx.output_attrs[1].zp;
        float scores_scale = rknn_ctx.output_attrs[1].scale;

        const float VARIANCES[2] = {0.1, 0.2};

        int filter_indices[16800];
        float props[16800];
        float loc_fp32[16800 * 4];
        memset(loc_fp32, 0, sizeof(loc_fp32));
        memset(filter_indices, 0, sizeof(filter_indices));
        memset(props, 0, sizeof(props));

        int validCount = 0;

        for (int i = 0; i < num_priors; i++) {
            float face_score = deqnt_affine_to_f32((int8_t)scores[i * 2 + 1], scores_zp, scores_scale);
            if (face_score > 0.5f) {
                filter_indices[validCount] = i;
                props[validCount] = face_score;
                int offset = i * 4;
                uint8_t* bbox = location + offset;

                float box_x = deqnt_affine_to_f32((int8_t)bbox[0], loc_zp, loc_scale) * VARIANCES[0] * prior_ptr[i][2] + prior_ptr[i][0];
                float box_y = deqnt_affine_to_f32((int8_t)bbox[1], loc_zp, loc_scale) * VARIANCES[0] * prior_ptr[i][3] + prior_ptr[i][1];
                float box_w = expf(deqnt_affine_to_f32((int8_t)bbox[2], loc_zp, loc_scale) * VARIANCES[1]) * prior_ptr[i][2];
                float box_h = expf(deqnt_affine_to_f32((int8_t)bbox[3], loc_zp, loc_scale) * VARIANCES[1]) * prior_ptr[i][3];

                float xmin = box_x - box_w * 0.5f;
                float ymin = box_y - box_h * 0.5f;
                float xmax = xmin + box_w;
                float ymax = ymin + box_h;

                loc_fp32[offset + 0] = xmin;
                loc_fp32[offset + 1] = ymin;
                loc_fp32[offset + 2] = xmax;
                loc_fp32[offset + 3] = ymax;

                ++validCount;
            }
        }

        if (validCount == 0) return 0;

        quick_sort_indice_inverse(props, 0, validCount - 1, filter_indices);
        nms(validCount, loc_fp32, filter_indices, 0.2f, rknn_ctx.model_width, rknn_ctx.model_height);

        int face_count = 0;
        for (int i = 0; i < validCount && face_count < max_faces; ++i) {
            if (filter_indices[i] == -1 || props[i] < 0.5f) continue;

            int n = filter_indices[i];
            float x1 = loc_fp32[n * 4 + 0] * rknn_ctx.model_width;
            float y1 = loc_fp32[n * 4 + 1] * rknn_ctx.model_height;
            float x2 = loc_fp32[n * 4 + 2] * rknn_ctx.model_width;
            float y2 = loc_fp32[n * 4 + 3] * rknn_ctx.model_height;

            faces[face_count].left = clamp(x1, 0, rknn_ctx.model_width);
            faces[face_count].top = clamp(y1, 0, rknn_ctx.model_height);
            faces[face_count].right = clamp(x2, 0, rknn_ctx.model_width);
            faces[face_count].bottom = clamp(y2, 0, rknn_ctx.model_height);
            faces[face_count].score = props[i];
            face_count++;
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

        // Detect faces using RetinaFace (image is already model size: 640x640)
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

        // Face coordinates are already in image space (image is model size)
        for (int i = 0; i < face_count; ++i) {
            result_msg->faces[i].id = i + 1;
            result_msg->faces[i].score = detected_faces[i].score;
            result_msg->faces[i].x = detected_faces[i].left;
            result_msg->faces[i].y = detected_faces[i].top;
            result_msg->faces[i].width = detected_faces[i].right - detected_faces[i].left;
            result_msg->faces[i].height = detected_faces[i].bottom - detected_faces[i].top;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result_msg->processing_time_ms = static_cast<float>(duration.count());
        result_msg->timestamp = frame.timestamp;

        std::cout << "[FaceRecognition] Detection completed in " << duration.count() << "ms" << std::endl;
        std::cout << "[FaceRecognition] Detected " << face_count << " faces" << std::endl;

        for (int i = 0; i < face_count; ++i) {
            const FaceBox& face = result_msg->faces[i];
            std::cout << "[FaceRecognition]   Face ID: " << face.id
                      << ", Score: " << face.score
                      << ", Box: (" << face.x << "," << face.y << ")-("
                      << face.x + face.width << "," << face.y + face.height << ")" << std::endl;
        }

        if (!writer_->Write(sample)) {
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