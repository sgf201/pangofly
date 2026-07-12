#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <signal.h>
#include <vector>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/videodev2.h>

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include "core/pangofly.h"
#include "core/node/node.h"
#include "face_detection.h"

#include "sample_comm.h"
#include "rga/im2d.h"
#include "rga/im2d_single.h"
#include "rga/im2d_buffer.h"
#include "rga/rga.h"
#include "rga/RgaApi.h"
#include "rga/drmrga.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_mb.h"

using namespace pangofly;
using namespace FaceDetection;

static const int MODEL_WIDTH = 640;
static const int MODEL_HEIGHT = 640;

struct V4L2Buffer {
    void* start;
    size_t length;
    int fd;
};

class ImageCaptureNode {
public:
    ImageCaptureNode()
        : node_(nullptr), writer_(), running_(true), frame_id_(0),
          camera_fd_(-1), buffers_(nullptr), buffer_count_(0),
          rga_inited_(false),
          rgb_buf_(nullptr), rgb_buf_size_(0),
          rgb_mb_(RK_NULL), rgb_pool_(0), rgb_use_dma_(false),
          use_camera_(false), camera_width_(0), camera_height_(0),
          isp_inited_(false) {}

    ~ImageCaptureNode() {
        Shutdown();
    }

    bool Init(bool use_camera = true, const std::string& device = "/dev/video11",
             int camera_width = 720, int camera_height = 480) {
        use_camera_ = use_camera;
        camera_width_ = camera_width;
        camera_height_ = camera_height;

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

        if (use_camera_) {
            if (!InitISP()) {
                std::cerr << "Failed to initialize ISP, falling back to simulated mode" << std::endl;
                use_camera_ = false;
            } else if (!InitCamera(device, camera_width, camera_height)) {
                std::cerr << "Failed to initialize camera, falling back to simulated mode" << std::endl;
                use_camera_ = false;
                CleanupCamera();
            } else if (!InitRGA()) {
                std::cerr << "Failed to initialize RGA, falling back to simulated mode" << std::endl;
                use_camera_ = false;
                CleanupCamera();
            }
        }

        if (use_camera_) {
            std::cout << "ImageCaptureNode initialized (CAMERA mode with RGA acceleration)" << std::endl;
            std::cout << "  Camera: " << camera_width_ << "x" << camera_height_ << " NV12 (V4L2)" << std::endl;
            std::cout << "  Output: " << MODEL_WIDTH << "x" << MODEL_HEIGHT << " RGB888" << std::endl;
        } else {
            std::cout << "ImageCaptureNode initialized (SIMULATED mode)" << std::endl;
        }
        return true;
    }

    void Run(int frame_count = 100) {
        if (!writer_) {
            std::cerr << "Writer not initialized" << std::endl;
            return;
        }

        std::cout << "Starting image capture..." << std::endl;

        if (use_camera_) {
            RunCamera(frame_count);
        } else {
            RunSimulated(frame_count);
        }

        std::cout << "Image capture completed" << std::endl;
    }

    void Stop() {
        running_ = false;
    }

    void Shutdown() {
        Stop();
        CleanupRGA();
        CleanupCamera();
        CleanupISP();
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
    }

private:
    bool InitISP() {
        std::cout << "Initializing ISP..." << std::endl;

        system("/oem/usr/bin/RkLunch-stop.sh > /dev/null 2>&1");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
        RK_BOOL multi_sensor = RK_FALSE;
        const char* iq_dir = "/etc/iqfiles";

        int ret = SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
        if (ret < 0) {
            std::cerr << "SAMPLE_COMM_ISP_Init failed: " << ret << std::endl;
            return false;
        }

        ret = SAMPLE_COMM_ISP_Run(0);
        if (ret < 0) {
            std::cerr << "SAMPLE_COMM_ISP_Run failed: " << ret << std::endl;
            return false;
        }

        isp_inited_ = true;
        std::cout << "RK ISP initialized" << std::endl;
        return true;
    }

