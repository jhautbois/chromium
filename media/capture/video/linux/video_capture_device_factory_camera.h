// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a VideoCaptureDeviceFactoryCamera class.

#ifndef MEDIA_CAPTURE_VIDEO_LINUX_VIDEO_CAPTURE_DEVICE_FACTORY_CAMERA_H_
#define MEDIA_CAPTURE_VIDEO_LINUX_VIDEO_CAPTURE_DEVICE_FACTORY_CAMERA_H_

#include <libcamera/libcamera.h>
#include <libcamera/property_ids.h>

#include "base/single_thread_task_runner.h"
#include "media/capture/video/linux/camera_capture_device.h"
#include "media/capture/video/linux/video_capture_device_camera.h"
#include "media/capture/video/video_capture_device_factory.h"
#include "media/capture/video_capture_types.h"

using libcamera::Camera;
using libcamera::CameraConfiguration;
using libcamera::CameraManager;
using libcamera::PixelFormat;
using libcamera::StreamConfiguration;
using libcamera::StreamFormats;

namespace media {
namespace libcamera {
// Extension of VideoCaptureDeviceFactory to create and manipulate Linux
// devices.
class CAPTURE_EXPORT VideoCaptureDeviceFactoryCamera
    : public VideoCaptureDeviceFactory {
 public:
  explicit VideoCaptureDeviceFactoryCamera(
      scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner);
  ~VideoCaptureDeviceFactoryCamera() override;

  std::unique_ptr<VideoCaptureDevice> CreateDevice(
      const VideoCaptureDeviceDescriptor& device_descriptor) override;
  void GetDevicesInfo(GetDevicesInfoCallback callback) override;

 private:
  scoped_refptr<CameraCaptureDevice> camera_;
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;

  CameraManager* cm_;

  DISALLOW_COPY_AND_ASSIGN(VideoCaptureDeviceFactoryCamera);
};

}  // namespace libcamera
}  // namespace media
#endif  // MEDIA_CAPTURE_VIDEO_LINUX_VIDEO_CAPTURE_DEVICE_FACTORY_CAMERA_H_
