// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mman.h>

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "build/build_config.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/video_types.h"
#include "media/capture/mojom/image_capture_types.h"
#include "media/capture/video/blob_utils.h"
#include "media/capture/video/linux/camera_capture_delegate.h"
#include "media/capture/video/linux/video_capture_device_camera.h"

#include <libcamera/libcamera.h>

using media::mojom::MeteringMode;

using libcamera::FrameMetadata;
using libcamera::PixelFormat;
using libcamera::StreamRole;

namespace media {
namespace libcamera {
namespace {

struct {
  PixelFormat libcamera_format;
  VideoPixelFormat pixel_format;
} constexpr kSupportedFormatsAndPlanarity[] = {
    {::libcamera::formats::YUV420, PIXEL_FORMAT_I420},
    {::libcamera::formats::NV12, PIXEL_FORMAT_NV12},
    {::libcamera::formats::YUYV, PIXEL_FORMAT_YUY2},
    {::libcamera::formats::BGR888, PIXEL_FORMAT_RGB24},
    {::libcamera::formats::RGB888, PIXEL_FORMAT_XBGR},
    // MJPEG is usually sitting fairly low since we don't want to have to
    // decode. However, it is needed for large resolutions due to USB bandwidth
    // limitations, so GetListOfUsablePixelFormats() can duplicate it on top,
    // see that method.
    {::libcamera::formats::MJPEG, PIXEL_FORMAT_MJPEG},
};
}  // namespace

// static
VideoPixelFormat CameraCaptureDelegate::LibCameraToChromiumPixelFormat(
    PixelFormat libcamera_format) {
  for (const auto& supported_formats : kSupportedFormatsAndPlanarity) {
    if (supported_formats.libcamera_format == libcamera_format)
      return supported_formats.pixel_format;
  }
  // Not finding a pixel format is OK during device capabilities enumeration.
  // Let the caller decide if PIXEL_FORMAT_UNKNOWN is an error or
  // not.
  DVLOG(1) << "Unsupported pixel format: " << libcamera_format.toString();
  return PIXEL_FORMAT_UNKNOWN;
}

PixelFormat CameraCaptureDelegate::ChromiumPixelFormatToLibCamera(
    VideoPixelFormat pixel_format) {
  for (const auto& supported_formats : kSupportedFormatsAndPlanarity) {
    if (supported_formats.pixel_format == pixel_format)
      return supported_formats.libcamera_format;
  }
  // Not finding a pixel format is OK during device capabilities enumeration.
  // Let the caller decide if PIXEL_FORMAT_UNKNOWN is an error or
  // not.
  DVLOG(1) << "Unsupported pixel format: "
           << VideoPixelFormatToString(pixel_format);
  return PixelFormat(0);
}

// static
std::vector<PixelFormat> CameraCaptureDelegate::GetListOfUsablePixelFormats(
    bool prefer_mjpeg) {
  std::vector<PixelFormat> supported_formats;
  supported_formats.reserve(base::size(kSupportedFormatsAndPlanarity));

  // Duplicate MJPEG on top of the list depending on |prefer_mjpeg|.
  if (prefer_mjpeg)
    supported_formats.push_back(::libcamera::formats::MJPEG);

  for (const auto& format : kSupportedFormatsAndPlanarity)
    supported_formats.push_back(format.libcamera_format);

  return supported_formats;
}

CameraCaptureDelegate::CameraCaptureDelegate(
    const VideoCaptureDeviceDescriptor& device_descriptor,
    const scoped_refptr<base::SingleThreadTaskRunner>& camera_task_runner,
    int rotation,
    CameraManager* cm)
    : camera_task_runner_(camera_task_runner),
      device_descriptor_(device_descriptor),
      is_capturing_(false),
      timeout_count_(0),
      rotation_(rotation),
      cm_(cm) {
  selected_camera_ = cm_->get(device_descriptor_.device_id);
}

void CameraCaptureDelegate::AllocateAndStart(
    unsigned int width,
    unsigned int height,
    float frame_rate,
    std::unique_ptr<VideoCaptureDevice::Client> client) {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());
  DCHECK(client);
  client_ = std::move(client);

