/*
 This file is largely reproduced from PS3EYEDriver(https://github.com/inspirit/PS3EYEDriver).
 We adapted the library to work with a shared USBManager in the service.
 The license for PS3EYEDriver is reproduced below:

 <license>
 License information for PS3EYEDriver
 ------------------------------------
 
 The license of the PS3EYEDriver is MIT (for newly-written code) and GPLv2 for
 all code derived from the Linux Kernel Driver (ov534) sources.
 
 In https://github.com/inspirit/PS3EYEDriver/pull/3, Eugene Zatepyakin writes:
 
 "all of my code is MIT licensed and to tell the truth i didnt check Linux
 p3eye version license. as far as i know it was contributed to Linux by some
 devs who decided to do it on their own..."
 
 The code is based on the Linux driver for the PSEye, which can be found here:
 
 http://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/drivers/media/usb/gspca/ov534.c
  
 ov534-ov7xxx gspca driver
 
 Copyright (C) 2008 Antonio Ospite <ospite@studenti.unina.it>
 Copyright (C) 2008 Jim Paris <jim@jtan.com>
 Copyright (C) 2009 Jean-Francois Moine http://moinejf.free.fr
 
 Based on a prototype written by Mark Ferrell <majortrips@gmail.com>
 USB protocol reverse engineered by Jim Paris <jim@jtan.com>
 https://jim.sh/svn/jim/devl/playstation/ps3/eye/test/
 
 PS3 Eye camera enhanced by Richard Kaswy http://kaswy.free.fr
 PS3 Eye camera - brightness, contrast, awb, agc, aec controls
                  added by Max Thrun <bear24rw@gmail.com>
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 </license>
*/

//-- includes -----
#include "PS3EyeLibUSBCapture.h"
#include "ServerLog.h"
#include "ServerUtility.h"
#include "USBDeviceManager.h"

#include "libusb.h"

#include <iomanip>
#include <string>
#include <deque>

#include "async/async.hpp"

//-- constants -----
#define TRANSFER_SIZE		16384
#define NUM_TRANSFERS		8

#define OV534_REG_ADDRESS	0xf1	/* sensor address */
#define OV534_REG_SUBADDR	0xf2
#define OV534_REG_WRITE		0xf3
#define OV534_REG_READ		0xf4
#define OV534_REG_OPERATION	0xf5
#define OV534_REG_STATUS	0xf6

#define OV534_OP_WRITE_3	0x37
#define OV534_OP_WRITE_2	0x33
#define OV534_OP_READ_2		0xf9

#define CTRL_TIMEOUT 500
#define VGA	 0
#define QVGA 1


//-- data -----
static const uint8_t ov534_reg_initdata[][2] = {
    { 0xe7, 0x3a },

    { OV534_REG_ADDRESS, 0x42 }, /* select OV772x sensor */

    { 0xc2, 0x0c },
    { 0x88, 0xf8 },
    { 0xc3, 0x69 },
    { 0x89, 0xff },
    { 0x76, 0x03 },
    { 0x92, 0x01 },
    { 0x93, 0x18 },
    { 0x94, 0x10 },
    { 0x95, 0x10 },
    { 0xe2, 0x00 },
    { 0xe7, 0x3e },

    { 0x96, 0x00 },

    { 0x97, 0x20 },
    { 0x97, 0x20 },
    { 0x97, 0x20 },
    { 0x97, 0x0a },
    { 0x97, 0x3f },
    { 0x97, 0x4a },
    { 0x97, 0x20 },
    { 0x97, 0x15 },
    { 0x97, 0x0b },

    { 0x8e, 0x40 },
    { 0x1f, 0x81 },
    { 0x34, 0x05 },
    { 0xe3, 0x04 },
    { 0x88, 0x00 },
    { 0x89, 0x00 },
    { 0x76, 0x00 },
    { 0xe7, 0x2e },
    { 0x31, 0xf9 },
    { 0x25, 0x42 },
    { 0x21, 0xf0 },

    { 0x1c, 0x00 },
    { 0x1d, 0x40 },
    { 0x1d, 0x02 }, /* payload size 0x0200 * 4 = 2048 bytes */
    { 0x1d, 0x00 }, /* payload size */

    // -------------

    //	{ 0x1d, 0x01 },/* frame size */		// kwasy
    //	{ 0x1d, 0x4b },/* frame size */
    //	{ 0x1d, 0x00 }, /* frame size */


    //	{ 0x1d, 0x02 },/* frame size */		// macam
    //	{ 0x1d, 0x57 },/* frame size */
    //	{ 0x1d, 0xff }, /* frame size */

    { 0x1d, 0x02 },/* frame size */		// jfrancois / linuxtv.org/hg/v4l-dvb
    { 0x1d, 0x58 },/* frame size */
    { 0x1d, 0x00 }, /* frame size */

    // ---------

    { 0x1c, 0x0a },
    { 0x1d, 0x08 }, /* turn on UVC header */
    { 0x1d, 0x0e }, /* .. */

    { 0x8d, 0x1c },
    { 0x8e, 0x80 },
    { 0xe5, 0x04 },

    // ----------------
    //	{ 0xc0, 0x28 },//	kwasy / macam
    //	{ 0xc1, 0x1e },//

    { 0xc0, 0x50 },		// jfrancois
    { 0xc1, 0x3c },
    { 0xc2, 0x0c },
};

