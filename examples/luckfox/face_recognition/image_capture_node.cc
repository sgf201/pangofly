#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "core/pangofly.h"
#include "core/node/node.h"
#include "idl/generated/face_detection.h"

using namespace pangofly;
using namespace FaceDetection;

class ImageCaptureNode {
public:
    ImageCaptureNode() : node_(nullptr), writer_(), save_images_(false), save_interval_(10), save_dir_("/tmp/frames") {}
    
    bool Init() {
        if (!pangofly::Init()) {
            std::cerr << "Failed to initialize pangofly" << std::endl;
            return false;
        }
        
        node_ = CreateNode("image_capture_node");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return false;
        }
        
        writer_ = node_->CreateWriter<ImageFrame>("image_channel");
        if (!writer_) {
            std::cerr << "Failed to create writer" << std::endl;
            return false;
        }
        
        std::cout << "ImageCaptureNode initialized successfully" << std::endl;
        return true;
    }
    
    void SetSaveImages(bool enable, int interval = 10, const std::string& dir = "/tmp/frames") {
        save_images_ = enable;
        save_interval_ = interval;
        save_dir_ = dir;
        
        if (save_images_) {
            std::string cmd = "mkdir -p " + save_dir_;
            system(cmd.c_str());
            std::cout << "Image saving enabled, interval: " << save_interval_ << " frames, directory: " << save_dir_ << std::endl;
        }
    }
    
    void Run(int frame_count = 100) {
        if (!writer_) {
            std::cerr << "Writer not initialized" << std::endl;
            return;
        }

        std::cout << "Starting image capture (using Loan/Write pattern)..." << std::endl;

        for (int i = 0; i < frame_count; ++i) {
            int32_t data_size = 640 * 480 * 3;

            Sample<ImageFrame> sample = writer_->LoanSample(sizeof(ImageFrame) + data_size);
            if (!sample.IsValid()) {
                std::cerr << "Failed to loan sample for frame " << i << std::endl;
                continue;
            }

            ImageFrame* frame = sample.message;
            frame->timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            frame->width = 640;
            frame->height = 480;
            frame->format = 0;
            frame->stride = 640 * 3;

            frame->data.resize(data_size);
            for (size_t j = 0; j < data_size; j += 3) {
                frame->data[j] = static_cast<uint8_t>((i * 10 + j) % 256);
                frame->data[j + 1] = static_cast<uint8_t>((i * 5 + j * 2) % 256);
                frame->data[j + 2] = static_cast<uint8_t>((i * 3 + j * 3) % 256);
            }

            if (save_images_ && (i % save_interval_ == 0)) {
                SaveImageToPPM(frame, i);
            }

            if (writer_->Write(sample)) {
                std::cout << "Frame " << i << " sent: "
                          << frame->width << "x" << frame->height
                          << ", size: " << frame->data.size() << " bytes";
                if (save_images_ && (i % save_interval_ == 0)) {
                    std::cout << ", saved to " << save_dir_;
                }
                std::cout << std::endl;
            } else {
                std::cerr << "Failed to write frame " << i << std::endl;
                writer_->Release(sample);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Image capture completed" << std::endl;
    }
    
    void Shutdown() {
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
    }

private:
    bool SaveImageToPPM(const ImageFrame* frame, int frame_num) {
        if (!frame || frame->data.empty()) {
            std::cerr << "Cannot save empty frame" << std::endl;
            return false;
        }

        std::ostringstream filename;
        filename << save_dir_ << "/frame_" << std::setfill('0') << std::setw(6) << frame_num << ".ppm";

        std::ofstream file(filename.str(), std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open file: " << filename.str() << std::endl;
            return false;
        }

        file << "P6\n";
        file << frame->width << " " << frame->height << "\n";
        file << "255\n";

        if (frame->format == 0) {
            file.write(reinterpret_cast<const char*>(frame->data.data()), frame->data.size());
        } else if (frame->format == 1) {
            for (size_t i = 0; i < frame->data.size(); i += 3) {
                uint8_t b = frame->data[i];
                uint8_t g = frame->data[i + 1];
                uint8_t r = frame->data[i + 2];
                file.write(reinterpret_cast<const char*>(&r), 1);
                file.write(reinterpret_cast<const char*>(&g), 1);
                file.write(reinterpret_cast<const char*>(&b), 1);
            }
        }

        file.close();
        return true;
    }

    std::unique_ptr<Node> node_;
    std::shared_ptr<Writer<ImageFrame>> writer_;
    bool save_images_;
    int save_interval_;
    std::string save_dir_;
};

int main(int argc, char** argv) {
    ImageCaptureNode node;
    
    if (!node.Init()) {
        return -1;
    }
    
    int frame_count = 100;
    bool save_images = false;
    int save_interval = 10;
    std::string save_dir = "/tmp/frames";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--save" || arg == "-s") {
            save_images = true;
        } else if (arg == "--interval" || arg == "-i") {
            if (i + 1 < argc) {
                save_interval = std::stoi(argv[++i]);
            }
        } else if (arg == "--dir" || arg == "-d") {
            if (i + 1 < argc) {
                save_dir = argv[++i];
            }
        } else {
            frame_count = std::stoi(arg);
        }
    }
    
    node.SetSaveImages(save_images, save_interval, save_dir);
    node.Run(frame_count);
    node.Shutdown();
    
    return 0;
}