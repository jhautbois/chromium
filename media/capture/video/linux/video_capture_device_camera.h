// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_VIDEO_LINUX_VIDEO_CAPTURE_DEVICE_CAMERA_H_
#define MEDIA_CAPTURE_VIDEO_LINUX_VIDEO_CAPTURE_DEVICE_CAMERA_H_

#include "base/threading/thread.h"
#include "media/capture/video/video_capture_device.h"

#include <libcamera/libcamera.h>

using libcamera::CameraManager;
using libcamera::PixelFormat;

namespace media {
namespace libcamera {
class CameraCaptureDelegate;

// Linux V4L2 implementation of VideoCaptureDevice.
class VideoCaptureDeviceCamera : public VideoCaptureDevice {
 public:
  static VideoPixelFormat LibCameraToChromiumPixelFormat(
      PixelFormat libcamera_format);
#if 0
  static std::vector<uint32_t> GetListOfUsableFourCCs(bool favour_mjpeg);
#endif

  explicit VideoCaptureDeviceCamera(
      const VideoCaptureDeviceDescriptor& device_descriptor,
      CameraManager* cm);
  ~VideoCaptureDeviceCamera() override;

  // VideoCaptureDevice implementation.
  void AllocateAndStart(const VideoCaptureParams& params,
                        std::unique_ptr<Client> client) override;
  void StopAndDeAllocate() override;
  void TakePhoto(TakePhotoCallback callback) override;
  void GetPhotoState(GetPhotoStateCallback callback) override;
  void SetPhotoOptions(mojom::PhotoSettingsPtr settings,
                       SetPhotoOptionsCallback callback) override;

 protected:
  virtual void SetRotation(int rotation);

  const VideoCaptureDeviceDescriptor device_descriptor_;

 private:
  // Internal delegate doing the actual capture setting, buffer allocation and
  // circulation with the libcamera API. Created in the thread where
  // VideoCaptureDeviceCamera lives but otherwise operating and deleted on
  // |camera_thread_|.
  std::unique_ptr<CameraCaptureDelegate> capture_impl_;

  // Photo-related requests waiting for |camera_thread_| to be active.
  std::vector<base::OnceClosure> photo_requests_queue_;

  base::Thread camera_thread_;  // Thread used for reading data from the device.

  // SetRotation() may get called even when the device is not started. When that
  // is the case we remember the value here and use it as soon as the device
  // gets started.
  int rotation_;

  CameraManager* cm_;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_IMPLICIT_CONSTRUCTORS(VideoCaptureDeviceCamera);
};

}  // namespace libcamera
}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_LINUX_VIDEO_CAPTURE_DEVICE_CAMERA_H_
