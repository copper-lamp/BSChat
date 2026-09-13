# Third-Party Notices

Betterlanguagechat is free software released under the GNU Affero General
Public License v3.0 (see `LICENSE`). This project also depends on and/or
statically links third-party components that remain under their own licenses.
Redistribution of the project therefore must comply with each of the licenses
below, in addition to the AGPL-3.0 terms of the project itself.

## Components

| Component | Version | License | Linking | Full text |
|---|---|---|---|---|
| [Opus](https://opus-codec.org/) (libopus) | v1.5.2 | BSD 3-Clause | static | `licenses/OPUS-LICENSE.md` |
| [nlohmann/json](https://github.com/nlohmann/json) | latest | MIT | static (header-only) | `licenses/NLOHMANN-JSON-LICENSE.md` |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | — | Apache-2.0 | server build | `licenses/SHERPA-ONNX-LICENSE.md` |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | — | MIT | server build dependency | `licenses/ONNXRUNTIME-LICENSE.md` |
| [LeviLamina](https://github.com/LiteLDev/LeviLamina) | 26.10.14 | LGPL-3.0 | dynamic (host loader) | `licenses/LEVILAMINA-LICENSE.md` |

## License summary and obligations

- **Opus (BSD 3-Clause)** — must be redistributed in binary form together with
  its copyright notice, this list of conditions and the disclaimer in the
  documentation and/or other materials provided with the distribution. The
  full notice is reproduced in `licenses/OPUS-LICENSE.md`.
- **nlohmann/json (MIT)** — its copyright notice and permission notice must be
  included in all copies or substantial portions of the Software.
- **sherpa-onnx (Apache-2.0)** — preserve the license and NOTICE requirements for the
  server-side streaming speech-to-text runtime.
- **ONNX Runtime (MIT)** — preserve the copyright and permission notice when the
  server-side runtime is redistributed.
- **LeviLamina (LGPL-3.0)** — dynamically linked only; Betterlanguagechat does
  not modify or redistribute LeviLamina's own source or binaries. Users remain
  responsible for complying with LeviLamina's license and Minecraft's EULA.

## Updating this file

Whenever a dependency is added, removed, or upgraded, review the requests in
`xmake.lua`, update the table above, and ensure the corresponding full license
text is present in `licenses/`. In particular, any new **statically-linked**
dependency with a permissive-but-attributable license (BSD/MIT/Apache) must
have its notice included here.