static const uint8_t ov772x_reg_initdata[][2] = {
    { 0x12, 0x80 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },
    { 0x11, 0x01 },

    { 0x3d, 0x03 },
    { 0x17, 0x26 },
    { 0x18, 0xa0 },
    { 0x19, 0x07 },
    { 0x1a, 0xf0 },
    { 0x32, 0x00 },
    { 0x29, 0xa0 },
    { 0x2c, 0xf0 },
    { 0x65, 0x20 },
    { 0x11, 0x01 },
    { 0x42, 0x7f },
    { 0x63, 0xAA }, 	// AWB
    { 0x64, 0xff },
    { 0x66, 0x00 },
    { 0x13, 0xf0 },	// COM8  - jfrancois 0xf0	orig x0f7
    { 0x0d, 0x41 },
    { 0x0f, 0xc5 },
    { 0x14, 0x11 },

    { 0x22, 0x7f },
    { 0x23, 0x03 },
    { 0x24, 0x40 },
    { 0x25, 0x30 },
    { 0x26, 0xa1 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x6b, 0xaa },
    { 0x13, 0xff },	// COM8 - jfrancois 0xff orig 0xf7

    { 0x90, 0x05 },
    { 0x91, 0x01 },
    { 0x92, 0x03 },
    { 0x93, 0x00 },
    { 0x94, 0x60 },
    { 0x95, 0x3c },
    { 0x96, 0x24 },
    { 0x97, 0x1e },
    { 0x98, 0x62 },
    { 0x99, 0x80 },
    { 0x9a, 0x1e },
    { 0x9b, 0x08 },
    { 0x9c, 0x20 },
    { 0x9e, 0x81 },

    { 0xa6, 0x04 },
    { 0x7e, 0x0c },
    { 0x7f, 0x16 },
    { 0x80, 0x2a },
    { 0x81, 0x4e },
    { 0x82, 0x61 },
    { 0x83, 0x6f },
    { 0x84, 0x7b },
    { 0x85, 0x86 },
    { 0x86, 0x8e },
    { 0x87, 0x97 },
    { 0x88, 0xa4 },
    { 0x89, 0xaf },
    { 0x8a, 0xc5 },
    { 0x8b, 0xd7 },
    { 0x8c, 0xe8 },
    { 0x8d, 0x20 },

    { 0x0c, 0x90 },

    { 0x2b, 0x00 },
    { 0x22, 0x7f },
    { 0x23, 0x03 },
    { 0x11, 0x01 },
    { 0x0c, 0xd0 },
    { 0x64, 0xff },
    { 0x0d, 0x41 },

    { 0x14, 0x41 },
    { 0x0e, 0xcd },
    { 0xac, 0xbf },
    { 0x8e, 0x00 },	// De-noise threshold - jfrancois 0x00 - orig 0x04
    { 0x0c, 0xd0 }

};

static const uint8_t bridge_start_vga[][2] = {
    { 0x1c, 0x00 },
    { 0x1d, 0x40 },
    { 0x1d, 0x02 },
    { 0x1d, 0x00 },
    { 0x1d, 0x02 },
    { 0x1d, 0x58 },
    { 0x1d, 0x00 },
    { 0xc0, 0x50 },
    { 0xc1, 0x3c },
};
static const uint8_t sensor_start_vga[][2] = {
    { 0x12, 0x00 },
    { 0x17, 0x26 },
    { 0x18, 0xa0 },
    { 0x19, 0x07 },
    { 0x1a, 0xf0 },
    { 0x29, 0xa0 },
    { 0x2c, 0xf0 },
    { 0x65, 0x20 },
};
static const uint8_t bridge_start_qvga[][2] = {
    { 0x1c, 0x00 },
    { 0x1d, 0x40 },
    { 0x1d, 0x02 },
    { 0x1d, 0x00 },
    { 0x1d, 0x01 },
    { 0x1d, 0x4b },
    { 0x1d, 0x00 },
    { 0xc0, 0x28 },
    { 0xc1, 0x1e },
};
static const uint8_t sensor_start_qvga[][2] = {
    { 0x12, 0x40 },
    { 0x17, 0x3f },
    { 0x18, 0x50 },
    { 0x19, 0x03 },
    { 0x1a, 0x78 },
    { 0x29, 0x50 },
    { 0x2c, 0x78 },
    { 0x65, 0x2f },
};

