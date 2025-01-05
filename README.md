# Introduction

KNX IoT Point API stack is an open-source, reference implementation of the KNX IoT standard for the Internet of Things (IoT). 
Specifically, the stack realizes all the functionalities of the KNX IoT Point API specification.

![](./images/knxstack-v1.png)
   
The responsibilities between the stack and an actual KNX IoT Point API device implementation is depicted 
in the following diagram.

```plantuml

@startuml

title Application vs. Stack 
class Application 
{
  Implements
  ..
  - Configure and listen to Datapoint GET/PUT/POST/DELETE requests
  - Issuing S-Mode requests
  - Receive S-Mode requests
  - Set device characteristics data
  - Startung the stack
}

class Stack 
{
  Functionallity
  ..
  - Discovery (CoAP & mDNS)
  - S-Mode client and server
  - Datapoint m/o client and server resources
  - Security  
  - Persistent Storage
  - Device characteristics data
  - Application data (tables)
}

Application ..> Stack : uses

@enduml
```

The project was created to bring together the open-source community to accelerate the development of the KNX IoT Point API devices and services required to connect the growing number of IoT devices. 
The project offers device vendors and application developers royalty-free access under the [Apache 2.0 license](LICENSE.md).

# Stack Features

* **OS Agnostic** 

  The KNX IoT Point API device stack and modules work cross-platform (pure C code) and execute in an event-driven style. 
  The stack interacts with lower level OS/hardware platform-specific functionality through a set of abstract interfaces. 
  This decoupling of standards related functionality from platform adaptation code promotes ease of long-term maintenance and evolution of the stack through successive releases.

![](./images/porting.png "Porting Layer")

* **Porting Layer** 

  The platform abstraction is a set of generically defined interfaces which elicit a specific contract from implementations. 
  The stack utilizes these interfaces to interact with the underlying OS/platform. 
  The simplicity and boundedness of these interface definitions allow them to be rapidly implemented on any chosen OS/target. Such an implementation constitutes a "port".

# Project Directory Structure

__api/*__
* contains the implementations of `client/server APIs <https://knx-iot.github.io/KNX-IOT-STACK-doxygen/>`_, the resources,
  utility and helper functions to encode/decode CBOR
  to/from data points (function blocks), module for encoding and interpreting endpoints, and handlers for the discovery, device
  and application resources.

__messaging/coap/__
* contains a tailored CoAP implementation.

__security/*__
* contains resource handlers that implement the security model, using OSCORE.

__utils/*__
* contains a few primitive building blocks used internally by the core
  framework.

__deps/*__
*  contains external project dependencies from below.
   
    __deps/tinycbor/*__
    * contains the tinyCBOR sources.

    __deps/mbedtls/*__
    * contains the mbedTLS sources.

    __include/*__
    * contains all common headers.

   > The IoT stack repository uses GIT **submodules** to retrieve the external code as part of the version control system (in 
   contrast to CMake **fetchcontent** that handels it as part of the build system). The `.gitmodules` file 
   defines the folder/path per submodule, the specific commit ID is defined in 
   the corresüponding folder with a gitlink (name@commit). See git documentation.   

__include/oc_api.h__
* contains client/server APIs.

__include/oc_rep.h__
* contains helper functions to encode/decode to/from cbor

__include/oc_helpers.h__
* contains utility functions for allocating strings and arrays either dynamically from the heap or 
  from pre-allocated memory pools.

__port/\*.h__
* collectively represents the platform abstraction.

__port/<OS>/*__
* contains adaptations for each OS. Platforms:
  
  - **Linux**
    Storage folder is created by the make system

  - **Windows**
    Storage folder is automatic created by the make system. As extra also the stack creates the storage folder.
    This allows copying of the executables to other folders without having to know which folder to create.

__apps/*__
* contains the sample [application](apps/Readme.md) desribeding how to use the stack.

# Build instructions

Grab source and dependencies from GitLab `git clone --recursive https://gitlab.knx.org/public-projects/knx-iot-point-api-stack.git`
Please check here for build instructions:

 - [Windows](<https://knx-iot.github.io/building_windows/>)
 - [Linux](<https://knx-iot.github.io/building_linux/>)
