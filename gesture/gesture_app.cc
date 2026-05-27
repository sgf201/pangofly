#include "gesture_app.h"
#include "../pangofly/pangofly.h"
#include <iostream>
#include <chrono>
#include <cstring>

#ifdef PANGOFLY_PLATFORM_K230
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

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
    // 使用 V4L2 进行摄像头采集
    int fd = -1;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    void* buffer_start[4] = {nullptr};
    
    if (video_device_ >= 0) {
        char dev_name[32];
        snprintf(dev_name, sizeof(dev_name), "/dev/video%d", video_device_);
        fd = open(dev_name, O_RDWR);
        if (fd < 0) {
            std::cerr << "Failed to open video device" << std::endl;
        }
    }
    
    if (fd >= 0) {
        // 设置视频格式
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = SENSOR_WIDTH;
        fmt.fmt.pix.height = SENSOR_HEIGHT;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        
        if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "Failed to set video format" << std::endl;
            close(fd);
            fd = -1;
        }
        
        // 请求缓冲区
        memset(&req, 0, sizeof(req));
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
            std::cerr << "Failed to request buffers" << std::endl;
            close(fd);
            fd = -1;
        }
        
        // 映射缓冲区
        for (int i = 0; i < 4; i++) {
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            
            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "Failed to query buffer" << std::endl;
                break;
            }
            
            buffer_start[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
            if (buffer_start[i] == MAP_FAILED) {
                std::cerr << "Failed to mmap buffer" << std::endl;
                break;
            }
        }
        
        // 启动流
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "Failed to start stream" << std::endl;
            close(fd);
            fd = -1;
        }
    }
    
    int frame_count = 0;
    while (running_.load()) {
        ImageFrame frame;
        frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        frame.width = SENSOR_WIDTH;
        frame.height = SENSOR_HEIGHT;
        frame.format = 0;
        
        if (fd >= 0) {
            // 从摄像头读取帧
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            
            if (ioctl(fd, VIDIOC_DQBUF, &buf) >= 0) {
                memcpy(frame.data, buffer_start[buf.index], SENSOR_WIDTH * SENSOR_HEIGHT * SENSOR_CHANNEL);
                ioctl(fd, VIDIOC_QBUF, &buf);
            }
        } else {
            // 生成模拟数据
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
    
    // 清理
    if (fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        
        for (int i = 0; i < 4; i++) {
            if (buffer_start[i] != nullptr) {
                munmap(buffer_start[i], SENSOR_WIDTH * SENSOR_HEIGHT * SENSOR_CHANNEL * 2);
            }
        }
        close(fd);
    }
    
    std::cout << "Gesture publisher stopped" << std::endl;
#else
    // 非K230平台：生成模拟数据
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
    
    while (running_.load()) {
        ImageFrame frame;
        pangofly::MessageInfo info;
        bool success = reader->Read(&frame, &info);
        
        if (success) {
            std::cout << "Received frame timestamp=" << frame.timestamp 
                      << " size=" << frame.width << "x" << frame.height << std::endl;
            
            // 简单的手势识别模拟
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.clear();
            
            // 根据图像数据计算一个简单的"手势"
            int sum = 0;
            for (int i = 0; i < 100; i++) {
                sum += frame.data[i];
            }
            
            GestureResult result;
            result.timestamp = frame.timestamp;
            result.confidence = 0.75f;
            result.x = 100;
            result.y = 100;
            result.width = 200;
            result.height = 200;
            
            // 根据数据特征模拟不同手势
            if (sum < 10000) {
                result.gesture = "fist";
            } else if (sum < 15000) {
                result.gesture = "palm";
            } else {
                result.gesture = "five";
            }
            
            results_.push_back(result);
            
            std::cout << "Detected gesture: " << result.gesture << " confidence: " << result.confidence << std::endl;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    std::cout << "Gesture subscriber stopped" << std::endl;
}