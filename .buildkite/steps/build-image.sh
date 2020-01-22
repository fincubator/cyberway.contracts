#!/bin/bash
set -euo pipefail

IMAGETAG=${BUILDKITE_BRANCH:-master}
BRANCHNAME=${BUILDKITE_BRANCH:-master}
REVISION=$(git rev-parse HEAD)

if [[ "${IMAGETAG}" == "alfa" ]]; then
    BUILDTYPE="alfa"
else
    BUILDTYPE="latest"
fi

docker build -t cyberway/cyberway.contracts:${IMAGETAG} --build-arg version=${REVISION} --build-arg buildtype=${BUILDTYPE} -f Docker/Dockerfile .
