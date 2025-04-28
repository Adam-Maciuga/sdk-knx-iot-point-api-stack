[TOC]

# Introduction 

A common (stack) introduction is available under the [wiki pages](../../wikis/Home), 
more branch specific topics are listed here below. 

# Project Directory Structure

__api/*__  
Contains the implementations of: 

* client/server APIs 
  * (rest) API 
  * (programming) API
* resources
* utility and helper functions to encode/decode CBOR to/from data points (function blocks)
* module for encoding and interpreting endpoints 
* handlers for the discovery, device and application resources

__messaging/coap/__  
Contains a tailored CoAP implementation.

__security/*__  
Contains resource handlers that implement the security model, using OSCORE.

__utils/*__  
Contains a few primitive building blocks used internally by the core framework.

__deps/*__  
Contains external project dependencies.

 *  __tinycbor/*__   
    Contains the tinyCBOR sources.

 *  __mbedtls/*__  
    Contains the mbedTLS sources.
   
 > The IoT stack repository uses GIT **submodules** to retrieve the (above described) external code 
   as part of the version control system (also possible is to use CMake **fetchcontent** that handles
   it as part of the build system). The `.gitmodules` file defines the folder/path per submodule, 
   the specifically used commit ID is defined in the corresponding folder with a gitlink (name@commit). 
   See git/stack overflow documentation for gitmodules (how to pull or init submodules).   

__include/*__  
Contains all common headers.

__include/oc_api.h__  
Contains client/server APIs.

__include/oc_rep.h__  
Contains helper functions to encode/decode to/from cbor.

__include/oc_helpers.h__  
Contains utility functions for allocating strings and arrays either dynamically from the heap or 
from pre-allocated memory pools.

__port/\*.h__  
Contains the shared platform abstractions.

- DNS/SD 
- Clock
  - The stack uses the clock functions only to evaluate time differences, such as with seconds 
    to inform a client on a server reboot startup time. An absolute (RFC 3339 UTC) time stamp 
    is optional and - if used -  only applicable for the endpoint `swu/lastupdate`. For this see the link 
    on the [wiki pages](../../wikis/Home#endpoints), `Rest API Endpoints`.
- Logging
  - The logging functions, either for the console print out or file print.    
- Random
- Storage 
  - The root folder for a possible storage output is created by the CMake build system, usually it is 
    the build "output" folder. The provided applications ('apps') creates an own (individually named) 
    storage folder in this root folder. This allows copying of the executables to other folders without 
    having to know which folder to create.

__port/\<OS>/*__  
Contains adaptations per supported OS platform. 

- **Linux** 
- **Windows**  

__apps/*__  
Contains the sample [application](apps/Readme.md) describing how to use the stack.

