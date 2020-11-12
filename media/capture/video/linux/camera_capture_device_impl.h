// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DEVICE_IMPL_H_
#define MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DEVICE_IMPL_H_

#include "media/capture/capture_export.h"
#include "media/capture/video/linux/camera_capture_device.h"

namespace media {

// Implementation of CameraCaptureDevice interface that delegates to the actual
// Camera APIs.
class CAPTURE_EXPORT CameraCaptureDeviceImpl : public CameraCaptureDevice {
 public:
 private:
  ~CameraCaptureDeviceImpl() override;
};

}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DEVICE_IMPL_H_
