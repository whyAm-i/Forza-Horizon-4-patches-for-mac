# Preserved active game-side dependencies

These are local installed support files, not independently verified working crash patches. OnlineFix64.dll, the modified steam_api64.dll, its steam_api64.of companion and xrnm.dll match the originally bundled OnlineFix copies. OnlineFix.ini preserves this setup's game/Steam interface configuration, including AppID 1293830 and fake AppID 480. The latest local INI may contain later DLC/label edits. OnlineFix being loaded is proven; causing or preventing the world-entry crash is not proven.

The current steam_api64.dll imports OnlineFix64. Removing OnlineFix alone will introduce a dependency failure. steam_api64.of is not an alternative to rename into place. Restore only a coherent set for this exact game build. The original files' presence, build dates or matching bundled hashes do not establish compatibility with a different executable.

The game-folder steamclient64.dll / hlm.ini seen in earlier sessions no longer exist in the captured current setup. They were not included. Steam's installed steamclient64.dll is a separate dependency. Do not reintroduce the old HOODLUM client as part of this archive.

dxva2.dll and uiautomationcore.dll preserve local Wine helper copies. The local `TargetHardwareProfiler.dll.bak` disabled-profiler backup is not included in Git. ClockSkew.json, steam_settings and Enable_Invites_FH4.bat are preserved as context; they are not confirmed remedies. Do not run the invite batch file or claim working multiplayer simply because it is included.
