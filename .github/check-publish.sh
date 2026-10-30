#!/usr/bin/env bash
# A tag may reuse workflows at a current branch tip without Workflows write.
# Historical workflows absent from every branch tip need the optional token.
set -euo pipefail

if [ "${HAS_PUBLISH_TOKEN:-false}" = true ]; then
    exit 0
fi

git fetch --prune --no-tags origin '+refs/heads/*:refs/remotes/origin/*'
selected_workflows=$(git rev-parse "$GITHUB_SHA:.github/workflows")
while IFS=' ' read -r branch_ref branch_commit; do
    branch_workflows=$(git rev-parse "$branch_commit:.github/workflows" 2>/dev/null) || continue
    if [ "$selected_workflows" = "$branch_workflows" ]; then
        echo "Publishing workflows match $branch_ref."
        exit 0
    fi
done < <(git for-each-ref --format='%(refname) %(objectname)' refs/remotes/origin/)

echo "::error::This commit's workflows are absent from every current branch tip. Add repository secret TIDE_PUBLISH_TOKEN, scoped to this repository with Contents: write and Workflows: write, to publish this historical snapshot."
exit 1
