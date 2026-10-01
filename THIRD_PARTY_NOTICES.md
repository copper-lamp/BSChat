# Third-Party Notices

BSChat is free software released under the GNU Affero General
Public License v3.0 (see `LICENSE`). This project also depends on and/or
statically links third-party components that remain under their own licenses.
Redistribution of the project therefore must comply with each of the licenses
below, in addition to the AGPL-3.0 terms of the project itself.

## Components

| Component | Version | License | Linking | Full text |
|---|---|---|---|---|
| [Opus](https://opus-codec.org/) (libopus) | v1.5.2 | BSD 3-Clause | static | `licenses/OPUS-LICENSE.md` |
| [nlohmann/json](https://github.com/nlohmann/json) | latest | MIT | static (header-only) | `licenses/NLOHMANN-JSON-LICENSE.md` |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | 1.13.8 | Apache-2.0 | server build | `licenses/SHERPA-ONNX-LICENSE.md` |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | — | MIT | server build dependency | `licenses/ONNXRUNTIME-LICENSE.md` |
| [LeviLamina](https://github.com/LiteLDev/LeviLamina) | 26.51.6 | LGPL-3.0 | dynamic (host loader) | `licenses/LEVILAMINA-LICENSE.md` |

### Speech recognition models (optional, user-installed)

Speech-to-text model weights are **not** redistributed with BSChat.
`Install-SttModel.cmd` downloads them on demand into `stt/models/`.
Users who install a model are responsible for complying with that model's own
license; see `docs/stt-model-installer.md` for the per-model inventory.

| Model family | Upstream | License | Model type |
| streaming zipformer (zh) | [k2-fsa/icefall](https://github.com/k2-fsa/icefall) | Apache-2.0 | `transducer` |
| streaming zipformer CTC (zh) | [k2-fsa/icefall](https://github.com/k2-fsa/icefall) | Apache-2.0 | `zipformer2_ctc` |
| streaming paraformer (zh-en) | [damo/speech_paraformer_asr_nat-zh-cn-16k-common-vocab8404-online](https://modelscope.cn/models/damo/speech_paraformer_asr_nat-zh-cn-16k-common-vocab8404-online) (Alibaba) | Apache-2.0 | `paraformer` |
| streaming paraformer (zh-yue-en) | [dengcunqin/speech_paraformer-large_asr_nat-zh-cantonese-en-16k-vocab8501-online](https://modelscope.cn/models/dengcunqin/speech_paraformer-large_asr_nat-zh-cantonese-en-16k-vocab8501-online) | Apache-2.0 | `paraformer` |

Licenses above were read from each upstream's own model registry on 2026-10-01.
The ModelScope model pages for both paraformer models state "Apache License 2.0",
and the Hugging Face card for the bilingual model carries the `apache-2.0` tag.
The ONNX conversions published in `k2-fsa/sherpa-onnx` inherit these terms.

Because BSChat does not redistribute the weights, this table is informational:
it tells you what you are agreeing to when you run the installer. Anyone
redistributing a model alongside BSChat takes on the Apache-2.0 attribution
obligations themselves.

## License summary and obligations

- **Opus (BSD 3-Clause)** — must be redistributed in binary form together with
  its copyright notice, this list of conditions and the disclaimer in the
  documentation and/or other materials provided with the distribution. The
  full notice is reproduced in `licenses/OPUS-LICENSE.md`.
- **nlohmann/json (MIT)** — its copyright notice and permission notice must be
  included in all copies or substantial portions of the Software.
- **sherpa-onnx (Apache-2.0)** — preserve the license and NOTICE requirements for the
  server-side streaming speech-to-text runtime. The runtime DLLs are downloaded by
  `Install-SttModel.cmd`, not redistributed in the repository.
- **ONNX Runtime (MIT)** — preserve the copyright and permission notice when the
  server-side runtime is redistributed.
- **LeviLamina (LGPL-3.0)** — dynamically linked only; BSChat does
  not modify or redistribute LeviLamina's own source or binaries. Users remain
  responsible for complying with LeviLamina's license and Minecraft's EULA.

## Updating this file

Whenever a dependency is added, removed, or upgraded, review the requests in
`xmake.lua`, update the table above, and ensure the corresponding full license
text is present in `licenses/`. In particular, any new **statically-linked**
dependency with a permissive-but-attributable license (BSD/MIT/Apache) must
have its notice included here.