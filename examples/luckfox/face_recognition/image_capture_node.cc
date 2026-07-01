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
#include <linux/videodev2.h>

#include "core/pangofly.h"
#include "core/node/node.h"
#include "face_detection.h"

#include "rga/im2d.h"
#include "rga/im2d_single.h"
#include "rga/im2d_buffer.h"
#include "rga/rga.h"

#define RKCIF_CMD_SET_CSI_MEMORY_MODE _IOW('V', 0x20, int)
#define CSI_LVDS_MEM_WORD_LOW_ALIGN  0x00000001

using namespace pangofly;
using namespace FaceDetection;

static const int MODEL_WIDTH = 640;
static const int MODEL_HEIGHT = 640;

struct V4L2Buffer {
    void* start;
    size_t length;
    int export_fd;
};

class ImageCaptureNode {
public:
    ImageCaptureNode()
        : node_(nullptr), writer_(), running_(true), frame_id_(0),
          camera_fd_(-1), buffers_(nullptr), buffer_count_(0),
          rga_inited_(false),
          nv12_resize_buf_(nullptr), nv12_resize_size_(0),
          rgb_buf_(nullptr), rgb_buf_size_(0),
          use_camera_(false), camera_width_(0), camera_height_(0) {}

    ~ImageCaptureNode() {
        Shutdown();
    }

    bool Init(bool use_camera = true, const std::string& device = "/dev/video11",
             int camera_width = 640, int camera_height = 480) {
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
            if (!InitCamera(device, camera_width, camera_height)) {
                std::cerr << "Failed to initialize camera, falling back to simulated mode" << std::endl;
                use_camera_ = false;
            } else if (!InitRGA()) {
                std::cerr << "Failed to initialize RGA, falling back to simulated mode" << std::endl;
                use_camera_ = false;
                CleanupCamera();
            }
        }

