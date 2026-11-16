# neurogenic-hil

Host-only controller for the reviewed MPC Sample USB development path.

    python3 tools/neurogenic-hil/hil.py build
    python3 tools/neurogenic-hil/hil.py test --level A
    python3 tools/neurogenic-hil/hil.py --config target/hil/config.json status

`status` and `collect` are read-only. `deploy`, `restart`, and `restore-stock`
require the exact `--confirm MPC-SAMPLE-1.3.0.12` token. The tool accepts only
root at `192.168.155.1:2222`, uses a dedicated identity and known-hosts file,
and never enrolls a host key.

The current build/deploy target is deliberately fixed to the reviewed M0.3e
artifact. Level B returns “unsupported”: the Rust test protocol exists, but no
safe in-process delivery bridge has been established.

See [HIL-0](../../docs/HIL-0.md) before any physical use.
