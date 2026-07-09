#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <cstring>
#include <signal.h>
#include <unistd.h>

#include "core/pangofly.h"
#include "core/node/node.h"
#include "face_detection.h"

#include "sample_comm.h"
#include "rga/im2d.h"
#include "rga/im2d_single.h"
#include "rga/im2d_buffer.h"
#include "rga/rga.h"

using namespace pangofly;
using namespace FaceDetection;

static const int MODEL_WIDTH = 640;
static const int MODEL_HEIGHT = 640;

class ImageCaptureNode {
public:
    ImageCaptureNode()
        : node_(nullptr), writer_(), running_(true), frame_id_(0),
          rga_inited_(false),
          rgb_buf_(nullptr), rgb_buf_size_(0),
          use_camera_(false), camera_width_(0), camera_height_(0),
          mpi_inited_(false), isp_inited_(false), vi_inited_(false) {}

    ~ImageCaptureNode() {
        Shutdown();
    }

    bool Init(bool use_camera = true, int camera_width = 720, int camera_height = 480) {
        use_camera_ = use_camera;
        camera_width_ = camera_width;
        camera_height_ = camera_height_;

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
            } else if (!InitMPI()) {
                std::cerr << "Failed to initialize MPI, falling back to simulated mode" << std::endl;
                use_camera_ = false;
            } else if (!InitVI()) {
                std::cerr << "Failed to initialize VI, falling back to simulated mode" << std::endl;
                use_camera_ = false;
            } else if (!InitRGA()) {
                std::cerr << "Failed to initialize RGA, falling back to simulated mode" << std::endl;
                use_camera_ = false;
            }
        }

        if (use_camera_) {
            std::cout << "ImageCaptureNode initialized (CAMERA mode with RGA acceleration)" << std::endl;
            std::cout << "  Camera: " << camera_width_ << "x" << camera_height_ << " NV12 (RK MPI)" << std::endl;
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
        CleanupVI();
        CleanupMPI();
        CleanupISP();
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
    }

