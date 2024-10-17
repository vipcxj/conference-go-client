#ifndef _CFGO_UVC_DEV_HPP_
#define _CFGO_UVC_DEV_HPP_

#include "libuvc/libuvc.h"

namespace cfgo
{
    namespace uvc
    {
        class Device
        {
        private:
            uvc_device_t * m_raw_dev;
        public:
            Device(uvc_device_t * raw_dev) noexcept;
            Device(const Device &) noexcept;
            Device & operator= (const Device &) noexcept;
            Device(Device &&) noexcept;
            Device & operator= (Device &&) noexcept;
            ~Device();

            DeviceHandle open();
        };
        
        using dev_t = Device;

        class DeviceHandle
        {
        private:
            uvc_device_handle_t * m_raw_handle;
        public:
            DeviceHandle(uvc_device_handle_t * raw_handle);
            DeviceHandle(const DeviceHandle &) = delete;
            DeviceHandle & operator= (const DeviceHandle &) = delete;
            DeviceHandle(DeviceHandle &&) noexcept;
            DeviceHandle & operator= (DeviceHandle &&) noexcept;
            ~DeviceHandle();

            uvc_device_handle_t * unwrap() const noexcept
            {
                return m_raw_handle;
            }

            void check_valid() const;
        };

        using dev_handle_t = DeviceHandle;

    } // namespace uvc
    
} // namespace cfgo


#endif