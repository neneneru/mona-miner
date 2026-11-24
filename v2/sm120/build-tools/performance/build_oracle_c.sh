set -eu
repo=$1
shift
tool_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
src="$repo/v2/sm120/source/vendor/cpu_upstream"
scratch="$(mktemp -d /tmp/monaminer-oracle.XXXXXX)"
output="$(pwd)/oracle-c.json"
cp -- "$tool_dir/oracle.c" "$scratch/oracle.c"
cp -r -- "$src" "$scratch/vendor"
cd "$scratch"
gcc -O0 -fno-strict-aliasing -Ivendor oracle.c vendor/sph/blake.c vendor/sph/keccak.c \
    vendor/sph/cubehash.c vendor/sph/skein.c vendor/sph/bmw.c \
    vendor/lyra2/Lyra2.c vendor/lyra2/Sponge.c -o oracle
./oracle "$@" > "$output"
