#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Run the device test tiers in Docker from a Linux, macOS or Git Bash shell.
# The bash twin of run-in-docker.ps1: packs the source tree, starts the CI
# image privileged (it creates a network namespace and a veth pair), and
# hands it tests/common/tools/run-local-ci.sh, which does what the GitLab
# jobs runtime-gcc / eitt-gcc do.
#
# Usage (from anywhere inside the repository):
#   tests/common/tools/run-in-docker.sh [--tier all|eitt|runtime|harness]
#                                       [--section 5.x]... [--uncommitted]
#                                       [--no-cache] [--image IMAGE]
#                                       [--preset linux-test-gcc]
#                                       [--server-logs] [--coap-debug]
#                                       [--oscore-debug] [--group-debug]
#                                       [--time-scale 0.05] [--slow]
#                                       [--] [pytest-args...]
#
#   --tier          eitt | runtime | harness | all (default: both device tiers,
#                   each in its own pytest process against a fresh DUT).
#   --section 5.x   Whole EITT section(s); repeatable. Prefer this over
#                   `-k 5.6`, which substring-matches 5.5.5.6 as well.
#   --uncommitted   Pack the working tree (tracked + untracked, .gitignore
#                   respected) instead of committed HEAD, which is the default.
#   --no-cache      Skip the persistent build/pip cache volumes (knx-rt-build,
#                   knx-rt-pip) and build from scratch: slower, but the way to
#                   rule out a stale artifact after a preset change.
#   Everything after the options (or after `--`) goes to pytest unchanged.
#
# Examples:
#   tests/common/tools/run-in-docker.sh                       # eitt + runtime, HEAD
#   tests/common/tools/run-in-docker.sh --tier eitt --section 5.6 --uncommitted
#   tests/common/tools/run-in-docker.sh --tier runtime -- tests/runtime/test_observe.py -x
#   tests/common/tools/run-in-docker.sh --tier harness --uncommitted
#
# Exit status is pytest's (the worst one across the tiers for --tier all).
# ---------------------------------------------------------------------------
set -euo pipefail

IMAGE=itgesellschaft/knxiot-knx-ci:latest
UNCOMMITTED=0
NOCACHE=0
INNER_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --uncommitted|--working-tree|--dirty) UNCOMMITTED=1; shift ;;
    --no-cache)     NOCACHE=1; shift ;;
    --image)        IMAGE="$2"; shift 2 ;;
    --image=*)      IMAGE="${1#*=}"; shift ;;
    -h|--help)      sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --)             INNER_ARGS+=("$@"); break ;;
    *)              INNER_ARGS+=("$1"); shift ;;
  esac
done

REPO_ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# Pack the tree into a tar the container unpacks into its own filesystem:
# no bind mount, so no stale CMake caches, CRLF surprises or try-compile
# failures from Docker Desktop's file sharing. Windows: Git Bash's tar cannot
# be used for the list-driven archive, so prefer the system tar.
TAR=tar
if [ -x /c/Windows/System32/tar.exe ]; then TAR=/c/Windows/System32/tar.exe; fi
PKG="$(mktemp -t knx-runtime-XXXXXX.tar)"
trap 'rm -f "$PKG" "$PKG.list"' EXIT
if [ "$UNCOMMITTED" = 1 ]; then
  git ls-files --cached --others --exclude-standard > "$PKG.list"
  "$TAR" -cf "$PKG" -T "$PKG.list"
  SOURCE="working tree (uncommitted + untracked)"
else
  git archive --format=tar -o "$PKG" HEAD
  SOURCE="committed HEAD"
fi

MOUNTS=()
if [ "$NOCACHE" = 0 ]; then
  MOUNTS=(-v knx-rt-build:/w/build -v knx-rt-pip:/root/.cache/pip)
fi

# The inner script is read from the tar itself, so the version that runs is
# the one in the packed tree (HEAD or working tree, as selected above).
# sed strips carriage returns first: a CRLF working copy of the scripts (an editor,
# or a checkout made before .gitattributes forced eol=lf on *.sh) would otherwise
# fail in bash with "$'\x0D': command not found".
INNER='mkdir -p /w && tar -xf - -C /w && cd /w && sed -i "s/\x0D//g" tests/common/tools/*.sh && bash tests/common/tools/run-local-ci.sh "$@"'

echo "=== Running device tests in Docker ($IMAGE) ==="
echo "    Repo:   $REPO_ROOT"
echo "    Branch: $(git branch --show-current)"
echo "    Source: $SOURCE"
echo "    Args:   ${INNER_ARGS[*]:-(defaults: --tier all)}"
set +e
docker run --rm -i --privileged --sysctl net.ipv6.conf.all.disable_ipv6=0 \
  "${MOUNTS[@]}" "$IMAGE" bash -c "$INNER" _ "${INNER_ARGS[@]}" < "$PKG"
rc=$?
set -e
if [ "$rc" = 0 ]; then echo "=== PASSED (exit 0) ==="; else echo "=== FAILED (exit $rc) ==="; fi
exit "$rc"
