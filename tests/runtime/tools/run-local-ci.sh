#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────
# Run the runtime conformance test suite locally, replicating CI.
#
# Usage (from repo root, PowerShell):
#   git archive --format=tar HEAD | docker run --rm -i --privileged itgesellschaft/knxiot-knx-ci:latest bash -c "mkdir /w && tar -xf - -C /w && cd /w && bash tests/runtime/run-local-ci.sh [pytest-args...]"
#
# Examples:
#   # Run only test_group_comm.py:
#   git archive --format=tar HEAD | docker run --rm -i --privileged itgesellschaft/knxiot-knx-ci:latest bash -c "mkdir /w && tar -xf - -C /w && cd /w && bash tests/runtime/run-local-ci.sh tests/runtime/runtime_tests/test_group_comm.py"
#
#   # Run a single test class:
#   git archive --format=tar HEAD | docker run --rm -i --privileged itgesellschaft/knxiot-knx-ci:latest bash -c "mkdir /w && tar -xf - -C /w && cd /w && bash tests/runtime/run-local-ci.sh 'tests/runtime/runtime_tests/test_group_comm.py::TestOwnGroupObjectUpdate' -v"
#
# NOTE: git archive uses HEAD, so commit your changes first.
#       To include uncommitted work, use tar piping instead:
#   tar -cf - --exclude=build --exclude=.git . | docker run ...
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

echo "=== Setting up veth pair ==="
apt-get update -qq && apt-get install -y -qq iproute2 > /dev/null 2>&1
ip link add veth-dut type veth peer name veth-test
ip link set veth-dut up
ip link set veth-test up
sleep 2  # wait for IPv6 DAD / link-local

DUT_ADDR=$(ip -6 -o addr show dev veth-dut scope link | awk '{print $4}' | cut -d/ -f1)
echo "DUT address=${DUT_ADDR} on veth-dut / veth-test"

echo "=== Building runtime_test_server ==="
cmake --preset=linux-test-gcc \
  -DOC_DEBUG_ENABLED=OFF \
  -DOC_PRINT_ENABLED=OFF \
  -DOC_DEBUG_OSCORE_ENABLED=OFF
cmake --build --preset=linux-test-gcc --target runtime_test_server

echo "=== Installing Python dependencies ==="
curl -sSL https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py
python3 /tmp/get-pip.py --break-system-packages --quiet
python3 -m pip install --break-system-packages --quiet \
  -r tests/runtime/requirements.txt

# Default: the whole runtime tier (same as CI); conftest.py orders the
# modules.  Override by passing args.
if [ $# -eq 0 ]; then
  set -- tests/runtime/runtime_tests/
fi

echo "=== Running tests ==="
exec env \
  RUNTIME_TEST_SERVER=build/linux-test-gcc/tests/runtime_test_server \
  DUT_IFACE=veth-dut \
  DEVICE_HOST="$DUT_ADDR" \
  DEVICE_IFACE=veth-test \
  RUNTIME_TEST_QUIET=1 \
  KNX_TEST_DEBUG_OSCORE=0 \
  KNX_TEST_DEBUG_COAP=0 \
  python3 -m pytest "$@" -v --tb=short
