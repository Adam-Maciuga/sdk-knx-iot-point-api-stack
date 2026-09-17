"""Wire-level clients the EITT interpreter drives.

Everything here speaks a protocol to the device under test and knows
nothing about EITT's XML: CoAP over UDP (`coap_client`), the OSCORE
security layer (`knx_oscore`), the SPAKE2+ handshake (`knx_spake2plus`),
and multicast DNS discovery (`mdns_client`).
"""
