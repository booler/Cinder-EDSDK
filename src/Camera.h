//
//  Camera.h
//  Cinder-EDSDK
//
//  Created by Jean-Pierre Mouilleseaux on 08 Dec 2013.
//  Copyright 2013 Chorded Constructions. All rights reserved.
//

#pragma once

#if defined(CINDER_MAC)
    #define __MACOS__
#elif defined(CINDER_MSW) || defined(CINDER_WINRT)
    #error Target platform unsupported by Cinder-EDSDK
#else
    #error Target platform unsupported by EDSDK
#endif

#include "EDSDK.h"
#include "cinder/Cinder.h"

namespace Cinder { namespace EDSDK {

typedef std::shared_ptr<class Camera> CameraRef;

typedef std::shared_ptr<class CameraFile> CameraFileRef;

class CameraHandler {
public:
    virtual void didRemoveCamera(Camera* camera) = 0;
    virtual void didAddFile(Camera* camera, CameraFileRef file) = 0;
};

class CameraSettings {
public:
    bool getShouldKeepAlive() const {
        return mShouldKeepAlive;
    }
    void setShouldKeepAlive(bool flag) {
        mShouldKeepAlive = flag;
    }
    EdsUInt32 getPictureSaveLocation() const {
        return mPictureSaveLocation;
    }
    void setPictureSaveLocation(EdsUInt32 saveLocation) {
        mPictureSaveLocation = saveLocation;
    }

private:
    bool mShouldKeepAlive = true;
    EdsUInt32 mPictureSaveLocation = kEdsSaveTo_Host;
};

class CameraFile : public std::enable_shared_from_this<CameraFile> {
public:
    static CameraFileRef create(EdsDirectoryItemRef directoryItem);
	~CameraFile();

    std::string getName() const;
    uint32_t getSize() const;

private:
    CameraFile(EdsDirectoryItemRef directoryItem);

    EdsDirectoryItemRef mDirectoryItem;
    EdsDirectoryItemInfo mDirectoryItemInfo;

    friend class Camera;
};

class Camera : public std::enable_shared_from_this<Camera> {
public:
    static CameraRef create(EdsCameraRef camera);
	~Camera();

    CameraHandler* getHandler() const;
    void setHandler(CameraHandler* handler);

    std::string getName() const;
    std::string getPortName() const;

    bool hasOpenSession() const;
    EdsError requestOpenSession(CameraSettings* settings);
    EdsError requestCloseSession();

    EdsError requestTakePicture();
    EdsError requestDownloadFile(CameraFileRef file, ci::fs::path destinationFolderPath);
//    EdsError requestReadFile(CameraFileRef file);

private:
    Camera(EdsCameraRef camera);

    static EdsError EDSCALLBACK handleObjectEvent(EdsUInt32 inEvent, EdsBaseRef inRef, EdsVoid* inContext);
    static EdsError EDSCALLBACK handlePropertyEvent(EdsUInt32 inEvent, EdsUInt32 inPropertyID, EdsUInt32 inParam, EdsVoid* inContext);
    static EdsError EDSCALLBACK handleStateEvent(EdsUInt32 inEvent, EdsUInt32 inParam, EdsVoid* inContext);

    CameraHandler* mHandler;
    EdsCameraRef mCamera;
    EdsDeviceInfo mDeviceInfo;
    bool mHasOpenSession;
    bool mShouldKeepAlive = true;
};

}}
