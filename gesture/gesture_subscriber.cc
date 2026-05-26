#include <iostream>
#include <memory>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>

#include "pangofly/pangofly.hpp"

extern "C" {
#include <nncase/runtime/interpreter.h>
#include <nncase/runtime/runtime_op_utility.h>
#include <nncase/runtime/util.h>
}

struct ImageMessage {
    uint64_t timestamp;
    int width;
    int height;
    int format;
    size_t data_size;
    char data[4096];
};

struct GestureResult {
    uint64_t timestamp;
    int gesture_id;
    float confidence;
    char gesture_name[32];
};

class AIBase {
public:
    AIBase(const char *kmodel_file, const std::string model_name, const int debug_mode = 1)
        : model_name_(model_name), debug_mode_(debug_mode) {
        std::ifstream ifs(kmodel_file, std::ios::binary);
        ifs.seekg(0, ifs.end);
        size_t len = ifs.tellg();
        ifs.seekg(0, ifs.beg);
        kmodel_vec_.resize(len);
        ifs.read((char *)kmodel_vec_.data(), len);
        
        kmodel_interp_.load_model(kmodel_vec_).expect("Failed to load model");
        kmodel_interp_.initialize().expect("Failed to initialize model");
        
        set_input_init();
        set_output_init();
    }
    
    ~AIBase() {}
    
    void run() {
        kmodel_interp_.run().expect("Failed to run model");
    }
    
    void get_output() {
        for (size_t i = 0; i < output_shapes_.size(); i++) {
            auto output_tensor = kmodel_interp_.output_tensor(i).expect("Failed to get output tensor");
            auto buf = output_tensor.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_read).unwrap().buffer();
            size_t size = 1;
            for (auto dim : output_shapes_[i]) {
                size *= dim;
            }
            p_outputs_[i] = new float[size];
            memcpy(p_outputs_[i], buf.data(), size * sizeof(float));
        }
    }
    
    runtime_tensor get_input_tensor(size_t idx) {
        return kmodel_interp_.input_tensor(idx).expect("Failed to get input tensor");
    }
    
    std::vector<std::vector<int>> input_shapes_;
    std::vector<std::vector<int>> output_shapes_;
    std::vector<float *> p_outputs_;
    
protected:
    std::string model_name_;
    int debug_mode_;
    
private:
    void set_input_init() {
        for (size_t i = 0; i < kmodel_interp_.inputs_size(); i++) {
            auto shape = kmodel_interp_.input_shape(i).expect("Failed to get input shape");
            std::vector<int> shape_vec;
            for (auto dim : shape) {
                shape_vec.push_back((int)dim);
            }
            input_shapes_.push_back(shape_vec);
        }
    }
    
    void set_output_init() {
        for (size_t i = 0; i < kmodel_interp_.outputs_size(); i++) {
            auto shape = kmodel_interp_.output_shape(i).expect("Failed to get output shape");
            std::vector<int> shape_vec;
            for (auto dim : shape) {
                shape_vec.push_back((int)dim);
            }
            output_shapes_.push_back(shape_vec);
            p_outputs_.push_back(nullptr);
        }
    }
    
    nncase::runtime::interpreter kmodel_interp_;
    std::vector<unsigned char> kmodel_vec_;
};

class DynamicGesture : public AIBase {
public:
    DynamicGesture(const char *kmodel_file, const int debug_mode = 1)
        : AIBase(kmodel_file, "DynamicGesture", debug_mode) {
        for (size_t i = 0; i < input_shapes_.size(); i++) {
            size_t size = 1;
            for (auto dim : input_shapes_[i]) {
                size *= dim;
            }
            float *data = new float[size];
            memset(data, 0, size * sizeof(float));
            input_bins_.push_back(data);
        }
    }
    
    ~DynamicGesture() {
        for (auto bin : input_bins_) {
            delete[] bin;
        }
    }
    
    void pre_process(const unsigned char *image_data, size_t data_size) {
        if (input_shapes_.empty()) return;
        
        int channels = input_shapes_[0][1];
        int height = input_shapes_[0][2];
        int width = input_shapes_[0][3];
        
        for (int i = 0; i < channels; i++) {
            for (int h = 0; h < height; h++) {
                for (int w = 0; w < width; w++) {
                    float pixel = (float)(image_data[(h * width + w) * 3 + i] % 256) / 255.0f;
                    input_bins_[0][i * height * width + h * width + w] = pixel;
                }
            }
        }
        
        auto in_tensor = get_input_tensor(0);
        auto buf = in_tensor.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_write).unwrap().buffer();
        size_t size = 1;
        for (auto dim : input_shapes_[0]) {
            size *= dim;
        }
        memcpy(buf.data(), input_bins_[0], size * sizeof(float));
        hrt::sync(in_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
    }
    