    void CleanupISP() {
        if (isp_inited_) {
            SAMPLE_COMM_ISP_Stop(0);
            isp_inited_ = false;
            std::cout << "ISP stopped" << std::endl;
        }
    }

    bool InitCamera(const std::string& device, int width, int height) {
        camera_fd_ = open(device.c_str(), O_RDWR, 0);
        if (camera_fd_ < 0) {
            std::cerr << "Failed to open camera device: " << device << ", errno: " << errno << std::endl;
            return false;
        }

        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

        if (ioctl(camera_fd_, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "Failed to set NV12 format, errno: " << errno << std::endl;
            return false;
        }

        camera_width_ = fmt.fmt.pix_mp.width;
        camera_height_ = fmt.fmt.pix_mp.height;

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(camera_fd_, VIDIOC_REQBUFS, &req) < 0) {
            std::cerr << "Failed to request buffers, errno: " << errno << std::endl;
            return false;
        }

        buffer_count_ = req.count;
        buffers_ = new V4L2Buffer[buffer_count_];

        for (int i = 0; i < buffer_count_; i++) {
            struct v4l2_plane planes[VIDEO_MAX_PLANES];
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            memset(planes, 0, sizeof(planes));

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.index = i;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.m.planes = planes;
            buf.length = 1;

            if (ioctl(camera_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "Failed to query buffer " << i << std::endl;
                return false;
            }

            buffers_[i].start = mmap(NULL, buf.m.planes[0].length,
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    camera_fd_, buf.m.planes[0].m.mem_offset);
            buffers_[i].length = buf.m.planes[0].length;
            buffers_[i].fd = -1;

            if (buffers_[i].start == MAP_FAILED) {
                std::cerr << "Failed to mmap buffer " << i << std::endl;
                return false;
            }

            if (ioctl(camera_fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "Failed to queue buffer " << i << std::endl;
                return false;
            }
        }

        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(camera_fd_, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "Failed to start stream, errno: " << errno << std::endl;
            return false;
        }

        std::cout << "V4L2 camera initialized: " << camera_width_ << "x" << camera_height_ << " NV12" << std::endl;
        return true;
    }

    void CleanupCamera() {
        if (camera_fd_ >= 0) {
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            ioctl(camera_fd_, VIDIOC_STREAMOFF, &type);

            if (buffers_) {
                for (int i = 0; i < buffer_count_; i++) {
                    if (buffers_[i].start != MAP_FAILED && buffers_[i].start) {
                        munmap(buffers_[i].start, buffers_[i].length);
                    }
                }
                delete[] buffers_;
                buffers_ = nullptr;
            }

            close(camera_fd_);
            camera_fd_ = -1;
            std::cout << "V4L2 camera closed" << std::endl;
        }
    }

    bool InitRGA() {
        rgb_buf_size_ = MODEL_WIDTH * MODEL_HEIGHT * 3;

        MB_POOL_CONFIG_S pool_cfg;
        memset(&pool_cfg, 0, sizeof(pool_cfg));
        pool_cfg.u64MBSize = rgb_buf_size_;
        pool_cfg.u32MBCnt = 2;
        pool_cfg.enAllocType = MB_ALLOC_TYPE_DMA;
        pool_cfg.bPreAlloc = RK_TRUE;

        rgb_pool_ = RK_MPI_MB_CreatePool(&pool_cfg);
        if (rgb_pool_ == 0) {
            std::cerr << "RK_MPI_MB_CreatePool failed, fallback to malloc" << std::endl;
            rgb_buf_ = (uint8_t*)malloc(rgb_buf_size_);
            if (!rgb_buf_) return false;
            memset(rgb_buf_, 0, rgb_buf_size_);
            rgb_use_dma_ = false;
        } else {
            rgb_mb_ = RK_MPI_MB_GetMB(rgb_pool_, rgb_buf_size_, RK_TRUE);
            if (rgb_mb_ == RK_NULL) {
                std::cerr << "RK_MPI_MB_GetMB failed, fallback to malloc" << std::endl;
                rgb_buf_ = (uint8_t*)malloc(rgb_buf_size_);
                if (!rgb_buf_) return false;
                memset(rgb_buf_, 0, rgb_buf_size_);
                rgb_use_dma_ = false;
            } else {
                rgb_buf_ = (uint8_t*)RK_MPI_MB_Handle2VirAddr(rgb_mb_);
                rgb_use_dma_ = true;
                memset(rgb_buf_, 0, rgb_buf_size_);
                std::cout << "RGB DMA buffer allocated, fd=" << RK_MPI_MB_Handle2Fd(rgb_mb_) << std::endl;
            }
        }

        rga_inited_ = true;
        std::cout << "RGA initialized" << std::endl;
        return true;
    }

    void CleanupRGA() {
        if (rgb_mb_ && rgb_use_dma_) {
            RK_MPI_MB_ReleaseMB(rgb_mb_);
            rgb_mb_ = RK_NULL;
        }
        if (rgb_pool_ && rgb_use_dma_) {
            RK_MPI_MB_DestroyPool(rgb_pool_);
            rgb_pool_ = 0;
        }
        if (rgb_buf_ && !rgb_use_dma_) {
            free(rgb_buf_);
        }
        rgb_buf_ = nullptr;
        rga_inited_ = false;
    }

    static inline uint8_t clamp(int v) {
        return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
    }

    static void SavePNG(const std::string& filename, const uint8_t* rgb, int width, int height) {
        cv::Mat img(height, width, CV_8UC3, const_cast<uint8_t*>(rgb));
        cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
        cv::imwrite(filename, img);
    }

    static void SaveNV12(const std::string& filename, const uint8_t* y, const uint8_t* uv, int width, int height) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) return;
        file.write(reinterpret_cast<const char*>(y), width * height);
        file.write(reinterpret_cast<const char*>(uv), width * height / 2);
    }

