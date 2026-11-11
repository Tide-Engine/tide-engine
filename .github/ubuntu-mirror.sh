#!/usr/bin/env bash
set -euo pipefail

# A hosted runner's apt goes through a list of Ubuntu's own archive hosts
# (azure.archive.ubuntu.com, archive.ubuntu.com, security.ubuntu.com). While
# the runner's resolver answers for none of them, apt has nowhere to fetch
# from: kernel.org's mirror of the same archive stands in. apt still checks
# every index against Ubuntu's signing keys, so what's installed is the same.
# With any of the hosts resolving, or no such list, nothing changes.
list=${TIDE_APT_MIRRORS:-/etc/apt/apt-mirrors.txt}
mirror=mirrors.edge.kernel.org
[[ -f "$list" ]] || exit 0
hosts=$(sed -nE 's#^https?://([^/[:space:]]+).*#\1#p' "$list")
for host in $hosts; do
  if getent ahostsv4 "$host" > /dev/null; then exit 0; fi
done
echo "::warning::Runner DNS cannot resolve Ubuntu's archive hosts; using $mirror's mirror of the archive."
getent ahostsv4 "$mirror" > /dev/null
printf 'https://%s/ubuntu/\tpriority:1\n' "$mirror" | sudo tee "$list" > /dev/null
