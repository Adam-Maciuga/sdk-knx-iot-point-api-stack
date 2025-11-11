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

All applications uses KNX standardized datapoints, the corresponding __Functional Block__ defintions you can
find in folder 'apps/ems/data'

a. 07_80 Introduction v01.01.01 WGI
b. 07_80_01 Photovoltaics v01.01.01.pdf
c. 07_80_03 eMoblity v01.01.01.pdf

## 1. Inverter

The inverter demo represents an own end device with the the photovoltaics functionality. 

> The demo implements the 'PowerDC' datapoint from the Functional Block (b), reflecting the
  sun beam radiation. It is represented by a slider, which allows the user to 'simulate'
  the present DC power from 0 to 10 kW in steps of 1 kW.

## 2. Charger

The charger demo represents an own end device with the the charging functionality. 

> The demo implements the 'ActivePowerLimit' datapoint from the Functional Block (c), reflecting the
  charging consumption from the e-car. It is represented by a moving bar, which allows to 'simulate'
  the charging process. 

## 3. Customer Energy Manager

The customer energy manager demo represents an own end device with the the cem functionality. 

> The demo implements the (input) counterpart for the (output) 'PowerDC' datapoint from the Functional Block (b),
  the (output) counterpart for the (input) 'ActivePowerLimit' datapoint from the Functional Block (c), 
  and the (sun/mix) operation mode setting.

This customer energy manager demo is intended to be used in an end device, to be considered as a specific 
functionality at runtime in the context of energy management.

For including also client configuration features (alike ETS), there will be an alternative CEM demo. 
It covers both, the cem runtime functionality (this CEM demo) and client configuration features.

# Details 

In general:
- all four above mentioned datapoints are of the same type, being DPT: 14.056 AH: (see # References) 
- all three apps are based on the public KNX IoT stack and are at this stage only tested on Windows
- regarding the CEM demo app, see (???)

# References
- PV: description of the functional block(s), ref. latest released KNX Specifications: 7/8/1 Photovoltaics 
- Charger: description of the functional block(s): Application_EVSE AH: where to find? number is missing 
- Datapoints: DPT 14.056, ref. latest released KNX Specifications: 3/7/2 Datapoint Types 

## Commissioning

For the commissioning of these demo devices/apps two different scenarios need to be distinguished:
- without certification
- with certification

### Commissioning without certification: mini-client
This is the complete ETS commission procedure, which entirely of partly shall be implemented in as of client features:
- add/scan the certificate of the target device 
- scan the medium for the device's serial number or possible active programming mode
- SPAKE2+ onboarding to check the device's certificate (pre-shared key), an ex-factory reset of the target device might be required
- POST the WellKnownKnxIndividualAddress
- PUT the ProgrammingMode to False
- GET the IA (subnet address + device address) to check
- GET the ManufacturerID to check
- GET the HardwareType to check
- POST a WellKownKnx FactoryResetWithoutIA to reset the target device
- POST the LoadStateMachine to Unload
- GET the LoadStateMachine to check
- POST the LoadStateMachine to StartLoading
- POST all GroupObjectTable entries
- POST all RecipientTable entries
- POST all PublisherTable entries
- POST all AuthenticationTable entries
- PUT all Parameter values (if any)
- POST the LoadStateMachine to LoadComplete
- GET the LoadStateMachine to check
- GET the Fingerprint to check

AH: was not part of step 1-3, why here? 

### Commissioning with certification: ETS
- ETS (online) catalog entries need to be created for all three devices/apps by means of the KNX Manufacturer Tool 

## Group addresses

From the (data) functionality point of view, the three (virtual) device are linked by means of two group addresses:
- GA1 (0/0/1): links the 'Present DC power' datapoint (object) from the 'Inverter PV control' device **WITH** the 'Present DC power' datapoint (object) from the 'CEM' device
- GA2 (0/0/2): links the 'Charge rate power' datapoint (object) from the 'CEM' device **WITH** the 'Charge rate power' datapoint (object) from the 'Electric car charger' device

### IA (and serial number)

The IA of the three devices are set as follows, ETS notation:
-  PV: 15.15.15 (serial number = 00fa:1002:0b00)
-  CEM: 15.15.15 (serial number = 00fa:1002:0c00)
-  Charger: 15.15.15 (serial number = 00fa:1002:0d00)

### IID

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