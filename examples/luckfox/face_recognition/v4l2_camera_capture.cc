/*
 * V4L2 Camera Capture for Luckfox Pico
 * Reference: SDK v4l2_ir.c implementation
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <thread>
#include <signal.h>
#include <vector>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/videodev2.h>

// RKCIF specific ioctl command
#define RKCIF_CMD_SET_CSI_MEMORY_MODE _IOW('V', 0x20, int)
#define CSI_LVDS_MEM_WORD_LOW_ALIGN  0x00000001

static bool g_running = true;

void sigterm_handler(int sig) {
    g_running = false;
}

struct Buffer {
    void* start;
    size_t length;
    int export_fd;
};

class V4L2CameraCapture {
public:
    V4L2CameraCapture() : fd_(-1), buffers_(nullptr), buffer_count_(0) {}
    
    ~V4L2CameraCapture() {
        Cleanup();
    }
    
    bool Open(const std::string& device = "/dev/video11") {
        // Use blocking mode as in SDK
        fd_ = open(device.c_str(), O_RDWR, 0);
        if (fd_ < 0) {
            std::cerr << "Failed to open device: " << device << ", errno: " << errno << std::endl;
            return false;
        }
        return true;
    }
    
    bool Configure(int width = 640, int height = 480) {
        v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;  // NV12 format
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "Failed to set format" << std::endl;
            return false;
        }
        
        // Set CIF memory mode (required for RKCIF)
        int memory_mode = CSI_LVDS_MEM_WORD_LOW_ALIGN;
        if (ioctl(fd_, RKCIF_CMD_SET_CSI_MEMORY_MODE, &memory_mode) < 0) {
            std::cerr << "Warning: Failed to set CIF memory mode" << std::endl;
            // Continue anyway
        }
        
        return true;
    }
    
    bool AllocBuffers(int buffer_count = 4) {
        v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = buffer_count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_DMABUF;
        
        if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
            std::cerr << "Failed to request buffers" << std::endl;
            return false;
        }
        
        buffer_count_ = req.count;
        buffers_ = new Buffer[buffer_count_];
        
        for (int i = 0; i < buffer_count_; i++) {
            v4l2_plane planes[VIDEO_MAX_PLANES];
            v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            memset(planes, 0, sizeof(planes));
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.index = i;
            buf.memory = V4L2_MEMORY_DMABUF;
            buf.m.planes = planes;
            buf.length = 1;  // NV12 single plane
            
            if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "Failed to query buffer " << i << std::endl;
                return false;
            }
            
            // Export DMA fd
            int export_fd = ioctl(fd_, VIDIOC_EXPBUF, &buf);
            if (export_fd < 0) {
                std::cerr << "Failed to export buffer " << i << std::endl;
                return false;
            }
            
            // Map the buffer
            buffers_[i].start = mmap(NULL, buf.m.planes[0].length,
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    fd_, buf.m.planes[0].m.fd);
            buffers_[i].length = buf.m.planes[0].length;
            buffers_[i].export_fd = export_fd;
            
            if (buffers_[i].start == MAP_FAILED) {
                std::cerr << "Failed to map buffer " << i << std::endl;
                return false;
            }
            
            // Queue the buffer
            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "Failed to queue buffer " << i << std::endl;
                return false;
            }
        }
        
        return true;
    }
    
    bool Start() {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "Failed to start streaming" << std::endl;
            return false;
        }
        return true;
    }
    
    bool CaptureFrame(std::vector<uint8_t>& out_data, int& out_width, int& out_height) {
        v4l2_plane planes[VIDEO_MAX_PLANES];
        v4l2_buffer buf;
        
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.m.planes = planes;
        buf.length = 1;
        
        // Dequeue buffer (blocking)
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) {
                return false;
            }
            std::cerr << "Failed to dequeue buffer, errno: " << errno << std::endl;
            return false;
        }
        
        // Get frame info
        v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd_, VIDIOC_G_FMT, &fmt);
        
        out_width = fmt.fmt.pix_mp.width;
        out_height = fmt.fmt.pix_mp.height;
        
        // Copy frame data
        size_t frame_size = buf.m.planes[0].bytesused;
        out_data.resize(frame_size);
        memcpy(out_data.data(), buffers_[buf.index].start, frame_size);
        
        // Re-queue buffer
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "Failed to re-queue buffer" << std::endl;
            return false;
        }
        
        return true;
    }
    
    void Stop() {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd_, VIDIOC_STREAMOFF, &type);
    }
    
    void Cleanup() {
        if (buffers_) {
            for (int i = 0; i < buffer_count_; i++) {
                if (buffers_[i].start && buffers_[i].start != MAP_FAILED) {
                    munmap(buffers_[i].start, buffers_[i].length);
                }
                if (buffers_[i].export_fd >= 0) {
                    close(buffers_[i].export_fd);
                }
            }
            delete[] buffers_;
            buffers_ = nullptr;
        }
        
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_;
    Buffer* buffers_;
    int buffer_count_;
};

bool SavePPM(const std::string& filename, const uint8_t* y_data, const uint8_t* uv_data,
             int width, int height) {
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    
    // Write PPM header
    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    
    // Convert NV12 to RGB and write
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int y_idx = y * width + x;
            int uv_idx = (y / 2) * width + (x / 2) * 2;
            
            uint8_t Y = y_data[y_idx];
            uint8_t U = uv_data[uv_idx];
            uint8_t V = uv_data[uv_idx + 1];
            
            // YUV to RGB conversion
            int R = Y + 1.402 * (V - 128);
            int G = Y - 0.344 * (U - 128) - 0.714 * (V - 128);
            int B = Y + 1.772 * (U - 128);
            
            R = std::max(0, std::min(255, R));
            G = std::max(0, std::min(255, G));
            B = std::max(0, std::min(255, B));
            
            uint8_t rgb[3] = {static_cast<uint8_t>(R), static_cast<uint8_t>(G), static_cast<uint8_t>(B)};
            fwrite(rgb, 1, 3, fp);
        }
    }
    
    fclose(fp);
    return true;
}

int main(int argc, char** argv) {
    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);
    
    std::cout << "V4L2 Camera Capture for Luckfox Pico" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    V4L2CameraCapture capture;
    
    // Open device
    std::string device = "/dev/video11";
    if (argc > 1) {
        device = argv[1];
    }
    
    std::cout << "Opening device: " << device << std::endl;
    if (!capture.Open(device)) {
        return -1;
    }
    
    // Configure format
    int width = 640;
    int height = 480;
    std::cout << "Configuring format: " << width << "x" << height << std::endl;
    if (!capture.Configure(width, height)) {
        return -1;
    }
    
    // Allocate buffers
    std::cout << "Allocating buffers..." << std::endl;
    if (!capture.AllocBuffers(4)) {
        return -1;
    }
    
    // Start streaming
    std::cout << "Starting capture..." << std::endl;
    if (!capture.Start()) {
        return -1;
    }
    
    int frame_count = 0;
    int max_frames = 10;
    
    std::cout << "Capturing " << max_frames << " frames..." << std::endl;
    
    while (g_running && frame_count < max_frames) {
        std::vector<uint8_t> frame_data;
        int w, h;
        
        if (capture.CaptureFrame(frame_data, w, h)) {
            std::ostringstream filename;
            filename << "camera_frame_" << std::setfill('0') << std::setw(6) << frame_count << ".ppm";
            
            // Save as PPM (NV12 format)
            // NV12: Y plane followed by UV interleaved plane
            const uint8_t* y_data = frame_data.data();
            const uint8_t* uv_data = y_data + w * h;
            
            if (SavePPM(filename.str(), y_data, uv_data, w, h)) {
                std::cout << "Saved frame " << frame_count << " to " << filename.str() 
                         << " (" << frame_data.size() << " bytes)" << std::endl;
            }
            
            frame_count++;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    capture.Stop();
    capture.Cleanup();
    
    std::cout << "Capture complete. Captured " << frame_count << " frames." << std::endl;
    
    return 0;
}
