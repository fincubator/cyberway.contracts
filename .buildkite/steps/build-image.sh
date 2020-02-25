#!/bin/bash
set -euo pipefail

REVISION=$(git rev-parse HEAD)
MASTER_REVISION=$(git rev-parse origin/master)

echo $REVISION
echo $MASTER_REVISION
if [[ "${REVISION}" == "${MASTER_REVISION}" ]]; then
    BUILDTYPE="stable"
else
    BUILDTYPE="latest"
fi
echo $BUILDTYPE

CDT_TAG=${CDT_TAG:-$BUILDTYPE}
CW_TAG=${CW_TAG:-$BUILDTYPE}
BUILDER_TAG=${BUILDER_TAG:-$BUILDTYPE}

docker build -t cyberway/cyberway.contracts:${REVISION} --build-arg=version=${REVISION} --build-arg=cw_tag=${CW_TAG} --build-arg=cdt_tag=${CDT_TAG}  --build-arg=builder_tag=${BUILDER_TAG} -f Docker/Dockerfile .