  int ret = selected_camera_->acquire();
  if (ret != 0) {
    LOG(ERROR) << "Camera can't be acquired: " << ret;
    return;
  }

  const std::vector<PixelFormat>& list_usuable_formats =
      GetListOfUsablePixelFormats(width > 640 || height > 480);
  auto best = list_usuable_formats.front();

  config_ =
      selected_camera_->generateConfiguration({StreamRole::VideoRecording});
  StreamConfiguration& cfg = config_->at(0);
  cfg.size = {width, height};
  cfg.pixelFormat = best;
  CameraConfiguration::Status status = config_->validate();

  switch (status) {
    case CameraConfiguration::Valid:
      capture_format_.frame_size.SetSize(cfg.size.width, cfg.size.height);
      capture_format_.frame_rate = frame_rate;
      capture_format_.pixel_format =
          LibCameraToChromiumPixelFormat(cfg.pixelFormat);
      LOG(INFO) << "Camera configuration validated: " << cfg.toString();
      break;
    case CameraConfiguration::Adjusted:
      capture_format_.frame_size.SetSize(cfg.size.width, cfg.size.height);
      capture_format_.frame_rate = frame_rate;
      capture_format_.pixel_format =
          LibCameraToChromiumPixelFormat(cfg.pixelFormat);
      LOG(WARNING) << "Camera configuration adjusted: " << cfg.toString();
      break;
    case CameraConfiguration::Invalid:
      LOG(ERROR) << "Camera configuration invalid: " << cfg.toString();
      selected_camera_->release();
      return;
  }

  selected_camera_->configure(config_.get());

  allocator = new FrameBufferAllocator(selected_camera_);
  nbuffers_ = UINT_MAX;
  for (StreamConfiguration& cfg : *config_) {
    int ret = allocator->allocate(cfg.stream());
    if (ret < 0) {
      LOG(ERROR) << "Can't allocate buffers";
    }

    unsigned int allocated = allocator->buffers(cfg.stream()).size();
    nbuffers_ = std::min(nbuffers_, allocated);
  }

  selected_camera_->requestCompleted.connect(
      this, &CameraCaptureDelegate::RequestComplete);

  if (!StartStream()) {
    UnmapBuffers();
    selected_camera_->requestCompleted.disconnect(
        this, &CameraCaptureDelegate::RequestComplete);
    selected_camera_->release();
    return;
  }

  captureCount_ = 0;

  client_->OnStarted();
}

void CameraCaptureDelegate::StopAndDeAllocate() {
  StopStream();
  selected_camera_.reset();

  UnmapBuffers();
  delete allocator;

  config_.reset();

  client_.reset();
}

void CameraCaptureDelegate::TakePhoto(
    VideoCaptureDevice::TakePhotoCallback callback) {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());
  take_photo_callbacks_.push(std::move(callback));
}

void CameraCaptureDelegate::GetPhotoState(
    VideoCaptureDevice::GetPhotoStateCallback callback) {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());
  if (!is_capturing_)
    return;
  mojom::PhotoStatePtr photo_capabilities = mojo::CreateEmptyPhotoState();
  std::move(callback).Run(std::move(photo_capabilities));
}

void CameraCaptureDelegate::SetPhotoOptions(
    mojom::PhotoSettingsPtr settings,
    VideoCaptureDevice::SetPhotoOptionsCallback callback) {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());
  if (!is_capturing_)
    return;
  std::move(callback).Run(true);
}

void CameraCaptureDelegate::SetRotation(int rotation) {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());
  DCHECK_GE(rotation, 0);
  DCHECK_LT(rotation, 360);
  DCHECK_EQ(rotation % 90, 0);
  rotation_ = rotation;
}

base::WeakPtr<CameraCaptureDelegate> CameraCaptureDelegate::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

CameraCaptureDelegate::~CameraCaptureDelegate() = default;

