#!/usr/bin/env bash
set -euo pipefail

key="${RUNNER_TEMP:?}/llvm-snapshot.gpg.key"
url=https://apt.llvm.org/llvm-snapshot.gpg.key
flags=(-4 -fsSL --retry 4 --retry-all-errors --retry-max-time 120 --connect-timeout 15 --max-time 30)
if getent ahostsv4 apt.llvm.org > /dev/null; then
  curl "${flags[@]}" -o "$key" "$url"
else
  echo "::warning::Runner DNS cannot resolve apt.llvm.org; trying LLVM's CDN hostname."
  # apt.llvm.org's observed CNAME (2026-11-03), independently resolvable while
  # llvm.org's authority is unavailable. --connect-to keeps LLVM's SNI, Host and
  # certificate verification; reject redirects and proxy peer addresses.
  address=$(curl "${flags[@]}" \
    --connect-to apt.llvm.org:443:dualstack.l2.shared.us-eu.fastly.net:443 \
    --noproxy apt.llvm.org --max-redirs 0 --write-out '%{remote_ip}' -o "$key" "$url")
  python3 -c 'import ipaddress, sys; ipaddress.IPv4Address(sys.argv[1])' "$address"
  # apt uses the runner resolver, too. Limit this ephemeral mapping to LLVM;
  # package HTTPS and apt signatures still authenticate all subsequent bytes.
  printf '%s apt.llvm.org\n' "$address" | sudo tee -a /etc/hosts > /dev/null
fi
sudo install -m 644 "$key" /etc/apt/trusted.gpg.d/apt.llvm.org.asc
