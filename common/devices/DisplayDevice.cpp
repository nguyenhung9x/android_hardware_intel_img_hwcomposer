/*
 * Copyright © 2012 Intel Corporation
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Jackie Li <yaodong.li@intel.com>
 *
 */
#include <cutils/log.h>

#include <Drm.h>
#include <Hwcomposer.h>
#include <DisplayDevice.h>

namespace android {
namespace intel {

DisplayDevice::DisplayDevice(uint32_t type, Hwcomposer& hwc, DisplayPlaneManager& dpm)
    : mType(type),
      mHwc(hwc),
      mDisplayPlaneManager(dpm),
      mActiveDisplayConfig(-1),
      mVsyncControl(0),
      mBlankControl(0),
      mHotplugControl(0),
      mHotplugObserver(0),
      mVsyncObserver(0),
      mLayerList(0),
      mPrimaryPlane(0),
      mConnection(DEVICE_DISCONNECTED),
      mInitialized(false)
{
    LOGV("DisplayDevice()");

    switch (type) {
    case DEVICE_PRIMARY:
        mName = "Primary";
        break;
    case DEVICE_EXTERNAL:
        mName = "External";
        break;
    case DEVICE_VIRTUAL:
        mName = "Virtual";
        break;
    default:
        mName = "Unknown";
    }

    mDisplayConfigs.setCapacity(DEVICE_COUNT);
}

DisplayDevice::~DisplayDevice()
{
    LOGV("~DisplayDevice()");
    deinitialize();
}

void DisplayDevice::onGeometryChanged(hwc_display_contents_1_t *list)
{
    LOGD("DisplayDevice::onGeometryChanged, disp %d, %d",
         mType, list->numHwLayers);

    // NOTE: should NOT be here
    if (mLayerList) {
        LOGW("DisplayDevice::onGeometryChanged: mLayerList exists");
        delete mLayerList;
    }

    // create a new layer list
    mLayerList = new HwcLayerList(list,
                                  mDisplayPlaneManager,
                                  mPrimaryPlane,
                                  mType);
    if (!mLayerList)
        LOGW("onGeometryChanged: failed to create layer list");
}

void DisplayDevice::prePrepare(hwc_display_contents_1_t *display)
{
    LOGV("DisplayDevice::prepare");

    if (!initCheck())
        return;

    Mutex::Autolock _l(mLock);

    if (mConnection != DEVICE_CONNECTED)
        return;

    // for a null list, delete hwc list
    if (!display) {
        delete mLayerList;
        mLayerList = 0;
        return;
    }

    // check if geometry is changed, if changed delete list
    if ((display->flags & HWC_GEOMETRY_CHANGED) && mLayerList) {
        delete mLayerList;
        mLayerList = 0;
    }
}

bool DisplayDevice::prepare(hwc_display_contents_1_t *display)
{

    LOGV("DisplayDevice::prepare");

    if (!initCheck())
        return false;

    Mutex::Autolock _l(mLock);

    if (mConnection != DEVICE_CONNECTED)
        return true;

    if (!display)
        return true;

    // check if geometry is changed
    if (display->flags & HWC_GEOMETRY_CHANGED)
        onGeometryChanged(display);

    if (!mLayerList) {
        LOGE("prepare: null HWC layer list");
        return false;
    }

    // update list with new list
    return mLayerList->update(display);
}

bool DisplayDevice::vsyncControl(int enabled)
{
    bool ret;

    LOGV("vsyncControl");

    if (!initCheck())
        return false;

    //Mutex::Autolock _l(mLock);

    LOGV("DisplayDevice::vsyncControl: disp %d, enabled %d", mType, enabled);

    ret = mVsyncControl->control(mType, enabled);
    if (ret == false) {
        LOGE("DisplayDevice::vsyncControl: failed set vsync");
        return false;
    }

    mVsyncObserver->control(enabled);
    return true;
}

bool DisplayDevice::blank(int blank)
{
    bool ret;

    LOGV("blank");

    if (!initCheck())
        return false;

    //Mutex::Autolock _l(mLock);

    if (mConnection != DEVICE_CONNECTED)
        return false;

    ret = mBlankControl->blank(mType, blank);
    if (ret == false) {
        LOGE("DisplayDevice::blank: failed to blank device");
        return false;
    }

    return true;
}

bool DisplayDevice::getDisplayConfigs(uint32_t *configs,
                                         size_t *numConfigs)
{
    LOGV("getDisplayConfigs");

    if (!initCheck())
        return false;

    //Mutex::Autolock _l(mLock);

    if (mConnection != DEVICE_CONNECTED)
        return false;

    if (!configs || !numConfigs) {
        LOGE("getDisplayConfigs: invalid parameters");
        return false;
    }

    *configs = 0;
    *numConfigs = mDisplayConfigs.size();

    return true;
}

bool DisplayDevice::getDisplayAttributes(uint32_t configs,
                                            const uint32_t *attributes,
                                            int32_t *values)
{
    LOGV("getDisplayAttributes");

    if (!initCheck())
        return false;

    //Mutex::Autolock _l(mLock);

    if (mConnection != DEVICE_CONNECTED)
        return false;

    if (!attributes || !values) {
        LOGE("getDisplayAttributes: invalid parameters");
        return false;
    }

    DisplayConfig *config = mDisplayConfigs.itemAt(mActiveDisplayConfig);
    if  (!config) {
        LOGE("getDisplayAttributes: failed to get display config");
        return false;
    }

    int i = 0;
    while (attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE) {
        switch (attributes[i]) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            values[i] = 1e9 / config->getRefreshRate();
            break;
        case HWC_DISPLAY_WIDTH:
            values[i] = config->getWidth();
            break;
        case HWC_DISPLAY_HEIGHT:
            values[i] = config->getHeight();
            break;
        case HWC_DISPLAY_DPI_X:
            values[i] = config->getDpiX() * 1000.0f;
            break;
        case HWC_DISPLAY_DPI_Y:
            values[i] = config->getDpiY() * 1000.0f;
            break;
        default:
            LOGE("getDisplayAttributes: unknown attribute %d", attributes[i]);
            break;
        }
        i++;
    }

