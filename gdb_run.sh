#!/usr/bin/env bash
# Run winedbg in gdb mode and prepare to run game or given program

export COMPATDIR="$HOME/.steam/steam/compatibilitytools.d/proton_tkg_10.3.r3.gf247cd5d"
export APPID="990050"
export PREFIX_DIR=""
export STEAMAPPSDIR="/mnt/NVME Storage/Steam/steamapps"

cd "$STEAMAPPSDIR/common/Death end reQuest"
DEF_CMD=("$STEAMAPPSDIR/common/Death end reQuest/resource/bin/Application.exe")

CMD=("$COMPATDIR/files/bin/wine64" winedbg --gdb --no-start --port 2159 "${@:-${DEF_CMD[@]}}")
# To connect, simply run gdb, and then run "target extended-remote tcp:localhost:12345"
# You can also get rid of --no-start and/or --port if you want.

# Check if steam-run is available, if so use that (If you're using NixOS, PLEASE install this).
if command -v steam-run &> /dev/null; then
    CMD=("steam-run" "${CMD[@]}")
fi

PATH="$COMPATDIR/files/bin/:/usr/bin:/run/current-system/sw/bin:/bin" \
    TERM="sh" \
    WINEDEBUG="-all" \
    WINEDLLPATH="$COMPATDIR/files/lib64/wine/x86_64-unix:$COMPATDIR/files/lib/wine/x86_64-unix" \
    LD_LIBRARY_PATH="$HOME/.local/share/Steam/ubuntu12_64/video/:$HOME/.local/share/Steam/ubuntu12_32/video/:$COMPATDIR/files/lib64/:$COMPATDIR/files/lib/:/usr/lib/pressure-vessel/overrides/lib/x86_64-linux-gnu/aliases:/usr/lib/pressure-vessel/overrides/lib/i386-linux-gnu/aliases" \
    WINEPREFIX="$HOME/.local/share/Steam/steamapps/compatdata/$APPID/pfx/" \
    WINEESYNC="1" \
    WINEFSYNC="1" \
    SteamGameId="$APPID" \
    SteamAppId="$APPID" \
    WINEDLLOVERRIDES="dinput8=n,b;steam.exe=b;dotnetfx35.exe=b;dotnetfx35setup.exe=b;beclient.dll=b,n;beclient_x64.dll=b,n;d3d11=n;d3d10core=n;d3d9=n;dxgi=n;d3d12=n" \
    STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.local/share/Steam" \
    WINE_LARGE_ADDRESS_AWARE="1" \
    GST_PLUGIN_SYSTEM_PATH_1_0="$COMPATDIR/files/lib64/gstreamer-1.0:$COMPATDIR/files/lib/gstreamer-1.0" \
    WINE_GST_REGISTRY_DIR="$HOME/.local/share/Steam/steamapps/compatdata/$APPID/gstreamer-1.0/" \
    MEDIACONV_AUDIO_DUMP_FILE="$STEAMAPPSDIR/shadercache/$APPID/fozmediav1/audiov2.foz" \
    MEDIACONV_AUDIO_TRANSCODED_FILE="$STEAMAPPSDIR/shadercache/$APPID/transcoded_audio.foz" \
    MEDIACONV_VIDEO_DUMP_FILE="$STEAMAPPSDIR/shadercache/$APPID/fozmediav1/video.foz" \
    MEDIACONV_VIDEO_TRANSCODED_FILE="$STEAMAPPSDIR/shadercache/$APPID/transcoded_video.foz" \
    "${CMD[@]}"
