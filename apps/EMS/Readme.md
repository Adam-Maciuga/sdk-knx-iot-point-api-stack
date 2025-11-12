# Introduction

This folder contains the Energy Management System (EMS) demo apps. 

The use case is to charge an e-car with a power of 4 kW DC, moreover a user can decide 
for two operation modes.

- In **Sun** mode the e-car is charged exlusivly with 'green' solar energy, if the availabe solar 
  power drops below the threshold of 4 kW (e.g.; cloudy weather) the charging of th e-car is paused.
- In **Mix** mode the charing will never be stopped unless the e-car is fully charged. 
  Hence the charged energy is a mix of solar energy and grid energy. In worst case all energy is 
  retrieved from the grid (e.g; during night), in best case all energy is 'green' solar energy.   

The charging mode can be set by th euser on the Customer Energy Manager (CEM).The below picture 
illustrates the use case.

![Concept](concept.png)

The following functionalities are involved in this demo use case. 

1. Inverter (provides solar energy)
2. Charger (consumes energy)
3. Customer Energy Manager (manages energy) 

> Note that the grid energy as such is not modelled in this demo. For simplification it is assumed 
  that grid energy is (always) available. 

In a typical installtion the above defined three functionalities can be shared amongst several (end) devices.
For example, the CEM can be a standalone device or the functionality is part of the 'Charger' (end) device. 
In a usual installation with a staionary battery or heat pump the CEM is often part in one of those devices.

# References
All applications uses KNX standardized datapoints, the corresponding __Functional Block__ and 
__Datapoint Type__ defintions you can find in folder 'apps/ems/data'

a. 07_80 Introduction 
b. 07_80_01 Photovoltaics 
c. 07_80_03 eMoblity 
d. 03_07_02 Datapoint Types 

# 1. Inverter

The inverter demo represents an own end device with the the photovoltaics functionality. 

> The demo implements the 'PowerDC' datapoint from the Functional Block (b), reflecting the
  sun beam radiation. It is represented by a slider, which allows the user to 'simulate'
  the present DC power from 0 to 10 kW in steps of 1 kW.

# 2. Charger

The charger demo represents an own end device with the the charging functionality. 

> The demo implements the 'ActivePowerLimit' datapoint from the Functional Block (c), reflecting the
  charging consumption from the e-car. It is represented by a moving bar, which allows to 'simulate'
  the charging process. 

# 3. Customer Energy Manager

The customer energy manager demo represents an own end device with the the cem functionality. 

> The demo implements the (input) counterpart for the (output) 'PowerDC' datapoint from the Functional Block (b),
  the (output) counterpart for the (input) 'ActivePowerLimit' datapoint from the Functional Block (c), 
  and the (sun/mix) operation mode setting.

# Details 

## Datapooints

All above for the runtime relevant mentioned i/o datapoints are of the same type, being DPT 14.056, 
for this see 03/07/02. 

## Commissioning

This demo show a specific EMS functionality at runtime on end devices. In KNX IoT several commissioning 
methods exist to establish a runtime communication between devices. For this a client (tool) needs to 
support them.  

