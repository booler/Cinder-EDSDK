
#include "cinder/app/AppNative.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"

#include "Cinder-EDSDK.h"

using namespace ci;
using namespace ci::app;
using namespace Cinder::EDSDK;
using namespace std;

class SimpleTetherApp : public AppNative {
public:
    void prepareSettings(Settings* settings);
    void setup();
    void keyDown(KeyEvent event);
    void update();
    void draw();

    void browserDidAddCamera(CameraRef camera);
    void browserDidRemoveCamera(CameraRef camera);
    void browserDidEnumerateCameras();

    void didRemoveCamera(CameraRef camera);
    void didAddFile(CameraRef camera, CameraFileRef file);

private:
    CameraRef mCamera;
    gl::TextureRef mPhotoTexture;
};

void SimpleTetherApp::prepareSettings(Settings* settings) {
    settings->enableHighDensityDisplay();
    settings->setWindowSize(640, 480);
}

void SimpleTetherApp::setup() {
    CameraBrowser::instance()->connectAddedHandler(&SimpleTetherApp::browserDidAddCamera, this);
    CameraBrowser::instance()->connectRemovedHandler(&SimpleTetherApp::browserDidRemoveCamera, this);
    CameraBrowser::instance()->connectEnumeratedHandler(&SimpleTetherApp::browserDidEnumerateCameras, this);
    CameraBrowser::instance()->start();
}

void SimpleTetherApp::keyDown(KeyEvent event) {
    switch (event.getCode()) {
        case app::KeyEvent::KEY_l:
            if (mCamera != NULL && mCamera->hasOpenSession()) {
                mCamera->toggleLiveView();
            }
            break;
        case app::KeyEvent::KEY_SPACE:
            if (mCamera != NULL && mCamera->hasOpenSession()) {
                mCamera->requestTakePicture();
            }
            break;
        case app::KeyEvent::KEY_ESCAPE:
            quit();
            break;
        default:
            break;
    }
}

void SimpleTetherApp::update() {
    if (mCamera != NULL && mCamera->hasOpenSession() && mCamera->isLiveViewActive()) {
        mCamera->requestLiveViewImage([&](EdsError error, ci::Surface8u surface) {
            if (error == EDS_ERR_OK) {
                mPhotoTexture = gl::Texture::create(surface);
            }
        });
    }
}

void SimpleTetherApp::draw() {
	gl::clear(Color::black());

    if (mPhotoTexture != NULL) {
        gl::color(Color::white());
        Rectf r = Rectf(ivec2(0), mPhotoTexture->getSize()).getCenteredFill(Rectf(ivec2(0), getWindowSize()), true);
        gl::draw(mPhotoTexture, r);
    }
}

#pragma mark - CAMERA BROWSER

void SimpleTetherApp::browserDidAddCamera(CameraRef camera) {
    console() << "added a camera: " << camera->getName() << std::endl;
    if (mCamera != NULL) {
        return;
    }

    mCamera = camera;
    mCamera->connectRemovedHandler(&SimpleTetherApp::didRemoveCamera, this);
    mCamera->connectFileAddedHandler(&SimpleTetherApp::didAddFile, this);
    console() << "grabbing camera: " << camera->getName() << std::endl;

    Cinder::EDSDK::Camera::Settings settings = Cinder::EDSDK::Camera::Settings();
//    settings.setPictureSaveLocation(kEdsSaveTo_Both);
//    settings.setShouldKeepAlive(false);
    EdsError error = mCamera->requestOpenSession(settings);
    if (error == EDS_ERR_OK) {
        console() << "session opened" << std::endl;
    }
}

void SimpleTetherApp::browserDidRemoveCamera(CameraRef camera) {
    // NB - somewhat redundant as the camera will notify handler first
    console() << "removed a camera: " << camera->getName() << std::endl;
    if (camera != mCamera) {
        return;
    }

    console() << "our camera was disconnected" << std::endl;
    mCamera = NULL;
}

void SimpleTetherApp::browserDidEnumerateCameras() {
    console() << "enumerated cameras" << std::endl;
}

#pragma mark - CAMERA

void SimpleTetherApp::didRemoveCamera(CameraRef camera) {
    console() << "removed a camera: " << camera->getName() << std::endl;
    if (camera != mCamera) {
        return;
    }

    console() << "our camera was disconnected" << std::endl;
    mCamera = NULL;
}

void SimpleTetherApp::didAddFile(CameraRef camera, CameraFileRef file) {
//    fs::path destinationFolderPath = expandPath(fs::path("~/Desktop/Captures"));
//    camera->requestDownloadFile(file, destinationFolderPath, [&](EdsError error, ci::fs::path outputFilePath) {
//        if (error == EDS_ERR_OK) {
//            console() << "image downloaded to '" << outputFilePath << "'" << std::endl;
//        }
//    });

    camera->requestReadFile(file, [&](EdsError error, ci::Surface surface) {
        if (error == EDS_ERR_OK) {
            mPhotoTexture = gl::Texture::create(surface);
        }
    });
}

CINDER_APP_NATIVE(SimpleTetherApp, RendererGl)
