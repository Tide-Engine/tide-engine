#!/usr/bin/env bash
set -euo pipefail

key="${RUNNER_TEMP:?}/llvm-snapshot.gpg.key"
url=https://apt.llvm.org/llvm-snapshot.gpg.key
flags=(-4 -fsSL --retry 4 --retry-all-errors --retry-max-time 120 --connect-timeout 15 --max-time 30)
if getent ahostsv4 apt.llvm.org > /dev/null; then
  curl "${flags[@]}" -o "$key" "$url"
else
  echo "::warning::Runner DNS cannot resolve apt.llvm.org; using DNS over HTTPS for this host."
  # Authenticate both the resolver and LLVM with their ordinary TLS checks.
  # The GET response can reuse the resolver's cached answer during an upstream
  # authority outage; a failed/empty answer still stops setup.
  dns="${RUNNER_TEMP}/llvm-dns.json"
  curl "${flags[@]}" -H 'accept: application/dns-json' -o "$dns" \
    'https://cloudflare-dns.com/dns-query?name=apt.llvm.org&type=A'
  address=$(python3 -c 'import ipaddress, json, sys
data = json.load(open(sys.argv[1]))
if data.get("Status") != 0: raise SystemExit("LLVM DNS lookup failed")
addresses = [str(ipaddress.IPv4Address(a["data"])) for a in data.get("Answer", []) if a.get("type") == 1]
if not addresses: raise SystemExit("LLVM DNS lookup returned no IPv4 address")
print(addresses[0])' "$dns")
  curl "${flags[@]}" --resolve "apt.llvm.org:443:$address" -o "$key" "$url"
  # apt uses the runner resolver, too. Limit this ephemeral mapping to LLVM;
  # package HTTPS and apt signatures still authenticate all subsequent bytes.
  printf '%s apt.llvm.org\n' "$address" | sudo tee -a /etc/hosts > /dev/null
fi
sudo install -m 644 "$key" /etc/apt/trusted.gpg.d/apt.llvm.org.asc
