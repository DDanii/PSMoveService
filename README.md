# PSMoveService
A background service that communicates with the psmove and serves its pose and button states.

# Download

`git clone --recursive https://github.com/cboulay/PSMoveService.git`

`cd PSMoveService`

# Build Dependencies

1. Compiler
    * Windows
        * Visual Studio 2013 is required by OpenCV 3.0.0 pre-compiled binaries.
    * Mac
        * Tested with XCode/clang. gcc may work.
1. OpenCV
    * I am opting for a system install of opencv instead of project-specific.
    * Windows
        * Follow steps 1-3 found [here](https://github.com/MicrocontrollersAndMore/OpenCV_3_Windows_10_Installation_Tutorial/blob/master/Installation%20Cheat%20Sheet%201%20-%20OpenCV%203%20and%20C%2B%2B.pdf)
        * The CMake scripts assume you install to the default directory (C:\OpenCV-3.0.0).
        If not, you will have to add a `-DOpenCV_DIR=<install dir>\build` flag to your cmake command.
    * Mac
        * Install [homebrew](http://brew.sh/)
        * `brew tap homebrew/science`
        * `brew install opencv`
1. Boost
    * Windows
        * From [here](sourceforge.net/projects/boost/files/boost-binaries/1.59.0/),
        get boost_1_59_0-msvc-12.0-32.exe and/or -64.exe.
        * Install to a directory of your choice
        * This path will be referred to BOOST_ROOT later
    * Mac
        * `brew install boost`
1. libusb (Required on Mac and Windows 64-bit for PS3EYEDriver, Optional on Win32)
    * Windows:
        * Open PSMoveService\thirdparty\libusb\msvc\libusb_2013.sln
        * For each combination of Release/Debug * Win32/x64, right-click on libusb-1.0 (static) and Build.
        * Close this Visual Studio Solution.
    * Mac:
        * `cd thirdparty/libusb`
        * `./autogen.sh`
        * `./configure`
        * `./configure` (yes, a second time)
        * `make`
1. Optional: [CL Eye Driver](https://codelaboratories.com/products/eye/driver/)
    * Only necessary for Windows 32-bit if not using PS3EYEDriver
    * Currently $2.99 USD (paypal or credit card)
    * Platform SDK not necessary

# Make PSMoveService

1. `mkdir build`
1. `cd build`
1. Run cmake
    * Windows: `cmake .. -G "Visual Studio 12" -DOpenCV_DIR=C:\OpenCV-3.0.0\build -DBOOST_ROOT=C:\boost_1_59_0`
    * Mac: `cmake .. - G Xcode`

# Build PSMoveService

### Windows

1. Open <path_to_repo>\build\PSMoveService.sln
1. Change to "Release" configuration
1. Rt-click on the project and build