void CameraCaptureDelegate::MapBuffer(FrameBuffer* buffer) {
  for (const FrameBuffer::Plane& plane : buffer->planes()) {
    void* memory =
        mmap(nullptr, plane.length, PROT_READ, MAP_SHARED, plane.fd.fd(), 0);

    mappedBuffers_[plane.fd.fd()] = std::make_pair(memory, plane.length);
  }
}

void CameraCaptureDelegate::UnmapBuffers() {
  for (auto& iter : mappedBuffers_) {
    void* memory = iter.second.first;
    unsigned int length = iter.second.second;
    munmap(memory, length);
  }
  mappedBuffers_.clear();
}

bool CameraCaptureDelegate::StartStream() {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());
  if (is_capturing_)
    return true;

  for (unsigned int i = 0; i < nbuffers_; i++) {
    std::unique_ptr<Request> request = selected_camera_->createRequest();
    if (!request) {
      LOG(ERROR) << "Can't create request";
      return false;
    }

    for (StreamConfiguration& cfg : *config_) {
      Stream* stream = cfg.stream();
      const std::vector<std::unique_ptr<FrameBuffer>>& buffers =
          allocator->buffers(stream);
      const std::unique_ptr<FrameBuffer>& buffer = buffers[i];

      if (request->addBuffer(stream, buffer.get()) < 0) {
        LOG(ERROR) << "Can't set buffer for request";
        return false;
      }

      MapBuffer(buffer.get());
    }

    requests_.push_back(std::move(request));
  }

  if (!is_capturing_) {
    if (selected_camera_->start()) {
      LOG(ERROR) << "Failed to start capture";
      return false;
    }
    is_capturing_ = true;
  }

  for (std::unique_ptr<Request>& request : requests_) {
    if (selected_camera_->queueRequest(request.get()) < 0) {
      LOG(ERROR) << "Can't queue request";
      selected_camera_->stop();
      return false;
    }
  }

  return true;
}

void CameraCaptureDelegate::RequestComplete(Request* request) {
  DCHECK(is_capturing_);

  if (request->status() != Request::RequestComplete)
    return;

  const Request::BufferMap& buffers = request->buffers();
  const base::TimeTicks now = base::TimeTicks::Now();
  if (first_ref_time_.is_null())
    first_ref_time_ = now;
  const base::TimeDelta timestamp = now - first_ref_time_;

  for (auto it = buffers.begin(); it != buffers.end(); ++it) {
    FrameBuffer* buffer = it->second;
    int bytesused = buffer->metadata().planes[0].bytesused;
    void* data = mappedBuffers_[buffer->planes()[0].fd.fd()].first;

    client_->OnIncomingCapturedData(
        (const uint8_t*)data, bytesused, capture_format_, gfx::ColorSpace(),
        rotation_, false /* flip_y */, now, timestamp);
    while (!take_photo_callbacks_.empty()) {
      VideoCaptureDevice::TakePhotoCallback cb =
          std::move(take_photo_callbacks_.front());
      take_photo_callbacks_.pop();

      mojom::BlobPtr blob = RotateAndBlobify((const uint8_t*)data, bytesused,
                                             capture_format_, rotation_);
      if (blob)
        std::move(cb).Run(std::move(blob));
    }
  }

  captureCount_++;

  request->reuse(Request::ReuseBuffers);
  selected_camera_->queueRequest(request);
}

bool CameraCaptureDelegate::StopStream() {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());

  selected_camera_->stop();
  selected_camera_->release();
  is_capturing_ = false;
  selected_camera_->requestCompleted.disconnect(
      this, &CameraCaptureDelegate::RequestComplete);
  requests_.clear();

  return true;
}

void CameraCaptureDelegate::SetErrorState(VideoCaptureError error,
                                          const base::Location& from_here,
                                          const std::string& reason) {
  DCHECK(camera_task_runner_->BelongsToCurrentThread());
  is_capturing_ = false;
  client_->OnError(error, from_here, reason);
}
}  // namespace libcamera
}  // namespace media
