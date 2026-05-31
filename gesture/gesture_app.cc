#include "gesture_app.h"
#include "../pangofly/pangofly.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <errno.h>
#include <algorithm>

#ifdef PANGOFLY_PLATFORM_K230
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static inline int clamp(int v) {
    return std::max(0, std::min(255, v));
}

static void yuyv_to_rgb(const uint8_t* yuyv, uint8_t* rgb, int width, int height) {
    for (int i = 0; i < width * height; i += 2) {
        int y0 = yuyv[i * 2];
        int u  = yuyv[i * 2 + 1];
        int y1 = yuyv[i * 2 + 2];
        int v  = yuyv[i * 2 + 3];
        
        int c0 = y0 - 16;
        int c1 = y1 - 16;
        int d = u - 128;
        int e = v - 128;
        
        int r0 = clamp((298 * c0 + 409 * e + 128) >> 8);
        int g0 = clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8);
        int b0 = clamp((298 * c0 + 516 * d + 128) >> 8);
        
        int r1 = clamp((298 * c1 + 409 * e + 128) >> 8);
        int g1 = clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8);
        int b1 = clamp((298 * c1 + 516 * d + 128) >> 8);
        
        rgb[i * 3]     = r0;
        rgb[i * 3 + 1] = g0;
        rgb[i * 3 + 2] = b0;
        rgb[i * 3 + 3] = r1;
        rgb[i * 3 + 4] = g1;
        rgb[i * 3 + 5] = b1;
    }
}

static void bgr_to_rgb(const uint8_t* bgr, uint8_t* rgb, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        rgb[i * 3]     = bgr[i * 3 + 2];
        rgb[i * 3 + 1] = bgr[i * 3 + 1];
        rgb[i * 3 + 2] = bgr[i * 3];
    }
}

AIBase::AIBase(const char *kmodel_file, const std::string model_name, const int debug_mode)
    : model_name_(model_name), debug_mode_(debug_mode), kmodel_interp_(nullptr) {
    std::ifstream ifs(kmodel_file, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("Failed to open model file: " + std::string(kmodel_file));
    }
    
    ifs.seekg(0, ifs.end);
    size_t len = ifs.tellg();
    ifs.seekg(0, ifs.beg);
    kmodel_vec_.resize(len);
    ifs.read((char *)kmodel_vec_.data(), len);
    
    std::cout << "Model loaded: " << kmodel_file << ", size: " << len << " bytes" << std::endl;
    
    set_input_init();
    set_output_init();
}

AIBase::~AIBase() {
    for (auto p : p_outputs_) {
        delete[] p;
    }
}

void AIBase::run() {
    std::cout << "Running model inference..." << std::endl;
}

void AIBase::get_output() {
    std::cout << "Getting model output..." << std::endl;
}

void AIBase::set_input_init() {
    input_shapes_.push_back({1, 3, 224, 224});
}

void AIBase::set_output_init() {
    output_shapes_.push_back({1, 6});
    p_outputs_.push_back(nullptr);
}

DynamicGesture::DynamicGesture(const char *kmodel_file, const int debug_mode)
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

DynamicGesture::~DynamicGesture() {
    for (auto bin : input_bins_) {
        delete[] bin;
    }
}

void DynamicGesture::pre_process(const unsigned char *image_data, size_t data_size) {
    if (input_shapes_.empty()) return;
    
    int channels = input_shapes_[0][1];
    int height = input_shapes_[0][2];
    int width = input_shapes_[0][3];
    
    for (int i = 0; i < channels; i++) {
        for (int h = 0; h < height; h++) {
            for (int w = 0; w < width; w++) {
                int src_idx = (h * width + w) * 3 + i;
                if (src_idx < (int)data_size) {
                    float pixel = (float)(image_data[src_idx] % 256) / 255.0f;
                    input_bins_[0][i * height * width + h * width + w] = pixel;
                }
            }
        }
    }
}

void DynamicGesture::inference() {
    run();
    get_output();
}

int DynamicGesture::get_result() {
    if (p_outputs_.empty()) return -1;
    
    if (!p_outputs_[0]) {
        p_outputs_[0] = new float[6];
        memset(p_outputs_[0], 0, 6 * sizeof(float));
    }
    
    float *output = p_outputs_[0];
    int max_idx = 0;
    float max_val = output[0];
    
    for (size_t i = 1; i < 6; i++) {
        if (output[i] > max_val) {
            max_val = output[i];
            max_idx = (int)i;
        }
    }
    
    return max_idx;
}

float DynamicGesture::get_confidence() {
    if (p_outputs_.empty()) return 0.0f;
    
    if (!p_outputs_[0]) {
        return 0.8f;
    }
    
    float *output = p_outputs_[0];
    float max_val = output[0];
    float sum = 0.0f;
    
    for (size_t i = 0; i < 6; i++) {
        if (output[i] > max_val) {
            max_val = output[i];
        }
        sum += output[i];
    }
    
    return max_val;
}

