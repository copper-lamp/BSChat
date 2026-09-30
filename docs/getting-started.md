# Getting Started with BSChat

BSChat is distributed as **two separate builds**: a **server** plugin and a
**client** mod. Both must be installed and running on the same LeviLamina baseline for
voice to work.

Common prerequisites:

- [LeviLamina](https://lamina.levimc.org/) `26.10.14` on both ends.
- Windows x64.

## 1. Install the server build

1. Set up LeviLamina on your Bedrock Dedicated Server.
2. Copy the **server** build into `plugins/bschat/`.
3. Restart the server.

## 2. Install the client build

1. Install LeviLamina on each player's Bedrock client.
2. Copy the **client** build into `mods/bschat/`.
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

1. On the server, run the model installer and pick a tier:

   ```powershell
   .\scripts\Install-SttModel.ps1
   ```

   It downloads the sherpa-onnx runtime and the selected model into `stt/`,
   then writes the `sttModel` section of `config/bschat.json`. Use
   `-Model <id>` to install without the interactive menu. Model tiers are
   listed in [stt-model-installer](stt-model-installer.md).

2. Restart the server so the new configuration takes effect.
3. On the client, keep `subtitleEnabled` set to `true`.

If the model is missing or fails to load, speech-to-text is disabled without
affecting voice.

## Next steps

- Tune options in `config.json`.
- Build from source: see [building](building.md).
- Review current implementation status: see [bschat-fullscope-execution.md](bschat-fullscope-execution.md).
- Report issues or suggest features by opening an issue.