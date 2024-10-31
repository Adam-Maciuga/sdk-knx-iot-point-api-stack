# Introduction

This folder contains code examples of how to use the stack.

The intention of the examples is to explain certain aspects of the stack.
e.g. provide information in how to build an KNX IoT Point API device based on the stack.

## Example applications

### Serial numbers/ passwords

Can be set in code file. 
Note, the example applications have deliberated incorrect serial numbers.

### knx_iot_virtual_sa.c

Console application for Windows & Linux.

- no KNX compliant application
- example to be used to validate against KNX EITT certification tests 

### knx_iot_virtual_sa.cpp

GUI application for Windows (fetches wxWidgets GUI framework).

- no KNX compliant application
- example to be used to validate against KNX EITT certification tests 
- view of device tables (auth, ...)
- trigger of device commands (reset, ...)