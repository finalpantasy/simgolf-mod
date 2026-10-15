# Install SimGolf Remastered

Takes about 5 minutes (plus the HD art download, about 2.6 GB). Windows 10 or 11.

## 1. Get your game files

You need two things. We cannot give them to you, and the installer never
downloads them.

**a) An existing SimGolf install folder, or the SimGolf disc image.**

To reuse an installed copy, choose its main folder (the folder containing
`Data`, `Sounds`, `Interface` and `jgld.dll`). Setup checks every required game
file against the original English disc's size and SHA-256 fingerprint before
copying it. A 1.03 install is usable only if its data files match. If any files
are missing or changed, setup lists them and stops; use a clean matching folder
or the disc image instead. Your old program, settings, saves and personal
snapshots are not imported. Install Remastered into a separate folder.

For the disc option, use either:
- the `.7z` from the Internet Archive:
  <https://archive.org/details/sid-meiers-sim-golf-1641547> (about 250 MB), or
- an `.iso` / `.bin` of your own SimGolf CD, made with any disc-image tool.

**Windows 10: extract the game archive first.** Some Windows versions cannot
read its LZMA compression and stop with `LZMA codec is unsupported`.

1. Open the game archive from the Internet Archive with
   [7-Zip](https://www.7-zip.org/) and extract it to a folder.
2. Find the `.iso` or `.bin` inside the extracted folder.
3. When Setup asks for the disc image, choose that extracted `.iso` or `.bin`.

These steps apply to the **game archive**. The version 1.02 no-CD ZIP is
selected separately and can stay zipped. Setup can open `.7z` or `.zip`
archives when Windows supports their compression format.

**b) The version 1.02 no-CD `golf.exe`.** This is usually a zip called
`SIMGOLF.102.ENG.MYTH.NOCD.ZIP`, from Abandonware DOS:
<https://www.abandonwaredos.com/abandonware-game.php?abandonware=Sid+Meier%27s+SimGolf&gid=2317>

It must be exactly version 1.02 with the no-CD fix. Setup checks the file's
fingerprint (SHA-1 `6f05a3ae3b5050ed82dc0010c03842a2e0ab166b`) and tells you if
it is wrong. You do not need to unzip it.

This file is required for **both** options. The remaster runs the original
1.02 program code, translated for the new build. The 1.03 program has a
different layout and cannot replace it. Choosing an installed folder skips
disc extraction, not this program check.

## 2. Download and unzip Remastered

Download `SimGolf-Remastered-v*-windows.zip` from the Releases page. Right-click
the zip, **Extract All**, and open the new folder. (Do not run Setup from
inside the zip; Windows will not let it find its files.)

## 3. Run Setup

Double-click **Setup.cmd**. If Windows says "Windows protected your PC", click
*More info*, then *Run anyway* (the program is not signed).

Setup asks you:
1. Where to install (the default is fine; it needs about 1.5 GB, plus 3 GB for HD art).
2. Whether to use an existing installed folder or a disc image, then where it is.
   On Windows 10, choose the extracted `.iso` or `.bin` for the disc option.
3. Your version 1.02 no-CD zip or `golf.exe`.
4. Whether to download the HD art pack. Say **Yes** unless you are short on space
   or on a slow connection. You can add it later: run Setup again and choose
   Yes, or download the pack yourself (below).

Setup checks your files, copies or unpacks the game, and puts a **SimGolf Remastered**
shortcut on your desktop. That is all.

For a scripted install from a folder (no dialogs, no HD download or shortcut):

```powershell
.\setup.ps1 -Dir "C:\Games\SimGolf Remastered" -GameDir "C:\Games\SimGolf" -NoCd "C:\Downloads\golf.exe" -Hd no -NoShortcut -Quiet
```

Use `-Disc "your disc"` instead of `-GameDir` for the disc option. Supply one
source, not both. The installer worker also supports
`sgsetup install --game-dir "game folder" "1.02 golf.exe" "install folder"`.
`sgsetup check-folder "game folder"` checks data without installing it.

## Updating from 0.1.5 or 0.1.6

Download the new release ZIP, unzip it fully, close the game and run Setup.cmd.
Choose your existing Remastered install folder when asked where to install it.
Your saved games and existing settings are kept. These older versions need
one manual update because their in-game updater can close before applying files.

## Add a community theme

Close the game, then run `Import-CommunityTheme.ps1` with the game's data folder
and the two compatible theme ZIPs. `GameDir` is the folder containing `Data`,
`Themes`, `Interface` and `Sounds`, which may differ from the folder containing
the launcher.

```powershell
.\Import-CommunityTheme.ps1 -GameDir "C:\Games\SimGolf Remastered\game" -StoriesZip "C:\Downloads\Stories.zip" -ProsAndCelebsZip "C:\Downloads\Pros-and-Celebs.zip" -ThemeName "Community"
```

The importer adds a new folder under `Themes\Community` and leaves existing
themes, settings and saved games intact. In the game, open **Themes**, select
**Community**, then start a new game.

## Adding the HD art later, or downloading it yourself

Go to the pack's page, <https://github.com/finalpantasy/simgolf-hd-art/releases>,
and download every file there (`parts.json` and the `simgolf-hd-art-part*.zip`
files) into one folder. Then run this in PowerShell, with your folder:

```
.\setup.ps1 -Dir "C:\path\to\your\install" -Disc "your disc" -NoCd "your no-CD zip" -Hd "C:\path\to\pack folder"
```

(Setup reinstalls the game too, which is quick. Your saved games are kept.)
Or just unzip the parts into the `art\upscaled` folder of your install, so that
`art\upscaled\index.tsv` exists.

## If something goes wrong

| What you see | What to do |
|---|---|
| "That golf.exe is not the version 1.02 no-CD file" | You have another version. Download `SIMGOLF.102.ENG.MYTH.NOCD.ZIP` as above. |
| "not the SimGolf CD" | The disc image is the wrong game or a damaged copy. Use the Internet Archive `.7z` above. |
| "missing or unreadable game file" / "changed game file" | The listed data does not match the English disc. Use a clean matching install folder or the disc image. Setup does not accept changed files by skipping their checks. |
| "choose a separate install folder" | Choose a new folder outside the existing game's folder. |
| "LZMA codec is unsupported" / Windows could not open the archive | Extract the game archive with 7-Zip, run Setup again, and choose the `.iso` or `.bin` inside. Choose the same version 1.02 no-CD ZIP separately; it can stay zipped. |
| "Not enough free space" | The HD pack needs about 6 GB free while installing. Free some space or say No to HD art. |
| A download stopped halfway | Run Setup again; it resumes. |
| "damaged (checksum mismatch)" | Delete the file named and run Setup again. |
| The game does not start | Open `golf.log` in the install folder; it says why. Make sure no other copy of the game is running. |
| The picture looks blurry or pixelated | The HD art pack is not installed (run Setup again, Yes to HD art) or Options is set to Original textures. |
| Windows SmartScreen warning | The program is unsigned. *More info*, then *Run anyway*. |

If you are stuck, open an issue on the project page and attach `golf.log`.

## Uninstall

Delete the install folder and the desktop shortcut. Nothing else was changed on your PC.
Your saved games are in `game\Saved Games` inside the install folder; copy them out first if you want to keep them.

The playable program is **Sim Golf Remastered.exe**. Setup creates the desktop shortcut for it. Your existing settings and saves are retained during updates.
