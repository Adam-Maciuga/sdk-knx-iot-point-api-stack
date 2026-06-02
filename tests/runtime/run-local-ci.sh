#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────
# Run the runtime conformance test suite locally, replicating CI.
#
# Usage (from repo root, PowerShell):
#   git archive --format=tar HEAD | docker run --rm -i --privileged luftd/knx-ci:latest bash -c "mkdir /w && tar -xf - -C /w && cd /w && bash tests/runtime/run-local-ci.sh [pytest-args...]"
#
# Examples:
#   # Run only test_5_4_group_comm.py:
#   git archive --format=tar HEAD | docker run --rm -i --privileged luftd/knx-ci:latest bash -c "mkdir /w && tar -xf - -C /w && cd /w && bash tests/runtime/run-local-ci.sh tests/runtime/test_5_4_group_comm.py"
#
#   # Run a single test class:
#   git archive --format=tar HEAD | docker run --rm -i --privileged luftd/knx-ci:latest bash -c "mkdir /w && tar -xf - -C /w && cd /w && bash tests/runtime/run-local-ci.sh 'tests/runtime/test_5_4_group_comm.py::TestOwnGroupObjectUpdate' -v"
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

# Default test files (same as CI) — override by passing args
if [ $# -eq 0 ]; then
  set -- \
    tests/runtime/test_5_1_discovery.py \
    tests/runtime/test_5_1_discovery_multicast.py \
    tests/runtime/test_5_1_discovery_extended.py \
    tests/runtime/test_5_2_wellknown_knx.py \
    tests/runtime/test_5_2_fingerprint.py \
    tests/runtime/test_5_2_device_resources.py \
    tests/runtime/test_5_2_swu.py \
    tests/runtime/test_5_3_security.py \
    tests/runtime/test_5_4_group_comm.py \
    tests/runtime/test_5_5_fp_tables.py \
    tests/runtime/test_5_6_app_program.py \
    tests/runtime/test_5_7_functional_blocks.py \
    tests/runtime/test_5_8_parameters.py \
    tests/runtime/test_5_10_generic.py \
    tests/runtime/test_5_1_discovery_mdns.py \
    tests/runtime/test_5_2_reset.py
fi

echo "=== Running tests ==="
exec env \
  RUNTIME_TEST_SERVER=build/linux-test-gcc/tests/runtime_test_server \
  DUT_IFACE=veth-dut \
  DEVICE_HOST="$DUT_ADDR" \
  DEVICE_IFACE=veth-test \
  RUNTIME_TEST_QUIET=1 \
  python3 -m pytest "$@" -v --tb=short
