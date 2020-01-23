#!/bin/bash
set -euo pipefail

REVISION=$(git rev-parse HEAD)

docker images

docker login -u=$DHUBU -p=$DHUBP

if [[ ${BUILDKITE_BRANCH} == "master" ]]; then
    docker tag cyberway/contracts:${REVISION} cyberway/contracts:stable
    docker push cyberway/contracts:stable
elif [[ ${BUILDKITE_BRANCH} == "develop" ]]; then
    docker tag cyberway/contracts:${REVISION} cyberway/contracts:latest
    docker push cyberway/contracts:latest
else
    docker tag cyberway/contracts:${REVISION} cyberway/contracts:${BUILDKITE_BRANCH}
    docker push cyberway/contracts:${BUILDKITE_BRANCH}
fi



