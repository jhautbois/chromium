// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/linux/video_capture_device_camera.h"
#include "media/capture/video/linux/camera_capture_delegate.h"

namespace media {

namespace libcamera {
// Translates Video4Linux pixel formats to Chromium pixel formats.
// static
VideoPixelFormat VideoCaptureDeviceCamera::LibCameraToChromiumPixelFormat(
    PixelFormat libcamera_format) {
  return CameraCaptureDelegate::LibCameraToChromiumPixelFormat(
      libcamera_format);
}

VideoCaptureDeviceCamera::VideoCaptureDeviceCamera(
    const VideoCaptureDeviceDescriptor& device_descriptor,
    CameraManager* cm)
    : device_descriptor_(device_descriptor),
      camera_thread_("CameraCaptureThread"),
      rotation_(0),
      cm_(cm) {}

VideoCaptureDeviceCamera::~VideoCaptureDeviceCamera() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Check if the thread is running.
  // This means that the device has not been StopAndDeAllocate()d properly.
  DCHECK(!camera_thread_.IsRunning());
  camera_thread_.Stop();
}

void VideoCaptureDeviceCamera::AllocateAndStart(
    const VideoCaptureParams& params,
    std::unique_ptr<VideoCaptureDevice::Client> client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!capture_impl_);
  if (camera_thread_.IsRunning())
    return;  // Wrong state.
  camera_thread_.Start();

  capture_impl_ = std::make_unique<CameraCaptureDelegate>(
      device_descriptor_, camera_thread_.task_runner(),
      rotation_, cm_);
  if (!capture_impl_) {
    client->OnError(VideoCaptureError::
                        kDeviceCaptureLinuxFailedToCreateVideoCaptureDelegate,
                    FROM_HERE, "Failed to create VideoCaptureDelegate");
    return;
  }
  camera_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraCaptureDelegate::AllocateAndStart,
                     capture_impl_->GetWeakPtr(),
                     params.requested_format.frame_size.width(),
                     params.requested_format.frame_size.height(),
                     params.requested_format.frame_rate, std::move(client)));

  for (auto& request : photo_requests_queue_)
    camera_thread_.task_runner()->PostTask(FROM_HERE, std::move(request));
  photo_requests_queue_.clear();
}

void VideoCaptureDeviceCamera::StopAndDeAllocate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!camera_thread_.IsRunning())
    return;  // Wrong state.
  camera_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&CameraCaptureDelegate::StopAndDeAllocate,
                                capture_impl_->GetWeakPtr()));
  camera_thread_.task_runner()->DeleteSoon(FROM_HERE, capture_impl_.release());
  camera_thread_.Stop();

  capture_impl_ = nullptr;
}

void VideoCaptureDeviceCamera::TakePhoto(TakePhotoCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(capture_impl_);
  auto functor =
      base::BindOnce(&CameraCaptureDelegate::TakePhoto,
                     capture_impl_->GetWeakPtr(), std::move(callback));
  if (!camera_thread_.IsRunning()) {
    // We have to wait until we get the device AllocateAndStart()ed.
    photo_requests_queue_.push_back(std::move(functor));
    return;
  }
  camera_thread_.task_runner()->PostTask(FROM_HERE, std::move(functor));
}

void VideoCaptureDeviceCamera::GetPhotoState(GetPhotoStateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto functor =
      base::BindOnce(&CameraCaptureDelegate::GetPhotoState,
                     capture_impl_->GetWeakPtr(), std::move(callback));
  if (!camera_thread_.IsRunning()) {
    // We have to wait until we get the device AllocateAndStart()ed.
    photo_requests_queue_.push_back(std::move(functor));
    return;
  }
  camera_thread_.task_runner()->PostTask(FROM_HERE, std::move(functor));
}

void VideoCaptureDeviceCamera::SetPhotoOptions(
    mojom::PhotoSettingsPtr settings,
    SetPhotoOptionsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto functor = base::BindOnce(&CameraCaptureDelegate::SetPhotoOptions,
                                capture_impl_->GetWeakPtr(),
                                std::move(settings), std::move(callback));
  if (!camera_thread_.IsRunning()) {
    // We have to wait until we get the device AllocateAndStart()ed.
    photo_requests_queue_.push_back(std::move(functor));
    return;
  }
  camera_thread_.task_runner()->PostTask(FROM_HERE, std::move(functor));
}

void VideoCaptureDeviceCamera::SetRotation(int rotation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  rotation_ = rotation;
  if (camera_thread_.IsRunning()) {
    camera_thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&CameraCaptureDelegate::SetRotation,
                                  capture_impl_->GetWeakPtr(), rotation));
  }
}
}  // namespace libcamera
}  // namespace media
