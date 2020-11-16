// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DELEGATE_H_
#define MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DELEGATE_H_
#include <vector>

#include "base/macros.h"
#include "build/build_config.h"
#include "media/capture/video/video_capture_device.h"

#include <libcamera/libcamera.h>

using libcamera::Camera;
using libcamera::CameraConfiguration;
using libcamera::CameraManager;
using libcamera::FrameBuffer;
using libcamera::FrameBufferAllocator;
using libcamera::PixelFormat;
using libcamera::Request;
using libcamera::Stream;
using libcamera::StreamConfiguration;

namespace base {
class Location;
}  // namespace base

namespace media {
namespace libcamera {
// Class doing the actual Linux capture using Libcamera API.
// Created on the owner's thread, otherwise living, operating
// and destroyed on |camera_task_runner_|.
class CAPTURE_EXPORT CameraCaptureDelegate final {
 public:
  // Returns the Chrome pixel format for |libcamera::PixelFormat| or
  // PIXEL_FORMAT_UNKNOWN.
  static VideoPixelFormat LibCameraToChromiumPixelFormat(
      PixelFormat libcamera_format);
  static PixelFormat ChromiumPixelFormatToLibCamera(
      VideoPixelFormat pixel_format);

  // Composes a list of usable and supported pixel formats, in order of
  // preference, with MJPEG prioritised depending on |prefer_mjpeg|.
  static std::vector<PixelFormat> GetListOfUsablePixelFormats(
      bool prefer_mjpeg);

  CameraCaptureDelegate(
      const VideoCaptureDeviceDescriptor& device_descriptor,
      const scoped_refptr<base::SingleThreadTaskRunner>& camera_task_runner,
      int rotation,
      CameraManager* cm);
  ~CameraCaptureDelegate();

  // Forward-to versions of VideoCaptureDevice virtual methods.
  void AllocateAndStart(unsigned int width,
                        unsigned int height,
                        float frame_rate,
                        std::unique_ptr<VideoCaptureDevice::Client> client);
  void StopAndDeAllocate();

  void TakePhoto(VideoCaptureDevice::TakePhotoCallback callback);

  void GetPhotoState(VideoCaptureDevice::GetPhotoStateCallback callback);
  void SetPhotoOptions(mojom::PhotoSettingsPtr settings,
                       VideoCaptureDevice::SetPhotoOptionsCallback callback);

  void SetRotation(int rotation);

  base::WeakPtr<CameraCaptureDelegate> GetWeakPtr();

 private:
  friend class CameraCaptureDelegateTest;

  bool StartStream();
  void DoCapture();
  bool StopStream();

  void SetErrorState(VideoCaptureError error,
                     const base::Location& from_here,
                     const std::string& reason);

  void RequestComplete(Request* request);
  void MapBuffer(FrameBuffer* buffer);
  void UnmapBuffers(void);

  // TODO(jhautbois): Can't use a scoped_refptr because libcamera uses
  // a std::shared_ptr. Any solution ?
  std::shared_ptr<Camera> selected_camera_;
  FrameBufferAllocator* allocator;
  unsigned int nbuffers_;
  std::unique_ptr<CameraConfiguration> config_;
  std::vector<std::unique_ptr<Request>> requests_;
  unsigned int captureCount_;
  uint64_t last_;
  std::map<int, std::pair<void*, unsigned int>> mappedBuffers_;

  const scoped_refptr<base::SingleThreadTaskRunner> camera_task_runner_;
  const VideoCaptureDeviceDescriptor device_descriptor_;

  // The following members are only known on AllocateAndStart().
  VideoCaptureFormat capture_format_;
  std::unique_ptr<VideoCaptureDevice::Client> client_;

  base::queue<VideoCaptureDevice::TakePhotoCallback> take_photo_callbacks_;

  bool is_capturing_;
  int timeout_count_;

  base::TimeTicks first_ref_time_;

  // Clockwise rotation in degrees. This value should be 0, 90, 180, or 270.
  int rotation_;

  base::WeakPtrFactory<CameraCaptureDelegate> weak_factory_{this};
  CameraManager* cm_;

  DISALLOW_COPY_AND_ASSIGN(CameraCaptureDelegate);
};

}  // namespace libcamera
}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DELEGATE_H_