    void SoftwareNV12ToRGB(const uint8_t* y_plane, const uint8_t* uv_plane,
                           int src_w, int src_h, int src_stride,
                           uint8_t* dst_rgb, int dst_w, int dst_h) {
        float x_ratio = (float)src_w / dst_w;
        float y_ratio = (float)src_h / dst_h;

        for (int y = 0; y < dst_h; y++) {
            for (int x = 0; x < dst_w; x++) {
                int src_x = (int)(x * x_ratio);
                int src_y = (int)(y * y_ratio);
                if (src_x >= src_w) src_x = src_w - 1;
                if (src_y >= src_h) src_y = src_h - 1;

                uint8_t y_val = y_plane[src_y * src_stride + src_x];
                int uv_x = src_x / 2;
                int uv_y = src_y / 2;
                uint8_t u_val = uv_plane[uv_y * (src_stride / 2) * 2 + uv_x * 2];
                uint8_t v_val = uv_plane[uv_y * (src_stride / 2) * 2 + uv_x * 2 + 1];

                int r = (int)(1.164f * (y_val - 16) + 1.596f * (v_val - 128));
                int g = (int)(1.164f * (y_val - 16) - 0.813f * (v_val - 128) - 0.391f * (u_val - 128));
                int b = (int)(1.164f * (y_val - 16) + 2.018f * (u_val - 128));

                int dst_idx = (y * dst_w + x) * 3;
                dst_rgb[dst_idx] = clamp(r);
                dst_rgb[dst_idx + 1] = clamp(g);
                dst_rgb[dst_idx + 2] = clamp(b);
            }
        }
    }

