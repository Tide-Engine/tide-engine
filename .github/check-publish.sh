#!/usr/bin/env bash
# GITHUB_TOKEN cannot authorize workflow changes; an optional publishing token can.
set -euo pipefail

if [ "${HAS_PUBLISH_TOKEN:-false}" = true ]; then
    exit 0
fi

default_ref="refs/remotes/origin/$DEFAULT_BRANCH"
git fetch --no-tags origin "+refs/heads/$DEFAULT_BRANCH:$default_ref"
selected_workflows=$(git rev-parse "$GITHUB_SHA:.github/workflows")
default_workflows=$(git rev-parse "$default_ref:.github/workflows")
if [ "$selected_workflows" != "$default_workflows" ]; then
    echo "::error::This commit's workflows differ from $DEFAULT_BRANCH. Add repository secret TIDE_PUBLISH_TOKEN, scoped to this repository with Contents: write and Workflows: write, before publishing. The default GITHUB_TOKEN cannot authorize workflow changes."
    exit 1
fi
