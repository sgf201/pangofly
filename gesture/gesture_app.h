#ifndef PANGOFLY_GESTURE_APP_H_
#define PANGOFLY_GESTURE_APP_H_

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>

#define SENSOR_WIDTH 640
#define SENSOR_HEIGHT 480
#define SENSOR_CHANNEL 3

struct GestureResult {
    int64_t timestamp;
    std::string gesture;
    float confidence;
    int x;
    int y;
    int width;
    int height;
};

struct ImageFrame {
    int64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    uint8_t data[SENSOR_WIDTH * SENSOR_HEIGHT * SENSOR_CHANNEL];
};

class GesturePublisher {
public:
    GesturePublisher(const std::string& channel_name);
    ~GesturePublisher();
    
    bool Init(int video_device = -1);
    bool Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    
private:
    void CaptureLoop();
    
    std::string channel_name_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> capture_thread_;
    int video_device_;
};

class GestureSubscriber {
public:
    GestureSubscriber(const std::string& channel_name);
    ~GestureSubscriber();
    
    bool Init(const std::string& model_path);
    bool Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    
    std::vector<GestureResult> GetResults() const;
    
private:
    void ProcessLoop();
    
    std::string channel_name_;
    std::string model_path_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> process_thread_;
    mutable std::mutex results_mutex_;
    std::vector<GestureResult> results_;
};

#endif // PANGOFLY_GESTURE_APP_H_