- __S-Mode__ (devices communicate over a fix endpoint, with group addresses)
- __Publish/Subscribe__ (devices communicate over one or more endpoints, with subsribtion and notification mechanism 
     ([CoAP RFC7641](https://www.rfc-editor.org/rfc/rfc7641.html)) 

Both commissioning procedures are desscribed as part of the KNX IoT specification 03/10/05. 
Client 'ETS' has implemented the S-Mode.
 
### S-Mode

The following commissioning steps are defined. For a (simple) installation of only a few devcies some steps may not 
be needed (marked). 

1. Device Discovery 
   a. add/scan in client the target device certificate (such as a QR Code) 
   b. resolve device IPv6 address and port by serial number (CoAP multicast/mDNS discovery)
   c. retrieve device functional block information (CoAP unicast discovery) 
2. Device Preparation 
   a. initial onboarding via device certificate (well-known/knx/spake, SPAKE2+) 
   b. check and set individual address (well-known/knx/ia)
   c. check and reset device programming mode (dev/pm) -> optional
   d. check manufuacturer id (dev/mid)
   e. check hardware type (dev/hwt)
   f. reset target device (well-known/knx) -> optional
3. Device Download
   a. check and set the load state machine (a/lsm)
   b. write Group Object table entries
   c. write Recipient table entries
   d. write Publisher table entries
   e. write Access Token entries
   f. write parameter values -> optional 
   g. check and close the load state machine (a/lsm)
4. Device Finishing
   a. read fingerprint and store in client (well-known/knx/f) -> optional
   b. restart device (well-known/knx) -> optional 

In case of commissioning with ETS a catalog entry is need for a device, to be created by means
of the KNX Manufacturer Tool. 

#### Group Addresses

In a simple installation usually only one device is present for a specific functionality, for example in this demo 
a single inverter functionality (and device). The inverter 'PowerDC' output datapoint maps 1:1 to the counterpart CEM 
input datapoint. Hence this, a user assigment of group addresses is not needed, a commissioning client can assign them 
by own means. For more complex installations/scenarios a user interaction is needed, this is not considered by this demo.  

#### Individual Addresses (IA)/ Serial Numbers (SN) / Installation ID (IID)

The used SN's (aribtrary) and IA's are set as follows (see also the corresponing *.c file).
-  Inverter: IA= 15.15.15, SN = 00fa:1002:0b00
-  CEM: IA= 15.15.15, SN = 00fa:1002:0c00
-  Charger: IA= 15.15.15, SN = 00fa:1002:0d00

The IID of the three devices is set to: 00fa:0000

### Group Object Table

The Group Object Tables of the three devices are set as follows:
  
- **PV**:
   -  url: '/p/pv'      cflags : '64' ...t.  ga : [ 0/0/1 ]
   
- **CEM**: 
   -  url: '/p/pv'      cflags : '16' .w...  ga : [ 0/0/1 ]
   -  url: '/p/charger' cflags : '64' ...t.  ga : [ 0/0/2 ]
  
- **Charger**:
   -  url: '/p/charger' cflags : '16' .w...  ga : [ 0/0/2 ]
  
### Publisher and Recipient Table

Both the Publisher and Recipeint Tables of the three devices are set as follows:
  
- **PV**:
   -  grpid:  00fa:0000  ga : [ 0/0/1 ]

- **CEM**: 
   -  grpid:  00fa:0000  ga : [ 0/0/1 0/0/2 ]
  
- **Charger**:
   -  grpid:  00fa:0000  ga : [ 0/0/2 ]
  
### Authentication Table

The Authentication Tables of the three devices are set as follows:
  
- **PV**:
   -  ga 0/0/1 osc_id [2]: 0001  osc_ms [16]: 00000000000000000000000000000000  osc_contextid (o)[6]: 000000000000
  
- **CEM**: 
   -  ga 0/0/1 osc_id [2]: 0001  osc_ms [16]: 000102030405060708090a0b0c0d0e0f  osc_contextid (o)[6]: 10020c000001
   -  ga 0/0/2 osc_id [2]: 0002  osc_ms [16]: 000102030405060708090a0b0c0d0e0f  osc_contextid (o)[6]: 10020c000002
  
- **Charger**:
   -  ga 0/0/2 osc_id [2]: 0002  osc_ms [16]: 00000000000000000000000000000000  osc_contextid (o)[6]: 000000000000

# Testing with Wireshark

To test with Wireshark (Windows) launch all three devices, move the slider to any value, **OSCORE** frames shall appear on the ethernet/wifi NIC.

The destination address of these captured frames shall be set to this IPv6 multicast address 'ff32:0030:fd00:00fa:0000:0000:00fa:0000'.

This destination address is composed as follows:
  - scope: fixed to ff32
  - ??: fixed to 0030:fd
  - iid: fixed to 00fa:0000
  - grpid: fixed to 00fa:0000

By adding the following OSCORE security contexts the actual payload becomes visible (decrypted):
- Sender ID = 0001, skip Recipient ID, Master Secret = 000102030405060708090a0b0c0d0e0f, skip Master Salt, ID Context = 10020c000001, keep default Algorithm
- Sender ID = 0002, skip Recipient ID, Master Secret = 000102030405060708090a0b0c0d0e0f, skip Master Salt, ID Context = 10020c000002, keep default Algorithm

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