        if (use_camera_) {
            std::cout << "ImageCaptureNode initialized (CAMERA mode with RGA acceleration)" << std::endl;
            std::cout << "  Camera: " << camera_width_ << "x" << camera_height_ << " NV12" << std::endl;
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
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
    }

private:
    bool InitCamera(const std::string& device, int width, int height) {
        camera_fd_ = open(device.c_str(), O_RDWR, 0);
        if (camera_fd_ < 0) {
            std::cerr << "Failed to open camera device: " << device << ", errno: " << errno << std::endl;
            return false;
        }

        v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

        if (ioctl(camera_fd_, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "Failed to set NV12 format" << std::endl;
            return false;
        }

        camera_width_ = fmt.fmt.pix_mp.width;
        camera_height_ = fmt.fmt.pix_mp.height;

        int memory_mode = CSI_LVDS_MEM_WORD_LOW_ALIGN;
        if (ioctl(camera_fd_, RKCIF_CMD_SET_CSI_MEMORY_MODE, &memory_mode) < 0) {
            std::cerr << "Warning: Failed to set CIF memory mode" << std::endl;
        }

        v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_DMABUF;

        if (ioctl(camera_fd_, VIDIOC_REQBUFS, &req) < 0) {
            std::cerr << "Failed to request buffers" << std::endl;
            return false;
        }

        buffer_count_ = req.count;
        buffers_ = new V4L2Buffer[buffer_count_];

        for (int i = 0; i < buffer_count_; i++) {
            v4l2_plane planes[VIDEO_MAX_PLANES];
            v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            memset(planes, 0, sizeof(planes));

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.index = i;
            buf.memory = V4L2_MEMORY_DMABUF;
            buf.m.planes = planes;
            buf.length = 1;

            if (ioctl(camera_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "Failed to query buffer " << i << std::endl;
                return false;
            }

            int export_fd = ioctl(camera_fd_, VIDIOC_EXPBUF, &buf);
            if (export_fd < 0) {
                std::cerr << "Failed to export buffer " << i << std::endl;
                return false;
            }

            buffers_[i].start = mmap(NULL, buf.m.planes[0].length,
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    camera_fd_, buf.m.planes[0].m.fd);
            buffers_[i].length = buf.m.planes[0].length;
            buffers_[i].export_fd = export_fd;

            if (buffers_[i].start == MAP_FAILED) {
                std::cerr << "Failed to map buffer " << i << std::endl;
                return false;
            }

            if (ioctl(camera_fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "Failed to queue buffer " << i << std::endl;
                return false;
            }
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(camera_fd_, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "Failed to start streaming" << std::endl;
            return false;
        }

        std::cout << "Camera initialized: " << camera_width_ << "x" << camera_height_ << " NV12" << std::endl;
        return true;
    }

    void CleanupCamera() {
        if (camera_fd_ >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            ioctl(camera_fd_, VIDIOC_STREAMOFF, &type);
        }

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

        if (camera_fd_ >= 0) {
            close(camera_fd_);
            camera_fd_ = -1;
        }
    }

    bool InitRGA() {
        int src_nv12_size = camera_width_ * camera_height_ * 3 / 2;
        int dst_nv12_size = MODEL_WIDTH * MODEL_HEIGHT * 3 / 2;
        nv12_resize_size_ = dst_nv12_size;
        nv12_resize_buf_ = (uint8_t*)malloc(nv12_resize_size_);
        if (!nv12_resize_buf_) {
            std::cerr << "Failed to allocate NV12 resize buffer" << std::endl;
            return false;
        }
        memset(nv12_resize_buf_, 0, nv12_resize_size_);

        rgb_buf_size_ = MODEL_WIDTH * MODEL_HEIGHT * 3;
        rgb_buf_ = (uint8_t*)malloc(rgb_buf_size_);
        if (!rgb_buf_) {
            std::cerr << "Failed to allocate RGB buffer" << std::endl;
            return false;
        }
        memset(rgb_buf_, 0, rgb_buf_size_);

        rga_inited_ = true;
        std::cout << "RGA initialized" << std::endl;
        return true;
    }

    void CleanupRGA() {
        if (nv12_resize_buf_) {
            free(nv12_resize_buf_);
            nv12_resize_buf_ = nullptr;
        }
        if (rgb_buf_) {
            free(rgb_buf_);
            rgb_buf_ = nullptr;
        }
        rga_inited_ = false;
    }

    bool ConvertNV12ToRGBWithResize(uint8_t* src_nv12, int src_w, int src_h,
                                 uint8_t* dst_rgb, int dst_w, int dst_h) {
        if (!rga_inited_) return false;

        IM_STATUS ret;
        rga_buffer_t src_img, resize_img, dst_img;
        rga_buffer_handle_t src_handle = 0, resize_handle = 0, dst_handle = 0;

        memset(&src_img, 0, sizeof(src_img));
        memset(&resize_img, 0, sizeof(resize_img));
        memset(&dst_img, 0, sizeof(dst_img));

        int src_nv12_fmt = RK_FORMAT_YCrCb_420_SP;
        int dst_rgb_fmt = RK_FORMAT_RGB_888;

        src_handle = importbuffer_virtualaddr(src_nv12, src_w * src_h * 3 / 2);
        resize_handle = importbuffer_virtualaddr(nv12_resize_buf_, nv12_resize_size_);
        dst_handle = importbuffer_virtualaddr(dst_rgb, dst_w * dst_h * 3);

        if (src_handle == 0 || resize_handle == 0 || dst_handle == 0) {
            std::cerr << "Failed to import RGA buffers" << std::endl;
            goto release;
        }

        src_img = wrapbuffer_handle(src_handle, src_w, src_h, src_nv12_fmt);
        resize_img = wrapbuffer_handle(resize_handle, dst_w, dst_h, src_nv12_fmt);
        dst_img = wrapbuffer_handle(dst_handle, dst_w, dst_h, dst_rgb_fmt);

        ret = imcheck(src_img, resize_img, {}, {});
        if (IM_STATUS_NOERROR != ret) {
            std::cerr << "RGA check (resize) error: " << imStrError(ret) << std::endl;
            goto release;
        }

        ret = imresize(src_img, resize_img);
        if (ret != IM_STATUS_SUCCESS) {
            std::cerr << "RGA resize failed: " << imStrError(ret) << std::endl;
            goto release;
        }

        ret = imcheck(resize_img, dst_img, {}, {});
        if (IM_STATUS_NOERROR != ret) {
            std::cerr << "RGA check (cvtcolor) error: " << imStrError(ret) << std::endl;
            goto release;
        }

        ret = imcvtcolor(resize_img, dst_img, src_nv12_fmt, dst_rgb_fmt, IM_YUV_TO_RGB_BT601_FULL);
        if (ret != IM_STATUS_SUCCESS) {
            std::cerr << "RGA cvtcolor failed: " << imStrError(ret) << std::endl;
            goto release;
        }

        releasebuffer_handle(src_handle);
        releasebuffer_handle(resize_handle);
        releasebuffer_handle(dst_handle);
        return true;

release:
        if (src_handle) releasebuffer_handle(src_handle);
        if (resize_handle) releasebuffer_handle(resize_handle);
        if (dst_handle) releasebuffer_handle(dst_handle);
        return false;
    }

    void RunCamera(int frame_count) {
        std::cout << "Camera capture mode, " << frame_count << " frames" << std::endl;

        for (int i = 0; i < frame_count && running_; ++i) {
            v4l2_plane planes[VIDEO_MAX_PLANES];
            v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            memset(planes, 0, sizeof(planes));

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_DMABUF;
            buf.m.planes = planes;
            buf.length = 1;

            if (ioctl(camera_fd_, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) continue;
                std::cerr << "Failed to dequeue buffer, errno: " << errno << std::endl;
                break;
            }

            uint8_t* nv12_data = (uint8_t*)buffers_[buf.index].start;
            int src_w = camera_width_;
            int src_h = camera_height_;

            bool ok = ConvertNV12ToRGBWithResize(nv12_data, src_w, src_h,
                                                rgb_buf_, MODEL_WIDTH, MODEL_HEIGHT);

            ioctl(camera_fd_, VIDIOC_QBUF, &buf);

            if (!ok) {
                std::cerr << "RGA conversion failed for frame " << i << std::endl;
                continue;
            }

            int data_size = MODEL_WIDTH * MODEL_HEIGHT * 3;
            Sample<ImageFrame> sample = writer_->LoanSample(sizeof(ImageFrame) + data_size);
            if (!sample.IsValid()) {
                std::cerr << "Failed to loan sample for frame " << i << std::endl;
                continue;
            }

            ImageFrame* frame = sample.message;
            frame->timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            frame->width = MODEL_WIDTH;
            frame->height = MODEL_HEIGHT;
            frame->format = 0;
            frame->stride = MODEL_WIDTH * 3;

            frame->data.resize(data_size);
            memcpy(frame->data.data(), rgb_buf_, data_size);

            if (writer_->Write(sample)) {
                if (i % 30 == 0) {
                    std::cout << "Frame " << i << " sent: "
                              << frame->width << "x" << frame->height
                              << " (RGA accelerated)" << std::endl;
                }
            } else {
                std::cerr << "Failed to write frame " << i << std::endl;
                writer_->Release(sample);
            }

            frame_id_++;
        }
    }

    void RunSimulated(int frame_count) {
        std::cout << "Simulated mode, " << frame_count << " frames" << std::endl;

        for (int i = 0; i < frame_count && running_; ++i) {
            int32_t data_size = MODEL_WIDTH * MODEL_HEIGHT * 3;

            Sample<ImageFrame> sample = writer_->LoanSample(sizeof(ImageFrame) + data_size);
            if (!sample.IsValid()) {
                std::cerr << "Failed to loan sample for frame " << i << std::endl;
                continue;
            }

            ImageFrame* frame = sample.message;
            frame->timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            frame->width = MODEL_WIDTH;
            frame->height = MODEL_HEIGHT;
            frame->format = 0;
            frame->stride = MODEL_WIDTH * 3;

            frame->data.resize(data_size);
            for (size_t j = 0; j < data_size; j += 3) {
                frame->data[j] = static_cast<uint8_t>((i * 10 + j) % 256);
                frame->data[j + 1] = static_cast<uint8_t>((i * 5 + j * 2) % 256);
                frame->data[j + 2] = static_cast<uint8_t>((i * 3 + j * 3) % 256);
            }

            if (writer_->Write(sample)) {
                if (i % 10 == 0) {
                    std::cout << "Frame " << i << " sent: "
                              << frame->width << "x" << frame->height << std::endl;
                }
            } else {
                std::cerr << "Failed to write frame " << i << std::endl;
                writer_->Release(sample);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            frame_id_++;
        }
    }

    std::unique_ptr<Node> node_;
    std::shared_ptr<Writer<ImageFrame>> writer_;
    bool running_;
    int frame_id_;

    int camera_fd_;
    V4L2Buffer* buffers_;
    int buffer_count_;
    bool use_camera_;
    int camera_width_;
    int camera_height_;

    bool rga_inited_;
    uint8_t* nv12_resize_buf_;
    int nv12_resize_size_;
    uint8_t* rgb_buf_;
    int rgb_buf_size_;
};

int main(int argc, char** argv) {
    ImageCaptureNode node;

    bool use_camera = true;
    std::string device = "/dev/video11";
    int frame_count = 100;
    int camera_width = 640;
    int camera_height = 480;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--sim" || arg == "-s") {
            use_camera = false;
        } else if (arg == "--device" || arg == "-d") {
            if (i + 1 < argc) {
                device = argv[++i];
            }
        } else if (arg == "--width") {
            if (i + 1 < argc) {
                camera_width = std::stoi(argv[++i]);
            }
        } else if (arg == "--height") {
            if (i + 1 < argc) {
                camera_height = std::stoi(argv[++i]);
            }
        } else {
            frame_count = std::stoi(arg);
        }
    }

    if (!node.Init(use_camera, device, camera_width, camera_height)) {
        return -1;
    }

    node.Run(frame_count);
    node.Shutdown();

    return 0;
}
