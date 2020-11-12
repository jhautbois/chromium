// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DEVICE_H_
#define MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DEVICE_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "media/capture/capture_export.h"

#include <libcamera/libcamera.h>

namespace media {

// Interface for abstracting out the libcamera API.
// This allows using a mock or fake implementation in testing.
class CAPTURE_EXPORT CameraCaptureDevice
    : public base::RefCountedThreadSafe<CameraCaptureDevice> {
 public:
 protected:
  virtual ~CameraCaptureDevice() {}

 private:
  friend class base::RefCountedThreadSafe<CameraCaptureDevice>;
};
}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_LINUX_CAMERA_CAPTURE_DEVICE_H_
