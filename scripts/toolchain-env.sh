#!/usr/bin/env bash
# Install a toolchain of the platforms benchmark (Linux and macOS) and print
# the environment that builds with it, one shell-quoted VAR=value line per variable
# (CXX, CXXFLAGS and, on macOS, SDKROOT), to be read with
#
#     eval "$(scripts/toolchain-env.sh gcc16)"
#
# The toolchains are those of the EasyLocal CI: gcc16, clang23-libstdcxx and
# clang23-libcxx on Linux (Clang 23 from apt.llvm.org), appleclang on macOS.
# Windows has no script: its two toolchains (clang-cl, msvc) come from the
# Visual Studio environment that the workflow exports.
set -euo pipefail

toolchain="${1:?usage: toolchain-env.sh TOOLCHAIN}"

apt_install() {
    sudo env DEBIAN_FRONTEND=noninteractive \
        apt-get install -y --no-install-recommends "$@" >&2
}

case "$(uname -s)" in
    Linux)
        case "$toolchain" in
            gcc16)
                apt_install g++-16
                echo 'CXX=/usr/bin/g++-16'
                echo "CXXFLAGS=''"
                ;;
            clang23-libstdcxx|clang23-libcxx)
                # Clang 23 is not in Ubuntu: it comes from the LLVM project's
                # own repository, apt.llvm.org, for this release of Ubuntu.
                if [ ! -f /etc/apt/sources.list.d/llvm-23.list ]; then
                    codename="$(. /etc/os-release && echo "$VERSION_CODENAME")"
                    apt_install ca-certificates curl
                    sudo install -d -m 0755 /etc/apt/keyrings
                    curl -fsSL --retry 3 https://apt.llvm.org/llvm-snapshot.gpg.key \
                        | sudo tee /etc/apt/keyrings/apt.llvm.org.asc > /dev/null
                    echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/$codename/ llvm-toolchain-$codename-23 main" \
                        | sudo tee /etc/apt/sources.list.d/llvm-23.list > /dev/null
                    sudo apt-get -o Acquire::Retries=3 update >&2
                fi
                if [ "$toolchain" = clang23-libstdcxx ]; then
                    apt_install g++-16 clang-23
                    gcc_dir="$(dirname "$(/usr/bin/gcc-16 -print-libgcc-file-name)")"
                    printf 'CXXFLAGS=%q\n' "-stdlib=libstdc++ --gcc-install-dir=$gcc_dir"
                else
                    apt_install clang-23 libc++-23-dev libc++abi-23-dev
                    echo "CXXFLAGS='-stdlib=libc++'"
                fi
                echo 'CXX=/usr/bin/clang++-23'
                ;;
            *) echo "unknown Linux toolchain: $toolchain" >&2; exit 1 ;;
        esac
        ;;
    Darwin)
        case "$toolchain" in
            appleclang)
                # The newest installed Xcode: EasyLocal needs a libc++ with
                # std::stop_token and floating-point std::from_chars.
                xcode="$(find /Applications -maxdepth 1 -name 'Xcode*.app' | sort -V | tail -1)"
                sudo xcode-select --switch "$xcode/Contents/Developer"
                printf 'SDKROOT=%q\n' "$(xcrun --sdk macosx --show-sdk-path)"
                printf 'CXX=%q\n' "$(xcrun --find clang++)"
                echo "CXXFLAGS=''"
                ;;
            *) echo "unknown macOS toolchain: $toolchain" >&2; exit 1 ;;
        esac
        ;;
    *) echo "unsupported system: $(uname -s)" >&2; exit 1 ;;
esac