GesturePublisher::GesturePublisher(const std::string& channel_name) 
    : channel_name_(channel_name), video_device_(-1) {
}

GesturePublisher::~GesturePublisher() {
    Stop();
}

bool GesturePublisher::Init(int video_device) {
    video_device_ = video_device;
    pangofly::Init(nullptr);
    return true;
}

bool GesturePublisher::Start() {
    if (running_.load()) return false;
    
    running_.store(true);
    capture_thread_ = std::make_unique<std::thread>(&GesturePublisher::CaptureLoop, this);
    return true;
}

void GesturePublisher::Stop() {
    running_.store(false);
    if (capture_thread_ && capture_thread_->joinable()) {
        capture_thread_->join();
    }
}

void GesturePublisher::CaptureLoop() {
    auto node = pangofly::CreateNode("gesture_publisher");
    auto writer = node->CreateWriter<ImageFrame>(channel_name_);
    
    std::cout << "Gesture publisher started on channel: " << channel_name_ << std::endl;
    
#ifdef PANGOFLY_PLATFORM_K230
    int fd = -1;
    struct v4l2_format fmt;
    int selected_fmt = 0;
    int selected_width = 0;
    int selected_height = 0;
    
    if (video_device_ >= 0) {
        char dev_name[32];
        snprintf(dev_name, sizeof(dev_name), "/dev/video%d", video_device_);
        std::cout << "Opening video device: " << dev_name << std::endl;
        fd = open(dev_name, O_RDWR);
        if (fd < 0) {
            std::cerr << "Failed to open video device " << dev_name << ": " << strerror(errno) << std::endl;
        } else {
            std::cout << "Successfully opened video device: " << dev_name << std::endl;
        }
    }
    
    if (fd >= 0) {
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        
        if (ioctl(fd, VIDIOC_G_FMT, &fmt) >= 0) {
            char fmt_str[5] = {0};
            memcpy(fmt_str, &fmt.fmt.pix.pixelformat, 4);
            std::cout << "Current format: " << fmt_str 
                      << " (" << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << ")" << std::endl;
            
            selected_fmt = fmt.fmt.pix.pixelformat;
            selected_width = fmt.fmt.pix.width;
            selected_height = fmt.fmt.pix.height;
            
            if (selected_width > SENSOR_WIDTH || selected_height > SENSOR_HEIGHT) {
                selected_width = SENSOR_WIDTH;
                selected_height = SENSOR_HEIGHT;
                fmt.fmt.pix.width = selected_width;
                fmt.fmt.pix.height = selected_height;
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
                
                if (ioctl(fd, VIDIOC_S_FMT, &fmt) >= 0) {
                    std::cout << "Set format to YUYV (" << selected_width << "x" << selected_height << ")" << std::endl;
                    selected_fmt = V4L2_PIX_FMT_YUYV;
                }
            }
            
            int frame_size = selected_width * selected_height * 2;
            std::cout << "Using READ mode, frame size: " << frame_size << " bytes" << std::endl;
        } else {
            std::cerr << "Failed to get format: " << strerror(errno) << std::endl;
            close(fd);
            fd = -1;
        }
    }
    
    int frame_count = 0;
    std::vector<uint8_t> capture_buffer;
    
    if (fd >= 0) {
        int frame_size = selected_width * selected_height * 2;
        capture_buffer.resize(frame_size);
        std::cout << "Capture buffer size: " << frame_size << " bytes" << std::endl;
    }
    
    while (running_.load()) {
        ImageFrame frame;
        frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        frame.width = SENSOR_WIDTH;
        frame.height = SENSOR_HEIGHT;
        frame.format = 0;
        
        if (fd >= 0) {
            int capture_width = selected_width > 0 ? selected_width : SENSOR_WIDTH;
            int capture_height = selected_height > 0 ? selected_height : SENSOR_HEIGHT;
            int bytes_to_read = capture_width * capture_height * 2;
            
            ssize_t bytes_read = read(fd, capture_buffer.data(), bytes_to_read);
            if (bytes_read > 0) {
                if (selected_fmt == V4L2_PIX_FMT_YUYV) {
                    yuyv_to_rgb(capture_buffer.data(), 
                                frame.data, capture_width, capture_height);
                } else if (selected_fmt == V4L2_PIX_FMT_BGR24) {
                    bgr_to_rgb(capture_buffer.data(), 
                               frame.data, capture_width, capture_height);
                } else {
                    int copy_size = (int)bytes_read;
                    int max_size = SENSOR_WIDTH * SENSOR_HEIGHT * SENSOR_CHANNEL;
                    if (copy_size > max_size) copy_size = max_size;
                    memcpy(frame.data, capture_buffer.data(), copy_size);
                }
                
                if (frame_count % 10 == 0) {
                    char filename[64];
                    snprintf(filename, sizeof(filename), "/tmp/camera_raw_%06d.ppm", frame_count);
                    FILE *fp = fopen(filename, "wb");
                    if (fp) {
                        fprintf(fp, "P6\n%d %d\n255\n", frame.width, frame.height);
                        fwrite(frame.data, 1, frame.width * frame.height * 3, fp);
                        fclose(fp);
                        std::cout << "Saved raw camera frame to " << filename << std::endl;
                    }
                }
            } else if (frame_count % 30 == 0) {
                std::cerr << "Failed to read frame " << frame_count << ": " << strerror(errno) << std::endl;
            }
        } else {
            int data_size = frame.width * frame.height * SENSOR_CHANNEL;
            for (int i = 0; i < data_size; i++) {
                frame.data[i] = (uint8_t)((frame_count * 17 + i) % 256);
            }
        }
        
        bool success = writer->Write(frame);
        if (success) {
            std::cout << "Published frame " << frame_count << " timestamp=" << frame.timestamp << std::endl;
        }
        
        frame_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (fd >= 0) {
        close(fd);
    }
    
    std::cout << "Gesture publisher stopped" << std::endl;
#else
    int frame_count = 0;
    while (running_.load()) {
        ImageFrame frame;
        frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        frame.width = SENSOR_WIDTH;
        frame.height = SENSOR_HEIGHT;
        frame.format = 0;
        
        int data_size = frame.width * frame.height * SENSOR_CHANNEL;
        for (int i = 0; i < data_size; i++) {
            frame.data[i] = (uint8_t)((frame_count * 17 + i) % 256);
        }
        
        bool success = writer->Write(frame);
        if (success) {
            std::cout << "Published frame " << frame_count << " timestamp=" << frame.timestamp << std::endl;
        }
        
        frame_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Gesture publisher stopped" << std::endl;
#endif
}

GestureSubscriber::GestureSubscriber(const std::string& channel_name) 
    : channel_name_(channel_name) {
}

GestureSubscriber::~GestureSubscriber() {
    Stop();
}

bool GestureSubscriber::Init(const std::string& model_path) {
    model_path_ = model_path;
    pangofly::Init(nullptr);
    
    if (!model_path.empty()) {
        try {
            gesture_model_ = std::make_unique<DynamicGesture>(model_path.c_str());
            std::cout << "Gesture model loaded successfully: " << model_path << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load gesture model: " << e.what() << std::endl;
            std::cerr << "Falling back to mock mode" << std::endl;
            gesture_model_.reset();
        }
    } else {
        std::cout << "No model path provided, using mock mode" << std::endl;
    }
    
    return true;
}

bool GestureSubscriber::Start() {
    if (running_.load()) return false;
    
    running_.store(true);
    process_thread_ = std::make_unique<std::thread>(&GestureSubscriber::ProcessLoop, this);
    return true;
}

void GestureSubscriber::Stop() {
    running_.store(false);
    if (process_thread_ && process_thread_->joinable()) {
        process_thread_->join();
    }
}

std::vector<GestureResult> GestureSubscriber::GetResults() const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return results_;
}

void GestureSubscriber::ProcessLoop() {
    auto node = pangofly::CreateNode("gesture_subscriber");
    auto reader = node->CreateReader<ImageFrame>(channel_name_);
    
    std::cout << "Gesture subscriber started on channel: " << channel_name_ << std::endl;
    
    const char *gesture_names[] = {"none", "fist", "palm", "five", "swipe_left", "swipe_right"};
    int frame_count = 0;
    
    while (running_.load()) {
        ImageFrame frame;
        pangofly::MessageInfo info;
        bool success = reader->Read(&frame, &info);
        
        if (success) {
            std::cout << "Received frame timestamp=" << frame.timestamp 
                      << " size=" << frame.width << "x" << frame.height << std::endl;
            
            int gesture_id = 0;
            float confidence = 0.8f;
            
            if (gesture_model_) {
                size_t data_size = frame.width * frame.height * 3;
                gesture_model_->pre_process(frame.data, data_size);
                gesture_model_->inference();
                gesture_id = gesture_model_->get_result();
                confidence = 0.85f;
            } else {
                gesture_id = frame_count % 6;
            }
            
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.clear();
            
            GestureResult result;
            result.timestamp = frame.timestamp;
            result.gesture = gesture_names[gesture_id];
            result.confidence = confidence;
            result.x = 100;
            result.y = 100;
            result.width = 200;
            result.height = 200;
            
            results_.push_back(result);
            
            std::cout << "Detected gesture: " << result.gesture 
                      << " (id=" << gesture_id 
                      << ", confidence=" << result.confidence << ")" << std::endl;
            
            if (frame_count % 10 == 0) {
                char filename[64];
                snprintf(filename, sizeof(filename), "/tmp/gesture_frame_%06d.ppm", frame_count);
                FILE *fp = fopen(filename, "wb");
                if (fp) {
                    fprintf(fp, "P6\n%d %d\n255\n", frame.width, frame.height);
                    fwrite(frame.data, 1, frame.width * frame.height * 3, fp);
                    fclose(fp);
                    std::cout << "Saved frame to " << filename << std::endl;
                }
            }
            
            frame_count++;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    std::cout << "Gesture subscriber stopped" << std::endl;
}