    return true;
}

bool DisplayDevice::compositionComplete()
{
    LOGV("compositionComplete");
    // do nothing by default
    return true;
}

void DisplayDevice::removeDisplayConfigs()
{
    for (size_t i = 0; i < mDisplayConfigs.size(); i++) {
        DisplayConfig *config = mDisplayConfigs.itemAt(i);
        delete config;
    }

    mDisplayConfigs.clear();
    mActiveDisplayConfig = -1;
}

bool DisplayDevice::updateDisplayConfigs(struct Output *output)
{
    drmModeConnectorPtr drmConnector;
    drmModeCrtcPtr drmCrtc;
    drmModeModeInfoPtr drmMode;
    drmModeModeInfoPtr drmPreferredMode;
    drmModeFBPtr drmFb;
    int drmModeCount;
    float physWidthInch;
    float physHeightInch;
    int dpiX, dpiY;

    LOGD("updateDisplayConfigs()");

    Mutex::Autolock _l(mLock);

    drmConnector = output->connector;
    if (!drmConnector) {
        LOGE("DisplayDevice::updateDisplayConfigs:Output has no connector");
        return false;
    }

    physWidthInch = (float)drmConnector->mmWidth * 0.039370f;
    physHeightInch = (float)drmConnector->mmHeight * 0.039370f;

    drmModeCount = drmConnector->count_modes;
    drmPreferredMode = 0;

    // reset display configs
    removeDisplayConfigs();

    // reset the number of display configs
    mDisplayConfigs.setCapacity(drmModeCount + 1);

    LOGV("updateDisplayConfigs: mode count %d", drmModeCount);

    // find preferred mode of this display device
    for (int i = 0; i < drmModeCount; i++) {
        drmMode = &drmConnector->modes[i];
        dpiX = drmMode->hdisplay / physWidthInch;
        dpiY = drmMode->vdisplay / physHeightInch;

        if ((drmMode->type & DRM_MODE_TYPE_PREFERRED)) {
            drmPreferredMode = drmMode;
            continue;
        }

        LOGV("updateDisplayConfigs: adding new config %dx%d %d\n",
              drmMode->hdisplay,
              drmMode->vdisplay,
              drmMode->vrefresh);

        // if not preferred mode add it to display configs
        DisplayConfig *config = new DisplayConfig(drmMode->vrefresh,
                                                  drmMode->hdisplay,
                                                  drmMode->vdisplay,
                                                  dpiX, dpiY);
        mDisplayConfigs.add(config);
    }

    // update device connection status
    mConnection = (output->connected) ? DEVICE_CONNECTED : DEVICE_DISCONNECTED;

    // if device is connected, continue checking current mode
    if (mConnection != DEVICE_CONNECTED)
        goto use_preferred_mode;
    else if (mConnection == DEVICE_CONNECTED) {
        // use active fb and mode
        drmCrtc = output->crtc;
        drmFb = output->fb;
        if (!drmCrtc || !drmFb) {
            LOGE("updateDisplayConfigs: impossible");
            goto use_preferred_mode;
        }

        drmMode = &drmCrtc->mode;
        if (!drmCrtc->mode_valid)
            goto use_preferred_mode;

        LOGV("updateDisplayConfigs: using current mode %dx%d %d\n",
              drmMode->hdisplay,
              drmMode->vdisplay,
              drmMode->vrefresh);

        // use current drm mode, likely it's preferred mode
        dpiX = drmMode->hdisplay / physWidthInch;
        dpiY = drmMode->vdisplay / physHeightInch;
        // use active fb dimension as config width/height
        DisplayConfig *config = new DisplayConfig(drmMode->vrefresh,
                                                  //drmFb->width,
                                                  //drmFb->height,
                                                  drmMode->hdisplay,
                                                  drmMode->vdisplay,
                                                  dpiX, dpiY);
        // add it to the front of other configs
        mDisplayConfigs.push_front(config);
    }

    // init the active display config
    mActiveDisplayConfig = 0;

    return true;
use_preferred_mode:
    if (drmPreferredMode) {
        dpiX = drmPreferredMode->hdisplay / physWidthInch;
        dpiY = drmPreferredMode->vdisplay / physHeightInch;
        DisplayConfig *config = new DisplayConfig(drmPreferredMode->vrefresh,
                                                  drmPreferredMode->hdisplay,
                                                  drmPreferredMode->vdisplay,
                                                  dpiX, dpiY);
        if (!config) {
            LOGE("updateDisplayConfigs: failed to allocate display config");
            return false;
        }

        LOGV("updateDisplayConfigs: using preferred mode %dx%d %d\n",
              drmPreferredMode->hdisplay,
              drmPreferredMode->vdisplay,
              drmPreferredMode->vrefresh);

        // add it to the front of other configs
        mDisplayConfigs.push_front(config);
    }

    // init the active display config
    mActiveDisplayConfig = 0;
    return true;
}

bool DisplayDevice::detectDisplayConfigs()
{
    int outputIndex = -1;
    struct Output *output;
    bool ret;
    Drm *drm = Hwcomposer::getInstance().getDrm();

    LOGD("detectDisplayConfigs");

    if (!drm) {
        LOGE("detectDisplayConfigs: failed to get drm");
        return false;
    }

    // detect drm objects
    switch (mType) {
    case DEVICE_PRIMARY:
        outputIndex = Drm::OUTPUT_PRIMARY;
        break;
    case DEVICE_EXTERNAL:
        outputIndex = Drm::OUTPUT_EXTERNAL;
        break;
    case DEVICE_VIRTUAL:
        return true;
    default:
        LOGE("detectDisplayConfigs: invalid display device");
        return false;
    }

    if (outputIndex < 0) {
        LOGW("detectDisplayConfigs(): failed to detect Drm objects");
        return false;
    }

    // detect
    ret = drm->detect();
    if (ret == false) {
        LOGE("detectDisplayConfigs(): Drm detection failed");
        return false;
    }

    // get output
    output = drm->getOutput(outputIndex);
    if (!output) {
        LOGE("detectDisplayConfigs(): failed to get output");
        return false;
    }

    // update display configs
    return updateDisplayConfigs(output);
}

bool DisplayDevice::initialize()
{
    bool ret;

    LOGV("DisplayDevice::initialize");

    // detect display configs
    ret = detectDisplayConfigs();
    if (ret == false) {
        LOGE("initialize(): failed to detect display config");
        return false;
    }

    // get primary plane of this device
    mPrimaryPlane = mDisplayPlaneManager.getPrimaryPlane(mType);
    if (!mPrimaryPlane) {
        LOGE("initialize(): failed to get primary plane");
        goto init_err;
    }

    // create vsync control
    mVsyncControl = createVsyncControl();
    if (!mVsyncControl) {
        LOGE("initialize(): failed to create vsync control");
        goto init_err;
    }

    // create blank control
    mBlankControl = createBlankControl();
    if (!mBlankControl) {
        LOGE("initialize(): failed to create blank control");
        goto init_err;
    }

    // create hotplug control/observer for external display
    if (mType == DEVICE_EXTERNAL) {
        mHotplugControl = createHotplugControl();
        if (!mHotplugControl) {
            LOGE("initialize(): failed to create hotplug control");
            goto init_err;
        }

        // create hotplug observer
        mHotplugObserver = new HotplugEventObserver(*this, *mHotplugControl);
        if (!mHotplugObserver.get()) {
            LOGE("initialize(): failed to create hotplug observer");
            goto init_err;
        }
    }

    // create vsync event observer
    mVsyncObserver = new VsyncEventObserver(*this, *mVsyncControl);
    if (!mVsyncObserver.get()) {
        LOGE("initialize(): failed to create vsync observer");
        goto init_err;
    }

    mInitialized = true;
    return true;
init_err:
    deinitialize();
    return false;
}

void DisplayDevice::deinitialize()
{
    // destroy vsync event observer
    if (mVsyncObserver.get()) {
        mVsyncObserver->requestExit();
        mVsyncObserver = 0;
    }

    // destroy hotplug event observer
    if (mHotplugObserver.get()) {
        mHotplugObserver->requestExit();
        mHotplugObserver = 0;
    }

    // destroy hotplug control
    if (mHotplugControl) {
        delete mHotplugControl;
        mHotplugControl = 0;
    }

    // destroy blank control
    if (mBlankControl) {
        delete mBlankControl;
        mBlankControl = 0;
    }

    // destroy vsync control
    if (mVsyncControl) {
        delete mVsyncControl;
        mVsyncControl = 0;
    }

    // remove configs
    removeDisplayConfigs();

    mInitialized = false;
}

bool DisplayDevice::isConnected() const
{
    if (!initCheck())
        return false;

    return (mConnection == DEVICE_CONNECTED) ? true : false;
}

const char* DisplayDevice::getName() const
{
    return mName;
}

int DisplayDevice::getType() const
{
    return mType;
}

void DisplayDevice::onHotplug()
{
    bool ret;

    LOGD("DisplayDevice::onHotplug");

    // detect display configs
    ret = detectDisplayConfigs();
    if (ret == false) {
        LOGD("DisplayDevice::onHotplug: failed to detect display config");
        return;
    }

    {   // lock scope
        Mutex::Autolock _l(mLock);
        // delete device layer list
        if (!mConnection && mLayerList){
            delete mLayerList;
            mLayerList = 0;
        }
    }
    // notify hwcomposer
    mHwc.hotplug(mType, mConnection);
}

void DisplayDevice::onVsync(int64_t timestamp)
{
    LOGV("DisplayDevice::timestamp");

    if (!initCheck())
        return;

    //Mutex::Autolock _l(mLock);

    if (mConnection != DEVICE_CONNECTED)
        return;

    // notify hwc
    mHwc.vsync(mType, timestamp);
}

void DisplayDevice::dump(Dump& d)
{
    d.append("-------------------------------------------------------------\n");
    d.append("Device Name: %s (%s)\n", mName,
            mConnection ? "connected" : "disconnected");
    d.append("Display configs (count = %d):\n", mDisplayConfigs.size());
    d.append(" CONFIG | VSYNC_PERIOD | WIDTH | HEIGHT | DPI_X | DPI_Y \n");
    d.append("--------+--------------+-------+--------+-------+-------\n");
    for (size_t i = 0; i < mDisplayConfigs.size(); i++) {
        DisplayConfig *config = mDisplayConfigs.itemAt(i);
        if (config) {
            d.append("%s %2d   |     %4d     | %5d |  %4d  |  %3d  |  %3d  \n",
                     (i == (size_t)mActiveDisplayConfig) ? "* " : "  ",
                     i,
                     config->getRefreshRate(),
                     config->getWidth(),
                     config->getHeight(),
                     config->getDpiX(),
                     config->getDpiY());
        }
    }
    // dump layer list
    if (mLayerList)
        mLayerList->dump(d);
}

} // namespace intel
} // namespace android
