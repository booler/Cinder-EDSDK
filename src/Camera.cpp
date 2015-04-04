//
//  Camera.cpp
//  Cinder-EDSDK
//
//  Created by Jean-Pierre Mouilleseaux on 08 Dec 2013.
//  Copyright 2013-2015 Chorded Constructions. All rights reserved.
//

#include "Camera.h"
#include "CameraBrowser.h"

using namespace ci;
using namespace ci::app;

namespace Cinder { namespace EDSDK {

#pragma mark CAMERA FILE

CameraFileRef CameraFile::create(const EdsDirectoryItemRef& directoryItem) {
    return CameraFileRef(new CameraFile(directoryItem))->shared_from_this();
}

CameraFile::CameraFile(const EdsDirectoryItemRef& directoryItem) {
    if (!directoryItem) {
        throw Exception();
    }

    EdsRetain(directoryItem);
    mDirectoryItem = directoryItem;

    EdsError error = EdsGetDirectoryItemInfo(mDirectoryItem, &mDirectoryItemInfo);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get directory item info" << std::endl;
        throw Exception();
    }
}

CameraFile::~CameraFile() {
    EdsRelease(mDirectoryItem);
    mDirectoryItem = NULL;
}

#pragma mark - CAMERA

CameraRef Camera::create(const EdsCameraRef& camera) {
    return CameraRef(new Camera(camera))->shared_from_this();
}

Camera::Camera(const EdsCameraRef& camera) : mHasOpenSession(false), mIsLiveViewActive(false) {
    if (!camera) {
        throw Exception();
    }

    EdsRetain(camera);
    mCamera = camera;

    EdsError error = EdsGetDeviceInfo(mCamera, &mDeviceInfo);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get device info" << std::endl;
        // TODO - NULL out mDeviceInfo
    }

    // set event handlers
    error = EdsSetObjectEventHandler(mCamera, kEdsObjectEvent_All, Camera::handleObjectEvent, this);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to set object event handler" << std::endl;
    }
    error = EdsSetPropertyEventHandler(mCamera, kEdsPropertyEvent_All, Camera::handlePropertyEvent, this);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to set property event handler" << std::endl;
    }
    error = EdsSetCameraStateEventHandler(mCamera, kEdsStateEvent_All, Camera::handleStateEvent, this);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to set object event handler" << std::endl;
    }
}

Camera::~Camera() {
    mRemovedHandler = NULL;
    mFileAddedHandler = NULL;

    if (mIsLiveViewActive) {
        requestStopLiveView();
    }

    if (mHasOpenSession) {
        requestCloseSession();
    }

    // NB - starting with EDSDK 2.10, this release will cause an EXC_BAD_ACCESS (code=EXC_I386_GPFLT) at the end of the runloop
//    EdsRelease(mCamera);
    mCamera = nullptr;
}

#pragma mark -

void Camera::connectRemovedHandler(const std::function<void(CameraRef)>& handler) {
    mRemovedHandler = handler;
}

void Camera::connectFileAddedHandler(const std::function<void(CameraRef, CameraFileRef)>& handler) {
    mFileAddedHandler = handler;
}

EdsError Camera::requestOpenSession(const Settings &settings) {
    if (mHasOpenSession) {
        return EDS_ERR_SESSION_ALREADY_OPEN;
    }

    EdsError error = EdsOpenSession(mCamera);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to open camera session" << std::endl;
        return error;
    }
    mHasOpenSession = true;

    mShouldKeepAlive = settings.getShouldKeepAlive();
    EdsUInt32 saveTo = settings.getPictureSaveLocation();
    error = EdsSetPropertyData(mCamera, kEdsPropID_SaveTo, 0, sizeof(saveTo) , &saveTo);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to set save destination host/device" << std::endl;
        return error;
    }

    if (saveTo == kEdsSaveTo_Host) {
        // ??? - requires UI lock?
        EdsCapacity capacity = {0x7FFFFFFF, 0x1000, 1};
        error = EdsSetCapacity(mCamera, capacity);
        if (error != EDS_ERR_OK) {
            console() << "ERROR - failed to set capacity of host" << std::endl;
            return error;
        }
    }

    return EDS_ERR_OK;
}

EdsError Camera::requestCloseSession() {
    if (!mHasOpenSession) {
        return EDS_ERR_SESSION_NOT_OPEN;
    }

    EdsError error = EdsCloseSession(mCamera);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to close camera session" << std::endl;
        return error;
    }

    mHasOpenSession = false;
    return EDS_ERR_OK;
}

