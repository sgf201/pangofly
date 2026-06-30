/*
 * Image Capture Node using real camera input
 * Based on RKADK framework for Luckfox Pico (RV1106)
 */

#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <cstring>
#include <signal.h>

#include "core/pangofly.h"
#include "core/node/node.h"
#include "idl/container/vector.h"

// RKADK headers
extern "C" {
#include "rkadk_common.h"
#include "rkadk_media_comm.h"
#include "rkadk_param.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_mb.h"
#include "rk_comm_video.h"
}

#ifdef RKAIQ
#include "isp/sample_isp.h"
#endif

using namespace pangofly;
using namespace pangofly::examples;

static bool g_running = true;

static void sigterm_handler(int sig) {
    fprintf(stderr, "signal %d\n", sig);
    g_running = false;
}

class CameraCaptureNode {
public:
    CameraCaptureNode() : node_(nullptr), writer_(nullptr), 
                          vi_pipe_(0), vi_chn_(0), cam_id_(0) {}
    
    bool Init(int cam_id = 0) {
        // Setup signal handler
        signal(SIGINT, sigterm_handler);
        signal(SIGTERM, sigterm_handler);
        
        cam_id_ = cam_id;
        
        // Initialize Pangofly
        if (!pangofly::Init()) {
            std::cerr << "Failed to initialize pangofly" << std::endl;
            return false;
        }
        
        node_ = CreateNode("camera_capture_node");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return false;
        }
        
        writer_ = node_->CreateWriter<ImageFrame>("image_channel");
        if (!writer_) {
            std::cerr << "Failed to create writer" << std::endl;
            return false;
        }
        
        // Initialize RKADK system
        RKADK_MPI_SYS_Init();
        RKADK_PARAM_Init(NULL, NULL);
        
#ifdef RKAIQ
        // Initialize ISP
        SAMPLE_ISP_PARAM stIspParam;
        memset(&stIspParam, 0, sizeof(SAMPLE_ISP_PARAM));
        stIspParam.iqFileDir = "/etc/iqfiles";
        stIspParam.WDRMode = RK_AIQ_WORKING_MODE_NORMAL;
        stIspParam.bMultiCam = false;
        stIspParam.fps = 30;
        SAMPLE_ISP_Start(cam_id_, stIspParam);
        std::cout << "ISP initialized for camera " << cam_id_ << std::endl;
#endif
        
        // Configure VI channel
        vi_pipe_ = cam_id_;
        vi_chn_ = 0;
        
        std::cout << "CameraCaptureNode initialized successfully" << std::endl;
        return true;
    }
    
    void Run(int frame_count = -1) {
        if (!writer_) {
            std::cerr << "Writer not initialized" << std::endl;
            return;
        }
        
        std::cout << "Starting camera capture..." << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        
        int captured_frames = 0;
        
        while (g_running && (frame_count < 0 || captured_frames < frame_count)) {
            VIDEO_FRAME_INFO_S frame_info;
            
            // Get frame from VI channel (timeout 1000ms)
            int ret = RK_MPI_VI_GetChnFrame(vi_pipe_, vi_chn_, &frame_info, 1000);
            
            if (ret != 0) {
                std::cerr << "RK_MPI_VI_GetChnFrame failed: " << ret << std::endl;
                continue;
            }
            
            // Convert to Pangofly ImageFrame
            auto image_frame = ConvertToImageFrame(frame_info);
            
            // Send via Pangofly
            if (writer_->Write(image_frame)) {
                captured_frames++;
                std::cout << "Frame " << captured_frames << " sent: " 
                          << image_frame.width() << "x" << image_frame.height() 
                          << ", format: " << image_frame.format()
                          << ", size: " << image_frame.data().size() << " bytes" << std::endl;
            } else {
                std::cerr << "Failed to write frame " << captured_frames << std::endl;
            }
            
            // Release frame back to VI
            RK_MPI_VI_ReleaseChnFrame(vi_pipe_, vi_chn_, &frame_info);
            
            // Control frame rate (optional)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "Camera capture completed. Total frames: " << captured_frames << std::endl;
    }
    
    void Shutdown() {
#ifdef RKAIQ
        SAMPLE_ISP_Stop(cam_id_);
#endif
        RKADK_MPI_SYS_Exit();
        
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
        
        std::cout << "CameraCaptureNode shutdown complete" << std::endl;
    }

private:
    ImageFrame ConvertToImageFrame(const VIDEO_FRAME_INFO_S& vi_frame) {
        ImageFrame frame;
        
        VIDEO_FRAME_S* stVFrame = &vi_frame.stVFrame;
        
        // Set frame metadata
        frame.set_timestamp(stVFrame->u64PTS);
        frame.set_width(stVFrame->u32Width);
        frame.set_height(stVFrame->u32Height);
        frame.set_format(static_cast<int32_t>(stVFrame->enPixelFormat));
        frame.set_stride(stVFrame->u32VirWidth);
        
        // Get virtual address of frame data
        void* vir_addr = RK_MPI_MB_Handle2VirAddr(stVFrame->pMbBlk);
        
        if (vir_addr) {
            // Calculate data size based on format
            size_t data_size = CalculateDataSize(stVFrame);
            
            // Copy frame data
            std::vector<uint8_t> data(data_size);
            memcpy(data.data(), vir_addr, data_size);
            frame.set_data(std::move(data));
        } else {
            std::cerr << "Failed to get virtual address for frame" << std::endl;
        }
        
        return frame;
    }
    
    size_t CalculateDataSize(const VIDEO_FRAME_S* stVFrame) {
        size_t size = 0;
        
        switch (stVFrame->enPixelFormat) {
            case RK_FMT_YUV420SP:  // NV12
                size = stVFrame->u32VirWidth * stVFrame->u32VirHeight * 3 / 2;
                break;
            case RK_FMT_YUV422SP:  // NV16
                size = stVFrame->u32VirWidth * stVFrame->u32VirHeight * 2;
                break;
            case RK_FMT_RGB888:
            case RK_FMT_BGR888:
                size = stVFrame->u32VirWidth * stVFrame->u32VirHeight * 3;
                break;
            case RK_FMT_ARGB8888:
            case RK_FMT_ABGR8888:
                size = stVFrame->u32VirWidth * stVFrame->u32VirHeight * 4;
                break;
            default:
                // Default to NV12 size
                size = stVFrame->u32VirWidth * stVFrame->u32VirHeight * 3 / 2;
                break;
        }
        
        return size;
    }
    
private:
    std::unique_ptr<Node> node_;
    std::shared_ptr<Writer<ImageFrame>> writer_;
    
    // VI parameters
    VI_PIPE vi_pipe_;
    VI_CHN vi_chn_;
    int cam_id_;
};

int main(int argc, char** argv) {
    int cam_id = 0;
    int frame_count = -1;  // Run until Ctrl+C
    
    if (argc > 1) {
        cam_id = std::stoi(argv[1]);
    }
    if (argc > 2) {
        frame_count = std::stoi(argv[2]);
    }
    
    CameraCaptureNode node;
    
    if (!node.Init(cam_id)) {
        return -1;
    }
    
    node.Run(frame_count);
    node.Shutdown();
    
    return 0;
}