private:
    bool InitMPI() {
        if (RK_MPI_SYS_Init() != RK_SUCCESS) {
            std::cerr << "RK_MPI_SYS_Init failed" << std::endl;
            return false;
        }
        mpi_inited_ = true;
        std::cout << "RK MPI initialized" << std::endl;
        return true;
    }

    void CleanupMPI() {
        if (mpi_inited_) {
            RK_MPI_SYS_Exit();
            mpi_inited_ = false;
        }
    }

    bool InitISP() {
        const char* iq_dir = "/etc/iqfiles";
        rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
        RK_BOOL multi_sensor = RK_FALSE;

        if (SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, (char*)iq_dir) != RK_SUCCESS) {
            std::cerr << "SAMPLE_COMM_ISP_Init failed" << std::endl;
            return false;
        }

        if (SAMPLE_COMM_ISP_Run(0) != RK_SUCCESS) {
            std::cerr << "SAMPLE_COMM_ISP_Run failed" << std::endl;
            SAMPLE_COMM_ISP_Stop(0);
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
        }
    }

    bool InitVI() {
        int ret = 0;
        int devId = 0;
        int pipeId = devId;
        int channelId = 0;

        VI_DEV_ATTR_S stDevAttr;
        VI_DEV_BIND_PIPE_S stBindPipe;
        memset(&stDevAttr, 0, sizeof(stDevAttr));
        memset(&stBindPipe, 0, sizeof(stBindPipe));

        ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
        if (ret == RK_ERR_VI_NOT_CONFIG) {
            ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
            if (ret != RK_SUCCESS) {
                std::cerr << "RK_MPI_VI_SetDevAttr failed: " << ret << std::endl;
                return false;
            }
        }

        ret = RK_MPI_VI_GetDevIsEnable(devId);
        if (ret != RK_SUCCESS) {
            ret = RK_MPI_VI_EnableDev(devId);
            if (ret != RK_SUCCESS) {
                std::cerr << "RK_MPI_VI_EnableDev failed: " << ret << std::endl;
                return false;
            }
            stBindPipe.u32Num = 1;
            stBindPipe.PipeId[0] = pipeId;
            ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
            if (ret != RK_SUCCESS) {
                std::cerr << "RK_MPI_VI_SetDevBindPipe failed: " << ret << std::endl;
                return false;
            }
            std::cout << "RK VI device enabled and pipe bound" << std::endl;
        } else {
            std::cout << "RK VI device already enabled" << std::endl;
        }

        VI_CHN_ATTR_S vi_chn_attr;
        memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
        vi_chn_attr.stIspOpt.u32BufCount = 3;
        vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
        vi_chn_attr.stSize.u32Width = camera_width_;
        vi_chn_attr.stSize.u32Height = camera_height_;
        vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
        vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
        vi_chn_attr.u32Depth = 2;

        ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
        if (ret != RK_SUCCESS) {
            std::cerr << "RK_MPI_VI_SetChnAttr failed: " << ret << std::endl;
            return false;
        }

        ret = RK_MPI_VI_EnableChn(0, channelId);
        if (ret != RK_SUCCESS) {
            std::cerr << "RK_MPI_VI_EnableChn failed: " << ret << std::endl;
            return false;
        }

        vi_inited_ = true;
        vi_chn_ = channelId;
        std::cout << "RK VI initialized: " << camera_width_ << "x" << camera_height_ << " NV12" << std::endl;
        return true;
    }

    void CleanupVI() {
        if (vi_inited_) {
            RK_MPI_VI_DisableChn(0, vi_chn_);
            vi_inited_ = false;
        }
    }

    bool InitRGA() {
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
        if (rgb_buf_) {
            free(rgb_buf_);
            rgb_buf_ = nullptr;
        }
        rga_inited_ = false;
    }

    bool ConvertNV12ToRGBWithResize(void* src_nv12, int src_w, int src_h,
                                 uint8_t* dst_rgb, int dst_w, int dst_h) {
        if (!rga_inited_) return false;

        IM_STATUS ret;
        rga_buffer_t src_img, dst_img;
        rga_buffer_handle_t src_handle = 0, dst_handle = 0;

        memset(&src_img, 0, sizeof(src_img));
        memset(&dst_img, 0, sizeof(dst_img));

        int src_fmt = RK_FORMAT_YCrCb_420_SP;
        int dst_fmt = RK_FORMAT_RGB_888;

        src_handle = importbuffer_virtualaddr(src_nv12, src_w * src_h * 3 / 2);
        dst_handle = importbuffer_virtualaddr(dst_rgb, dst_w * dst_h * 3);

        if (src_handle == 0 || dst_handle == 0) {
            std::cerr << "Failed to import RGA buffers" << std::endl;
            goto release;
        }

        src_img = wrapbuffer_handle(src_handle, src_w, src_h, src_fmt);
        dst_img = wrapbuffer_handle(dst_handle, dst_w, dst_h, dst_fmt);

        ret = imcheck(src_img, dst_img, {}, {});
        if (IM_STATUS_NOERROR != ret) {
            std::cerr << "RGA check error: " << imStrError(ret) << std::endl;
            goto release;
        }

        ret = imresize(src_img, dst_img);
        if (ret != IM_STATUS_SUCCESS) {
            std::cerr << "RGA resize failed: " << imStrError(ret) << std::endl;
            goto release;
        }

        ret = imcvtcolor(dst_img, dst_img, src_fmt, dst_fmt, IM_YUV_TO_RGB_BT601_FULL);
        if (ret != IM_STATUS_SUCCESS) {
            std::cerr << "RGA cvtcolor failed: " << imStrError(ret) << std::endl;
            goto release;
        }

        releasebuffer_handle(src_handle);
        releasebuffer_handle(dst_handle);
        return true;

release:
        if (src_handle) releasebuffer_handle(src_handle);
        if (dst_handle) releasebuffer_handle(dst_handle);
        return false;
    }

    void RunCamera(int frame_count) {
        std::cout << "Camera capture mode, " << frame_count << " frames" << std::endl;

        VIDEO_FRAME_INFO_S stViFrame;

        for (int i = 0; i < frame_count && running_; ++i) {
            RK_S32 s32Ret = RK_MPI_VI_GetChnFrame(0, vi_chn_, &stViFrame, 1000);
            if (s32Ret != RK_SUCCESS) {
                std::cerr << "RK_MPI_VI_GetChnFrame failed: " << s32Ret << std::endl;
                continue;
            }

            void* vi_data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
            int src_w = stViFrame.stVFrame.u32Width;
            int src_h = stViFrame.stVFrame.u32Height;

            bool ok = ConvertNV12ToRGBWithResize(vi_data, src_w, src_h,
                                                rgb_buf_, MODEL_WIDTH, MODEL_HEIGHT);

            RK_MPI_VI_ReleaseChnFrame(0, vi_chn_, &stViFrame);

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

            if (i % 30 == 0) {
                std::cout << "Frame " << i << " sent: "
                          << frame->width << "x" << frame->height
                          << " (RK MPI + RGA)" << std::endl;
            }
            if (!writer_->Write(sample)) {
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

            if (i % 10 == 0) {
                std::cout << "Frame " << i << " sent: "
                          << frame->width << "x" << frame->height << std::endl;
            }
            if (!writer_->Write(sample)) {
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

    bool use_camera_;
    int camera_width_;
    int camera_height_;

    bool mpi_inited_;
    bool isp_inited_;
    bool vi_inited_;
    int vi_chn_;

    bool rga_inited_;
    uint8_t* rgb_buf_;
    int rgb_buf_size_;
};

int main(int argc, char** argv) {
    ImageCaptureNode node;

    bool use_camera = true;
    int frame_count = 100;
    int camera_width = 720;
    int camera_height = 480;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--sim" || arg == "-s") {
            use_camera = false;
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

    if (use_camera) {
        system("/oem/usr/bin/RkLunch-stop.sh");
        sleep(1);
    }

    if (!node.Init(use_camera, camera_width, camera_height)) {
        return -1;
    }

    node.Run(frame_count);
    node.Shutdown();

    return 0;
}