EdsError Camera::requestTakePicture() {
    if (!mHasOpenSession) {
        return EDS_ERR_SESSION_NOT_OPEN;
    }

    EdsError error = EdsSendCommand(mCamera, kEdsCameraCommand_TakePicture, 0);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to take picture" << std::endl;
    }
    return error;
}

void Camera::requestDownloadFile(const CameraFileRef& file, const fs::path& destinationFolderPath, const std::function<void(EdsError error, fs::path outputFilePath)>& callback) {
    // check if destination exists and create if not
    if (!fs::exists(destinationFolderPath)) {
        bool status = fs::create_directories(destinationFolderPath);
        if (!status) {
            console() << "ERROR - failed to create destination folder path '" << destinationFolderPath << "'" << std::endl;
            return callback(EDS_ERR_INTERNAL_ERROR, NULL);
        }
    }

    fs::path filePath = destinationFolderPath / file->getName();

    EdsStreamRef stream = NULL;
    EdsError error = EdsCreateFileStream(filePath.generic_string().c_str(), kEdsFileCreateDisposition_CreateAlways, kEdsAccess_ReadWrite, &stream);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to create file stream" << std::endl;
        goto download_cleanup;
    }

    error = EdsDownload(file->mDirectoryItem, file->getSize(), stream);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to download" << std::endl;
        goto download_cleanup;
    }

    error = EdsDownloadComplete(file->mDirectoryItem);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to mark download as complete" << std::endl;
        goto download_cleanup;
    }

download_cleanup:
    if (stream) {
        EdsRelease(stream);
    }

    callback(error, filePath);
}

void Camera::requestReadFile(const CameraFileRef& file, const std::function<void(EdsError error, SurfaceRef surface)>& callback) {
    Buffer buffer = NULL;
    SurfaceRef surface = nullptr;

    EdsStreamRef stream = NULL;
    EdsError error = EdsCreateMemoryStream(0, &stream);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to create memory stream" << std::endl;
        goto read_cleanup;
    }

    error = EdsDownload(file->mDirectoryItem, file->getSize(), stream);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to download" << std::endl;
        goto read_cleanup;
    }

    error = EdsDownloadComplete(file->mDirectoryItem);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to mark download as complete" << std::endl;
        goto read_cleanup;
    }

    void* data;
    error = EdsGetPointer(stream, (EdsVoid**)&data);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get pointer from stream" << std::endl;
        goto read_cleanup;
    }

    EdsUInt32 length;
    error = EdsGetLength(stream, &length);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get stream length" << std::endl;
        goto read_cleanup;
    }

    buffer = Buffer(data, length);
    surface = Surface::create(loadImage(DataSourceBuffer::create(buffer), ImageSource::Options(), "jpg"));

read_cleanup:
    if (stream) {
        EdsRelease(stream);
    }

    callback(error, surface);
}

EdsError Camera::requestStartLiveView() {
    if (!mHasOpenSession) {
        return EDS_ERR_SESSION_NOT_OPEN;
    }
    if (mIsLiveViewActive) {
        return EDS_ERR_INTERNAL_ERROR;
    }

    EdsUInt32 device;
    EdsError error = EdsGetPropertyData(mCamera, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get output device for Live View" << std::endl;
        return error;
    }

    // connect PC to Live View output device
    device |= kEdsEvfOutputDevice_PC;
    error = EdsSetPropertyData(mCamera, kEdsPropID_Evf_OutputDevice, 0 , sizeof(device), &device);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to set output device to connect PC to Live View output device" << std::endl;
        return error;
    }

    mIsLiveViewActive = true;

    return EDS_ERR_OK;
}

EdsError Camera::requestStopLiveView() {
    if (!mIsLiveViewActive) {
        return EDS_ERR_INTERNAL_ERROR;
    }

    EdsUInt32 device;
    EdsError error = EdsGetPropertyData(mCamera, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get output device for Live View" << std::endl;
        return error;
    }

    // disconnect PC from Live View output device
    device &= ~kEdsEvfOutputDevice_PC;
    error = EdsSetPropertyData(mCamera, kEdsPropID_Evf_OutputDevice, 0 , sizeof(device), &device);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to set output device to disconnect PC from Live View output device" << std::endl;
        return error;
    }

    mIsLiveViewActive = false;

    return EDS_ERR_OK;
}

