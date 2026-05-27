#ifndef PANGOFLY_GESTURE_APP_H_
#define PANGOFLY_GESTURE_APP_H_

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <fstream>
#include <cstring>

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

class AIBase {
public:
    AIBase(const char *kmodel_file, const std::string model_name, const int debug_mode = 1);
    ~AIBase();
    
    void run();
    void get_output();
    
    std::vector<std::vector<int>> input_shapes_;
    std::vector<std::vector<int>> output_shapes_;
    std::vector<float *> p_outputs_;
    
protected:
    std::string model_name_;
    int debug_mode_;
    
private:
    void set_input_init();
    void set_output_init();
    void* kmodel_interp_;
    std::vector<unsigned char> kmodel_vec_;
};

class DynamicGesture : public AIBase {
public:
    DynamicGesture(const char *kmodel_file, const int debug_mode = 1);
    ~DynamicGesture();
    
    void pre_process(const unsigned char *image_data, size_t data_size);
    void inference();
    int get_result();
    float get_confidence();
    
private:
    std::vector<float *> input_bins_;
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
    std::unique_ptr<DynamicGesture> gesture_model_;
    mutable std::mutex results_mutex_;
    std::vector<GestureResult> results_;
};

#endif // PANGOFLY_GESTURE_APP_H_
