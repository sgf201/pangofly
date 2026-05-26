#include "gesture_app.h"
#include <iostream>
#include <string>

void print_usage(const char* name) {
    std::cout << "Usage: " << name << " <mode> [options]" << std::endl;
    std::cout << "Modes:" << std::endl;
    std::cout << "  publisher   - Start gesture publisher" << std::endl;
    std::cout << "  subscriber  - Start gesture subscriber" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "publisher") {
        std::cout << "Starting gesture publisher..." << std::endl;
        GesturePublisher publisher("gesture_channel");
        if (!publisher.Init(0)) {
            std::cerr << "Failed to init publisher" << std::endl;
            return -1;
        }
        if (!publisher.Start()) {
            std::cerr << "Failed to start publisher" << std::endl;
            return -1;
        }
        
        std::cout << "Publisher started. Press Enter to stop..." << std::endl;
        std::cin.get();
        publisher.Stop();
        std::cout << "Publisher stopped." << std::endl;
        
    } else if (mode == "subscriber") {
        std::cout << "Starting gesture subscriber..." << std::endl;
        GestureSubscriber subscriber("gesture_channel");
        if (!subscriber.Init("")) {
            std::cerr << "Failed to init subscriber" << std::endl;
            return -1;
        }
        if (!subscriber.Start()) {
            std::cerr << "Failed to start subscriber" << std::endl;
            return -1;
        }
        
        std::cout << "Subscriber started. Press Enter to stop..." << std::endl;
        std::cin.get();
        subscriber.Stop();
        std::cout << "Subscriber stopped." << std::endl;
        
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        print_usage(argv[0]);
        return -1;
    }
    
    return 0;
}