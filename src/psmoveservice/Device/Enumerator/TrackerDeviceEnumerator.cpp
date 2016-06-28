// -- includes -----
#include "TrackerDeviceEnumerator.h"
#include "ServerUtility.h"
#include "USBAsyncRequestManager.h"
#include "assert.h"
#include "libusb.h"
#include "string.h"

// -- private definitions -----
#ifdef _MSC_VER
#pragma warning (disable: 4996) // 'This function or variable may be unsafe': snprintf
#define snprintf _snprintf
#endif

// -- macros ----
#define MAX_CAMERA_TYPE_INDEX               GET_DEVICE_TYPE_INDEX(CommonDeviceState::SUPPORTED_CAMERA_TYPE_COUNT)

// -- globals -----
// NOTE: This list must match the tracker order in CommonDeviceState::eDeviceType
USBDeviceInfo k_supported_tracker_infos[MAX_CAMERA_TYPE_INDEX] = {
    { 0x1415, 0x2000 }, // PS3Eye
    //{ 0x05a9, 0x058a }, // PS4 Camera - TODO
    //{ 0x045e, 0x02ae }, // V1 Kinect - TODO
};

// -- private prototypes -----
static bool get_usb_tracker_type(t_usb_device_handle usb_device_handle, CommonDeviceState::eDeviceType &out_device_type);

// -- methods -----
TrackerDeviceEnumerator::TrackerDeviceEnumerator()
    : m_USBDeviceHandle(k_invalid_usb_device_handle)
    , m_cameraIndex(0)
{
    USBAsyncRequestManager *usbRequestMgr = USBAsyncRequestManager::getInstance();

    assert(m_deviceType >= 0 && GET_DEVICE_TYPE_INDEX(m_deviceType) < MAX_CAMERA_TYPE_INDEX);
    m_USBDeviceHandle = usbRequestMgr->getFirstUSBDeviceHandle();

    // If the first USB device handle isn't a tracker, move on to the next device
    if (get_usb_tracker_type(m_USBDeviceHandle, m_deviceType))
    {
        // Cache the current usb path
        usbRequestMgr->getUSBDevicePath(m_USBDeviceHandle, m_currentUSBPath, sizeof(m_currentUSBPath));
    }
    else
    {
        next();
    }
}

const char *TrackerDeviceEnumerator::get_path() const
{
    const char *result = nullptr;

    if (is_valid())
    {
        // Return a pointer to our member variable that has the path cached
        result= m_currentUSBPath;
    }

    return result;
}

bool TrackerDeviceEnumerator::is_valid() const
{
    return m_USBDeviceHandle != k_invalid_usb_device_handle;
}

bool TrackerDeviceEnumerator::next()
{
    USBAsyncRequestManager *usbRequestMgr= USBAsyncRequestManager::getInstance();
    bool foundValid = false;

    while (is_valid() && !foundValid)
    {
        m_USBDeviceHandle = usbRequestMgr->getNextUSBDeviceHandle(m_USBDeviceHandle);

        if (is_valid() && get_usb_tracker_type(m_USBDeviceHandle, m_deviceType))
        {
            // Cache the path to the device
            usbRequestMgr->getUSBDevicePath(m_USBDeviceHandle, m_currentUSBPath, sizeof(m_currentUSBPath));
            foundValid= true;
            break;
        }
    }

    if (foundValid)
    {
        ++m_cameraIndex;
    }

    return foundValid;
}

//-- private methods -----
static bool get_usb_tracker_type(t_usb_device_handle usb_device_handle, CommonDeviceState::eDeviceType &out_device_type)
{
    USBDeviceInfo devInfo;
    bool bIsValidDevice = false;

    if (USBAsyncRequestManager::getInstance()->getUSBDeviceInfo(usb_device_handle, devInfo))
    {
        // See if the next filtered device is a camera that we care about
        for (int tracker_type_index = 0; tracker_type_index < MAX_CAMERA_TYPE_INDEX; ++tracker_type_index)
        {
            const USBDeviceInfo &supported_type = k_supported_tracker_infos[tracker_type_index];

            if (devInfo.product_id == supported_type.product_id &&
                devInfo.vendor_id == supported_type.vendor_id)
            {
                out_device_type = static_cast<CommonDeviceState::eDeviceType>(CommonDeviceState::TrackingCamera + tracker_type_index);
                bIsValidDevice = true;
                break;
            }
        }
    }

    return bIsValidDevice;
}