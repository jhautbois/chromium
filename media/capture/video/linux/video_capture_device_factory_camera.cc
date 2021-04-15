// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/linux/video_capture_device_factory_camera.h"
using libcamera::ControlList;
using libcamera::Size;
using libcamera::StreamRole;

namespace media {
namespace libcamera {

VideoCaptureDeviceFactoryCamera::VideoCaptureDeviceFactoryCamera(
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner)
    : ui_task_runner_(ui_task_runner) {
  cm_ = new CameraManager;
  cm_->start();
}

VideoCaptureDeviceFactoryCamera::~VideoCaptureDeviceFactoryCamera() {
  cm_->stop();
  delete cm_;
}

std::unique_ptr<VideoCaptureDevice>
VideoCaptureDeviceFactoryCamera::CreateDevice(
    const VideoCaptureDeviceDescriptor& device_descriptor) {
  DCHECK(thread_checker_.CalledOnValidThread());

  std::shared_ptr<Camera> camera = cm_->get(device_descriptor.device_id);
  if (!camera) {
    LOG(ERROR) << "Cannot open device " << device_descriptor.device_id;
    return nullptr;
  }
  return std::make_unique<VideoCaptureDeviceCamera>(device_descriptor, cm_);
}

void VideoCaptureDeviceFactoryCamera::GetDevicesInfo(
    GetDevicesInfoCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  std::vector<VideoCaptureDeviceInfo> devices_info;
  std::vector<std::string> filepaths;

  std::string name;
  for (const std::shared_ptr<Camera>& cam : cm_->cameras()) {
    Camera* camera = cam.get();
    std::unique_ptr<CameraConfiguration> config;

    const ControlList& props = camera->properties();
    if (props.contains(::libcamera::properties::Model))
      name = props.get(::libcamera::properties::Model);
    else
      name = camera->id();
    filepaths.emplace_back(name);

    VideoCaptureControlSupport control_support;

    devices_info.emplace_back(VideoCaptureDeviceDescriptor(
        name, camera->id(), "", VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE,
        control_support, VideoCaptureTransportType::OTHER_TRANSPORT,
        VideoFacingMode::MEDIA_VIDEO_FACING_NONE));

    int ret = camera->acquire();
    if (ret == 0) {
      config = camera->generateConfiguration({StreamRole::Viewfinder});

      VideoCaptureFormats* supported_formats =
          &devices_info.back().supported_formats;

      unsigned int index = 0;
      for (const StreamConfiguration& cfg : *config) {
        LOG(ERROR) << index << ": " << cfg.toString();

        const StreamFormats& formats = cfg.formats();
        for (PixelFormat pixelformat : formats.pixelformats()) {
          LOG(ERROR) << " * Pixelformat: "
 				             << pixelformat.toString() << " "
				             << formats.range(pixelformat).toString();

          VideoCaptureFormat supported_format;
          supported_format.pixel_format =
              VideoCaptureDeviceCamera::LibCameraToChromiumPixelFormat(
                  pixelformat);

          if (supported_format.pixel_format == PIXEL_FORMAT_UNKNOWN)
            continue;

          for (const Size& size : formats.sizes(pixelformat)) {
            LOG(ERROR) << "  - " << size.toString();

            supported_format.frame_size.SetSize(size.width, size.height);
            supported_format.frame_rate = 0;
            supported_formats->push_back(supported_format);
          }
        }
      }
      camera->release();
    }
  }

  std::move(callback).Run(std::move(devices_info));
}

}  // namespace libcamera
}  // namespace media