    bool ConvertNV12ToRGBWithResize(uint8_t* src_nv12, int src_w, int src_h,
                                    MB_BLK dst_mb, uint8_t* dst_rgb,
                                    int dst_w, int dst_h,
                                    bool use_dma_dst, bool& used_rga) {
        used_rga = false;
        if (!rga_inited_) return false;

        const uint8_t* y_plane = src_nv12;
        const uint8_t* uv_plane = src_nv12 + src_w * src_h;

        rga_buffer_handle_t src_handle = 0;
        rga_buffer_handle_t dst_handle = 0;
        bool src_imported = false;
        bool dst_imported = false;

        src_handle = importbuffer_virtualaddr((void*)y_plane, src_w * src_h * 3 / 2);
        if (src_handle != 0) {
            src_imported = true;
        }

        if (use_dma_dst && dst_mb) {
            int dst_fd = RK_MPI_MB_Handle2Fd(dst_mb);
            if (dst_fd >= 0) {
                RK_U64 dst_size = RK_MPI_MB_GetSize(dst_mb);
                dst_handle = importbuffer_fd(dst_fd, dst_size);
                if (dst_handle != 0) {
                    dst_imported = true;
                }
            }
        }

        if (!dst_imported) {
            dst_handle = importbuffer_virtualaddr(dst_rgb, dst_w * dst_h * 3);
            if (dst_handle != 0) {
                dst_imported = true;
            }
        }

        if (src_imported && dst_imported) {
            int ret = 0;

            rga_info_t src_info;
            rga_info_t dst_info;
            memset(&src_info, 0, sizeof(src_info));
            memset(&dst_info, 0, sizeof(dst_info));

            src_info.hnd = src_handle;
            src_info.virAddr = (void*)y_plane;
            rga_set_rect(&src_info.rect, 0, 0, src_w, src_h, src_w, src_h, RK_FORMAT_YCbCr_420_SP);

            dst_info.hnd = dst_handle;
            dst_info.virAddr = (void*)dst_rgb;
            rga_set_rect(&dst_info.rect, 0, 0, dst_w, dst_h, dst_w, dst_h, RK_FORMAT_RGB_888);

            ret = c_RkRgaBlit(&src_info, &dst_info, NULL);

            if (ret == 0) {
                used_rga = true;
                releasebuffer_handle(src_handle);
                releasebuffer_handle(dst_handle);
                return true;
            }
        }

        if (src_handle) releasebuffer_handle(src_handle);
        if (dst_handle) releasebuffer_handle(dst_handle);

        SoftwareNV12ToRGB(y_plane, uv_plane, src_w, src_h, src_w,
                          dst_rgb, dst_w, dst_h);
        return true;
    }