void Camera::toggleLiveView() {
    if (mIsLiveViewActive) {
        requestStopLiveView();
    } else {
        requestStartLiveView();
    }
}

void Camera::requestLiveViewImage(const std::function<void(EdsError error, SurfaceRef surface)>& callback) {
    EdsError error = EDS_ERR_OK;
    EdsStreamRef stream = NULL;
    EdsEvfImageRef evfImage = NULL;
    Buffer buffer = NULL;
    SurfaceRef surface = nullptr;

    if (!mIsLiveViewActive) {
        error = EDS_ERR_INTERNAL_ERROR;
        goto cleanup;
    }

    error = EdsCreateMemoryStream(0, &stream);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to create memory stream" << std::endl;
        goto cleanup;
    }

    error = EdsCreateEvfImageRef(stream, &evfImage);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to create Evf image" << std::endl;
        goto cleanup;
    }

    error = EdsDownloadEvfImage(mCamera, evfImage);
    if (error != EDS_ERR_OK) {
        if (error == EDS_ERR_OBJECT_NOTREADY) {
            console() << "ERROR - failed to download Evf image, not ready yet" << std::endl;
        } else {
            console() << "ERROR - failed to download Evf image" << std::endl;
        }
        goto cleanup;
    }

    EdsUInt32 length;
    error = EdsGetLength(stream, &length);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get Evf image length" << std::endl;
        goto cleanup;
    }
    if (length == 0) {
        goto cleanup;
    }

    void* data;
    error = EdsGetPointer(stream, (EdsVoid**)&data);
    if (error != EDS_ERR_OK) {
        console() << "ERROR - failed to get pointer from stream" << std::endl;
        goto cleanup;
    }

    buffer = Buffer(data, length);
    surface = Surface::create(loadImage(DataSourceBuffer::create(buffer), ImageSource::Options(), "jpg"));

cleanup:
    if (stream) {
        EdsRelease(stream);
    }
    if (evfImage) {
        EdsRelease(evfImage);
    }

    callback(error, surface);
}

#pragma mark - CALLBACKS

EdsError EDSCALLBACK Camera::handleObjectEvent(EdsUInt32 event, EdsBaseRef ref, EdsVoid* context) {
    Camera* c = (Camera*)context;
    CameraRef camera = CameraBrowser::instance()->cameraForPortName(c->getPortName());
    switch (event) {
        case kEdsObjectEvent_DirItemRequestTransfer: {
            EdsDirectoryItemRef directoryItem = (EdsDirectoryItemRef)ref;
            CameraFileRef file = nullptr;
            try {
                file = CameraFile::create(directoryItem);
            } catch (...) {
                EdsRelease(directoryItem);
                break;
            }
            EdsRelease(directoryItem);
            directoryItem = NULL;
            if (camera->mFileAddedHandler) {
                camera->mFileAddedHandler(camera, file);
            }
            break;
        }
        default:
            if (ref) {
                EdsRelease(ref);
                ref = NULL;
            }
            break;
    }
    return EDS_ERR_OK;
}

EdsError EDSCALLBACK Camera::handlePropertyEvent(EdsUInt32 event, EdsUInt32 propertyID, EdsUInt32 param, EdsVoid* context) {
    if (propertyID == kEdsPropID_Evf_OutputDevice && event == kEdsPropertyEvent_PropertyChanged) {
//        console() << "output device changed, Live View possibly ready" << std::endl;
    }
    return EDS_ERR_OK;
}

EdsError EDSCALLBACK Camera::handleStateEvent(EdsUInt32 event, EdsUInt32 param, EdsVoid* context) {
    Camera* c = (Camera*)context;
    CameraRef camera = c->shared_from_this();
    switch (event) {
        case kEdsStateEvent_WillSoonShutDown:
            if (camera->mHasOpenSession && camera->mShouldKeepAlive) {
                EdsError error = EdsSendCommand(camera->mCamera, kEdsCameraCommand_ExtendShutDownTimer, 0);
                if (error != EDS_ERR_OK) {
                    console() << "ERROR - failed to extend shut down timer" << std::endl;
                }
            }
            break;
        case kEdsStateEvent_Shutdown:
            camera->requestCloseSession();
            // send handler and browser removal notices
            if (camera->mRemovedHandler) {
                camera->mRemovedHandler(camera);
            }
            CameraBrowser::instance()->removeCamera(camera);
            break;
        default:
            break;
    }
    return EDS_ERR_OK;
}

}}
