<!-- Made by tools/docs-gen.py from devices/*/device.conf and socs/*/soc.conf. Do not edit: change that, then run tools/docs-gen.py. -->


# Devices

Every device with a repository, and how far each part of it works. A device of yours: [Port a device](../porting/index.md).

| Device | SoC | display | touch | buttons | cpu | gpu | storage | audio | battery | haptic | camera | torch | wifi | bluetooth | cellular | network |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| [QEMU virt (aarch64)](qemu.md) | `none` | yes | yes | yes |  | no | yes | yes |  |  |  |  |  |  |  | yes |
| [Redmi K20 Pro / Mi 9T Pro](raphael.md) | `sm8150` | yes | yes | yes | yes | yes | no | yes | yes | yes | no | yes | partly | no | no |  |

An empty cell is a part nobody has looked at on that device yet.

## SoC families

| SoC | What | |
|---|---|---|
| `sm8150` | Qualcomm Snapdragon 855 / 855+ (SM8150, "msmnile") | carried by raphael (`devices/raphael/soc/`) |