    void RunCamera(int frame_count) {
        std::cout << "Camera capture mode, " << frame_count << " frames" << std::endl;

        int success_count = 0;
        int fail_count = 0;

        for (int i = 0; i < frame_count && running_; i++) {
            struct v4l2_plane planes[VIDEO_MAX_PLANES];
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            memset(planes, 0, sizeof(planes));

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.m.planes = planes;
            buf.length = 1;

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(camera_fd_, &fds);

            struct timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            int ret = select(camera_fd_ + 1, &fds, NULL, NULL, &tv);
            if (ret <= 0) {
                std::cerr << "select timeout or error for frame " << i << std::endl;
                fail_count++;
                continue;
            }

            if (ioctl(camera_fd_, VIDIOC_DQBUF, &buf) < 0) {
                std::cerr << "Failed to dequeue buffer for frame " << i << ", errno: " << errno << std::endl;
                fail_count++;
                continue;
            }

            uint8_t* nv12_data = (uint8_t*)buffers_[buf.index].start;
            int src_w = camera_width_;
            int src_h = camera_height_;

            bool used_rga = false;
            bool ok = ConvertNV12ToRGBWithResize(nv12_data, src_w, src_h,
                                                rgb_mb_, rgb_buf_,
                                                MODEL_WIDTH, MODEL_HEIGHT,
                                                rgb_use_dma_, used_rga);

            if (success_count == 1) {
                const uint8_t* y_plane = nv12_data;
                const uint8_t* uv_plane = nv12_data + src_w * src_h;
                SaveNV12("raw_nv12_frame.yuv", y_plane, uv_plane, src_w, src_h);
                std::cout << "Saved raw NV12 frame: " << src_w << "x" << src_h << std::endl;
            }

            if (ioctl(camera_fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "Failed to re-queue buffer " << buf.index << std::endl;
            }

            if (!ok) {
                std::cerr << "Conversion failed for frame " << i << std::endl;
                fail_count++;
                continue;
            }

            success_count++;

            if (success_count == 1) {
                std::cout << "Conversion mode: " << (used_rga ? "RGA HARDWARE" : "SOFTWARE FALLBACK") << std::endl;
            }

            if (success_count <= 5) {
                std::stringstream ss;
                ss << "camera_frame_" << std::setw(6) << std::setfill('0') << success_count << ".png";
                SavePNG(ss.str(), rgb_buf_, MODEL_WIDTH, MODEL_HEIGHT);
                std::cout << "Saved frame " << success_count << " to " << ss.str() << std::endl;
            }

            int data_size = MODEL_WIDTH * MODEL_HEIGHT * 3;
            Sample<ImageFrame> sample = writer_->LoanSample(sizeof(ImageFrame) + data_size);
            if (!sample.IsValid()) {
                std::cerr << "Failed to loan sample for frame " << i << std::endl;
                continue;
            }

            ImageFrame* frame = sample.message;
            frame->timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            frame->width = MODEL_WIDTH;
            frame->height = MODEL_HEIGHT;
            frame->format = 0;
            frame->stride = MODEL_WIDTH * 3;
            frame->data.resize(data_size);
            memcpy(frame->data.data(), rgb_buf_, data_size);

            writer_->Write(sample);
            frame_id_++;

            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        std::cout << "Camera capture done: " << success_count << " success, " << fail_count << " failed" << std::endl;
    }

    void RunSimulated(int frame_count) {
        std::cout << "Simulated mode, " << frame_count << " frames" << std::endl;
        std::mt19937 gen(42);
        std::uniform_int_distribution<int> dist(0, 255);
        int data_size = MODEL_WIDTH * MODEL_HEIGHT * 3;
        uint8_t* img = new uint8_t[data_size];

        for (int i = 0; i < frame_count && running_; i++) {
            for (int j = 0; j < data_size; j += 3) {
                img[j] = dist(gen);
                img[j + 1] = dist(gen);
                img[j + 2] = dist(gen);
            }

            Sample<ImageFrame> sample = writer_->LoanSample(sizeof(ImageFrame) + data_size);
            if (!sample.IsValid()) {
                std::cerr << "Failed to loan sample for frame " << i << std::endl;
                continue;
            }

            ImageFrame* frame = sample.message;
            frame->timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            frame->width = MODEL_WIDTH;
            frame->height = MODEL_HEIGHT;
            frame->format = 0;
            frame->stride = MODEL_WIDTH * 3;
            frame->data.resize(data_size);
            memcpy(frame->data.data(), img, data_size);

            writer_->Write(sample);
            frame_id_++;

            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        delete[] img;
        std::cout << "Simulated capture done: " << frame_count << " frames" << std::endl;
    }

    std::shared_ptr<Node> node_;
    std::shared_ptr<Writer<ImageFrame>> writer_;
    bool running_;
    uint64_t frame_id_;

    int camera_fd_;
    V4L2Buffer* buffers_;
    int buffer_count_;

    bool rga_inited_;
    uint8_t* rgb_buf_;
    int rgb_buf_size_;
    MB_BLK rgb_mb_;
    MB_POOL rgb_pool_;
    bool rgb_use_dma_;

    bool use_camera_;
    int camera_width_;
    int camera_height_;
    bool isp_inited_;
};

static ImageCaptureNode* g_node = nullptr;

void signal_handler(int sig) {
    if (g_node) {
        g_node->Stop();
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    bool use_camera = true;
    int frame_count = 100;

    if (argc >= 2) {
        frame_count = std::atoi(argv[1]);
    }

    if (argc >= 3 && std::string(argv[2]) == "--sim") {
        use_camera = false;
    }

    g_node = new ImageCaptureNode();
    if (!g_node->Init(use_camera)) {
        std::cerr << "Failed to initialize image capture node" << std::endl;
        delete g_node;
        return 1;
    }

    g_node->Run(frame_count);
    delete g_node;
    return 0;
}
