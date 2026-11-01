# The Debian the Linux container rows run in (test/run_gc_linux.sh,
# run_wasi_linux.sh and run_gowasi_linux.sh), pinned: trixie-slim by its
# digest, and what the images add from snapshot.debian.org's archive as it was
# at one moment, which fixes every version apt can pick.  The images built
# from it are tagged with both, so that a new pin builds anew rather than
# finding the old image under the name.  Sourced, not run.

LINUX_BASE=docker.io/library/debian:trixie-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4
LINUX_SNAPSHOT=20261031T202858Z
LINUX_SECURITY_SNAPSHOT=20261031T222717Z
LINUX_TAG=trixie-${LINUX_BASE##*sha256:}
LINUX_TAG=${LINUX_TAG:0:19}-$LINUX_SNAPSHOT

# A Dockerfile for the pinned base with the Debian packages named, from the
# snapshot.  The base image's apt has no CA certificates, so the archive is
# read over http; apt checks it against the release's signature either way.
linux_dockerfile() {
  local s=http://snapshot.debian.org/archive
  cat <<EOF
FROM $LINUX_BASE
RUN rm -f /etc/apt/sources.list.d/debian.sources && \\
    { echo 'deb [check-valid-until=no] $s/debian/$LINUX_SNAPSHOT trixie main'; \\
      echo 'deb [check-valid-until=no] $s/debian/$LINUX_SNAPSHOT trixie-updates main'; \\
      echo 'deb [check-valid-until=no] $s/debian-security/$LINUX_SECURITY_SNAPSHOT trixie-security main'; \\
    } > /etc/apt/sources.list && \\
    apt-get -qq update && \\
    apt-get -qq install -y --no-install-recommends $* && \\
    rm -rf /var/lib/apt/lists/*
EOF
}
