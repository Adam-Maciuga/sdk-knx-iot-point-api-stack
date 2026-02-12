"""
KNX IoT Device Discovery using mDNS/DNS-SD.

Based on KNX IoT Point API specification (3_10_5).
KNX IoT devices advertise themselves using mDNS with the service type '_knx._udp.local.'
"""
import logging
import time
from typing import Optional, List, Dict, Mapping, Union
from zeroconf import ServiceBrowser, ServiceListener, Zeroconf, ServiceInfo

logger = logging.getLogger(__name__)

class KNXIoTDevice:
    """Represents a discovered KNX IoT device."""

    def __init__(
            self,
            name: str,
            addresses: List[str],
            port: int,
            properties: Mapping[Union[str, bytes], bytes],
    ):
        self.name = name
        self.addresses = addresses
        self.port = port
        self.properties = properties
        # Extract serial number from service name
        self.serial_number = (
            name.split('._knx._udp.local.')[0] if '._knx._udp.local.' in name else None
        )
        # Extract optional TXT record properties
        self.sleep_period = self._get_property('SP')  # Sleep period in ms
        self.role = self._get_property('rl')  # Device role
        self.mac_address = self._get_property('mac')  # MAC address
        self.eui64 = self._get_property('eui')  # IEEE EUI-64 identifier

    def _get_property(self, key: str) -> Optional[str]:
        """Get a property value from the mDNS TXT record."""
        # Try both string and bytes keys
        if key in self.properties:
            return self.properties[key].decode('utf-8', errors='ignore')
        # Also try bytes key
        key_bytes = key.encode('utf-8')
        if key_bytes in self.properties:
            return self.properties[key_bytes].decode('utf-8', errors='ignore')
        return None

    def __str__(self):
        """String representation of the device."""
        addr_str = ', '.join(self.addresses)
        lines = [
            f"KNX IoT Device: {self.name}",
        ]

        # Add serial number
        if self.serial_number:
            lines.append(f"  Serial Number: {self.serial_number}")

        lines.extend([
            f"  Addresses: {addr_str}",
            f"  Port: {self.port}"
        ])

        # Add optional properties if available
        if self.mac_address:
            lines.append(f"  MAC: {self.mac_address}")
        if self.eui64:
            lines.append(f"  EUI-64: {self.eui64}")
        if self.role:
            lines.append(f"  Role: {self.role}")
        if self.sleep_period:
            lines.append(f"  Sleep Period: {self.sleep_period}ms")

        return '\n'.join(lines)

class KNXIoTDiscoveryListener(ServiceListener):
    """Listener for KNX IoT device mDNS announcements."""

    def __init__(self, serial_number_filter: Optional[str] = None):
        self.devices: List[KNXIoTDevice] = []
        self.serial_number_filter = serial_number_filter
        self.zeroconf: Optional[Zeroconf] = None

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Called when a service is discovered."""
        logger.debug("Service discovered: %s", name)
        info = zc.get_service_info(type_, name)
        if info:
            self._process_service(info)
        else:
            logger.warning("Could not get service info for: %s", name)

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Called when a service is updated."""
        logger.debug("Service updated: %s", name)
        info = zc.get_service_info(type_, name)
        if info:
            self._process_service(info)

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """
        Called when a service is removed.

        Args:
            zc: Zeroconf instance (unused but required by interface)
            type_: Service type (unused but required by interface)
            name: Service name to remove
        """
        _ = zc  # Mark as intentionally unused
        _ = type_  # Mark as intentionally unused
        logger.debug("Service removed: %s", name)
        # Remove device from list if it exists
        self.devices = [d for d in self.devices if d.name != name]

    def _process_service(self, info: ServiceInfo) -> None:
        """Process discovered service information."""
        # Extract addresses
        addresses = list(info.parsed_addresses())

        # Extract properties from TXT records
        properties: Dict[Union[str, bytes], bytes] = {
            k: v for k, v in (info.properties or {}).items() if v is not None
        }

        # Get serial number from properties or service name
        serial_number = None
        if b'serialnumber' in properties:
            value = properties[b'serialnumber']
            if value is not None:
                serial_number = value.decode('utf-8', errors='ignore')
        if not serial_number:
            # Extract from service name (format: <serialnumber>._knx._udp.local.)
            if info.name and '._knx._udp.local.' in info.name:
                serial_number = info.name.split('._knx._udp.local.')[0]

        logger.info("Discovered device: %s at %s", serial_number, addresses)

        # Filter by serial number if specified
        if self.serial_number_filter:
            if serial_number != self.serial_number_filter:
                logger.debug(
                    "Skipping device %s - not matching %s",
                    serial_number, self.serial_number_filter
                )
                return

        # Create device object
        device = KNXIoTDevice(
            name=info.name,
            addresses=addresses,
            port=info.port or 3671,
            properties=properties
        )

        # Add to list if not already present
        if not any(d.name == device.name for d in self.devices):
            self.devices.append(device)
        else:
            logger.debug("Device %s already in list", device.name)

def discover_knx_iot_devices(
    timeout: float = 5.0,
    serial_number: Optional[str] = None
) -> List[KNXIoTDevice]:
    """
    Discover KNX IoT devices on the network using mDNS.

    Args:
        timeout: How long to listen for devices (in seconds)
        serial_number: Optional serial number to filter for a specific device

    Returns:
        List of discovered KNXIoTDevice objects
    """
    logger.info("Starting mDNS discovery (timeout: %ss)", timeout)
    if serial_number:
        logger.info("Looking for device: %s", serial_number)

    # KNX IoT service type as per specification
    service_type = "_knx._udp.local."

    try:
        # Create zeroconf instance
        zeroconf = Zeroconf()

        # Create listener
        listener = KNXIoTDiscoveryListener(serial_number_filter=serial_number)
        listener.zeroconf = zeroconf

        # Browse for services
        browser = ServiceBrowser(zeroconf, service_type, listener)

        try:
            # Wait for specified timeout
            time.sleep(timeout)
        finally:
            # Cleanup
            browser.cancel()
            zeroconf.close()

        logger.info("Discovery completed. Found %s device(s)", len(listener.devices))
        return listener.devices

    except Exception as e:
        logger.error("Error during mDNS discovery: %s", str(e), exc_info=True)
        raise

def discover_device_by_serial(serial_number: str, timeout: float = 5.0) -> Optional[KNXIoTDevice]:
    """
    Discover a specific KNX IoT device by serial number.

    Args:
        serial_number: The serial number of the device to find
        timeout: How long to search (in seconds)

    Returns:
        KNXIoTDevice if found, None otherwise
    """
    devices = discover_knx_iot_devices(timeout=timeout, serial_number=serial_number)
    return devices[0] if devices else None
