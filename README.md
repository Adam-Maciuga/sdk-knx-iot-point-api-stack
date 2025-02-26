[TOC]

# Introduction 

A common (stack) introduction is available under the [wiki pages](../../wikis/Home), more branch specific topics are listed here below. 

# Project Directory Structure

__api/*__  
contains the implementations of: 
* client/server APIs 
  * (rest) API 
  * (programming) API
* resources
* utility and helper functions to encode/decode CBOR to/from data points (function blocks)
* module for encoding and interpreting endpoints 
* handlers for the discovery, device and application resources

__messaging/coap/__  
contains a tailored CoAP implementation

__security/*__  
contains resource handlers that implement the security model, using OSCORE

__utils/*__  
contains a few primitive building blocks used internally by the core framework

__deps/*__  
contains external project dependencies

 *  __deps/tinycbor/*__   
    contains the tinyCBOR sources

 *  __deps/mbedtls/*__  
    contains the mbedTLS sources
   
 > The IoT stack repository uses GIT **submodules** to retrieve the (above described) external code as part of the 
   version control system (also possible is to use CMake **fetchcontent** that handles it as part of the build system). 
   The `.gitmodules` file defines the folder/path per submodule, the specifically used commit ID is defined in 
   the corresponding folder with a gitlink (name@commit). 
   See git/stack overflow documentation for gitmodules (how to pull or init submodules).   

__include/*__  
contains all common headers

__include/oc_api.h__  
contains client/server APIs

__include/oc_rep.h__  
contains helper functions to encode/decode to/from cbor

__include/oc_helpers.h__  
contains utility functions for allocating strings and arrays either dynamically from the heap or 
from pre-allocated memory pools

__port/\*.h__
* collectively represents the platform abstraction

__port/\<OS>/*__  
contains adaptations for OS platforms
  
- **Linux**  
Storage folder is created by the make system.

- **Windows**  
 Storage folder is automatically created by the make system. As extra also the stack creates the storage folder.
 This allows copying of the executables to other folders without having to know which folder to create.

__apps/*__  
contains the sample [application](apps/Readme.md) describing how to use the stack

