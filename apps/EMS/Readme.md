# Introduction

This folder contains the EMS apps.

EMS stands for Energy Management System.
These are the three EMS apps:
- PV
- CEM
- Charger

![Concept](concept.png)

## 1. PV app

PV stands for photovoltaic and the app represents the functionality of an Invertor PV control device.

> The current implementation contains one datapoint, representing 'present DC power'.
  This datapoint is represented by a slider, which allows the user to set the present DC power from 0 to 10 kW in steps of 1 kW and to send out the selected value to the medium.

## 2. CEM app

CEM stands for Central Energy Manager.

> The current implementation contains two datapoints and foresees two operation modes.
  
  The first datapoint serves the role of capturing the present DC power from the medium, which is typically send out to the medium by Invertor PV control devices.
  
  The two operation modes are:
  - sun mode: the principle is to only charge the electric car at 4 kW if at least 4kW DC power is procuded by sun light (through PV panels)
  - mix mode: charge the electric car at 4 kW regardless of the present produced DC power by sun light
  The operation mode is represented by a dedicated button, wich allows the user to toggle its value, the current value is indicated inside the button, either 'sun' or 'mix'.
  
  The second datapoint sends, depending on the operation mode out to the medium the requested (calculated) charge rate, eihter at 0 kW or at 4 kW.

## 3. Charger app

This app represents the functionality of an Electric car charger device.

> The current implementation contains one datapoint and serves the role of capturing the requested charge rate from the medium, which is typically send out to the medium by CEM devices.

# Details 

In general:
- all four above mentioned datapoints are of the same type, being DPT: 14.056
- all three apps are based on the public KNX IoT stack and are at this stage only tested on Windows

## Fixed configuration

All three apps have a fixed (hardcoded) configuration for:  
  - IA: individual address
  - IID: installation identifier
  - Group Object Table
  - Publisher Table
  - Recipient Table
  - Authentication Table

A specific algorithm has been implemented.
The idea is to configured these 3 device based on the serial number of the CEM device, the algorithm composes:
- the IID of the 3 devices
- the grpid of all publisher and recipient tables of the 3 devices
- the authentication tables of the 3 devices

## Group addresses

From the fucntionality point of view, the three (virtual) device are linked by means of two group addresses:
- GA1 (0/0/1): links the 'Present DC power' datapoint (object) from the 'Inverter PV control' device **WITH** the 'Present DC power' datapoint (object) from the 'CEM' device
- GA2 (0/0/2): links the 'Charge rate power' datapoint (object) from the 'CEM' device **WITH** the 'Charge rate power' datapoint (object) from the 'Electric car charger' device

### IA (and serial number)

The IA of the three devices are set as follows:
-  PV: 2.0.2 (serial number = 00fa:1002:0b00)
-  CEM: 2.0.3 (serial number = 00fa:1002:0c00)
-  Charger: 2.0.4 (serial number = 00fa:1002:0d00)

### IID

The IID of the three devices is set to: 7d:ff7f:6c9a

### Group Object Table

The Group Object Tables of the three devices are set as follows:
  
- **PV**:
   -  Index 0   id: '0'    url: '/p/pv'   cflags : '64' ...t.  ga : [ 0/0/1 ]
   
- **CEM**: 
   -  Index 0   id: '1'    url: '/p/pv'   cflags : '16' .w...  ga : [ 0/0/1 ]
   -  Index 1   id: '3'    url: '/p/charger'   cflags : '64' ...t.  ga : [ 0/0/2 ]
  
- **Charger**:
   -  Index 0   id: '0'    url: '/p/charger'   cflags : '16' .w...  ga : [ 0/0/2 ]
  
### Publisher and Recipient Table

Both the Publisher and Recipeint Tables of the three devices are set as follows:
  
- **PV**:
   -  Index 0   id: '0'    grpid:  c285:fba0  ga : [ 0/0/1 ]

- **CEM**: 
   -  Index 0   id: '0'    grpid:  c285:fba0  ga : [ 0/0/1 0/0/2 ]
  
- **Charger**:
   -  Index 0   id: '0'    grpid:  c285:fba0  ga : [ 0/0/2 ]
  
### Authentication Table

The Authentication Tables of the three devices are set as follows:
  
- **PV**:
   -  index : '0' id = '0/0/1'   profile : 2 (coap_oscore)  osc_id [2]: 0001  osc_ms [16]: e7cf5dda0d26b5d46ca9dbfb0af02e95  osc_contextid (o)[6]: 000000000800  osc_ga : [ 0/0/1 ]
  
- **CEM**: 
   -  index : '0' id = '0/0/1'   profile : 2 (coap_oscore)  osc_id [2]: 0001  osc_ms [16]: e7cf5dda0d26b5d46ca9dbfb0af02e95  osc_contextid (o)[6]: 000000000800  osc_ga : [ 0/0/1 ]
   -  index : '1' id = '0/0/2'   profile : 2 (coap_oscore)  osc_id [2]: 0002  osc_ms [16]: e7cf5dda0d26b5d46ca9dbfb0af02e96  osc_contextid (o)[6]: 000000000900  osc_ga : [ 0/0/2 ]
  
- **Charger**:
   -  index : '0' id = '0/0/2'   profile : 2 (coap_oscore)  osc_id [2]: 0002  osc_ms [16]: e7cf5dda0d26b5d46ca9dbfb0af02e96  osc_contextid (o)[6]: 000000000900  osc_ga : [ 0/0/2 ]

# Testing with Wireshark

To test with Wireshark (Windows) launch all three devices, move the slider to any value, **OSCORE** frames shall appear on the ethernet/wifi NIC.

The destination address of these captured frames shall be set to this IPv6 multicast address 'ff32:0030:fd7d:ff7f:6c9a:0000:c285:fba0'.

This destination address is composed as follows:
  - scope: fixed to ff32
  - ??: fixed to 0030:fd
  - iid: fixed to 7d:ff7f:6c9a
  - grpid: fixed to c285:fba0

By adding the following OSCORE security contexts the actual payload becomes visible (decrypted):
- Sender ID = 0001, skip Recipient ID, Master Secret = E7CF5DDA0D26B5D46CA9DBFB0AF02E95, skip Master Salt, ID Context = 000000000800, keep default Algorithm
- Sender ID = 0002, skip Recipient ID, Master Secret = E7CF5DDA0D26B5D46CA9DBFB0AF02E96, skip Master Salt, ID Context = 000000000900, keep default Algorithm

The Concise Binary Object Representation (CBOR) in Wireshark serves the purpose of validating the payload, in this case the frames contain a (CBOR) MAP within a MAP, the value of the datapoints can be found within the inner MAP.

The inner MAP contains three (CBOR) pairs:
- 7: ga = either 1 or 2
- 6: service = w ('write')
- 1: value

The values of the datapoints are set as (CBOR) unsigned integer 'Atoms' and can have the following values:
- 00 = 0 kW
- 19 03 e8 = 1 kW
- 19 07 d0 = 2 kW
- 19 0b b8 = 3 kW
- 19 0f a0 = 4 kW
- 19 13 88 = 5 kW
- 19 17 70 = 6 kW
- 19 1b 58 = 7 kW
- 19 1f 40 = 8 kW
- 19 23 28 = 9 kW
- 19 27 10 = 10 kW