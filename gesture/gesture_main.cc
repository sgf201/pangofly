#include "gesture_app.h"
#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    g_running.store(false);
}

void print_usage(const char* name) {
    std::cout << "Usage: " << name << " <mode> [options]" << std::endl;
    std::cout << "Modes:" << std::endl;
    std::cout << "  publisher   - Start gesture publisher" << std::endl;
    std::cout << "  subscriber  - Start gesture subscriber" << std::endl;
    std::cout << std::endl;
    std::cout << "Options for publisher:" << std::endl;
    std::cout << "  -d <device>   Video device number (default: -1, use simulated data)" << std::endl;
    std::cout << std::endl;
    std::cout << "Options for subscriber:" << std::endl;
    std::cout << "  -m <model>    Path to gesture recognition model (optional)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << name << " publisher" << std::endl;
    std::cout << "  " << name << " publisher -d 0" << std::endl;
    std::cout << "  " << name << " subscriber" << std::endl;
    std::cout << "  " << name << " subscriber -m /path/to/model.kmodel" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Pangofly Gesture Application" << std::endl;
    std::cout << "Built at: " << __DATE__ << " " << __TIME__ << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }
    
    std::string mode = argv[1];
    int video_device = -1;
    std::string model_path = "";
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            video_device = std::stoi(argv[i + 1]);
            i++;
        } else if (arg == "-m" && i + 1 < argc) {
            model_path = argv[i + 1];
            i++;
        }
    }
    
    if (mode == "publisher") {
        std::cout << "\nStarting gesture publisher..." << std::endl;
        std::cout << "Video device: " << (video_device >= 0 ? std::to_string(video_device) : "simulated") << std::endl;
        
        GesturePublisher publisher("gesture_channel");
        if (!publisher.Init(video_device)) {
            std::cerr << "Failed to init publisher" << std::endl;
            return -1;
        }
        if (!publisher.Start()) {
            std::cerr << "Failed to start publisher" << std::endl;
            return -1;
        }
        
        std::cout << "\nPublisher started. Press Ctrl+C to stop..." << std::endl;
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        publisher.Stop();
        std::cout << "Publisher stopped." << std::endl;
        
    } else if (mode == "subscriber") {
        std::cout << "\nStarting gesture subscriber..." << std::endl;
        if (!model_path.empty()) {
            std::cout << "Model path: " << model_path << std::endl;
        }
        
        GestureSubscriber subscriber("gesture_channel");
        if (!subscriber.Init(model_path)) {
            std::cerr << "Failed to init subscriber" << std::endl;
            return -1;
        }
        if (!subscriber.Start()) {
            std::cerr << "Failed to start subscriber" << std::endl;
            return -1;
        }
        
        std::cout << "\nSubscriber started. Press Ctrl+C to stop..." << std::endl;
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        subscriber.Stop();
        std::cout << "Subscriber stopped." << std::endl;
        
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        print_usage(argv[0]);
        return -1;
    }
    
    return 0;
}