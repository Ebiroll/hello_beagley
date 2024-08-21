# Project Setup and Build Guide
This document provides a comprehensive guide to set up and build the Hello World project using the Processor SDK RTOS for J722S, SysConfig, and the C7000 Compiler.
Thank you [Saran Sujan](https://github.com/touchhowling) for the updated README   

## 1. Download Required Tools

### Download and Install:

1. **SysConfig:**
   - [Download SysConfig](https://www.ti.com/tool/download/SYSCONFIG)

2. **Processor SDK RTOS for J722S:**
   - [Download Processor SDK](https://www.ti.com/tool/PROCESSOR-SDK-J722S)

3. **C7000 Compiler:**
   - [Download C7000 CGT](https://www.ti.com/tool/C7000-CGT)

### Installation:

1. **Install SysConfig:**
   - Install the SysConfig tarball into the `ti` directory:

2. **Install C7000 Compiler:**
   - Install the C7000 Compiler tarball into the `ti` directory:

3. **Extract Processor SDK RTOS:**
   - Extract the Processor SDK RTOS tarball into the `ti` directory:

     ```sh
     tar -xf ti-processor-sdk-rtos-j722s-evm-09_02_00_05.tar.gz -C ~/ti
     ```

## 2. Set Environment Variables

Set the environment variables to point to the installed tools.
```sh
export CG_TOOL_ROOT=${HOME}/ti/ti-cgt-c7000_4.1.0.LTS
export SDK_INSTALL_PATH=${HOME}/ti/ti-processor-sdk-rtos-j722s-evm-09_02_00_05/mcu_plus_sdk_j722s_09_02_00_59
```


## 3. Navigate to SDK Installation Path

Change to the SDK installation directory:

```sh
cd ${SDK_INSTALL_PATH}
```

### Update SysConfig Version

1. Open the `imports.mak` file in a text editor:

   ```sh
   vi imports.mak
   ```

2. Replace `SYSCONFIG_VERSION` with your SysConfig version, e.g., `1.21.0`:

   ```makefile
   SYSCONFIG_VERSION = 1.21.0
   ```

3. Save the changes and exit the editor.

## 4. Navigate to the Project Directory

Change to the `examples` directory:

```sh
cd ${SDK_INSTALL_PATH}/examples
```

### Clone the Repository

Clone the example project repository:

```sh
git clone https://github.com/Ebiroll/hello_beagley.git
```

Change into the project directory:

```sh
cd hello_beagley
```

## 5. Build the Project

Build the project using the following make commands:

```sh
make -s -C j722s-evm/main-r5fss0-0_freertos/ti-arm-clang
make -s -C j722s-evm/c75ss0-0_freertos/ti-c7000
make -s -C j722s-evm/c75ss1-0_freertos/ti-c7000
```

- Ensure all tools and dependencies are correctly installed.
- Consult the documentation provided with the tools for troubleshooting.
- Adjust paths and filenames based on your actual installation and versions.

  ## 6. Deploy on BeagleY-AI board

  This is described in this article.
  https://medium.com/@olof-astrand/hello-world-on-the-c7x-dsp-coprocessor-with-the-beagley-ai-c18d646c8655
