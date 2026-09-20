# Getting Started with Betterlanguagechat

Betterlanguagechat is distributed as **two separate builds**: a **server** plugin and a
**client** mod. Both must be installed and running on the same LeviLamina baseline for
voice to work.

Common prerequisites:

- [LeviLamina](https://lamina.levimc.org/) `26.10.14` on both ends.
- Windows x64.

## 1. Install the server build

1. Set up LeviLamina on your Bedrock Dedicated Server.
2. Copy the **server** build into `plugins/voicechat/`.
3. Restart the server.

## 2. Install the client build

1. Install LeviLamina on each player's Bedrock client.
2. Copy the **client** build into `mods/voicechat/`.
3. Restart the game.

> [!IMPORTANT]
> Verify you installed the correct build on the correct side. A server plugin will not
> load in `mods/`, and a client mod will not load in `plugins/`.

## 3. Talk

Join the world and **hold V** to speak; release it to go quiet.

If you hear nothing, check:

- The microphone is available to your client and not occupied by another program.
- Both the server and client versions are compatible (same LeviLamina baseline).

## 4. Optional: enable subtitles

Subtitles require the server-side speech-to-text feature:

1. On the server, set `sttEnabled` to `true` in `config.json`.
2. Provide the streaming Zipformer files configured by `sttModel.encoderPath`, `sttModel.decoderPath`, `sttModel.joinerPath`, and `sttModel.tokensPath`.
3. On the client, keep `subtitleEnabled` set to `true`.

If the model is missing, speech-to-text is disabled without affecting voice.

## Next steps

- Tune options in `config.json`.
- Build from source: see [building](building.md).
- Review current implementation status: see [voicechat-fullscope-execution.md](voicechat-fullscope-execution.md).
- Report issues or suggest features by opening an issue.