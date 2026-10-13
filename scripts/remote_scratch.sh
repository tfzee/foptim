#!/usr/bin/env bash
#   scripts/remote_scratch.sh [-H user@host] [-l LOCAL_FOPTIM] [-r REMOTE_PROJECTS_DIR] [-- fb scratch args...]
# WARNING: assumes paths and shit gotta define it or it breaks
# The remote foptim working tree (incl. .git, except its config/hooks) is made identical to the local one.
set -euo pipefail

host="${FB_REMOTE:-tim@shrex}"
local_repo="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
remote_dir="projects"

while getopts "H:l:r:h" o; do
  case $o in
    H) host=$OPTARG ;;
    l) local_repo=$OPTARG ;;
    r) remote_dir=$OPTARG ;;
    *) sed -n '2,6p' "$0"; exit 1 ;;
  esac
done
shift $((OPTIND - 1))
[ "${1:-}" = "--" ] && shift

local_repo="$(cd "$local_repo" && pwd)"
sock="$(mktemp -u /tmp/fb-ssh-XXXXXX)"
ssh_opts=(-o ControlMaster=auto -o ControlPath="$sock" -o ControlPersist=10m)
trap 'ssh "${ssh_opts[@]}" -O exit "$host" 2>/dev/null || true' EXIT

echo "== connecting to $host"
ssh "${ssh_opts[@]}" "$host" true

echo "== checking remote layout"
ssh "${ssh_opts[@]}" "$host" bash -s -- "$remote_dir" <<'REMOTE'
d=$1; fail=0
for r in foptim foptim_test; do
  [ -d "$d/$r/.git" ] || { echo "error: $d/$r is missing or not a git checkout" >&2; fail=1; }
done
[ -x "$d/foptim_test/fb" ] || { echo "error: $d/foptim_test/fb not found or not executable" >&2; fail=1; }
exit $fail
REMOTE

[ -d "$local_repo/.git" ] || { echo "error: $local_repo is not a git repo" >&2; exit 1; }

echo "== rsync $local_repo -> $host:$remote_dir/foptim"
rsync -az --delete --info=stats1 -e "ssh ${ssh_opts[*]}" \
  --filter=':- .gitignore' \
  --exclude='/.git/config' --exclude='/.git/hooks' --exclude='/build/' --exclude='/.direnv/' --exclude='/.cache/' \
  "$local_repo/" "$host:$remote_dir/foptim/"

echo "== scratch run"
ssh -t "${ssh_opts[@]}" "$host" "cd $remote_dir/foptim_test && nix develop -c ./fb scratch ${*:---wait}"