//-- private methods -----
static void async_set_autogain(t_usb_device_handle device_handle, bool bAutoGain, uint8_t gain, uint8_t exposure, async::TaskCallback<int> outCallback);
static void async_set_auto_white_balance(t_usb_device_handle device_handle, bool bAutoWhiteBalance, async::TaskCallback<int> outCallback);
static void async_set_gain(t_usb_device_handle handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_exposure(t_usb_device_handle handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_sharpness(t_usb_device_handle device_handle,unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_contrast(t_usb_device_handle device_handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_brightness(t_usb_device_handle device_handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_hue(t_usb_device_handle device_handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_red_balance(t_usb_device_handle device_handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_green_balance(t_usb_device_handle device_handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_blue_balance(t_usb_device_handle device_handle, unsigned char val, async::TaskCallback<int> outCallback);
static void async_set_flip(t_usb_device_handle device_handle, bool horizontal, bool vertical, async::TaskCallback<int> outCallback);
static uint8_t async_set_frame_rate(t_usb_device_handle device_handle, uint32_t frame_width, uint8_t frame_rate, bool dry_run, async::TaskCallback<int> outCallback);

static void async_sccb_reg_write(t_usb_device_handle device_handle, uint8_t reg, uint8_t val, async::TaskCallback<int> outCallback);
static void async_sccb_reg_write_array(t_usb_device_handle device_handle, const uint8_t (*sequence)[2], int sequenceLength, async::TaskCallback<int> outCallback);
static void async_sccb_reg_read(t_usb_device_handle device_handle, uint16_t reg, async::TaskCallback<int> outCallback);
static void async_sccb_check_status(t_usb_device_handle device_handle, async::TaskCallback<int> checkStatusCallback);

static void async_ov534_reg_write(t_usb_device_handle device_handle, uint16_t reg, uint8_t val, async::TaskCallback<int> callback);
static void async_ov534_reg_write_array(t_usb_device_handle device_handle, const uint8_t (*sequence)[2], int sequenceLength, async::TaskCallback<int> outCallback);
static void async_ov534_reg_read(t_usb_device_handle device_handle, uint16_t reg, async::TaskCallback<int> callback);
static void async_ov534_set_led(t_usb_device_handle device_handle, bool bLedOn, async::TaskCallback<int> outCallback);

static void log_usb_result_code(const char *function_name, eUSBResultCode result_code);

//-- public interface -----
struct USBAsyncTask
{
    std::string task_name;
    async::Task<int> task_func;
};

class USBAsyncTaskQueue
{
public:
    USBAsyncTaskQueue() 
        : m_taskQueue()
    {}

    void addAsyncTask(const std::string &task_name, async::Task<int> task_func)
    {
        USBAsyncTask task= {task_name, task_func};
         
        m_taskQueue.push_back(task);

        // If this is the only task, go ahead and start it because there
        // won't be an older task compeleting to start this new one
        if (m_taskQueue.size() == 1)
        {
            startNextTask();
        }
    }

    void startNextTask()
    {
        if (m_taskQueue.size() > 0)
        {
            // Pop the next task in the list
            USBAsyncTask &task= m_taskQueue.front();
            m_taskQueue.pop_front();

            async::TaskCallback<int> onTaskCompleted = [this, task](async::ErrorCode error, int result) {
                if (error == async::FAIL)
                {
                    SERVER_LOG_ERROR("PSEYECaptureCAM_LibUSB::startNextTask") 
                        << "Camera Async Task(" << task.task_name 
                        << ") failed with code: " << result;
                }

                startNextTask();
            };

            task.task_func(onTaskCompleted);
        }
    }

    std::deque<USBAsyncTask> m_taskQueue;
};

//-- PS3EyeLibUSBCapture -----
PS3EyeLibUSBCapture::PS3EyeLibUSBCapture(t_usb_device_handle usb_device_handle)
    : m_autogain(false)
    , m_gain(20)
    , m_exposure(120)
    , m_sharpness(0)
    , m_hue(143)
    , m_awb(false)
    , m_brightness(20)
    , m_contrast(37)
    , m_blueBalance(128)
    , m_redBalance(128)
    , m_greenBalance(128)
    , m_flip_h(false)
    , m_flip_v(false)
    , m_is_streaming(false)
    , m_frame_width(0)
    , m_frame_height(0)
    , m_frame_stride(0)
    , m_frame_rate(0)
    , m_last_qued_frame_time(0.0)
    , m_usb_device_handle(usb_device_handle)
    , m_task_queue(new USBAsyncTaskQueue())
{
    m_libusb_thread_frame_buffers[0] = nullptr;
    m_libusb_thread_frame_buffers[1] = nullptr;
    m_main_thread_frame_buffer= nullptr;
}

PS3EyeLibUSBCapture::~PS3EyeLibUSBCapture()
{
    stop();
    release();

    delete m_task_queue;
}


bool PS3EyeLibUSBCapture::init(
    unsigned int width, 
    unsigned int height,
    unsigned int desiredFrameRate)
{
    bool bSuccess= false;

    return bSuccess;
}

void PS3EyeLibUSBCapture::release()
{
    if(m_usb_device_handle != k_invalid_usb_device_handle)
    {
        //close_usb();
    }

    if (m_libusb_thread_frame_buffers[0] != nullptr)
    {
        delete[] m_libusb_thread_frame_buffers[0];
        m_libusb_thread_frame_buffers[0]= nullptr;
    } 

    if (m_libusb_thread_frame_buffers[1] != nullptr)
    {
        delete[] m_libusb_thread_frame_buffers[1];
        m_libusb_thread_frame_buffers[1]= nullptr;
    } 

    if (m_main_thread_frame_buffer != nullptr)
    {
        delete[] m_main_thread_frame_buffer;
        m_main_thread_frame_buffer= nullptr;
    }
}

void PS3EyeLibUSBCapture::start()
{

}

void PS3EyeLibUSBCapture::stop()
{

}

void PS3EyeLibUSBCapture::setAutogain(bool bAutoGain)
{
    // Cache the new auto-gain flag (the camera will converge to this)
    m_autogain = bAutoGain;

    // Add an async task to set the autogain on the camera
    m_task_queue->addAsyncTask(
        "setAutogain", 
        [this, bAutoGain](async::TaskCallback<int> callback) {
            async_set_autogain(m_usb_device_handle, bAutoGain, m_gain, m_exposure, callback);
        });
}

void PS3EyeLibUSBCapture::setAutoWhiteBalance(bool val)
{
    // Cache the new AWB value (the camera will converge to this)
    m_awb = val;

    // Add an async task to set the awb on the camera
    m_task_queue->addAsyncTask(
        "setAutoWhiteBalance", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_auto_white_balance(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setGain(unsigned char val)
{
    // Cache the new gain value (the camera will converge to this)
    m_gain = val;

    // Add an async task to set the gain on the camera
    m_task_queue->addAsyncTask(
        "setGain", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_gain(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setExposure(unsigned char val)
{
    // Cache the new exposure value (the camera will converge to this)
    m_exposure = val;

    // Add an async task to set the exposure on the camera
    m_task_queue->addAsyncTask(
        "setExposure", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_exposure(m_usb_device_handle, val, callback);
        });
}


void PS3EyeLibUSBCapture::setSharpness(unsigned char val)
{
    // Cache the new sharpness value (the camera will converge to this)
    m_sharpness = val;

    // Add an async task to set the sharpness on the camera
    m_task_queue->addAsyncTask(
        "setSharpness", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_sharpness(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setContrast(unsigned char val)
{
    // Cache the new contrast value (the camera will converge to this)
    m_contrast = val;

    // Add an async task to set the sharpness on the camera
    m_task_queue->addAsyncTask(
        "setContrast", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_contrast(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setBrightness(unsigned char val)
{
    // Cache the new brightness value (the camera will converge to this)
    m_brightness = val;

    // Add an async task to set the sharpness on the camera
    m_task_queue->addAsyncTask(
        "setBrightness", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_brightness(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setHue(unsigned char val)
{
    // Cache the new hue value (the camera will converge to this)
    m_hue = val;

    // Add an async task to set the sharpness on the camera
    m_task_queue->addAsyncTask(
        "setHue", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_hue(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setRedBalance(unsigned char val)
{
    // Cache the new red balance value (the camera will converge to this)
    m_redBalance = val;

    // Add an async task to set the red balance on the camera
    m_task_queue->addAsyncTask(
        "setRedBalance", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_red_balance(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setGreenBalance(unsigned char val)
{
    // Cache the new green balance value (the camera will converge to this)
    m_greenBalance = val;

    // Add an async task to set the green balance on the camera
    m_task_queue->addAsyncTask(
        "setGreenBalance", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_green_balance(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setBlueBalance(unsigned char val)
{
    // Cache the new blue balance value (the camera will converge to this)
    m_blueBalance = val;

    // Add an async task to set the red balance on the camera
    m_task_queue->addAsyncTask(
        "setBlueBalance", 
        [this, val](async::TaskCallback<int> callback) {
            async_set_blue_balance(m_usb_device_handle, val, callback);
        });
}

void PS3EyeLibUSBCapture::setFlip(bool horizontal, bool vertical)
{
    // Cache the new camera flip flags (the camera will converge to this)
    m_flip_h= horizontal;
    m_flip_v= vertical;

    // Add an async task to set the red balance on the camera
    m_task_queue->addAsyncTask(
        "setFlip", 
        [this, horizontal, vertical](async::TaskCallback<int> callback) {
            async_set_flip(m_usb_device_handle, horizontal, vertical, callback);
        });
}

bool PS3EyeLibUSBCapture::getUSBPortPath(char *out_identifier, size_t max_identifier_length) const
{
    return USBDeviceManager::getInstance()->getUSBDevicePath(m_usb_device_handle, out_identifier, max_identifier_length);   
}

unsigned char* PS3EyeLibUSBCapture::getFrame()
{
    return m_main_thread_frame_buffer;
}

//-- private helpers ----
static void async_set_autogain(
    t_usb_device_handle device_handle, 
    bool bAutoGain, 
    uint8_t gain,
    uint8_t exposure,
    async::TaskCallback<int> outCallback)
{
    async::TaskVector<int> tasks;
    uint8_t read_reg_0x64_result= 0;

    if (bAutoGain) 
    {
        tasks.push_back(
            [device_handle](async::TaskCallback<int> taskDoneCallback) {
                async_sccb_reg_write(device_handle, 0x13, 0xf7, taskDoneCallback); //AGC,AEC,AWB ON
            });
        tasks.push_back(
            [device_handle, &read_reg_0x64_result](async::TaskCallback<int> taskDoneCallback) mutable {
                async_sccb_reg_read(device_handle, 0x64, 
                    [&read_reg_0x64_result, taskDoneCallback](async::ErrorCode error, int result) mutable {
                        read_reg_0x64_result= result;
                        taskDoneCallback(error, result);
                    });
            });
        tasks.push_back(
            [device_handle, read_reg_0x64_result](async::TaskCallback<int> taskDoneCallback) {
                async_sccb_reg_write(device_handle, 0x64, read_reg_0x64_result | 0x03, taskDoneCallback);
            });
    }
    else 
    {
        tasks.push_back(
            [device_handle](async::TaskCallback<int> taskDoneCallback) {
                async_sccb_reg_write(device_handle, 0x13, 0xf0, taskDoneCallback); //AGC,AEC,AWB OFF
            });
        tasks.push_back(
            [device_handle, &read_reg_0x64_result](async::TaskCallback<int> taskDoneCallback) mutable {
                async_sccb_reg_read(device_handle, 0x64, 
                    [&read_reg_0x64_result, taskDoneCallback](async::ErrorCode error, int result) mutable {
                        read_reg_0x64_result= result;
                        taskDoneCallback(error, result);
                    });
            });
        tasks.push_back(
            [device_handle, read_reg_0x64_result](async::TaskCallback<int> taskDoneCallback) {
                async_sccb_reg_write(device_handle, 0x64, read_reg_0x64_result & 0xFC, taskDoneCallback);
            });
        tasks.push_back(
            [device_handle, gain](async::TaskCallback<int> taskDoneCallback) {
                async_set_gain(device_handle, gain, taskDoneCallback);
            });
        tasks.push_back(
            [device_handle, exposure](async::TaskCallback<int> taskDoneCallback) {
                async_set_exposure(device_handle, exposure, taskDoneCallback);
            });
    }

    // Evaluate tasks in order
    // Pass outCallback the final error code and result of the task chain
    async::series<int>(
        tasks,
        [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
    {
        int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

        outCallback(outErrorCode, outResult);
    });
}

static void async_set_auto_white_balance(
    t_usb_device_handle device_handle, 
    bool bAutoWhiteBalance, 
    async::TaskCallback<int> outCallback)
{
    if (bAutoWhiteBalance)
    {
        async_sccb_reg_write(device_handle, 0x63, 0xe0, outCallback); //AWB ON
    }
    else
    {
        async_sccb_reg_write(device_handle, 0x63, 0xAA, outCallback); //AWB OFF
    }
}

static void async_set_gain(
    t_usb_device_handle device_handle, 
    unsigned char val, 
    async::TaskCallback<int> outCallback)
{
    switch (val & 0x30)
    {
    case 0x00:
        val &= 0x0F;
        break;
    case 0x10:
        val &= 0x0F;
        val |= 0x30;
        break;
    case 0x20:
        val &= 0x0F;
        val |= 0x70;
        break;
    case 0x30:
        val &= 0x0F;
        val |= 0xF0;
        break;
    }

    async_sccb_reg_write(device_handle, 0x00, val, outCallback);
}

static void async_set_exposure(
    t_usb_device_handle device_handle, 
    unsigned char val, 
    async::TaskCallback<int> outCallback)
{
    async::TaskVector<int> tasks {
        [device_handle, val](async::TaskCallback<int> taskDoneCallback) {
            async_sccb_reg_write(device_handle, 0x08, val >> 7, taskDoneCallback);
        },
        [device_handle, val](async::TaskCallback<int> taskDoneCallback) {
            async_sccb_reg_write(device_handle, 0x10, val << 1, taskDoneCallback);
        },
    };

    // Evaluate tasks in order
    // Pass outCallback the final error code and result of the task chain
    async::series<int>(
        tasks,
        [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
        {
            int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

            outCallback(outErrorCode, outResult);
        });
}

static void async_set_sharpness(
    t_usb_device_handle device_handle,
    unsigned char val, 
    async::TaskCallback<int> outCallback)
{
    async::TaskVector<int> tasks {
        [device_handle, val](async::TaskCallback<int> taskDoneCallback) {
            async_sccb_reg_write(device_handle, 0x91, val, taskDoneCallback);
        },
        [device_handle, val](async::TaskCallback<int> taskDoneCallback) {
            async_sccb_reg_write(device_handle, 0x8E, val, taskDoneCallback);
        },
    };

    // Evaluate tasks in order
    // Pass outCallback the final error code and result of the task chain
    async::series<int>(
        tasks,
        [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
        {
            int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

            outCallback(outErrorCode, outResult);
        });
}

static void async_set_contrast(
    t_usb_device_handle device_handle, 
    unsigned char val, 
    async::TaskCallback<int> outCallback)
{
    async_sccb_reg_write(device_handle, 0x9C, val, outCallback);
}

static void async_set_brightness(
    t_usb_device_handle device_handle, 
    unsigned char val, 
    async::TaskCallback<int> outCallback)
{
    async_sccb_reg_write(device_handle, 0x9B, val, outCallback);
}

static void async_set_hue(
    t_usb_device_handle device_handle,
    unsigned char val,
    async::TaskCallback<int> outCallback)
{
    async_sccb_reg_write(device_handle, 0x01, val, outCallback);
}

static void async_set_red_balance(
    t_usb_device_handle device_handle,
    unsigned char val,
    async::TaskCallback<int> outCallback)
{
    async_sccb_reg_write(device_handle, 0x43, val, outCallback);
}

static void async_set_green_balance(
    t_usb_device_handle device_handle,
    unsigned char val,
    async::TaskCallback<int> outCallback)
{
    async_sccb_reg_write(device_handle, 0x44, val, outCallback);
}

static void async_set_blue_balance(
    t_usb_device_handle device_handle,
    unsigned char val,
    async::TaskCallback<int> outCallback)
{
    async_sccb_reg_write(device_handle, 0x42, val, outCallback);
}

static void async_set_flip(
    t_usb_device_handle device_handle, 
    bool horizontal, 
    bool vertical, 
    async::TaskCallback<int> outCallback)
{
    uint8_t read_reg_0x0C_result= 0;

    async::TaskVector<int> tasks {
        [device_handle, &read_reg_0x0C_result](async::TaskCallback<int> taskDoneCallback) mutable {
            async_sccb_reg_read(device_handle, 0x0C, 
                [&read_reg_0x0C_result, taskDoneCallback](async::ErrorCode error, int result) mutable {
                    read_reg_0x0C_result= result & ~0xC0;
                    taskDoneCallback(error, result);
                });
        },
        [device_handle, read_reg_0x0C_result, horizontal, vertical](async::TaskCallback<int> taskDoneCallback) {
            uint8_t val= read_reg_0x0C_result;
            if (!horizontal) val |= 0x40;
            if (!vertical) val |= 0x80;
            async_sccb_reg_write(device_handle, 0x0C, val, taskDoneCallback);
        }
    };

    // Evaluate tasks in order
    // Pass outCallback the final error code and result of the task chain
    async::series<int>(
        tasks,
        [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
        {
            int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

            outCallback(outErrorCode, outResult);
        });
}

static uint8_t async_set_frame_rate(
    t_usb_device_handle device_handle, 
    uint32_t frame_width,
    uint8_t frame_rate, 
    bool dry_run,
    async::TaskCallback<int> outCallback)
{
    struct rate_s {
            uint8_t fps;
            uint8_t r11;
            uint8_t r0d;
            uint8_t re5;
    };
    static const struct rate_s rate_0[] = { /* 640x480 */
            {60, 0x01, 0xc1, 0x04},
            {50, 0x01, 0x41, 0x02},
            {40, 0x02, 0xc1, 0x04},
            {30, 0x04, 0x81, 0x02},
            {15, 0x03, 0x41, 0x04},
    };
    static const struct rate_s rate_1[] = { /* 320x240 */
            {205, 0x01, 0xc1, 0x02}, /* 205 FPS: video is partly corrupt */
            {187, 0x01, 0x81, 0x02}, /* 187 FPS or below: video is valid */
            {150, 0x01, 0xc1, 0x04},
            {137, 0x02, 0xc1, 0x02},
            {125, 0x02, 0x81, 0x02},
            {100, 0x02, 0xc1, 0x04},
            {75, 0x03, 0xc1, 0x04},
            {60, 0x04, 0xc1, 0x04},
            {50, 0x02, 0x41, 0x04},
            {37, 0x03, 0x41, 0x04},
            {30, 0x04, 0x41, 0x04},
    };

    int rate_index;
    const struct rate_s *rate= nullptr;

    if (frame_width == 640) {
            rate = rate_0;
            rate_index = ARRAY_SIZE(rate_0);
    } else {
            rate = rate_1;
            rate_index = ARRAY_SIZE(rate_1);
    }
    while (--rate_index > 0) {
            if (frame_rate >= rate->fps)
                    break;
            rate++;
    }

    if (!dry_run)
    {
        async::TaskVector<int> tasks {
            [device_handle, rate](async::TaskCallback<int> taskDoneCallback) {
                async_sccb_reg_write(device_handle, 0x11, rate->r11, taskDoneCallback);
            },
            [device_handle, rate](async::TaskCallback<int> taskDoneCallback) {
                async_sccb_reg_write(device_handle, 0x0d, rate->r0d, taskDoneCallback);
            },
            [device_handle, rate](async::TaskCallback<int> taskDoneCallback) {
                async_ov534_reg_write(device_handle, 0xe5, rate->re5, taskDoneCallback);
            }
        };

        // Evaluate tasks in order
        // Pass outCallback the final error code and result of the task chain
        async::series<int>(
            tasks, 
            [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
            {
                int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

                outCallback(outErrorCode, outResult);
            });
    }

    return rate->fps;
}

static void async_sccb_reg_write(
    t_usb_device_handle device_handle, 
    uint8_t reg, 
    uint8_t val, 
    async::TaskCallback<int> outCallback)
{
    async::TaskVector<int> tasks {
        [device_handle, reg](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, OV534_REG_SUBADDR, reg, taskDoneCallback);
        },
        [device_handle, val](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, OV534_REG_WRITE, val, taskDoneCallback);
        },
        [device_handle](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, OV534_REG_OPERATION, OV534_OP_WRITE_3, taskDoneCallback);
        },
        [device_handle](async::TaskCallback<int> taskDoneCallback) {
            async_sccb_check_status(device_handle, [taskDoneCallback](async::ErrorCode statusCheckErrorCode, int statusCheckResult)
            {
                taskDoneCallback((statusCheckResult == 1) ? async::OK : async::FAIL, statusCheckResult);
            });
        },
    };

    // Evaluate tasks in order
    // Pass outCallback the final error code and result of the task chain
    async::series<int>(
        tasks, 
        [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
        {
            int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

            outCallback(outErrorCode, outResult);
        });
}

static void async_sccb_reg_write_array(
    t_usb_device_handle device_handle, 
    const uint8_t (*sequence)[2], 
    int sequenceLength, 
    async::TaskCallback<int> outCallback)
{
    const uint8_t (*data)[2]= sequence;
    int len = sequenceLength;

    // Loop-Test function
    // Keep running Work function while Loop-Test return true
    auto loop_test = [&len]() mutable
    {
        --len;
        return len >= 0;
    };

    // Work function
    // This gets run each iteration
    auto work_func = [device_handle, &data](
        std::function<void(async::ErrorCode error)> work_done_callback) mutable
    {
        if ((*data)[0] != 0xff) 
        {
            // Write to the register...
            async_sccb_reg_write(
                device_handle,
                (*data)[0], // register
                (*data)[1], // value
                [&data, work_done_callback](async::ErrorCode errorCode, int result) mutable
                {
                    //... then increment the sequence pointer
                    data++;

                    // Move on to the next iteration
                    work_done_callback(errorCode);
                });
        }
        else
        {
            // Read the register first..
            async_sccb_reg_read(
                device_handle,
                (*data)[1], // register
                [device_handle, &data, work_done_callback](async::ErrorCode errorCode, int result) mutable
                {
                    // ... then write 0x00 to reg 0xff ...
                    async_sccb_reg_write(device_handle,0xff, 0x00, 
                        [&data, work_done_callback](async::ErrorCode errorCode, int data) mutable
                        {
                            // ... then increment the sequence pointer
                            data++;

                            // Move on to the next iteration
                            work_done_callback(errorCode);
                        });
                });
        }
    };

    // Write out the (reg - val) pair array
    async::whilst(loop_test, work_func);
}

static void async_sccb_reg_read(
    t_usb_device_handle device_handle, 
    uint16_t reg, 
    async::TaskCallback<int> outCallback)
{
    async::TaskVector<int> tasks{
        [device_handle, reg](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, OV534_REG_SUBADDR, (uint8_t)reg, taskDoneCallback);
        },
        [device_handle](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, OV534_REG_OPERATION, OV534_OP_WRITE_2, taskDoneCallback);
        },
        [device_handle](async::TaskCallback<int> taskDoneCallback) {
            async_sccb_check_status(device_handle, [taskDoneCallback](async::ErrorCode statusCheckErrorCode, int statusCheckResult)
            {
                taskDoneCallback((statusCheckResult == 1) ? async::OK : async::FAIL, statusCheckResult);
            });
        },
        [device_handle](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, OV534_REG_OPERATION, OV534_OP_READ_2, taskDoneCallback);
        },
        [device_handle](async::TaskCallback<int> taskDoneCallback) {
            async_sccb_check_status(device_handle, [taskDoneCallback](async::ErrorCode statusCheckErrorCode, int statusCheckResult)
            {
                taskDoneCallback((statusCheckResult == 1) ? async::OK : async::FAIL, statusCheckResult);
            });
        },
        [device_handle](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_read(device_handle, OV534_REG_READ, taskDoneCallback);
        },
    };

    // Evaluate tasks in order
    // Pass outCallback the final error code and result of the task chain
    async::series<int>(
        tasks,
        [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
        {
            // NOTE: If the task chain succeeded, 
            // the final result value will be the value read from camera register
            // via the ov534_reg_read task.
            int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

            outCallback(outErrorCode, outResult);
        });
}

static void async_sccb_check_status(
    t_usb_device_handle device_handle,
    async::TaskCallback<int> outCallback)
{
    int sccbStatusResult= 0;
    bool bQuerySuccess= false;
    int queryAttempCount = 0;

    // Loop-Test function
    // Keep running Work function while Loop-Test return true
    auto loop_test = [&queryAttempCount, bQuerySuccess]() mutable
    {
        bool keep_going = !bQuerySuccess && queryAttempCount < 5;
        queryAttempCount++;
        return keep_going;
    };

    // Work function
    // This gets run each iteration
    auto work_func = [device_handle, &bQuerySuccess, &sccbStatusResult](
        std::function<void(async::ErrorCode error)> work_done_callback) mutable
    {
        async_ov534_reg_read(
            device_handle,
            OV534_REG_STATUS,
            [&bQuerySuccess, &sccbStatusResult, work_done_callback](async::ErrorCode errorCode, int data) mutable
            {
                if (errorCode == async::OK)
                {
                    bQuerySuccess = true;

                    switch (data)
                    {
                    case 0x00:
                        sccbStatusResult = 1;
                    case 0x04:
                        sccbStatusResult = 0;
                    case 0x03:
                        break;
                    default:
                        SERVER_LOG_WARNING("sccb_check_status") << "unknown sccb status 0x"
                            << std::hex << std::setfill('0') << std::setw(2) << data;
                    }
                }

                work_done_callback(async::OK);
            });
    };

    // Final-Callback function
    // This gets run after the Loop-Test function returns false
    auto final_callback = [sccbStatusResult, outCallback](async::ErrorCode errorCode)
    {
        // Send the final query result
        outCallback(errorCode, sccbStatusResult);
    };

    // Try to poll status up to 5 time max from the OV534_REG_STATUS register
    async::whilst(loop_test, work_func, final_callback);
}

static void async_ov534_reg_write(
    t_usb_device_handle device_handle, 
    uint16_t reg, 
    uint8_t val, 
    async::TaskCallback<int> outCallback)
{
    USBTransferRequest request;
    memset(&request, 0, sizeof(USBTransferRequest));
    request.request_type= _USBRequestType_ControlTransfer;
    request.payload.control_transfer.usb_device_handle= device_handle;
    request.payload.control_transfer.bmRequestType = 
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE;
    request.payload.control_transfer.bRequest= 0x01;
    request.payload.control_transfer.wValue= 0x00;
    request.payload.control_transfer.wIndex = reg;
    request.payload.control_transfer.data[0]= val;
    request.payload.control_transfer.wLength= 1;
    request.payload.control_transfer.timeout= 500;

    // Submit the async usb control transfer request...
    USBDeviceManager::getInstance()->submitTransferRequest(
        request, 
        [&outCallback](USBTransferResult &result) 
        {
            assert(result.result_type == _USBResultType_ControlTransfer);

            //... whose result we get notified of here
            if (result.payload.control_transfer.result_code == _USBResultCode_Completed)
            {
                // Tell the callback the transfer completed ok :)
                // The callback parameter value will be _USBResultCode_Completed
                outCallback(async::OK, result.payload.control_transfer.result_code);
            }
            else
            {
                // Tell the callback the transfer failed :(
                // The callback parameter value will be the error code
                log_usb_result_code("ov534_reg_write", result.payload.control_transfer.result_code);
                outCallback(async::FAIL, result.payload.control_transfer.result_code);
            }
        }
    );
}

static void async_ov534_reg_write_array(
    t_usb_device_handle device_handle,
    const uint8_t (*sequence)[2], 
    int sequenceLength,
    async::TaskCallback<int> outCallback)
{
    const uint8_t (*data)[2]= sequence;
    int len = sequenceLength;

    // Loop-Test function
    // Keep running Work function while Loop-Test return true
    auto loop_test = [&len]() mutable
    {
        --len;
        return len >= 0;
    };

    // Work function
    // This gets run each iteration
    auto work_func = [device_handle, &data](
        std::function<void(async::ErrorCode error)> work_done_callback) mutable
    {
        async_ov534_reg_write(
            device_handle,
            (*data)[0], // register
            (*data)[1], // value
            [&data, work_done_callback](async::ErrorCode errorCode, int result) mutable
            {
                data++;
                work_done_callback(errorCode);
            });
    };

    // Write out the (reg - val) pair array
    async::whilst(loop_test, work_func);
}

static void async_ov534_reg_read(
    t_usb_device_handle device_handle, 
    uint16_t reg, 
    async::TaskCallback<int> outCallback)
{
    USBTransferRequest request;
    memset(&request, 0, sizeof(USBTransferRequest));
    request.request_type = _USBRequestType_ControlTransfer;
    request.payload.control_transfer.usb_device_handle= device_handle;
    request.payload.control_transfer.bmRequestType =
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE;
    request.payload.control_transfer.bRequest = 0x01;
    request.payload.control_transfer.wValue = 0x00;
    request.payload.control_transfer.wIndex = reg;
    request.payload.control_transfer.wLength = 1;
    request.payload.control_transfer.timeout = 500;

    // Submit the async usb control transfer request...
    USBDeviceManager::getInstance()->submitTransferRequest(
        request,
        [&outCallback](USBTransferResult &result)
        {
            assert(result.result_type == _USBResultType_ControlTransfer);

            //... whose result we get notified of here
            if (result.payload.control_transfer.result_code == _USBResultCode_Completed)
            {
                assert(result.payload.control_transfer.dataLength == 1);

                // Tell the callback the transfer completed ok :)
                // The callback parameter value will value we read from the camera register.
                outCallback(async::OK, static_cast<int>(result.payload.control_transfer.data[0]));
            }
            else
            {
                // Tell the callback the transfer failed :(
                log_usb_result_code("ov534_reg_read", result.payload.control_transfer.result_code);
                outCallback(async::FAIL, result.payload.control_transfer.result_code);
            }
        }
    );
}

/* Two bits control LED: 0x21 bit 7 and 0x23 bit 7.
 * (direction and output)? */
static void async_ov534_set_led(
    t_usb_device_handle device_handle,
    bool bLedOn, 
    async::TaskCallback<int> outCallback)
{
    uint8_t read_reg_result= 0;

    async::TaskVector<int> tasks {
        // Change register value 0x21
        [device_handle, &read_reg_result](async::TaskCallback<int> taskDoneCallback) mutable {
            async_ov534_reg_read(device_handle, 0x21, 
                [&read_reg_result, taskDoneCallback](async::ErrorCode error, int result) mutable {
                    read_reg_result= result | 0x80;
                    taskDoneCallback(error, result);
                });
        },
        [device_handle, read_reg_result](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, 0x21, read_reg_result, taskDoneCallback);
        },
        // Change register value 0x23
        [device_handle, &read_reg_result, bLedOn](async::TaskCallback<int> taskDoneCallback) mutable {
            async_ov534_reg_read(device_handle, 0x23, 
                [&read_reg_result, taskDoneCallback, bLedOn](async::ErrorCode error, int result) mutable {
                    if (bLedOn)
                        read_reg_result= result | 0x80;
                    else
                        read_reg_result= result & ~0x80;
                    taskDoneCallback(error, result);
                });
        },
        [device_handle, read_reg_result](async::TaskCallback<int> taskDoneCallback) {
            async_ov534_reg_write(device_handle, 0x23, read_reg_result, taskDoneCallback);
        },
    };

    if (!bLedOn)
    {
        tasks.push_back(
            [device_handle, &read_reg_result](async::TaskCallback<int> taskDoneCallback) mutable {
                async_ov534_reg_read(device_handle, 0x21, 
                    [&read_reg_result, taskDoneCallback](async::ErrorCode error, int result) mutable {
                        read_reg_result= result & ~0x80;
                        taskDoneCallback(error, result);
                    });
            });
        tasks.push_back(
            [device_handle, read_reg_result](async::TaskCallback<int> taskDoneCallback) {
                async_ov534_reg_write(device_handle, 0x21, read_reg_result, taskDoneCallback);
            });
    }

    // Evaluate tasks in order
    // Pass outCallback the final error code and result of the task chain
    async::series<int>(
        tasks,
        [outCallback](async::ErrorCode outErrorCode, std::vector<int> results)
        {
            // NOTE: If the task chain succeeded, 
            // the final result value will be the value read from camera register
            // via the ov534_reg_read task.
            int outResult = (results.size() > 0) ? results[results.size() - 1] : 0;

            outCallback(outErrorCode, outResult);
        });
}

static void log_usb_result_code(const char *function_name, eUSBResultCode result_code)
{
    switch (result_code)
    {
    // Success Codes
    case eUSBResultCode::_USBResultCode_Started:
        SERVER_LOG_INFO(function_name) << "request started";
        break;
    case eUSBResultCode::_USBResultCode_Canceled:
        SERVER_LOG_INFO(function_name) << "request canceled";
        break;
    case eUSBResultCode::_USBResultCode_Completed:
        SERVER_LOG_INFO(function_name) << "request completed";
        break;

    // Failure Codes
    case eUSBResultCode::_USBResultCode_GeneralError:
        SERVER_LOG_ERROR(function_name) << "request failed: general request error";
        break;
    case eUSBResultCode::_USBResultCode_BadHandle:
        SERVER_LOG_ERROR(function_name) << "request failed: bad USB device handle";
        break;
    case eUSBResultCode::_USBResultCode_NoMemory:
        SERVER_LOG_ERROR(function_name) << "request failed: no memory";
        break;
    case eUSBResultCode::_USBResultCode_SubmitFailed:
        SERVER_LOG_ERROR(function_name) << "request failed: submit failed";
        break;
    case eUSBResultCode::_USBResultCode_DeviceNotOpen:
        SERVER_LOG_ERROR(function_name) << "request failed: device not open";
        break;
    case eUSBResultCode::_USBResultCode_TransferNotActive:
        SERVER_LOG_ERROR(function_name) << "request failed: transfer not active";
        break;
    case eUSBResultCode::_USBResultCode_TransferAlreadyStarted:
        SERVER_LOG_ERROR(function_name) << "request failed: transfer already started";
        break;
    case eUSBResultCode::_USBResultCode_Overflow:
        SERVER_LOG_ERROR(function_name) << "request failed: transfer overflow";
        break;
    case eUSBResultCode::_USBResultCode_Pipe:
        SERVER_LOG_ERROR(function_name) << "request failed: transfer pipe error";
        break;
    case eUSBResultCode::_USBResultCode_TimedOut:
        SERVER_LOG_ERROR(function_name) << "request failed: transfer timed out";
        break;
    };
}