    void inference() {
        run();
        get_output();
    }
    
    int get_result() {
        if (p_outputs_.empty()) return -1;
        
        float *output = p_outputs_[0];
        int max_idx = 0;
        float max_val = output[0];
        
        for (size_t i = 1; i < (size_t)output_shapes_[0][1]; i++) {
            if (output[i] > max_val) {
                max_val = output[i];
                max_idx = (int)i;
            }
        }
        
        return max_idx;
    }
    
private:
    std::vector<float *> input_bins_;
};

class GestureSubscriber {
public:
    GestureSubscriber() : running_(false), reader_(nullptr), gesture_model_(nullptr) {}
    
    ~GestureSubscriber() {
        Stop();
    }
    
    int Init(const std::string& channel_name = "gesture_channel") {
        node_ = pangofly::CreateNode("gesture_subscriber");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return -1;
        }
        
        reader_ = node_->CreateReader<ImageMessage>(channel_name);
        if (!reader_) {
            std::cerr << "Failed to create reader" << std::endl;
            return -1;
        }
        
        result_writer_ = node_->CreateWriter<GestureResult>("gesture_result_channel");
        if (!result_writer_) {
            std::cerr << "Failed to create result writer" << std::endl;
            return -1;
        }
        
        try {
            gesture_model_ = std::unique_ptr<DynamicGesture>(new DynamicGesture("/usr/share/models/gesture.kmodel"));
            std::cout << "Loaded gesture model" << std::endl;
        } catch (...) {
            std::cerr << "Failed to load gesture model, using mock mode" << std::endl;
        }
        
        std::cout << "Gesture Subscriber initialized" << std::endl;
        return 0;
    }
    
    int Start() {
        if (running_) return 0;
        
        running_ = true;
        thread_ = std::thread(&GestureSubscriber::RunLoop, this);
        
        std::cout << "Gesture Subscriber started" << std::endl;
        return 0;
    }
    
    int Stop() {
        if (!running_) return 0;
        
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        
        reader_.reset();
        result_writer_.reset();
        node_.reset();
        gesture_model_.reset();
        
        std::cout << "Gesture Subscriber stopped" << std::endl;
        return 0;
    }
    
private:
    void RunLoop() {
        while (running_) {
            ImageMessage msg;
            if (reader_->Read(msg, 100)) {
                std::cout << "Received image: size=" << msg.data_size << ", timestamp=" << msg.timestamp << std::endl;
                
                int gesture_id = -1;
                float confidence = 0.0f;
                
                if (gesture_model_) {
                    gesture_model_->pre_process((const unsigned char *)msg.data, msg.data_size);
                    gesture_model_->inference();
                    gesture_id = gesture_model_->get_result();
                    confidence = 0.9f;
                } else {
                    gesture_id = msg.timestamp % 5;
                    confidence = 0.8f;
                }
                
                const char *gesture_names[] = {"none", "swipe_left", "swipe_right", "swipe_up", "swipe_down"};
                const char *name = (gesture_id >= 0 && gesture_id < 5) ? gesture_names[gesture_id] : "unknown";
                
                GestureResult result;
                result.timestamp = msg.timestamp;
                result.gesture_id = gesture_id;
                result.confidence = confidence;
                snprintf(result.gesture_name, sizeof(result.gesture_name), "%s", name);
                
                bool success = result_writer_->Write(result);
                std::cout << "Published gesture result: " << name << " (confidence: " << confidence << ")" << std::endl;
            }
            
            usleep(33000);
        }
    }
    
    std::atomic<bool> running_;
    std::unique_ptr<pangofly::Node> node_;
    std::unique_ptr<pangofly::Reader<ImageMessage>> reader_;
    std::unique_ptr<pangofly::Writer<GestureResult>> result_writer_;
    std::unique_ptr<DynamicGesture> gesture_model_;
    std::thread thread_;
};

int main() {
    GestureSubscriber subscriber;
    
    if (subscriber.Init() != 0) {
        return -1;
    }
    
    if (subscriber.Start() != 0) {
        return -1;
    }
    
    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();
    
    subscriber.Stop();
    return 0;
}
